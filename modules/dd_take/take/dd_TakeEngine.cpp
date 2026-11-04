namespace dd
{
namespace
{
double secondsOf (int64_t samples, double rate) { return rate > 0.0 ? (double) samples / rate : 0.0; }
int64_t samplesOf (double seconds, double rate) { return (int64_t) std::llround (seconds * rate); }
} // namespace

TakeEngine::TakeEngine (const juce::String& pluginFolder, const juce::String& stateTag)
    : juce::Thread (pluginFolder + " worker"), folder (pluginFolder), tag (stateTag)
{
    startThread (juce::Thread::Priority::normal);
}

TakeEngine::~TakeEngine()
{
    jassert (! isThreadRunning());   // subclasses call stopWorker() in their destructor
    stopWorker();
}

void TakeEngine::stopWorker()
{
    cancel.store (true);
    stopThread (10000);
}

void TakeEngine::prepare (double sampleRate, int)
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

//==============================================================================
void TakeEngine::process (juce::AudioBuffer<float>& buffer, juce::AudioPlayHead* playHead, bool nonRealtime) noexcept
{
    audioInBlock.store (true);
    const int n = buffer.getNumSamples(), chs = buffer.getNumChannels();
    const double rate = sr.load();

    bool playing = false;
    int64_t pos = 0;
    if (playHead != nullptr)
        if (auto info = playHead->getPosition())
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
    hostPlaying.store (playing);
    if (playing)
        internalPlay.store (false);

    bool endedHere = false;
    for (int off = 0; off < n; off += kMaxBlock)
    {
        const int len = std::min (kMaxBlock, n - off);
        const int64_t t = pos + off;

        if (playing && armed.load() && chs > 0)
        {
            // Transfer: record the track (mono) against the timeline; the live input plays on.
            for (int i = 0; i < len; ++i)
            {
                float m = 0.0f;
                for (int ch = 0; ch < chs; ++ch)
                    m += buffer.getReadPointer (ch)[off + i];
                monoBuf[(size_t) i] = m / (float) chs;
            }
            if (! capturing)
            {
                capturing = true;
                capturingNow.store (true);
                busy.store (true);
            }
            fifo.push (t, monoBuf.data(), len);
            processLive (buffer, off, len);
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
        processLive (buffer, off, len);

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

        // The newest snapshot; a change crossfades over 10 ms so edits during playback never click.
        if (fadeLeft <= 0)
        {
            published.hold (1, cur);
            const auto* p = published.acquire (0);
            if (cur != nullptr && p != cur && p != nullptr)
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

        renderPlayback (*cur, rpos, len, wetBuf.data(), coverBuf.data(), false);
        if (old != nullptr)
        {
            renderPlayback (*old, rpos, len, wetOldBuf.data(), coverOldBuf.data(), false);
            for (int i = 0; i < len; ++i)
            {
                const float a = juce::jlimit (0.0f, 1.0f, 1.0f - (float) (fadeLeft - i) / (float) fadeLen);
                wetBuf[(size_t) i] = wetOldBuf[(size_t) i] + (wetBuf[(size_t) i] - wetOldBuf[(size_t) i]) * a;
                coverBuf[(size_t) i] = coverOldBuf[(size_t) i] + (coverBuf[(size_t) i] - coverOldBuf[(size_t) i]) * a;
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
                const float c = coverBuf[(size_t) i];
                d[i] = (live ? d[i] * (1.0f - c) : 0.0f) + wetBuf[(size_t) i] * c;
            }
        }
    }
    audioInBlock.store (false);

    // Offline renders (exports, the QA harness) wait for the analysis so the next pass plays the edit.
    if (endedHere && nonRealtime)
        for (int waited = 0; waited < 120000 && (busy.load() || captureEnded.load()); ++waited)
            juce::Thread::sleep (1);
}

void TakeEngine::playFrom (int64_t from, int64_t to) noexcept
{
    internalPos.store (std::max<int64_t> (0, from));
    internalStop.store (to);
    internalPlay.store (true);
}

//==============================================================================
void TakeEngine::lockOutAudio()
{
    busy.store (true);
    for (int i = 0; i < 2000 && audioInBlock.load(); ++i)
        juce::Thread::sleep (1);
}

void TakeEngine::run()
{
    while (! threadShouldExit())
    {
        wait (4);
        if (! prepared.load() || threadShouldExit())
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

        if (settingsChanged())
            dirty.store (true);
        if (dirty.exchange (false))
            rebuild();
        if (bouncePending.exchange (false))
            doBounce();
        saveTakeIfNeeded();
        published.reclaim();
    }
}

void TakeEngine::resetAll()
{
    resetModel();
    spans.clear();
    takeFile = juce::File();
    takeFileStart = 0.0;
    saveTakePending.store (false);
}

void TakeEngine::finaliseCapture()
{
    const double rate = sr.load();
    const auto fresh = captured;
    captured.clear();
    if (fresh.isEmpty())
    {
        busy.store (false);
        return;
    }

    std::vector<Span> merged;
    {
        const juce::ScopedLock sl (modelLock);
        if (! juce::approximatelyEqual (modelRate, rate))
        {
            resetAll();
            modelRate = rate;
        }
        for (const auto& s : fresh.spans())
            spans.add (s, samplesOf (0.3, rate));
        merged = spans.spans();
    }
    analyseCapture (fresh.spans(), merged, rate);
    {
        const juce::ScopedLock sl (modelLock);
        missing.store (false);
        message.clear();
        saveTakePending.store (true);
        version.fetch_add (1);
    }
    rebuild();
    armed.store (false);
    busy.store (false);
}

void TakeEngine::rebuild()
{
    auto pb = buildPlayback();
    jassert (pb != nullptr);   // an empty snapshot of the subclass's type when there is nothing to play
    if (pb != nullptr)
        published.publish (std::move (pb));
    version.fetch_add (1);
}

void TakeEngine::doLoad()
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
    const auto res = takefiles::load (take, at, file, rate, &cancel);
    if (res.ok)
    {
        const Span span { at, at + res.length };
        {
            const juce::ScopedLock sl (modelLock);
            resetAll();
            modelRate = rate;
            spans.add (span);
        }
        analyseCapture ({ span }, { span }, rate);
        const juce::ScopedLock sl (modelLock);
        message.clear();
        missing.store (false);
        saveTakePending.store (true);   // our own copy, so the project survives the original moving
        armed.store (false);
        version.fetch_add (1);
    }
    else
    {
        const juce::ScopedLock sl (modelLock);
        resetAll();
        message = res.error;
        version.fetch_add (1);
    }
    rebuild();
    busy.store (false);
}

void TakeEngine::doClear()
{
    lockOutAudio();
    take.clear();
    captured.clear();
    {
        const juce::ScopedLock sl (modelLock);
        resetAll();
        message.clear();
        missing.store (false);
        version.fetch_add (1);
    }
    armed.store (true);
    rebuild();
    busy.store (false);
}

void TakeEngine::doRestore (const juce::ValueTree& tree)
{
    const double rate = sr.load();
    lockOutAudio();
    take.clear();
    captured.clear();
    finalisePending = false;

    const juce::File file (tree.getProperty ("take").toString());
    const double fileStart = (double) tree.getProperty ("takeStart", 0.0);
    const auto sp = SpanSet::fromString (tree.getProperty ("spans").toString(), rate);

    if (file == juce::File() || sp.isEmpty())
    {
        const juce::ScopedLock sl (modelLock);
        resetAll();
        modelRate = rate;
        armed.store (true);
        version.fetch_add (1);
    }
    else if (! file.existsAsFile())
    {
        const juce::ScopedLock sl (modelLock);
        resetAll();
        modelRate = rate;
        message = "Take not found: " + file.getFullPathName() + ". Press TRANSFER and play to capture again.";
        missing.store (true);
        armed.store (false);
        version.fetch_add (1);
    }
    else
    {
        const auto res = takefiles::load (take, samplesOf (fileStart, rate), file, rate, &cancel);
        {
            const juce::ScopedLock sl (modelLock);
            resetAll();
            modelRate = rate;
            spans = sp;
            takeFile = file;
            takeFileStart = fileStart;
            message = res.ok ? juce::String() : res.error;
            missing.store (! res.ok);
        }
        analyseRestored (tree, sp.spans(), rate);
        armed.store (false);
        version.fetch_add (1);
    }
    dirty.store (false);
    rebuild();
    busy.store (false);
}

void TakeEngine::saveTakeIfNeeded (bool evenIfBusy)
{
    const juce::ScopedLock save (saveLock);
    if (! saveTakePending.load() || (busy.load() && ! evenIfBusy))
        return;
    Span bounds;
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
    const auto file = takefiles::newTakeFile (folder);
    const bool ok = takefiles::save (take, bounds, rate, file);
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

void TakeEngine::doBounce()
{
    const auto* pb = published.latest();
    if (pb == nullptr || pb->spans.empty())
    {
        setMessage ("Nothing to bounce yet: capture or drop a vocal first.");
        version.fetch_add (1);
        return;
    }
    // From the song start, so the file lines up when dropped at bar 1.
    const int64_t end = pb->spans.back().end;
    juce::AudioBuffer<float> out (1, (int) std::min<int64_t> (end, std::numeric_limits<int>::max() - 1));
    out.clear();
    std::array<float, kMaxBlock> w {}, c {};
    for (int64_t t = pb->spans.front().start; t < end; t += kMaxBlock)
    {
        const int len = (int) std::min<int64_t> (kMaxBlock, end - t);
        renderPlayback (*pb, t, len, w.data(), c.data(), true);
        for (int i = 0; i < len; ++i)
            out.setSample (0, (int) (t + i), w[(size_t) i] * c[(size_t) i]);
    }
    const auto file = takefiles::folder (folder, "Bounces")
                          .getChildFile (folder + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S") + ".wav");
    const bool ok = takefiles::writeWav (out, sr.load(), file);
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
TakeStatus TakeEngine::getStatus() const noexcept
{
    if (capturingNow.load())
        return TakeStatus::Capturing;
    if (busy.load())
        return TakeStatus::Analysing;
    if (missing.load())
        return TakeStatus::Missing;
    if (armed.load())
        return TakeStatus::Listening;
    return TakeStatus::Ready;
}

juce::String TakeEngine::getMessage() const
{
    const juce::ScopedLock sl (modelLock);
    return message;
}

void TakeEngine::setMessage (const juce::String& m)
{
    const juce::ScopedLock sl (modelLock);
    message = m;
}

bool TakeEngine::hasTake() const
{
    const juce::ScopedLock sl (modelLock);
    return ! spans.isEmpty();
}

double TakeEngine::getModelRate() const
{
    const juce::ScopedLock sl (modelLock);
    return modelRate > 0.0 ? modelRate : sr.load();
}

void TakeEngine::loadFile (const juce::File& file, int64_t at)
{
    {
        const juce::ScopedLock sl (modelLock);
        loadRequest = file;
        loadAt = std::max<int64_t> (0, at);
    }
    notify();
}

void TakeEngine::clearTake()
{
    clearPending.store (true);
    notify();
}

void TakeEngine::bounce()
{
    bouncePending.store (true);
    notify();
}

juce::File TakeEngine::getLastBounce() const
{
    const juce::ScopedLock sl (modelLock);
    return lastBounce;
}

//==============================================================================
juce::ValueTree TakeEngine::makeStateTree (double rate) const
{
    juce::ValueTree t (tag);
    if (rate <= 0.0)
        return t;
    t.setProperty ("take", takeFile.getFullPathName(), nullptr);
    t.setProperty ("takeStart", takeFileStart, nullptr);
    t.setProperty ("spans", spans.toString (rate), nullptr);
    writeModel (t, rate);
    return t;
}

void TakeEngine::writeState (juce::ValueTree& parent)
{
    // A take captured moments ago may not be on disk yet: write it now so the project can find it.
    saveTakeIfNeeded();
    const juce::ScopedLock sl (modelLock);
    if (pendingState != nullptr)
        parent.appendChild (pendingState->createCopy(), nullptr);   // not restored yet: keep what we were given
    else
        parent.appendChild (makeStateTree (modelRate), nullptr);
}

void TakeEngine::readState (const juce::ValueTree& parent)
{
    const auto t = parent.getChildWithName (tag);
    {
        const juce::ScopedLock sl (modelLock);
        pendingState = std::make_unique<juce::ValueTree> (t.isValid() ? t.createCopy() : juce::ValueTree (tag));
    }
    busy.store (true);   // live input until the take is back
    notify();
}
} // namespace dd
