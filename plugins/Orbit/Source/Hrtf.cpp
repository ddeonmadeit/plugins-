#include "Hrtf.h"

#include "OrbitHrtf.h"

#include <cmath>
#include <cstring>

namespace orbit
{
HrtfSet::HrtfSet()
{
    const auto* data = reinterpret_cast<const unsigned char*> (OrbitHrtf::hrtf_bin);
    const int size = OrbitHrtf::hrtf_binSize;
    if (size < 40 || std::memcmp (data, "OHRT", 4) != 0)
    {
        jassertfalse;
        return;
    }
    auto u32 = [data] (int off) { uint32_t v; std::memcpy (&v, data + off, 4); return (int) juce::ByteOrder::swapIfBigEndian (v); };
    auto f32 = [data] (int off) { uint32_t v; std::memcpy (&v, data + off, 4); v = juce::ByteOrder::swapIfBigEndian (v); float f; std::memcpy (&f, &v, 4); return f; };
    numHeads = u32 (8); numEl = u32 (12); numAz = u32 (16); srcTaps = u32 (20);
    elMin = f32 (24); step = f32 (28); srcRate = f32 (32);
    const float scale = f32 (36);
    const size_t count = (size_t) numHeads * (size_t) numEl * (size_t) numAz * 2 * (size_t) srcTaps;
    jassert ((size_t) size >= 40 + count * 2);
    source.resize (count);
    for (size_t i = 0; i < count; ++i)
    {
        int16_t v;
        std::memcpy (&v, data + 40 + i * 2, 2);
        source[i] = (float) (int16_t) juce::ByteOrder::swapIfBigEndian ((uint16_t) v) * scale;
    }
}

void HrtfSet::prepare (double sampleRate)
{
    if (std::abs (sampleRate - preparedRate) < 1.0e-6 || source.empty())
        return;
    preparedRate = sampleRate;
    const double ratio = srcRate / sampleRate;              // source samples per output sample
    numTaps = std::max (16, (int) std::lround (srcTaps / ratio));
    const double cutoff = std::min (1.0, sampleRate / srcRate);
    constexpr int halfWidth = 12;

    // Windowed-sinc weights from every output tap to the source taps, shared by all filters.
    std::vector<float> weights ((size_t) numTaps * (size_t) srcTaps, 0.0f);
    for (int n = 0; n < numTaps; ++n)
    {
        const double x = n * ratio;
        for (int k = 0; k < srcTaps; ++k)
        {
            const double d = x - k;
            if (std::abs (d) >= halfWidth)
                continue;
            const double arg = juce::MathConstants<double>::pi * d * cutoff;
            const double sinc = std::abs (arg) < 1.0e-9 ? 1.0 : std::sin (arg) / arg;
            const double win = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * d / halfWidth);
            weights[(size_t) n * (size_t) srcTaps + (size_t) k] = (float) (cutoff * sinc * win * ratio);
        }
    }
    // Fade the last few taps so a filter that got longer or shorter still ends at zero.
    const int fade = std::max (2, numTaps / 8);

    const size_t filters = (size_t) numHeads * (size_t) numEl * (size_t) numAz * 2;
    table.assign (filters * (size_t) numTaps, 0.0f);
    for (size_t f = 0; f < filters; ++f)
    {
        const float* in = source.data() + f * (size_t) srcTaps;
        float* out = table.data() + f * (size_t) numTaps;
        for (int n = 0; n < numTaps; ++n)
        {
            const float* w = weights.data() + (size_t) n * (size_t) srcTaps;
            float acc = 0.0f;
            for (int k = 0; k < srcTaps; ++k)
                acc += w[k] * in[k];
            if (n >= numTaps - fade)
                acc *= 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (n - (numTaps - fade) + 1) / (float) (fade + 1));
            out[n] = acc;
        }
    }
}

void HrtfSet::lookup (int head, float azimuth, float elevation, float* left, float* right) const noexcept
{
    if (table.empty())
    {
        std::fill (left, left + numTaps, 0.0f);
        std::fill (right, right + numTaps, 0.0f);
        return;
    }
    head = juce::jlimit (0, numHeads - 1, head);
    float azDeg = juce::radiansToDegrees (azimuth);
    azDeg -= 360.0f * std::floor (azDeg / 360.0f);
    const float elDeg = juce::jlimit (elMin, elMin + step * (float) (numEl - 1), juce::radiansToDegrees (elevation));

    const float fe = (elDeg - elMin) / step;
    const int e0 = std::min ((int) fe, numEl - 2);
    const float te = fe - (float) e0;
    const float fa = azDeg / step;
    const int a0 = (int) fa % numAz;
    const int a1 = (a0 + 1) % numAz;
    const float ta = fa - std::floor (fa);

    const size_t earStride = (size_t) numTaps, azStride = earStride * 2, elStride = azStride * (size_t) numAz;
    const float* base = table.data() + (size_t) head * elStride * (size_t) numEl;
    const float* p00 = base + (size_t) e0 * elStride + (size_t) a0 * azStride;
    const float* p01 = base + (size_t) e0 * elStride + (size_t) a1 * azStride;
    const float* p10 = base + (size_t) (e0 + 1) * elStride + (size_t) a0 * azStride;
    const float* p11 = base + (size_t) (e0 + 1) * elStride + (size_t) a1 * azStride;
    const float w00 = (1.0f - te) * (1.0f - ta), w01 = (1.0f - te) * ta, w10 = te * (1.0f - ta), w11 = te * ta;
    for (int i = 0; i < numTaps; ++i)
    {
        left[i]  = w00 * p00[i] + w01 * p01[i] + w10 * p10[i] + w11 * p11[i];
        right[i] = w00 * p00[earStride + (size_t) i] + w01 * p01[earStride + (size_t) i]
                 + w10 * p10[earStride + (size_t) i] + w11 * p11[earStride + (size_t) i];
    }
}

float HrtfSet::itdSeconds (float azimuth, float elevation) noexcept
{
    constexpr float headRadius = 0.0875f, speedOfSound = 343.0f;
    const float lateral = std::asin (juce::jlimit (-1.0f, 1.0f, std::sin (azimuth) * std::cos (elevation)));
    return headRadius / speedOfSound * (lateral + std::sin (lateral));
}

const juce::StringArray& HrtfSet::headNames()
{
    static const juce::StringArray n { "Average", "Sharp", "Alt 1", "Alt 2" };
    return n;
}
} // namespace orbit
