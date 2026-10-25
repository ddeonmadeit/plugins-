#pragma once

namespace dd
{
/** Pre-warped TPT integrator gain for a cutoff, clamped below Nyquist. */
inline float tptG (float hz, double sr) noexcept
{
    const float fc = juce::jlimit (5.0f, (float) (sr * 0.49), hz);
    return std::tan (kPi * fc / (float) sr);
}

/** Topology-preserving one-pole low/high pass. */
struct OnePole
{
    void setCutoff (float hz, double sr) noexcept { const float g = tptG (hz, sr); G = g / (1.0f + g); }
    void reset (float v = 0.0f) noexcept          { s = v; }

    float lp (float x) noexcept
    {
        const float v = (x - s) * G;
        const float y = v + s;
        s = y + v;
        return y;
    }

    float hp (float x) noexcept { return x - lp (x); }

    float G = 0.5f, s = 0.0f;
};

/** Andy Simper / Cytomic trapezoidal state variable filter. */
struct Svf
{
    void set (float hz, float q, double sr) noexcept
    {
        g = tptG (hz, sr);
        k = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    /** Set from a precomputed tptG() and damping k = 1/Q (for per-sample modulation). */
    void setCoeffs (float gIn, float kIn) noexcept
    {
        g = gIn;
        k = kIn;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    void process (float v0, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = v0 - k * v1 - v2;
    }

    float lp (float x) noexcept { float l, b, h; process (x, l, b, h); return l; }
    float hp (float x) noexcept { float l, b, h; process (x, l, b, h); return h; }

    float g = 0.1f, k = 1.414f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
};

/** 4th-order Linkwitz-Riley crossover: low + high sums to an allpass (flat magnitude). */
struct Crossover
{
    void set (float hz, double sr) noexcept
    {
        if (juce::exactlyEqual (hz, lastHz) && juce::exactlyEqual (sr, lastSr))
            return;
        lastHz = hz;
        lastSr = sr;
        constexpr float q = 0.70710678f;
        for (auto* f : { &lp1, &lp2, &hp1, &hp2 })
            f->set (hz, q, sr);
    }

    void reset() noexcept { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }

    void process (float x, float& low, float& high) noexcept
    {
        low  = lp2.lp (lp1.lp (x));
        high = hp2.hp (hp1.hp (x));
    }

    float lowOnly (float x) noexcept  { return lp2.lp (lp1.lp (x)); }
    float highOnly (float x) noexcept { return hp2.hp (hp1.hp (x)); }

    Svf lp1, lp2, hp1, hp2;
    float lastHz = -1.0f;
    double lastSr = 0.0;
};

/** Zero-delay-feedback 4-pole ladder (Zavalishin) with a tanh-saturated input stage. */
struct LadderFilter
{
    void reset() noexcept { s.fill (0.0f); }

    /** g = tptG(cutoff), res in [0, 1), drive >= 1. */
    float process (float x, float g, float res, float drive) noexcept
    {
        const float G = g / (1.0f + g);
        const float k = res * 3.95f;

        // Instantaneous response of the cascade: y4 = G^4 * u + S
        const float oneMinusG = 1.0f - G;
        const float b1 = s[0] * oneMinusG, b2 = s[1] * oneMinusG, b3 = s[2] * oneMinusG, b4 = s[3] * oneMinusG;
        const float S = ((b1 * G + b2) * G + b3) * G + b4;
        const float G4 = G * G * G * G;

        // Passband gain drops as resonance rises; give some of it back.
        const float in = x * (1.0f + 0.55f * k);
        float u = (in - k * S) / (1.0f + k * G4);
        u = std::tanh (u * drive) / std::sqrt (drive);

        float y = u;
        for (size_t i = 0; i < 4; ++i)
        {
            const float v = (y - s[i]) * G;
            const float out = v + s[i];
            s[i] = out + v;
            y = out;
        }
        return y;
    }

    std::array<float, 4> s {};
};

/** 10 Hz DC blocker. */
struct DcBlocker
{
    void prepare (double sr) noexcept { R = 1.0f - (kTwoPi * 10.0f / (float) sr); }
    void reset() noexcept             { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = x - x1 + R * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    float R = 0.999f, x1 = 0.0f, y1 = 0.0f;
};

/** RBJ biquad, transposed direct form II. */
struct Biquad
{
    enum class Type { LowPass, HighPass, Peak, LowShelf, HighShelf };

    void set (Type type, float hz, float q, float gainDb, double sr) noexcept
    {
        const float f = juce::jlimit (10.0f, (float) (sr * 0.49), hz);
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w0 = kTwoPi * f / (float) sr;
        const float cw = std::cos (w0), sw = std::sin (w0);
        const float alpha = sw / (2.0f * q);
        float a0 = 1.0f, a1_ = 0.0f, a2_ = 0.0f, b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;

        switch (type)
        {
            case Type::LowPass:
                b0_ = (1.0f - cw) * 0.5f; b1_ = 1.0f - cw; b2_ = b0_;
                a0 = 1.0f + alpha; a1_ = -2.0f * cw; a2_ = 1.0f - alpha;
                break;
            case Type::HighPass:
                b0_ = (1.0f + cw) * 0.5f; b1_ = -(1.0f + cw); b2_ = b0_;
                a0 = 1.0f + alpha; a1_ = -2.0f * cw; a2_ = 1.0f - alpha;
                break;
            case Type::Peak:
                b0_ = 1.0f + alpha * A; b1_ = -2.0f * cw; b2_ = 1.0f - alpha * A;
                a0 = 1.0f + alpha / A; a1_ = -2.0f * cw; a2_ = 1.0f - alpha / A;
                break;
            case Type::LowShelf:
            {
                const float sq = 2.0f * std::sqrt (A) * alpha;
                b0_ = A * ((A + 1.0f) - (A - 1.0f) * cw + sq);
                b1_ = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw);
                b2_ = A * ((A + 1.0f) - (A - 1.0f) * cw - sq);
                a0 = (A + 1.0f) + (A - 1.0f) * cw + sq;
                a1_ = -2.0f * ((A - 1.0f) + (A + 1.0f) * cw);
                a2_ = (A + 1.0f) + (A - 1.0f) * cw - sq;
                break;
            }
            case Type::HighShelf:
            {
                const float sq = 2.0f * std::sqrt (A) * alpha;
                b0_ = A * ((A + 1.0f) + (A - 1.0f) * cw + sq);
                b1_ = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw);
                b2_ = A * ((A + 1.0f) + (A - 1.0f) * cw - sq);
                a0 = (A + 1.0f) - (A - 1.0f) * cw + sq;
                a1_ = 2.0f * ((A - 1.0f) - (A + 1.0f) * cw);
                a2_ = (A + 1.0f) - (A - 1.0f) * cw - sq;
                break;
            }
        }

        b0 = b0_ / a0; b1 = b1_ / a0; b2 = b2_ / a0; a1 = a1_ / a0; a2 = a2_ / a0;
    }

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
};
} // namespace dd
