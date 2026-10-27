#pragma once

#include <dd_dsp/dd_dsp.h>

namespace ag
{
enum class Source { Analog, Strings, Choir, Glass, Tines, Air, Sine, Pluck, Organ, Mallet, Sync, Electric };
constexpr int kNumSources = 12;

/** One layer's settings for a block, with the macros and global modulation already applied. */
struct LayerParams
{
    bool on = false;
    Source source = Source::Analog;
    float level = 0.0f;
    float offset = 0.0f;        // semitones: octave + semi + fine
    int voices = 1;
    float detune = 0.0f;        // cents across the whole unison stack
    float spread = 0.5f;
    float shape = 0.3f;         // source-specific, see Layer
    float character = 0.5f;     // source-specific, see Layer
    float shapeEnv = 0.0f;      // mod envelope -> shape
    float shapeOffset = 0.0f;   // global modulation (LFO 2) -> shape
    float pan = 0.0f;
    float drift = 0.2f;
};

/**
    One sound source inside a voice, up to 7 unison copies spread across the stereo field.

      Analog   saw that morphs into a slowly width-modulated pulse (shape); char = PWM depth (Juno / Prophet)
      Strings  saw 8' + saw 4' (char) through a string-machine band (shape = brightness): Solina, Juno strings
      Choir    saw + pulse through three vowel formants, shape morphs A-E-I-O-U, char = breath
      Glass    two-operator FM: shape = index, char picks the ratio (bells, glass, metal)
      Tines    electric piano: 1:1 FM with a decaying index (shape = bark) plus a tine partial (char)
      Air      noise through a resonant band at the note: char = how pitched, shape = breath blend
      Sine     sine into triangle (shape), char adds the octave
      Pluck    plucked string (Karplus-Strong): shape = brightness, char = how long it rings (harp, nylon, koto)
      Organ    nine drawbars: shape sweeps flute > jazz > gospel > reed > full, char = percussion and key click
      Mallet   struck bar (modal): shape = mallet hardness, char picks the bar (marimba, vibes, kalimba, glock, steel)
      Sync     hard-sync saw: shape = slave ratio (sweep it with Shape Env), char blends the master saw back in
      Electric electric guitar string: picked near the bridge, heard through a magnetic pickup. shape = pickup
               (0 neck, warm and round .. 1 bridge, thin and bright), char = sustain (0 palm-muted .. 1 singing)
*/
class Layer
{
public:
    static constexpr int kMaxUnison = 7;

    void prepare (double sampleRate, uint32_t seed)
    {
        sr = sampleRate;
        rng.seed (seed);
        for (auto& d : drift)
            d.prepare (sr, 0.25f + 0.2f * rng.uni());
        for (auto& f : breathLp)
            f.setCutoff (2600.0f, sr);

        // String delay lines long enough for A0 at this sample rate.
        int len = 64;
        while (len < (int) (sr / 27.5) + 4)
            len <<= 1;
        if ((int) strings[0].size() != len)
            for (auto& s : strings)
                s.assign ((size_t) len, 0.0f);
        stringMask = len - 1;
        dcCoef = 1.0f - dd::kTwoPi * 8.0f / (float) sr;
        configured = -1;
    }

    /** fresh = the voice was silent, so a plucked string starts from rest instead of being re-plucked. */
    void start (const LayerParams& p, float vel, bool fresh) noexcept
    {
        velocity = vel;
        for (int i = 0; i < kMaxUnison; ++i)
        {
            // Free-running phases: chord notes never line up into one spike at the attack (the
            // envelope's attack keeps the start click-free).
            const float ph = rng.uni();
            phase[(size_t) i] = ph;
            phase2[(size_t) i] = ph;
            phase3[(size_t) i] = ph;
            phase4[(size_t) i] = ph;
            pwm[(size_t) i] = rng.uni();
            drift[(size_t) i].reset (rng);
            airBp[(size_t) i].reset();
            excite[(size_t) i] = -1;   // a pluck excites one period once the pitch is known
            syncPrev[(size_t) i] = 0.0f;
        }
        if ((p.source == Source::Pluck || p.source == Source::Electric) && fresh)
        {
            for (auto& s : strings)
                std::fill (s.begin(), s.end(), 0.0f);
            std::fill (stringZ.begin(), stringZ.end(), 0.0f);
            std::fill (dcX.begin(), dcX.end(), 0.0f);
            std::fill (dcY.begin(), dcY.end(), 0.0f);
            std::fill (apX.begin(), apX.end(), 0.0f);
            std::fill (apY.begin(), apY.end(), 0.0f);
        }
        if (p.source == Source::Electric)
        {
            // Every pick lands a little differently; a string starts a few cents sharp and settles.
            for (auto& b : pickPos)
                b = 0.1f + 0.07f * rng.uni();
            twang = (3.0f + 9.0f * vel) / 1200.0f;
            twangCoef = dd::onePoleCoef (0.045f, sr);
            for (auto& f : pickupLp)
                f.reset();
            pickupDc[0] = pickupDc[1] = 0.0f;
        }
        for (auto& f : formant) for (auto& s : f) s.reset();
        for (auto& f : toneLp) f.reset();
        for (auto& f : toneHp) f.reset();
        indexEnv = 1.0f;
        tineEnv = 1.0f;
        clickEnv = 1.0f;
        modalReady = false;
        // Tines: harder hits keep the bark longer.
        indexCoef = dd::onePoleCoef (0.25f + 0.9f * (1.0f - p.shape) * (0.6f + 0.4f * vel), sr);
        tineCoef = dd::onePoleCoef (p.source == Source::Organ ? 0.22f : 0.045f, sr);
        clickCoef = dd::onePoleCoef (p.source == Source::Mallet ? 0.0025f : (p.source == Source::Electric ? 0.0009f : 0.0012f), sr);
        configure (p);
    }

    /** Adds n samples into L/R. hz = note frequency per sample, env = mod envelope, morph = LFO 1 -> shape. */
    void render (const LayerParams& p, const float* hz, const float* env, const float* morph, float* L, float* R, int n) noexcept
    {
        if (! p.on || p.level <= 0.0001f)
            return;
        configure (p);
        const int nv = voices;

        // Drift, PWM and the tone filters move slowly: update once per block.
        for (int v = 0; v < nv; ++v)
        {
            driftRatio[(size_t) v] = std::exp2 (drift[(size_t) v].process (rng) * p.drift * 9.0f / 1200.0f);
            auto& pw = pwm[(size_t) v];
            pw += (0.31f + 0.05f * (float) v) * (float) n / (float) sr;
            pw -= std::floor (pw);
            pulseWidth[(size_t) v] = 0.5f - 0.42f * p.character * (0.5f + 0.5f * dd::sine01 (pw));
        }
        if (p.source == Source::Strings)
            for (auto& f : toneLp) f.setCutoff (1800.0f + p.shape * 9000.0f, sr);
        if (p.source == Source::Air)
        {
            const float q = 1.5f + p.character * p.character * 60.0f;
            for (int v = 0; v < nv; ++v)
                airBp[(size_t) v].set (std::min (hz[0] * ratio[(size_t) v], 0.45f * (float) sr), q, sr);
            airGain = std::sqrt (q) * std::sqrt (440.0f / std::max (40.0f, hz[0])) * 2.2f;
        }
        if (p.source == Source::Pluck)
        {
            // Ring time is per second, not per pass, so low and high strings sustain alike.
            pluckT60 = 0.35f * std::exp2 (p.character * 5.5f);
            pluckDamp = 0.5f - 0.46f * p.shape;
            for (int v = 0; v < nv; ++v)
                pluckGain[(size_t) v] = std::pow (0.001f, 1.0f / (std::max (20.0f, hz[0] * ratio[(size_t) v]) * pluckT60));
        }
        if (p.source == Source::Mallet && ! modalReady)
            setupModes (p, hz[0]);
        if (p.source == Source::Electric)
            configureElectric (p, hz[0] * ratio[0], nv);

        const float inv = 1.0f / (float) sr;
        for (int i = 0; i < n; ++i)
        {
            const float s = dd::clamp01 (p.shape + p.shapeEnv * env[i] + morph[i] + p.shapeOffset);
            if (p.source == Source::Organ)
                setDrawbars (s);
            else if (p.source == Source::Mallet)
                for (size_t k = 0; k < 4; ++k)
                    modeEnv[k] *= modeCoef[k];

            float l = 0.0f, r = 0.0f;
            for (int v = 0; v < nv; ++v)
            {
                const float dt = std::min (0.45f, hz[i] * ratio[(size_t) v] * driftRatio[(size_t) v] * inv);
                const float y = sample (p, v, dt, s);
                l += y * gainL[(size_t) v];
                r += y * gainR[(size_t) v];
            }

            if (p.source == Source::Choir)
            {
                l = vowel (0, l, s);
                r = vowel (1, r, s);
            }
            else if (p.source == Source::Strings)
            {
                l = toneLp[0].lp (toneHp[0].hp (l));
                r = toneLp[1].lp (toneHp[1].hp (r));
            }
            else if (p.source == Source::Electric)
            {
                // Pick scrape on the attack, then the pickup's coil resonance.
                const float pick = clickEnv * velocity * velocity * 0.35f * rng.bi();
                l = pickup (0, l + pick);
                r = pickup (1, r + pick);
                twang *= twangCoef;
            }
            if (p.source == Source::Organ || p.source == Source::Mallet || p.source == Source::Electric)
            {
                tineEnv *= tineCoef;
                clickEnv *= clickCoef;
            }

            L[i] += l * p.level * kSourceGain[(size_t) p.source];
            R[i] += r * p.level * kSourceGain[(size_t) p.source];
        }
    }

private:
    // Per-source gain so every source is equally loud (measured on the pads phrase, one unison voice).
    static constexpr std::array<float, kNumSources> kSourceGain { 0.14f, 0.17f, 0.29f, 0.13f, 0.14f, 0.61f, 0.135f,
                                                                  0.47f, 0.135f, 0.147f, 0.165f, 0.36f };

    static float saw (float t, float dt) noexcept { return 2.0f * t - 1.0f - dd::polyBlep (t, dt); }

    static void advance (float& ph, float dt) noexcept
    {
        ph += dt;
        if (ph >= 1.0f) ph -= 1.0f;
    }

    static float frac (float x) noexcept { return x - std::floor (x); }

    float sample (const LayerParams& p, int v, float dt, float s) noexcept
    {
        auto& ph = phase[(size_t) v];
        float y = 0.0f;
        switch (p.source)
        {
            case Source::Analog:
            {
                float t2 = ph + pulseWidth[(size_t) v];
                if (t2 >= 1.0f) t2 -= 1.0f;
                const float sw = saw (ph, dt);
                const float pulse = sw - saw (t2, dt);
                y = sw + s * (pulse - sw);
                advance (ph, dt);
                break;
            }
            case Source::Strings:
            {
                auto& ph2 = phase2[(size_t) v];
                y = saw (ph, dt) * (1.0f - 0.45f * p.character) + saw (ph2, std::min (0.45f, dt * 2.0f)) * p.character * 0.8f;
                advance (ph, dt);
                advance (ph2, std::min (0.45f, dt * 2.0f));
                break;
            }
            case Source::Choir:
            {
                float t2 = ph + 0.3f;
                if (t2 >= 1.0f) t2 -= 1.0f;
                const float sw = saw (ph, dt);
                y = 0.65f * sw + 0.35f * (sw - saw (t2, dt)) + p.character * 0.5f * rng.bi();
                advance (ph, dt);
                break;
            }
            case Source::Glass:
            {
                static constexpr float ratios[] = { 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 3.5f, 4.0f, 5.0f, 7.0f, 9.0f };
                const float ratioM = ratios[std::min (9, (int) (p.character * 9.99f))];
                auto& pm = phase2[(size_t) v];
                const float index = s * 1.1f;   // in turns: ~7 radians at full shape
                y = dd::sine01 (ph + index * dd::sine01 (pm));
                advance (ph, dt);
                pm += dt * ratioM;
                pm -= std::floor (pm);
                break;
            }
            case Source::Tines:
            {
                if (v == 0)
                {
                    indexEnv *= indexCoef;
                    tineEnv *= tineCoef;
                }
                auto& pm = phase2[(size_t) v];
                auto& pt = phase3[(size_t) v];
                const float index = (0.06f + s * 0.42f) * (0.3f + 0.7f * indexEnv) * (0.5f + 0.5f * velocity);
                y = dd::sine01 (ph + index * dd::sine01 (pm)) + p.character * 0.5f * tineEnv * dd::sine01 (pt);
                advance (ph, dt);
                advance (pm, dt);
                pt += dt * 14.0f;
                pt -= std::floor (pt);
                break;
            }
            case Source::Air:
            {
                const float white = rng.bi();
                float lp, bp, hp;
                airBp[(size_t) v].process (white, lp, bp, hp);
                const float band = bp * airBp[(size_t) v].k * airGain;
                y = band + s * (breathLp[(size_t) v].lp (white) * 0.55f - band);
                juce::ignoreUnused (lp, hp);
                break;
            }
            case Source::Sine:
            {
                const float t = ph;
                const float tri = t < 0.25f ? 4.0f * t : (t < 0.75f ? 2.0f - 4.0f * t : 4.0f * t - 4.0f);
                y = dd::sine01 (t) + s * (tri - dd::sine01 (t)) + p.character * 0.45f * dd::sine01 (2.0f * t);
                advance (ph, dt);
                break;
            }
            case Source::Pluck:   y = pluck (v, dt, s); break;
            case Source::Organ:   y = organ (v, dt, p.character); break;
            case Source::Mallet:  y = mallet (v, dt, s); break;
            case Source::Sync:    y = sync (v, dt, s, p.character); break;
            case Source::Electric: y = electric (v, dt); break;
        }
        return y;
    }

    /** Karplus-Strong: a burst of noise one period long circulating through a delay line and a damping filter. */
    float pluck (int v, float dt, float s) noexcept
    {
        auto& line = strings[(size_t) v];
        auto& w = stringWrite[(size_t) v];
        const float period = std::min ((float) (stringMask - 2), 1.0f / dt);
        float x = 0.0f;
        if (excite[(size_t) v] < 0)
        {
            excite[(size_t) v] = (int) period;
            exciteLp[(size_t) v] = exciteLp2[(size_t) v] = 0.0f;
        }
        if (excite[(size_t) v] > 0)
        {
            // Brighter picks for brighter strings and harder hits.
            // Two poles keep the pick soft; 1/sqrt(a) keeps dark picks as loud as bright ones.
            const float a = 0.06f + 0.9f * s * (0.55f + 0.45f * velocity);
            exciteLp[(size_t) v] += a * (rng.bi() - exciteLp[(size_t) v]);
            exciteLp2[(size_t) v] += a * (exciteLp[(size_t) v] - exciteLp2[(size_t) v]);
            x = exciteLp2[(size_t) v] * (0.4f + 0.6f * velocity) * 0.8f / std::sqrt (a);
            --excite[(size_t) v];
        }

        // The loop filter averages two samples (delay = damp); the line supplies the rest of the period.
        const float damp = pluckDamp;
        const float d = std::max (1.0f, period - damp);
        const int di = (int) d;
        const float df = d - (float) di;
        const float d0 = line[(size_t) ((w - di) & stringMask)];
        const float d1 = line[(size_t) ((w - di - 1) & stringMask)];
        const float delayed = d0 + df * (d1 - d0);
        auto& z = stringZ[(size_t) v];
        const float filtered = (1.0f - damp) * delayed + damp * z;
        z = delayed;
        const float out = filtered * pluckGain[(size_t) v] + x;
        line[(size_t) (w & stringMask)] = out;
        w = (w + 1) & stringMask;

        const float y = out - dcX[(size_t) v] + dcCoef * dcY[(size_t) v];
        dcX[(size_t) v] = out;
        dcY[(size_t) v] = y;
        return y;
    }

    /**
        Electric guitar string (extended Karplus-Strong): a triangle-shaped pluck written straight into the
        string, an allpass for exact tuning, a gentle loss filter, and the output read through a comb at the
        pickup's position along the string, so neck and bridge pickups lose different harmonics like the
        real thing.
    */
    float electric (int v, float dt) noexcept
    {
        auto& line = strings[(size_t) v];
        auto& w = stringWrite[(size_t) v];
        const float period = juce::jlimit (4.0f, (float) (stringMask - 4), 1.0f / (dt * (1.0f + twang * 0.69315f)));
        if (excite[(size_t) v] < 0)
        {
            pickString (v, period);
            excite[(size_t) v] = 0;
        }

        // Loop delay = integer line delay + allpass (0.1..1.1 samples) + loss filter (elecDamp samples).
        const float len = period - elecDamp;
        const int di = (int) (len - 0.1f);
        const float frac = len - (float) di;
        const float c = (1.0f - frac) / (1.0f + frac);
        const float x = line[(size_t) ((w - di) & stringMask)];
        const float ap = c * x + apX[(size_t) v] - c * apY[(size_t) v];
        apX[(size_t) v] = x;
        apY[(size_t) v] = ap;
        auto& z = stringZ[(size_t) v];
        const float out = ((1.0f - elecDamp) * ap + elecDamp * z) * pluckGain[(size_t) v];
        z = ap;
        line[(size_t) (w & stringMask)] = out;

        const int m = std::max (1, (int) (pickupPos * period + 0.5f));
        const float y = out - 0.92f * line[(size_t) ((w - m) & stringMask)];
        w = (w + 1) & stringMask;
        return y;
    }

    /** The pick: a triangle displacement peaking at the pick point, a little noise for the plectrum's edge. */
    void pickString (int v, float period) noexcept
    {
        auto& line = strings[(size_t) v];
        const int len = (int) period;
        const int w = stringWrite[(size_t) v];
        const float beta = pickPos[(size_t) v];
        const float amp = 0.35f + 0.65f * velocity;
        const float grit = 0.08f + 0.22f * velocity;
        const float soft = 0.25f + 0.75f * velocity;   // soft touches round the corner of the pluck
        const auto shape = [&] (dd::Rng& r, float& lp, int k)
        {
            const float u = (float) k / (float) len;
            const float tri = u < beta ? u / beta : (1.0f - u) / (1.0f - beta);
            lp += soft * ((tri + grit * r.bi()) - lp);
            return lp;
        };

        // Two passes over the same noise (a copy of the generator): the first finds the mean so the
        // pluck carries no DC, the second writes it. A ringing string is damped by the pick first.
        auto replay = rng;
        float mean = 0.0f, lp = 0.0f;
        for (int k = 0; k < len; ++k)
            mean += shape (replay, lp, k);
        mean /= (float) len;
        lp = 0.0f;
        for (int k = 0; k < len; ++k)
        {
            auto& slot = line[(size_t) ((w - len + k) & stringMask)];
            slot = slot * 0.3f + (shape (rng, lp, k) - mean) * amp;
        }
    }

    void configureElectric (const LayerParams& p, float noteHz, int nv) noexcept
    {
        // Sustain: palm-muted thud (0.12 s, dull) .. normal ring .. singing (13 s); high notes die sooner.
        const float sustain = p.character;
        const float t60 = 0.12f * std::exp2 (sustain * 6.5f) * std::pow (196.0f / juce::jlimit (60.0f, 2000.0f, noteHz), 0.4f);
        const float mute = juce::jlimit (0.0f, 1.0f, (0.3f - sustain) / 0.3f);
        elecDamp = 0.12f + 0.36f * mute * mute;
        for (int v = 0; v < nv; ++v)
            pluckGain[(size_t) v] = std::pow (0.001f, 1.0f / (std::max (20.0f, noteHz * ratio[(size_t) v] / ratio[0]) * t60));

        // Pickup: neck (a quarter of the string from the bridge, humbucker resonance ~2.6 kHz) to bridge
        // (6 %, single-coil bite ~4.2 kHz).
        const float sel = p.shape;
        pickupPos = 0.25f - 0.19f * sel;
        const float fc = 2600.0f * std::exp2 (sel * 0.7f);
        const float q = 1.4f + 1.0f * sel;
        if (std::abs (fc - pickupFc) > 1.0f)
        {
            pickupFc = fc;
            for (auto& f : pickupLp)
                f.set (std::min (fc, 0.45f * (float) sr), q, sr);
        }
        // The comb thins the low end more at the bridge; level it so the pickup switch is not a volume knob.
        pickupGain = 1.0f / (0.35f + 2.6f * pickupPos);
    }

    float pickup (int ch, float x) noexcept
    {
        float lp, bp, hp;
        pickupLp[(size_t) ch].process (x, lp, bp, hp);
        juce::ignoreUnused (bp, hp);
        // DC-free: the comb removes DC from the string, this guards the pick noise.
        const float y = lp * pickupGain;
        pickupDc[ch] += 0.0005f * (y - pickupDc[ch]);
        return y - pickupDc[ch];
    }

    /** Drawbar organ: every footage is an integer multiple of a phase running at half the note (the 16'). */
    float organ (int v, float dt, float character) noexcept
    {
        static constexpr float mult[9] = { 1, 3, 2, 4, 6, 8, 10, 12, 16 };   // 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1'
        auto& ph = phase[(size_t) v];
        const float half = dt * 0.5f;
        float y = 0.0f;
        for (int k = 0; k < 9; ++k)
            if (half * mult[k] < 0.45f && drawbar[(size_t) k] > 0.001f)
                y += drawbar[(size_t) k] * dd::sine01 (frac (ph * mult[k]));
        y *= drawbarNorm;
        // Percussion on the third harmonic and a short key click, both decaying from the key press.
        if (half * 6.0f < 0.45f)
            y += character * 0.7f * tineEnv * dd::sine01 (frac (ph * 6.0f));
        y += character * 0.25f * clickEnv * rng.bi();
        advance (ph, half);
        return y;
    }

    void setDrawbars (float s) noexcept
    {
        static constexpr float reg[5][9] = {
            { 0.0f, 0.0f, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },     // flute
            { 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },     // jazz 888
            { 1.0f, 0.6f, 1.0f, 0.8f, 0.3f, 0.5f, 0.0f, 0.0f, 0.4f },     // gospel
            { 0.0f, 0.0f, 1.0f, 0.0f, 0.85f, 0.0f, 0.7f, 0.0f, 0.0f },    // reed: odd harmonics only
            { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } };   // full
        if (std::abs (s - lastDrawbar) < 0.002f)
            return;
        lastDrawbar = s;
        const float pos = s * 3.999f;
        const int i0 = (int) pos;
        const float fr = pos - (float) i0;
        float power = 0.0f;
        for (size_t k = 0; k < 9; ++k)
        {
            drawbar[k] = reg[i0][k] + fr * (reg[i0 + 1][k] - reg[i0][k]);
            power += drawbar[k] * drawbar[k];
        }
        drawbarNorm = 1.0f / std::sqrt (power + 0.01f);
    }

    /** Struck bar: four decaying partials at the bar's ratios, plus the mallet's knock. */
    float mallet (int v, float dt, float s) noexcept
    {
        std::array<float*, 4> ph { &phase[(size_t) v], &phase2[(size_t) v], &phase3[(size_t) v], &phase4[(size_t) v] };
        const float hard = (0.15f + 1.7f * s) * (0.5f + 0.5f * velocity);
        float y = 0.0f;
        for (size_t k = 0; k < 4; ++k)
        {
            const float pdt = dt * modeRatio[k];
            if (pdt >= 0.45f)
                continue;
            const float amp = modeAmp[k] * (k == 0 ? 1.0f : std::min (1.6f, hard));
            y += amp * modeEnv[k] * dd::sine01 (*ph[k]);
            *ph[k] += pdt;
            *ph[k] -= std::floor (*ph[k]);
        }
        y += s * 0.6f * clickEnv * rng.bi() * velocity;
        return y;
    }

    void setupModes (const LayerParams& p, float noteHz) noexcept
    {
        struct Bar { float ratio[4], amp[4], decay[4]; };
        static constexpr Bar bars[5] = {
            { { 1.0f, 3.99f, 10.65f, 0.0f }, { 1.0f, 0.35f, 0.12f, 0.0f }, { 1.2f, 0.35f, 0.12f, 0.1f } },     // marimba
            { { 1.0f, 3.98f, 9.9f, 2.0f },   { 1.0f, 0.3f, 0.1f, 0.05f },  { 4.5f, 1.2f, 0.4f, 1.0f } },       // vibraphone
            { { 1.0f, 5.93f, 13.0f, 0.0f },  { 1.0f, 0.25f, 0.1f, 0.0f },  { 2.2f, 0.25f, 0.1f, 0.1f } },      // kalimba
            { { 1.0f, 2.76f, 5.4f, 8.93f },  { 1.0f, 0.5f, 0.25f, 0.12f }, { 3.0f, 1.5f, 0.7f, 0.3f } },       // glockenspiel
            { { 1.0f, 2.0f, 3.0f, 4.2f },    { 1.0f, 0.5f, 0.3f, 0.15f },  { 2.6f, 1.8f, 1.0f, 0.5f } } };     // steel pan
        const auto& b = bars[std::min (4, (int) (p.character * 4.99f))];
        // Higher notes ring shorter, as real bars do.
        const float pitchScale = std::pow (440.0f / juce::jlimit (40.0f, 8000.0f, noteHz), 0.35f);
        for (size_t k = 0; k < 4; ++k)
        {
            modeRatio[k] = b.ratio[k] > 0.0f ? b.ratio[k] : 100.0f;   // unused modes sit above Nyquist
            modeAmp[k] = b.amp[k];
            modeEnv[k] = 1.0f;
            modeCoef[k] = dd::onePoleCoef (b.decay[k] * pitchScale, sr);
        }
        modalReady = true;
    }

    /** Hard sync with a one-sample-late band-limited step at every reset and wrap of the slave. */
    float sync (int v, float dt, float s, float character) noexcept
    {
        auto& ph = phase[(size_t) v];
        auto& ps = phase2[(size_t) v];
        const float sdt = std::min (0.45f, dt * (1.0f + s * 7.0f));
        ph += dt;
        ps += sdt;
        float jump = 0.0f, d = 0.0f;
        if (ph >= 1.0f)
        {
            ph -= 1.0f;
            d = ph / dt;
            const float atReset = frac (ps - d * sdt);
            jump = -2.0f * atReset;
            ps = d * sdt;
        }
        else if (ps >= 1.0f)
        {
            ps -= 1.0f;
            d = ps / sdt;
            jump = -2.0f;
        }
        // jump is 0 when nothing reset this sample, so both corrections vanish.
        const float out = syncPrev[(size_t) v] + 0.5f * jump * d * d;
        syncPrev[(size_t) v] = 2.0f * ps - 1.0f - 0.5f * jump * (1.0f - d) * (1.0f - d);
        return out * (1.0f - 0.4f * character) + character * 0.6f * saw (ph, dt);
    }

    /** Three parallel formant bands, vowel picked by s (A E I O U), alto formants. */
    float vowel (int ch, float x, float s) noexcept
    {
        static constexpr float f[5][3] = { { 800, 1150, 2800 }, { 400, 1600, 2700 }, { 350, 1700, 2700 },
                                           { 450, 800, 2830 }, { 325, 700, 2530 } };
        static constexpr float a[5][3] = { { 1.0f, 0.63f, 0.1f }, { 1.0f, 0.3f, 0.16f }, { 1.0f, 0.1f, 0.12f },
                                           { 1.0f, 0.36f, 0.08f }, { 1.0f, 0.25f, 0.05f } };
        const float pos = s * 3.999f;
        const int i0 = (int) pos;
        const float fr = pos - (float) i0;
        if (ch == 0 && std::abs (s - lastVowel) > 0.002f)
        {
            lastVowel = s;
            for (int k = 0; k < 3; ++k)
            {
                const float hz = f[i0][k] + fr * (f[i0 + 1][k] - f[i0][k]);
                formantAmp[(size_t) k] = a[i0][k] + fr * (a[i0 + 1][k] - a[i0][k]);
                for (auto& bank : formant)
                    bank[(size_t) k].set (hz, 9.0f, sr);
            }
        }
        float y = 0.0f;
        for (int k = 0; k < 3; ++k)
        {
            float lp, bp, hp;
            formant[(size_t) ch][(size_t) k].process (x, lp, bp, hp);
            y += bp * formant[(size_t) ch][(size_t) k].k * formantAmp[(size_t) k];
            juce::ignoreUnused (lp, hp);
        }
        return y * 2.2f;
    }

    void configure (const LayerParams& p) noexcept
    {
        if (p.voices == configured && juce::exactlyEqual (p.detune, configuredDetune)
            && juce::exactlyEqual (p.spread, configuredSpread) && juce::exactlyEqual (p.pan, configuredPan)
            && juce::exactlyEqual (p.offset, configuredOffset))
            return;
        configured = p.voices;
        configuredDetune = p.detune;
        configuredSpread = p.spread;
        configuredPan = p.pan;
        configuredOffset = p.offset;
        voices = juce::jlimit (1, kMaxUnison, p.voices);
        const float norm = 1.0f / std::sqrt ((float) voices);
        for (int v = 0; v < voices; ++v)
        {
            const float pos = voices == 1 ? 0.0f : ((float) v / (float) (voices - 1)) * 2.0f - 1.0f;
            ratio[(size_t) v] = std::exp2 ((p.offset * 100.0f + pos * p.detune * 0.5f) / 1200.0f);
            const float pan = juce::jlimit (-1.0f, 1.0f, pos * p.spread + p.pan);
            const float angle = (pan + 1.0f) * dd::kPi * 0.25f;
            gainL[(size_t) v] = std::cos (angle) * 1.41421356f * norm;
            gainR[(size_t) v] = std::sin (angle) * 1.41421356f * norm;
        }
        for (auto& f : toneHp)
            f.setCutoff (160.0f, sr);
    }

    double sr = 44100.0;
    dd::Rng rng;
    int voices = 1, configured = -1;
    float configuredDetune = -1.0f, configuredSpread = -1.0f, configuredPan = -9.0f, configuredOffset = -999.0f;
    float velocity = 1.0f;
    std::array<float, kMaxUnison> phase {}, phase2 {}, phase3 {}, phase4 {}, pwm {}, pulseWidth {}, ratio {}, driftRatio {}, gainL {}, gainR {};
    std::array<dd::Drift, kMaxUnison> drift {};
    std::array<dd::Svf, kMaxUnison> airBp {};
    std::array<std::array<dd::Svf, 3>, 2> formant {};
    std::array<float, 3> formantAmp {};
    float lastVowel = -1.0f;
    dd::OnePole toneLp[2], toneHp[2];
    std::array<dd::OnePole, kMaxUnison> breathLp {};
    float indexEnv = 1.0f, indexCoef = 0.0f, tineEnv = 1.0f, tineCoef = 0.0f, airGain = 1.0f;
    float clickEnv = 0.0f, clickCoef = 0.0f;

    // Pluck
    std::array<std::vector<float>, kMaxUnison> strings;
    std::array<int, kMaxUnison> stringWrite {}, excite {};
    std::array<float, kMaxUnison> stringZ {}, exciteLp {}, exciteLp2 {}, dcX {}, dcY {}, pluckGain {};
    int stringMask = 0;
    float pluckT60 = 2.0f, pluckDamp = 0.3f, dcCoef = 0.999f;

    // Organ
    std::array<float, 9> drawbar {};
    float drawbarNorm = 1.0f, lastDrawbar = -1.0f;

    // Mallet
    std::array<float, 4> modeRatio {}, modeAmp {}, modeEnv {}, modeCoef {};
    bool modalReady = false;

    // Sync
    std::array<float, kMaxUnison> syncPrev {};

    // Electric
    std::array<float, kMaxUnison> apX {}, apY {}, pickPos {};
    std::array<dd::Svf, 2> pickupLp {};
    float pickupDc[2] {};
    float elecDamp = 0.12f, pickupPos = 0.2f, pickupFc = -1.0f, pickupGain = 1.0f, twang = 0.0f, twangCoef = 0.0f;
};
} // namespace ag
