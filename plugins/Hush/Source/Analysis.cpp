#include "Model.h"

#include <juce_dsp/juce_dsp.h>

namespace hush
{
namespace
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    static Biquad make (bool highPass, double hz, double q, double sr)
    {
        const double w = juce::MathConstants<double>::twoPi * std::min (hz, sr * 0.45) / sr;
        const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
        Biquad f;
        f.b0 = (highPass ? (1.0 + cw) * 0.5 : (1.0 - cw) * 0.5) / a0;
        f.b1 = (highPass ? -(1.0 + cw) : (1.0 - cw)) / a0;
        f.b2 = f.b0;
        f.a1 = -2.0 * cw / a0;
        f.a2 = (1.0 - alpha) / a0;
        return f;
    }

    float process (float x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

/** Butterworth run forwards then backwards: zero phase, twice the slope. order 2 or 4. */
void zeroPhase (std::vector<float>& x, bool highPass, double hz, double sr, int order = 2)
{
    const std::vector<double> qs = order == 4 ? std::vector<double> { 0.5411961, 1.3065630 } : std::vector<double> { 0.70710678 };
    for (int pass = 0; pass < 2; ++pass)
        for (double q : qs)
        {
            auto f = Biquad::make (highPass, hz, q, sr);
            if (pass == 0)
                for (auto& v : x) v = f.process (v);
            else
                for (auto it = x.rbegin(); it != x.rend(); ++it) *it = f.process (*it);
        }
}

float db (double power) { return (float) (10.0 * std::log10 (power + 1.0e-14)); }
} // namespace

Analysis detectSounds (const dd::TakeStore& take, dd::Span span, double sr, float sens, uint32_t& nextId, const std::atomic<bool>* cancel)
{
    Analysis out;
    const int pad = (int) (0.05 * sr);
    const int64_t base = span.start - pad;
    const auto len = (size_t) (span.length() + 2 * pad);
    std::vector<float> x (len);
    take.read (base, x.data(), (int) len);
    auto hi = x, lo = x;
    zeroPhase (hi, true, 4000.0, sr);    // where "s" lives
    zeroPhase (lo, false, 3000.0, sr);   // the voice's body

    const int hop = std::max (1, (int) std::lround (0.002 * sr));
    const int frames = (int) (len / (size_t) hop);
    std::vector<float> lh ((size_t) frames), ll ((size_t) frames), lf ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        double eh = 0.0, el = 0.0, ef = 0.0;
        const int c = f * hop + hop / 2;
        int count = 0;
        for (int i = std::max (0, c - hop); i < std::min ((int) len, c + hop); ++i, ++count)
        {
            eh += (double) hi[(size_t) i] * hi[(size_t) i];
            el += (double) lo[(size_t) i] * lo[(size_t) i];
            ef += (double) x[(size_t) i] * x[(size_t) i];
        }
        const double n = std::max (1, count);
        lh[(size_t) f] = db (eh / n);
        ll[(size_t) f] = db (el / n);
        lf[(size_t) f] = db (ef / n);
    }
    auto timeOf = [&] (int f) { return base + (int64_t) f * hop; };   // frame f starts here

    // Picture for the editor (the span only).
    out.picture.start = span.start;
    out.picture.hop = hop;
    for (int f = pad / hop; f < frames - pad / hop; ++f)
    {
        out.picture.full.push_back (lf[(size_t) f]);
        out.picture.high.push_back (lh[(size_t) f]);
    }
    if (cancel != nullptr && cancel->load())
        return out;

    // Thresholds from the take itself: loud sharp frames set the scale, the ratio to the body tells an "s"
    // (sibilance band level near or above the voice's own) from a bright vowel.
    std::vector<float> loud;
    for (auto v : lh)
        if (v > -80.0f)
            loud.push_back (v);
    if (loud.empty())
        return out;
    std::sort (loud.begin(), loud.end());
    const float p95 = loud[(size_t) ((double) (loud.size() - 1) * 0.95)];
    const float absThr = std::max (-62.0f, p95 - (8.0f + 18.0f * sens));
    const float relThr = 4.0f - 14.0f * sens;
    const float riseThr = 12.0f - 8.0f * sens;

    // The sibilance band's local background (a low percentile over +-150 ms): an "s" sung over a vowel
    // doesn't beat the vowel's body, but it jumps well above the vowel's own top end.
    std::vector<float> bg ((size_t) frames);
    {
        const int half = std::max (1, (int) (0.15 * sr) / hop), step = 8;
        std::vector<float> win;
        float prevVal = -120.0f;
        int prevAt = 0;
        for (int f = 0; f <= frames; f += step)
        {
            const int c = std::min (f, frames - 1);
            win.assign (lh.begin() + std::max (0, c - half), lh.begin() + std::min (frames, c + half + 1));
            const auto k = (long) ((double) (win.size() - 1) * 0.2);
            std::nth_element (win.begin(), win.begin() + k, win.end());
            const float v = win[(size_t) k];
            for (int j = prevAt; j <= c; ++j)
                bg[(size_t) j] = c > prevAt ? prevVal + (v - prevVal) * (float) (j - prevAt) / (float) (c - prevAt) : v;
            prevVal = v;
            prevAt = c;
        }
    }
    std::vector<bool> on ((size_t) frames);
    for (int f = 0; f < frames; ++f)
        on[(size_t) f] = lh[(size_t) f] > absThr
                      && (lh[(size_t) f] - ll[(size_t) f] > relThr || lh[(size_t) f] - bg[(size_t) f] > riseThr);

    // Group into sounds: bridge gaps under 12 ms, drop blips under 10 ms, widen to the whole sound.
    for (int f = 0; f < frames;)
    {
        if (! on[(size_t) f]) { ++f; continue; }
        int lastOn = f, e = f + 1;
        while (e < frames && e - lastOn <= 6)
        {
            if (on[(size_t) e])
                lastOn = e;
            ++e;
        }
        e = lastOn + 1;
        if (e - f >= 5)
        {
            float peak = -200.0f;
            int peakAt = f;
            for (int k = f; k < e; ++k)
                if (lh[(size_t) k] > peak) { peak = lh[(size_t) k]; peakAt = k; }
            int a = f, b = e;
            while (a > 0 && f - a < 10 && lh[(size_t) a - 1] > peak - 15.0f) --a;
            while (b < frames && b - e < 10 && lh[(size_t) b] > peak - 15.0f) ++b;

            Sound s;
            s.id = nextId++;
            s.start = std::max (span.start, timeOf (a));
            s.end = std::min (span.end, timeOf (b));
            s.peakDb = peak;

            // Where its energy sits (spectral centroid above 2.5 kHz around the peak).
            constexpr int order = 10, N = 1 << order;
            juce::dsp::FFT fft (order);
            std::vector<float> buf ((size_t) (2 * N), 0.0f);
            const int c = peakAt * hop + hop / 2;
            for (int i = 0; i < N; ++i)
            {
                const int idx = c - N / 2 + i;
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
                buf[(size_t) i] = idx >= 0 && idx < (int) len ? x[(size_t) idx] * w : 0.0f;
            }
            fft.performFrequencyOnlyForwardTransform (buf.data());
            double num = 0.0, den = 0.0;
            for (int k = 1; k < N / 2; ++k)
            {
                const double hz = k * sr / N;
                if (hz < 2500.0 || hz > 16000.0) continue;
                const double m = (double) buf[(size_t) k] * buf[(size_t) k];
                num += hz * m;
                den += m;
            }
            s.hz = den > 0.0 ? (float) (num / den) : 6000.0f;

            // A burst that jumps up and is over quickly is a "t" / "k"; otherwise high = "s", lower = "sh".
            float rise = 0.0f;
            for (int k = std::max (1, a); k < std::min (b, a + 4); ++k)
                rise = std::max (rise, lh[(size_t) k] - lh[(size_t) std::max (0, a - 2)]);
            const double dur = (double) (s.end - s.start) / sr;
            s.kind = (int) (dur < 0.045 && rise > 12.0f ? SoundKind::T : s.hz >= 5800.0f ? SoundKind::S : SoundKind::SH);
            if (s.end - s.start > hop * 3)
                out.sounds.push_back (s);
        }
        f = e;
    }
    return out;
}

std::shared_ptr<dd::TakeStore> lowBand (const dd::TakeStore& take, const std::vector<dd::Span>& spans, double sr, float hz, const std::atomic<bool>* cancel)
{
    auto store = std::make_shared<dd::TakeStore>();
    const int pad = (int) (0.05 * sr);
    for (const auto& s : spans)
    {
        if (cancel != nullptr && cancel->load())
            break;
        std::vector<float> x ((size_t) (s.length() + 2 * pad));
        take.read (s.start - pad, x.data(), (int) x.size());
        zeroPhase (x, false, hz, sr, 4);   // eighth-order slope: a clean split
        store->write (s.start, x.data() + pad, (int) s.length());
    }
    return store;
}
} // namespace hush
