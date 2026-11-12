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
    fol.prepare (sampleRate);
    prepared.store (true);
    notify();
}

//==============================================================================
void TakeEngine::Follow::prepare (double rate)
{
    // ~21 ms frames, 2.5 s of history (pre-roll and hold-back come from it).
    frameLen = rate > 60000.0 ? 2048 : 1024;
    hist.assign ((size_t) juce::nextPowerOfTwo ((int) (rate * 2.5)), 0.0f);
    const float edges[kBands - 1] = { 200.0f, 500.0f, 1200.0f, 3000.0f, 7000.0f };
    for (int b = 0; b < kBands - 1; ++b)
        coef[(size_t) b] = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * edges[b] / (float) rate);
    confirmFrames = juce::roundToInt (0.3 * rate / frameLen);
    limit = (int64_t) (rate * 60.0 * 15.0);   // a song; past 15 min (Session View jams) it stops capturing
    gainStep = (float) (1.0 / (0.01 * rate));
    gain = 0.0f;
    nStale = 0;
    reset();
}

void TakeEngine::Follow::reset() noexcept
{
    lpIn.fill (0.0f);
    lpTk.fill (0.0f);
    eIn.fill (0.0);
    eTk.fill (0.0);
    eErr = 0.0;
    zIn = zTk = 0;
    zPastIn.fill (0);
    zPastTk.fill (0);
    frameFill = 0;
    diffScore = matchRun = mutedRun = 0;
    expected = -1;
    histEnd = 0;
    histLen = 0;
    capturing = holding = false;
}

TakeEngine::Follow::Kind TakeEngine::Follow::classify() const noexcept
{
    constexpr double floorMs = 3.2e-6;   // -55 dBFS: below this a frame counts as silent
    double in = 0.0, tk = 0.0;
    for (int b = 0; b < kBands; ++b)
    {
        in += eIn[(size_t) b];
        tk += eTk[(size_t) b];
    }
    in /= frameLen;
    tk /= frameLen;
    const bool inLoud = in > floorMs, tkLoud = tk > floorMs;
    if (! inLoud && ! tkLoud) return Kind::quiet;
    if (! inLoud)             return Kind::muted;
    if (! tkLoud)             return Kind::diff;
    if (eErr / frameLen < 1.0e-3 * std::max (in, tk))
        return Kind::match;   // the same samples (to -30 dB): the clip as captured

    // Not sample-exact (Ableton re-warps a clip on every play): compare the band envelopes instead.
    double sum = 0.0, worst = 0.0;
    int used = 0;
    for (int b = 0; b < kBands; ++b)
    {
        const double a = eIn[(size_t) b] / frameLen, c = eTk[(size_t) b] / frameLen;
        if (a < floorMs * 0.1 && c < floorMs * 0.1)
            continue;
        const double d = std::min (24.0, std::abs (10.0 * std::log10 ((a + floorMs * 0.1) / (c + floorMs * 0.1))));
        sum += d;
        worst = std::max (worst, d);
        ++used;
    }
    const double mean = used > 0 ? sum / used : 0.0;
    // The lows' zero-crossing rate follows the sung pitch: a new take a semitone or two away has the same
    // vowel envelope but not the same rate (counted over ~85 ms, so a crossing more or less doesn't count).
    int zi = 0, zt = 0;
    for (size_t k = 0; k < zPastIn.size(); ++k)
    {
        zi += zPastIn[k];
        zt += zPastTk[k];
    }
    const double pitchGap = zi > 16 && zt > 16 ? std::abs (std::log2 ((zi + 1.0) / (zt + 1.0))) : 0.0;
    if (mean < 3.0 && worst < 9.0 && pitchGap < 0.07) return Kind::match;
    if (mean > 6.0 || pitchGap > 0.11)                return Kind::diff;
    return Kind::unsure;
}

bool TakeEngine::covered (int64_t t) const noexcept
{
    if (cur == nullptr)
        return false;
    const auto& sp = cur->spans;
    auto it = std::upper_bound (sp.begin(), sp.end(), t, [] (int64_t v, const Span& s) { return v < s.end; });
    return it != sp.end() && it->contains (t);
}

void TakeEngine::pushHistory (int64_t from, int64_t to) noexcept
{
    const int64_t histStart = fol.histEnd - fol.histLen;
    from = std::max (from, histStart);
    const auto mask = (int64_t) fol.hist.size() - 1;
    while (from < to)
    {
        const int64_t idx = from & mask;
        const int len = (int) std::min<int64_t> (to - from, (int64_t) fol.hist.size() - idx);
        fifo.push (from, fol.hist.data() + idx, len);
        from += len;
    }
    fol.pushedTo = std::max (fol.pushedTo, to);
}

void TakeEngine::endAutoCapture() noexcept
{
    if (! fol.capturing)
        return;
    const int64_t end = fol.holding ? fol.holdFrom : fol.pushedTo;
    fifo.flush();
    if (end > fol.captureStart)
    {
        // Until the worker has analysed it, the old snapshot doesn't fit this stretch any more: play it live.
        if (fol.nStale == (int) fol.stale.size())
        {
            std::move (fol.stale.begin() + 1, fol.stale.end(), fol.stale.begin());
            --fol.nStale;
        }
        const auto serial = autoEnded.load (std::memory_order_relaxed) + 1;
        fol.stale[(size_t) fol.nStale++] = { { fol.captureStart, end }, serial };
        autoEnded.store (serial, std::memory_order_release);
    }
    fol.capturing = fol.holding = false;
    capturingNow.store (false);
    captureEnded.store (true);
}

void TakeEngine::follow (int64_t t, int len) noexcept
{
    if (t != fol.expected)
    {
        // A jump (loop, seek): what came before isn't the run-up to this any more.
        endAutoCapture();
        fol.reset();
        fol.histEnd = t;
    }
    fol.expected = t + len;

    std::array<float, kMaxBlock> tk {};
    take.read (t, tk.data(), len);
    const auto mask = (int64_t) fol.hist.size() - 1;
    for (int i = 0; i < len; ++i)
    {
        const int64_t ti = t + i;
        if (! covered (ti))
            tk[(size_t) i] = 0.0f;
        const float x = monoBuf[(size_t) i], y = tk[(size_t) i];
        fol.hist[(size_t) (ti & mask)] = x;

        // Band energies of what plays and of the take: lows up to 200 Hz ... air above 7 kHz.
        float px = x, py = y;
        for (int b = 0; b < kBands - 1; ++b)
        {
            fol.lpIn[(size_t) b] += fol.coef[(size_t) b] * (x - fol.lpIn[(size_t) b]);
            fol.lpTk[(size_t) b] += fol.coef[(size_t) b] * (y - fol.lpTk[(size_t) b]);
            const float bx = b == 0 ? fol.lpIn[0] : fol.lpIn[(size_t) b] - fol.lpIn[(size_t) b - 1];
            const float by = b == 0 ? fol.lpTk[0] : fol.lpTk[(size_t) b] - fol.lpTk[(size_t) b - 1];
            fol.eIn[(size_t) b] += (double) bx * bx;
            fol.eTk[(size_t) b] += (double) by * by;
            px = x - fol.lpIn[(size_t) b];
            py = y - fol.lpTk[(size_t) b];
        }
        fol.eIn[kBands - 1] += (double) px * px;
        fol.eTk[kBands - 1] += (double) py * py;
        fol.eErr += (double) (x - y) * (x - y);
        const float lowIn = fol.lpIn[1], lowTk = fol.lpTk[1];
        if ((lowIn >= 0.0f) != fol.signIn && std::abs (lowIn) > 1.0e-4f) { fol.signIn = ! fol.signIn; ++fol.zIn; }
        if ((lowTk >= 0.0f) != fol.signTk && std::abs (lowTk) > 1.0e-4f) { fol.signTk = ! fol.signTk; ++fol.zTk; }

        if (++fol.frameFill < fol.frameLen)
            continue;
        const int64_t fe = ti + 1, fs = fe - fol.frameLen;
        fol.zPastIn[(size_t) fol.zSlot] = fol.zIn;
        fol.zPastTk[(size_t) fol.zSlot] = fol.zTk;
        fol.zSlot = (fol.zSlot + 1) % (int) fol.zPastIn.size();
        fol.histEnd = fe;
        fol.histLen = (int) std::min<int64_t> ((int64_t) fol.hist.size() - kMaxBlock, fol.histLen + fol.frameLen);
        onFrame (fol.classify(), fs, fe);
        fol.frameFill = 0;
        fol.eIn.fill (0.0);
        fol.eTk.fill (0.0);
        fol.eErr = 0.0;
        fol.zIn = fol.zTk = 0;
    }
}

void TakeEngine::onFrame (Follow::Kind kind, int64_t fs, int64_t fe) noexcept
{
    using K = Follow::Kind;
    if (! fol.capturing)
    {
        // A leaky score, so a few frames of a new take that happen to look like the old one don't reset it.
        const int before = fol.diffScore;
        switch (kind)
        {
            case K::diff:   ++fol.diffScore; break;
            case K::match:  fol.diffScore = std::max (0, fol.diffScore - 2); break;
            case K::quiet:
            case K::muted:  fol.diffScore = std::max (0, fol.diffScore - 1); break;
            case K::unsure: break;
        }
        fol.mutedRun = kind == K::muted ? fol.mutedRun + 1 : 0;
        if (before == 0 && fol.diffScore > 0)
            fol.suspectStart = fs;
        if (fol.diffScore >= fol.confirmFrames && fs < fol.limit)
        {
            // New or changed audio: capture it, from where it began (plus a frame, the detection's slack).
            fol.capturing = true;
            fol.holding = false;
            fol.matchRun = 0;
            fol.captureStart = std::max (fol.histEnd - fol.histLen, fol.suspectStart - fol.frameLen);
            fol.pushedTo = fol.captureStart;
            capturingNow.store (true);
            pushHistory (fol.captureStart, fe);
        }
        return;
    }

    // Capturing: keep going while it differs; once it plays what the take holds again for a while, stop
    // there and drop the matching tail (re-capturing it would only reset edits).
    const bool same = kind == K::match || (kind == K::quiet && fol.holding);
    if (same)
    {
        if (! fol.holding)
        {
            fol.holding = true;
            fol.holdFrom = fol.pushedTo;
            fol.matchRun = 0;
        }
        if (++fol.matchRun >= fol.confirmFrames)
        {
            endAutoCapture();
            fol.diffScore = 0;
        }
        return;
    }
    fol.holding = false;
    fol.matchRun = 0;
    pushHistory (fol.pushedTo, fe);
}

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

    // Stale stretches the worker has analysed since play the take again.
    const auto analysed = autoAnalysed.load (std::memory_order_acquire);
    for (int k = fol.nStale - 1; k >= 0; --k)
        if (fol.stale[(size_t) k].serial <= analysed)
        {
            std::move (fol.stale.begin() + k + 1, fol.stale.begin() + fol.nStale, fol.stale.begin() + k);
            --fol.nStale;
        }

    bool endedHere = false;
    for (int off = 0; off < n; off += kMaxBlock)
    {
        const int len = std::min (kMaxBlock, n - off);
        const int64_t t = pos + off;

        if (playing && chs > 0)
            for (int i = 0; i < len; ++i)
            {
                float m = 0.0f;
                for (int ch = 0; ch < chs; ++ch)
                    m += buffer.getReadPointer (ch)[off + i];
                monoBuf[(size_t) i] = m / (float) chs;
            }

        if (playing && armed.load() && chs > 0)
        {
            // Transfer: record the track (mono) against the timeline; the live input plays on.
            endAutoCapture();
            fol.expected = -1;
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

        // Follow: compare what plays with the take; new or changed audio is captured by itself.
        const bool following = playing && chs > 0 && autoFollow.load() && ! busy.load() && rate > 0.0;
        if (following)
            follow (t, len);
        else
        {
            endAutoCapture();
            fol.expected = -1;
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

        // Where the track no longer plays what the take holds (changed, muted, being captured, waiting for
        // analysis), the live input plays instead; 10 ms ramps either way.
        if (following || fol.gain > 0.0f)
        {
            const bool suspect = fol.capturing || fol.diffScore >= 2 || fol.mutedRun >= 2;
            for (int i = 0; i < len; ++i)
            {
                bool live = following && suspect;
                for (int k = 0; k < fol.nStale && ! live; ++k)
                    live = fol.stale[(size_t) k].span.contains (rpos + i);
                fol.gain = live ? std::min (1.0f, fol.gain + fol.gainStep) : std::max (0.0f, fol.gain - fol.gainStep);
                coverBuf[(size_t) i] *= 1.0f - fol.gain;
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

        const auto endedBeforeDrain = autoEnded.load (std::memory_order_acquire);
        fifo.drain ([this] (int64_t p, const float* d, int len)
        {
            take.write (p, d, len);
            captured.add ({ p, p + len });
        });
        drainedSerial = std::max (drainedSerial, endedBeforeDrain);
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
    const auto serial = drainedSerial;
    captured.clear();
    if (fresh.isEmpty())
    {
        autoAnalysed.store (serial, std::memory_order_release);
        busy.store (false);
        return;
    }
    analysingNow.store (true);

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
    autoAnalysed.store (serial, std::memory_order_release);
    analysingNow.store (false);
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
    autoAnalysed.store (autoEnded.load());
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
    autoAnalysed.store (autoEnded.load());
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

    autoFollow.store ((bool) tree.getProperty ("follow", true));
    autoAnalysed.store (autoEnded.load());
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
    if (busy.load() || analysingNow.load() || autoAnalysed.load() < autoEnded.load())
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
    t.setProperty ("follow", autoFollow.load(), nullptr);
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
