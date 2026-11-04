#include "Render.h"

namespace hush
{
void Renderer::render (const Playback& pb, const dd::TakeStore& take, int64_t t0, int n, float* out, float* cover) noexcept
{
    std::fill (cover, cover + n, 0.0f);
    constexpr float kRamp = 64.0f;
    auto sp = std::upper_bound (pb.spans.begin(), pb.spans.end(), t0, [] (int64_t v, const dd::Span& s) { return v < s.end; });
    for (; sp != pb.spans.end() && sp->start < t0 + n; ++sp)
        for (int64_t t = std::max (t0, sp->start); t < std::min (t0 + n, sp->end); ++t)
        {
            const auto d = (float) std::min (t - sp->start, sp->end - 1 - t);
            cover[t - t0] = std::min (1.0f, (d + 1.0f) / kRamp);
        }

    // Gain curve over the block: each sound's reduction, ramped in and out over the fade.
    std::array<float, dd::TakeEngine::kMaxBlock> g;
    std::fill (g.begin(), g.begin() + n, 1.0f);
    const int64_t fade = std::max (1, pb.fade);
    auto ev = std::lower_bound (pb.events.begin(), pb.events.end(), t0 - fade, [] (const Event& e, int64_t v) { return e.end < v; });
    for (; ev != pb.events.end() && ev->start - fade < t0 + n; ++ev)
    {
        const auto a = std::max (t0, ev->start - fade), b = std::min (t0 + n, ev->end + fade);
        for (int64_t t = a; t < b; ++t)
        {
            float ramp = 1.0f;
            if (t < ev->start)
                ramp = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (t - (ev->start - fade)) / (float) fade);
            else if (t >= ev->end)
                ramp = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (t - ev->end) / (float) fade);
            const float gt = 1.0f + (ev->gain - 1.0f) * ramp;
            g[(size_t) (t - t0)] = std::min (g[(size_t) (t - t0)], gt);
        }
    }

    for (int i = 0; i < n; ++i)
    {
        const int64_t t = t0 + i;
        const float x = take.get (t), gi = g[(size_t) i];
        float y = x;
        if (gi < 1.0f)
            y = pb.wide || pb.low == nullptr ? gi * x : gi * x + (1.0f - gi) * pb.low->get (t);
        out[i] = (pb.listen ? x - y : y) * pb.outGain;
    }
}

//==============================================================================
void LiveDeEsser::prepare (double sampleRate)
{
    sr = sampleRate;
    setHz = 0.0f;
    for (auto& f : lp)
        f = {};
    envH = envL = 0.0f;
    gain = 1.0f;
    attack = (float) std::exp (-1.0 / (0.0004 * sr));
    release = (float) std::exp (-1.0 / (0.06 * sr));
    smooth = (float) std::exp (-1.0 / (0.002 * sr));
    grDb = 0.0f;
}

void LiveDeEsser::process (juce::AudioBuffer<float>& buffer, int start, int n, const Settings& s) noexcept
{
    const int chs = std::min (2, buffer.getNumChannels());
    if (chs == 0)
        return;
    if (! juce::exactlyEqual (setHz, s.freq))
    {
        setHz = s.freq;
        for (auto& f : lp)
            f.set (s.freq, sr);
    }
    const float relThr = 4.0f - 14.0f * s.sensitivity;          // same meaning as the captured detector
    const float absThr = -30.0f - 20.0f * s.sensitivity;
    float maxGr = 0.0f;
    for (int i = start; i < start + n; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < chs; ++ch)
            m += buffer.getReadPointer (ch)[i];
        m /= (float) chs;
        const float low = lp[2].low (m), high = m - low;
        const float ah = std::abs (high), al = std::abs (low);
        envH = ah > envH ? ah + (envH - ah) * attack : ah + (envH - ah) * release;
        envL = al > envL ? al + (envL - al) * attack : al + (envL - al) * release;
        const float dh = juce::Decibels::gainToDecibels (envH, -120.0f), dl = juce::Decibels::gainToDecibels (envL, -120.0f);
        const float over = std::max (0.0f, std::min (dh - (dl + relThr), dh - absThr));
        const float target = juce::Decibels::decibelsToGain (-std::min (24.0f, s.amount * over * 0.8f));
        gain = target + (gain - target) * smooth;
        maxGr = std::max (maxGr, -juce::Decibels::gainToDecibels (gain, -60.0f));
        for (int ch = 0; ch < chs; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            const float x = d[i], lo = lp[(size_t) ch].low (x);
            const float y = s.mode == 1 ? gain * x : gain * x + (1.0f - gain) * lo;
            d[i] = (s.listen ? x - y : y) * s.outGain;
        }
    }
    grDb = maxGr;
}
} // namespace hush
