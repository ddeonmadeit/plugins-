#pragma once

#include <dd_dsp/dd_dsp.h>

namespace sunburst
{
/** Everything after the strings, resolved once per block. */
struct RigSettings
{
    bool acoustic = true;
    float tone = 0.0f, body = 0.5f, doubleTrack = 0.0f;
    bool compOn = false;   float comp = 0.5f;
    bool driveOn = false;  float driveGain = 0.5f, driveTone = 0.5f;
    bool fuzzOn = false;   float fuzzGain = 0.6f;
    bool chorusOn = false; float chorusRate = 0.6f, chorusDepth = 0.5f;
    bool tremOn = false;   double tremBeats = 0.25; float tremDepth = 0.5f;
    bool delayOn = false;  float delaySamples = 10000.0f, delayFeedback = 0.35f, delayMix = 0.25f;
    bool ampOn = false;    int ampModel = 0;
    float gain = 0.35f, bass = 0.0f, mid = 0.0f, treble = 0.0f, presence = 0.0f, levelDb = 0.0f;
    bool cabOn = true;     int cabType = 0; float mic = 0.3f;
    bool reverbOn = true;  float reverbMix = 0.2f, reverbSize = 0.45f;
    double beat = 0.0, beatsPerSample = 0.0;   // host position for the tremolo
};

/**
    The guitar rig: guitar tone and acoustic body, compressor, drive and fuzz pedals and a two-stage
    tube preamp (4x oversampled), tone stack, power amp, speaker cabinet with mic placement, then
    chorus, tremolo, delay and room. Stereo in, where L is the take and R its double (or a copy).
*/
class Rig
{
public:
    static constexpr int kMaxChunk = 64;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        os->initProcessing (kMaxChunk);
        osr = sr * 4.0;
        cab.prepare (sr);
        chorus.prepare (sr);
        delay.prepare (sr, 3.0f);
        space.prepare (sr);
        comp.prepare (sr);
        for (int ch = 0; ch < 2; ++ch)
        {
            driveHp[ch].setCutoff (720.0f, osr);
            fuzzHp[ch].setCutoff (90.0f, osr);
            fuzzMid[ch].set (dd::Biquad::Type::LowPass, 3800.0f, 0.7071f, 0.0f, osr);
            interstage[ch].set (dd::Biquad::Type::LowPass, 7000.0f, 0.6f, 0.0f, osr);
            dcPre[ch].setCutoff (12.0f, osr);
            dcPost[ch].setCutoff (10.0f, sr);
            sideLowCut[ch].set (dd::Biquad::Type::HighPass, 160.0f, 0.7071f, 0.0f, sr);
        }
        cache = {};
        cache.valid = false;
        reset();
    }

    void reset()
    {
        if (os != nullptr) os->reset();
        cab.reset();
        chorus.reset();
        delay.reset();
        space.reset();
        comp.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            for (auto* f : { &toneLp[ch], &driveHp[ch], &driveLp[ch], &fuzzHp[ch], &ampHp[ch], &dcPre[ch], &dcPost[ch] })
                f->reset();
            for (auto* b : { &toneShelf[ch], &fuzzMid[ch], &interstage[ch], &preMid[ch], &stackLow[ch], &stackMid[ch], &stackHigh[ch],
                             &presenceShelf[ch], &micShelf[ch], &micLow[ch], &sideLowCut[ch] })
                b->reset();
            for (auto& b : bodyEq[ch])
                b.reset();
        }
    }

    int getLatency() const { return os != nullptr ? (int) std::lround (os->getLatencyInSamples()) : 0; }

    void process (float* L, float* R, int n, const RigSettings& s)
    {
        updateFilters (s);

        for (int i = 0; i < n; ++i)
        {
            float x[2] = { L[i], s.doubleTrack > 0.001f ? R[i] : L[i] };
            for (int ch = 0; ch < 2; ++ch)
            {
                float y = x[ch];
                if (cache.tone < -0.005f)      y = toneLp[ch].lp (y);
                else if (cache.tone > 0.005f)  y = toneShelf[ch].process (y);
                if (s.acoustic && s.body > 0.001f)
                    for (auto& b : bodyEq[ch])
                        y = b.process (y);
                x[ch] = y;
            }
            if (s.compOn)
                comp.process (x[0], x[1], 0.35f + 0.65f * s.comp);
            L[i] = x[0];
            R[i] = x[1];
        }

        // Pedal drive, fuzz and the preamp run oversampled; a bypassed stage is a straight wire.
        {
            float* chans[2] = { L, R };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
            auto up = os->processSamplesUp (block);
            const int m = (int) up.getNumSamples();
            for (int ch = 0; ch < 2; ++ch)
            {
                float* d = up.getChannelPointer ((size_t) ch);
                for (int i = 0; i < m; ++i)
                    d[i] = shapeOversampled (ch, d[i], s);
            }
            os->processSamplesDown (block);
        }

        for (int i = 0; i < n; ++i)
        {
            float x[2] = { L[i], R[i] };
            if (s.ampOn)
                for (int ch = 0; ch < 2; ++ch)
                {
                    float y = stackHigh[ch].process (stackMid[ch].process (stackLow[ch].process (x[ch])));
                    // Power amp: sags into a soft clip, harder on the gainier models.
                    const float push = 1.0f + 2.5f * cache.powerDrive;
                    y = std::tanh (y * push) / push * (1.0f + 0.3f * cache.powerDrive);
                    y = presenceShelf[ch].process (y) * cache.ampLevel;
                    if (s.cabOn)
                        y = micLow[ch].process (micShelf[ch].process (cab.process (ch, y)));
                    x[ch] = y;
                }
            // Asymmetric clipping (drive, fuzz, preamp) leaves an offset; block it whichever stages ran.
            for (int ch = 0; ch < 2; ++ch)
                x[ch] = dcPost[ch].hp (x[ch]);

            // Double tracking: the take leans left and its double right, the wider the more Double.
            const float d = s.doubleTrack > 0.001f ? juce::jlimit (0.0f, 1.0f, s.doubleTrack) : 0.0f;
            const float a = 0.5f + 0.5f * d, b = 0.5f - 0.5f * d;
            const float l = x[0] * a + x[1] * b, r = x[1] * a + x[0] * b;
            L[i] = l;
            R[i] = r;
        }

        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            if (s.chorusOn)
            {
                float wl, wr;
                chorus.process (l, r, wl, wr, s.chorusRate, s.chorusDepth);
                l = 0.78f * l + 0.62f * wl;
                r = 0.78f * r + 0.62f * wr;
            }
            if (s.tremOn)
            {
                const double beat = s.beat + (double) i * s.beatsPerSample;
                const float phase = (float) (beat / s.tremBeats - std::floor (beat / s.tremBeats));
                const float g = 1.0f - s.tremDepth * 0.5f * (1.0f - std::cos (dd::kTwoPi * phase));
                l *= g;
                r *= g;
            }
            if (s.delayOn)
            {
                float wl, wr;
                delay.process (l, r, wl, wr, s.delayFeedback, true);
                l += wl * s.delayMix;
                r += wr * s.delayMix;
            }
            if (s.reverbOn && s.reverbMix > 0.001f)
            {
                float wl, wr;
                space.process (l, r, wl, wr);
                l += wl * s.reverbMix;
                r += wr * s.reverbMix;
            }
            L[i] = l;
            R[i] = r;
        }
    }

    void setDelayTime (float samples, bool jump) { delay.setTime (samples, jump); }

private:
    float shapeOversampled (int ch, float x, const RigSettings& s) noexcept
    {
        if (s.driveOn)
        {
            // Mid-hump overdrive: only the band above ~720 Hz is driven and clipped, the lows stay clean.
            const float hi = driveHp[ch].hp (x);
            const float clipped = std::tanh (hi * cache.driveK + 0.08f) - 0.0798f;
            x = driveLp[ch].lp (x * 0.5f + clipped * 0.45f) * cache.driveLevel;
        }
        if (s.fuzzOn)
        {
            float y = fuzzHp[ch].hp (x) * cache.fuzzK;
            y = std::atan (y) * 0.64f;
            y = fuzzMid[ch].process (y) * 3.0f;
            y = std::atan (y + 0.15f) * 0.64f - 0.0955f;
            x = y * 0.2f;
        }
        if (s.ampOn)
        {
            float y = ampHp[ch].hp (x);
            if (cache.preMidDb > 0.1f)
                y = preMid[ch].process (y);
            // Two triode stages: asymmetric, so even harmonics and a little bias shift when pushed.
            y = dcPre[ch].hp (std::tanh (y * cache.preK + 0.12f) - 0.1194f);
            if (cache.stage2K > 0.0f)
                y = std::tanh (interstage[ch].process (y) * cache.stage2K - 0.05f) + 0.04996f;
            x = y * cache.preMakeup;
        }
        return x;
    }

    void updateFilters (const RigSettings& s)
    {
        auto changed = [] (float a, float b) { return std::abs (a - b) > 1.0e-4f; };
        if (! cache.valid || changed (cache.tone, s.tone))
        {
            cache.tone = s.tone;
            for (int ch = 0; ch < 2; ++ch)
            {
                toneLp[ch].setCutoff (20000.0f * std::exp2 (s.tone * 4.5f), sr);
                toneShelf[ch].set (dd::Biquad::Type::HighShelf, 3200.0f, 0.7071f, s.tone * 8.0f, sr);
            }
        }
        if (! cache.valid || changed (cache.body, s.body))
        {
            cache.body = s.body;
            // A flat-top's air cavity and top plate, less of the archtop's boxy middle, a little air.
            for (int ch = 0; ch < 2; ++ch)
            {
                bodyEq[ch][0].set (dd::Biquad::Type::Peak, 98.0f, 1.1f, 6.5f * s.body, sr);
                bodyEq[ch][1].set (dd::Biquad::Type::Peak, 210.0f, 1.3f, 2.5f * s.body, sr);
                bodyEq[ch][2].set (dd::Biquad::Type::Peak, 520.0f, 0.9f, -3.0f * s.body, sr);
                bodyEq[ch][3].set (dd::Biquad::Type::HighShelf, 7500.0f, 0.7071f, 3.5f * s.body, sr);
            }
        }
        if (! cache.valid || changed (cache.driveGain, s.driveGain) || changed (cache.driveTone, s.driveTone))
        {
            cache.driveGain = s.driveGain;
            cache.driveTone = s.driveTone;
            cache.driveK = 2.0f + 120.0f * s.driveGain * s.driveGain;
            cache.driveLevel = dd::dbToGain (-5.0f - 5.0f * s.driveGain);
            for (int ch = 0; ch < 2; ++ch)
                driveLp[ch].setCutoff (700.0f * std::exp2 (s.driveTone * 3.3f), osr);
        }
        if (! cache.valid || changed (cache.fuzzGain, s.fuzzGain))
        {
            cache.fuzzGain = s.fuzzGain;
            cache.fuzzK = 6.0f + 500.0f * s.fuzzGain * s.fuzzGain;
        }
        if (! cache.valid || cache.ampModel != s.ampModel || changed (cache.gain, s.gain) || changed (cache.bass, s.bass)
            || changed (cache.mid, s.mid) || changed (cache.treble, s.treble) || changed (cache.presence, s.presence)
            || changed (cache.levelDb, s.levelDb))
        {
            cache.ampModel = s.ampModel;
            cache.gain = s.gain;
            cache.bass = s.bass;
            cache.mid = s.mid;
            cache.treble = s.treble;
            cache.presence = s.presence;
            cache.levelDb = s.levelDb;
            // Clean: a scooped black-panel; Crunch: a cranked plexi; Lead: a tight high-gain channel.
            // Make-up keeps each model roughly level across its Gain range (measured on chords).
            struct Model { float hp, preDb, preRange, stage2Db, preMidDb, midOffset, power, makeupDb, makeupSlope, makeupCurve; };
            static constexpr Model models[3] = {
                { 70.0f,  0.0f, 22.0f, -100.0f, 0.0f, -3.0f, 0.15f,  1.6f,   0.0f, 0.0f },
                { 95.0f,  6.0f, 26.0f,   4.0f,  2.5f, 1.0f, 0.45f,  -8.7f, -20.5f, 9.4f },
                { 150.0f, 12.0f, 30.0f, 12.0f,  5.0f, 0.0f, 0.65f, -17.0f,  -5.0f, 0.0f } };
            const auto& m = models[juce::jlimit (0, 2, s.ampModel)];
            const float preDb = m.preDb + m.preRange * s.gain;
            cache.preK = dd::dbToGain (preDb);
            cache.stage2K = m.stage2Db > -50.0f ? dd::dbToGain (m.stage2Db + 8.0f * s.gain) : 0.0f;
            cache.preMidDb = m.preMidDb;
            cache.powerDrive = m.power * (0.4f + 0.6f * s.gain);
            // Make-up: a clean amp gets louder with gain until it breaks up; saturated ones are already loud.
            cache.preMakeup = dd::dbToGain (m.makeupDb + (m.makeupSlope + m.makeupCurve * s.gain) * s.gain - (s.ampModel == 0 ? 0.72f * preDb : 0.0f));
            cache.ampLevel = dd::dbToGain (s.levelDb);
            for (int ch = 0; ch < 2; ++ch)
            {
                ampHp[ch].setCutoff (m.hp, osr);
                preMid[ch].set (dd::Biquad::Type::Peak, 900.0f, 0.8f, m.preMidDb, osr);
                stackLow[ch].set (dd::Biquad::Type::LowShelf, 120.0f, 0.7071f, s.bass * 12.0f, sr);
                stackMid[ch].set (dd::Biquad::Type::Peak, 650.0f, 0.8f, s.mid * 10.0f + m.midOffset, sr);
                stackHigh[ch].set (dd::Biquad::Type::HighShelf, 2800.0f, 0.7071f, s.treble * 12.0f, sr);
                presenceShelf[ch].set (dd::Biquad::Type::HighShelf, 4500.0f, 0.7071f, s.presence * 8.0f, sr);
            }
        }
        if (! cache.valid || cache.cabType != s.cabType || changed (cache.mic, s.mic))
        {
            cache.cabType = s.cabType;
            cache.mic = s.mic;
            cab.setType (s.cabType == 0 ? dd::CabSim::Type::Combo : dd::CabSim::Type::Stack);
            // Mic off the cone's centre: darker and a touch fuller.
            for (int ch = 0; ch < 2; ++ch)
            {
                micShelf[ch].set (dd::Biquad::Type::HighShelf, 2800.0f, 0.7071f, -9.0f * s.mic, sr);
                micLow[ch].set (dd::Biquad::Type::LowShelf, 180.0f, 0.7071f, 2.0f * s.mic, sr);
            }
        }
        if (! cache.valid || changed (cache.reverbSize, s.reverbSize))
        {
            cache.reverbSize = s.reverbSize;
            space.setSize (s.reverbSize);
        }
        cache.valid = true;
    }

    struct Cache
    {
        bool valid = false;
        float tone = 0, body = -1, driveGain = -1, driveTone = -1, fuzzGain = -1, gain = -1, bass = 9, mid = 9, treble = 9,
              presence = 9, levelDb = 99, mic = -1, reverbSize = -1;
        int ampModel = -1, cabType = -1;
        float driveK = 1, driveLevel = 1, fuzzK = 1, preK = 1, stage2K = 0, preMidDb = 0, powerDrive = 0, preMakeup = 1, ampLevel = 1;
    };

    double sr = 44100.0, osr = 176400.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    Cache cache;
    dd::OnePole toneLp[2], driveHp[2], driveLp[2], fuzzHp[2], ampHp[2], dcPre[2], dcPost[2];
    dd::Biquad toneShelf[2], fuzzMid[2], interstage[2], preMid[2], stackLow[2], stackMid[2], stackHigh[2], presenceShelf[2],
               micShelf[2], micLow[2], sideLowCut[2];
    std::array<dd::Biquad, 4> bodyEq[2];
    dd::GlueCompressor comp;
    dd::CabSim cab;
    dd::Chorus chorus;
    dd::StereoDelay delay;
    dd::Space space;
};
} // namespace sunburst
