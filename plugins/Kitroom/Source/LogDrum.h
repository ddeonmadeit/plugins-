#pragma once

#include <dd_dsp/dd_dsp.h>

namespace kitroom
{
/** Log drum settings, resolved once per block. */
struct LogSettings
{
    float gain = 1.0f, tune = 0.0f, lengthMs = 380.0f, knock = 0.6f, tone = 0.55f, drive = 0.3f, sub = 0.35f, glideMs = 70.0f;
    int model = 0;
};

inline const juce::StringArray& logModelNames()
{
    static const juce::StringArray n { "Classic", "Woody", "Deep", "Bounce" };
    return n;
}

/**
    The Amapiano log drum: a pitched, percussive FM bass that is the kick and the bassline at once. A low sine
    carrier with a modulator a ratio or two up whose index blooms for a few milliseconds and collapses (the
    woody "pock"), a fast pitch drop on every hit (the knock), a short decay with no sustain, a sine sub under
    it, then a gentle low-pass and saturation for the upper-mid ping. Monophonic: a note that starts while the
    last one is still held slides to its pitch without a new knock; any other note cuts the last one off with
    a few milliseconds' fade, as on the original FL Studio preset.
*/
class LogDrum
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        reset();
    }

    void reset()
    {
        for (auto& v : voices)
            v = Voice {};
        lp = 0.0f;
    }

    /** `note`: the sounding MIDI note. `slide`: glide from where the current note is, no new knock. */
    void noteOn (int note, float velocity, bool slide, const LogSettings& s) noexcept
    {
        auto& cur = voices[(size_t) current];
        if (slide && cur.active && cur.fadeStep <= 0.0f)
        {
            cur.target = (float) note;
            cur.glideCoef = glideCoef (s.glideMs);
            cur.env = std::max (cur.env, 0.8f * level (velocity));   // the slide swells back up, without a new knock
            return;
        }
        if (cur.active)
            cur.fadeStep = 1.0f / (float) (0.006 * sr);   // cut the last note, click-free
        current = 1 - current;
        auto& v = voices[(size_t) current];
        v = Voice {};
        v.active = true;
        v.pitch = v.target = (float) note;
        v.glideCoef = 1.0f;
        v.velKnock = 0.55f + 0.45f * velocity;
        v.env = level (velocity);
        v.noise = 0x9e3779b9u ^ (uint32_t) (note * 7919 + (int) (velocity * 1000.0f));
    }

    /** Adds the log drum into L / R (mono, centre). */
    void render (float* L, float* R, int n, const LogSettings& s) noexcept
    {
        if (! voices[0].active && ! voices[1].active)
            return;
        const auto& m = model (s.model);
        const float knockScale = 0.4f + 1.2f * s.knock;
        const float blipSt = m.blipSt * knockScale, tauBlip = m.tauBlipMs * 0.001f;
        const float idxBurst = m.idxBurst * knockScale, tauIdx = m.tauIdxMs * 0.001f;
        const float click = m.click * (0.3f + 1.4f * s.knock);
        const float subMix = juce::jlimit (0.0f, 1.0f, s.sub + m.subBoost);
        const float decay = std::exp (-4.6f / (std::max (0.04f, s.lengthMs * 0.001f) * (float) sr));   // -40 dB at Length
        const float lpCoef = 1.0f - std::exp (-dd::kTwoPi * 300.0f * std::exp2 (s.tone * 5.3f) / (float) sr);
        // Into the saturator gently: at the default it rounds the peaks and adds harmonics, flat-tops only when pushed.
        const float drv = 0.6f + 4.4f * s.drive, drvNorm = 0.9f / std::tanh (drv * 0.9f);
        const float bpF = 2.0f * std::sin (dd::kPi * std::min (m.clickHz, 0.2f * (float) sr) / (float) sr);
        const float dt = 1.0f / (float) sr;
        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            for (auto& v : voices)
            {
                if (! v.active)
                    continue;
                v.pitch += (v.target - v.pitch) * v.glideCoef;
                const float blip = blipSt * fastExp (-v.t / tauBlip);
                const float hz = 440.0f * std::exp2 ((v.pitch + s.tune + blip - 69.0f) / 12.0f);
                v.phC += hz * dt;
                if (v.phC >= 1.0) v.phC -= 1.0;
                v.phM += hz * m.ratio * dt;
                while (v.phM >= 1.0) v.phM -= 1.0;
                const float index = (idxBurst * v.velKnock) * fastExp (-v.t / tauIdx) + m.idxBody;
                const float carrier = std::sin (dd::kTwoPi * v.phC + index * std::sin (dd::kTwoPi * v.phM));
                const float sub = std::sin (dd::kTwoPi * v.phC);
                // The mallet: a short band of noise around the click frequency.
                v.noise = v.noise * 1664525u + 1013904223u;
                const float white = (float) (int32_t) v.noise * (1.0f / 2147483648.0f);
                v.bpLow += bpF * v.bpBand;
                const float high = white - v.bpLow - 0.8f * v.bpBand;
                v.bpBand += bpF * high;
                const float clickEnv = click * v.velKnock * fastExp (-v.t / 0.0012f);
                const float attack = std::min (1.0f, v.t / 0.0006f);
                float y = (carrier * (1.0f - subMix) + sub * subMix * 1.2f) * v.env * attack + v.bpBand * clickEnv;
                v.env *= decay;
                v.t += dt;
                if (v.fadeStep > 0.0f)
                {
                    v.fade -= v.fadeStep;
                    if (v.fade <= 0.0f)
                        v.active = false;
                    y *= std::max (0.0f, v.fade);
                }
                if (v.env < 1.0e-4f && v.t > 0.05f)
                    v.active = false;
                x += y;
            }
            // Darker or brighter, then rounded off: the saturation brings out the upper-mid ping.
            lp += (x - lp) * lpCoef;
            const float out = std::tanh (lp * drv) * drvNorm * s.gain * 0.3f;   // at Volume 0 it leads a kit by a few LU
            L[i] += out;
            R[i] += out;
        }
    }

private:
    struct Model { float ratio, idxBurst, idxBody, tauIdxMs, blipSt, tauBlipMs, click, clickHz, subBoost; };

    static const Model& model (int i) noexcept
    {
        // Classic: the FL Studio FM log (modulator an octave up). Woody: an inharmonic modulator and more mallet.
        // Deep: a rounder sine body, slow soft drop, more sub. Bounce: the "donk": a held index and a sharp blip.
        static const Model models[] {
            { 2.0f, 4.5f, 0.95f, 8.0f, 12.0f, 10.0f, 0.22f, 1500.0f, 0.0f },
            { 3.5f, 3.4f, 0.55f, 5.0f, 9.0f, 6.0f, 0.60f, 2300.0f, 0.0f },
            { 1.0f, 2.0f, 0.30f, 10.0f, 7.0f, 22.0f, 0.08f, 1200.0f, 0.25f },
            { 2.0f, 3.0f, 1.45f, 40.0f, 19.0f, 4.0f, 0.12f, 1800.0f, 0.0f },
        };
        return models[juce::jlimit (0, 3, i)];
    }

    float glideCoef (float ms) const noexcept
    {
        return ms < 1.0f ? 1.0f : 1.0f - std::exp (-3.0f / (ms * 0.001f * (float) sr));   // ~95 % of the way in `ms`
    }

    static float level (float velocity) noexcept { return std::pow (juce::jlimit (0.0f, 1.0f, velocity), 0.7f); }

    /** exp (x) for x <= 0, accurate enough for envelopes and cheap (no library call per sample). */
    static float fastExp (float x) noexcept
    {
        if (x < -20.0f)
            return 0.0f;
        x = 1.0f + x / 256.0f;
        for (int k = 0; k < 8; ++k)
            x *= x;
        return x;
    }

    struct Voice
    {
        bool active = false;
        double phC = 0.0, phM = 0.0;
        float t = 0.0f, env = 0.0f, pitch = 0.0f, target = 0.0f, glideCoef = 1.0f, velKnock = 1.0f;
        float fade = 1.0f, fadeStep = 0.0f, bpLow = 0.0f, bpBand = 0.0f;
        uint32_t noise = 1u;
    };

    double sr = 44100.0;
    std::array<Voice, 2> voices {};
    int current = 0;
    float lp = 0.0f;
};
} // namespace kitroom
