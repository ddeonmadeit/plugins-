#pragma once

#include "Layer.h"

namespace ag
{
/** Everything a voice needs for one block, resolved from the parameters and macros by the processor. */
struct VoiceParams
{
    std::array<LayerParams, 2> layer {};
    float noiseLevel = 0.0f, noiseTone = 0.5f;
    int noiseType = 0;
    int filterType = 0;
    float cutoff = 4000.0f, reso = 0.1f, filterDrive = 0.1f, filterEnv = 0.0f, keyTrack = 0.5f, velFilter = 0.2f;
    float aA = 400.0f, aD = 3000.0f, aS = 0.8f, aR = 1500.0f;
    float mA = 800.0f, mD = 4000.0f, mS = 0.3f, mR = 2000.0f;
    float velocity = 0.4f;
    float glideCoef = 0.0f, bend = 0.0f;
    dd::Lfo::Shape lfo1Shape = dd::Lfo::Shape::Sine;
    float lfo1Rate = 5.0f, lfo1Fade = 600.0f, lfo1Pitch = 0.0f, lfo1Cutoff = 0.0f, lfo1Amp = 0.0f, lfo1Pan = 0.0f, lfo1Morph = 0.0f;
    float globalCutoff = 0.0f;   // LFO 2, octaves
};

class Voice
{
public:
    static constexpr int kBlock = 64;

    void prepare (double sampleRate, uint32_t seed)
    {
        sr = sampleRate;
        rng.seed (seed);
        for (int l = 0; l < 2; ++l)
            layers[(size_t) l].prepare (sr, seed * 31u + (uint32_t) l * 977u);
        amp.setSampleRate (sr);
        mod.setSampleRate (sr);
        lfo.prepare (sr);
        windDrift.prepare (sr, 0.15f);
        chokeStep = 1.0f / (0.006f * (float) sr);
        reset();
    }

    void reset()
    {
        active = gate = choking = false;
        amp.reset();
        mod.reset();
        for (auto& f : ladder) f.reset();
        for (auto& f : svf) f.reset();
        for (auto& f : noiseLp) f.reset();
        for (auto& f : noiseBp) f.reset();
        crackle = 0.0f;
    }

    void start (int newNote, float vel, float fromPitch, const VoiceParams& vp, uint32_t stamp)
    {
        const bool wasActive = active && ! choking;
        note = newNote;
        targetPitch = (float) newNote;
        pitch = fromPitch;
        velocity = vel;
        age = stamp;
        active = gate = true;
        choking = false;
        chokeGain = 1.0f;
        if (! wasActive)
        {
            amp.reset();
            mod.reset();
            for (auto& f : ladder) f.reset();
            for (auto& f : svf) f.reset();
        }
        for (int l = 0; l < 2; ++l)
            layers[(size_t) l].start (vp.layer[(size_t) l], vel, ! wasActive);
        lfo.retrigger();
        windDrift.reset (rng);
        amp.noteOn();
        mod.noteOn();
    }

    void legatoTo (int newNote) noexcept { note = newNote; targetPitch = (float) newNote; }
    void release() noexcept              { gate = false; amp.noteOff(); mod.noteOff(); }
    void choke() noexcept                { if (active) { choking = true; gate = false; } }

    bool isActive() const noexcept   { return active; }
    bool isGateOn() const noexcept   { return active && gate && ! choking; }
    bool isChoking() const noexcept  { return choking; }
    int getNote() const noexcept     { return note; }
    float getPitch() const noexcept  { return pitch; }
    uint32_t getAge() const noexcept { return age; }

    void render (float* L, float* R, int n, const VoiceParams& vp) noexcept
    {
        amp.setParams (vp.aA, vp.aD, vp.aS, vp.aR);
        mod.setParams (vp.mA, vp.mD, vp.mS, vp.mR);
        lfo.setFade (vp.lfo1Fade);

        for (int i = 0; i < n; ++i)
        {
            pitch = targetPitch + (pitch - targetPitch) * vp.glideCoef;
            const float l1 = lfo.process (vp.lfo1Rate, vp.lfo1Shape);
            lfoBuf[(size_t) i] = l1;
            hz[(size_t) i] = dd::midiToHz (pitch + vp.bend + l1 * vp.lfo1Pitch * 0.01f);
            modEnv[(size_t) i] = mod.process();
            morph[(size_t) i] = l1 * vp.lfo1Morph * 0.5f;
        }

        std::fill (bufL.begin(), bufL.begin() + n, 0.0f);
        std::fill (bufR.begin(), bufR.begin() + n, 0.0f);
        for (int l = 0; l < 2; ++l)
            layers[(size_t) l].render (vp.layer[(size_t) l], hz.data(), modEnv.data(), morph.data(), bufL.data(), bufR.data(), n);
        if (vp.noiseLevel > 0.0001f)
            addNoise (vp, n);

        const float baseOct = vp.keyTrack * ((float) note - 60.0f) / 12.0f + vp.velFilter * (velocity - 0.75f) * 2.0f + vp.globalCutoff;
        const float velGain = 1.0f - vp.velocity * (1.0f - velocity);
        const float drive = 1.0f + vp.filterDrive * 5.0f;
        float g = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            if ((i & 3) == 0)
            {
                const float oct = baseOct + vp.filterEnv * modEnv[(size_t) i] + vp.lfo1Cutoff * lfoBuf[(size_t) i];
                g = dd::tptG (juce::jlimit (20.0f, 19000.0f, vp.cutoff * std::exp2 (oct)), sr);
            }
            float yl, yr;
            if (vp.filterType == 0)
            {
                yl = ladder[0].process (bufL[(size_t) i], g, vp.reso * 0.95f, drive);
                yr = ladder[1].process (bufR[(size_t) i], g, vp.reso * 0.95f, drive);
            }
            else
            {
                const float k = 2.0f - 1.9f * vp.reso;
                float out[2];
                for (int ch = 0; ch < 2; ++ch)
                {
                    svf[ch].setCoeffs (g, k);
                    float lp, bp, hp;
                    const float x = std::tanh ((ch == 0 ? bufL : bufR)[(size_t) i] * drive) / std::sqrt (drive);
                    svf[ch].process (x, lp, bp, hp);
                    out[ch] = vp.filterType == 1 ? lp : (vp.filterType == 2 ? bp * k : hp);
                }
                yl = out[0];
                yr = out[1];
            }

            const float l1 = lfoBuf[(size_t) i];
            float a = amp.process() * velGain * (1.0f - vp.lfo1Amp * 0.5f * (1.0f + l1));
            if (choking)
            {
                chokeGain -= chokeStep;
                if (chokeGain <= 0.0f)
                {
                    reset();
                    return;
                }
                a *= chokeGain;
            }
            const float pan = vp.lfo1Pan * l1;
            L[i] += yl * a * (1.0f - std::max (0.0f, pan));
            R[i] += yr * a * (1.0f + std::min (0.0f, pan));
        }

        if (! amp.isActive())
            reset();
    }

private:
    void addNoise (const VoiceParams& vp, int n) noexcept
    {
        const float level = vp.noiseLevel * 0.35f;
        switch (vp.noiseType)
        {
            case 0:   // hiss: soft tape / air hiss
                for (auto& f : noiseLp) f.setCutoff (2000.0f * std::exp2 (vp.noiseTone * 3.0f), sr);
                for (int i = 0; i < n; ++i)
                {
                    bufL[(size_t) i] += noiseLp[0].lp (rng.bi()) * level;
                    bufR[(size_t) i] += noiseLp[1].lp (rng.bi()) * level;
                }
                break;
            case 1:   // vinyl: sparse crackle over a little dust
            {
                const float rate = 0.0004f + 0.0012f * vp.noiseTone;
                for (int i = 0; i < n; ++i)
                {
                    if (rng.uni() < rate)
                        crackle = rng.bi() * (0.5f + 0.5f * rng.uni());
                    crackle *= 0.72f;
                    const float dust = rng.bi() * 0.06f;
                    bufL[(size_t) i] += (crackle + dust) * level * 2.0f;
                    bufR[(size_t) i] += (crackle * 0.8f + dust) * level * 2.0f;
                }
                break;
            }
            default:  // wind: a slowly wandering band of noise
            {
                const float centre = (350.0f + 2200.0f * vp.noiseTone) * std::exp2 (windDrift.process (rng) * 1.2f);
                for (auto& f : noiseBp) f.set (centre, 2.2f, sr);
                for (int i = 0; i < n; ++i)
                {
                    float lp, bp, hp;
                    noiseBp[0].process (rng.bi(), lp, bp, hp);
                    bufL[(size_t) i] += bp * level * 1.6f;
                    noiseBp[1].process (rng.bi(), lp, bp, hp);
                    bufR[(size_t) i] += bp * level * 1.6f;
                }
                break;
            }
        }
    }

    double sr = 44100.0;
    dd::Rng rng;
    std::array<Layer, 2> layers {};
    dd::Adsr amp, mod;
    dd::Lfo lfo;
    dd::LadderFilter ladder[2];
    dd::Svf svf[2], noiseBp[2];
    dd::OnePole noiseLp[2];
    dd::Drift windDrift;
    std::array<float, kBlock> hz {}, modEnv {}, morph {}, lfoBuf {}, bufL {}, bufR {};
    int note = 60;
    float pitch = 60.0f, targetPitch = 60.0f, velocity = 1.0f, chokeGain = 1.0f, chokeStep = 0.01f, crackle = 0.0f;
    uint32_t age = 0;
    bool active = false, gate = false, choking = false;
};
} // namespace ag
