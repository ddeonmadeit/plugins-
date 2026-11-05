#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <mutex>

namespace ag
{
/** The live instruments, in the order of the aInstr / bInstr choices (permanent: presets and sessions use it). */
struct LiveInstrumentDef { const char* id; const char* name; };
inline const std::array<LiveInstrumentDef, 19>& liveInstruments()
{
    static const std::array<LiveInstrumentDef, 19> defs {{
        { "strings", "String Section" }, { "pizz", "Pizzicato Strings" }, { "trem", "Tremolo Strings" }, { "violin", "Solo Violin" },
        { "grand", "Grand Piano" }, { "upright", "Upright Piano" }, { "harp", "Harp" },
        { "flute", "Flute" }, { "clarinet", "Clarinet" }, { "sax", "Tenor Sax" }, { "harmonica", "Harmonica" },
        { "horn", "French Horn" }, { "trumpet", "Trumpet" }, { "trombone", "Trombone" },
        { "organ", "Pipe Organ" }, { "vibes", "Vibraphone" }, { "marimba", "Marimba" }, { "glock", "Glockenspiel" },
        { "bells", "Tubular Bells" },
    }};
    return defs;
}

/** One recorded note at one dynamic: stereo 16-bit PCM, with a seamless loop for sustaining instruments. */
struct LiveZone
{
    int note = 60, dyn = 1, frames = 0;
    int loopStart = -1, loopEnd = -1;   // loopEnd > loopStart: jump back to loopStart on reaching loopEnd
    float cents = 0.0f;                 // tuning error of the recording; played back in tune
    std::vector<int16_t> pcm;           // interleaved L/R

    bool looped() const noexcept { return loopEnd > loopStart && loopStart >= 0; }
    float rootHz() const noexcept { return 440.0f * std::exp2 (((float) note + cents * 0.01f - 69.0f) / 12.0f); }
};

/** One loaded instrument. Immutable once loaded and shared with the audio thread. */
class LiveInstrument : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<LiveInstrument>;

    juce::String id;
    double sampleRate = 44100.0;
    std::vector<std::vector<LiveZone>> layers;   // dynamic layers, softest first; each sorted by note

    /** The recorded note nearest `note` in the dynamic layer for t (0 softest .. 1 loudest). */
    const LiveZone* find (float note, float t, int roundRobin) const noexcept;
};

/**
    The installed library (catalog.json + FLAC). Located via $AFTERGLOW_LIBRARY, then
    ~/Library/Application Support/ddeonmadeit/Afterglow/Library, then the system-wide equivalent.
*/
class LiveLibrary
{
public:
    static juce::File findInstalled();
    bool open (const juce::File& dir);
    bool isOpen() const noexcept { return ! entries.empty(); }
    bool has (const juce::String& id) const { return entries.count (id) > 0; }

    /** Decodes every zone of an instrument. Blocking: call from a background thread. */
    LiveInstrument::Ptr load (const juce::String& id) const;

private:
    juce::File directory;
    std::map<juce::String, juce::var> entries;   // id -> catalog entry
};

/**
    Background loader for the two layers' instruments. The audio thread says which instrument each layer
    wants (an index into liveInstruments(), -1 for none) through atomics, lock-free; the loader thread polls
    them, loads, and hands the result back as a raw pointer. Reference counts only change on the loader
    thread, and a replaced instrument waits in a retire pool for a while so voices still playing it are safe.
*/
class LiveLoader : private juce::Thread
{
public:
    explicit LiveLoader (const LiveLibrary& lib);
    ~LiveLoader() override;

    void want (int layer, int instrument) noexcept { wanted[(size_t) layer].store (instrument, std::memory_order_relaxed); }
    const LiveInstrument* get (int layer) const noexcept { return installed[(size_t) layer].load (std::memory_order_acquire); }
    bool isLoading (int layer) const noexcept
    {
        return wanted[(size_t) layer].load (std::memory_order_relaxed) != loaded[(size_t) layer].load (std::memory_order_acquire);
    }
    /** Offline renders: wait until both layers have what they want. */
    void waitUntilIdle (int timeoutMs = 60000) const;

private:
    void run() override;

    const LiveLibrary& library;
    std::array<std::atomic<int>, 2> wanted {}, loaded {};
    std::array<std::atomic<const LiveInstrument*>, 2> installed {};

    // Loader thread only.
    std::array<LiveInstrument::Ptr, 2> current;
    std::map<int, LiveInstrument::Ptr> cache;
    std::vector<std::pair<juce::uint32, LiveInstrument::Ptr>> retired;
};
} // namespace ag
