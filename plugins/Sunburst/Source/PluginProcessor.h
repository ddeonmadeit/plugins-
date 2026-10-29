#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <dd_core/dd_core.h>
#include <dd_dsp/dd_dsp.h>

#include "GuitarEngine.h"
#include "Library.h"
#include "Params.h"
#include "Rig.h"

class SunburstProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    SunburstProcessor();
    ~SunburstProcessor() override;

    static const juce::StringArray& categoryOrder();

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
    double getTailLengthSeconds() const override { return 4.0; }

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

    // For the editor
    const sunburst::Library& getLibrary() const noexcept { return library; }
    bool isLoading() const noexcept { return loader->isLoading(); }
    float getStringLevel (int s) const noexcept { return stringLevels[(size_t) s].load(); }
    int getStringFret (int s) const noexcept { return stringFrets[(size_t) s].load(); }
    float editorScale = 1.0f;

private:
    static constexpr int kChunk = 64;
    void parameterChanged (const juce::String& id, float value) override;
    void requestGuitar();
    void handleMidi (const juce::MidiMessage& m, const sunburst::InstrumentData* g);
    void processChunk (float* L, float* R, int n);
    sunburst::PlayParams playParams() const;

    sunburst::ParamCache params;
    sunburst::Library library;
    std::unique_ptr<sunburst::Loader> loader;
    sunburst::GuitarEngine engine;
    sunburst::Rig rig;
    sunburst::RigSettings rigSettings;
    double sr = 44100.0;
    double hostBeat = 0.0, freeBeat = 0.0, bpm = 120.0;
    float modWheel = 0.0f, bendValue = 0.0f;
    std::array<float, kChunk> dblScratch {};

    dd::Biquad sideLowCut[2];
    dd::GlueCompressor glue;
    dd::Clipper clipper;
    juce::SmoothedValue<float> outGainSm { 1.0f }, widthSm { 1.0f };
    std::array<std::atomic<float>, sunburst::GuitarEngine::kStrings> stringLevels {};
    std::array<std::atomic<int>, sunburst::GuitarEngine::kStrings> stringFrets {};
    const sunburst::InstrumentData* lastGuitar = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SunburstProcessor)
};
