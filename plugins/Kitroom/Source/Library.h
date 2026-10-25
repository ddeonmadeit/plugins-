#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <mutex>

#include "Kit.h"

namespace kitroom
{
/** Stem layout of every hit file: close mic(s) premixed to mono, then overheads and room pairs. */
constexpr int kStems = 5;   // close, ohL, ohR, roomL, roomR

struct Hit
{
    int frames = 0;
    float loudnessDb = 0.0f;          // measured on the default mic mix, used for velocity mapping
    std::vector<int16_t> pcm;         // kStems interleaved
};

struct ArticulationData
{
    std::vector<Hit> hits;            // sorted quietest -> loudest
    float minDb = 0.0f, maxDb = 0.0f;
};

/** One loaded sound (e.g. "DRS Snare"). Immutable once loaded, shared with the audio thread. */
class SoundData : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<SoundData>;

    juce::String id;
    double sampleRate = 44100.0;
    float closePan = 0.0f;   // where the piece sits in the overheads; the slot's Pan is an offset from this
    std::map<juce::String, ArticulationData> articulations;

    const ArticulationData* find (const juce::String& art) const
    {
        auto it = articulations.find (art);
        return it != articulations.end() && ! it->second.hits.empty() ? &it->second : nullptr;
    }
};

struct SoundInfo
{
    juce::String id, name, kit, category;
    double sampleRate = 44100.0;
    float closePan = 0.0f;
    std::map<juce::String, juce::Array<juce::var>> articulations;   // art -> [{file, loudness}]
};

struct KitInfo
{
    juce::String id, name, credit, license, url;
    juce::String finish;   // shell colour for the kit view, "#rrggbb"
};

/**
    The installed sample library (catalog.json + FLAC hits). Located via $KITROOM_LIBRARY, then
    ~/Library/Application Support/ddeonmadeit/Kitroom/Library, then the system-wide equivalent.
*/
class Library
{
public:
    static juce::File findInstalled();

    bool open (const juce::File& dir);
    bool isOpen() const noexcept { return ! sounds.empty(); }
    juce::File getDirectory() const { return directory; }

    const std::vector<SoundInfo>& getSounds() const noexcept { return sounds; }
    const std::vector<KitInfo>& getKits() const noexcept { return kits; }
    int indexOf (const juce::String& soundId) const;
    std::vector<int> soundsFor (kr::Category c) const;
    juce::String kitName (const juce::String& kitId) const;

    /** Decodes every hit of a sound. Blocking: call from a background thread. */
    SoundData::Ptr load (const SoundInfo& info) const;

private:
    juce::File directory;
    std::vector<SoundInfo> sounds;
    std::vector<KitInfo> kits;
};

/**
    Background loader. Requests come from the message thread; loaded sounds are handed to the
    audio thread as raw pointers through `installed`. Every reference-count change happens on this
    thread, and replaced sounds stay alive in a pool for a while so voices still playing them are safe.
*/
class SoundLoader : private juce::Thread
{
public:
    explicit SoundLoader (const Library& lib);
    ~SoundLoader() override;

    void request (int slot, const juce::String& soundId);
    bool isLoading (int slot) const noexcept { return pending[(size_t) slot].load() || busy[(size_t) slot].load(); }

    /** Audio thread: current sound for a slot (may be null). */
    const SoundData* get (int slot) const noexcept { return installed[(size_t) slot].load (std::memory_order_acquire); }

    /** Message thread: id of the sound currently installed or requested for a slot. */
    juce::String getRequested (int slot) const;

    /** Blocks until all requests are served (offline rendering, tests). */
    void waitUntilIdle (int timeoutMs = 60000) const;

private:
    void run() override;

    const Library& library;
    mutable std::mutex lock;
    std::array<juce::String, kr::kNumSlots> requested;
    std::array<std::atomic<bool>, kr::kNumSlots> pending {}, busy {};
    std::array<std::atomic<const SoundData*>, kr::kNumSlots> installed {};

    // Owned only by the loader thread.
    std::array<SoundData::Ptr, kr::kNumSlots> current;
    std::map<juce::String, SoundData::Ptr> cache;              // same sound in two slots loads once
    std::vector<std::pair<juce::uint32, SoundData::Ptr>> retired; // (time retired, sound)
};
} // namespace kitroom
