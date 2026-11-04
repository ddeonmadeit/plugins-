#include "Engine.h"

namespace contour
{
namespace
{
double secondsOf (int64_t samples, double rate) { return rate > 0.0 ? (double) samples / rate : 0.0; }
int64_t samplesOf (double seconds, double rate) { return (int64_t) std::llround (seconds * rate); }
} // namespace

Engine::Engine (juce::AudioProcessorValueTreeState& state)
    : dd::TakeEngine ("Contour", "Contour")
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
    s.key = params.getInt (P::perfKey);
    s.scale = params.getInt (P::perfScale);
    s.transpose = params.getInt (P::transpose);
    s.center = params.get (P::center);
    s.drift = params.get (P::drift);
    s.vibrato = params.get (P::vibrato);
    s.transitionMs = params.get (P::transition);
    s.formant = params.get (P::formant);
    s.outGain = juce::Decibels::decibelsToGain (params.get (P::outGain));
    return s;
}

Settings Engine::currentSettings() const { return readSettings(); }

bool Engine::settingsChanged()
{
    const auto s = readSettings();
    if (settingsValid && s == lastSettings)
        return false;
    lastSettings = s;
    settingsValid = true;
    return true;
}

void Engine::resetModel()
{
    analysis = nullptr;
    notes.clear();
    curve = nullptr;
    undoStack.clear();
    redoStack.clear();
}

void Engine::analyseCapture (const std::vector<dd::Span>& fresh, const std::vector<dd::Span>& merged, double rate)
{
    std::shared_ptr<const Analysis> prev;
    uint32_t idBase = 1;
    {
        const juce::ScopedLock sl (modelLock);
        prev = analysis;
        idBase = nextId;
    }
    if (prev != nullptr && ! juce::approximatelyEqual (prev->sampleRate, rate))
        prev = nullptr;

    // Re-analyse every captured span the new audio touches; keep the rest.
    auto next = std::make_shared<Analysis>();
    next->sampleRate = rate;
    std::vector<Note> found;
    for (const auto& m : merged)
    {
        bool touched = false;
        for (const auto& f : fresh)
            touched = touched || m.overlaps (f);
        const RangeAnalysis* existing = nullptr;
        if (prev != nullptr && ! touched)
            for (const auto& r : prev->ranges)
                if (r.span.start == m.start && r.span.end == m.end)
                    existing = &r;
        if (existing != nullptr)
            next->ranges.push_back (*existing);
        else
        {
            next->ranges.push_back (analyseRange (take, m, rate, &cancel));
            auto ns = findNotes (next->ranges.back(), idBase);
            found.insert (found.end(), ns.begin(), ns.end());
        }
    }

    const juce::ScopedLock sl (modelLock);
    // Notes the new audio replaced go; edited notes elsewhere stay as they are.
    auto overlapsFresh = [&fresh] (const Note& n)
    {
        for (const auto& f : fresh)
            if (n.start < f.end && f.start < n.end)
                return true;
        return false;
    };
    std::vector<Note> kept;
    for (const auto& n : notes)
        if (! overlapsFresh (n))
            kept.push_back (n);
    for (const auto& n : found)
    {
        if (! overlapsFresh (n))
            continue;
        bool clash = false;
        for (const auto& k : kept)
            clash = clash || (n.start < k.end && k.start < n.end);
        if (! clash)
            kept.push_back (n);
    }
    std::sort (kept.begin(), kept.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    notes = std::move (kept);
    nextId = idBase;
    analysis = next;
    undoStack.clear();
    redoStack.clear();
    bumpVersion();
}

void Engine::analyseRestored (const juce::ValueTree& tree, const std::vector<dd::Span>& sp, double rate)
{
    std::vector<Note> restored;
    for (const auto& c : tree)
    {
        if (! c.hasType ("Note"))
            continue;
        Note n;
        n.id = (uint32_t) (int) c.getProperty ("id", 0);
        n.start = samplesOf ((double) c.getProperty ("s"), rate);
        n.end = samplesOf ((double) c.getProperty ("e"), rate);
        n.centre = (float) c.getProperty ("c", 60.0);
        n.moved = (bool) c.getProperty ("mv", false);
        n.pitch = (float) c.getProperty ("p", 0.0);
        n.drift = (float) c.getProperty ("dr", 1.0);
        n.mod = (float) c.getProperty ("md", 1.0);
        n.formant = (float) c.getProperty ("fm", 0.0);
        n.gainDb = (float) c.getProperty ("g", 0.0);
        n.muted = (bool) c.getProperty ("mu", false);
        n.newStart = samplesOf ((double) c.getProperty ("ns", c.getProperty ("s")), rate);
        n.newEnd = samplesOf ((double) c.getProperty ("ne", c.getProperty ("e")), rate);
        if (n.end > n.start)
            restored.push_back (n);
    }
    std::sort (restored.begin(), restored.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });

    auto a = std::make_shared<Analysis>();
    a->sampleRate = rate;
    for (const auto& s : sp)
        a->ranges.push_back (analyseRange (take, s, rate, &cancel));

    const juce::ScopedLock sl (modelLock);
    notes = restored;
    nextId = (uint32_t) (int) tree.getProperty ("nextId", 1);
    for (const auto& n : notes)
        nextId = std::max (nextId, n.id + 1);
    analysis = a;
}

void Engine::writeModel (juce::ValueTree& t, double rate) const
{
    t.setProperty ("nextId", (int) nextId, nullptr);
    for (const auto& n : notes)
    {
        juce::ValueTree c ("Note");
        c.setProperty ("id", (int) n.id, nullptr);
        c.setProperty ("s", secondsOf (n.start, rate), nullptr);
        c.setProperty ("e", secondsOf (n.end, rate), nullptr);
        c.setProperty ("c", n.centre, nullptr);
        if (n.moved)
        {
            c.setProperty ("mv", true, nullptr);
            c.setProperty ("p", n.pitch, nullptr);
        }
        if (! juce::exactlyEqual (n.drift, 1.0f))   c.setProperty ("dr", n.drift, nullptr);
        if (! juce::exactlyEqual (n.mod, 1.0f))     c.setProperty ("md", n.mod, nullptr);
        if (! juce::exactlyEqual (n.formant, 0.0f)) c.setProperty ("fm", n.formant, nullptr);
        if (! juce::exactlyEqual (n.gainDb, 0.0f))  c.setProperty ("g", n.gainDb, nullptr);
        if (n.muted)                                c.setProperty ("mu", true, nullptr);
        if (n.timeEdited())
        {
            c.setProperty ("ns", secondsOf (n.newStart, rate), nullptr);
            c.setProperty ("ne", secondsOf (n.newEnd, rate), nullptr);
        }
        t.appendChild (c, nullptr);
    }
}

std::unique_ptr<dd::TakePlayback> Engine::buildPlayback()
{
    std::shared_ptr<const Analysis> a;
    std::vector<Note> ns;
    std::vector<dd::Span> sp;
    {
        const juce::ScopedLock sl (modelLock);
        a = analysis;
        ns = notes;
        sp = spans.spans();
    }
    if (a == nullptr)
    {
        const juce::ScopedLock sl (modelLock);
        curve = nullptr;
        return std::make_unique<Playback>();
    }
    auto r = build (*a, ns, lastSettings, sp);
    const juce::ScopedLock sl (modelLock);
    curve = r.curve;
    return std::move (r.playback);
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

//==============================================================================
Engine::Snapshot Engine::snapshot() const
{
    const juce::ScopedLock sl (modelLock);
    Snapshot s;
    s.analysis = analysis;
    s.notes = notes;
    s.spans = spans.spans();
    s.curve = curve;
    s.sampleRate = modelRate > 0.0 ? modelRate : getSampleRate();
    s.version = getVersion();
    return s;
}

void Engine::setNotes (std::vector<Note> ns)
{
    {
        const juce::ScopedLock sl (modelLock);
        std::sort (ns.begin(), ns.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        notes = std::move (ns);
        bumpVersion();
    }
    markDirty();
}

void Engine::commit (std::vector<Note> before)
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
        redoStack.push_back (notes);
        notes = std::move (undoStack.back());
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
        undoStack.push_back (notes);
        notes = std::move (redoStack.back());
        redoStack.pop_back();
        bumpVersion();
    }
    markDirty();
    return true;
}

bool Engine::canUndo() const { const juce::ScopedLock sl (modelLock); return ! undoStack.empty(); }
bool Engine::canRedo() const { const juce::ScopedLock sl (modelLock); return ! redoStack.empty(); }
} // namespace contour
