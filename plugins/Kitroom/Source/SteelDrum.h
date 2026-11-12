#pragma once

#include <dd_dsp/dd_dsp.h>

namespace kitroom
{
/** Steel drum settings, resolved once per block. */
struct SteelSettings
{
    float gain = 1.0f, tune = 0.0f, lengthMs = 1400.0f, bright = 0.55f, shimmer = 0.4f, roll = 0.0f;
    int model = 0;
};

inline const juce::StringArray& steelModelNames()
{
    static const juce::StringArray n { "Tenor", "Double Second", "Cello", "Bass" };
    return n;
}

/**
    A steel pan (Trinidad's steel drum), synthesised. Each note area on a pan is tuned so its first partials
    land on the fundamental, the octave and the twelfth, which is what makes a pan sing instead of clank: here
    each partial is a pair of slightly detuned decaying sines (the shimmer of the hammered metal beating
    against itself), the octave swells in a few milliseconds after the strike as the note's energy moves up
    into it, and a short burst of filtered noise and a soft thump are the rubber-tipped stick. Higher notes
    ring shorter. Polyphonic, with no damper: a note rings on after its key is released, as on the real
    instrument. Holding a key with Roll up rolls it (fast re-strikes), the way pannists sustain long notes.
*/
class SteelDrum
{
public:
    static constexpr int kVoices = 12, kPool = kVoices + 4, kPartials = 5;   // the pool leaves room for stolen notes to fade

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        reset();
    }

    void reset()
    {
        for (auto& v : voices)
            v = Voice {};
        age = 0;
    }

    /** `note`: the sounding MIDI note (before the model's octave). */
    void noteOn (int key, int note, float velocity, const SteelSettings& s) noexcept
    {
        // The same note again re-strikes its own voice (it keeps ringing, as on a pan). Otherwise a free voice;
        // with kVoices already ringing, the oldest fades out in 5 ms.
        Voice* v = nullptr;
        for (auto& c : voices)
            if (c.active && c.key == key && c.fadeStep <= 0.0f)
                v = &c;
        if (v == nullptr)
        {
            int ringing = 0;
            Voice* oldest = nullptr;
            for (auto& c : voices)
                if (c.active && c.fadeStep <= 0.0f)
                {
                    ++ringing;
                    if (oldest == nullptr || c.started < oldest->started)
                        oldest = &c;
                }
            if (ringing >= kVoices && oldest != nullptr)
                oldest->fadeStep = 1.0f / (float) (0.005 * sr);
            for (auto& c : voices)
                if (! c.active)
                {
                    v = &c;
                    break;
                }
            if (v == nullptr)   // every slot still fading: take the quietest
            {
                for (auto& c : voices)
                    if (c.fadeStep > 0.0f && (v == nullptr || c.fade < v->fade))
                        v = &c;
                if (v == nullptr)
                    v = oldest != nullptr ? oldest : &voices[0];
            }
        }
        const bool fresh = ! v->active || v->key != key || v->fadeStep > 0.0f;
        if (fresh)
        {
            *v = Voice {};
            v->active = true;
            v->key = key;
            v->note = (float) note;
            v->pan = juce::jlimit (-0.2f, 0.2f, ((float) note - 76.0f) * 0.012f);   // low notes a little left, as on a tenor pan
            v->noise = 0x2545f491u ^ ((uint32_t) key * 2654435761u);
            setUpPartials (*v, s);
        }
        v->held = true;
        v->velocity = velocity;
        v->started = ++age;
        v->rollTimer = 0.0f;
        strike (*v, velocity, s);
    }

    void noteOff (int key) noexcept
    {
        for (auto& v : voices)
            if (v.active && v.key == key)
                v.held = false;   // no damper: it rings on; only the roll stops
    }

    void allOff() noexcept
    {
        for (auto& v : voices)
            if (v.active)
                v.fadeStep = 1.0f / (float) (0.01 * sr);
    }

    /** Adds the steel drum into L / R. */
    void render (float* L, float* R, int n, const SteelSettings& s) noexcept
    {
        const float outGain = s.gain * 0.24f;
        const float rollHz = 9.0f + 9.0f * s.roll;
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            const float gl = outGain * std::sqrt (0.5f * (1.0f - v.pan)), gr = outGain * std::sqrt (0.5f * (1.0f + v.pan));
            for (int i = 0; i < n; ++i)
            {
                // Rolls: re-strike a held note a little softer, a touch unevenly, while the key is down.
                if (v.held && s.roll > 0.001f)
                {
                    v.rollTimer += 1.0f / (float) sr;
                    const float wait = (v.strikes == 1 ? 0.14f : 1.0f / rollHz) * (0.92f + 0.16f * nextRandom (v.noise));
                    if (v.rollTimer >= wait)
                    {
                        v.rollTimer = 0.0f;
                        strike (v, v.velocity * (0.55f + 0.15f * nextRandom (v.noise)) * (0.6f + 0.4f * s.roll), s);
                    }
                }

                float y = 0.0f;
                for (int p = 0; p < v.numPartials; ++p)
                {
                    auto& m = v.part[(size_t) p];
                    // Two decaying phasors a fraction of a hertz apart (complex multiply = rotate and shrink).
                    const float ar = m.re * m.cr - m.im * m.ci, ai = m.re * m.ci + m.im * m.cr;
                    m.re = ar;
                    m.im = ai;
                    const float br = m.re2 * m.cr2 - m.im2 * m.ci2, bi = m.re2 * m.ci2 + m.im2 * m.cr2;
                    m.re2 = br;
                    m.im2 = bi;
                    m.swell += (1.0f - m.swell) * m.swellCoef;
                    y += (m.im + m.im2) * m.swell;
                }

                // The stick: a short burst of filtered noise (the tip on the metal) and a soft low thump.
                if (v.exc > 1.0e-5f)
                {
                    const float w = nextRandom (v.noise) * 2.0f - 1.0f;
                    v.bp1 += v.bpCoef * (w - v.bp1);
                    v.bp2 += v.bpCoef * (v.bp1 - v.bp2);
                    y += (v.bp1 - v.bp2) * v.exc * v.clickAmt;
                    v.thump += 0.02f * (v.exc * std::sin (v.thumpPhase) - v.thump);
                    v.thumpPhase += v.thumpInc;
                    y += v.thump * v.thumpAmt;
                    v.exc *= v.excDecay;
                }

                if (v.fadeStep > 0.0f)
                {
                    v.fade -= v.fadeStep;
                    if (v.fade <= 0.0f)
                    {
                        v.active = false;
                        break;
                    }
                    y *= v.fade;
                }
                L[i] += y * gl;
                R[i] += y * gr;
            }

            // Done once everything has rung out.
            if (v.active && ! v.held && (++v.checkCount & 15) == 0)
            {
                float e = v.exc;
                for (int p = 0; p < v.numPartials; ++p)
                    e += std::abs (v.part[(size_t) p].re) + std::abs (v.part[(size_t) p].im)
                       + std::abs (v.part[(size_t) p].re2) + std::abs (v.part[(size_t) p].im2);
                if (e < 1.0e-5f)
                    v.active = false;
            }
        }
    }

private:
    struct Partial
    {
        float re = 0.0f, im = 0.0f, cr = 1.0f, ci = 0.0f;       // one phasor and its rotation per sample
        float re2 = 0.0f, im2 = 0.0f, cr2 = 1.0f, ci2 = 0.0f;   // its slightly detuned twin
        float amp = 0.0f, swell = 1.0f, swellCoef = 1.0f;
    };

    struct Voice
    {
        bool active = false, held = false;
        int key = -1, numPartials = 0, strikes = 0, checkCount = 0;
        float note = 60.0f, velocity = 0.0f, pan = 0.0f, rollTimer = 0.0f;
        float fade = 1.0f, fadeStep = 0.0f;
        uint64_t started = 0;
        uint32_t noise = 1;
        std::array<Partial, kPartials> part {};
        float exc = 0.0f, excDecay = 0.99f, clickAmt = 0.0f, bp1 = 0.0f, bp2 = 0.0f, bpCoef = 0.5f;
        float thump = 0.0f, thumpPhase = 0.0f, thumpInc = 0.0f, thumpAmt = 0.0f;
    };

    struct Model
    {
        float octave;                                   // semitones added to the note
        std::array<float, kPartials> ratio, amp, life;  // partial frequency / level / ring time vs the fundamental
        float octaveSwellMs, click, clickHz, thump;
    };

    static const Model& model (int i) noexcept
    {
        // Tenor: bright and singing, the twelfth clearly there. Double Second: rounder, octave-led. Cello and
        // Bass: an octave and two down, darker, longer, more stick thump.
        static const Model m[] {
            { 0.0f,   { 1.0f, 2.0f, 3.0f, 4.02f, 5.05f }, { 1.0f, 0.72f, 0.38f, 0.16f, 0.07f }, { 1.0f, 0.75f, 0.45f, 0.3f, 0.2f }, 12.0f, 0.55f, 3200.0f, 0.18f },
            { 0.0f,   { 1.0f, 2.0f, 3.0f, 4.02f, 6.1f },  { 1.0f, 0.85f, 0.2f, 0.08f, 0.03f },  { 1.0f, 0.8f, 0.4f, 0.25f, 0.15f }, 18.0f, 0.4f, 2600.0f, 0.22f },
            { -12.0f, { 1.0f, 2.0f, 3.0f, 4.0f, 5.1f },   { 1.0f, 0.65f, 0.22f, 0.08f, 0.03f }, { 1.0f, 0.8f, 0.5f, 0.3f, 0.2f },   20.0f, 0.35f, 2000.0f, 0.35f },
            { -24.0f, { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f },   { 1.0f, 0.5f, 0.15f, 0.05f, 0.02f },  { 1.0f, 0.7f, 0.4f, 0.25f, 0.15f }, 25.0f, 0.25f, 1500.0f, 0.5f },
        };
        return m[juce::jlimit (0, 3, i)];
    }

    void setUpPartials (Voice& v, const SteelSettings& s) noexcept
    {
        const auto& m = model (s.model);
        const float f0 = 440.0f * std::exp2 ((v.note + s.tune + m.octave - 69.0f) / 12.0f);
        // High notes ring shorter (a small, stiff note area); Length sets the middle of the pan.
        const float t60 = std::max (0.12f, s.lengthMs * 0.001f) * std::pow (440.0f / juce::jlimit (60.0f, 2500.0f, f0), 0.45f);
        const float nyq = (float) sr * 0.45f;
        const float spread = 0.0004f + 0.003f * s.shimmer;   // how far apart each partial's twins are (beats)
        v.numPartials = 0;
        for (int p = 0; p < kPartials; ++p)
        {
            const float f = f0 * m.ratio[(size_t) p];
            if (f >= nyq)
                break;
            auto& pt = v.part[(size_t) v.numPartials++];
            const float life = t60 * m.life[(size_t) p];
            const float r = std::exp (-6.9f / (life * (float) sr));   // -60 dB after `life` seconds
            const float w1 = dd::kTwoPi * f * (1.0f - spread) / (float) sr, w2 = dd::kTwoPi * f * (1.0f + spread) / (float) sr;
            pt.cr = r * std::cos (w1); pt.ci = r * std::sin (w1);
            pt.cr2 = r * std::cos (w2); pt.ci2 = r * std::sin (w2);
            // Brightness lifts the upper partials; the octave's swell is the note "blooming" after the hit.
            const float tilt = p == 0 ? 1.0f : std::pow (0.35f + 1.3f * s.bright, (float) p * 0.8f);
            pt.amp = m.amp[(size_t) p] * tilt;
            pt.swell = p == 1 ? 0.25f : 1.0f;
            pt.swellCoef = p == 1 ? 1.0f - std::exp (-1.0f / (m.octaveSwellMs * 0.001f * (float) sr)) : 1.0f;
        }
        v.bpCoef = 1.0f - std::exp (-dd::kTwoPi * juce::jmin (nyq, m.clickHz * (0.6f + 0.9f * s.bright)) / (float) sr);
        v.clickAmt = m.click * (0.4f + 0.9f * s.bright);
        v.thumpAmt = m.thump;
        v.thumpInc = dd::kTwoPi * juce::jlimit (60.0f, 220.0f, f0 * 0.5f) / (float) sr;
        v.excDecay = std::exp (-1.0f / (0.0025f * (float) sr));
    }

    void strike (Voice& v, float velocity, const SteelSettings& s) noexcept
    {
        // Harder hits bring out the upper partials more than the fundamental.
        const float level = 0.25f + 0.75f * velocity;
        for (int p = 0; p < v.numPartials; ++p)
        {
            auto& pt = v.part[(size_t) p];
            const float a = pt.amp * level * (p == 0 ? 1.0f : 0.55f + 0.45f * velocity) * 0.5f;
            // Added to the real part: the output (the imaginary part) starts from zero, so no click from the partials.
            pt.re += a;
            pt.re2 += a;
            if (p == 1)
                pt.swell = std::min (pt.swell, 0.35f);
        }
        v.exc = level * (0.5f + 0.5f * s.bright);
        v.thumpPhase = 0.0f;
        ++v.strikes;
    }

    static float nextRandom (uint32_t& state) noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (state & 0xffffff) / 16777216.0f;
    }

    double sr = 48000.0;
    std::array<Voice, kPool> voices {};
    uint64_t age = 0;
};
} // namespace kitroom
