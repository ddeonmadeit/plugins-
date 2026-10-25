#pragma once

#include <dd_dsp/dd_dsp.h>

namespace ag
{
enum class Source { Analog, Strings, Choir, Glass, Tines, Air, Sine };

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
        configured = -1;
    }

    void start (const LayerParams& p, float vel) noexcept
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
            pwm[(size_t) i] = rng.uni();
            drift[(size_t) i].reset (rng);
            airBp[(size_t) i].reset();
        }
        for (auto& f : formant) for (auto& s : f) s.reset();
        for (auto& f : toneLp) f.reset();
        for (auto& f : toneHp) f.reset();
        indexEnv = 1.0f;
        tineEnv = 1.0f;
        // Tines: harder hits keep the bark longer.
        indexCoef = dd::onePoleCoef (0.25f + 0.9f * (1.0f - p.shape) * (0.6f + 0.4f * vel), sr);
        tineCoef = dd::onePoleCoef (0.045f, sr);
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

        const float inv = 1.0f / (float) sr;
        for (int i = 0; i < n; ++i)
        {
            const float s = dd::clamp01 (p.shape + p.shapeEnv * env[i] + morph[i] + p.shapeOffset);
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

            L[i] += l * p.level * kSourceGain[(size_t) p.source];
            R[i] += r * p.level * kSourceGain[(size_t) p.source];
        }
    }

private:
    // Per-source gain so every source is equally loud (measured on the pads phrase, one unison voice).
    static constexpr std::array<float, 7> kSourceGain { 0.14f, 0.17f, 0.29f, 0.13f, 0.14f, 0.61f, 0.135f };

    static float saw (float t, float dt) noexcept { return 2.0f * t - 1.0f - dd::polyBlep (t, dt); }

    static void advance (float& ph, float dt) noexcept
    {
        ph += dt;
        if (ph >= 1.0f) ph -= 1.0f;
    }

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
        }
        return y;
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
    std::array<float, kMaxUnison> phase {}, phase2 {}, phase3 {}, pwm {}, pulseWidth {}, ratio {}, driftRatio {}, gainL {}, gainR {};
    std::array<dd::Drift, kMaxUnison> drift {};
    std::array<dd::Svf, kMaxUnison> airBp {};
    std::array<std::array<dd::Svf, 3>, 2> formant {};
    std::array<float, 3> formantAmp {};
    float lastVowel = -1.0f;
    dd::OnePole toneLp[2], toneHp[2];
    std::array<dd::OnePole, kMaxUnison> breathLp {};
    float indexEnv = 1.0f, indexCoef = 0.0f, tineEnv = 1.0f, tineCoef = 0.0f, airGain = 1.0f;
};
} // namespace ag
