#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <dd_core/dd_core.h>
#include <dd_dsp/dd_dsp.h>

#include "Params.h"

class __NAME__Processor : public juce::AudioProcessor
{
public:
    __NAME__Processor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return DD_PLUGIN_NAME; }
//@SYNTH
    bool acceptsMidi() const override  { return true; }
//@END
//@EFFECT
    bool acceptsMidi() const override  { return false; }
//@END
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

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

private:
    static constexpr int kChunk = 64;
    void processChunk (float* L, float* R, int n);

    __NS__::ParamCache params;
    double sr = 44100.0;
    juce::SmoothedValue<float> outGainSm { 1.0f };
//@SYNTH

    struct Voice
    {
        dd::BlepOscillator osc;
        dd::Adsr env;
        dd::Svf filter;
        int note = -1;
        float velocity = 0.0f;
        uint64_t age = 0;
        bool gate = false;
    };
    std::array<Voice, 8> voices;
    uint64_t stamp = 0;
    void noteOn (int note, float velocity);
    void noteOff (int note);
//@END
//@EFFECT

    dd::Dirt dirt;
//@END
    dd::Clipper clipper;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (__NAME__Processor)
};
