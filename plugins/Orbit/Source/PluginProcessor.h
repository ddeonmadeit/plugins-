#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <dd_core/dd_core.h>
#include <dd_dsp/dd_dsp.h>

#include "Params.h"
#include "Spatializer.h"

class OrbitProcessor : public juce::AudioProcessor
{
public:
    OrbitProcessor();

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
    double getTailLengthSeconds() const override { return 12.0; }

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
    float editorScale = 1.0f;   // the editor's zoom, kept with the session

    /** What the display shows: where the sound is heard right now (normalised), how loud it is coming
        in, how much the room is ringing, and how far through its motion cycle it is. */
    struct Telemetry
    {
        std::atomic<float> x { 0.0f }, y { 0.5f }, z { 0.4f }, input { 0.0f }, late { 0.0f };
        std::atomic<double> phase { 0.0 };
    };
    Telemetry telemetry;

private:
    static constexpr int kChunk = orbit::Spatializer::kMaxChunk;
    orbit::ParamCache params;
    orbit::Spatializer spatial;
    double sr = 44100.0;
    double freePhase = 0.0;
    float inputRms = 0.0f;
    juce::SmoothedValue<float> outGainSm { 1.0f }, mixSm { 1.0f };
    std::array<float, kChunk> wetL {}, wetR {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrbitProcessor)
};
