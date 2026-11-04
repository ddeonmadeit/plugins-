#include "Model.h"

namespace contour
{
namespace
{
struct Filter
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    static Filter make (bool highPass, double hz, double q, double sr)
    {
        const double w = juce::MathConstants<double>::twoPi * hz / sr;
        const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
        Filter f;
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

/** Voiced runs of a track: [first, last + 1) frame ranges. */
std::vector<std::pair<int, int>> voicedRuns (const dd::PitchTrack& tr)
{
    std::vector<std::pair<int, int>> runs;
    for (int f = 0; f < tr.size();)
    {
        if (! tr.voiced (f)) { ++f; continue; }
        int e = f;
        while (e < tr.size() && tr.voiced (e)) ++e;
        runs.push_back ({ f, e });
        f = e;
    }
    return runs;
}

float periodAt (const dd::PitchTrack& tr, double t)
{
    float m = tr.midiAt (t);
    if (m <= 0.0f)
    {
        const int f = tr.nearestFrame (t);
        for (int d = 0; d < 8 && m <= 0.0f; ++d)
        {
            if (tr.voiced (f - d)) m = tr.midi[(size_t) (f - d)];
            else if (tr.voiced (f + d)) m = tr.midi[(size_t) (f + d)];
        }
    }
    if (m <= 0.0f)
        m = 57.0f;
    return (float) (tr.sampleRate / (440.0 * std::pow (2.0, (m - 69.0) / 12.0)));
}
} // namespace

RangeAnalysis analyseRange (const dd::TakeStore& take, dd::Span span, double sr, const std::atomic<bool>* cancel)
{
    RangeAnalysis ra;
    ra.span = span;
    ra.track = dd::trackPitch (take, span, sr, {}, cancel);
    const auto& tr = ra.track;

    // Pulse alignment works on a smooth copy (first formant region), so marks follow the waveform's
    // period rather than jumping between similar peaks.
    const int pad = (int) (sr * 0.05);
    const int64_t base = span.start - pad;
    std::vector<float> x ((size_t) (span.length() + 2 * pad));
    take.read (base, x.data(), (int) x.size());
    {
        auto hp = Filter::make (true, 60.0, 0.707, sr);
        auto lp1 = Filter::make (false, 1100.0, 0.5412, sr), lp2 = Filter::make (false, 1100.0, 1.3066, sr);
        for (auto& v : x)
            v = lp2.process (lp1.process (hp.process (v)));
        // Zero-phase: run the same filters backwards too, so the marks sit on the real pulses.
        auto hpb = Filter::make (true, 60.0, 0.707, sr);
        auto lpb1 = Filter::make (false, 1100.0, 0.5412, sr), lpb2 = Filter::make (false, 1100.0, 1.3066, sr);
        for (auto it = x.rbegin(); it != x.rend(); ++it)
            *it = lpb2.process (lpb1.process (hpb.process (*it)));
    }
    auto at = [&] (int64_t t) { const auto i = t - base; return i >= 0 && i < (int64_t) x.size() ? x[(size_t) i] : 0.0f; };

    int32_t runIndex = 0;
    for (const auto& [fa, fb] : voicedRuns (tr))
    {
        if (cancel != nullptr && cancel->load())
            break;
        const int64_t t0 = std::max (span.start, tr.timeOf (fa) - tr.hop / 2);
        const int64_t t1 = std::min (span.end - 1, tr.timeOf (fb - 1) + tr.hop / 2);
        if (t1 - t0 < 16)
            continue;

        // First pulse: the strongest positive peak in the first period.
        float p = periodAt (tr, (double) t0);
        int64_t prev = t0;
        float best = -1.0e30f;
        for (int64_t t = t0; t < std::min (t1, t0 + (int64_t) p); ++t)
            if (at (t) > best) { best = at (t); prev = t; }
        ra.marks.push_back (prev);
        ra.markRun.push_back (runIndex);

        for (;;)
        {
            p = periodAt (tr, (double) prev);
            const auto predicted = prev + (int64_t) std::lround (p);
            if (predicted > t1)
                break;
            // The candidate whose waveform matches the previous period best (normalised correlation).
            const int radius = std::max (1, (int) (p * 0.2f));
            const int half = std::max (4, (int) (p * 0.5f));
            int64_t pick = predicted;
            double bestScore = -1.0e30;
            for (int c = -radius; c <= radius; ++c)
            {
                const int64_t cand = predicted + c;
                double xy = 0.0, yy = 1.0e-12;
                for (int k = -half; k < half; k += 2)
                {
                    const float a = at (prev + k), b = at (cand + k);
                    xy += (double) a * b;
                    yy += (double) b * b;
                }
                const double score = xy / std::sqrt (yy) - 0.002 * std::abs (c) / radius;
                if (score > bestScore) { bestScore = score; pick = cand; }
            }
            if (pick <= prev + 8)
                pick = predicted;
            ra.marks.push_back (pick);
            ra.markRun.push_back (runIndex);
            prev = pick;
        }
        ++runIndex;
    }
    return ra;
}

float measureCentre (const dd::PitchTrack& tr, int64_t start, int64_t end)
{
    const int fa = std::max (0, (int) std::ceil (tr.frameOf ((double) start)));
    const int fb = std::min (tr.size(), (int) std::floor (tr.frameOf ((double) end)) + 1);
    std::vector<std::pair<float, float>> v;   // pitch, weight
    for (int f = fa; f < fb; ++f)
        if (tr.voiced (f))
            v.push_back ({ tr.midi[(size_t) f], std::pow (10.0f, tr.level[(size_t) f] / 20.0f) });
    if (v.empty())
        return 60.0f;
    // The ear hears a note's middle, not its scoops: trim the ends of longer notes.
    if (v.size() >= 10)
    {
        const size_t trim = v.size() / 6;
        v = std::vector<std::pair<float, float>> (v.begin() + (long) trim, v.end() - (long) trim);
    }
    auto sorted = v;
    std::sort (sorted.begin(), sorted.end());
    const float median = sorted[sorted.size() / 2].first;
    double sum = 0.0, wsum = 0.0;
    for (const auto& [m, w] : v)
        if (std::abs (m - median) < 1.5f)
        {
            sum += (double) m * w;
            wsum += w;
        }
    return wsum > 0.0 ? (float) (sum / wsum) : median;
}

std::vector<Note> findNotes (const RangeAnalysis& ra, uint32_t& nextId)
{
    const auto& tr = ra.track;
    std::vector<Note> notes;

    // Join voiced runs split by a dropout of a few frames when the pitch carries on.
    auto runs = voicedRuns (tr);
    std::vector<std::pair<int, int>> joined;
    for (const auto& r : runs)
    {
        if (! joined.empty() && r.first - joined.back().second <= 4
            && std::abs (tr.midi[(size_t) r.first] - tr.midi[(size_t) joined.back().second - 1]) < 1.5f)
            joined.back().second = r.second;
        else
            joined.push_back (r);
    }

    for (const auto& [fa, fb] : joined)
    {
        const int len = fb - fa;
        if (len < 6)
            continue;   // under 30 ms: a click of voicing, not a note

        // Pitch without vibrato (moving average ~90 ms), holding over the dropouts.
        std::vector<float> raw ((size_t) len), sm ((size_t) len);
        float last = tr.midi[(size_t) fa];
        for (int i = 0; i < len; ++i)
        {
            const float m = tr.midi[(size_t) (fa + i)];
            if (m > 0.0f) last = m;
            raw[(size_t) i] = last;
        }
        for (int i = 0; i < len; ++i)
        {
            double s = 0.0;
            int c = 0;
            for (int k = std::max (0, i - 9); k <= std::min (len - 1, i + 9); ++k, ++c)
                s += raw[(size_t) k];
            sm[(size_t) i] = (float) (s / c);
        }

        // Most plausible sequence of held semitones: changing note costs, being off a semitone costs.
        const float lo = *std::min_element (sm.begin(), sm.end()), hi = *std::max_element (sm.begin(), sm.end());
        const int k0 = (int) std::floor (lo) - 1, numK = std::max (1, (int) std::ceil (hi) + 1 - k0 + 1);
        std::vector<float> cost ((size_t) numK), next ((size_t) numK);
        std::vector<std::vector<int16_t>> back ((size_t) len, std::vector<int16_t> ((size_t) numK, 0));
        constexpr float kSwitch = 2.0f;
        for (int k = 0; k < numK; ++k)
            cost[(size_t) k] = std::min (1.5f, juce::square (sm[0] - (float) (k0 + k)));
        for (int i = 1; i < len; ++i)
        {
            int bestPrev = 0;
            for (int k = 1; k < numK; ++k)
                if (cost[(size_t) k] < cost[(size_t) bestPrev]) bestPrev = k;
            for (int k = 0; k < numK; ++k)
            {
                const float stay = cost[(size_t) k], change = cost[(size_t) bestPrev] + kSwitch;
                const float e = std::min (1.5f, juce::square (sm[(size_t) i] - (float) (k0 + k)));
                if (stay <= change) { next[(size_t) k] = stay + e; back[(size_t) i][(size_t) k] = (int16_t) k; }
                else                { next[(size_t) k] = change + e; back[(size_t) i][(size_t) k] = (int16_t) bestPrev; }
            }
            std::swap (cost, next);
        }
        std::vector<int> state ((size_t) len);
        {
            int k = (int) std::distance (cost.begin(), std::min_element (cost.begin(), cost.end()));
            for (int i = len - 1; i >= 0; --i)
            {
                state[(size_t) i] = k;
                k = back[(size_t) i][(size_t) k];
            }
        }
        std::vector<int> cuts { 0 };
        for (int i = 1; i < len; ++i)
            if (state[(size_t) i] != state[(size_t) i - 1])
                cuts.push_back (i);
        cuts.push_back (len);

        // A new syllable on the same pitch: a clear dip in level between two louder parts.
        std::vector<int> withDips;
        for (size_t s = 0; s + 1 < cuts.size(); ++s)
        {
            withDips.push_back (cuts[s]);
            const int a = cuts[s], b = cuts[s + 1];
            int lastCut = a;
            for (int i = a + 10; i < b - 10; ++i)
            {
                const float l = tr.level[(size_t) (fa + i)];
                float leftMax = -200.0f, rightMax = -200.0f, localMin = l;
                for (int k = i - 12; k < i; ++k)  leftMax = std::max (leftMax, tr.level[(size_t) (fa + std::max (a, k))]);
                for (int k = i + 1; k <= i + 12; ++k) rightMax = std::max (rightMax, tr.level[(size_t) (fa + std::min (b - 1, k))]);
                for (int k = i - 2; k <= i + 2; ++k) localMin = std::min (localMin, tr.level[(size_t) (fa + k)]);
                if (juce::exactlyEqual (l, localMin) && l < std::min (leftMax, rightMax) - 7.0f && i - lastCut >= 10)
                {
                    withDips.push_back (i);
                    lastCut = i;
                }
            }
        }
        withDips.push_back (len);
        cuts = withDips;

        // Fold pieces shorter than 50 ms into the neighbour nearer in pitch.
        auto pieceMean = [&] (int a, int b)
        {
            double s = 0.0;
            for (int i = a; i < b; ++i) s += sm[(size_t) i];
            return (float) (s / std::max (1, b - a));
        };
        bool changed = true;
        while (changed && cuts.size() > 2)
        {
            changed = false;
            for (size_t s = 0; s + 1 < cuts.size(); ++s)
            {
                if (cuts[s + 1] - cuts[s] >= 10)
                    continue;
                const float m = pieceMean (cuts[s], cuts[s + 1]);
                const bool hasPrev = s > 0, hasNext = s + 2 < cuts.size();
                bool mergePrev = hasPrev;
                if (hasPrev && hasNext)
                    mergePrev = std::abs (pieceMean (cuts[s - 1], cuts[s]) - m) <= std::abs (pieceMean (cuts[s + 1], cuts[s + 2]) - m);
                if (! hasPrev && ! hasNext)
                    break;
                cuts.erase (cuts.begin() + (long) (mergePrev ? s : s + 1));
                changed = true;
                break;
            }
        }

        const int64_t runStart = std::max (ra.span.start, tr.timeOf (fa) - tr.hop / 2);
        const int64_t runEnd = std::min (ra.span.end, tr.timeOf (fb - 1) + tr.hop / 2);
        for (size_t s = 0; s + 1 < cuts.size(); ++s)
        {
            Note n;
            n.id = nextId++;
            n.start = s == 0 ? runStart : tr.timeOf (fa + cuts[s]) - tr.hop / 2;
            n.end = s + 2 == cuts.size() ? runEnd : tr.timeOf (fa + cuts[s + 1]) - tr.hop / 2;
            if (n.end - n.start < tr.hop * 4)
                continue;
            n.centre = measureCentre (tr, n.start, n.end);
            n.newStart = n.start;
            n.newEnd = n.end;
            notes.push_back (n);
        }
    }
    return notes;
}

std::pair<int, int> detectKey (const std::vector<Note>& notes)
{
    // Krumhansl-Kessler profiles against the time each pitch class is held.
    static constexpr float major[12] { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static constexpr float minor[12] { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    std::array<double, 12> hist {};
    for (const auto& n : notes)
        hist[(size_t) (((int) std::lround (n.centre) % 12 + 12) % 12)] += (double) (n.end - n.start);
    auto corr = [&] (const float* prof, int key)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += hist[(size_t) i]; my += prof[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = hist[(size_t) ((i + key) % 12)] - mx, b = prof[i] - my;
            sxy += a * b; sxx += a * a; syy += b * b;
        }
        return sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : -1.0;
    };
    int bestKey = 0, bestScale = 1;
    double best = -2.0;
    for (int k = 0; k < 12; ++k)
    {
        if (const double c = corr (major, k); c > best) { best = c; bestKey = k; bestScale = 1; }
        if (const double c = corr (minor, k); c > best) { best = c; bestKey = k; bestScale = 2; }
    }
    return { bestKey, bestScale };
}
} // namespace contour
