#include "Live.h"

namespace ag
{
const LiveZone* LiveInstrument::find (float note, float t, int roundRobin) const noexcept
{
    if (layers.empty())
        return nullptr;
    const int li = juce::jlimit (0, (int) layers.size() - 1, (int) std::lround (juce::jlimit (0.0f, 1.0f, t) * (float) (layers.size() - 1)));
    const auto& zones = layers[(size_t) li];
    if (zones.empty())
        return nullptr;

    // Nearest recorded note; among equally near ones (round robins of one note) rotate.
    float bestDist = 1.0e9f;
    for (const auto& z : zones)
        bestDist = std::min (bestDist, std::abs ((float) z.note - note));
    const LiveZone* matches[8];
    int count = 0;
    for (const auto& z : zones)
        if (std::abs ((float) z.note - note) <= bestDist + 0.01f && count < 8)
            matches[count++] = &z;
    if (count == 0)
        return nullptr;
    // Prefer the recording just below a note halfway between two (transposing up sounds less muffled).
    if (count > 1 && matches[0]->note != matches[count - 1]->note)
    {
        int below = 0;
        for (int i = 0; i < count; ++i)
            if (matches[i]->note <= note)
                below = i;
        return matches[below];
    }
    return matches[(size_t) (std::abs (roundRobin) % count)];
}

//==============================================================================
juce::File LiveLibrary::findInstalled()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("AFTERGLOW_LIBRARY", {});
    if (env.isNotEmpty() && juce::File (env).getChildFile ("catalog.json").existsAsFile())
        return juce::File (env);
    const auto rel = juce::String ("ddeonmadeit/Afterglow/Library");
    for (auto base : { juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory),
                       juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory) })
    {
        auto dir = base.getChildFile ("Application Support").getChildFile (rel);
        if (dir.getChildFile ("catalog.json").existsAsFile())
            return dir;
        dir = base.getChildFile (rel);
        if (dir.getChildFile ("catalog.json").existsAsFile())
            return dir;
    }
    return {};
}

bool LiveLibrary::open (const juce::File& dir)
{
    entries.clear();
    directory = dir;
    if (! dir.isDirectory())
        return false;
    const auto catalog = juce::JSON::parse (dir.getChildFile ("catalog.json"));
    if (auto* list = catalog["instruments"].getArray())
        for (auto& e : *list)
            entries[e["id"].toString()] = e;
    return ! entries.empty();
}

LiveInstrument::Ptr LiveLibrary::load (const juce::String& id) const
{
    auto it = entries.find (id);
    if (it == entries.end())
        return nullptr;
    const auto& entry = it->second;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    LiveInstrument::Ptr inst = new LiveInstrument();
    inst->id = id;
    inst->sampleRate = (double) entry["sampleRate"];

    std::map<int, std::vector<LiveZone>> byDyn;
    if (auto* zones = entry["zones"].getArray())
        for (auto& zv : *zones)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (directory.getChildFile (zv["file"].toString())));
            if (reader == nullptr)
                continue;
            LiveZone z;
            z.note = (int) zv["note"];
            z.dyn = (int) zv["dyn"];
            z.cents = (float) (double) zv["cents"];
            z.frames = (int) reader->lengthInSamples;
            if (zv.hasProperty ("loopStart"))
            {
                z.loopStart = (int) zv["loopStart"];
                z.loopEnd = (int) zv["loopEnd"];
            }
            juce::AudioBuffer<float> buf ((int) reader->numChannels, z.frames);
            reader->read (&buf, 0, z.frames, 0, true, true);
            z.pcm.resize ((size_t) z.frames * 2);
            for (int i = 0; i < z.frames; ++i)
                for (int c = 0; c < 2; ++c)
                {
                    const float v = buf.getSample (std::min (c, buf.getNumChannels() - 1), i);
                    z.pcm[(size_t) i * 2 + (size_t) c] = (int16_t) juce::jlimit (-32767, 32767, juce::roundToInt (v * 32767.0f));
                }
            byDyn[z.dyn].push_back (std::move (z));
        }
    for (auto& [dyn, zones] : byDyn)
    {
        std::sort (zones.begin(), zones.end(), [] (const LiveZone& a, const LiveZone& b) { return a.note < b.note; });
        inst->layers.push_back (std::move (zones));
    }
    return inst->layers.empty() ? nullptr : inst;
}

//==============================================================================
LiveLoader::LiveLoader (const LiveLibrary& lib) : juce::Thread ("Afterglow live loader"), library (lib)
{
    for (auto& w : wanted) w.store (-1);
    for (auto& l : loaded) l.store (-1);
    for (auto& i : installed) i.store (nullptr);
    startThread (juce::Thread::Priority::low);
}

LiveLoader::~LiveLoader()
{
    stopThread (4000);
}

void LiveLoader::waitUntilIdle (int timeoutMs) const
{
    const auto start = juce::Time::getMillisecondCounter();
    while ((isLoading (0) || isLoading (1)) && juce::Time::getMillisecondCounter() - start < (juce::uint32) timeoutMs)
        juce::Thread::sleep (2);
}

void LiveLoader::run()
{
    while (! threadShouldExit())
    {
        for (int l = 0; l < 2; ++l)
        {
            const int want = wanted[(size_t) l].load (std::memory_order_relaxed);
            if (want == loaded[(size_t) l].load (std::memory_order_relaxed))
                continue;
            LiveInstrument::Ptr inst;
            if (juce::isPositiveAndBelow (want, (int) liveInstruments().size()))
            {
                if (auto it = cache.find (want); it != cache.end())
                    inst = it->second;
                else if ((inst = library.load (liveInstruments()[(size_t) want].id)) != nullptr)
                    cache[want] = inst;
            }
            installed[(size_t) l].store (inst.get(), std::memory_order_release);
            if (current[(size_t) l] != nullptr)
                retired.emplace_back (juce::Time::getMillisecondCounter(), current[(size_t) l]);
            current[(size_t) l] = inst;
            loaded[(size_t) l].store (want, std::memory_order_release);
        }

        // Free instruments nothing uses any more, once voices that might still read them are long gone.
        const auto now = juce::Time::getMillisecondCounter();
        retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (auto& r) { return now - r.first > 30000; }), retired.end());
        for (auto it = cache.begin(); it != cache.end();)
        {
            const bool inUse = current[0] == it->second || current[1] == it->second
                               || std::any_of (retired.begin(), retired.end(), [&] (auto& r) { return r.second == it->second; });
            it = inUse ? std::next (it) : cache.erase (it);
        }
        wait (15);
    }
}
} // namespace ag
