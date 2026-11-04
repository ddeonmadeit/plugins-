#pragma once

#include "Render.h"
#include "Params.h"

/*
    Hush's engine: dd::TakeEngine does the capture, take files, state and playback hand-over; this adds the
    sounds. After each capture the new audio is searched for sharp sounds; each gets a reduction (automatic
    from Amount, or set by hand) and every change rebuilds the gain events the audio thread plays.
*/
namespace hush
{
using Status = dd::TakeStatus;

class Engine : public dd::TakeEngine
{
public:
    explicit Engine (juce::AudioProcessorValueTreeState& state);
    ~Engine() override;

    void prepareLive (double sampleRate) { live.prepare (sampleRate); }

    struct Snapshot
    {
        std::vector<Sound> sounds;
        std::vector<dd::Span> spans;
        std::vector<Picture> pictures;
        double sampleRate = 48000.0;
        float refDb = -40.0f;
        uint64_t version = 0;
    };
    Snapshot snapshot() const;
    Settings currentSettings() const;

    void setSounds (std::vector<Sound> sounds);   // a gesture in progress
    void commit (std::vector<Sound> before);       // ends it: `before` becomes an undo step
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    float getLiveReductionDb() const noexcept { return live.getReductionDb(); }

protected:
    void resetModel() override;
    void analyseCapture (const std::vector<dd::Span>& fresh, const std::vector<dd::Span>& merged, double rate) override;
    void analyseRestored (const juce::ValueTree& state, const std::vector<dd::Span>& spans, double rate) override;
    void writeModel (juce::ValueTree& state, double rate) const override;
    bool settingsChanged() override;
    std::unique_ptr<dd::TakePlayback> buildPlayback() override;
    void renderPlayback (const dd::TakePlayback& pb, int64_t t0, int n, float* wet, float* cover, bool offline) noexcept override;
    void processLive (juce::AudioBuffer<float>& buffer, int start, int n) noexcept override;

private:
    Settings readSettings() const;
    /** Detects again at a new sensitivity, keeping hand-set reductions on the sounds they belonged to. */
    void redetect (float sensitivity);

    ParamCache params;
    Renderer audioRenderer, bounceRenderer;
    LiveDeEsser live;
    Settings lastSettings;
    bool settingsValid = false;

    // modelLock.
    std::vector<Sound> sounds;
    std::vector<Picture> pictures;
    std::vector<std::vector<Sound>> undoStack, redoStack;
    uint32_t nextId = 1;
    float detectedAt = -1.0f;             // sensitivity the sounds were found with
    std::shared_ptr<dd::TakeStore> low;   // worker only
    float lowHz = 0.0f;
    bool lowDirty = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};
} // namespace hush
