#pragma once

#include "Render.h"
#include "Params.h"

/*
    Contour's engine. The audio thread captures the track while the host plays ("transfer", like
    Melodyne), then plays the edited take back in sync with the host timeline. A worker thread moves
    captured audio into the take, analyses it, rebuilds the grain table after every edit or parameter
    change, saves takes and renders bounces. The audio thread never locks, allocates or waits (except
    when the host renders offline, where it waits for analysis so exports are complete).
*/
namespace contour
{
enum class Status { Listening, Capturing, Analysing, Ready, Missing };

class Engine : private juce::Thread
{
public:
    explicit Engine (juce::AudioProcessorValueTreeState& state);
    ~Engine() override;

    void prepare (double sampleRate, int maxBlock);
    void process (juce::AudioBuffer<float>& buffer, juce::AudioPlayHead* playHead, bool nonRealtime) noexcept;

    //==============================================================================
    // Message thread.
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
    uint64_t getVersion() const noexcept { return version.load(); }
    Settings currentSettings() const;

    /** Replaces the notes (a gesture in progress: no undo step). */
    void setNotes (std::vector<Note> notes);
    /** Ends a gesture: `before` becomes an undo step. */
    void commit (std::vector<Note> before);
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

    void setArmed (bool shouldBeArmed) noexcept { armed.store (shouldBeArmed); }
    bool isArmed() const noexcept { return armed.load(); }
    Status getStatus() const noexcept;
    juce::String getMessage() const;
    bool hasTake() const;

    int64_t getPlayhead() const noexcept { return internalPlay.load() ? internalPos.load() : hostPos.load(); }
    bool isPlaying() const noexcept { return hostPlaying.load() || internalPlay.load(); }
    bool isInternalPlaying() const noexcept { return internalPlay.load(); }
    double getBpm() const noexcept { return bpm.load(); }
    /** Beat position at timeline sample 0 (so the editor can draw bars), valid once the host played. */
    double getBeatAtZero() const noexcept { return beatAtZero.load(); }
    double getSampleRate() const noexcept { return sr.load(); }

    /** Plays the take from `from` (to `to`, or on) while the host is stopped. */
    void playFrom (int64_t from, int64_t to = -1) noexcept;
    void stopPlaying() noexcept { internalPlay.store (false); }

    void loadFile (const juce::File& file, int64_t at);
    void clearTake();
    void bounce();
    juce::File getLastBounce() const;
    int getBounceCount() const noexcept { return bounceCount.load(); }

    //==============================================================================
    // State (host thread).
    void writeState (juce::ValueTree& parent);
    void readState (const juce::ValueTree& parent);

private:
    void run() override;
    Settings readSettings() const;
    void lockOutAudio();
    void finaliseCapture();
    void rebuild();
    void doLoad();
    void doClear();
    void doRestore (const juce::ValueTree& tree);
    void doBounce();
    void saveTakeIfNeeded (bool evenIfBusy = false);
    juce::ValueTree makeStateTree (double rate) const;   // caller holds modelLock
    void resetModel();                                    // caller holds modelLock

    juce::AudioProcessorValueTreeState& apvts;
    ParamCache params;

    // Shared take and hand-over structures.
    dd::TakeStore take;
    dd::CaptureFifo fifo;
    dd::Published<Playback> published;

    // Audio thread state.
    Renderer renderer;
    std::array<float, Renderer::kMaxBlock> mono {}, wet {}, cover {}, wetOld {}, coverOld {};
    const Playback* cur = nullptr;
    const Playback* old = nullptr;
    int fadeLeft = 0, fadeLen = 480;
    bool capturing = false;

    // Flags between threads.
    std::atomic<bool> armed { true }, busy { false }, audioInBlock { false }, captureEnded { false }, capturingNow { false };
    std::atomic<bool> prepared { false }, missing { false }, internalPlay { false }, hostPlaying { false };
    std::atomic<bool> dirty { true }, clearPending { false }, bouncePending { false }, saveTakePending { false };
    std::atomic<double> sr { 0.0 }, bpm { 120.0 }, beatAtZero { 0.0 }, rateChangedFrom { 0.0 };
    std::atomic<int64_t> hostPos { 0 }, internalPos { 0 }, internalStop { -1 };
    std::atomic<int> bounceCount { 0 };
    std::atomic<uint64_t> version { 1 };

    // Worker state.
    dd::SpanSet captured;
    bool finalisePending = false;
    Settings lastSettings;
    bool settingsValid = false;
    std::atomic<bool> cancel { false };

    // The model, shared by the worker and the editor (never the audio thread).
    mutable juce::CriticalSection modelLock, saveLock;
    double modelRate = 0.0;
    std::shared_ptr<const Analysis> analysis;
    std::vector<Note> notes;
    dd::SpanSet spans;
    std::shared_ptr<const DisplayCurve> curve;
    std::vector<std::vector<Note>> undoStack, redoStack;
    uint32_t nextId = 1;
    juce::File takeFile;
    double takeFileStart = 0.0;   // seconds: where the saved file begins on the timeline
    juce::String message;
    juce::File loadRequest;
    int64_t loadAt = 0;
    std::unique_ptr<juce::ValueTree> pendingState;
    juce::File lastBounce;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};
} // namespace contour
