#pragma once

namespace dd
{
enum class Wave { Sine, Triangle, Saw, Square, Pulse, Thin };

inline const juce::StringArray& waveNames()
{
    static const juce::StringArray names { "Sine", "Tri", "Saw", "Square", "Pulse", "Thin" };
    return names;
}

/** PolyBLEP residual for a discontinuity at phase 0 (t in [0,1), dt = phase increment). */
inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/** Band-limited (PolyBLEP) oscillator. Every wave starts at a zero crossing when phase = 0. */
struct BlepOscillator
{
    float phase = 0.0f;

    void reset (float p = 0.0f) noexcept { phase = p - std::floor (p); }

    float process (float dt, Wave wave) noexcept
    {
        dt = juce::jlimit (0.0f, 0.45f, dt);
        const float t = phase;
        float y = 0.0f;

        switch (wave)
        {
            case Wave::Sine:
                y = std::sin (kTwoPi * t);
                break;

            case Wave::Triangle:
                y = t < 0.25f ? 4.0f * t : (t < 0.75f ? 2.0f - 4.0f * t : 4.0f * t - 4.0f);
                break;

            case Wave::Saw:
            {
                float ts = t + 0.5f;
                if (ts >= 1.0f) ts -= 1.0f;
                y = 2.0f * ts - 1.0f - polyBlep (ts, dt);
                break;
            }

            case Wave::Square: y = pulse (t, dt, 0.5f);  break;
            case Wave::Pulse:  y = pulse (t, dt, 0.25f); break;
            case Wave::Thin:   y = pulse (t, dt, 0.1f);  break;
        }

        phase += dt;
        if (phase >= 1.0f)
            phase -= 1.0f;

        return y;
    }

private:
    static float pulse (float t, float dt, float width) noexcept
    {
        float y = t < width ? 1.0f : -1.0f;
        y += polyBlep (t, dt);
        float t2 = t + 1.0f - width;
        if (t2 >= 1.0f) t2 -= 1.0f;
        y -= polyBlep (t2, dt);
        return y - (2.0f * width - 1.0f); // remove DC
    }
};

/** Up to 7 detuned PolyBLEP voices, spread across the stereo field. */
struct UnisonOscillator
{
    static constexpr int kMaxVoices = 7;

    void noteOn (Rng& rng, float driftAmount) noexcept
    {
        for (int i = 0; i < kMaxVoices; ++i)
        {
            // Voice 0 always restarts at phase 0 so the attack is identical on every hit.
            osc[(size_t) i].reset (i == 0 ? 0.0f : rng.uni());
            staticDetune[(size_t) i] = i == 0 ? 0.0f : rng.bi() * driftAmount * 4.0f; // cents
        }
        updateRatios();
    }

    /** Recompute spread tables. Call when voices/detune/width change. */
    void configure (int numVoices, float detuneCents, float width) noexcept
    {
        voices = juce::jlimit (1, kMaxVoices, numVoices);
        const float norm = 1.0f / std::sqrt ((float) voices);

        for (int i = 0; i < voices; ++i)
        {
            // Spread order: 0 centre, then alternating outward pairs.
            const float pos = voices == 1 ? 0.0f
                                          : ((float) i / (float) (voices - 1)) * 2.0f - 1.0f;
            detuneCentsPer[(size_t) i] = pos * detuneCents * 0.5f;
            const float pan = juce::jlimit (-1.0f, 1.0f, pos * width);
            gainL[(size_t) i] = std::min (1.0f, 1.0f - pan) * norm;
            gainR[(size_t) i] = std::min (1.0f, 1.0f + pan) * norm;
        }
        updateRatios();
    }

    void process (float baseDt, Wave wave, float& outL, float& outR) noexcept
    {
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < voices; ++i)
        {
            const float s = osc[(size_t) i].process (baseDt * ratio[(size_t) i], wave);
            l += s * gainL[(size_t) i];
            r += s * gainR[(size_t) i];
        }
        outL = l;
        outR = r;
    }

    void updateRatios() noexcept
    {
        for (int i = 0; i < voices; ++i)
            ratio[(size_t) i] = std::exp2 ((detuneCentsPer[(size_t) i] + staticDetune[(size_t) i]) * (1.0f / 1200.0f));
    }

    std::array<BlepOscillator, kMaxVoices> osc {};
    std::array<float, kMaxVoices> detuneCentsPer {}, staticDetune {}, gainL {}, gainR {}, ratio {};
    int voices = 1;
};
} // namespace dd
