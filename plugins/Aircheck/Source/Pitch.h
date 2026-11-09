#pragma once

#include <dd_dsp/dd_dsp.h>

/*
    Pitch correction: a YIN detector, a corrector that turns the sung pitch into a target note
    (key / scale, retune speed, humanize, flex, natural vibrato), and a pitch-synchronous overlap-add
    (TD-PSOLA) shifter. PSOLA moves whole glottal periods closer together or further apart, so the
    voice's formants (its vowel and timbre) stay where they are; resampling each grain moves the
    formants on their own (the Formant control).
*/
namespace aircheck
{
inline const juce::StringArray& keyNames()
{
    static const juce::StringArray n { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n;
}

inline const juce::StringArray& scaleNames()
{
    static const juce::StringArray n { "Chromatic", "Major", "Minor", "Harmonic Minor", "Major Pentatonic",
                                       "Minor Pentatonic", "Blues", "Dorian", "Mixolydian" };
    return n;
}

/** Bit i set = the note i semitones above the key is in the scale. */
inline uint16_t scaleMask (int scale) noexcept
{
    auto bits = [] (std::initializer_list<int> steps)
    {
        uint16_t m = 0;
        for (int s : steps) m = (uint16_t) (m | (1u << s));
        return m;
    };
    switch (scale)
    {
        case 1:  return bits ({ 0, 2, 4, 5, 7, 9, 11 });
        case 2:  return bits ({ 0, 2, 3, 5, 7, 8, 10 });
        case 3:  return bits ({ 0, 2, 3, 5, 7, 8, 11 });
        case 4:  return bits ({ 0, 2, 4, 7, 9 });
        case 5:  return bits ({ 0, 3, 5, 7, 10 });
        case 6:  return bits ({ 0, 3, 5, 6, 7, 10 });
        case 7:  return bits ({ 0, 2, 3, 5, 7, 9, 10 });
        case 8:  return bits ({ 0, 2, 4, 5, 7, 9, 10 });
        default: return 0x0fff;
    }
}

/** Is a MIDI note in key + scale? */
inline bool inScale (int note, int key, uint16_t mask) noexcept
{
    return (mask >> (((note - key) % 12 + 12) % 12)) & 1u;
}

//==============================================================================
/**
    Monophonic YIN on a 4x decimated copy (~12 kHz) for the coarse period, then refined on the
    full-rate signal, so the estimate is accurate to a few cents while staying cheap.
*/
class PitchDetector
{
public:
    static constexpr float kMinHz = 65.0f, kMaxHz = 1050.0f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        decim = std::max (1, (int) std::round (sr / 12000.0));
        dsr = sr / decim;
        W = (int) (0.024 * dsr);
        maxLag = (int) std::ceil (dsr / kMinHz) + 2;
        minLag = std::max (2, (int) std::floor (dsr / kMaxHz));
        lp1.set (dd::Biquad::Type::LowPass, 2400.0f, 0.5412f, 0.0f, sr);
        lp2.set (dd::Biquad::Type::LowPass, 2400.0f, 1.3066f, 0.0f, sr);
        hp.set (dd::Biquad::Type::HighPass, 55.0f, 0.707f, 0.0f, sr);

        dbuf.assign ((size_t) nextPow2 (W + maxLag + 64), 0.0f);
        dmask = (int) dbuf.size() - 1;
        full.assign ((size_t) nextPow2 ((W + maxLag) * decim + 256), 0.0f);
        fmask = (int) full.size() - 1;
        frame.assign ((size_t) (W + maxLag), 0.0f);
        diff.assign ((size_t) maxLag + 2, 0.0f);
        cmnd.assign ((size_t) maxLag + 2, 1.0f);
        reset();
    }

    void reset()
    {
        std::fill (dbuf.begin(), dbuf.end(), 0.0f);
        std::fill (full.begin(), full.end(), 0.0f);
        lp1.reset(); lp2.reset(); hp.reset();
        dpos = fpos = dcount = hopCount = 0;
        count = 0;
        voiced = false;
        hz = 0.0f;
        prevTau = 0.0f;
        jumpVotes = 0;
    }

    /** Feed one full-rate mono sample; true when a new estimate is ready (every ~2.7 ms). */
    bool push (float x) noexcept
    {
        x = hp.process (x);
        full[(size_t) fpos] = x;
        fpos = (fpos + 1) & fmask;
        ++count;
        const float y = lp2.process (lp1.process (x));
        if (++dcount < decim)
            return false;
        dcount = 0;
        dbuf[(size_t) dpos] = y;
        dpos = (dpos + 1) & dmask;
        if (++hopCount < kHop)
            return false;
        hopCount = 0;
        analyse();
        return true;
    }

    bool isVoiced() const noexcept   { return voiced; }
    float getHz() const noexcept     { return hz; }
    float getClarity() const noexcept { return clarity; }
    /** Sample index (since prepare) the latest estimate describes: the middle of its analysis window. */
    int64_t getTime() const noexcept { return count - (int64_t) ((W + maxLag) * decim) / 2; }
    int getHopSamples() const noexcept { return kHop * decim; }
    /** How long after a moment the estimate describing it arrives (half the window, plus a hop). */
    int getDelaySamples() const noexcept { return (W + maxLag) * decim / 2 + kHop * decim; }

private:
    static int nextPow2 (int n) { int p = 1; while (p < n) p <<= 1; return p; }

    void analyse() noexcept
    {
        const int N = W + maxLag;
        for (int j = 0; j < N; ++j)
            frame[(size_t) j] = dbuf[(size_t) ((dpos - N + j + dmask + 1) & dmask)];

        float energy = 0.0f;
        for (int j = maxLag; j < N; ++j)
            energy += frame[(size_t) j] * frame[(size_t) j];
        const float rmsDb = dd::gainToDb (std::sqrt (energy / (float) W));
        if (rmsDb < -52.0f)
        {
            voiced = false;
            prevTau = 0.0f;
            return;
        }

        // Difference function and its cumulative-mean normalisation (YIN steps 2 and 3).
        const float* x = frame.data();
        float running = 0.0f;
        cmnd[0] = 1.0f;
        for (int tau = 1; tau <= maxLag - 1; ++tau)
        {
            float d = 0.0f;
            for (int j = 0; j < W; ++j)
            {
                const float e = x[j] - x[j + tau];
                d += e * e;
            }
            diff[(size_t) tau] = d;
            running += d;
            cmnd[(size_t) tau] = running > 0.0f ? d * (float) tau / running : 1.0f;
        }

        // First dip under the threshold (step 4), else the global minimum if it is clear enough.
        const float threshold = voiced ? 0.2f : 0.13f;
        int best = -1;
        for (int tau = minLag; tau < maxLag - 2; ++tau)
            if (cmnd[(size_t) tau] < threshold)
            {
                while (tau + 1 < maxLag - 1 && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            best = minLag;
            for (int tau = minLag; tau < maxLag - 1; ++tau)
                if (cmnd[(size_t) tau] < cmnd[(size_t) best])
                    best = tau;
            if (cmnd[(size_t) best] > (voiced ? 0.42f : 0.3f))
            {
                voiced = false;
                prevTau = 0.0f;
                return;
            }
        }

        // Continuity: a voice does not leap to an overtone (or below its note) in one 2.7 ms hop, but a
        // breathy or bright voice can make YIN do exactly that. While the previous period still fits nearly
        // as well, keep following it; a real note change makes it fit badly and is taken at once, and a
        // new period that keeps winning for four hops is taken anyway. (A wrong period makes the shifter
        // cut grains a third or a sixth of a real period long: a squeaky buzz, not a voice.)
        if (voiced && prevTau > 0.0f)
        {
            const float ratio = (float) best / prevTau;
            if (ratio < 0.88f || ratio > 1.12f)
            {
                const int lo = std::max (minLag, (int) std::floor (prevTau * 0.88f));
                const int hi = std::min (maxLag - 2, (int) std::ceil (prevTau * 1.12f));
                int near = -1;
                for (int tau = lo; tau <= hi; ++tau)
                    if (near < 0 || cmnd[(size_t) tau] < cmnd[(size_t) near])
                        near = tau;
                if (near >= 0 && cmnd[(size_t) near] < 0.3f && cmnd[(size_t) near] < cmnd[(size_t) best] + 0.12f && ++jumpVotes < 4)
                    best = near;
                else
                    jumpVotes = 0;
            }
            else
                jumpVotes = 0;
        }
        prevTau = (float) best;
        clarity = 1.0f - cmnd[(size_t) best];

        // Parabolic interpolation (step 5), then refine on the full-rate signal around the same lag.
        float tauD = (float) best;
        if (best > 1 && best < maxLag - 1)
        {
            const float a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
            const float den = a - 2.0f * b + c;
            if (den > 1.0e-9f)
                tauD += juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den);
        }
        const float lagF = tauD * (float) decim;
        const int lo = std::max (2, (int) std::floor (lagF) - decim);
        const int hi = std::min ((maxLag - 1) * decim, (int) std::ceil (lagF) + decim);
        const int WF = W * decim;
        const int span = WF + hi;
        const int base = fpos - span;   // oldest sample of the full-rate window
        float bestD = 1.0e30f, dm = 0.0f, dp = 0.0f;
        int bestL = lo;
        std::array<float, 64> dl {};
        const int nl = std::min (64, hi - lo + 1);
        for (int k = 0; k < nl; ++k)
        {
            const int L = lo + k;
            float d = 0.0f;
            for (int j = 0; j < WF; j += 1)
            {
                const float e = full[(size_t) ((base + j) & fmask)] - full[(size_t) ((base + j + L) & fmask)];
                d += e * e;
            }
            dl[(size_t) k] = d;
            if (d < bestD) { bestD = d; bestL = L; }
        }
        float lag = (float) bestL;
        const int bk = bestL - lo;
        if (bk > 0 && bk < nl - 1)
        {
            dm = dl[(size_t) bk - 1];
            dp = dl[(size_t) bk + 1];
            const float den = dm - 2.0f * bestD + dp;
            if (den > 1.0e-12f)
                lag += juce::jlimit (-0.5f, 0.5f, 0.5f * (dm - dp) / den);
        }
        hz = (float) (sr / lag);
        voiced = true;
    }

    static constexpr int kHop = 32;   // decimated samples between estimates
    double sr = 48000.0, dsr = 12000.0;
    int decim = 4, W = 288, maxLag = 187, minLag = 11;
    dd::Biquad lp1, lp2, hp;
    std::vector<float> dbuf, full, frame, diff, cmnd;
    int dmask = 0, fmask = 0, dpos = 0, fpos = 0, dcount = 0, hopCount = 0;
    int64_t count = 0;
    bool voiced = false;
    float hz = 0.0f, clarity = 0.0f, prevTau = 0.0f;
    int jumpVotes = 0;
};

//==============================================================================
/** Settings for the corrector, resolved once per block. */
struct TuneSettings
{
    int key = 0, scale = 0;
    float retuneMs = 50.0f;     // 0 = instant (the hard, robotic effect)
    float humanize = 0.0f;      // slows the retune on held notes
    float flex = 0.0f;          // leaves notes alone the further they are from the target
    float vibrato = 1.0f;       // 1 = keep the singer's vibrato, 0 = flatten it
    float amount = 1.0f;
    float transpose = 0.0f;     // semitones
};

/**
    One step per detector estimate: picks the target note (with hysteresis), and smooths the shift
    toward it at the retune speed. Output is the shift in semitones to apply at that moment.
*/
class Corrector
{
public:
    void prepare (double sampleRate, int hopSamples)
    {
        hopSec = (float) hopSamples / (float) sampleRate;
        reset();
    }

    void reset()
    {
        shift = 0.0f;
        centre = -1.0f;
        target = -1;
        noteAge = 0.0f;
        unvoicedFor = 1.0f;
        hist[0] = hist[1] = hist[2] = 0.0f;
        histN = 0;
    }

    /** Returns the semitone shift to apply (correction + transpose). */
    float step (bool voiced, float hz, const TuneSettings& s) noexcept
    {
        if (! voiced || hz <= 0.0f)
        {
            unvoicedFor += hopSec;
            if (unvoicedFor > 0.06f)
            {
                histN = 0;
                centre = -1.0f;
            }
            return shift + s.transpose;
        }

        // Median of three estimates removes single-frame octave slips.
        const float raw = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        hist[2] = hist[1]; hist[1] = hist[0]; hist[0] = raw;
        histN = std::min (3, histN + 1);
        float midi = raw;
        if (histN == 3)
            midi = std::max (std::min (hist[0], hist[1]), std::min (std::max (hist[0], hist[1]), hist[2]));

        const bool onset = unvoicedFor > 0.06f;
        unvoicedFor = 0.0f;

        // The note's centre: an average that rides under vibrato (~5 Hz) but follows note changes.
        // Fast while a note settles (scoops into it), slower once it is held and vibrato develops.
        if (centre < 0.0f || onset || std::abs (midi - centre) > 0.9f)
        {
            centre = midi;
            settle = 0.0f;
        }
        else
        {
            settle += hopSec;
            centre += (midi - centre) * (1.0f - std::exp (-hopSec / (settle < 0.16f ? 0.025f : 0.12f)));
        }
        const float follow = centre + (midi - centre) * (1.0f - s.vibrato);   // what we correct

        // Target: nearest note of the scale, kept until another one is clearly closer.
        const uint16_t mask = scaleMask (s.scale);
        const int candidate = nearestInScale (follow, s.key, mask);
        if (target < 0 || onset)
            target = candidate;
        else if (candidate != target && std::abs (follow - (float) candidate) + 0.1f < std::abs (follow - (float) target))
            target = candidate;
        if (! inScale (target, s.key, mask))
            target = candidate;
        noteAge = (target == lastTarget && ! onset) ? noteAge + hopSec : 0.0f;
        lastTarget = target;

        float want = ((float) target - follow) * s.amount;
        if (s.flex > 0.0f)
        {
            // Flex: full correction close to the note, fading out for notes sung well away from it.
            const float window = juce::jmap (s.flex, 1.0f, 0.18f);
            const float err = std::abs ((float) target - follow);
            want *= 1.0f - juce::jlimit (0.0f, 1.0f, (err - window * 0.5f) / (window * 0.5f));
        }

        // Natural settings let the scoop into a note through and correct once it settles; the hard
        // setting (retune 0) corrects everything at once, which is the effect.
        const float keepScoop = juce::jlimit (0.0f, 1.0f, s.retuneMs / 60.0f);
        want *= 1.0f - keepScoop * (1.0f - juce::jlimit (0.0f, 1.0f, settle / 0.12f));

        // Retune speed; humanize stretches it on notes held longer.
        float tau = s.retuneMs * 0.001f;
        tau *= 1.0f + s.humanize * 4.0f * juce::jlimit (0.0f, 1.0f, (noteAge - 0.15f) / 0.5f);
        if (tau <= 0.0005f)
            shift = want;
        else
        {
            if (onset)
                shift = 0.0f;
            shift += (want - shift) * (1.0f - std::exp (-hopSec / tau));
        }

        inputMidi = midi;
        return shift + s.transpose;
    }

    float getInputMidi() const noexcept { return inputMidi; }
    int getTargetNote() const noexcept  { return target; }

private:
    static int nearestInScale (float midi, int key, uint16_t mask) noexcept
    {
        const int n0 = (int) std::floor (midi);
        int best = n0;
        float bestD = 99.0f;
        for (int n = n0 - 6; n <= n0 + 7; ++n)
            if (inScale (n, key, mask) && std::abs ((float) n - midi) < bestD)
            {
                bestD = std::abs ((float) n - midi);
                best = n;
            }
        return best;
    }

    float hopSec = 0.0027f, shift = 0.0f, centre = -1.0f, settle = 0.0f, noteAge = 0.0f, unvoicedFor = 1.0f, inputMidi = 0.0f;
    int target = -1, lastTarget = -1, histN = 0;
    float hist[3] {};
};

//==============================================================================
/**
    Real-time TD-PSOLA for up to two channels sharing one set of pitch marks (from the mono sum).
    Analysis marks follow the glottal periods (each one aligned to its predecessor by correlation);
    synthesis marks are spaced by period / ratio, and each takes the Hann-windowed grain of the
    nearest analysis mark. Unvoiced sound uses fixed 5 ms grains at ratio 1 (perfect reconstruction).
    Fixed latency, reported to the host.
*/
class Psola
{
public:
    static constexpr float kMaxFormant = 1.5f;    // grains are read at most this much faster (+7 st formant)
    static constexpr int kMaxBlock = 64;

    /** What the shifter needs to know about one moment of the input. */
    struct Frame { int64_t time = 0; bool voiced = false; float period = 0.0f, ratio = 1.0f; };

    /** `detectorDelay`: how late the pitch estimate for a moment arrives (PitchDetector::getDelaySamples). */
    void prepare (double sampleRate, int detectorDelay)
    {
        sr = sampleRate;
        tMax = (float) (sr / PitchDetector::kMinHz);
        tUnvoiced = (float) (0.005 * sr);
        lookahead = (int) (0.010 * sr);
        // Grains read up to a period and a half past their synthesis time, faster with the formant up;
        // and the marks they come from only exist once the detector has described that moment (plus the
        // look-ahead that catches note starts).
        const float readAhead = tMax * (1.0f + 0.5f + kMaxFormant + 0.3f);
        const float marksReady = tMax * 1.6f + (float) (detectorDelay + lookahead);
        latency = kMaxBlock + (int) std::ceil (std::max (readAhead, marksReady)) + 8;
        int size = 1;
        while (size < latency * 4 + (int) tMax * 4)
            size <<= 1;
        mask = size - 1;
        for (int ch = 0; ch < 2; ++ch)
        {
            in[ch].assign ((size_t) size, 0.0f);
            acc[ch].assign ((size_t) size, 0.0f);
        }
        mono.assign ((size_t) size, 0.0f);
        wsum.assign ((size_t) size, 0.0f);
        rsum.assign ((size_t) size, 0.0f);
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (in[ch].begin(), in[ch].end(), 0.0f);
            std::fill (acc[ch].begin(), acc[ch].end(), 0.0f);
        }
        std::fill (mono.begin(), mono.end(), 0.0f);
        std::fill (wsum.begin(), wsum.end(), 0.0f);
        std::fill (rsum.begin(), rsum.end(), 0.0f);
        n = 0;
        numMarks = 0;
        markHead = 0;
        addMark ({ -(int64_t) tUnvoiced, tUnvoiced, false, 1.0f });
        nextSynth = 0.0;
        numFrames = 0;
        frameHead = 0;
        knownUntil = std::numeric_limits<int64_t>::min();
    }

    int getLatency() const noexcept { return latency; }
    int64_t getWritePosition() const noexcept { return n; }

    /** Pitch info from the detector/corrector, stamped with the input time it describes. */
    void pushFrame (const Frame& f) noexcept
    {
        knownUntil = std::max (knownUntil, f.time);
        frames[(size_t) frameHead] = f;
        frameHead = (frameHead + 1) % kFrames;
        numFrames = std::min (kFrames, numFrames + 1);
    }

    /** The newest frame at or before `time` (falls back to the oldest). */
    Frame frameAt (int64_t time) const noexcept
    {
        Frame f;
        for (int k = 0; k < numFrames; ++k)
        {
            const auto& c = frames[(size_t) ((frameHead - 1 - k + kFrames) % kFrames)];
            f = c;
            if (c.time <= time)
                break;
        }
        return f;
    }

    /**
        Processes up to kMaxBlock samples in place on `ch0` / `ch1` (ch1 may be null for mono).
        `formant` is the grain read speed (1 = formants untouched).
    */
    void process (float* ch0, float* ch1, int len, float formant) noexcept
    {
        formant = juce::jlimit (1.0f / kMaxFormant, kMaxFormant, formant);
        for (int i = 0; i < len; ++i)
        {
            const int w = (int) ((n + i) & mask);
            in[0][(size_t) w] = ch0[i];
            in[1][(size_t) w] = ch1 != nullptr ? ch1[i] : ch0[i];
            mono[(size_t) w] = ch1 != nullptr ? 0.5f * (ch0[i] + ch1[i]) : ch0[i];
        }
        n += len;

        makeAnalysisMarks();

        // Every grain that reaches into this block's output window must be added before reading it.
        const int64_t r0 = n - len - latency;
        while (nextSynth - (double) tMax * 1.05 < (double) (r0 + len))
            placeGrain (formant);

        for (int i = 0; i < len; ++i)
        {
            const int r = (int) ((r0 + i) & mask);
            // Overlapping grains are averaged, so a voice shifted up would lose level as its periods
            // crowd together (-3.5 dB at +7 st). Keep the loudness instead: more periods each a little
            // quieter, the same power, as a real voice singing higher with the same effort.
            const float ws = wsum[(size_t) r];
            const float localRatio = ws > 1.0e-6f ? rsum[(size_t) r] / ws : 1.0f;
            const float norm = std::sqrt (std::max (1.0f, localRatio)) / std::max (ws, 0.5f);
            ch0[i] = acc[0][(size_t) r] * norm;
            if (ch1 != nullptr)
                ch1[i] = acc[1][(size_t) r] * norm;
            acc[0][(size_t) r] = acc[1][(size_t) r] = wsum[(size_t) r] = rsum[(size_t) r] = 0.0f;
        }
    }

    /** The unprocessed input at an absolute input time (still in the buffer), for dry / bypass paths. */
    float readInput (int ch, double pos) const noexcept { return hermite (in[(size_t) ch], pos); }

private:
    struct Mark { int64_t pos; float period; bool voiced; float ratio; };
    static constexpr int kMarks = 256, kFrames = 128;

    void addMark (Mark m) noexcept
    {
        marks[(size_t) markHead] = m;
        markHead = (markHead + 1) % kMarks;
        numMarks = std::min (kMarks, numMarks + 1);
    }

    const Mark& lastMark() const noexcept { return marks[(size_t) ((markHead - 1 + kMarks) % kMarks)]; }

    void makeAnalysisMarks() noexcept
    {
        for (;;)
        {
            const Mark prev = lastMark();
            Frame f = frameAt (prev.pos + (int64_t) prev.period);
            auto usable = [this] (const Frame& fr) { return fr.voiced && fr.period > 8.0f && fr.period <= tMax; };
            if (! usable (f))
            {
                // A note starts here if the detector hears one a moment later: its window needed to fill
                // with voice first. Take that period now, so the note is shifted from its first cycle.
                const Frame ahead = frameAt (prev.pos + (int64_t) prev.period + lookahead);
                if (usable (ahead))
                    f = ahead;
            }
            const bool v = usable (f);
            const float period = v ? f.period : tUnvoiced;
            const int radius = v ? (int) (period * 0.22f) : 0;
            const int64_t predicted = prev.pos + (int64_t) std::lround (period);
            if (predicted + (int64_t) period + radius + 2 > n)
                return;
            // The detector reports a moment about 20 ms after it happens (half its window). Waiting until
            // it has described this spot keeps note starts from being cut as unvoiced (played at the old
            // pitch, then snapping to the new one) and note ends from being cut as voiced.
            if (predicted + lookahead > knownUntil)
                return;

            int64_t pos = predicted;
            if (v && prev.voiced && radius > 0)
            {
                // Align to the previous grain: the candidate whose waveform matches it best.
                const int half = (int) period;
                float bestC = -1.0e30f;
                for (int c = -radius; c <= radius; c += 1)
                {
                    float sum = 0.0f;
                    const int64_t cand = predicted + c;
                    for (int k = -half; k < half; k += 2)
                        sum += mono[(size_t) ((cand + k) & mask)] * mono[(size_t) ((prev.pos + k) & mask)];
                    if (sum > bestC) { bestC = sum; pos = cand; }
                }
            }
            addMark ({ pos, period, v, f.ratio });
        }
    }

    const Mark& nearestMark (double t) const noexcept
    {
        int best = (markHead - 1 + kMarks) % kMarks;
        double bestD = 1.0e30;
        for (int k = 0; k < numMarks; ++k)
        {
            const int idx = (markHead - 1 - k + kMarks) % kMarks;
            const double d = std::abs ((double) marks[(size_t) idx].pos - t);
            if (d < bestD) { bestD = d; best = idx; }
            else if ((double) marks[(size_t) idx].pos < t - (double) tMax * 2.0)
                break;   // marks only get further away from here
        }
        return marks[(size_t) best];
    }

    void placeGrain (float formant) noexcept
    {
        const auto& m = nearestMark (nextSynth);
        const Frame f = frameAt ((int64_t) nextSynth);
        // The ratio of this moment, unless the detector had not heard the note yet here (a note start
        // the look-ahead caught): then the ratio that came with the mark's period, not a stale one.
        const float ratio = m.voiced ? juce::jlimit (0.5f, 2.0f, f.voiced ? f.ratio : m.ratio) : 1.0f;
        const float ta = m.period;
        const float readSpeed = m.voiced ? formant : 1.0f;
        const int half = std::max (2, (int) std::lround (ta));
        const int64_t centre = (int64_t) std::floor (nextSynth);
        const double frac = nextSynth - (double) centre;
        const float invHalf = 1.0f / (float) half;
        for (int k = -half + 1; k < half; ++k)
        {
            const float x = (float) k * invHalf;
            const float w = 0.5f + 0.5f * std::cos (dd::kPi * x);
            const double src = (double) m.pos + ((double) k - frac) * readSpeed;
            if (src > (double) (n - 3))
                break;   // only at the very lowest notes with formant up: never read past the input
            const int o = (int) ((centre + k) & mask);
            acc[0][(size_t) o] += w * hermite (in[0], src);
            acc[1][(size_t) o] += w * hermite (in[1], src);
            wsum[(size_t) o] += w;
            rsum[(size_t) o] += w * ratio;
        }
        nextSynth += std::max (8.0, (double) (ta / ratio));
    }

    float hermite (const std::vector<float>& b, double pos) const noexcept
    {
        const int64_t i = (int64_t) std::floor (pos);
        const float t = (float) (pos - (double) i);
        const float xm1 = b[(size_t) ((i - 1) & mask)], x0 = b[(size_t) (i & mask)];
        const float x1 = b[(size_t) ((i + 1) & mask)], x2 = b[(size_t) ((i + 2) & mask)];
        const float c = (x1 - xm1) * 0.5f, v = x0 - x1, w = c + v, a = w + v + (x2 - x0) * 0.5f, bb = w + a;
        return ((a * t - bb) * t + c) * t + x0;
    }

    double sr = 48000.0, nextSynth = 0.0;
    float tMax = 740.0f, tUnvoiced = 240.0f;
    int latency = 0, mask = 0, lookahead = 480;
    std::vector<float> in[2], acc[2], mono, wsum, rsum;
    int64_t n = 0;
    std::array<Mark, kMarks> marks {};
    int numMarks = 0, markHead = 0;
    std::array<Frame, kFrames> frames {};
    int numFrames = 0, frameHead = 0;
    int64_t knownUntil = std::numeric_limits<int64_t>::min();
};
} // namespace aircheck
