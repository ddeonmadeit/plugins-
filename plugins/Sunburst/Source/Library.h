#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>

#include <map>
#include <mutex>

namespace sunburst
{
/** One recorded take: mono 16-bit PCM. */
struct Sample
{
    int frames = 0;
    float loudnessDb = 0.0f;      // attack loudness after the library's per-guitar gain
    float cents = 0.0f;           // measured tuning error; the voice plays it back in tune
    std::vector<int16_t> pcm;
};

/** All takes of one velocity layer of one sampled note. */
struct Layer
{
    float loudnessDb = 0.0f;      // median of its takes
    std::vector<Sample> takes;
};

/** One articulation (sustain, hammer-on, release): sampled notes, each with velocity layers (quiet -> loud). */
struct NoteMap
{
    std::map<int, std::vector<Layer>> notes;
    std::array<int, 128> nearest {};      // MIDI note -> nearest sampled note (-1 = none)
    float maxDb = -100.0f;

    bool empty() const noexcept { return notes.empty(); }
    const std::vector<Layer>* find (int note) const
    {
        const int n = nearest[(size_t) juce::jlimit (0, 127, note)];
        if (n < 0) return nullptr;
        auto it = notes.find (n);
        return it != notes.end() ? &it->second : nullptr;
    }
    int sampledNote (int note) const noexcept { return nearest[(size_t) juce::jlimit (0, 127, note)]; }
    void finish();
};

enum class NoiseKind { Squeak, Strum, Pick, Knock, Mute, count };

struct Noise
{
    Sample sample;
    int string = -1;              // Mute noises are per string (0 = low E)
};

/** One loaded guitar. Immutable once loaded, shared with the audio thread. */
class InstrumentData : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<InstrumentData>;

    juce::String id, name, family;
    double sampleRate = 44100.0;
    NoteMap sustain, hammer, release;
    std::array<std::vector<Noise>, (size_t) NoiseKind::count> noises;
    int lowest = 40, highest = 86;
};

struct InstrumentInfo
{
    juce::String id, name, family, credit, license, url;
    juce::Colour finish;
    juce::var sustain, release, hammer;   // arrays of {file, note, layer, loudness, cents}
};

/**
    The installed sample library (catalog.json + FLAC files). Located via $SUNBURST_LIBRARY, then
    ~/Library/Application Support/ddeonmadeit/Sunburst/Library, then the system-wide equivalent.
*/
class Library
{
public:
    static juce::File findInstalled();

    bool open (const juce::File& dir);
    bool isOpen() const noexcept { return ! instruments.empty(); }
    const std::vector<InstrumentInfo>& getInstruments() const noexcept { return instruments; }
    const InstrumentInfo* find (const juce::String& instrumentId) const;

    /** Decodes every file of a guitar plus its family's noise pool. Blocking: background thread only. */
    InstrumentData::Ptr load (const InstrumentInfo& info) const;

private:
    void loadNotes (juce::AudioFormatManager&, const juce::var& list, NoteMap& into) const;
    bool readSample (juce::AudioFormatManager&, const juce::String& file, Sample& into) const;

    juce::File directory;
    std::vector<InstrumentInfo> instruments;
    std::map<juce::String, juce::var> noisePools;   // family -> [{file, kind, string}]
};

/**
    Background loader for the current guitar. The message thread asks for an instrument id; the
    audio thread reads the installed one through an atomic raw pointer. Replaced guitars stay alive
    for 30 s so voices still playing them are safe, and every reference-count change happens here.
*/
class Loader : private juce::Thread
{
public:
    explicit Loader (const Library& lib);
    ~Loader() override;

    void request (const juce::String& instrumentId);
    bool isLoading() const noexcept { return pending.load() || busy.load(); }
    const InstrumentData* get() const noexcept { return installed.load (std::memory_order_acquire); }
    juce::String getRequested() const;
    void waitUntilIdle (int timeoutMs = 60000) const;

private:
    void run() override;

    const Library& library;
    mutable std::mutex lock;
    juce::String requested;
    std::atomic<bool> pending { false }, busy { false };
    std::atomic<const InstrumentData*> installed { nullptr };

    // Loader thread only.
    InstrumentData::Ptr current;
    std::map<juce::String, InstrumentData::Ptr> cache;
    std::vector<std::pair<juce::uint32, InstrumentData::Ptr>> retired;
};
} // namespace sunburst
