#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"

namespace hush
{
/** What the user is pointing at, shared by the canvas, the inspector and the session panel. */
struct EditorState : public juce::ChangeBroadcaster
{
    std::set<uint32_t> selection;
    int64_t cursor = 0;
    double viewStart = 0.0, viewSeconds = 10.0;
    double takeStart = 0.0, takeEnd = 0.0;
    bool follow = true;
    juce::String hint;
    void changed() { sendChangeMessage(); }
};

/**
    The take as a waveform with every sharp sound lit where it happens (and labelled S / SH / T), and under
    it a lane of reductions: one handle per sound, dragged down to turn just that sound down.
*/
class SoundCanvas : public juce::Component,
                    public juce::FileDragAndDropTarget,
                    private juce::Timer,
                    private juce::ChangeListener
{
public:
    SoundCanvas (HushProcessor&, EditorState&);
    ~SoundCanvas() override;

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
    /** Applies `fn` to the selected sounds (or all when none are selected and allIfNone) as one undo step. */
    void editSelection (const std::function<void (Sound&)>& fn, bool allIfNone = false);
    void selectKind (int kind);
    std::vector<Sound> selectedSounds() const;
    float effectiveDb (const Sound& s) const { return reductionDb (s, settings, snap.refDb); }
    const Settings& getSettings() const noexcept { return settings; }

    static constexpr int kScaleW = 46, kRulerH = 22;
    static constexpr float kTopDb = 6.0f, kBottomDb = -30.0f;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { invalidate(); }
    void refresh (bool force);
    void invalidate() { cacheValid = false; repaint(); }
    void paintStatic (juce::Graphics&);
    void setHint (const juce::String& h);
    void applySounds (std::vector<Sound> next);
    void clampView();

    juce::Rectangle<float> waveArea() const;
    juce::Rectangle<float> laneArea() const;
    float timeToX (double samples) const;
    double xToTime (float x) const;
    float dbToY (float db) const;
    float yToDb (float y) const;
    double sampleRate() const noexcept { return snap.sampleRate > 0.0 ? snap.sampleRate : 48000.0; }
    int soundAt (juce::Point<float> p) const;

    HushProcessor& proc;
    EditorState& state;
    Engine::Snapshot snap;
    Settings settings;
    uint64_t seenVersion = 0;
    juce::Image cache;
    bool cacheValid = false, dropHover = false, fitPending = true;
    int64_t lastPlayhead = -1;

    enum class Drag { None, Level, Rubber, Ruler };
    struct DragState
    {
        Drag kind = Drag::None;
        juce::Point<float> start;
        std::vector<Sound> before;
        uint32_t anchor = 0;
        bool changed = false;
        std::set<uint32_t> baseSelection;
        juce::Rectangle<float> rubber;
    } drag;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundCanvas)
};

class Overview : public juce::Component, private juce::Timer
{
public:
    Overview (HushProcessor&, EditorState&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { jump (e.position.x); }
    void mouseDrag (const juce::MouseEvent& e) override { jump (e.position.x); }

private:
    void timerCallback() override { repaint(); }
    void jump (float x);
    HushProcessor& proc;
    EditorState& state;
};

/** Toolbar + overview + canvas + hint line. */
class SoundEditor : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    SoundEditor (HushProcessor&, EditorState&);
    ~SoundEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    SoundCanvas& getCanvas() noexcept { return canvas; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    HushProcessor& proc;
    EditorState& state;
    SoundCanvas canvas;
    Overview overview;
    dd::gui::Key sKey { "All S" }, shKey { "All SH" }, tKey { "All T" }, autoKey { "Auto" }, bypassKey { "Bypass" },
                 undoKey { "Undo" }, redoKey { "Redo" }, fitKey { "Fit" }, followKey { "Follow", true };
};

/** The selected sound(s): kind, time, level, reduction. */
class Inspector : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    Inspector (HushProcessor&, EditorState&, SoundCanvas&);
    ~Inspector() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    HushProcessor& proc;
    EditorState& state;
    SoundCanvas& canvas;
    dd::gui::Key lessKey { "-1 dB" }, moreKey { "+1 dB" };
};

/** What the take is doing (following, picking up, analysing), PLAY, LOAD, PRINT + drag-out, CLEAR. */
class SessionView : public juce::Component, private juce::Timer
{
public:
    SessionView (HushProcessor&, EditorState&, SoundCanvas&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class DragKey : public dd::gui::Key
    {
    public:
        explicit DragKey (SessionView& o) : Key ("Drag Out"), owner (o) {}
        void mouseDrag (const juce::MouseEvent&) override;
    private:
        SessionView& owner;
    };

    void timerCallback() override;
    HushProcessor& proc;
    EditorState& state;
    SoundCanvas& canvas;
    dd::gui::Key playKey { "Play" }, loadKey { "Load" }, bounceKey { "Print" }, clearKey { "Clear" };
    DragKey dragKey { *this };
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragging = false;
};

/** FOLLOW in the top bar: lit, the take follows the vocal on the track by itself (new or changed audio is
    captured as the song plays) and the levels play live in the mix. */
class FollowKey : public dd::gui::Key
{
public:
    explicit FollowKey (HushProcessor& p);
private:
    struct Poll : juce::Timer
    {
        std::function<void()> fn;
        void timerCallback() override { fn(); }
    };
    HushProcessor& proc;
    Poll poll;
};
} // namespace hush
