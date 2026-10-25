#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <dd_core/dd_core.h>
#include <dd_dsp/dd_dsp.h>

#include "DrumEngine.h"
#include "Library.h"
#include "Params.h"

class KitroomProcessor : public juce::AudioProcessor
{
public:
    KitroomProcessor();
    ~KitroomProcessor() override;

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
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Kit: which sound sits in each slot (message thread).
    juce::String getSlotSound (int slot) const { return loader->getRequested (slot); }
    void setSlotSound (int slot, const juce::String& soundId);
    bool isSlotLoading (int slot) const { return loader->isLoading (slot); }
    void loadDefaultKit();

    // Keys: the primary key of each articulation; GM aliases fill in when free (message thread).
    int getKey (int slot, int art) const { return primaryKeys[(size_t) slot][(size_t) art]; }
    void setKey (int slot, int art, int note);
    void resetKeys();

    // Learn: the next note played assigns itself to (slot, art). Poll from a UI timer.
    void startLearn (int slot, int art) { learnTarget.store (kr::packTarget (slot, art)); learnedNote.store (-1); }
    void cancelLearn()                  { learnTarget.store (-1); }
    int getLearnTarget() const          { return learnTarget.load(); }
    void pollLearn();

    // Play a piece from the UI.
    void audition (int slot, int art, float velocity);

    /** Velocity of the latest hit per slot since the last call (UI flashes), 0 if none. */
    float takeHit (int slot) { return hitLevels[(size_t) slot].exchange (0.0f); }

    juce::AudioProcessorValueTreeState apvts;
    dd::PresetManager presets;
    dd::ScopeBuffer scope;
    kitroom::Library library;
    float editorScale = 1.0f;
    int selectedSlot = kr::Snare;   // UI state, message thread

    static const juce::StringArray& categoryOrder();

private:
    static constexpr int kChunk = 64;

    void rebuildNoteMap();
    void handleNote (int note, float velocity);
    void processChunk (float* L, float* R, int n);
    juce::var captureKit() const;
    void applyKit (const juce::var& kit);

    kitroom::ParamCache params;
    std::unique_ptr<kitroom::SoundLoader> loader;
    kitroom::DrumEngine engine;
    std::array<kitroom::SlotMix, kr::kNumSlots> mixes {};

    std::array<std::array<int, kr::kMaxArticulations>, kr::kNumSlots> primaryKeys {};
    std::array<std::atomic<int>, 128> noteMap {};
    std::atomic<int> learnTarget { -1 }, learnedNote { -1 };
    std::array<std::atomic<float>, kr::kNumSlots> hitLevels {};

    juce::AbstractFifo auditionFifo { 64 };
    std::array<int, 64> auditionQueue {};

    dd::Svf sideLowCut[2];   // LR4 high-pass on the side signal: lows stay mono whatever the rooms do
    dd::GlueCompressor glue;
    dd::Clipper clipper;
    juce::SmoothedValue<float> outGainSm { 1.0f }, widthSm { 1.0f };
    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitroomProcessor)
};
