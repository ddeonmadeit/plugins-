#pragma once

#include <dd_dsp/dd_dsp.h>

#include "Library.h"

namespace sunburst
{
/** Playing settings resolved once per block from the parameters and MIDI controllers. */
struct PlayParams
{
    float stringNoise = 0.7f;   // 0 = no fret squeaks, release damping, pick or slide noise at all
    float pick = 0.6f;          // 0 soft fingers .. 1 hard pick attack
    float mute = 0.0f;          // palm mute (parameter + mod wheel)
    float ring = 0.5f;          // how long a released note keeps ringing
    float strumMs = 0.0f;
    int legato = 0;             // 0 off, 1 hammer-on / pull-off, 2 slide
    float slideMs = 90.0f;
    float vibrato = 0.0f;
    float humanize = 0.3f;
    float velCurve = 0.0f;
    float bend = 0.0f;          // semitones, already scaled by the bend range
    float doubleTrack = 0.0f;
    int transpose = 0;
};

/**
    Six strings of sampled guitar. A note goes to the string a guitarist would most likely play it
    on (lowest fret, never a string that is already held), so a new note on a ringing string stops it
    like a real one, chords spread across the strings, and fast lines cut each other off naturally.

    Velocity picks the recorded layer nearest the wanted loudness and makes up the rest with gain;
    takes rotate so repeated notes never machine-gun. Legato notes on the same string become
    hammer-ons / pull-offs or slides. String noise (release damping, position-shift squeaks, slide
    scrapes, pick ticks) follows playing and is scaled by one knob, so it can be turned off entirely.

    Two tracks render separately: track 0 is the take, track 1 its double (another take, a few ms
    late and a few cents off) that the rig pans opposite for a double-tracked stereo guitar.
*/
class GuitarEngine
{
public:
    static constexpr int kStrings = 6;
    static constexpr int kVoices = 64;

    void prepare (double sampleRate);
    void reset();

    void noteOn (int note, float velocity, const InstrumentData* g, const PlayParams& p);
    void noteOff (int note, const InstrumentData* g, const PlayParams& p);
    void setSustain (bool down, const InstrumentData* g, const PlayParams& p);
    void allNotesOff();
    void fxKey (int key, float velocity, const InstrumentData* g);   // muted strum, knock, mute, squeak, pick

    /** Renders `n` samples of both tracks (added into main / dbl). */
    void render (float* main, float* dbl, int n, const InstrumentData* g, const PlayParams& p) noexcept;

    /** 0..1 activity per string, for the UI. */
    float stringLevel (int s) const noexcept { return levels[(size_t) s]; }
    int stringFret (int s) const noexcept { return frets[(size_t) s]; }

    static constexpr int kFxLow = 24, kFxHigh = 28;   // MIDI keys below the guitar for noise one-shots

private:
    enum class Kind { Note, Noise, Squeak, Scrape };

    struct Voice
    {
        bool active = false, gate = false;
        Kind kind = Kind::Note;
        int track = 0, string = -1, note = -1;
        const Sample* sample = nullptr;
        double pos = 0.0;
        float pitch = 0.0f, targetPitch = 0.0f, glideCoef = 0.0f;   // semitones relative to the sample
        float gain = 1.0f, env = 1.0f, releaseCoef = 1.0f, fadeIn = 1.0f, fadeInc = 1.0f;
        float muteCoef = 1.0f, boost = 0.0f, boostCoef = 0.0f;
        bool releasing = false, stopping = false;
        uint32_t age = 0, heldSamples = 0;
        float vibPhase = 0.0f, detune = 0.0f;
        dd::OnePole lp1, lp2;
        float lpHz = 20000.0f, curHz = -1.0f;
        // Synthesized noises (squeak / scrape)
        dd::Svf band;
        float bandHz = 1500.0f, bandTarget = 1500.0f, bandCoef = 0.0f;
        int length = 0, elapsed = 0;
    };

    struct Queued { int note; float velocity; int64_t startAt; };

    void startNote (int note, float velocity, const InstrumentData* g, const PlayParams& p);
    void startTake (int track, int string, int note, float velocity, const InstrumentData* g, const PlayParams& p,
                    bool legato, int delaySamples);
    int chooseString (int note, int avoidHeld) const;
    void stopString (int track, int string, float fadeMs);
    void releaseVoice (Voice& v, const InstrumentData* g, const PlayParams& p, bool damp);
    void playNoise (const Sample* s, float gain, int delaySamples = 0);
    void playSqueak (float gain, int string);
    void playScrape (float gain, float fromNote, float toNote, float seconds);
    const Sample* pickTake (const std::vector<Layer>& layers, float velocity, const PlayParams& p, float& gainDb, int key);
    Voice* allocate();
    void renderVoice (Voice& v, float* out, int n, double srcRate, const PlayParams& p) noexcept;

    double sr = 44100.0;
    std::array<Voice, kVoices> voices {};
    std::array<Queued, 32> queue {};
    int queued = 0, groupStarted = 0;
    int64_t clock = 0, groupStart = -(1 << 30);
    bool groupDown = true;

    std::array<int, kStrings> stringNote {};          // note held on each string (-1 = none)
    std::array<int, kStrings> lastFret {};
    std::array<int64_t, kStrings> lastRelease {};
    std::array<float, kStrings> levels {};
    std::array<int, kStrings> frets {};
    std::array<int, 128> noteString {};               // which string a held note is on
    std::array<bool, 128> sustained {};
    bool sustainDown = false;
    int lastNote = -1;
    int64_t lastNoteTime = 0;
    std::map<int, int> rr;                            // round robin per (note, layer) key
    uint32_t ageCounter = 0;
    dd::Rng rng;
    juce::Random random;

    // Delayed takes (doubles, humanized timing) wait here until their sample offset.
    struct Pending { int track, string, note; float velocity; bool legato; int64_t at; };
    std::array<Pending, 32> pending {};
    int numPending = 0;
};
} // namespace sunburst
