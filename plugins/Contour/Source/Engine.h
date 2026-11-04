#pragma once

#include "Render.h"
#include "Params.h"

/*
    Contour's engine: dd::TakeEngine does the capture, take files, state and playback hand-over; this adds
    the notes. After each capture the new audio is analysed (pitch track, pulse marks, notes); every edit or
    parameter change rebuilds the grain table that the audio thread plays.
*/
namespace contour
{
using Status = dd::TakeStatus;

class Engine : public dd::TakeEngine
{
public:
    explicit Engine (juce::AudioProcessorValueTreeState& state);
    ~Engine() override;

    struct Snapshot
    {
        std::shared_ptr<const Analysis> analysis;
        std::vector<Note> notes;
        std::vector<dd::Span> spans;
        std::shared_ptr<const DisplayCurve> curve;
        double sampleRate = 48000.0;
        uint64_t version = 0;
    };
    Snapshot snapshot() const;
    Settings currentSettings() const;

    /** Replaces the notes (a gesture in progress: no undo step). */
    void setNotes (std::vector<Note> notes);
    /** Ends a gesture: `before` becomes an undo step. */
    void commit (std::vector<Note> before);
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

protected:
    void resetModel() override;
    void analyseCapture (const std::vector<dd::Span>& fresh, const std::vector<dd::Span>& merged, double rate) override;
    void analyseRestored (const juce::ValueTree& state, const std::vector<dd::Span>& spans, double rate) override;
    void writeModel (juce::ValueTree& state, double rate) const override;
    bool settingsChanged() override;
    std::unique_ptr<dd::TakePlayback> buildPlayback() override;
    void renderPlayback (const dd::TakePlayback& pb, int64_t t0, int n, float* wet, float* cover, bool offline) noexcept override;

private:
    Settings readSettings() const;

    ParamCache params;
    Renderer audioRenderer, bounceRenderer;
    Settings lastSettings;
    bool settingsValid = false;

    // modelLock.
    std::shared_ptr<const Analysis> analysis;
    std::vector<Note> notes;
    std::shared_ptr<const DisplayCurve> curve;
    std::vector<std::vector<Note>> undoStack, redoStack;
    uint32_t nextId = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};
} // namespace contour
