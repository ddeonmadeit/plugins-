#pragma once

namespace dd
{
/**
    Analog-style ADSR (after Nigel Redmon). Exponential decay/release segments reach their
    target at exactly the set time, so "decay 2 s" means silent after ~2 s with -20 dB at ~0.5 s.
    noteOn() continues from the current level, so legato retriggers never click.
*/
class Adsr
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void setSampleRate (double newSr) noexcept { sr = newSr; dirty = true; }

    void setParams (float attackMs, float decayMs, float sustain, float releaseMs) noexcept
    {
        if (dirty || ! juce::exactlyEqual (attackMs, aMs) || ! juce::exactlyEqual (decayMs, dMs)
            || ! juce::exactlyEqual (sustain, sus) || ! juce::exactlyEqual (releaseMs, rMs))
        {
            aMs = attackMs; dMs = decayMs; sus = juce::jlimit (0.0f, 1.0f, sustain); rMs = releaseMs;
            dirty = false;
            calc (aMs, kAttackRatio, attackCoef);
            attackBase = (1.0f + kAttackRatio) * (1.0f - attackCoef);
            calc (dMs, kDecayRatio, decayCoef);
            decayBase = (sus - kDecayRatio) * (1.0f - decayCoef);
            calc (rMs, kDecayRatio, releaseCoef);
            releaseBase = -kDecayRatio * (1.0f - releaseCoef);
        }
    }

    void noteOn() noexcept   { stage = Stage::Attack; }
    void noteOff() noexcept  { if (stage != Stage::Idle) stage = Stage::Release; }
    void reset() noexcept    { stage = Stage::Idle; level = 0.0f; }

    float process() noexcept
    {
        switch (stage)
        {
            case Stage::Idle:
                break;

            case Stage::Attack:
                level = attackBase + level * attackCoef;
                if (level >= 1.0f) { level = 1.0f; stage = Stage::Decay; }
                break;

            case Stage::Decay:
                level = decayBase + level * decayCoef;
                if (level <= sus)
                {
                    level = sus;
                    stage = sus <= 0.0f ? Stage::Idle : Stage::Sustain;
                }
                break;

            case Stage::Sustain:
                level += (sus - level) * 0.002f; // follow sustain changes without zipper
                break;

            case Stage::Release:
                level = releaseBase + level * releaseCoef;
                if (level <= 1.0e-5f) { level = 0.0f; stage = Stage::Idle; }
                break;
        }
        return level;
    }

    bool isActive() const noexcept   { return stage != Stage::Idle; }
    Stage getStage() const noexcept  { return stage; }
    float getLevel() const noexcept  { return level; }

private:
    static constexpr float kAttackRatio = 0.3f;
    static constexpr float kDecayRatio  = 0.0001f;

    void calc (float ms, float ratio, float& coef) const noexcept
    {
        const float samples = std::max (1.0f, ms * 0.001f * (float) sr);
        coef = std::exp (-std::log ((1.0f + ratio) / ratio) / samples);
    }

    double sr = 44100.0;
    Stage stage = Stage::Idle;
    float level = 0.0f;
    float aMs = -1.0f, dMs = -1.0f, sus = -1.0f, rMs = -1.0f;
    bool dirty = true;
    float attackCoef = 0.0f, attackBase = 0.0f, decayCoef = 0.0f, decayBase = 0.0f, releaseCoef = 0.0f, releaseBase = 0.0f;
};

/** Exponential one-shot decay from 1 to 0, "time" = time to fall to ~5%. */
struct DecayEnv
{
    void trigger (float timeMs, double sr) noexcept
    {
        level = 1.0f;
        coef = onePoleCoef (std::max (0.05f, timeMs) * 0.001f / 3.0f, sr);
    }

    float process() noexcept { const float y = level; level *= coef; return y; }

    float level = 0.0f, coef = 0.0f;
};

/** Global LFO with tempo sync, retrigger and fade-in. Output in [-1, 1]. */
class Lfo
{
public:
    enum class Shape { Sine, Triangle, SawDown, Square, SampleHold };

    static const juce::StringArray& shapeNames()
    {
        static const juce::StringArray names { "Sine", "Triangle", "Saw Down", "Square", "S&H" };
        return names;
    }

    void prepare (double newSr) noexcept { sr = newSr; smooth.setCutoff (60.0f, sr); }

    void retrigger() noexcept { phase = 0.0; fadeLevel = 0.0f; smooth.reset (0.0f); held = 0.0f; }

    void setFade (float ms) noexcept
    {
        fadeInc = ms <= 1.0f ? 1.0f : 1.0f / (ms * 0.001f * (float) sr);
    }

    float process (float rateHz, Shape shape) noexcept
    {
        const float t = (float) phase;
        float y = 0.0f;
        switch (shape)
        {
            case Shape::Sine:       y = std::sin (kTwoPi * t); break;
            case Shape::Triangle:   y = t < 0.25f ? 4.0f * t : (t < 0.75f ? 2.0f - 4.0f * t : 4.0f * t - 4.0f); break;
            case Shape::SawDown:    y = 1.0f - 2.0f * t; break;
            case Shape::Square:     y = t < 0.5f ? 1.0f : -1.0f; break;
            case Shape::SampleHold: y = held; break;
        }

        phase += (double) rateHz / sr;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            held = rng.bi();
        }

        fadeLevel = std::min (1.0f, fadeLevel + fadeInc);
        // Light smoothing removes clicks from square / S&H when they hit amp or pitch.
        return smooth.lp (y) * fadeLevel;
    }

private:
    double sr = 44100.0, phase = 0.0;
    float fadeLevel = 1.0f, fadeInc = 1.0f, held = 0.0f;
    OnePole smooth;
    Rng rng;
};
} // namespace dd
