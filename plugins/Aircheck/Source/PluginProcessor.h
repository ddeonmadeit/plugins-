#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <dd_core/dd_core.h>
#include <dd_dsp/dd_dsp.h>

#include "Air.h"
#include "Chain.h"
#include "Params.h"
#include "Pitch.h"
#include "ProEq.h"
#include "Smooth.h"

/** Sung pitch and corrected pitch over time, written by the audio thread, read by the pitch display. */
class PitchHistory
{
public:
    static constexpr int kSize = 512;

    void push (float inMidi, float outMidi, int targetNote) noexcept
    {
        const int w = write.load (std::memory_order_relaxed);
        in[(size_t) w].store (inMidi, std::memory_order_relaxed);
        out[(size_t) w].store (outMidi, std::memory_order_relaxed);
        target[(size_t) w].store (targetNote, std::memory_order_relaxed);
        write.store ((w + 1) & (kSize - 1), std::memory_order_release);
    }

    /** k = 0 is the newest point. Unvoiced points are 0. */
    float inAt (int k) const noexcept     { return in[(size_t) idx (k)].load (std::memory_order_relaxed); }
    float outAt (int k) const noexcept    { return out[(size_t) idx (k)].load (std::memory_order_relaxed); }
    int targetAt (int k) const noexcept   { return target[(size_t) idx (k)].load (std::memory_order_relaxed); }

private:
    int idx (int k) const noexcept { return (write.load (std::memory_order_acquire) - 1 - k + kSize * 4) & (kSize - 1); }
    std::array<std::atomic<float>, kSize> in {}, out {};
    std::array<std::atomic<int>, kSize> target {};
    std::atomic<int> write { 0 };
};

class AircheckProcessor : public juce::AudioProcessor
{
public:
    AircheckProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return DD_PLUGIN_NAME; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    dd::PresetManager presets;
    dd::ScopeBuffer scope, preEq, postEq;   // output; the EQ analyzer's before / after
    PitchHistory pitchHistory;
    std::atomic<float> compGr { 0.0f }, deessGr { 0.0f }, smoothGr { 0.0f }, limitGr { 0.0f };
    std::array<std::atomic<float>, aircheck::ProEq::kBands> bandDynDb {};
    float editorScale = 1.0f;

    static const juce::StringArray& categoryOrder();

private:
    static constexpr int kChunk = aircheck::Psola::kMaxBlock;
    void processChunk (float* L, float* R, int n);
    void readSettings();
    void migrateLegacyEq (const juce::ValueTree& savedState);

    aircheck::ParamCache params;
    double sr = 44100.0, bpm = 120.0;
    juce::SmoothedValue<float> inGainSm { 1.0f }, outGainSm { 1.0f }, mixSm { 1.0f }, tuneMixSm { 0.0f };

    aircheck::PitchDetector detector;
    aircheck::Corrector corrector;
    aircheck::Psola psola;
    aircheck::Gate gate;
    aircheck::MicVoicing mic;
    aircheck::Preamp preamp;
    aircheck::ProEq proEq;
    aircheck::Rider rider;
    aircheck::Compressor comp;
    aircheck::Smooth smooth;
    aircheck::Air air;
    aircheck::Dimension dimension;
    aircheck::Limiter limiter;
    aircheck::DryAlign dryAlign;
    aircheck::DeEsser deesser;
    aircheck::Doubler doubler;
    aircheck::Vocoder vocoder;
    dd::Tape tape;
    dd::StereoDelay delay;
    dd::Space reverb;

    // Resolved once per block.
    aircheck::TuneSettings tune;
    std::array<aircheck::BandSettings, aircheck::ProEq::kBands> bands {};
    aircheck::Smooth::Settings smoothSettings;
    aircheck::Compressor::Settings compSettings;
    bool tuneOn = false, pitchActive = false;
    float lastCorrection = 0.0f, formantRatio = 1.0f;
    int64_t pitchCounter = 0;
    float duckEnv = 0.0f, gateEnv = 0.0f, revGate = 0.0f;
    int revHold = 0;
    float duckCoefA = 0.0f, duckCoefR = 0.0f;
    std::array<float, kChunk> dryL {}, dryR {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AircheckProcessor)
};
