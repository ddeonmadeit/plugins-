#include "Render.h"

namespace contour
{
//==============================================================================
TimeMap::TimeMap (const std::vector<Note>& notes, const std::vector<dd::Span>& spans)
{
    std::vector<std::pair<double, double>> a;
    for (const auto& s : spans)
    {
        a.push_back ({ (double) s.start, (double) s.start });
        a.push_back ({ (double) s.end, (double) s.end });
    }
    for (const auto& n : notes)
    {
        a.push_back ({ (double) n.start, (double) n.newStart });
        a.push_back ({ (double) n.end, (double) n.newEnd });
        if (n.timeEdited())
            identity = false;
    }
    if (identity)
        return;
    std::sort (a.begin(), a.end());
    // Keep it strictly increasing on both sides (edits are constrained in the editor; this is the safety net).
    for (const auto& p : a)
    {
        if (! anchors.empty() && p.first <= anchors.back().first + 0.5)
            continue;
        const double out = anchors.empty() ? p.second : std::max (p.second, anchors.back().second + 1.0);
        anchors.push_back ({ p.first, out });
    }
    if (anchors.size() < 2)
        identity = true;
}

double TimeMap::map (double t, bool inverse) const noexcept
{
    if (identity)
        return t;
    auto key = [inverse] (const std::pair<double, double>& p) { return inverse ? p.second : p.first; };
    auto val = [inverse] (const std::pair<double, double>& p) { return inverse ? p.first : p.second; };
    if (t <= key (anchors.front()))
        return t - key (anchors.front()) + val (anchors.front());
    if (t >= key (anchors.back()))
        return t - key (anchors.back()) + val (anchors.back());
    auto it = std::upper_bound (anchors.begin(), anchors.end(), t,
                                [&key] (double v, const std::pair<double, double>& p) { return v < key (p); });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const double span = key (b) - key (a);
    return span > 0.0 ? val (a) + (t - key (a)) * (val (b) - val (a)) / span : val (a);
}

//==============================================================================
namespace
{
/** Raised-cosine smoothing over `half` frames each side, within [a, b) only. */
std::vector<float> smoothWithin (const std::vector<float>& v, int a, int b, int half)
{
    std::vector<float> out (v);
    if (half <= 0)
        return out;
    std::vector<float> w ((size_t) (2 * half + 1));
    for (int k = -half; k <= half; ++k)
        w[(size_t) (k + half)] = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) k / (float) (half + 1));
    for (int i = a; i < b; ++i)
    {
        double s = 0.0, ws = 0.0;
        for (int k = std::max (a, i - half); k <= std::min (b - 1, i + half); ++k)
        {
            const float wk = w[(size_t) (k - i + half)];
            s += (double) v[(size_t) k] * wk;
            ws += wk;
        }
        out[(size_t) i] = ws > 0.0 ? (float) (s / ws) : v[(size_t) i];
    }
    return out;
}

/** Two passes of a moving average (triangle), within [a, b). */
std::vector<float> lowpassWithin (const std::vector<float>& v, int a, int b, int half)
{
    std::vector<float> cur (v), next (v);
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = a; i < b; ++i)
        {
            double s = 0.0;
            int c = 0;
            for (int k = std::max (a, i - half); k <= std::min (b - 1, i + half); ++k, ++c)
                s += cur[(size_t) k];
            next[(size_t) i] = c > 0 ? (float) (s / c) : cur[(size_t) i];
        }
        std::swap (cur, next);
    }
    return cur;
}

/** Per-frame edit curves of one analysed span. */
struct FrameCurves
{
    std::vector<float> shift, formant, gain, outMidi;
};

FrameCurves frameCurves (const RangeAnalysis& ra, const std::vector<Note>& notes, const Settings& s)
{
    const auto& tr = ra.track;
    const int F = tr.size();
    FrameCurves fc;
    fc.shift.assign ((size_t) F, 0.0f);
    fc.formant.assign ((size_t) F, s.formant);
    fc.gain.assign ((size_t) F, 1.0f);
    fc.outMidi.assign ((size_t) F, 0.0f);
    if (F == 0)
        return fc;

    // Which note owns each frame.
    std::vector<int> owner ((size_t) F, -1);
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const auto& n = notes[(size_t) i];
        if (n.end <= ra.span.start || n.start >= ra.span.end)
            continue;
        const int fa = std::max (0, (int) std::ceil (tr.frameOf ((double) n.start)));
        const int fb = std::min (F, (int) std::ceil (tr.frameOf ((double) n.end)));
        for (int f = fa; f < fb; ++f)
            owner[(size_t) f] = i;
    }

    const int tHalf = std::max (0, (int) std::lround (s.transitionMs * 0.001 * tr.sampleRate / tr.hop * 0.5));
    std::vector<float> centre ((size_t) F, 0.0f), delta ((size_t) F, 0.0f), ds ((size_t) F, 1.0f), ms ((size_t) F, 1.0f);
    for (int f = 0; f < F; ++f)
    {
        const int o = owner[(size_t) f];
        if (o >= 0)
        {
            const auto& n = notes[(size_t) o];
            centre[(size_t) f] = n.centre;
            delta[(size_t) f] = targetCentre (n, s) - n.centre;
            ds[(size_t) f] = n.drift * (1.0f - s.drift);
            ms[(size_t) f] = n.mod * s.vibrato;
            fc.formant[(size_t) f] = s.formant + n.formant;
            fc.gain[(size_t) f] = n.muted ? 0.0f : juce::Decibels::decibelsToGain (n.gainDb, -100.0f);
        }
        else
            centre[(size_t) f] = tr.midi[(size_t) f];
    }

    // Each voiced run on its own: the sung line = held note centres (smoothed by the transition time)
    // plus a slow drift plus fast modulation (vibrato); edits scale each part.
    for (int f = 0; f < F;)
    {
        if (! tr.voiced (f)) { ++f; continue; }
        int e = f;
        while (e < F && tr.voiced (e)) ++e;

        const auto held = smoothWithin (centre, f, e, tHalf);
        std::vector<float> dev ((size_t) F, 0.0f);
        for (int k = f; k < e; ++k)
            dev[(size_t) k] = tr.midi[(size_t) k] - held[(size_t) k];
        const auto drift = lowpassWithin (dev, f, e, 16);
        const auto dlt = smoothWithin (delta, f, e, tHalf);
        const auto dss = smoothWithin (ds, f, e, tHalf);
        const auto mss = smoothWithin (ms, f, e, tHalf);
        for (int k = f; k < e; ++k)
        {
            const float d = drift[(size_t) k], m = dev[(size_t) k] - d;
            const float sh = dlt[(size_t) k] + (dss[(size_t) k] - 1.0f) * d + (mss[(size_t) k] - 1.0f) * m;
            fc.shift[(size_t) k] = juce::jlimit (-24.0f, 24.0f, sh);
            fc.outMidi[(size_t) k] = tr.midi[(size_t) k] + fc.shift[(size_t) k];
        }
        f = e;
    }

    fc.formant = smoothWithin (fc.formant, 0, F, 2);
    fc.gain = smoothWithin (fc.gain, 0, F, 2);
    return fc;
}

float interp (const std::vector<float>& v, double f) noexcept
{
    if (v.empty())
        return 0.0f;
    if (f <= 0.0)
        return v.front();
    const auto i = (size_t) f;
    if (i + 1 >= v.size())
        return v.back();
    const float t = (float) (f - (double) i);
    return v[i] + (v[i + 1] - v[i]) * t;
}
} // namespace

BuildResult build (const Analysis& analysis, const std::vector<Note>& notes, const Settings& s, const std::vector<dd::Span>& spans)
{
    BuildResult result;
    result.playback = std::make_unique<Playback>();
    result.curve = std::make_shared<DisplayCurve>();
    auto& pb = *result.playback;
    pb.spans = spans;
    pb.outGain = s.outGain;
    const double sr = analysis.sampleRate;
    const TimeMap tm (notes, spans);
    const double hopU = std::round (0.004 * sr);

    for (const auto& ra : analysis.ranges)
    {
        const auto fc = frameCurves (ra, notes, s);
        const auto& tr = ra.track;

        for (int f = 0; f < tr.size(); ++f)
        {
            CurvePoint p;
            p.t = (int64_t) std::llround (tm.toOutput ((double) tr.timeOf (f)));
            p.midi = fc.outMidi[(size_t) f];
            p.original = tr.midi[(size_t) f];
            p.level = tr.level[(size_t) f];
            result.curve->points.push_back (p);
            result.curve->peakLevel = std::max (result.curve->peakLevel, p.level);
        }

        // Voiced runs by their pulses: [first, last] mark position of each run.
        const auto& marks = ra.marks;
        const auto& runs = ra.markRun;
        auto runFirst = [&] (size_t k) { while (k > 0 && runs[k - 1] == runs[k]) --k; return k; };
        auto runLast = [&] (size_t k) { while (k + 1 < runs.size() && runs[k + 1] == runs[k]) ++k; return k; };

        std::vector<Grain> grains;
        std::vector<bool> knownLeft, knownRight;
        double tOut = (double) ra.span.start;
        const double endOut = tm.toOutput ((double) ra.span.end);
        size_t k = 0;
        while (tOut < endOut)
        {
            const double tIn = tm.toInput (tOut);
            while (k + 1 < marks.size() && (double) marks[k + 1] <= tIn)
                ++k;
            size_t km = k;
            if (k + 1 < marks.size() && std::abs ((double) marks[k + 1] - tIn) < std::abs ((double) marks[k] - tIn))
                km = k + 1;

            bool voiced = false;
            float leftA = -1.0f, rightA = -1.0f;
            if (! marks.empty())
            {
                const size_t a = runFirst (km), b = runLast (km);
                const float pb2 = b > a ? (float) (marks[b] - marks[b - 1]) : (float) hopU;
                // From the run's first pulse (unvoiced grains land on it exactly) to half a period past its last.
                voiced = tIn >= (double) marks[a] - 0.5 && tIn <= (double) marks[b] + 0.5 * pb2;
                if (voiced)
                {
                    if (km > a) leftA = (float) (marks[km] - marks[km - 1]);
                    if (km < b) rightA = (float) (marks[km + 1] - marks[km]);
                }
            }

            const double frame = tr.frameOf (tIn);
            const float gain = interp (fc.gain, frame);
            Grain g;
            g.out = tOut;
            g.gain = gain;
            double step;
            if (voiced)
            {
                const float shift = interp (fc.shift, frame);
                const double ratio = juce::jlimit (0.25, 4.0, std::pow (2.0, shift / 12.0));
                float period = tIn >= (double) marks[km] ? (rightA > 0.0f ? rightA : leftA) : (leftA > 0.0f ? leftA : rightA);
                if (period <= 0.0f)
                    period = (float) hopU;
                g.in = (double) marks[km];
                g.left = leftA;
                g.right = rightA;
                g.speed = std::pow (2.0f, juce::jlimit (-12.0f, 12.0f, interp (fc.formant, frame)) / 12.0f);
                step = period / ratio;
            }
            else
            {
                g.in = tIn;
                g.left = g.right = -1.0f;
                step = hopU;
                // Land exactly on the next voiced run's first pulse (so unedited audio stays sample-exact).
                if (k + 1 < marks.size() || (! marks.empty() && (double) marks[k] > tIn))
                {
                    const size_t nk = (double) marks[k] > tIn ? k : k + 1;
                    const double nOut = tm.toOutput ((double) marks[runFirst (nk)]);
                    if (nOut > tOut + 1.0 && nOut - tOut < hopU * 1.5)
                        step = nOut - tOut;
                }
            }
            grains.push_back (g);
            knownLeft.push_back (g.left > 0.0f);
            knownRight.push_back (g.right > 0.0f);
            tOut += std::max (8.0, step);
        }

        // Unknown halves take the spacing of the neighbouring grains as placed.
        for (size_t i = 0; i < grains.size(); ++i)
        {
            auto& g = grains[i];
            if (! knownLeft[i])
                g.left = (float) (i > 0 ? g.out - grains[i - 1].out : hopU);
            if (! knownRight[i])
                g.right = (float) (i + 1 < grains.size() ? grains[i + 1].out - g.out : hopU);
            g.left = juce::jlimit (8.0f, 4096.0f, g.left);
            g.right = juce::jlimit (8.0f, 4096.0f, g.right);
            pb.maxHalf = std::max ({ pb.maxHalf, g.left, g.right });
        }
        pb.grains.insert (pb.grains.end(), grains.begin(), grains.end());
    }
    std::sort (pb.grains.begin(), pb.grains.end(), [] (const Grain& a, const Grain& b) { return a.out < b.out; });
    return result;
}

//==============================================================================
Renderer::Renderer()
{
    for (int i = 0; i <= kLut + 1; ++i)
    {
        const float u = std::min (1.0f, (float) i / (float) kLut);
        lut[(size_t) i] = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * u);
    }
}

float Renderer::grainMean (const Grain& g, const dd::TakeStore& take) noexcept
{
    for (const auto& e : meanCache)
        if (e.grain == &g && juce::exactlyEqual (e.in, g.in) && juce::exactlyEqual (e.speed, g.speed))
            return e.mean;
    double sum = 0.0, wsum = 0.0;
    const auto a = (int64_t) std::ceil (g.out - g.left), b = (int64_t) std::floor (g.out + g.right);
    const float invL = 1.0f / g.left, invR = 1.0f / g.right;
    for (int64_t t = a; t <= b; ++t)
    {
        const double x = (double) t - g.out;
        const float w = x < 0.0 ? window ((float) -x * invL) : window ((float) x * invR);
        sum += (double) w * take.hermite (g.in + x * g.speed);
        wsum += w;
    }
    const float m = wsum > 0.0 ? (float) (sum / wsum) : 0.0f;
    meanCache[(size_t) meanNext] = { &g, g.in, g.speed, m };
    meanNext = (meanNext + 1) % kMeanCache;
    return m;
}

void Renderer::render (const Playback& pb, const dd::TakeStore& take, int64_t t0, int n, float* out, float* cover) noexcept
{
    n = std::min (n, kMaxBlock);
    std::fill (num.begin(), num.begin() + n, 0.0f);
    std::fill (den.begin(), den.begin() + n, 0.0f);
    std::fill (cover, cover + n, 0.0f);

    // Coverage, with short ramps at the edges of each captured span.
    constexpr float kRamp = 64.0f;
    auto sp = std::upper_bound (pb.spans.begin(), pb.spans.end(), t0, [] (int64_t v, const dd::Span& s) { return v < s.end; });
    for (; sp != pb.spans.end() && sp->start < t0 + n; ++sp)
        for (int64_t t = std::max (t0, sp->start); t < std::min (t0 + n, sp->end); ++t)
        {
            const auto d = (float) std::min (t - sp->start, sp->end - 1 - t);
            cover[t - t0] = std::min (1.0f, (d + 1.0f) / kRamp);
        }

    const double from = (double) t0 - pb.maxHalf - 2.0, to = (double) (t0 + n) + pb.maxHalf + 2.0;
    auto g = std::lower_bound (pb.grains.begin(), pb.grains.end(), from, [] (const Grain& gr, double v) { return gr.out < v; });
    for (; g != pb.grains.end() && g->out < to; ++g)
    {
        const auto a = std::max (t0, (int64_t) std::ceil (g->out - g->left));
        const auto b = std::min (t0 + n - 1, (int64_t) std::floor (g->out + g->right));
        if (a > b)
            continue;
        const float invL = 1.0f / g->left, invR = 1.0f / g->right;
        const float offset = std::abs (g->speed - 1.0f) > 1.0e-4f ? grainMean (*g, take) : 0.0f;
        for (int64_t t = a; t <= b; ++t)
        {
            const double x = (double) t - g->out;
            const float w = x < 0.0 ? window ((float) -x * invL) : window ((float) x * invR);
            if (w <= 0.0f)
                continue;
            const float v = take.hermite (g->in + x * g->speed) - offset;
            num[(size_t) (t - t0)] += w * g->gain * v;
            den[(size_t) (t - t0)] += w;
        }
    }
    // Normalise by the square root of the window sum: unedited audio (sum exactly 1) is untouched, and when
    // pulses are packed closer (pitch up) or spread apart (pitch down) the voice keeps its loudness.
    for (int i = 0; i < n; ++i)
    {
        const float d = den[(size_t) i];
        out[i] = d > 1.0e-6f ? num[(size_t) i] / std::sqrt (std::max (d, 0.1f)) * pb.outGain : 0.0f;
    }
}
} // namespace contour
