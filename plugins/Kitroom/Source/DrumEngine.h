#pragma once

#include <dd_dsp/dd_dsp.h>

#include "Library.h"
#include "Params.h"

namespace kitroom
{
/** Mix settings for one slot, resolved once per block. */
struct SlotMix
{
    float volume = 1.0f, panL = 1.0f, panR = 1.0f, tuneRatio = 1.0f, decay = 1.0f;
    float close = 1.0f, overheads = 1.0f, room = 1.0f;
};

/**
    Sample voices for the whole kit. Velocity chooses a recorded hit by loudness rank (so a soft note
    plays a genuinely soft hit, not a quieter loud one) and a small gain correction makes the dynamic
    curve continuous between recorded hits. Neighbouring hits rotate so repeats never machine-gun.
*/
class DrumEngine
{
public:
    static constexpr int kMaxVoices = 96;
    static constexpr int kMaxPerArticulation = 6;

    void prepare (double sampleRate);
    void reset();

    /** Plays (slot, articulation) at velocity 0..1. sound may be null (slot not loaded yet). */
    void trigger (int slot, int art, float velocity, const SoundData* sound, const SlotMix& mix,
                  float velCurve, float humanize);

    /** Adds every voice into L/R (n samples) using the current mixes. */
    void render (float* L, float* R, int n, const std::array<SlotMix, kr::kNumSlots>& mixes) noexcept;

    int activeVoices() const noexcept;

private:
    struct Voice
    {
        const Hit* hit = nullptr;
        double pos = 0.0, inc = 1.0;
        float gain = 0.0f;
        int slot = 0, art = 0, group = 0;
        uint64_t age = 0;
        float decayEnv = 1.0f, decayCoef = 1.0f;
        int decayDelay = 0;
        float fade = 1.0f, fadeStep = 0.0f;   // fadeStep > 0 while choking / stealing
        bool active = false;
    };

    const ArticulationData* resolve (const SoundData* sound, int slot, int art) const;
    Voice* allocate();
    void fadeOut (Voice& v, float seconds) noexcept;

    double sr = 44100.0;
    std::array<Voice, kMaxVoices> voices {};
    std::array<std::array<int, kr::kMaxArticulations>, kr::kNumSlots> lastHit {};
    uint64_t stamp = 0;
    dd::Rng rng;
};
} // namespace kitroom
