#pragma once

namespace dd
{
/**
    Stereo phaser: six first-order allpass stages swept exponentially from 160 Hz, left and right
    LFOs in quadrature, saturated feedback. Returns the allpass output only: mixing it 50/50 with the
    dry signal is what carves the moving notches (Small Stone / Phase 90 style).
*/
class Phaser
{
public:
    void prepare (double newSr) { sr = newSr; reset(); }

    void reset()
    {
        for (auto& ch : z) ch.fill (0.0f);
        fb[0] = fb[1] = 0.0f;
        phase = 0.0;
    }

    void process (float inL, float inR, float& outL, float& outR, float rateHz, float depth, float feedback) noexcept
    {
        phase += (double) rateHz / sr;
        if (phase >= 1.0) phase -= 1.0;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float lfo = 0.5f + 0.5f * sine01 ((float) phase + (ch == 0 ? 0.0f : 0.25f));
            const float hz = std::min (160.0f * std::exp2 (depth * 5.0f * lfo), 0.4f * (float) sr);
            const float t = std::tan (kPi * hz / (float) sr);
            const float a = (t - 1.0f) / (t + 1.0f);
            float x = (ch == 0 ? inL : inR) + fb[ch] * feedback;
            for (auto& state : z[(size_t) ch])
            {
                const float y = a * x + state;
                state = x - a * y;
                x = y;
            }
            fb[ch] = std::tanh (x);
            (ch == 0 ? outL : outR) = x;
        }
    }

private:
    double sr = 44100.0, phase = 0.0;
    std::array<std::array<float, 6>, 2> z {};
    float fb[2] {};
};

/**
    Bucket-brigade ensemble, wet only.
    JunoI / JunoII: one triangle LFO (0.51 / 0.86 Hz), the two sides swept in opposite directions,
    like the Juno-60 chorus. Strings: three lines each swept by a slow and a fast sine 120 degrees
    apart, the Solina / ARP string-machine shimmer.
*/
class Ensemble
{
public:
    enum class Mode { JunoI, JunoII, Strings };

    static const juce::StringArray& modeNames()
    {
        static const juce::StringArray names { "Juno I", "Juno II", "Strings" };
        return names;
    }

    void prepare (double newSr)
    {
        sr = newSr;
        for (auto& d : lines)
            d.prepare ((int) (0.03 * sr));
        reset();
    }

    void reset()
    {
        for (auto& d : lines) d.reset();
        slow = fast = tri = 0.0;
    }

    void process (float inL, float inR, float& outL, float& outR, Mode mode, float depth) noexcept
    {
        const float ms = 0.001f * (float) sr;
        if (mode != Mode::Strings)
        {
            tri += (mode == Mode::JunoI ? 0.513 : 0.863) / sr;
            if (tri >= 1.0) tri -= 1.0;
            const float t = (float) tri;
            const float lfo = t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t;
            const float sweep = 1.85f * depth;
            lines[0].push (inL);
            lines[1].push (inR);
            outL = lines[0].read ((3.5f + sweep * lfo) * ms);
            outR = lines[1].read ((3.5f - sweep * lfo) * ms);
            return;
        }

        slow += 0.63 / sr;
        fast += 5.7 / sr;
        if (slow >= 1.0) slow -= 1.0;
        if (fast >= 1.0) fast -= 1.0;
        const float mono = 0.5f * (inL + inR);
        float v[3];
        for (int k = 0; k < 3; ++k)
        {
            const float off = (float) k / 3.0f;
            const float mod = 1.6f * sine01 ((float) slow + off) + 0.3f * sine01 ((float) fast + off);
            lines[(size_t) k].push (mono);
            v[k] = lines[(size_t) k].read ((5.0f + depth * mod) * ms);
        }
        outL = 0.62f * v[0] + 0.45f * v[1] - 0.2f * v[2];
        outR = 0.62f * v[2] + 0.45f * v[1] - 0.2f * v[0];
    }

private:
    double sr = 44100.0, slow = 0.0, fast = 0.0, tri = 0.0;
    DelayBuffer lines[3];
};

/**
    Tape machine, in place: wow (slow, slightly irregular) and flutter (fast) pitch wobble through a
    modulated delay, warm asymmetric saturation, and a head-bump-free high roll-off.
*/
class Tape
{
public:
    void prepare (double newSr)
    {
        sr = newSr;
        for (auto& d : lines)
            d.prepare ((int) (0.03 * sr));
        wander.prepare (sr, 0.7f);
        for (int ch = 0; ch < 2; ++ch)
        {
            dc[ch].prepare (sr);
            dc[ch].reset();
        }
        reset();
    }

    void reset()
    {
        for (auto& d : lines) d.reset();
        for (auto& f : rolloff) f.reset();
        wowPhase = flutterPhase = 0.0;
        lastDrive = -1.0f;
    }

    void process (float& l, float& r, float drive, float wow, float flutter) noexcept
    {
        if (std::abs (drive - lastDrive) > 1.0e-3f)
        {
            lastDrive = drive;
            for (auto& f : rolloff)
                f.setCutoff (16000.0f - 7000.0f * drive, sr);
        }

        wowPhase += 0.55 / sr;
        flutterPhase += 7.3 / sr;
        if (wowPhase >= 1.0) wowPhase -= 1.0;
        if (flutterPhase >= 1.0) flutterPhase -= 1.0;
        const float modMs = wow * 2.4f * (0.75f * sine01 ((float) wowPhase) + 0.25f * wander.process (rng))
                          + flutter * 0.14f * sine01 ((float) flutterPhase);
        const float d = (6.0f + modMs) * 0.001f * (float) sr;

        float* io[2] = { &l, &r };
        const float g = dbToGain (drive * 16.0f);
        const float bias = 0.12f * drive;
        const float makeup = 1.0f / std::pow (g, 0.8f);
        for (int ch = 0; ch < 2; ++ch)
        {
            lines[ch].push (*io[ch]);
            float x = lines[ch].read (d);
            if (drive > 0.001f)
                x = dc[ch].process ((std::tanh (g * x + bias) - std::tanh (bias)) * makeup);
            *io[ch] = rolloff[ch].lp (x);
        }
    }

private:
    double sr = 44100.0, wowPhase = 0.0, flutterPhase = 0.0;
    DelayBuffer lines[2];
    OnePole rolloff[2];
    DcBlocker dc[2];
    Drift wander;
    Rng rng;
    float lastDrive = -1.0f;
};

/** Lo-fi, in place: sample-rate reduction (sample and hold) plus bit-depth reduction. amount 0..1. */
class Crusher
{
public:
    void reset() { counter = 0; held[0] = held[1] = 0.0f; }

    void process (float& l, float& r, float amount) noexcept
    {
        if (amount <= 0.001f)
            return;
        const int hold = 1 + (int) (amount * amount * 10.0f);
        if (--counter <= 0)
        {
            counter = hold;
            const float q = std::exp2 (15.0f - amount * 10.0f);   // 16 bits down to 6
            held[0] = std::round (l * q) / q;
            held[1] = std::round (r * q) / q;
        }
        const float wet = std::min (1.0f, amount * 3.0f);
        l += (held[0] - l) * wet;
        r += (held[1] - r) * wet;
    }

private:
    int counter = 0;
    float held[2] {};
};

/** Octave-up pitch shifter (two crossfaded taps sweeping a 40 ms window), mono. Feeds shimmer reverbs. */
class OctaveShifter
{
public:
    void prepare (double newSr)
    {
        sr = newSr;
        window = (float) (0.04 * sr);
        line.prepare ((int) window + 8);
        reset();
    }

    void reset() { line.reset(); phase = 0.0f; }

    float process (float x) noexcept
    {
        line.push (x);
        phase += 1.0f / window;   // ratio 2: the read point closes in one sample per sample
        if (phase >= 1.0f) phase -= 1.0f;
        float y = 0.0f;
        for (int k = 0; k < 2; ++k)
        {
            float p = phase + 0.5f * (float) k;
            if (p >= 1.0f) p -= 1.0f;
            const float gain = sine01 (p * 0.5f);   // sin (pi p): silent where the tap wraps
            y += line.read (1.0f + window * (1.0f - p)) * gain * gain;
        }
        return y;
    }

private:
    double sr = 44100.0;
    float window = 2000.0f, phase = 0.0f;
    DelayBuffer line;
};
} // namespace dd
