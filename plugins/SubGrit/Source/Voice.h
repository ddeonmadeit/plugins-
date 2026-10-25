#pragma once

#include <dd_dsp/dd_dsp.h>

namespace sg
{
/** Everything a voice needs for one render chunk, resolved from parameters by the engine. */
struct VoiceParams
{
    float pitchShift = 0.0f;     // octave/tune/fine, semitones
    float bend = 0.0f;           // semitones
    float glideCoef = 0.0f;      // per-sample one-pole coefficient, 0 = instant
    float punchAmt = 0.0f, punchTimeMs = 45.0f;
    float diveAmt = 0.0f, diveTimeMs = 400.0f;
    float velocitySens = 0.4f;
    float drift = 0.0f;

    float bodyLevel = 0.85f, bodyDrive = 0.0f;
    float clickLevel = 0.0f, clickTone = 0.5f;

    struct Osc
    {
        float level = 0.0f;
        dd::Wave wave = dd::Wave::Saw;
        float offset = 0.0f;     // semitones from note
        int voices = 1;
        float detune = 0.0f, width = 0.0f;
    } osc[2];

    float noiseLevel = 0.0f, noiseColor = 0.5f;

    int filterMode = 0;
    float cutoff = 3000.0f, resonance = 0.0f, filterDrive = 0.0f, filterEnv = 0.0f, keyTrack = 0.0f;
    bool bodyToFilter = false;

    float fA = 1, fD = 400, fS = 0, fR = 250;
    float aA = 0.5f, aD = 2500, aS = 0.6f, aR = 180;

    float lfoPitch = 0.0f, lfoCutoff = 0.0f, lfoAmp = 0.0f;
};

class Voice
{
public:
    void prepare (double sampleRate, uint32_t seed)
    {
        sr = sampleRate;
        invSr = (float) (1.0 / sr);
        rng.seed (seed);
        ampEnv.setSampleRate (sr);
        filtEnv.setSampleRate (sr);
        for (auto& d : drift) d.prepare (sr, 0.35f);
        clickHp.setCutoff (2500.0f, sr);
        chokeStep = 1.0f / (0.006f * (float) sr);
        reset();
    }

    void reset()
    {
        active = gate = false;
        ampEnv.reset();
        filtEnv.reset();
        for (auto& f : ladder) f.reset();
        for (auto& f : svf) f.reset();
    }

    /** Fresh note. startPitch != note means glide in from startPitch. */
    void start (int note, float velocity, float startPitch, const VoiceParams& vp, uint64_t stamp)
    {
        midiNote = note;
        targetPitch = (float) note;
        pitch = startPitch;
        vel = velocity;
        age = stamp;
        active = gate = true;
        choking = false;
        chokePos = 0.0f;
        released = false;
        diveLevel = 0.0f;

        ampEnv.reset();
        filtEnv.reset();
        ampEnv.setParams (vp.aA, vp.aD, vp.aS, vp.aR);
        filtEnv.setParams (vp.fA, vp.fD, vp.fS, vp.fR);
        ampEnv.noteOn();
        filtEnv.noteOn();
        punchEnv.trigger (vp.punchTimeMs, sr);

        bodyPhase = 0.0f;
        clickPhase = 0.0f;
        clickEnv.trigger (3.0f, sr);
        clickNoiseEnv.trigger (2.0f, sr);
        clickActive = vp.clickLevel > 0.0001f;

        for (int o = 0; o < 2; ++o)
        {
            uni[o].noteOn (rng, vp.drift);
            drift[o].reset (rng);
            cfg[o] = {};
        }
        for (auto& f : ladder) f.reset();
        for (auto& f : svf) f.reset();
        for (auto& n : noiseLp) n.reset();
    }

    /** Mono legato: move to a new note without retriggering envelopes. */
    void legatoTo (int note, bool glide)
    {
        midiNote = note;
        targetPitch = (float) note;
        if (! glide)
            pitch = targetPitch;
    }

    void release()
    {
        gate = false;
        released = true;
        ampEnv.noteOff();
        filtEnv.noteOff();
    }

    void choke() { if (active) { choking = true; gate = false; } }

    bool isActive() const noexcept     { return active; }
    bool isGateOn() const noexcept     { return active && gate; }
    bool isChoking() const noexcept    { return choking; }
    int getNote() const noexcept       { return midiNote; }
    float getPitch() const noexcept    { return pitch; }
    uint64_t getAge() const noexcept   { return age; }

    void render (float* outL, float* outR, int n, const VoiceParams& vp, const float* lfo) noexcept
    {
        if (! active)
            return;

        ampEnv.setParams (vp.aA, vp.aD, vp.aS, vp.aR);
        filtEnv.setParams (vp.fA, vp.fD, vp.fS, vp.fR);

        bool oscOn[2];
        for (int o = 0; o < 2; ++o)
        {
            const auto& op = vp.osc[o];
            oscOn[o] = op.level > 0.0001f;
            if (oscOn[o] && (cfg[o].voices != op.voices || ! juce::exactlyEqual (cfg[o].detune, op.detune)
                             || ! juce::exactlyEqual (cfg[o].width, op.width)))
            {
                uni[o].configure (op.voices, op.detune, op.width);
                cfg[o] = { op.voices, op.detune, op.width };
            }
        }

        const bool noiseOn = vp.noiseLevel > 0.0001f;
        if (noiseOn)
            for (auto& f : noiseLp)
                f.setCutoff (300.0f * std::exp2 (vp.noiseColor * 5.7f), sr); // 300 Hz .. 16 kHz

        const bool filterOn = oscOn[0] || oscOn[1] || noiseOn || vp.bodyToFilter;
        const float bodyK = vp.bodyDrive * 6.0f;
        const float bodyNorm = bodyK > 0.01f ? 1.0f / std::tanh (bodyK) : 1.0f;
        const float velGain = 1.0f - vp.velocitySens + vp.velocitySens * vel;
        const float diveCoef = 1.0f - dd::onePoleCoef (vp.diveTimeMs * 0.001f / 3.0f, sr);
        const float clickHz = 1000.0f * std::exp2 (vp.clickTone * 2.6f);
        const float fDrive = 1.0f + vp.filterDrive * 7.0f;
        const float svfK = 1.0f / (0.6f + vp.resonance * 11.0f);
        const float driftCents = vp.drift * 9.0f;

        for (int i = 0; i < n; ++i)
        {
            pitch = targetPitch + (pitch - targetPitch) * vp.glideCoef;

            const float punch = vp.punchAmt * punchEnv.process();
            if (released)
                diveLevel += (1.0f - diveLevel) * diveCoef;

            const float lfoV = lfo[i];
            const float notePitch = pitch + vp.pitchShift + vp.bend + punch + vp.diveAmt * diveLevel + lfoV * vp.lfoPitch;

            // 808 body: phase-reset sine, optionally rounded toward a square
            bodyPhase += dd::midiToHz (notePitch) * invSr;
            if (bodyPhase >= 1.0f)
                bodyPhase -= std::floor (bodyPhase);
            float body = std::sin (dd::kTwoPi * bodyPhase);
            if (bodyK > 0.01f)
                body = std::tanh (bodyK * body) * bodyNorm;
            body *= vp.bodyLevel * kBodyGain;

            float l = 0.0f, r = 0.0f;
            if (filterOn)
            {
                float tl = 0.0f, tr = 0.0f;
                for (int o = 0; o < 2; ++o)
                {
                    if (! oscOn[o])
                        continue;
                    const auto& op = vp.osc[o];
                    const float dv = drift[o].process (rng) * driftCents * 0.01f;
                    const float dt = dd::midiToHz (notePitch + op.offset + dv) * invSr;
                    float ol, orr;
                    uni[o].process (dt, op.wave, ol, orr);
                    tl += ol * op.level * kOscGain;
                    tr += orr * op.level * kOscGain;
                }
                if (noiseOn)
                {
                    tl += noiseLp[0].lp (rng.bi()) * vp.noiseLevel * kNoiseGain;
                    tr += noiseLp[1].lp (rng.bi()) * vp.noiseLevel * kNoiseGain;
                }
                if (vp.bodyToFilter)
                {
                    tl += body;
                    tr += body;
                }

                const float fe = filtEnv.process();
                const float mod = vp.filterEnv * fe
                                + vp.keyTrack * (pitch + vp.pitchShift - 60.0f) * (1.0f / 12.0f)
                                + lfoV * vp.lfoCutoff;
                const float g = dd::tptG (vp.cutoff * std::exp2 (mod), sr);

                if (vp.filterMode == 0)
                {
                    tl = ladder[0].process (tl, g, vp.resonance, fDrive);
                    tr = ladder[1].process (tr, g, vp.resonance, fDrive);
                }
                else
                {
                    const float pre = 1.0f / std::sqrt (fDrive);
                    float lp, bp, hp;
                    svf[0].setCoeffs (g, svfK);
                    svf[1].setCoeffs (g, svfK);
                    svf[0].process (std::tanh (tl * fDrive) * pre, lp, bp, hp);
                    tl = vp.filterMode == 1 ? lp : (vp.filterMode == 2 ? bp : hp);
                    svf[1].process (std::tanh (tr * fDrive) * pre, lp, bp, hp);
                    tr = vp.filterMode == 1 ? lp : (vp.filterMode == 2 ? bp : hp);
                }
                l = tl;
                r = tr;
            }

            if (! vp.bodyToFilter)
            {
                l += body;
                r += body;
            }

            const float trem = 1.0f - vp.lfoAmp * (0.5f - 0.5f * lfoV);
            const float amp = ampEnv.process() * velGain * trem;
            l *= amp;
            r *= amp;

            if (clickActive)
            {
                const float e = clickEnv.process();
                const float ne = clickNoiseEnv.process();
                clickPhase += clickHz * (1.0f + e) * invSr;
                if (clickPhase >= 1.0f) clickPhase -= 1.0f;
                const float c = (std::sin (dd::kTwoPi * clickPhase) * e + clickHp.hp (rng.bi()) * ne * 0.6f)
                                * vp.clickLevel * kClickGain * velGain;
                l += c;
                r += c;
                if (e < 1.0e-4f)
                    clickActive = false;
            }

            if (choking)
            {
                // Raised-cosine fade: no slope discontinuity at either end, so no tick.
                chokePos += chokeStep;
                if (chokePos >= 1.0f)
                {
                    active = false;
                    return;
                }
                const float g = 0.5f + 0.5f * std::cos (dd::kPi * chokePos);
                l *= g;
                r *= g;
            }

            outL[i] += l;
            outR[i] += r;
        }

        if (! ampEnv.isActive() && ! clickActive)
            active = false;
    }

private:
    static constexpr float kBodyGain = 0.8f, kOscGain = 0.42f, kNoiseGain = 0.35f, kClickGain = 0.7f;

    struct OscConfig { int voices = 0; float detune = -1.0f, width = -1.0f; };

    double sr = 44100.0;
    float invSr = 1.0f / 44100.0f;
    dd::Rng rng;

    bool active = false, gate = false, released = false, choking = false, clickActive = false;
    int midiNote = 60;
    float vel = 1.0f, pitch = 60.0f, targetPitch = 60.0f;
    float diveLevel = 0.0f, chokePos = 0.0f, chokeStep = 0.01f;
    uint64_t age = 0;

    dd::Adsr ampEnv, filtEnv;
    dd::DecayEnv punchEnv, clickEnv, clickNoiseEnv;

    float bodyPhase = 0.0f, clickPhase = 0.0f;
    dd::OnePole clickHp;

    dd::UnisonOscillator uni[2];
    OscConfig cfg[2];
    dd::Drift drift[2];
    dd::OnePole noiseLp[2];

    dd::LadderFilter ladder[2];
    dd::Svf svf[2];
};
} // namespace sg
