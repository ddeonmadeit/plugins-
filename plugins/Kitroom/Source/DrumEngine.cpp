#include "DrumEngine.h"

namespace kitroom
{
void DrumEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    rng.seed (0x51ab1e5u);
    reset();
}

void DrumEngine::reset()
{
    for (auto& v : voices) v.active = false;
    for (auto& s : lastHit) s.fill (-1);
}

int DrumEngine::activeVoices() const noexcept
{
    int n = 0;
    for (auto& v : voices) n += v.active ? 1 : 0;
    return n;
}

const ArticulationData* DrumEngine::resolve (const SoundData* sound, int slot, int art) const
{
    if (sound == nullptr)
        return nullptr;
    const auto& defs = kr::slots()[(size_t) slot].articulations;
    if (! juce::isPositiveAndBelow (art, (int) defs.size()))
        return nullptr;

    // Follow the fallback chain (e.g. half-open hat -> open), at most a few steps.
    juce::String id = defs[(size_t) art].id;
    for (int step = 0; step < 3 && id.isNotEmpty(); ++step)
    {
        if (auto* a = sound->find (id))
            return a;
        juce::String next;
        for (auto& d : defs)
            if (id == d.id)
                next = d.fallback;
        id = next;
    }
    return nullptr;
}

DrumEngine::Voice* DrumEngine::allocate()
{
    Voice* oldest = nullptr;
    for (auto& v : voices)
    {
        if (! v.active)
            return &v;
        if (oldest == nullptr || v.age < oldest->age)
            oldest = &v;
    }
    // Out of voices: reuse the oldest one immediately (it is almost certainly a quiet tail).
    return oldest;
}

void DrumEngine::fadeOut (Voice& v, float seconds) noexcept
{
    const float step = 1.0f / std::max (1.0f, seconds * (float) sr);
    v.fadeStep = std::max (v.fadeStep, step);
}

void DrumEngine::trigger (int slot, int art, float velocity, const SoundData* sound, const SlotMix& mix,
                          float velCurve, float humanize)
{
    const auto& def = kr::slots()[(size_t) slot].articulations[(size_t) art];

    // Choke: a closed / pedal hat stops ringing open hats.
    if (def.chokes != 0)
        for (auto& v : voices)
            if (v.active && v.slot == slot && v.group == def.chokes)
                fadeOut (v, 0.045f);

    const auto* data = resolve (sound, slot, art);
    if (data == nullptr || velocity <= 0.0f)
        return;

    // Velocity -> target loudness. 40 log10 (v) is the GM / DLS curve: 100 sits 4 dB under full,
    // 64 about 12 dB under, 16 about 36 dB under. The curve knob bends it like an exponent.
    const float v = juce::jlimit (0.008f, 1.0f, std::pow (velocity, std::exp2 (velCurve * 1.2f)));
    const float wantDb = data->maxDb + 40.0f * std::log10 (v) + humanize * 3.0f * rng.bi();

    // Pick among the recorded hits closest in loudness to the target, never the one just played.
    const int n = (int) data->hits.size();
    const float aim = juce::jlimit (data->minDb, data->maxDb, wantDb);
    const auto lower = std::lower_bound (data->hits.begin(), data->hits.end(), aim,
                                         [] (const Hit& h, float db) { return h.loudnessDb < db; });
    int lo = (int) std::distance (data->hits.begin(), lower), hi = lo;   // window [lo, hi)
    const int want = std::min (n, n >= 12 ? 4 : 3);
    while (hi - lo < want)
    {
        const bool canDown = lo > 0, canUp = hi < n;
        if (canUp && (! canDown || data->hits[(size_t) hi].loudnessDb - aim < aim - data->hits[(size_t) lo - 1].loudnessDb))
            ++hi;
        else
            --lo;
    }
    int candidates[4], count = 0;
    for (int i = lo; i < hi; ++i)
        if (i != lastHit[(size_t) slot][(size_t) art] || n == 1)
            candidates[count++] = i;
    const int best = candidates[std::min (count - 1, (int) (rng.uni() * (float) count))];
    lastHit[(size_t) slot][(size_t) art] = best;
    const Hit& hit = data->hits[(size_t) best];

    // Make up the rest with gain: soft notes below the softest recording get quieter, never louder than played.
    const float corr = juce::jlimit (-30.0f, 1.5f, wantDb - hit.loudnessDb);

    // Limit pile-up on fast repeats of the same articulation.
    int same = 0;
    Voice* oldestSame = nullptr;
    for (auto& vc : voices)
        if (vc.active && vc.slot == slot && vc.art == art && vc.fadeStep <= 0.0f)
        {
            ++same;
            if (oldestSame == nullptr || vc.age < oldestSame->age)
                oldestSame = &vc;
        }
    if (same >= kMaxPerArticulation && oldestSame != nullptr)
        fadeOut (*oldestSame, 0.02f);

    Voice* voice = allocate();
    *voice = {};
    voice->hit = &hit;
    voice->inc = (sound->sampleRate / sr) * mix.tuneRatio;
    voice->gain = dd::dbToGain (corr);
    voice->slot = slot;
    voice->art = art;
    voice->group = def.chokeGroup;
    voice->age = ++stamp;
    voice->active = true;

    // Decay < 1 shortens the natural ring: an exponential fade that starts after the attack.
    if (mix.decay < 0.995f)
    {
        const float tau = 0.04f + 2.5f * mix.decay * mix.decay;
        voice->decayCoef = (float) std::exp (-1.0 / (tau * sr));
        voice->decayDelay = (int) (0.025 * sr);
    }
}

void DrumEngine::render (float* L, float* R, int n, const std::array<SlotMix, kr::kNumSlots>& mixes) noexcept
{
    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const auto& m = mixes[(size_t) v.slot];
        const float gClose = v.gain * m.volume * m.close;
        const float gOh = v.gain * m.volume * m.overheads;
        const float gRoom = v.gain * m.volume * m.room;
        const float cl = gClose * m.panL, cr = gClose * m.panR;
        const int16_t* pcm = v.hit->pcm.data();
        const int last = v.hit->frames - 2;
        constexpr float s = 1.0f / 32767.0f;

        for (int i = 0; i < n; ++i)
        {
            const int i0 = (int) v.pos;
            if (i0 >= last)
            {
                v.active = false;
                break;
            }
            const float f = (float) (v.pos - (double) i0);

            // 4-point Hermite on each stem.
            float st[kStems];
            const int16_t* p0 = pcm + (size_t) std::max (0, i0 - 1) * kStems;
            const int16_t* p1 = pcm + (size_t) i0 * kStems;
            const int16_t* p2 = p1 + kStems;
            const int16_t* p3 = i0 + 2 <= last ? p2 + kStems : p2;
            for (int c = 0; c < kStems; ++c)
            {
                const float xm1 = p0[c] * s, x0 = p1[c] * s, x1 = p2[c] * s, x2 = p3[c] * s;
                const float c1 = 0.5f * (x1 - xm1);
                const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
                const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                st[c] = ((c3 * f + c2) * f + c1) * f + x0;
            }

            float env = v.fade;
            if (v.decayDelay > 0)
                --v.decayDelay;
            else if (v.decayCoef < 1.0f)
                v.decayEnv *= v.decayCoef;
            env *= v.decayEnv;

            if (v.fadeStep > 0.0f)
            {
                v.fade -= v.fadeStep;
                if (v.fade <= 0.0f)
                {
                    v.active = false;
                    break;
                }
            }
            if (env < 1.0e-4f && v.decayCoef < 1.0f && v.decayDelay <= 0)
            {
                v.active = false;
                break;
            }

            L[i] += env * (cl * st[0] + gOh * st[1] + gRoom * st[3]);
            R[i] += env * (cr * st[0] + gOh * st[2] + gRoom * st[4]);
            v.pos += v.inc;
        }
    }
}
} // namespace kitroom
