#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <dd_core/dd_core.h>
#include <dd_dsp/dd_dsp.h>

#include "Engine.h"
#include "Params.h"

class AfterglowProcessor : public juce::AudioProcessor
{
public:
    AfterglowProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return DD_PLUGIN_NAME; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static const juce::StringArray& categoryOrder();

    juce::AudioProcessorValueTreeState apvts;
    dd::PresetManager presets;
    dd::ScopeBuffer scope;
    float editorScale = 1.0f;

    /** Smoothed output level and LFO 2 value for the display (audio thread writes, UI reads). */
    std::atomic<float> displayLevel { 0.0f }, displayMotion { 0.0f };

private:
    static constexpr int kChunk = ag::Voice::kBlock;

    ag::VoiceParams buildVoiceParams (float lfo2, double bpm) const;
    void updateFx (double bpm);
    void processChunk (float* L, float* R, int n, double bpm);

    ag::ParamCache params;
    ag::Engine engine;
    dd::Lfo lfo2;
    double sr = 44100.0, freeBeat = 0.0, hostBeat = 0.0;
    bool hostPlaying = false;

    dd::Tape tape;
    dd::Crusher crusher;
    dd::Ensemble ensemble;
    dd::Phaser phaser;
    dd::StereoDelay delay;
    dd::Space reverb;
    dd::OctaveShifter shimmerShift;
    dd::OnePole shimmerLp, reverbTone[2];
    float shimmerFb = 0.0f, pumpGain = 1.0f, delayBaseSamples = 24000.0f;
    double delayModPhase = 0.0;

    dd::Biquad lowCut[2], highCut[2], tilt[2];
    float lastEq[3] { -1.0f, -1.0f, -99.0f };
    dd::Svf sideLowCut[2];
    dd::GlueCompressor glue;
    dd::Clipper clipper;
    juce::SmoothedValue<float> outGainSm { 1.0f }, widthSm { 1.0f }, ensMixSm, phaserMixSm, delayMixSm, reverbMixSm, pumpDepthSm;
    float levelFollow = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterglowProcessor)
};
