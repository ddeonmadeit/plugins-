#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <dd_core/dd_core.h>
#include <dd_dsp/dd_dsp.h>

#include "Params.h"
#include "SynthEngine.h"

class SubGritProcessor : public juce::AudioProcessor
{
public:
    SubGritProcessor();
    ~SubGritProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    dd::PresetManager presets;
    dd::ScopeBuffer scope;

    static const juce::StringArray& categoryOrder();

    /** Editor zoom, saved with the session. Message thread only. */
    float editorScale = 1.0f;

private:
    static constexpr int kChunk = 64;

    sg::VoiceParams buildVoiceParams() const;
    void processChunk (float* L, float* R, int n, double bpm);
    void updateFxSettings (double bpm);

    sg::ParamCache params;
    sg::SynthEngine engine;
    dd::Lfo lfo;

    dd::Dirt dirt;
    dd::Biquad eqSub[2], eqMid[2], eqHigh[2], subsonic[2];
    dd::Crossover fxSplit[2], sideSplit;
    dd::Chorus chorus;
    dd::StereoDelay delay;
    dd::Space space;
    dd::GlueCompressor glue;
    dd::Clipper clipper;
    dd::DcBlocker preClipDc[2], outDc[2];

    juce::SmoothedValue<float> outGainSm { 1.0f }, widthSm { 1.0f }, delayMixSm { 0.0f }, spaceMixSm { 0.0f },
                               chorusMixSm { 0.0f };

    double sr = 44100.0;
    float lastEq[4] { -999.0f, -999.0f, -999.0f, -999.0f };
    std::array<float, kChunk> lfoBuf {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubGritProcessor)
};
