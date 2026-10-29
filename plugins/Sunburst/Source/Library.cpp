#include "Library.h"

namespace sunburst
{
void NoteMap::finish()
{
    nearest.fill (-1);
    maxDb = -100.0f;
    for (auto& [note, layers] : notes)
    {
        std::sort (layers.begin(), layers.end(), [] (const Layer& a, const Layer& b) { return a.loudnessDb < b.loudnessDb; });
        if (! layers.empty())
            maxDb = std::max (maxDb, layers.back().loudnessDb);
    }
    if (notes.empty())
        return;
    // Nearest sampled note; on a tie the one below wins (pitching up keeps the attack crisper).
    for (int n = 0; n < 128; ++n)
    {
        int best = -1, bestDist = 1000;
        for (auto& kv : notes)
        {
            const int d = std::abs (kv.first - n) * 2 + (kv.first > n ? 1 : 0);
            if (d < bestDist) { bestDist = d; best = kv.first; }
        }
        nearest[(size_t) n] = best;
    }
}

juce::File Library::findInstalled()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("SUNBURST_LIBRARY", {});
    juce::Array<juce::File> candidates;
    if (env.isNotEmpty())
        candidates.add (juce::File (env));

   #if JUCE_MAC
    candidates.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("Application Support/ddeonmadeit/Sunburst/Library"));
    candidates.add (juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                        .getChildFile ("Application Support/ddeonmadeit/Sunburst/Library"));
   #else
    candidates.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("ddeonmadeit/Sunburst/Library"));
    candidates.add (juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                        .getChildFile ("ddeonmadeit/Sunburst/Library"));
   #endif

    for (auto& c : candidates)
        if (c.getChildFile ("catalog.json").existsAsFile())
            return c;
    return {};
}

bool Library::open (const juce::File& dir)
{
    instruments.clear();
    noisePools.clear();
    directory = dir;

    juce::var root;
    if (juce::JSON::parse (dir.getChildFile ("catalog.json").loadFileAsString(), root).failed())
        return false;

    if (auto* list = root["instruments"].getArray())
        for (auto& i : *list)
        {
            InstrumentInfo info;
            info.id = i["id"].toString();
            info.name = i["name"].toString();
            info.family = i["family"].toString();
            info.credit = i["credit"].toString();
            info.license = i["license"].toString();
            info.url = i["url"].toString();
            info.finish = juce::Colour::fromString ("ff" + i["finish"].toString().fromFirstOccurrenceOf ("#", false, false));
            info.sustain = i["sus"];
            info.release = i["rel"];
            info.hammer = i["ham"];
            instruments.push_back (std::move (info));
        }
    if (auto* pools = root["noises"].getDynamicObject())
        for (auto& p : pools->getProperties())
            noisePools[p.name.toString()] = p.value;
    return ! instruments.empty();
}

const InstrumentInfo* Library::find (const juce::String& instrumentId) const
{
    for (auto& i : instruments)
        if (i.id == instrumentId)
            return &i;
    return nullptr;
}

bool Library::readSample (juce::AudioFormatManager& formats, const juce::String& file, Sample& into) const
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (directory.getChildFile (file)));
    if (reader == nullptr)
    {
        DBG ("Sunburst: could not read " << file);
        return false;
    }
    const int frames = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> buf ((int) reader->numChannels, frames);
    reader->read (&buf, 0, frames, 0, true, true);
    into.frames = frames;
    into.pcm.resize ((size_t) frames);
    const float* src = buf.getReadPointer (0);
    for (int i = 0; i < frames; ++i)
        into.pcm[(size_t) i] = (int16_t) juce::jlimit (-32767.0f, 32767.0f, std::round (src[i] * 32767.0f));
    return true;
}

void Library::loadNotes (juce::AudioFormatManager& formats, const juce::var& list, NoteMap& into) const
{
    std::map<int, std::map<int, std::vector<Sample>>> grouped;   // note -> layer -> takes
    if (auto* arr = list.getArray())
        for (auto& item : *arr)
        {
            Sample s;
            if (! readSample (formats, item["file"].toString(), s))
                continue;
            s.loudnessDb = (float) (double) item["loudness"];
            s.cents = (float) (double) item.getProperty ("cents", 0.0);
            grouped[(int) item["note"]][(int) item.getProperty ("layer", 0)].push_back (std::move (s));
        }

    for (auto& [note, layers] : grouped)
        for (auto& [layerIndex, takes] : layers)
        {
            juce::ignoreUnused (layerIndex);
            Layer layer;
            std::vector<float> l;
            for (auto& t : takes)
                l.push_back (t.loudnessDb);
            std::sort (l.begin(), l.end());
            layer.loudnessDb = l[l.size() / 2];
            layer.takes = std::move (takes);
            into.notes[note].push_back (std::move (layer));
        }
    into.finish();
}

InstrumentData::Ptr Library::load (const InstrumentInfo& info) const
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    InstrumentData::Ptr g = new InstrumentData();
    g->id = info.id;
    g->name = info.name;
    g->family = info.family;
    loadNotes (formats, info.sustain, g->sustain);
    loadNotes (formats, info.hammer, g->hammer);
    loadNotes (formats, info.release, g->release);
    if (! g->sustain.empty())
    {
        g->lowest = g->sustain.notes.begin()->first;
        g->highest = g->sustain.notes.rbegin()->first;
    }

    static const std::map<juce::String, NoiseKind> kinds { { "squeak", NoiseKind::Squeak }, { "strum", NoiseKind::Strum },
                                                           { "pick", NoiseKind::Pick }, { "knock", NoiseKind::Knock },
                                                           { "mute", NoiseKind::Mute } };
    if (auto it = noisePools.find (info.family); it != noisePools.end())
        if (auto* arr = it->second.getArray())
            for (auto& item : *arr)
            {
                auto k = kinds.find (item["kind"].toString());
                Noise n;
                if (k == kinds.end() || ! readSample (formats, item["file"].toString(), n.sample))
                    continue;
                n.string = (int) item.getProperty ("string", -1);
                g->noises[(size_t) k->second].push_back (std::move (n));
            }
    return g;
}

//==============================================================================
Loader::Loader (const Library& lib) : juce::Thread ("Sunburst loader"), library (lib)
{
    startThread (juce::Thread::Priority::low);
}

Loader::~Loader()
{
    stopThread (5000);
}

void Loader::request (const juce::String& instrumentId)
{
    {
        std::lock_guard<std::mutex> g (lock);
        if (instrumentId == requested)
            return;
        requested = instrumentId;
    }
    pending.store (true);
    notify();
}

juce::String Loader::getRequested() const
{
    std::lock_guard<std::mutex> g (lock);
    return requested;
}

void Loader::waitUntilIdle (int timeoutMs) const
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (isLoading() && juce::Time::getMillisecondCounter() < end)
        juce::Thread::sleep (5);
}

void Loader::run()
{
    while (! threadShouldExit())
    {
        if (pending.exchange (false))
        {
            busy.store (true);
            juce::String id;
            {
                std::lock_guard<std::mutex> g (lock);
                id = requested;
            }
            InstrumentData::Ptr next;
            if (auto it = cache.find (id); it != cache.end())
                next = it->second;
            else if (const auto* info = library.find (id))
                next = cache[id] = library.load (*info);

            installed.store (next.get(), std::memory_order_release);
            if (current != nullptr && current != next)
                retired.emplace_back (juce::Time::getMillisecondCounter(), current);
            current = next;
            busy.store (false);
        }

        // Free guitars nobody uses once anything that could still be ringing has ended; keep the
        // previous one cached a little longer so A/B switching is instant.
        const auto now = juce::Time::getMillisecondCounter();
        retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (auto& r) { return now - r.first > 30000; }),
                       retired.end());
        for (auto it = cache.begin(); it != cache.end();)
        {
            const bool inUse = it->second == current
                            || std::any_of (retired.begin(), retired.end(), [&] (auto& r) { return r.second == it->second; });
            it = inUse ? std::next (it) : cache.erase (it);
        }
        wait (500);
    }
}
} // namespace sunburst
