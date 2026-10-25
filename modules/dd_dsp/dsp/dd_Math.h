#pragma once

namespace dd
{
inline constexpr float kPi    = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;

inline float dbToGain (float db) noexcept           { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept            { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float semitonesToRatio (float st) noexcept   { return std::exp2 (st * (1.0f / 12.0f)); }
inline float midiToHz (float note) noexcept         { return 440.0f * std::exp2 ((note - 69.0f) * (1.0f / 12.0f)); }
inline float clamp01 (float x) noexcept             { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

/** Coefficient for y += (target - y) * (1 - c): reaches ~63% after timeSec. */
inline float onePoleCoef (float timeSec, double sampleRate) noexcept
{
    return timeSec <= 0.0f ? 0.0f : (float) std::exp (-1.0 / (timeSec * sampleRate));
}

/** Cubic soft clip with a C1-continuous knee: linear near 0, flat at +-1 for |x| >= 1.5. */
inline float softClipCubic (float x) noexcept
{
    x = juce::jlimit (-1.5f, 1.5f, x);
    return x - (4.0f / 27.0f) * x * x * x;
}

/** Linear up to |x| = knee, then a tanh shoulder into a ceiling of 1. */
inline float kneeClip (float x, float knee = 0.7f) noexcept
{
    const float a = std::abs (x);
    if (a <= knee)
        return x;
    const float range = 1.0f - knee;
    const float y = knee + range * std::tanh ((a - knee) / range);
    return x < 0.0f ? -y : y;
}

inline bool isBad (float x) noexcept { return ! std::isfinite (x); }

/** Small, fast, deterministic RNG (xorshift32). */
struct Rng
{
    uint32_t state = 0x9E3779B9u;

    void seed (uint32_t s) noexcept     { state = s != 0 ? s : 0x9E3779B9u; }
    uint32_t next() noexcept            { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    float uni() noexcept                { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() noexcept                 { return uni() * 2.0f - 1.0f; }
};

/** Slow band-limited random wander in [-1, 1], for analog drift. */
struct Drift
{
    void prepare (double sr, float rateHz) noexcept
    {
        coef = 1.0f - onePoleCoef (1.0f / (kTwoPi * rateHz), sr);
        holdSamples = std::max (1, (int) (sr / (rateHz * 2.0f)));
    }

    float process (Rng& rng) noexcept
    {
        if (--counter <= 0) { target = rng.bi(); counter = holdSamples; }
        s1 += (target - s1) * coef;
        s2 += (s1 - s2) * coef;
        return s2;
    }

    void reset (Rng& rng) noexcept      { s1 = s2 = target = rng.bi() * 0.5f; counter = 0; }

    float coef = 0.001f, target = 0.0f, s1 = 0.0f, s2 = 0.0f;
    int holdSamples = 1000, counter = 0;
};

/** Note divisions for tempo sync. */
inline const juce::StringArray& divisionNames()
{
    static const juce::StringArray names { "1/1", "1/2", "1/2D", "1/2T", "1/4", "1/4D", "1/4T",
                                           "1/8", "1/8D", "1/8T", "1/16", "1/16D", "1/16T", "1/32" };
    return names;
}

/** Length in quarter-note beats of the division at index i in divisionNames(). */
inline double divisionBeats (int i) noexcept
{
    static constexpr double beats[] = { 4.0, 2.0, 3.0, 4.0 / 3.0, 1.0, 1.5, 2.0 / 3.0,
                                        0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0, 0.125 };
    return beats[juce::jlimit (0, 13, i)];
}
} // namespace dd
