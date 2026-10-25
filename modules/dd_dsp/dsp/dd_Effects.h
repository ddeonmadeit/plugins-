#pragma once

namespace dd
{
/** Power-of-two circular delay buffer with Hermite interpolation. */
struct DelayBuffer
{
    void prepare (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4)
            size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        pos = 0;
    }

    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }

    void push (float x) noexcept { buf[(size_t) pos] = x; pos = (pos + 1) & mask; }

    /** Read `delay` samples behind the most recent push (delay >= 1). */
    float read (float delay) const noexcept
    {
        const float d = juce::jlimit (1.0f, (float) (mask - 3), delay);
        const int di = (int) d;
        const float f = d - (float) di;
        const int i0 = (pos - di + mask + 1) & mask;
        const float xm1 = buf[(size_t) ((i0 + 1) & mask)];
        const float x0  = buf[(size_t) i0];
        const float x1  = buf[(size_t) ((i0 - 1 + mask + 1) & mask)];
        const float x2  = buf[(size_t) ((i0 - 2 + mask + 1) & mask)];
        const float c = (x1 - xm1) * 0.5f;
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + (x2 - x0) * 0.5f;
        const float b = w + a;
        return ((a * f - b) * f + c) * f + x0;
    }

    float readInt (int delay) const noexcept { return buf[(size_t) ((pos - delay + mask + 1) & mask)]; }

    std::vector<float> buf;
    int mask = 0, pos = 0;
};

/** Stereo chorus with quadrature LFOs (Juno-ish). process() returns the wet signal only, so the
    dry path is never touched (use it as a send fed from the band above the sub). */
class Chorus
{
public:
    void prepare (double newSr)
    {
        sr = newSr;
        for (auto& d : lines)
            d.prepare ((int) (0.04 * sr));
    }

    void reset() { for (auto& d : lines) d.reset(); phase = 0.0; }

    void process (float inL, float inR, float& wetL, float& wetR, float rateHz, float depth) noexcept
    {
        lines[0].push (inL);
        lines[1].push (inR);

        phase += rateHz / sr;
        if (phase >= 1.0) phase -= 1.0;
        const float mL = std::sin (kTwoPi * (float) phase);
        const float mR = std::sin (kTwoPi * (float) (phase + 0.25));
        const float base = 0.007f * (float) sr;
        const float span = (0.0005f + depth * 0.0065f) * (float) sr;

        wetL = lines[0].read (base + span * (0.5f + 0.5f * mL));
        wetR = lines[1].read (base + span * (0.5f + 0.5f * mR));
    }

private:
    double sr = 44100.0, phase = 0.0;
    DelayBuffer lines[2];
};

/** Tempo-syncable stereo / ping-pong delay with damped, saturating feedback. */
class StereoDelay
{
public:
    void prepare (double newSr, float maxSeconds = 3.0f)
    {
        sr = newSr;
        for (auto& d : lines)
            d.prepare ((int) (maxSeconds * sr));
        timeSm.reset (sr, 0.25);
        for (int ch = 0; ch < 2; ++ch)
            hp[ch].setCutoff (180.0f, sr);
    }

    void reset()
    {
        for (auto& d : lines) d.reset();
        for (int ch = 0; ch < 2; ++ch) { lp[ch].reset(); hp[ch].reset(); }
    }

    void setTime (float samples, bool jump = false)
    {
        if (jump) timeSm.setCurrentAndTargetValue (samples);
        else      timeSm.setTargetValue (samples);
    }

    void setTone (float tone01)
    {
        const float hz = 900.0f * std::exp2 (tone01 * 4.0f); // 900 Hz .. 14.4 kHz
        if (! juce::exactlyEqual (hz, lastToneHz))
        {
            lastToneHz = hz;
            for (int ch = 0; ch < 2; ++ch)
                lp[ch].setCutoff (hz, sr);
        }
    }

    /** Returns wet only. */
    void process (float inL, float inR, float& outL, float& outR, float feedback, bool pingPong) noexcept
    {
        const float d = timeSm.getNextValue();
        const float rl = lines[0].read (d);
        const float rr = lines[1].read (d);
        const float fl = lp[0].lp (hp[0].hp (rl)) * feedback;
        const float fr = lp[1].lp (hp[1].hp (rr)) * feedback;

        float wl, wr;
        if (pingPong) { wl = (inL + inR) * 0.5f + fr; wr = fl; }
        else          { wl = inL + fl;                wr = inR + fr; }

        lines[0].push (std::tanh (wl));
        lines[1].push (std::tanh (wr));
        outL = rl;
        outR = rr;
    }

private:
    double sr = 44100.0;
    DelayBuffer lines[2];
    OnePole lp[2], hp[2];
    float lastToneHz = -1.0f;
    juce::SmoothedValue<float> timeSm { 10000.0f };
};

/** 8-line feedback delay network reverb with input diffusion and gentle modulation. */
class Space
{
public:
    void prepare (double newSr)
    {
        sr = newSr;
        for (size_t i = 0; i < 8; ++i)
        {
            lines[i].prepare ((int) (kLineMs[i] * 0.001f * 1.8f * (float) sr) + 64);
            damp[i].setCutoff (6500.0f, sr);
        }
        for (size_t i = 0; i < 4; ++i)
            diff[i].prepare ((int) (kDiffMs[i] * 0.001f * (float) sr) + 8);
        preDelay.prepare ((int) (0.03 * sr));
        inHp.setCutoff (220.0f, sr);
        lastSize = -1.0f;
    }

    void reset()
    {
        for (auto& l : lines) l.reset();
        for (auto& d : diff) d.reset();
        for (auto& d : damp) d.reset();
        preDelay.reset();
        inHp.reset();
        modPhase = 0.0;
    }

    void setSize (float size01)
    {
        if (std::abs (size01 - lastSize) < 1.0e-4f)
            return;
        lastSize = size01;
        const float scale = 0.55f + 1.2f * size01;
        const float t60 = 0.5f + 6.0f * size01 * size01;
        for (size_t i = 0; i < 8; ++i)
        {
            lengths[i] = kLineMs[i] * 0.001f * scale * (float) sr;
            gains[i] = std::pow (10.0f, -3.0f * lengths[i] / (t60 * (float) sr));
        }
    }

    /** Returns wet only. */
    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        float x = inHp.hp ((inL + inR) * 0.5f);
        preDelay.push (x);
        x = preDelay.readInt ((int) (0.012 * sr));

        for (size_t i = 0; i < 4; ++i)
        {
            const float delayed = diff[i].readInt ((int) (kDiffMs[i] * 0.001f * (float) sr));
            const float v = x + 0.62f * delayed;
            diff[i].push (v);
            x = delayed - 0.62f * v;
        }

        modPhase += 0.37 / sr;
        if (modPhase >= 1.0) modPhase -= 1.0;
        const float mod = std::sin (kTwoPi * (float) modPhase) * 0.0009f * (float) sr;

        std::array<float, 8> y {};
        for (size_t i = 0; i < 8; ++i)
        {
            const float m = (i == 1 || i == 6) ? mod : (i == 3 ? -mod : 0.0f);
            y[i] = damp[i].lp (lines[i].read (lengths[i] + m));
        }

        outL = (y[0] - y[2] + y[4] - y[6]) * 0.5f;
        outR = (y[1] - y[3] + y[5] - y[7]) * 0.5f;

        hadamard8 (y);
        for (size_t i = 0; i < 8; ++i)
        {
            const float inj = (i & 1) ? -x : x;
            lines[i].push (inj * 0.5f + y[i] * gains[i]);
        }
    }

private:
    static void hadamard8 (std::array<float, 8>& a) noexcept
    {
        for (size_t h = 1; h < 8; h <<= 1)
            for (size_t i = 0; i < 8; i += h << 1)
                for (size_t j = i; j < i + h; ++j)
                {
                    const float x = a[j], y = a[j + h];
                    a[j] = x + y;
                    a[j + h] = x - y;
                }
        for (auto& v : a)
            v *= 0.35355339f; // 1/sqrt(8)
    }

    static constexpr float kLineMs[8] = { 31.7f, 37.9f, 41.3f, 46.1f, 53.9f, 59.3f, 67.1f, 73.7f };
    static constexpr float kDiffMs[4] = { 4.7f, 3.6f, 12.7f, 9.3f };

    double sr = 44100.0, modPhase = 0.0;
    DelayBuffer lines[8], diff[4], preDelay;
    OnePole damp[8], inHp;
    std::array<float, 8> lengths {}, gains {};
    float lastSize = -1.0f;
};

/** Stereo-linked bus compressor for keeping 808s even. amount 0 = bypass. */
class GlueCompressor
{
public:
    void prepare (double newSr)
    {
        sr = newSr;
        attack = onePoleCoef (0.006f, sr);
        release = onePoleCoef (0.14f, sr);
        grDb = 0.0f;
    }

    void reset() { grDb = 0.0f; }

    void process (float& l, float& r, float amount) noexcept
    {
        if (amount <= 0.0005f)
        {
            grDb = 0.0f;
            return;
        }

        const float thr = -3.0f - 21.0f * amount;
        const float ratio = 2.0f + 4.0f * amount;
        const float knee = 6.0f;
        const float lvl = gainToDb (std::max (std::abs (l), std::abs (r)));

        const float over = lvl - thr;
        float target = 0.0f;
        if (over > knee * 0.5f)       target = over * (1.0f - 1.0f / ratio);
        else if (over > -knee * 0.5f) { const float k = over + knee * 0.5f; target = (1.0f - 1.0f / ratio) * k * k / (2.0f * knee); }

        const float c = target > grDb ? attack : release;
        grDb = target + (grDb - target) * c;

        const float makeup = -thr * (1.0f - 1.0f / ratio) * 0.45f;
        const float g = dbToGain (makeup - grDb);
        l *= g;
        r *= g;
    }

    float getGainReductionDb() const noexcept { return grDb; }

private:
    double sr = 44100.0;
    float attack = 0.0f, release = 0.0f, grDb = 0.0f;
};

/**
    4x oversampled soft-knee clipper, ceiling -1 dBFS. The downsampling filter rings after hard
    clipping (bright material overshoots by 1-3 dB), so a gentle base-rate stage catches that
    overshoot; it only touches the few samples above the ceiling, so it adds almost no aliasing.
*/
class Clipper
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        juce::ignoreUnused (sampleRate);
        os.initProcessing ((size_t) maxBlock);
        driveSm.reset (sampleRate, 0.02);
    }

    void reset() { os.reset(); }

    int getLatency() const noexcept { return (int) std::lround (os.getLatencyInSamples()); }

    void process (juce::dsp::AudioBlock<float>& block, float drive01)
    {
        driveSm.setTargetValue (drive01);
        const int n = (int) block.getNumSamples();
        auto up = os.processSamplesUp (block);
        const int upN = (int) up.getNumSamples();
        const int factor = upN / std::max (1, n);
        constexpr float ceiling = 0.891f;

        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            auto sm = driveSm;
            float* d = up.getChannelPointer (ch);
            float drive = sm.getCurrentValue();
            for (int i = 0; i < upN; ++i)
            {
                if (i % factor == 0)
                    drive = sm.getNextValue();
                const float gain = dbToGain (drive * 18.0f);
                const float knee = 0.9f - 0.3f * drive;
                d[i] = ceiling * kneeClip (d[i] * gain / ceiling, knee);
            }
        }
        driveSm.skip (n);
        os.processSamplesDown (block);

        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            float* d = block.getChannelPointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] = kSafetyCeiling * kneeClip (d[i] / kSafetyCeiling, 0.9f);
        }
    }

private:
    static constexpr float kSafetyCeiling = 0.944f; // -0.5 dBFS

    using OS = juce::dsp::Oversampling<float>;
    OS os { 2, 2, OS::filterHalfBandPolyphaseIIR, true, true };
    juce::SmoothedValue<float> driveSm { 0.0f };
};
} // namespace dd
