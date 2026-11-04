#pragma once

#include <dd_dsp/dd_dsp.h>

/*
    Air exciter (the job Fresh Air does): a "mid air" band (about 2.5-9 kHz, presence) and a "high air"
    band (9 kHz up, sheen). Each is lifted dynamically, more when the band is quiet and less when it is
    already loud, so breaths and consonants open up without sibilants spiking, and each gets a little of its
    own harmonic content from a soft saturator (high-passed, so no low intermodulation). Runs 2x
    oversampled so the generated harmonics don't alias.
*/
namespace aircheck
{
class Air
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        os.initProcessing ((size_t) maxBlock);
        const double osr = sr * 2.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            midHp[ch].set (dd::Biquad::Type::HighPass, 2500.0f, 0.707f, 0.0f, osr);
            midLp[ch].set (dd::Biquad::Type::LowPass, 9000.0f, 0.707f, 0.0f, osr);
            highHp[ch].set (dd::Biquad::Type::HighPass, 9000.0f, 0.707f, 0.0f, osr);
            harmHp[ch].set (dd::Biquad::Type::HighPass, 3500.0f, 0.707f, 0.0f, osr);
            topLp[ch].set (dd::Biquad::Type::LowPass, (float) std::min (21000.0, sr * 0.45), 0.707f, 0.0f, osr);
        }
        att = dd::onePoleCoef (0.002f, osr);
        rel = dd::onePoleCoef (0.07f, osr);
        reset();
    }

    void reset()
    {
        os.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            midHp[ch].reset(); midLp[ch].reset(); highHp[ch].reset(); harmHp[ch].reset(); topLp[ch].reset();
            envM[ch] = envH[ch] = 0.0f;
        }
    }

    float getLatency() const noexcept { return os.getLatencyInSamples(); }

    void process (juce::dsp::AudioBlock<float>& block, float mid, float high) noexcept
    {
        if (mid <= 0.001f && high <= 0.001f)
        {
            // Keep the oversampler's delay so latency never changes.
            auto up = os.processSamplesUp (block);
            juce::ignoreUnused (up);
            os.processSamplesDown (block);
            return;
        }
        auto up = os.processSamplesUp (block);
        for (size_t ch = 0; ch < up.getNumChannels() && ch < 2; ++ch)
        {
            auto* d = up.getChannelPointer (ch);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                const float x = d[i];
                const float bm = midLp[ch].process (midHp[ch].process (x));
                const float bh = highHp[ch].process (x);
                const float am = std::abs (bm), ah = std::abs (bh);
                envM[ch] = am + (envM[ch] - am) * (am > envM[ch] ? att : rel);
                envH[ch] = ah + (envH[ch] - ah) * (ah > envH[ch] ? att : rel);
                // Up to about +8 dB (mid) and +11 dB (high) on quiet material, much less on loud bursts.
                const float gm = mid * 1.5f / (1.0f + envM[ch] * 10.0f);
                const float gh = high * 2.6f / (1.0f + envH[ch] * 14.0f);
                // Harmonics of the bands themselves: the distortion part of a soft saturator.
                const float dm = std::tanh (bm * 3.0f) / 3.0f;
                const float dh = std::tanh (bh * 4.0f) / 4.0f;
                const float harm = harmHp[ch].process ((dm - bm * 0.95f) * mid * 1.2f + (dh - bh * 0.95f) * high * 1.6f);
                d[i] = x + topLp[ch].process (bm * gm + bh * gh + harm);
            }
        }
        os.processSamplesDown (block);
    }

private:
    using OS = juce::dsp::Oversampling<float>;
    double sr = 48000.0;
    OS os { 2, 1, OS::filterHalfBandFIREquiripple, true, true };   // linear phase, whole-sample latency
    dd::Biquad midHp[2], midLp[2], highHp[2], harmHp[2], topLp[2];
    float envM[2] {}, envH[2] {}, att = 0.0f, rel = 0.0f;
};
} // namespace aircheck
