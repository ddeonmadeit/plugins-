#pragma once

namespace dd
{
enum class TakeStatus { Listening, Capturing, Analysing, Ready, Missing };

/** What the audio thread plays. Each plugin's snapshot (grain table, gain events...) derives from this. */
struct TakePlayback
{
    virtual ~TakePlayback() = default;
    std::vector<Span> spans;   // where the take replaces the live input
};

/**
    The machinery shared by plugins that capture a track and play back an edit of it (Contour, Hush):

    - Transfer: while armed and the host plays, the audio thread hands the track (mono) to a worker through
      a lock-free FIFO, stamped with timeline positions; the live input plays on. When the transport stops,
      the worker merges the new audio into the take, asks the plugin to analyse it, saves the take to disk
      and disarms, so the next playback plays the edit.
    - Follow (on by default): while the host plays, every ~21 ms frame of the track is compared with the take
      (sample-exact, or by six band envelopes when a host re-renders the clip, as Ableton's warping does).
      Audio where the take has none, or that no longer matches it for 300 ms, is captured by itself (from
      where it began, out of a 2.5 s history) and analysed while the song plays on; until then that stretch
      plays live. Where the track goes silent over the take (a muted clip) it plays live too, without
      forgetting the edits. So the edit always follows the vocal on the track, with no transfer step.
    - Playback: the audio thread renders the newest published snapshot at the host's position (or from an
      internal cursor while the host is stopped), crossfading 10 ms whenever a new one is published, and
      mixes it over the live input where the take covers the timeline. It never locks, allocates or waits,
      except in offline renders, where it waits for analysis so exports are complete.
    - Takes: saved as FLAC in Music/ddeonmadeit/<Plugin>/Takes and referenced from the project state with
      the captured spans; reopened, re-laid at a new sample rate, or reported missing.
    - Loading audio files, clearing, bouncing the edit to WAV.

    Subclasses own their model (notes, sounds...) under `modelLock` and implement the hooks below. They must
    call stopWorker() first thing in their destructor, so the worker never calls into a half-destroyed object.
*/
class TakeEngine : private juce::Thread
{
public:
    static constexpr int kMaxBlock = 512;

    /** `pluginFolder` names the folder under Music/ddeonmadeit; `stateTag` the child written into the state. */
    TakeEngine (const juce::String& pluginFolder, const juce::String& stateTag);
    ~TakeEngine() override;

    void prepare (double sampleRate, int maxBlock);
    void process (juce::AudioBuffer<float>& buffer, juce::AudioPlayHead* playHead, bool nonRealtime) noexcept;

    // Any thread.
    uint64_t getVersion() const noexcept { return version.load(); }
    void setArmed (bool shouldBeArmed) noexcept { armed.store (shouldBeArmed); }
    void setAutoFollow (bool shouldFollow) noexcept { autoFollow.store (shouldFollow); }
    bool isAutoFollow() const noexcept { return autoFollow.load(); }
    /** How many stretches the follower has captured by itself (tests, UI). */
    uint64_t getFollowCaptures() const noexcept { return autoEnded.load(); }
    bool isArmed() const noexcept { return armed.load(); }
    TakeStatus getStatus() const noexcept;
    juce::String getMessage() const;
    bool hasTake() const;
    int64_t getPlayhead() const noexcept { return internalPlay.load() ? internalPos.load() : hostPos.load(); }
    bool isPlaying() const noexcept { return hostPlaying.load() || internalPlay.load(); }
    bool isHostPlaying() const noexcept { return hostPlaying.load(); }
    bool isInternalPlaying() const noexcept { return internalPlay.load(); }
    double getBpm() const noexcept { return bpm.load(); }
    /** Beat position at timeline sample 0 (for drawing bars), valid once the host has played. */
    double getBeatAtZero() const noexcept { return beatAtZero.load(); }
    double getSampleRate() const noexcept { return sr.load(); }
    double getModelRate() const;

    /** Plays the take from `from` (to `to`, or on) while the host is stopped. */
    void playFrom (int64_t from, int64_t to = -1) noexcept;
    void stopPlaying() noexcept { internalPlay.store (false); }

    void loadFile (const juce::File& file, int64_t at);
    void clearTake();
    void bounce();
    juce::File getLastBounce() const;
    int getBounceCount() const noexcept { return bounceCount.load(); }

    // Host thread.
    void writeState (juce::ValueTree& parent);
    void readState (const juce::ValueTree& parent);

protected:
    /** Stops the worker. Call it first in the subclass destructor. */
    void stopWorker();
    /** The model changed (an edit): rebuild the playback soon. */
    void markDirty() noexcept { dirty.store (true); notify(); }
    void bumpVersion() noexcept { version.fetch_add (1); }
    void setMessage (const juce::String& m);   // takes modelLock

    //==============================================================================
    // Hooks. Worker thread unless noted. `modelLock` is NOT held unless noted.

    /** Forget the model (modelLock held). */
    virtual void resetModel() = 0;
    /** New audio landed in `fresh` spans (captured or loaded); `merged` are all captured spans now. Analyse and
        update the model (lock modelLock while touching it). */
    virtual void analyseCapture (const std::vector<Span>& fresh, const std::vector<Span>& merged, double rate) = 0;
    /** A project reopened: the take is back in `take`; read the model from `state` (written by writeModel). */
    virtual void analyseRestored (const juce::ValueTree& state, const std::vector<Span>& spans, double rate) = 0;
    /** Write the model into `state` (modelLock held). Times in seconds at `rate`. */
    virtual void writeModel (juce::ValueTree& state, double rate) const = 0;
    /** Polled every few ms: true when parameters changed and the playback needs rebuilding. */
    virtual bool settingsChanged() = 0;
    /** A fresh snapshot of the current model; an empty one (no spans) when there is nothing to play, never nullptr. */
    virtual std::unique_ptr<TakePlayback> buildPlayback() = 0;
    /** Render timeline [t0, t0 + n) of `pb` (n <= kMaxBlock): the take's output into `wet`, how much the take
        covers each sample into `cover`. Called on the audio thread (offline == false) and by the worker for
        bounces (offline == true): keep separate scratch state for the two. */
    virtual void renderPlayback (const TakePlayback& pb, int64_t t0, int n, float* wet, float* cover, bool offline) noexcept = 0;
    /** Audio thread: process the live input in place (samples [start, start + n) of `buffer`) before the take is
        mixed over it, e.g. a real-time version of the effect for audio that hasn't been captured. Capture
        always records the unprocessed input. Default: untouched. */
    virtual void processLive (juce::AudioBuffer<float>& buffer, int start, int n) noexcept { juce::ignoreUnused (buffer, start, n); }

    TakeStore take;
    mutable juce::CriticalSection modelLock;
    SpanSet spans;                  // modelLock
    double modelRate = 0.0;         // modelLock: the rate the model's sample positions are in
    std::atomic<bool> cancel { false };

private:
    static constexpr int kBands = 6;

    /** Audio-thread state of the follower (see the class comment). */
    struct Follow
    {
        enum class Kind { quiet, match, unsure, diff, muted };
        struct Stale { Span span; uint64_t serial = 0; };

        void prepare (double rate);
        void reset() noexcept;
        Kind classify() const noexcept;

        std::vector<float> hist;   // input history, indexed by timeline sample & (size - 1)
        std::array<float, kBands - 1> coef {}, lpIn {}, lpTk {};
        std::array<double, kBands> eIn {}, eTk {};
        double eErr = 0.0;
        int zIn = 0, zTk = 0;           // zero crossings of the lows (< 500 Hz): tells a re-sung pitch apart
        std::array<int, 4> zPastIn {}, zPastTk {};   // ... counted over the last four frames (~85 ms)
        int zSlot = 0;
        bool signIn = false, signTk = false;
        int frameLen = 1024, frameFill = 0, confirmFrames = 14, histLen = 0;
        int diffScore = 0, matchRun = 0, mutedRun = 0, nStale = 0;
        int64_t expected = -1, histEnd = 0, suspectStart = 0, captureStart = 0, pushedTo = 0, holdFrom = 0, limit = 0;
        bool capturing = false, holding = false;
        float gain = 0.0f, gainStep = 0.001f;
        std::array<Stale, 16> stale {};   // captured, not analysed yet: plays live
    };

    void follow (int64_t t, int len) noexcept;
    void onFrame (Follow::Kind kind, int64_t fs, int64_t fe) noexcept;
    void endAutoCapture() noexcept;
    void pushHistory (int64_t from, int64_t to) noexcept;
    bool covered (int64_t t) const noexcept;

    void run() override;
    void lockOutAudio();
    void finaliseCapture();
    void rebuild();
    void doLoad();
    void doClear();
    void doRestore (const juce::ValueTree& tree);
    void doBounce();
    void saveTakeIfNeeded (bool evenIfBusy = false);
    juce::ValueTree makeStateTree (double rate) const;   // modelLock held
    void resetAll();                                      // modelLock held

    const juce::String folder, tag;
    CaptureFifo fifo;
    Published<TakePlayback> published;

    // Audio thread.
    std::array<float, kMaxBlock> monoBuf {}, wetBuf {}, coverBuf {}, wetOldBuf {}, coverOldBuf {};
    const TakePlayback* cur = nullptr;
    const TakePlayback* old = nullptr;
    int fadeLeft = 0, fadeLen = 480;
    bool capturing = false;
    Follow fol;

    std::atomic<bool> armed { true }, busy { false }, audioInBlock { false }, captureEnded { false }, capturingNow { false };
    std::atomic<bool> prepared { false }, missing { false }, internalPlay { false }, hostPlaying { false };
    std::atomic<bool> dirty { true }, clearPending { false }, bouncePending { false }, saveTakePending { false };
    std::atomic<double> sr { 0.0 }, bpm { 120.0 }, beatAtZero { 0.0 }, rateChangedFrom { 0.0 };
    std::atomic<int64_t> hostPos { 0 }, internalPos { 0 }, internalStop { -1 };
    std::atomic<int> bounceCount { 0 };
    std::atomic<uint64_t> version { 1 };
    std::atomic<bool> autoFollow { true }, analysingNow { false };
    std::atomic<uint64_t> autoEnded { 0 }, autoAnalysed { 0 };   // follower captures ended / analysed

    // Worker.
    SpanSet captured;
    uint64_t drainedSerial = 0;   // follower captures whose audio has all been drained
    bool finalisePending = false;

    // modelLock.
    juce::CriticalSection saveLock;
    juce::File takeFile, loadRequest, lastBounce;
    double takeFileStart = 0.0;
    int64_t loadAt = 0;
    juce::String message;
    std::unique_ptr<juce::ValueTree> pendingState;

    JUCE_DECLARE_NON_COPYABLE (TakeEngine)
};
} // namespace dd
