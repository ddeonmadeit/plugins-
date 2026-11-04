#pragma once

#include "Model.h"

/*
    Turning each sound's reduction into audio. In Split mode only what lies above Freq is turned down:
    the output crossfades from the take to its zero-phase low band by (1 - gain), so with no reduction it is
    the take, sample for sample, and with a reduction nothing below Freq moves (no phase smear, no latency).
    Wide mode turns the whole sound down. Each reduction ramps in and out over Fade.
*/
namespace hush
{
struct Event
{
    int64_t start = 0, end = 0;
    float gain = 1.0f;
};

struct Playback : dd::TakePlayback
{
    std::vector<Event> events;                   // ascending, non-overlapping
    std::shared_ptr<const dd::TakeStore> low;    // the take below Freq (Split mode)
    int fade = 240;
    bool wide = false, listen = false;
    float outGain = 1.0f;
};

class Renderer
{
public:
    void render (const Playback& pb, const dd::TakeStore& take, int64_t t0, int n, float* out, float* cover) noexcept;
};

/**
    A conventional real-time split-band de-esser for audio that hasn't been captured (and while capturing):
    the sibilance band's level against the voice's body decides the reduction, applied the same way as the
    per-sound one. No lookahead, so it is gentler than the captured version.
*/
class LiveDeEsser
{
public:
    void prepare (double sampleRate);
    void process (juce::AudioBuffer<float>& buffer, int start, int n, const Settings& s) noexcept;
    float getReductionDb() const noexcept { return grDb; }

private:
    struct Svf
    {
        float g = 0.0f, k = 1.41421356f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
        void set (float hz, double sr) noexcept
        {
            g = (float) std::tan (juce::MathConstants<double>::pi * std::min ((double) hz, sr * 0.45) / sr);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        float low (float x) noexcept
        {
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v2;
        }
    };

    double sr = 48000.0;
    float setHz = 0.0f;
    std::array<Svf, 3> lp;   // L, R, detector
    float envH = 0.0f, envL = 0.0f, gain = 1.0f, attack = 0.0f, release = 0.0f, smooth = 0.0f;
    float grDb = 0.0f;
};
} // namespace hush
