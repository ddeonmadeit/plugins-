#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"

namespace contour
{
enum class Tool { Note, Vibrato, Drift, Formant, Level, Split };

/** What the user is pointing at, shared by the canvas, the inspector and the session panel. */
struct EditorState : public juce::ChangeBroadcaster
{
    std::set<uint32_t> selection;
    Tool tool = Tool::Note;
    int64_t cursor = 0;                  // where PLAY starts (timeline samples)
    double viewStart = 0.0;              // seconds
    double viewSeconds = 10.0;
    double takeStart = 0.0, takeEnd = 0.0;   // seconds, for the overview
    bool follow = true;
    juce::String hint;

    void changed() { sendChangeMessage(); }
};

/**
    The note editor: sung notes as blobs on a piano roll (thickness = loudness), the pitch line through
    them, the tools, zoom and scroll, and file drops. Paints into a cached image; the playhead and the
    rubber band go on top each frame.
*/
class NoteCanvas : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   private juce::Timer,
                   private juce::ChangeListener
{
public:
    NoteCanvas (ContourProcessor&, EditorState&);
    ~NoteCanvas() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void zoomToFit();
    void togglePlay();
    /** Applies `fn` to the selected notes (or every note when none is selected) as one undo step. */
    void editSelection (const std::function<void (Note&)>& fn, bool allIfNone = true);
    std::vector<Note> selectedNotes() const;
    const Settings& getSettings() const noexcept { return settings; }

    static constexpr int kKeysW = 46, kRulerH = 22;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { invalidate(); }
    void refresh (bool force);
    void invalidate() { cacheValid = false; repaint(); }
    void paintStatic (juce::Graphics&);
    void paintEmpty (juce::Graphics&, juce::Rectangle<float> area);
    void setHint (const juce::String& h);

    juce::Rectangle<float> noteArea() const;
    float timeToX (double samples) const;
    double xToTime (float x) const;
    float midiToY (float midi) const;
    float yToMidi (float y) const;
    double sampleRate() const noexcept { return snap.sampleRate > 0.0 ? snap.sampleRate : 48000.0; }

    int noteAt (juce::Point<float> p, bool* nearLeft = nullptr, bool* nearRight = nullptr) const;
    juce::Path blobPath (const Note& n) const;
    float displayPitch (const Note& n) const { return targetCentre (n, settings); }
    void applyNotes (std::vector<Note> next);
    void clampView();
    double gridSamples() const;
    double gridOrigin() const;

    ContourProcessor& proc;
    EditorState& state;
    Engine::Snapshot snap;
    Settings settings;
    uint64_t seenVersion = 0;

    float topMidi = 76.0f, pxPerSemi = 16.0f;
    juce::Image cache;
    bool cacheValid = false, dropHover = false, fitPending = true;
    int64_t lastPlayhead = -1;

    enum class Drag { None, Move, StretchLeft, StretchRight, Param, Rubber, Ruler };
    struct DragState
    {
        Drag kind = Drag::None;
        juce::Point<float> start;
        std::vector<Note> before;
        uint32_t anchor = 0;
        bool axisDecided = false, vertical = true, changed = false;
        std::set<uint32_t> baseSelection;
        juce::Rectangle<float> rubber;
    } drag;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteCanvas)
};

/** The whole take in one strip, with the visible window; click or drag to jump there. */
class Overview : public juce::Component, private juce::Timer
{
public:
    Overview (ContourProcessor&, EditorState&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { jump (e.position.x); }
    void mouseDrag (const juce::MouseEvent& e) override { jump (e.position.x); }

private:
    void timerCallback() override { repaint(); }
    void jump (float x);
    ContourProcessor& proc;
    EditorState& state;
};

/** Toolbar + overview + canvas + hint line. */
class NoteEditor : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    NoteEditor (ContourProcessor&, EditorState&);
    ~NoteEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    NoteCanvas& getCanvas() noexcept { return canvas; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void syncKeys();

    ContourProcessor& proc;
    EditorState& state;
    NoteCanvas canvas;
    Overview overview;
    std::vector<std::unique_ptr<dd::gui::Key>> toolKeys;
    dd::gui::Key undoKey { "Undo" }, redoKey { "Redo" }, fitKey { "Fit" }, followKey { "Follow", true };
};

/** The selected note(s): pitch, how far it moved, and each edit; RESET and SNAP act on the selection. */
class Inspector : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    Inspector (ContourProcessor&, EditorState&, NoteCanvas&);
    ~Inspector() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    ContourProcessor& proc;
    EditorState& state;
    NoteCanvas& canvas;
    dd::gui::Key resetKey { "Reset" }, snapKey { "Snap" };
};

/** Transfer status, PLAY, LOAD, BOUNCE + drag-out, CLEAR. */
class SessionView : public juce::Component, private juce::Timer
{
public:
    SessionView (ContourProcessor&, EditorState&, NoteCanvas&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class DragKey : public dd::gui::Key
    {
    public:
        DragKey (SessionView& o) : Key ("Drag Out"), owner (o) {}
        void mouseDrag (const juce::MouseEvent&) override;
    private:
        SessionView& owner;
    };

    void timerCallback() override;
    ContourProcessor& proc;
    EditorState& state;
    NoteCanvas& canvas;
    dd::gui::Key playKey { "Play" }, loadKey { "Load" }, bounceKey { "Bounce" }, clearKey { "Clear" }, keyKey { "Detect Key" };
    DragKey dragKey { *this };
    std::unique_ptr<juce::FileChooser> chooser;
    int seenBounces = 0;
    bool dragging = false;
};

/** TRANSFER in the top bar: lit while Contour is listening for the next playback. */
class TransferKey : public dd::gui::Key
{
public:
    explicit TransferKey (ContourProcessor& p);
private:
    struct Poll : juce::Timer
    {
        std::function<void()> fn;
        void timerCallback() override { fn(); }
    };
    ContourProcessor& proc;
    Poll poll;
};
} // namespace contour
