#pragma once

namespace dd
{
enum class DirtType { Tape, Tube, Fuzz, Clip, Fold, Crush, Rectify, Amp };

inline const juce::StringArray& dirtTypeNames()
{
    static const juce::StringArray names { "Tape", "Tube", "Fuzz", "Clip", "Fold", "Crush", "Rectify", "Amp" };
    return names;
}

/** Max pre-gain in dB at drive = 1, per type (folders and crushers need far less than clippers). */
inline float dirtMaxDriveDb (DirtType t) noexcept
{
    switch (t)
    {
        case DirtType::Fold:    return 20.0f;
        case DirtType::Crush:   return 18.0f;
        case DirtType::Tape:    return 28.0f;
        case DirtType::Fuzz:    return 30.0f;
        case DirtType::Tube:
        case DirtType::Clip:
        case DirtType::Rectify:
        case DirtType::Amp:     break;
    }
    return 32.0f;
}

/** Per-channel memory for curves that need it (DC-free rectification). */
struct ShaperState
{
    float absMean = 0.0f;
};

/**
    Transfer curves. `bias` in [0, 1] adds asymmetry / character per type. `meanCoef` sets how fast
    the rectifier's DC tracker follows the signal (so octave-up grit never thumps on note on/off).
*/
inline float shapeSample (DirtType type, float x, float bias, ShaperState& st, float meanCoef) noexcept
{
    switch (type)
    {
        case DirtType::Tape:
        {
            // arctan has a softer knee than tanh: compresses smoothly instead of squaring off.
            auto f = [] (float u) { return 0.63661977f * std::atan (u * 1.5707963f); };
            const float b = 0.3f * bias;
            return f (x + b) - f (b);
        }
        case DirtType::Tube:
        {
            // Hard-ish positive grid clipping, soft negative swing -> even harmonics.
            const float b = 0.5f * bias;
            auto f = [] (float u) { return u >= 0.0f ? std::tanh (u) : u / (1.0f - 0.6f * u); };
            return f (x + b) - f (b);
        }
        case DirtType::Fuzz:
        {
            const float s1 = std::tanh (2.5f * x + 0.4f * bias);
            const float a = std::abs (s1);
            st.absMean += (a - st.absMean) * meanCoef;
            const float oct = a - st.absMean;           // octave-up rasp, DC-free
            return std::tanh (3.0f * (s1 + bias * 1.2f * oct)) * 0.95f;
        }
        case DirtType::Clip:
        {
            const float b = 0.3f * bias;
            return juce::jlimit (-1.0f, 1.0f, x + b) - b;
        }
        case DirtType::Fold:
        {
            const float b = 0.5f * bias;
            const float y = std::sin (kPi * 0.5f * (x + b)) - std::sin (kPi * 0.5f * b);
            return std::tanh (1.2f * y);
        }
        case DirtType::Rectify:
        {
            const float t = std::tanh (x);
            const float a = std::abs (t);
            st.absMean += (a - st.absMean) * meanCoef;
            const float r = 0.25f + 0.75f * bias;
            return (1.0f - r) * t + r * 2.0f * (a - st.absMean);
        }
        case DirtType::Amp:
        {
            const float s1 = std::tanh (1.6f * x + 0.25f * bias);
            return std::tanh (2.2f * s1);
        }
        case DirtType::Crush:
            return std::tanh (x);
    }
    return x;
}

/**
    Multiband "grungy but full" distortion.

    The signal is distorted at 4x oversampling, then split at the crossover. Above the crossover you
    hear the distortion; below it `subKeep` blends the clean, phase-aligned low band back in, so the
    808's fundamental stays round and heavy however hard you drive it.
    Output level is auto-compensated so turning up drive changes tone, not loudness.
*/
class Dirt
{
public:
    static constexpr int kMaxBlock = 1024;

    struct Params
    {
        DirtType type = DirtType::Tape;
        float drive = 0.3f;      // 0..1
        float tone = 0.0f;       // -1 dark .. +1 bright
        float bias = 0.0f;       // 0..1
        float mix = 1.0f;        // 0..1
        float subKeep = 0.5f;    // 0..1
        float xoverHz = 120.0f;
    };

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        os.initProcessing ((size_t) maxBlock);
        latency = (int) std::lround (os.getLatencyInSamples());
        jassert (maxBlock <= kMaxBlock);
        for (auto& ring : alignRing)
            ring.assign ((size_t) std::max (1, latency) * 2, 0.0f);
        for (int ch = 0; ch < 2; ++ch)
        {
            dc[ch].prepare (sr);
            preHp[ch].setCutoff (70.0f, sr);
            ampPre[ch].setCutoff (650.0f, sr);
            ampCab[ch].set (Biquad::Type::LowPass, 4800.0f, 0.9f, 0.0f, sr);
            ampPeak[ch].set (Biquad::Type::Peak, 1600.0f, 0.9f, 4.5f, sr);
            fuzzPost[ch].set (Biquad::Type::LowPass, 6500.0f, 0.7f, 0.0f, sr);
            fuzzLo[ch].setCutoff (700.0f, sr);
            fuzzHi[ch].setCutoff (1300.0f, sr);
            tapePre[ch].setCutoff (2500.0f, sr);
            tapePost[ch].setCutoff (2500.0f, sr);
            tapeLoss[ch].setCutoff (12000.0f, sr);
            toneSplit[ch].setCutoff (900.0f, sr);
        }
        driveSm.reset (sr, 0.02);
        mixSm.reset (sr, 0.02);
        keepSm.reset (sr, 0.02);
        compSm.reset (sr, 0.05);
        reset();
    }

    void reset()
    {
        os.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            inSplit[ch].reset(); outSplit[ch].reset(); dc[ch].reset(); preHp[ch].reset();
            ampPre[ch].reset(); ampCab[ch].reset(); ampPeak[ch].reset(); fuzzPost[ch].reset(); toneSplit[ch].reset();
            fuzzLo[ch].reset(); fuzzHi[ch].reset(); tapePre[ch].reset(); tapePost[ch].reset(); tapeLoss[ch].reset();
            shaperState[ch] = {};
            crushHold[ch] = 0.0f; crushCount[ch] = 0;
            std::fill (alignRing[(size_t) ch].begin(), alignRing[(size_t) ch].end(), 0.0f);
        }
        delayPos = 0;
        lastCompKey = -1.0f;
    }

    int getLatency() const noexcept { return latency; }

    /** driveMod is added to drive (e.g. from an LFO) for this block. */
    void process (juce::dsp::AudioBlock<float>& block, const Params& p, float driveMod = 0.0f)
    {
        const int n = (int) block.getNumSamples();
        const float drive01 = clamp01 (p.drive + driveMod);
        const float driveGain = dbToGain (drive01 * dirtMaxDriveDb (p.type));

        updateCompensation (p.type, driveGain, p.bias);
        driveSm.setTargetValue (driveGain);
        mixSm.setTargetValue (p.mix);
        keepSm.setTargetValue (p.subKeep);
        compSm.setTargetValue (compensation);

        for (int ch = 0; ch < 2; ++ch)
        {
            inSplit[ch].set (p.xoverHz, sr);
            outSplit[ch].set (p.xoverHz, sr);
        }

        float* L = block.getChannelPointer (0);
        float* R = block.getChannelPointer (1);

        // Keep the dry signal and the clean low band, delayed to line up with the oversampled path.
        for (int i = 0; i < n; ++i)
        {
            dry[0][(size_t) i] = L[i];
            dry[1][(size_t) i] = R[i];
            cleanLow[0][(size_t) i] = inSplit[0].lowOnly (L[i]);
            cleanLow[1][(size_t) i] = inSplit[1].lowOnly (R[i]);
        }
        alignInPlace (n);

        // Pre-emphasis at base rate
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                float* d = ch == 0 ? L : R;
                float x = d[i];
                if (p.type == DirtType::Fuzz)      x = preHp[ch].hp (x) * 1.15f + x * 0.15f;
                else if (p.type == DirtType::Amp)  x = x + 1.8f * ampPre[ch].hp (x);
                else if (p.type == DirtType::Tape) x = x + 0.6f * tapePre[ch].hp (x); // pre-emphasis
                d[i] = x;
            }
        }

        // Oversampled shaper
        auto up = os.processSamplesUp (block);
        const int upN = (int) up.getNumSamples();
        const int factor = upN / std::max (1, n);
        const float bits = 16.0f - 13.0f * drive01;
        const float quant = std::exp2 (bits - 1.0f);
        const int holdLen = 1 + (int) (p.bias * 20.0f) * factor;
        const float meanCoef = 1.0f - onePoleCoef (0.008f, sr * factor);

        for (int ch = 0; ch < 2; ++ch)
        {
            float* d = up.getChannelPointer ((size_t) ch);
            auto sm = driveSm; // each channel walks the same ramp
            for (int i = 0; i < upN; ++i)
            {
                const float g = (i % factor == 0) ? sm.getNextValue() : sm.getCurrentValue();
                float y = shapeSample (p.type, d[i] * g, p.bias, shaperState[ch], meanCoef);
                if (p.type == DirtType::Crush)
                {
                    if (--crushCount[ch] <= 0)
                    {
                        crushHold[ch] = std::round (y * quant) / quant;
                        crushCount[ch] = holdLen;
                    }
                    y = crushHold[ch];
                }
                d[i] = y;
            }
        }
        driveSm.skip (n);

        os.processSamplesDown (block);

        for (int i = 0; i < n; ++i)
        {
            const float comp = compSm.getNextValue();
            const float mix = mixSm.getNextValue();
            const float keep = keepSm.getNextValue();

            for (int ch = 0; ch < 2; ++ch)
            {
                float* d = ch == 0 ? L : R;
                float y = dc[ch].process (d[i]) * comp;

                if (p.type == DirtType::Amp)
                    y = ampCab[ch].process (ampPeak[ch].process (y));
                else if (p.type == DirtType::Fuzz)  // Muff-style mid scoop
                    y = fuzzPost[ch].process (0.6f * fuzzLo[ch].lp (y) + 0.8f * fuzzHi[ch].hp (y));
                else if (p.type == DirtType::Tape)  // de-emphasis + head loss
                    y = tapeLoss[ch].lp (y - 0.375f * tapePost[ch].hp (y));

                float lo, hi;
                outSplit[ch].process (y, lo, hi);
                hi = applyTone (ch, hi, p.tone);

                const float wet = hi + lo * (1.0f - keep) + cleanLow[(size_t) ch][(size_t) i] * keep;
                d[i] = dry[(size_t) ch][(size_t) i] + (wet - dry[(size_t) ch][(size_t) i]) * mix;
            }
        }
    }

private:
    float applyTone (int ch, float x, float tone) noexcept
    {
        if (std::abs (tone) < 1.0e-3f)
            return x;
        const float lo = toneSplit[ch].lp (x);
        const float hi = x - lo;
        const float t = tone * 9.0f; // +-9 dB tilt around 900 Hz
        return lo * dbToGain (-t * 0.5f) + hi * dbToGain (t);
    }

    /** Delays dry + cleanLow by the oversampler latency so every path lines up. */
    void alignInPlace (int n)
    {
        if (latency <= 0)
            return;
        for (int i = 0; i < n; ++i)
        {
            const size_t idx = (size_t) delayPos * 2;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& ring = alignRing[(size_t) ch];
                std::swap (ring[idx], dry[(size_t) ch][(size_t) i]);
                std::swap (ring[idx + 1], cleanLow[(size_t) ch][(size_t) i]);
            }
            if (++delayPos >= latency)
                delayPos = 0;
        }
    }

    void updateCompensation (DirtType type, float driveGain, float bias)
    {
        const float key = (float) type * 1000.0f + driveGain * 3.0f + bias;
        if (std::abs (key - lastCompKey) < 1.0e-4f)
            return;
        lastCompKey = key;

        // Calibrate on a -6 dBFS sine: output RMS (minus DC) matches input RMS.
        // Run a few cycles so stateful curves settle, measure the last one.
        constexpr int N = 64, cycles = 6;
        ShaperState st;
        const float coef = 1.0f - onePoleCoef (0.008f, 64.0 * 55.0); // ~55 Hz test tone
        double inSq = 0.0, outSq = 0.0, mean = 0.0;
        for (int c = 0; c < cycles; ++c)
            for (int i = 0; i < N; ++i)
            {
                const float x = 0.5f * std::sin (kTwoPi * (float) i / (float) N);
                const float y = shapeSample (type, x * driveGain, bias, st, coef);
                if (c == cycles - 1)
                {
                    inSq += (double) x * x;
                    outSq += (double) y * y;
                    mean += y;
                }
            }
        mean /= N;
        outSq = std::max (1.0e-9, outSq / N - mean * mean) * N;

        compensation = juce::jlimit (0.05f, 4.0f, (float) std::sqrt (inSq / outSq));
    }

    using OS = juce::dsp::Oversampling<float>;
    OS os { 2, 2, OS::filterHalfBandPolyphaseIIR, true, true };

    double sr = 44100.0;
    int latency = 0, delayPos = 0;
    Crossover inSplit[2], outSplit[2];
    DcBlocker dc[2];
    OnePole preHp[2], ampPre[2], toneSplit[2], fuzzLo[2], fuzzHi[2], tapePre[2], tapePost[2], tapeLoss[2];
    Biquad ampCab[2], ampPeak[2], fuzzPost[2];
    ShaperState shaperState[2];
    float crushHold[2] {};
    int crushCount[2] {};
    float compensation = 1.0f, lastCompKey = -1.0f;

    std::array<std::array<float, kMaxBlock>, 2> dry {}, cleanLow {};
    std::array<std::vector<float>, 2> alignRing;

    juce::SmoothedValue<float> driveSm { 1.0f }, mixSm { 1.0f }, keepSm { 0.5f }, compSm { 1.0f };
};
} // namespace dd
