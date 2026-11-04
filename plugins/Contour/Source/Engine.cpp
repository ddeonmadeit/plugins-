#include "Engine.h"

namespace contour
{
namespace
{
constexpr const char* kPluginFolder = "Contour";

double secondsOf (int64_t samples, double rate) { return rate > 0.0 ? (double) samples / rate : 0.0; }
int64_t samplesOf (double seconds, double rate) { return (int64_t) std::llround (seconds * rate); }
} // namespace

Engine::Engine (juce::AudioProcessorValueTreeState& state)
    : juce::Thread ("Contour worker"), apvts (state)
{
    params.attach (apvts);
    startThread (juce::Thread::Priority::normal);
}

Engine::~Engine()
{
    cancel.store (true);
    stopThread (10000);
}

void Engine::prepare (double sampleRate, int)
{
    const double previous = sr.exchange (sampleRate);
    if (previous > 0.0 && ! juce::approximatelyEqual (previous, sampleRate))
    {
        // Positions are in samples: until the worker has re-laid the take at the new rate, play live input.
        busy.store (true);
        rateChangedFrom.store (previous);
    }
    fadeLeft = 0;
    prepared.store (true);
    notify();
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

//==============================================================================
void Engine::process (juce::AudioBuffer<float>& buffer, juce::AudioPlayHead* playHead, bool nonRealtime) noexcept
{
    audioInBlock.store (true);
    const int n = buffer.getNumSamples(), chs = buffer.getNumChannels();
    const double rate = sr.load();

    bool playing = false;
    int64_t pos = 0;
    if (playHead != nullptr)
        if (auto info = playHead->getPosition())
        {
            if (auto t = info->getTimeInSamples())
            {
                pos = *t;
                hostPos.store (pos);
                playing = info->getIsPlaying();
                if (auto b = info->getBpm(); b.hasValue() && *b > 1.0)
                {
                    bpm.store (*b);
                    if (auto ppq = info->getPpqPosition(); ppq.hasValue() && rate > 0.0)
                        beatAtZero.store (*ppq - (double) pos / rate * *b / 60.0);
                }
            }
        }
    hostPlaying.store (playing);
    if (playing)
        internalPlay.store (false);

    bool endedHere = false;
    for (int off = 0; off < n; off += Renderer::kMaxBlock)
    {
        const int len = std::min (Renderer::kMaxBlock, n - off);
        const int64_t t = pos + off;

        if (playing && armed.load() && chs > 0)
        {
            // Transfer: record the track (mono) against the timeline; the live input plays on.
            for (int i = 0; i < len; ++i)
            {
                float m = 0.0f;
                for (int ch = 0; ch < chs; ++ch)
                    m += buffer.getReadPointer (ch)[off + i];
                mono[(size_t) i] = m / (float) chs;
            }
            if (! capturing)
            {
                capturing = true;
                capturingNow.store (true);
                busy.store (true);
            }
            fifo.push (t, mono.data(), len);
            continue;
        }
        if (capturing)
        {
            fifo.flush();
            capturing = false;
            capturingNow.store (false);
            captureEnded.store (true);
            endedHere = true;
        }

        int64_t rpos = t;
        if (! playing)
        {
            if (! internalPlay.load())
                continue;
            rpos = internalPos.load();
            const int64_t stopAt = internalStop.load();
            internalPos.store (rpos + len);
            if (stopAt >= 0 && rpos + len >= stopAt)
                internalPlay.store (false);
        }
        if (busy.load())
            continue;

        // Newest edit table; a change crossfades over 10 ms so edits during playback never click.
        if (fadeLeft <= 0)
        {
            published.hold (1, cur);
            const auto* p = published.acquire (0);
            if (cur != nullptr && p != cur)
            {
                old = cur;
                fadeLeft = fadeLen;
            }
            else
            {
                old = nullptr;
                published.release (1);
            }
            cur = p;
        }
        if (cur == nullptr)
            continue;

        renderer.render (*cur, take, rpos, len, wet.data(), cover.data());
        if (old != nullptr)
        {
            renderer.render (*old, take, rpos, len, wetOld.data(), coverOld.data());
            for (int i = 0; i < len; ++i)
            {
                const float a = juce::jlimit (0.0f, 1.0f, 1.0f - (float) (fadeLeft - i) / (float) fadeLen);
                wet[(size_t) i] = wetOld[(size_t) i] + (wet[(size_t) i] - wetOld[(size_t) i]) * a;
                cover[(size_t) i] = coverOld[(size_t) i] + (cover[(size_t) i] - coverOld[(size_t) i]) * a;
            }
            fadeLeft -= len;
            if (fadeLeft <= 0)
            {
                old = nullptr;
                published.release (1);
            }
        }
        const bool live = playing;   // stopped: the host's input is not this take, play the take alone
        for (int ch = 0; ch < chs; ++ch)
        {
            auto* d = buffer.getWritePointer (ch) + off;
            for (int i = 0; i < len; ++i)
            {
                const float c = cover[(size_t) i];
                d[i] = (live ? d[i] * (1.0f - c) : 0.0f) + wet[(size_t) i] * c;
            }
        }
    }
    audioInBlock.store (false);

    // Offline renders (exports, the QA harness) wait for the analysis so the next pass plays the edit.
    if (endedHere && nonRealtime)
        for (int waited = 0; waited < 120000 && (busy.load() || captureEnded.load()); ++waited)
            juce::Thread::sleep (1);
}

void Engine::playFrom (int64_t from, int64_t to) noexcept
{
    internalPos.store (std::max<int64_t> (0, from));
    internalStop.store (to);
    internalPlay.store (true);
}

//==============================================================================
void Engine::lockOutAudio()
{
    busy.store (true);
    for (int i = 0; i < 2000 && audioInBlock.load(); ++i)
        juce::Thread::sleep (1);
}

void Engine::run()
{
    while (! threadShouldExit())
    {
        wait (4);
        if (! prepared.load())
            continue;

        std::unique_ptr<juce::ValueTree> st;
        {
            const juce::ScopedLock sl (modelLock);
            st = std::move (pendingState);
        }
        if (st != nullptr)
        {
            rateChangedFrom.store (0.0);
            doRestore (*st);
        }
        else if (rateChangedFrom.exchange (0.0) > 0.0)
        {
            // New sample rate: save the take if it isn't yet, then lay it out again at the new rate.
            bool has = false;
            {
                const juce::ScopedLock sl (modelLock);
                has = ! spans.isEmpty();
            }
            if (has)
            {
                saveTakeIfNeeded (true);
                juce::ValueTree t;
                {
                    const juce::ScopedLock sl (modelLock);
                    t = makeStateTree (modelRate);
                }
                doRestore (t);
            }
            else
                busy.store (false);
        }

        fifo.drain ([this] (int64_t p, const float* d, int len)
        {
            take.write (p, d, len);
            captured.add ({ p, p + len });
        });
        if (captureEnded.exchange (false))
            finalisePending = true;
        if (finalisePending && ! capturingNow.load() && fifo.isEmpty())
        {
            finalisePending = false;
            finaliseCapture();
        }

        bool load = false;
        {
            const juce::ScopedLock sl (modelLock);
            load = loadRequest != juce::File();
        }
        if (load)
            doLoad();
        if (clearPending.exchange (false))
            doClear();

        const auto s = readSettings();
        if (! settingsValid || s != lastSettings)
        {
            lastSettings = s;
            settingsValid = true;
            dirty.store (true);
        }
        if (dirty.exchange (false))
            rebuild();
        if (bouncePending.exchange (false))
            doBounce();
        saveTakeIfNeeded();
        published.reclaim();
    }
}

void Engine::finaliseCapture()
{
    const double rate = sr.load();
    const auto fresh = captured;
    captured.clear();
    if (fresh.isEmpty())
    {
        busy.store (false);
        return;
    }

    std::shared_ptr<const Analysis> prev;
    std::vector<dd::Span> merged;
    {
        const juce::ScopedLock sl (modelLock);
        if (! juce::approximatelyEqual (modelRate, rate))
        {
            resetModel();
            modelRate = rate;
        }
        for (const auto& s : fresh.spans())
            spans.add (s, samplesOf (0.3, rate));
        merged = spans.spans();
        prev = analysis;
    }

    // Re-analyse every captured span the new audio touches.
    auto next = std::make_shared<Analysis>();
    next->sampleRate = rate;
    std::vector<Note> found;
    uint32_t idBase = 0;
    {
        const juce::ScopedLock sl (modelLock);
        idBase = nextId;
    }
    for (const auto& m : merged)
    {
        bool touched = false;
        for (const auto& f : fresh.spans())
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

    {
        const juce::ScopedLock sl (modelLock);
        // Notes the new audio replaced go; edited notes elsewhere stay as they are.
        auto overlapsFresh = [&fresh] (const Note& n)
        {
            for (const auto& f : fresh.spans())
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
        missing.store (false);
        message.clear();
        saveTakePending.store (true);
        version.fetch_add (1);
    }
    rebuild();
    armed.store (false);
    busy.store (false);
}

void Engine::rebuild()
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
        auto empty = std::make_unique<Playback>();
        published.publish (std::move (empty));
        const juce::ScopedLock sl (modelLock);
        curve = nullptr;
        version.fetch_add (1);
        return;
    }
    auto r = build (*a, ns, lastSettings, sp);
    published.publish (std::move (r.playback));
    const juce::ScopedLock sl (modelLock);
    curve = r.curve;
    version.fetch_add (1);
}

void Engine::resetModel()
{
    analysis = nullptr;
    notes.clear();
    spans.clear();
    curve = nullptr;
    undoStack.clear();
    redoStack.clear();
    takeFile = juce::File();
    takeFileStart = 0.0;
    saveTakePending.store (false);
}

void Engine::doLoad()
{
    juce::File file;
    int64_t at = 0;
    {
        const juce::ScopedLock sl (modelLock);
        file = loadRequest;
        at = loadAt;
        loadRequest = juce::File();
    }
    const double rate = sr.load();
    lockOutAudio();
    take.clear();
    captured.clear();
    const auto res = dd::takefiles::load (take, at, file, rate, &cancel);
    if (res.ok)
    {
        const dd::Span span { at, at + res.length };
        auto a = std::make_shared<Analysis>();
        a->sampleRate = rate;
        a->ranges.push_back (analyseRange (take, span, rate, &cancel));
        const juce::ScopedLock sl (modelLock);
        resetModel();
        modelRate = rate;
        spans.add (span);
        uint32_t id = 1;
        notes = findNotes (a->ranges.front(), id);
        nextId = id;
        analysis = a;
        message.clear();
        missing.store (false);
        saveTakePending.store (true);   // our own copy, so the project survives the original moving
        armed.store (false);
        version.fetch_add (1);
    }
    else
    {
        const juce::ScopedLock sl (modelLock);
        resetModel();
        message = res.error;
        version.fetch_add (1);
    }
    rebuild();
    busy.store (false);
}

void Engine::doClear()
{
    lockOutAudio();
    take.clear();
    captured.clear();
    {
        const juce::ScopedLock sl (modelLock);
        resetModel();
        message.clear();
        missing.store (false);
        version.fetch_add (1);
    }
    armed.store (true);
    rebuild();
    busy.store (false);
}

void Engine::doRestore (const juce::ValueTree& tree)
{
    const double rate = sr.load();
    lockOutAudio();
    take.clear();
    captured.clear();
    finalisePending = false;

    const juce::File file (tree.getProperty ("take").toString());
    const double fileStart = (double) tree.getProperty ("takeStart", 0.0);
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
    const auto sp = dd::SpanSet::fromString (tree.getProperty ("spans").toString(), rate);

    if (file == juce::File() || sp.isEmpty())
    {
        const juce::ScopedLock sl (modelLock);
        resetModel();
        modelRate = rate;
        armed.store (true);
        version.fetch_add (1);
    }
    else if (! file.existsAsFile())
    {
        const juce::ScopedLock sl (modelLock);
        resetModel();
        modelRate = rate;
        message = "Take not found: " + file.getFullPathName() + ". Press TRANSFER and play to capture again.";
        missing.store (true);
        armed.store (false);
        version.fetch_add (1);
    }
    else
    {
        const auto res = dd::takefiles::load (take, samplesOf (fileStart, rate), file, rate, &cancel);
        auto a = std::make_shared<Analysis>();
        a->sampleRate = rate;
        for (const auto& s : sp.spans())
            a->ranges.push_back (analyseRange (take, s, rate, &cancel));
        const juce::ScopedLock sl (modelLock);
        resetModel();
        modelRate = rate;
        spans = sp;
        notes = restored;
        nextId = (uint32_t) (int) tree.getProperty ("nextId", 1);
        for (const auto& n : notes)
            nextId = std::max (nextId, n.id + 1);
        analysis = a;
        takeFile = file;
        takeFileStart = fileStart;
        message = res.ok ? juce::String() : res.error;
        missing.store (! res.ok);
        armed.store (false);
        version.fetch_add (1);
    }
    dirty.store (false);
    rebuild();
    busy.store (false);
}

void Engine::saveTakeIfNeeded (bool evenIfBusy)
{
    const juce::ScopedLock save (saveLock);
    if (! saveTakePending.load() || (busy.load() && ! evenIfBusy))
        return;
    dd::Span bounds;
    double rate = 0.0;
    {
        const juce::ScopedLock sl (modelLock);
        bounds = spans.bounds();
        rate = modelRate;
    }
    if (bounds.length() <= 0 || rate <= 0.0)
    {
        saveTakePending.store (false);
        return;
    }
    const auto file = dd::takefiles::newTakeFile (kPluginFolder);
    const bool ok = dd::takefiles::save (take, bounds, rate, file);
    const juce::ScopedLock sl (modelLock);
    if (ok)
    {
        takeFile = file;
        takeFileStart = secondsOf (bounds.start, rate);
    }
    else
        message = "Couldn't save the take to " + file.getParentDirectory().getFullPathName();
    saveTakePending.store (false);
}

void Engine::doBounce()
{
    const auto* pb = published.latest();
    if (pb == nullptr || pb->spans.empty())
    {
        const juce::ScopedLock sl (modelLock);
        message = "Nothing to bounce yet: capture or drop a vocal first.";
        version.fetch_add (1);
        return;
    }
    // From the song start, so the file lines up when dropped at bar 1.
    const int64_t end = pb->spans.back().end;
    juce::AudioBuffer<float> out (1, (int) std::min<int64_t> (end, std::numeric_limits<int>::max() - 1));
    out.clear();
    Renderer r;
    std::array<float, Renderer::kMaxBlock> w {}, c {};
    for (int64_t t = pb->spans.front().start; t < end; t += Renderer::kMaxBlock)
    {
        const int len = (int) std::min<int64_t> (Renderer::kMaxBlock, end - t);
        r.render (*pb, take, t, len, w.data(), c.data());
        for (int i = 0; i < len; ++i)
            out.setSample (0, (int) (t + i), w[(size_t) i] * c[(size_t) i]);
    }
    const auto file = dd::takefiles::folder (kPluginFolder, "Bounces")
                          .getChildFile ("Contour " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S") + ".wav");
    const bool ok = dd::takefiles::writeWav (out, sr.load(), file);
    const juce::ScopedLock sl (modelLock);
    if (ok)
    {
        lastBounce = file;
        message.clear();
        bounceCount.fetch_add (1);
    }
    else
        message = "Couldn't write " + file.getFullPathName();
    version.fetch_add (1);
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
    s.sampleRate = modelRate > 0.0 ? modelRate : sr.load();
    s.version = version.load();
    return s;
}

void Engine::setNotes (std::vector<Note> ns)
{
    {
        const juce::ScopedLock sl (modelLock);
        std::sort (ns.begin(), ns.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        notes = std::move (ns);
        version.fetch_add (1);
    }
    dirty.store (true);
    notify();
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
        version.fetch_add (1);
    }
    dirty.store (true);
    notify();
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
        version.fetch_add (1);
    }
    dirty.store (true);
    notify();
    return true;
}

bool Engine::canUndo() const { const juce::ScopedLock sl (modelLock); return ! undoStack.empty(); }
bool Engine::canRedo() const { const juce::ScopedLock sl (modelLock); return ! redoStack.empty(); }

Status Engine::getStatus() const noexcept
{
    if (capturingNow.load())
        return Status::Capturing;
    if (busy.load())
        return Status::Analysing;
    if (missing.load())
        return Status::Missing;
    if (armed.load())
        return Status::Listening;
    return Status::Ready;
}

juce::String Engine::getMessage() const
{
    const juce::ScopedLock sl (modelLock);
    return message;
}

bool Engine::hasTake() const
{
    const juce::ScopedLock sl (modelLock);
    return ! spans.isEmpty();
}

void Engine::loadFile (const juce::File& file, int64_t at)
{
    {
        const juce::ScopedLock sl (modelLock);
        loadRequest = file;
        loadAt = std::max<int64_t> (0, at);
    }
    notify();
}

void Engine::clearTake()
{
    clearPending.store (true);
    notify();
}

void Engine::bounce()
{
    bouncePending.store (true);
    notify();
}

juce::File Engine::getLastBounce() const
{
    const juce::ScopedLock sl (modelLock);
    return lastBounce;
}

//==============================================================================
juce::ValueTree Engine::makeStateTree (double rate) const
{
    juce::ValueTree t ("Contour");
    if (rate <= 0.0)
        return t;
    t.setProperty ("take", takeFile.getFullPathName(), nullptr);
    t.setProperty ("takeStart", takeFileStart, nullptr);
    t.setProperty ("spans", spans.toString (rate), nullptr);
    t.setProperty ("nextId", (int) nextId, nullptr);
    for (const auto& n : notes)
    {
        juce::ValueTree c ("Note");
        c.setProperty ("id", (int) n.id, nullptr);
        c.setProperty ("s", secondsOf (n.start, rate), nullptr);
        c.setProperty ("e", secondsOf (n.end, rate), nullptr);
        c.setProperty ("c", n.centre, nullptr);
        if (n.moved)              c.setProperty ("mv", true, nullptr);
        if (n.moved)              c.setProperty ("p", n.pitch, nullptr);
        if (! juce::exactlyEqual (n.drift, 1.0f))   c.setProperty ("dr", n.drift, nullptr);
        if (! juce::exactlyEqual (n.mod, 1.0f))     c.setProperty ("md", n.mod, nullptr);
        if (! juce::exactlyEqual (n.formant, 0.0f)) c.setProperty ("fm", n.formant, nullptr);
        if (! juce::exactlyEqual (n.gainDb, 0.0f))  c.setProperty ("g", n.gainDb, nullptr);
        if (n.muted)              c.setProperty ("mu", true, nullptr);
        if (n.timeEdited())
        {
            c.setProperty ("ns", secondsOf (n.newStart, rate), nullptr);
            c.setProperty ("ne", secondsOf (n.newEnd, rate), nullptr);
        }
        t.appendChild (c, nullptr);
    }
    return t;
}

void Engine::writeState (juce::ValueTree& parent)
{
    // A take captured moments ago may not be on disk yet: write it now so the project can find it.
    saveTakeIfNeeded();
    const juce::ScopedLock sl (modelLock);
    if (pendingState != nullptr)
        parent.appendChild (pendingState->createCopy(), nullptr);   // not restored yet: keep what we were given
    else
        parent.appendChild (makeStateTree (modelRate), nullptr);
}

void Engine::readState (const juce::ValueTree& parent)
{
    const auto t = parent.getChildWithName ("Contour");
    {
        const juce::ScopedLock sl (modelLock);
        pendingState = std::make_unique<juce::ValueTree> (t.isValid() ? t.createCopy() : juce::ValueTree ("Contour"));
    }
    busy.store (true);   // live input until the take is back
    notify();
}
} // namespace contour
