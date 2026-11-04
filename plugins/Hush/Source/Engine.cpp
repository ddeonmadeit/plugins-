#include "Engine.h"

namespace hush
{
namespace
{
double secondsOf (int64_t samples, double rate) { return rate > 0.0 ? (double) samples / rate : 0.0; }
int64_t samplesOf (double seconds, double rate) { return (int64_t) std::llround (seconds * rate); }

bool overlaps (const Sound& a, int64_t s, int64_t e) { return a.start < e && s < a.end; }
} // namespace

Engine::Engine (juce::AudioProcessorValueTreeState& state)
    : dd::TakeEngine ("Hush", "Hush")
{
    params.attach (state);
}

Engine::~Engine()
{
    stopWorker();
}

Settings Engine::readSettings() const
{
    Settings s;
    s.amount = params.get (P::amount);
    s.sensitivity = params.get (P::sensitivity);
    s.freq = params.get (P::freq);
    s.fadeMs = params.get (P::fade);
    s.mode = params.getInt (P::mode);
    s.outGain = juce::Decibels::decibelsToGain (params.get (P::outGain));
    s.listen = params.getBool (P::perfListen);
    return s;
}

Settings Engine::currentSettings() const { return readSettings(); }

bool Engine::settingsChanged()
{
    const auto s = readSettings();
    if (settingsValid && s == lastSettings)
        return false;
    const bool sensChanged = ! settingsValid || ! juce::exactlyEqual (s.sensitivity, lastSettings.sensitivity);
    lastSettings = s;
    settingsValid = true;
    if (sensChanged)
    {
        bool has = false;
        float at = -1.0f;
        {
            const juce::ScopedLock sl (modelLock);
            has = ! spans.isEmpty();
            at = detectedAt;
        }
        if (has && ! juce::exactlyEqual (at, s.sensitivity))
            redetect (s.sensitivity);
    }
    return true;
}

void Engine::resetModel()
{
    sounds.clear();
    pictures.clear();
    undoStack.clear();
    redoStack.clear();
    detectedAt = -1.0f;
    lowDirty = true;
}

void Engine::redetect (float sensitivity)
{
    std::vector<dd::Span> sp;
    double rate = 0.0;
    std::vector<Sound> oldSounds;
    uint32_t id = 1;
    {
        const juce::ScopedLock sl (modelLock);
        sp = spans.spans();
        rate = modelRate;
        oldSounds = sounds;
        id = nextId;
    }
    std::vector<Sound> found;
    std::vector<Picture> pics;
    for (const auto& s : sp)
    {
        auto a = detectSounds (take, s, rate, sensitivity, id, &cancel);
        found.insert (found.end(), a.sounds.begin(), a.sounds.end());
        pics.push_back (std::move (a.picture));
    }
    // Hand-set reductions follow the sound they were set on.
    for (auto& f : found)
        for (const auto& o : oldSounds)
            if (o.manual && overlaps (f, o.start, o.end))
            {
                f.manual = true;
                f.userDb = o.userDb;
                break;
            }
    const juce::ScopedLock sl (modelLock);
    sounds = std::move (found);
    pictures = std::move (pics);
    nextId = id;
    detectedAt = sensitivity;
    undoStack.clear();
    redoStack.clear();
    bumpVersion();
}

void Engine::analyseCapture (const std::vector<dd::Span>& fresh, const std::vector<dd::Span>& merged, double rate)
{
    const float sens = params.get (P::sensitivity);
    uint32_t id = 1;
    std::vector<Sound> oldSounds;
    {
        const juce::ScopedLock sl (modelLock);
        id = nextId;
        oldSounds = sounds;
    }
    std::vector<Sound> found;
    std::vector<Picture> pics;
    for (const auto& m : merged)
    {
        auto a = detectSounds (take, m, rate, sens, id, &cancel);
        found.insert (found.end(), a.sounds.begin(), a.sounds.end());
        pics.push_back (std::move (a.picture));
    }
    // Keep hand-set reductions outside the new audio.
    auto inFresh = [&fresh] (const Sound& s)
    {
        for (const auto& f : fresh)
            if (overlaps (s, f.start, f.end))
                return true;
        return false;
    };
    for (auto& f : found)
        if (! inFresh (f))
            for (const auto& o : oldSounds)
                if (o.manual && overlaps (f, o.start, o.end))
                {
                    f.manual = true;
                    f.userDb = o.userDb;
                    break;
                }
    const juce::ScopedLock sl (modelLock);
    sounds = std::move (found);
    pictures = std::move (pics);
    nextId = id;
    detectedAt = sens;
    undoStack.clear();
    redoStack.clear();
    lowDirty = true;
    bumpVersion();
}

void Engine::analyseRestored (const juce::ValueTree& tree, const std::vector<dd::Span>& sp, double rate)
{
    std::vector<Sound> restored;
    for (const auto& c : tree)
    {
        if (! c.hasType ("Sound"))
            continue;
        Sound s;
        s.id = (uint32_t) (int) c.getProperty ("id", 0);
        s.start = samplesOf ((double) c.getProperty ("s"), rate);
        s.end = samplesOf ((double) c.getProperty ("e"), rate);
        s.peakDb = (float) c.getProperty ("pk", -40.0);
        s.hz = (float) c.getProperty ("hz", 6000.0);
        s.kind = juce::jlimit (0, 2, (int) c.getProperty ("k", 0));
        s.manual = (bool) c.getProperty ("man", false);
        s.userDb = (float) c.getProperty ("db", 0.0);
        if (s.end > s.start)
            restored.push_back (s);
    }
    std::sort (restored.begin(), restored.end(), [] (const Sound& a, const Sound& b) { return a.start < b.start; });
    // The picture is quick to redraw from the take; the sounds (and their edits) come from the project.
    std::vector<Picture> pics;
    uint32_t scratch = 1;
    const float at = (float) tree.getProperty ("sens", params.get (P::sensitivity));
    for (const auto& s : sp)
        pics.push_back (detectSounds (take, s, rate, at, scratch, &cancel).picture);

    const juce::ScopedLock sl (modelLock);
    sounds = restored;
    pictures = std::move (pics);
    detectedAt = at;
    nextId = (uint32_t) (int) tree.getProperty ("nextId", 1);
    for (const auto& s : sounds)
        nextId = std::max (nextId, s.id + 1);
    lowDirty = true;
}

void Engine::writeModel (juce::ValueTree& t, double rate) const
{
    t.setProperty ("nextId", (int) nextId, nullptr);
    t.setProperty ("sens", detectedAt, nullptr);
    for (const auto& s : sounds)
    {
        juce::ValueTree c ("Sound");
        c.setProperty ("id", (int) s.id, nullptr);
        c.setProperty ("s", secondsOf (s.start, rate), nullptr);
        c.setProperty ("e", secondsOf (s.end, rate), nullptr);
        c.setProperty ("pk", s.peakDb, nullptr);
        c.setProperty ("hz", s.hz, nullptr);
        c.setProperty ("k", s.kind, nullptr);
        if (s.manual)
        {
            c.setProperty ("man", true, nullptr);
            c.setProperty ("db", s.userDb, nullptr);
        }
        t.appendChild (c, nullptr);
    }
}

std::unique_ptr<dd::TakePlayback> Engine::buildPlayback()
{
    std::vector<Sound> ss;
    std::vector<dd::Span> sp;
    double rate = 0.0;
    bool rebuildLow = false;
    {
        const juce::ScopedLock sl (modelLock);
        ss = sounds;
        sp = spans.spans();
        rate = modelRate;
        rebuildLow = lowDirty;
        lowDirty = false;
    }
    auto pb = std::make_unique<Playback>();
    if (sp.empty() || rate <= 0.0)
        return pb;
    const auto& s = lastSettings;
    if (rebuildLow || low == nullptr || ! juce::exactlyEqual (lowHz, s.freq))
    {
        low = lowBand (take, sp, rate, s.freq, &cancel);
        lowHz = s.freq;
    }
    pb->spans = sp;
    pb->low = low;
    pb->fade = std::max (1, (int) std::lround (s.fadeMs * 0.001 * rate));
    pb->wide = s.mode == 1;
    pb->listen = s.listen;
    pb->outGain = s.outGain;
    const float ref = referenceDb (ss);
    for (const auto& snd : ss)
    {
        const float g = juce::Decibels::decibelsToGain (reductionDb (snd, s, ref), -100.0f);
        if (g < 0.9999f || g > 1.0001f)
            pb->events.push_back ({ snd.start, snd.end, g });
    }
    return pb;
}

void Engine::renderPlayback (const dd::TakePlayback& pb, int64_t t0, int n, float* wet, float* cover, bool offline) noexcept
{
    if (const auto* p = dynamic_cast<const Playback*> (&pb))
        (offline ? bounceRenderer : audioRenderer).render (*p, take, t0, n, wet, cover);
    else
    {
        std::fill (wet, wet + n, 0.0f);
        std::fill (cover, cover + n, 0.0f);
    }
}

void Engine::processLive (juce::AudioBuffer<float>& buffer, int start, int n) noexcept
{
    live.process (buffer, start, n, readSettings());
}

//==============================================================================
Engine::Snapshot Engine::snapshot() const
{
    const juce::ScopedLock sl (modelLock);
    Snapshot s;
    s.sounds = sounds;
    s.spans = spans.spans();
    s.pictures = pictures;
    s.sampleRate = modelRate > 0.0 ? modelRate : getSampleRate();
    s.refDb = referenceDb (sounds);
    s.version = getVersion();
    return s;
}

void Engine::setSounds (std::vector<Sound> ss)
{
    {
        const juce::ScopedLock sl (modelLock);
        std::sort (ss.begin(), ss.end(), [] (const Sound& a, const Sound& b) { return a.start < b.start; });
        sounds = std::move (ss);
        bumpVersion();
    }
    markDirty();
}

void Engine::commit (std::vector<Sound> before)
{
    const juce::ScopedLock sl (modelLock);
    undoStack.push_back (std::move (before));
    if (undoStack.size() > 200)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool Engine::undo()
{
    {
        const juce::ScopedLock sl (modelLock);
        if (undoStack.empty())
            return false;
        redoStack.push_back (sounds);
        sounds = std::move (undoStack.back());
        undoStack.pop_back();
        bumpVersion();
    }
    markDirty();
    return true;
}

bool Engine::redo()
{
    {
        const juce::ScopedLock sl (modelLock);
        if (redoStack.empty())
            return false;
        undoStack.push_back (sounds);
        sounds = std::move (redoStack.back());
        redoStack.pop_back();
        bumpVersion();
    }
    markDirty();
    return true;
}

bool Engine::canUndo() const { const juce::ScopedLock sl (modelLock); return ! undoStack.empty(); }
bool Engine::canRedo() const { const juce::ScopedLock sl (modelLock); return ! redoStack.empty(); }
} // namespace hush
