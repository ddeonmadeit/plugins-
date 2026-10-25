#include "Library.h"

namespace kitroom
{
juce::File Library::findInstalled()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("KITROOM_LIBRARY", {});
    juce::Array<juce::File> candidates;
    if (env.isNotEmpty())
        candidates.add (juce::File (env));

   #if JUCE_MAC
    candidates.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("Application Support/ddeonmadeit/Kitroom/Library"));
    candidates.add (juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                        .getChildFile ("Application Support/ddeonmadeit/Kitroom/Library"));
   #else
    candidates.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("ddeonmadeit/Kitroom/Library"));
    candidates.add (juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                        .getChildFile ("ddeonmadeit/Kitroom/Library"));
   #endif

    for (auto& c : candidates)
        if (c.getChildFile ("catalog.json").existsAsFile())
            return c;
    return {};
}

bool Library::open (const juce::File& dir)
{
    sounds.clear();
    kits.clear();
    directory = dir;

    juce::var root;
    if (juce::JSON::parse (dir.getChildFile ("catalog.json").loadFileAsString(), root).failed())
        return false;

    if (auto* kitArray = root["kits"].getArray())
        for (auto& k : *kitArray)
            kits.push_back ({ k["id"].toString(), k["name"].toString(), k["credit"].toString(),
                              k["license"].toString(), k["url"].toString(), k["finish"].toString() });

    if (auto* soundArray = root["sounds"].getArray())
    {
        for (auto& s : *soundArray)
        {
            SoundInfo info;
            info.id = s["id"].toString();
            info.name = s["name"].toString();
            info.kit = s["kit"].toString();
            info.category = s["category"].toString();
            info.sampleRate = (double) s.getProperty ("sampleRate", 44100.0);
            info.closePan = (float) (double) s.getProperty ("closePan", 0.0);
            if (auto* obj = s["articulations"].getDynamicObject())
                for (auto& prop : obj->getProperties())
                    if (auto* hits = prop.value.getArray())
                        info.articulations[prop.name.toString()] = *hits;
            sounds.push_back (std::move (info));
        }
    }
    return ! sounds.empty();
}

int Library::indexOf (const juce::String& soundId) const
{
    for (size_t i = 0; i < sounds.size(); ++i)
        if (sounds[i].id == soundId)
            return (int) i;
    return -1;
}

std::vector<int> Library::soundsFor (kr::Category c) const
{
    std::vector<int> out;
    for (size_t i = 0; i < sounds.size(); ++i)
        if (sounds[i].category == kr::categoryId (c))
            out.push_back ((int) i);
    return out;
}

juce::String Library::kitName (const juce::String& kitId) const
{
    for (auto& k : kits)
        if (k.id == kitId)
            return k.name;
    return kitId;
}

SoundData::Ptr Library::load (const SoundInfo& info) const
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    SoundData::Ptr sound = new SoundData();
    sound->id = info.id;
    sound->sampleRate = info.sampleRate;
    sound->closePan = info.closePan;

    for (auto& [artName, hitList] : info.articulations)
    {
        ArticulationData art;
        for (auto& h : hitList)
        {
            const auto file = directory.getChildFile (h["file"].toString());
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            if (reader == nullptr || reader->numChannels != (unsigned int) kStems)
            {
                DBG ("Kitroom: could not read " << file.getFullPathName());
                continue;
            }

            const int frames = (int) reader->lengthInSamples;
            juce::AudioBuffer<float> buf (kStems, frames);
            reader->read (&buf, 0, frames, 0, true, true);

            Hit hit;
            hit.frames = frames;
            hit.loudnessDb = (float) (double) h["loudness"];
            hit.pcm.resize ((size_t) frames * kStems);
            for (int c = 0; c < kStems; ++c)
            {
                const float* src = buf.getReadPointer (c);
                for (int i = 0; i < frames; ++i)
                    hit.pcm[(size_t) i * kStems + (size_t) c] = (int16_t) juce::jlimit (-32767.0f, 32767.0f, std::round (src[i] * 32767.0f));
            }
            art.hits.push_back (std::move (hit));
        }

        std::sort (art.hits.begin(), art.hits.end(), [] (const Hit& a, const Hit& b) { return a.loudnessDb < b.loudnessDb; });
        if (! art.hits.empty())
        {
            art.minDb = art.hits.front().loudnessDb;
            art.maxDb = art.hits.back().loudnessDb;
            sound->articulations[artName] = std::move (art);
        }
    }
    return sound;
}

//==============================================================================
SoundLoader::SoundLoader (const Library& lib) : juce::Thread ("Kitroom loader"), library (lib)
{
    for (auto& i : installed) i.store (nullptr);
    startThread (juce::Thread::Priority::low);
}

SoundLoader::~SoundLoader()
{
    stopThread (5000);
}

void SoundLoader::request (int slot, const juce::String& soundId)
{
    {
        std::lock_guard<std::mutex> g (lock);
        requested[(size_t) slot] = soundId;
    }
    pending[(size_t) slot].store (true);
    notify();
}

juce::String SoundLoader::getRequested (int slot) const
{
    std::lock_guard<std::mutex> g (lock);
    return requested[(size_t) slot];
}

void SoundLoader::waitUntilIdle (int timeoutMs) const
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    for (;;)
    {
        bool idle = true;
        for (int s = 0; s < kr::kNumSlots; ++s)
            idle = idle && ! isLoading (s);
        if (idle || juce::Time::getMillisecondCounter() > end)
            return;
        juce::Thread::sleep (5);
    }
}

void SoundLoader::run()
{
    while (! threadShouldExit())
    {
        for (int s = 0; s < kr::kNumSlots && ! threadShouldExit(); ++s)
        {
            if (! pending[(size_t) s].exchange (false))
                continue;

            busy[(size_t) s].store (true);
            juce::String id;
            {
                std::lock_guard<std::mutex> g (lock);
                id = requested[(size_t) s];
            }

            SoundData::Ptr next;
            if (id.isNotEmpty())
            {
                auto it = cache.find (id);
                if (it != cache.end())
                    next = it->second;
                else if (const int idx = library.indexOf (id); idx >= 0)
                    next = cache[id] = library.load (library.getSounds()[(size_t) idx]);
            }

            installed[(size_t) s].store (next.get(), std::memory_order_release);
            if (current[(size_t) s] != nullptr && current[(size_t) s] != next)
                retired.emplace_back (juce::Time::getMillisecondCounter(), current[(size_t) s]);
            current[(size_t) s] = next;
            busy[(size_t) s].store (false);
        }

        // Free sounds nobody uses any more once anything that could still be ringing has ended.
        const auto now = juce::Time::getMillisecondCounter();
        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [now] (auto& r) { return now - r.first > 30000; }),
                       retired.end());
        for (auto it = cache.begin(); it != cache.end();)
        {
            const bool inUse = std::any_of (current.begin(), current.end(), [&] (auto& c) { return c == it->second; })
                            || std::any_of (retired.begin(), retired.end(), [&] (auto& r) { return r.second == it->second; });
            it = inUse ? std::next (it) : cache.erase (it);
        }

        wait (1000);
    }
}
} // namespace kitroom
