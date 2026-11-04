#pragma once

#include <dd_dsp/dd_dsp.h>

#include <complex>

#include "Pitch.h"

/* The vocal chain after pitch correction: mic voicing, preamp, EQ, compressor, de-esser, gate,
   doubler and vocoder. All stereo, in place, no allocation after prepare(). */
namespace aircheck
{
inline const juce::StringArray& micNames()
{
    static const juce::StringArray n { "Off", "Dynamic", "Condenser", "Ribbon", "Radio", "Lo-Fi" };
    return n;
}
inline const juce::StringArray& preampNames()
{
    static const juce::StringArray n { "Tube", "Tape", "Console", "Crunch" };
    return n;
}
inline const juce::StringArray& compNames()
{
    static const juce::StringArray n { "FET", "Opto", "Old Limiter", "VCA" };
    return n;
}
inline const juce::StringArray& carrierNames()
{
    static const juce::StringArray n { "Strings", "Robot", "Choir", "Brass" };
    return n;
}
inline const juce::StringArray& chordNames()
{
    static const juce::StringArray n { "Unison", "Octaves", "Power", "Triad", "Seventh" };
    return n;
}
inline const juce::StringArray& spaceNames()
{
    static const juce::StringArray n { "Plate", "Room", "Hall", "Chamber", "Gated" };
    return n;
}

/** RBJ band-pass with 0 dB peak gain (not in dd::Biquad's set). */
inline void setBandPass (dd::Biquad& b, float hz, float q, double sr) noexcept
{
    const float w0 = dd::kTwoPi * juce::jlimit (20.0f, (float) (sr * 0.45), hz) / (float) sr;
    const float alpha = std::sin (w0) / (2.0f * q), cw = std::cos (w0), a0 = 1.0f + alpha;
    b.b0 = alpha / a0; b.b1 = 0.0f; b.b2 = -alpha / a0; b.a1 = -2.0f * cw / a0; b.a2 = (1.0f - alpha) / a0;
}

/** |H| of a biquad at `hz`, in dB (for drawing EQ curves). */
inline float magnitudeDb (const dd::Biquad& b, double hz, double sr) noexcept
{
    const double w = juce::MathConstants<double>::twoPi * hz / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    const auto h = ((double) b.b0 + (double) b.b1 * z1 + (double) b.b2 * z2) / (1.0 + (double) b.a1 * z1 + (double) b.a2 * z2);
    return (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (h))));
}

//==============================================================================
/** Static voicing of a microphone (or a deliberately small speaker). */
class MicVoicing
{
public:
    void prepare (double newSr) { sr = newSr; type = -1; }
    void reset() { for (auto& c : f) for (auto& b : c) b.reset(); }

    void setType (int t)
    {
        if (t == type)
            return;
        type = t;
        using T = dd::Biquad::Type;
        auto set = [this] (int i, T ty, float hz, float q, float db) { for (auto& c : f) c[(size_t) i].set (ty, hz, q, db, sr); };
        numStages = 0;
        switch (t)
        {
            case 1:   // dynamic (Beyer M88 / SM7 type): proximity warmth, a presence lift, soft top
                set (0, T::LowShelf, 140.0f, 0.7f, 2.5f); set (1, T::Peak, 4600.0f, 0.9f, 3.5f);
                set (2, T::LowPass, 15000.0f, 0.6f, 0.0f); set (3, T::Peak, 380.0f, 1.0f, -1.5f); numStages = 4; break;
            case 2:   // large condenser: open top, air, a little upper-mid sheen
                set (0, T::HighPass, 35.0f, 0.7f, 0.0f); set (1, T::HighShelf, 9000.0f, 0.7f, 3.0f);
                set (2, T::Peak, 5200.0f, 1.2f, 1.5f); set (3, T::Peak, 200.0f, 0.9f, 0.8f); numStages = 4; break;
            case 3:   // ribbon: warm, dark, smooth
                set (0, T::LowShelf, 220.0f, 0.7f, 2.0f); set (1, T::HighShelf, 6000.0f, 0.6f, -6.0f);
                set (2, T::Peak, 2500.0f, 0.8f, -1.0f); numStages = 3; break;
            case 4:   // radio / telephone band
                set (0, T::HighPass, 420.0f, 0.9f, 0.0f); set (1, T::HighPass, 420.0f, 0.6f, 0.0f);
                set (2, T::LowPass, 3400.0f, 0.9f, 0.0f); set (3, T::Peak, 1600.0f, 1.0f, 5.0f); numStages = 4; break;
            case 5:   // lo-fi: tiny speaker, boxy
                set (0, T::HighPass, 650.0f, 1.1f, 0.0f); set (1, T::LowPass, 2600.0f, 1.2f, 0.0f);
                set (2, T::Peak, 1100.0f, 1.4f, 6.0f); set (3, T::LowPass, 3200.0f, 0.7f, 0.0f); numStages = 4; break;
            default: break;
        }
    }

    void process (float& l, float& r) noexcept
    {
        for (int i = 0; i < numStages; ++i)
        {
            l = f[0][(size_t) i].process (l);
            r = f[1][(size_t) i].process (r);
        }
    }

private:
    double sr = 48000.0;
    int type = -1, numStages = 0;
    std::array<std::array<dd::Biquad, 4>, 2> f {};
};

//==============================================================================
/**
    Preamp saturation, 4x oversampled. Tube: asymmetric, its bias rising with the level so louder
    passages get more even harmonics. Tape: soft symmetrical compression with a top end that softens as
    it is driven. Console: transformer-style, the lows saturate first. Crunch: hard and forward.
    tone tilts the result around 1 kHz; mix blends it with the clean signal (parallel saturation).
*/
class Preamp
{
public:
    void prepare (double newSr, int maxBlock)
    {
        sr = newSr;
        os.initProcessing ((size_t) maxBlock);
        const double osr = sr * 4.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            dc[ch].prepare (osr);
            tapeLp[ch].setCutoff (14000.0f, osr);
            lowBand[ch].setCutoff (220.0f, osr);
        }
        envCoef = dd::onePoleCoef (0.03f, osr);
        lastTone = 99.0f;
        reset();
    }

    void reset()
    {
        os.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            dc[ch].reset(); tapeLp[ch].reset(); lowBand[ch].reset();
            tilt[ch][0].reset(); tilt[ch][1].reset();
            env[ch] = 0.0f;
        }
    }

    float getLatency() const noexcept { return os.getLatencyInSamples(); }

    void process (juce::dsp::AudioBlock<float>& block, int type, float drive, float tone = 0.0f, float mix = 1.0f) noexcept
    {
        if (std::abs (tone - lastTone) > 0.001f)
        {
            lastTone = tone;
            for (int ch = 0; ch < 2; ++ch)
            {
                tilt[ch][0].set (dd::Biquad::Type::LowShelf, 1000.0f, 0.5f, -tone * 4.0f, sr * 4.0);
                tilt[ch][1].set (dd::Biquad::Type::HighShelf, 1000.0f, 0.5f, tone * 4.0f, sr * 4.0);
            }
        }
        if (type == 1)
            for (auto& t : tapeLp)
                t.setCutoff (16000.0f - 8000.0f * drive, sr * 4.0);
        auto up = os.processSamplesUp (block);
        const float g = 1.0f + 14.0f * drive * drive;
        const float comp = 1.0f / std::pow (g, 0.72f);
        mix *= juce::jlimit (0.0f, 1.0f, drive * 25.0f);   // drive 0 = untouched (the curves are never quite linear)
        for (size_t ch = 0; ch < up.getNumChannels() && ch < 2; ++ch)
        {
            auto* d = up.getChannelPointer (ch);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                const float clean = d[i];
                const float x = clean * g;
                const float a = std::abs (clean);
                env[ch] = a + (env[ch] - a) * envCoef;
                float y;
                switch (type)
                {
                    case 1:   // tape
                        y = tapeLp[ch].lp (dd::softClipCubic (x * 0.8f) * 1.25f);
                        break;
                    case 2:   // console: transformer lows saturate first
                    {
                        const float lo = lowBand[ch].lp (x);
                        y = x + 0.5f * drive * (std::tanh (lo * 1.8f) / 1.8f - lo);
                        y = dd::kneeClip (y - 0.06f * y * y * y / (1.0f + 0.3f * y * y), 0.88f);
                        break;
                    }
                    case 3:   // crunch
                        y = dd::kneeClip (x + 0.25f * x * std::abs (x), 0.5f);
                        break;
                    default:  // tube: the bias follows the level
                    {
                        const float bias = 0.12f * drive + 0.5f * std::min (env[ch] * g, 1.0f) * drive;
                        y = std::tanh (x + bias) - std::tanh (bias);
                        break;
                    }
                }
                y = dc[ch].process (y) * comp;
                y = tilt[ch][1].process (tilt[ch][0].process (y));
                d[i] = clean + (y - clean) * mix;
            }
        }
        os.processSamplesDown (block);
    }

private:
    using OS = juce::dsp::Oversampling<float>;
    double sr = 48000.0;
    // Linear-phase FIR halfbands with whole-sample latency: the dry path can be delayed exactly, so a
    // dry / wet blend never combs or dulls.
    OS os { 2, 2, OS::filterHalfBandFIREquiripple, true, true };
    dd::DcBlocker dc[2];
    dd::OnePole tapeLp[2], lowBand[2];
    dd::Biquad tilt[2][2];
    float env[2] {}, envCoef = 0.0f, lastTone = 99.0f;
};

//==============================================================================
/** The six-band vocal EQ. The UI builds its own copy to draw the curve. */
class VocalEq
{
public:
    struct Settings
    {
        float lowCut = 20.0f, lsHz = 150.0f, lsDb = 0.0f;
        float p1Hz = 300.0f, p1Db = 0.0f, p1Q = 1.0f;
        float p2Hz = 1500.0f, p2Db = 0.0f, p2Q = 1.0f;
        float p3Hz = 4500.0f, p3Db = 0.0f, p3Q = 1.0f;
        float hsHz = 10000.0f, hsDb = 0.0f, highCut = 22000.0f;
        bool operator== (const Settings&) const = default;
    };

    void prepare (double newSr) { sr = newSr; first = true; }
    void reset() { for (auto& c : f) for (auto& b : c) b.reset(); }

    void set (const Settings& s) noexcept
    {
        if (s == cur && ! first)
            return;
        cur = s;
        first = false;
        using T = dd::Biquad::Type;
        lowCutOn = s.lowCut > 21.0f;
        highCutOn = s.highCut < 21500.0f && s.highCut < (float) sr * 0.45f;
        for (auto& c : f)
        {
            c[0].set (T::HighPass, s.lowCut, 0.5412f, 0.0f, sr);
            c[1].set (T::HighPass, s.lowCut, 1.3066f, 0.0f, sr);
            c[2].set (T::LowShelf, s.lsHz, 0.7f, s.lsDb, sr);
            c[3].set (T::Peak, s.p1Hz, s.p1Q, s.p1Db, sr);
            c[4].set (T::Peak, s.p2Hz, s.p2Q, s.p2Db, sr);
            c[5].set (T::Peak, s.p3Hz, s.p3Q, s.p3Db, sr);
            c[6].set (T::HighShelf, s.hsHz, 0.7f, s.hsDb, sr);
            c[7].set (T::LowPass, s.highCut, 0.5412f, 0.0f, sr);
            c[8].set (T::LowPass, s.highCut, 1.3066f, 0.0f, sr);
        }
    }

    void process (float& l, float& r) noexcept
    {
        for (int i = 0; i < 9; ++i)
        {
            if ((i < 2 && ! lowCutOn) || (i > 6 && ! highCutOn))
                continue;
            l = f[0][(size_t) i].process (l);
            r = f[1][(size_t) i].process (r);
        }
    }

    /** Total response in dB at `hz` (uses the left channel's coefficients). */
    float responseDb (double hz) const noexcept
    {
        float db = 0.0f;
        for (int i = 0; i < 9; ++i)
            if (! ((i < 2 && ! lowCutOn) || (i > 6 && ! highCutOn)))
                db += magnitudeDb (f[0][(size_t) i], hz, sr);
        return db;
    }

private:
    double sr = 48000.0;
    bool first = true, lowCutOn = false, highCutOn = false;
    Settings cur;
    std::array<std::array<dd::Biquad, 9>, 2> f {};
};

//==============================================================================
/**
    Vocal compressor with four characters: FET (fast, bright, aggressive), Opto (smooth, program-
    dependent two-stage release), Old Limiter (cheap and gritty, the 1981 vocal sound: let the front
    of each word through with a slow attack), VCA (clean). amount sets the threshold.
*/
class Compressor
{
public:
    struct Settings { int type = 1; float amount = 0.3f, attackMs = 3.0f, releaseMs = 120.0f, ratio = 4.0f, mix = 1.0f, scHpf = 20.0f; };

    void prepare (double newSr)
    {
        sr = newSr;
        rmsCoef = dd::onePoleCoef (0.008f, sr);
        memCoef = dd::onePoleCoef (1.2f, sr);
        for (auto& d : dc) d.prepare (sr);
        lookahead = std::max (1, (int) std::lround (0.0015 * sr));
        for (auto& d : delay) d.prepare (lookahead + 4);
        reset();
    }

    void reset()
    {
        gr = 0.0f; rms = 0.0f; memory = 0.0f;
        for (auto& d : dc) d.reset();
        for (auto& d : delay) d.reset();
        for (auto& f : scHp) f.reset();
    }

    /** The compressor always delays the audio by this much (FET / VCA look ahead by it). */
    int getLatency() const noexcept { return lookahead; }

    void process (float& l, float& r, const Settings& s) noexcept
    {
        // FET and VCA detect on the undelayed signal, so they are already clamping when an onset
        // arrives (no overshoot spikes); the opto and the old limiter detect late, as the originals do.
        delay[0].push (l);
        delay[1].push (r);
        const float inL = l, inR = r;
        l = delay[0].readInt (lookahead + 1);   // readInt (1) is the sample just pushed
        r = delay[1].readInt (lookahead + 1);
        if (s.amount <= 0.001f)
        {
            gr = 0.0f;
            return;
        }
        const bool ahead = s.type == 0 || s.type == 3;
        float dl = ahead ? inL : l, dr = ahead ? inR : r;
        // Sidechain high-pass: plosives and low rumble don't pump the voice.
        if (std::abs (s.scHpf - lastScHz) > 0.5f)
        {
            lastScHz = s.scHpf;
            for (auto& f : scHp) f.set (dd::Biquad::Type::HighPass, s.scHpf, 0.707f, 0.0f, sr);
        }
        if (s.scHpf > 21.0f)
        {
            dl = scHp[0].process (dl);
            dr = scHp[1].process (dr);
        }
        const float thr = -4.0f - 34.0f * s.amount;
        float level;
        if (s.type == 1)
        {
            // Opto cells respond to the average, not the peaks.
            const float x2 = 0.5f * (dl * dl + dr * dr);
            rms = x2 + (rms - x2) * rmsCoef;
            level = dd::gainToDb (std::sqrt (rms) * 1.41f);
        }
        else
            level = dd::gainToDb (std::max (std::abs (dl), std::abs (dr)));

        const float knee = s.type == 1 ? 10.0f : (s.type == 0 ? 2.0f : (s.type == 2 ? 3.0f : 6.0f));
        const float ratio = s.type == 1 ? std::min (s.ratio, 6.0f) : s.ratio;
        const float over = level - thr;
        float target = 0.0f;
        if (over > knee * 0.5f)        target = over * (1.0f - 1.0f / ratio);
        else if (over > -knee * 0.5f)  { const float k = over + knee * 0.5f; target = (1.0f - 1.0f / ratio) * k * k / (2.0f * knee); }

        float attack = s.attackMs, release = s.releaseMs;
        if (s.type == 1)
        {
            // Two-stage opto release: quick at first, slower the longer and harder it has been working.
            memory = gr + (memory - gr) * memCoef;
            attack = std::max (attack, 6.0f);
            release *= 1.0f + 5.0f * juce::jlimit (0.0f, 1.0f, memory / 8.0f);
        }
        const float c = target > gr ? dd::onePoleCoef (attack * 0.001f, sr) : dd::onePoleCoef (release * 0.001f, sr);
        gr = target + (gr - target) * c;

        const float makeup = -thr * (1.0f - 1.0f / ratio) * 0.42f;
        const float g = dd::dbToGain (makeup - gr);
        float cl = l * g, cr = r * g;

        // Character: FET adds odd harmonics as it digs in, the old limiter even ones.
        const float work = juce::jlimit (0.0f, 1.0f, gr / 12.0f);
        if (s.type == 0)
        {
            const float k = 1.0f + 0.6f * work;
            cl = std::tanh (cl * k) / k;
            cr = std::tanh (cr * k) / k;
        }
        else if (s.type == 2)
        {
            const float k = 0.2f + 0.5f * work;
            cl = dc[0].process (std::tanh (cl + k * cl * cl));
            cr = dc[1].process (std::tanh (cr + k * cr * cr));
        }
        l = l + (cl - l) * s.mix;
        r = r + (cr - r) * s.mix;
    }

    float getGainReductionDb() const noexcept { return gr; }

private:
    double sr = 48000.0;
    float gr = 0.0f, rms = 0.0f, memory = 0.0f, rmsCoef = 0.0f, memCoef = 0.0f;
    int lookahead = 1;
    float lastScHz = -1.0f;
    dd::DcBlocker dc[2];
    dd::DelayBuffer delay[2];
    dd::Biquad scHp[2];
};

//==============================================================================
/** Split-band de-esser: only the band above `hz` is turned down, and only on sibilants. */
class DeEsser
{
public:
    void prepare (double newSr)
    {
        sr = newSr;
        att = dd::onePoleCoef (0.0008f, sr);
        rel = dd::onePoleCoef (0.06f, sr);
        lastHz = -1.0f;
        reset();
    }

    void reset() { for (auto& h : hp) h.reset(); envS = envF = 0.0f; gainDb = 0.0f; }

    void process (float& l, float& r, float amount, float hz) noexcept
    {
        if (std::abs (hz - lastHz) > 1.0f)
        {
            lastHz = hz;
            for (auto& h : hp) h.set (dd::Biquad::Type::HighPass, hz, 0.707f, 0.0f, sr);
        }
        const float hl = hp[0].process (l), hr = hp[1].process (r);
        if (amount <= 0.001f)
        {
            gainDb = 0.0f;
            return;
        }
        const float s = std::max (std::abs (hl), std::abs (hr));
        const float f = std::max (std::abs (l), std::abs (r));
        envS = s + (envS - s) * (s > envS ? att : rel);
        envF = f + (envF - f) * (f > envF ? att : rel);
        // Sibilant when the top band carries most of the energy and is loud enough.
        const float sideDb = dd::gainToDb (envS);
        const float share = envS / (envF + 1.0e-6f);
        const float thr = -16.0f - 24.0f * amount;
        float target = std::max (0.0f, sideDb - thr) * (0.5f + 0.5f * amount) * juce::jlimit (0.0f, 1.0f, (share - 0.25f) / 0.35f);
        target = std::min (target, 4.0f + 16.0f * amount);
        gainDb = target + (gainDb - target) * (target > gainDb ? 0.3f : 0.995f);
        const float g = dd::dbToGain (-gainDb) - 1.0f;
        l += hl * g;
        r += hr * g;
    }

    float getReductionDb() const noexcept { return gainDb; }

private:
    double sr = 48000.0;
    dd::Biquad hp[2];
    float att = 0.0f, rel = 0.0f, envS = 0.0f, envF = 0.0f, gainDb = 0.0f, lastHz = -1.0f;
};

//==============================================================================
/** Downward expander: quiets room noise and breaths under the threshold. -80 dB = off. */
class Gate
{
public:
    void prepare (double newSr)
    {
        sr = newSr;
        envAtt = dd::onePoleCoef (0.0005f, sr);
        envRel = dd::onePoleCoef (0.03f, sr);
        open = dd::onePoleCoef (0.001f, sr);
        close = dd::onePoleCoef (0.09f, sr);
        holdLen = (int) (0.04 * sr);
        reset();
    }

    void reset() { env = 0.0f; gain = 1.0f; hold = 0; }

    void process (float& l, float& r, float thresholdDb) noexcept
    {
        if (thresholdDb <= -79.5f)
        {
            gain = 1.0f;
            return;
        }
        const float x = std::max (std::abs (l), std::abs (r));
        env = x + (env - x) * (x > env ? envAtt : envRel);
        const float db = dd::gainToDb (env);
        float target = 1.0f;
        if (db >= thresholdDb)
            hold = holdLen;
        else if (hold > 0)
            --hold;
        else
            target = dd::dbToGain (std::max (-40.0f, (db - thresholdDb) * 2.0f));
        gain = target + (gain - target) * (target > gain ? open : close);
        l *= gain;
        r *= gain;
    }

private:
    double sr = 48000.0;
    float env = 0.0f, gain = 1.0f, envAtt = 0.0f, envRel = 0.0f, open = 0.0f, close = 0.0f;
    int hold = 0, holdLen = 1;
};

//==============================================================================
/** Two extra "takes" a few ms late with their own slow pitch and timing drift, panned apart (ADT). */
class Doubler
{
public:
    void prepare (double newSr)
    {
        sr = newSr;
        for (auto& d : lines) d.prepare ((int) (0.06 * sr));
        for (int i = 0; i < 2; ++i)
        {
            drift[i].prepare (sr, 0.6f + 0.35f * (float) i);
            tone[i].setCutoff (8500.0f, sr);
        }
        rng.seed (99);
        reset();
    }

    void reset()
    {
        for (auto& d : lines) d.reset();
        for (auto& t : tone) t.reset();
        for (auto& d : drift) d.reset (rng);
    }

    void process (float inL, float inR, float& outL, float& outR, float width) noexcept
    {
        const float m = 0.5f * (inL + inR);
        static constexpr float baseMs[2] = { 19.0f, 31.0f };
        float v[2];
        for (int i = 0; i < 2; ++i)
        {
            lines[i].push (m);
            const float ms = baseMs[i] + 3.0f * drift[i].process (rng);
            v[i] = tone[i].lp (lines[i].read (ms * 0.001f * (float) sr));
        }
        const float a = 0.5f + 0.5f * width, b = 0.5f - 0.5f * width;
        outL = v[0] * a + v[1] * b;
        outR = v[1] * a + v[0] * b;
    }

private:
    double sr = 48000.0;
    dd::DelayBuffer lines[2];
    dd::Drift drift[2];
    dd::OnePole tone[2];
    dd::Rng rng;
};

//==============================================================================
/**
    16-band channel vocoder (Roland VP-330 style). The carrier is played at the corrected sung pitch
    (plus chord notes from the key and scale), so it follows the singer without MIDI; noise takes
    over on unvoiced sounds so consonants stay intelligible.
*/
class Vocoder
{
public:
    static constexpr int kBands = 16;

    void prepare (double newSr)
    {
        sr = newSr;
        for (int b = 0; b < kBands; ++b)
        {
            const float centre = 150.0f * std::pow (7200.0f / 150.0f, (float) b / (float) (kBands - 1));
            for (int s = 0; s < 2; ++s)
            {
                setBandPass (mod[b][s], centre, 5.0f, sr);
                setBandPass (car[b][s], centre, 5.0f, sr);
            }
            bandGain[(size_t) b] = std::sqrt (centre / 600.0f) * 0.9f + 0.4f;
        }
        att = dd::onePoleCoef (0.002f, sr);
        rel = dd::onePoleCoef (0.03f, sr);
        glide = dd::onePoleCoef (0.012f, sr);
        vibrato.prepare (sr, 4.5f);
        reset();
    }

    void reset()
    {
        for (auto& bb : mod) for (auto& f : bb) f.reset();
        for (auto& bb : car) for (auto& f : bb) f.reset();
        env.fill (0.0f);
        for (auto& o : osc) o.reset (rng.uni());
        hz = 0.0f;
        unvoicedMix = 1.0f;
    }

    /** One sample. `pitchHz` <= 0 when unvoiced; intervals are semitones above the sung note. */
    float process (float in, float pitchHz, int carrier, const std::array<int, 4>& intervals, int numNotes) noexcept
    {
        const bool voiced = pitchHz > 0.0f;
        if (voiced)
            hz = hz <= 0.0f ? pitchHz : pitchHz + (hz - pitchHz) * glide;
        unvoicedMix += ((voiced ? 0.08f : 1.0f) - unvoicedMix) * 0.002f;

        float c = 0.0f;
        if (hz > 0.0f)
        {
            const float vib = carrier == 2 ? 0.004f * vibrato.process (rng) : 0.0f;
            int o = 0;
            for (int k = 0; k < numNotes; ++k)
            {
                const float f = hz * dd::semitonesToRatio ((float) intervals[(size_t) k]) * (1.0f + vib);
                switch (carrier)
                {
                    case 1:   c += osc[(size_t) o++].process (f / (float) sr, dd::Wave::Thin); break;   // robot: narrow pulse
                    case 3:   // brass: saw plus a slightly sharp square
                        c += osc[(size_t) o].process (f / (float) sr, dd::Wave::Saw);
                        c += 0.6f * osc[(size_t) o + 1].process (f * 1.003f / (float) sr, dd::Wave::Square);
                        o += 2;
                        break;
                    default:  // strings / choir: three detuned saws
                        for (float det : { 0.994f, 1.0f, 1.006f })
                            c += 0.6f * osc[(size_t) o++].process (f * det / (float) sr, dd::Wave::Saw);
                        break;
                }
            }
            c /= std::sqrt ((float) std::max (1, numNotes));
        }
        c = c * (1.0f - unvoicedMix) + rng.bi() * unvoicedMix * 0.9f;

        float out = 0.0f;
        for (int b = 0; b < kBands; ++b)
        {
            const float m = mod[b][1].process (mod[b][0].process (in));
            const float a = std::abs (m);
            env[(size_t) b] = a + (env[(size_t) b] - a) * (a > env[(size_t) b] ? att : rel);
            const float cb = car[b][1].process (car[b][0].process (c));
            out += cb * env[(size_t) b] * bandGain[(size_t) b];
        }
        return out * 4.0f;
    }

private:
    double sr = 48000.0;
    dd::Biquad mod[kBands][2], car[kBands][2];
    std::array<float, kBands> env {}, bandGain {};
    std::array<dd::BlepOscillator, 16> osc {};
    dd::Drift vibrato;
    dd::Rng rng;
    float att = 0.0f, rel = 0.0f, glide = 0.0f, hz = 0.0f, unvoicedMix = 1.0f;
};

//==============================================================================
/**
    Vocal rider: rides the level of the performance toward a steady loudness before the compressor,
    the way an engineer rides a fader (slow, about a third of a second; never pumps). Holds still in
    the gaps so it doesn't pull up breaths and room noise.
*/
class Rider
{
public:
    void prepare (double newSr)
    {
        sr = newSr;
        rmsCoef = dd::onePoleCoef (0.3f, sr);
        moveCoef = dd::onePoleCoef (0.25f, sr);
        reset();
    }
    void reset() { ms = 0.0f; gainDb = 0.0f; }

    void process (float& l, float& r, float amount) noexcept
    {
        if (amount <= 0.001f)
        {
            gainDb *= 0.999f;
            if (std::abs (gainDb) > 0.01f) { const float g = dd::dbToGain (gainDb); l *= g; r *= g; }
            return;
        }
        const float x2 = 0.5f * (l * l + r * r);
        ms = x2 + (ms - x2) * rmsCoef;
        const float levelDb = 10.0f * std::log10 (ms + 1.0e-12f);
        if (levelDb > -50.0f)
        {
            const float want = juce::jlimit (-12.0f, 12.0f, -20.0f - levelDb) * amount;
            gainDb = want + (gainDb - want) * moveCoef;
        }
        const float g = dd::dbToGain (gainDb);
        l *= g;
        r *= g;
    }

    float getGainDb() const noexcept { return gainDb; }

private:
    double sr = 48000.0;
    float ms = 0.0f, gainDb = 0.0f, rmsCoef = 0.0f, moveCoef = 0.0f;
};

//==============================================================================
/**
    Transparent lookahead true-peak limiter (ceiling -1 dBTP). It watches the waveform between samples
    (4x polyphase interpolation, as BS.1770 meters do), holds the lowest gain any peak in the lookahead
    window needs, and ramps into it with a moving average timed to arrive exactly with the peak, so it never
    overshoots; release is smooth. drive pushes the voice into it, 0..12 dB.
*/
class Limiter
{
public:
    void prepare (double newSr)
    {
        sr = newSr;
        look = std::max (8, (int) std::lround (0.0015 * sr));
        hold = look + kTaps;                    // also covers the interpolator's own delay
        delaySamples = look + kTaps / 2 + 1;
        for (auto& d : delay) d.prepare (delaySamples + 8);
        need.assign ((size_t) hold, 1.0f);
        box.assign ((size_t) look, 1.0f);
        release = 1.0f - std::exp (-1.0f / (0.06f * (float) sr));
        // Kaiser-windowed sinc, 97 taps (odd, so the phases sit exactly at 1/4, 1/2 and 3/4 of a sample;
        // long enough to stay accurate right up to Nyquist, where bright vocals peak between samples).
        const int total = 4 * (kTaps - 1) + 1;
        const double centre = (total - 1) * 0.5, beta = 6.0;
        for (auto& ph : phase) ph.fill (0.0f);
        auto bessel0 = [] (double x) { double sum = 1.0, term = 1.0; for (int k = 1; k < 25; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; } return sum; };
        for (int n = 0; n < total; ++n)
        {
            const double t = ((double) n - centre) / 4.0;
            const double sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
            const double r = ((double) n - centre) / centre;
            const double w = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
            phase[(size_t) (n % 4)][(size_t) (n / 4)] = (float) (sinc * w);
        }
        reset();
    }

    void reset()
    {
        for (auto& d : delay) d.reset();
        std::fill (need.begin(), need.end(), 1.0f);
        std::fill (box.begin(), box.end(), 1.0f);
        for (auto& h : hist) h.fill (0.0f);
        boxSum = (double) look;
        follow = gain = 1.0f;
        needIdx = boxIdx = hpos = 0;
    }

    int getLatency() const noexcept { return delaySamples; }

    void process (float& l, float& r, float drive) noexcept
    {
        constexpr float ceiling = 0.891f;
        const float in = dd::dbToGain (drive * 12.0f);
        const float xl = l * in, xr = r * in;
        delay[0].push (xl);
        delay[1].push (xr);

        hpos = (hpos + 1) % kTaps;
        hist[0][(size_t) hpos] = xl;
        hist[1][(size_t) hpos] = xr;
        float peak = std::max (std::abs (xl), std::abs (xr));
        for (size_t ch = 0; ch < 2; ++ch)
            for (size_t ph = 1; ph < 4; ++ph)
            {
                float acc = 0.0f;
                for (int k = 0; k < kTaps; ++k)
                    acc += phase[ph][(size_t) k] * hist[ch][(size_t) ((hpos - k + kTaps) % kTaps)];
                peak = std::max (peak, std::abs (acc));
            }
        need[(size_t) needIdx] = peak > ceiling ? ceiling / peak : 1.0f;
        needIdx = (needIdx + 1) % hold;
        float lowest = 1.0f;
        for (float v : need) lowest = std::min (lowest, v);

        // Instant attack on the held minimum, smooth release; then the moving average shapes the ramp.
        follow = lowest < follow ? lowest : follow + (lowest - follow) * release;
        boxSum += (double) follow - (double) box[(size_t) boxIdx];
        box[(size_t) boxIdx] = follow;
        boxIdx = (boxIdx + 1) % look;
        gain = (float) (boxSum / (double) look);

        l = delay[0].readInt (delaySamples + 1) * gain;
        r = delay[1].readInt (delaySamples + 1) * gain;
    }

    float getGainReductionDb() const noexcept { return -dd::gainToDb (gain); }

private:
    static constexpr int kTaps = 25;   // per phase (97-tap windowed-sinc interpolator)
    double sr = 48000.0, boxSum = 0.0;
    int look = 72, hold = 97, delaySamples = 85, needIdx = 0, boxIdx = 0, hpos = 0;
    dd::DelayBuffer delay[2];
    std::vector<float> need, box;
    std::array<std::array<float, kTaps>, 2> hist {};
    std::array<std::array<float, kTaps>, 4> phase {};
    float follow = 1.0f, gain = 1.0f, release = 0.001f;
};

//==============================================================================
inline const juce::StringArray& dimensionModeNames()
{
    static const juce::StringArray n { "Off", "1", "2", "3", "4" };
    return n;
}

/**
    Chorus (a CE-5 style stereo chorus: rate and depth) then Dimension (the Roland SDD-320 idea: two dark
    bucket-brigade-like lines swept in opposite phase and cross-mixed, so the voice widens without an
    obvious wobble; modes 1-4 from subtle to deep). Either can be used alone.
*/
class Dimension
{
public:
    void prepare (double newSr)
    {
        sr = newSr;
        for (auto& d : lines) d.prepare ((int) (0.03 * sr));
        for (auto& f : dark) f.setCutoff (8500.0f, sr);
        reset();
    }
    void reset() { for (auto& d : lines) d.reset(); for (auto& f : dark) f.reset(); chPhase = dimPhase = 0.0; }

    void process (float& l, float& r, float rateHz, float depth, float chorusMix, int dimMode, float dimMix) noexcept
    {
        lines[0].push (l);
        lines[1].push (r);
        if (chorusMix > 0.001f)
        {
            chPhase += rateHz / sr;
            if (chPhase >= 1.0) chPhase -= 1.0;
            const float mL = std::sin (dd::kTwoPi * (float) chPhase), mR = std::sin (dd::kTwoPi * (float) (chPhase + 0.25));
            const float base = 0.007f * (float) sr, span = (0.0005f + depth * 0.006f) * (float) sr;
            l += dark[0].lp (lines[0].read (base + span * (0.5f + 0.5f * mL))) * chorusMix;
            r += dark[1].lp (lines[1].read (base + span * (0.5f + 0.5f * mR))) * chorusMix;
        }
        const float mono = 0.5f * (l + r);
        lines[2].push (mono);
        lines[3].push (mono);
        if (dimMode > 0 && dimMix > 0.001f)
        {
            static constexpr float rates[4] { 0.22f, 0.28f, 0.42f, 0.5f }, depths[4] { 0.35f, 0.55f, 0.75f, 1.0f };
            const int m = juce::jlimit (0, 3, dimMode - 1);
            dimPhase += rates[m] / sr;
            if (dimPhase >= 1.0) dimPhase -= 1.0;
            auto tri = [] (double p) { const float x = (float) (p - std::floor (p)); return x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x; };
            const float base = 0.0065f * (float) sr, span = 0.0016f * depths[m] * (float) sr;
            const float d1 = dark[2].lp (lines[2].read (base + span * tri (dimPhase)));
            const float d2 = dark[3].lp (lines[3].read (base + span * tri (dimPhase + 0.5)));
            l += (0.8f * d1 - 0.35f * d2) * dimMix;
            r += (0.8f * d2 - 0.35f * d1) * dimMix;
        }
    }

private:
    double sr = 48000.0, chPhase = 0.0, dimPhase = 0.0;
    dd::DelayBuffer lines[4];
    dd::OnePole dark[4];
};

//==============================================================================
/**
    The dry path for the Mix knob, delayed through the same structure as the wet chain (the preamp's
    4x and the air's 2x oversamplers, bypassed, with the compressor's and smoother's delays between), so
    dry and wet line up in phase at every frequency and any blend is clean.
*/
class DryAlign
{
public:
    void prepare (int maxBlock, int middleDelay)
    {
        os4.initProcessing ((size_t) maxBlock);
        os2.initProcessing ((size_t) maxBlock);
        delay = middleDelay;
        for (auto& d : lines) d.prepare (delay + 8);
        reset();
    }
    void reset() { os4.reset(); os2.reset(); for (auto& d : lines) d.reset(); }

    void process (juce::dsp::AudioBlock<float>& block) noexcept
    {
        auto up = os4.processSamplesUp (block);
        juce::ignoreUnused (up);
        os4.processSamplesDown (block);
        for (size_t ch = 0; ch < block.getNumChannels() && ch < 2; ++ch)
        {
            auto* d = block.getChannelPointer (ch);
            for (size_t i = 0; i < block.getNumSamples(); ++i)
            {
                lines[ch].push (d[i]);
                d[i] = lines[ch].readInt (delay + 1);
            }
        }
        auto up2 = os2.processSamplesUp (block);
        juce::ignoreUnused (up2);
        os2.processSamplesDown (block);
    }

private:
    using OS = juce::dsp::Oversampling<float>;
    OS os4 { 2, 2, OS::filterHalfBandFIREquiripple, true, true };
    OS os2 { 2, 1, OS::filterHalfBandFIREquiripple, true, true };
    dd::DelayBuffer lines[2];
    int delay = 0;
};
} // namespace aircheck
