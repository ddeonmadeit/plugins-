namespace dd
{
namespace
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    static Biquad make (bool highPass, double hz, double q, double sr)
    {
        const double w = juce::MathConstants<double>::twoPi * hz / sr;
        const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        Biquad f;
        if (highPass)
        {
            f.b0 = (1.0 + cw) * 0.5 / a0;
            f.b1 = -(1.0 + cw) / a0;
        }
        else
        {
            f.b0 = (1.0 - cw) * 0.5 / a0;
            f.b1 = (1.0 - cw) / a0;
        }
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

struct Candidate
{
    float lag = 0.0f;     // decimated samples (fractional)
    float value = 1.0f;   // CMNDF at the dip (0 = perfectly periodic)
};

constexpr int kMaxCands = 5;

/** Sum of squared differences between x[j0..j0+len) and the same span `lag` later. */
double squaredDifference (const float* x, int len, int lag) noexcept
{
    double s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    int j = 0;
    for (; j + 4 <= len; j += 4)
    {
        const float d0 = x[j] - x[j + lag], d1 = x[j + 1] - x[j + 1 + lag];
        const float d2 = x[j + 2] - x[j + 2 + lag], d3 = x[j + 3] - x[j + 3 + lag];
        s0 += d0 * d0; s1 += d1 * d1; s2 += d2 * d2; s3 += d3 * d3;
    }
    for (; j < len; ++j)
    {
        const float d = x[j] - x[j + lag];
        s0 += d * d;
    }
    return s0 + s1 + s2 + s3;
}
} // namespace

PitchTrack trackPitch (const TakeStore& take, Span span, double sr, const PitchTrackSettings& s, const std::atomic<bool>* cancel)
{
    // Padding so the first and last frames see real context (silence outside the take).
    const int pad = (int) std::ceil (sr * 0.06) + 64;
    const auto len = (size_t) std::max<int64_t> (0, span.length()) + (size_t) (2 * pad);
    std::vector<float> x (len);
    take.read (span.start - pad, x.data(), (int) len);
    auto tr = trackPitch (x.data(), (int) len, span.start - pad, sr, s, cancel);

    // Trim to the span.
    const int skip = (int) std::lround ((double) pad / tr.hop);
    const int keep = (int) ((span.length() + tr.hop - 1) / tr.hop);
    PitchTrack out;
    out.sampleRate = sr;
    out.hop = tr.hop;
    out.start = tr.start + (int64_t) skip * tr.hop;
    for (int f = skip; f < std::min (tr.size(), skip + keep); ++f)
    {
        out.midi.push_back (tr.midi[(size_t) f]);
        out.level.push_back (tr.level[(size_t) f]);
    }
    return out;
}

PitchTrack trackPitch (const float* data, int numSamples, int64_t start, double sr, const PitchTrackSettings& s, const std::atomic<bool>* cancel)
{
    PitchTrack tr;
    tr.sampleRate = sr;
    tr.hop = std::max (1, (int) std::lround (sr * 0.005));
    tr.start = start + tr.hop / 2;   // frame f is centred here + f * hop
    const int numFrames = std::max (0, numSamples / tr.hop);
    tr.midi.assign ((size_t) numFrames, 0.0f);
    tr.level.assign ((size_t) numFrames, -120.0f);
    if (numFrames == 0)
        return tr;

    // Detection signal: no rumble, no sibilance or formant peaks to confuse the period estimate.
    std::vector<float> xf ((size_t) numSamples);
    {
        auto hp = Biquad::make (true, 50.0, 0.707, sr);
        auto lp1 = Biquad::make (false, 1800.0, 0.5412, sr), lp2 = Biquad::make (false, 1800.0, 1.3066, sr);
        for (int i = 0; i < numSamples; ++i)
            xf[(size_t) i] = lp2.process (lp1.process (hp.process (data[i])));
    }
    const int D = std::max (1, (int) std::lround (sr / 12000.0));
    const double dsr = sr / D;
    const int nd = numSamples / D;
    std::vector<float> xd ((size_t) nd);
    for (int k = 0; k < nd; ++k)
        xd[(size_t) k] = xf[(size_t) (k * D)];

    const int maxLag = (int) std::ceil (dsr / s.minHz) + 1;
    const int minLag = std::max (2, (int) std::floor (dsr / s.maxHz));
    const int W = (int) std::ceil (0.025 * dsr);
    std::vector<double> d ((size_t) maxLag + 2), cm ((size_t) maxLag + 2);

    std::vector<std::array<Candidate, kMaxCands>> cands ((size_t) numFrames);
    std::vector<int> numCands ((size_t) numFrames, 0);
    const int rmsHalf = (int) (0.01 * sr);

    for (int f = 0; f < numFrames; ++f)
    {
        if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
            return tr;

        const int c = f * tr.hop + tr.hop / 2;
        {
            double e = 0.0;
            int count = 0;
            for (int i = std::max (0, c - rmsHalf); i < std::min (numSamples, c + rmsHalf); ++i, ++count)
                e += (double) data[i] * data[i];
            tr.level[(size_t) f] = count > 0 ? (float) (10.0 * std::log10 (e / count + 1.0e-14)) : -120.0f;
        }
        if (tr.level[(size_t) f] < s.silenceDb)
            continue;

        const int j0 = c / D - (W + maxLag) / 2;
        if (j0 < 0 || j0 + W + maxLag + 1 >= nd)
            continue;
        const float* xp = xd.data() + j0;
        double running = 0.0;
        cm[0] = 1.0;
        for (int lag = 1; lag <= maxLag; ++lag)
        {
            d[(size_t) lag] = squaredDifference (xp, W, lag);
            running += d[(size_t) lag];
            cm[(size_t) lag] = running > 0.0 ? d[(size_t) lag] * lag / running : 1.0;
        }

        // Dips of the normalised difference: the candidate periods.
        auto& cs = cands[(size_t) f];
        int n = 0;
        for (int lag = std::max (minLag, 2); lag < maxLag; ++lag)
        {
            const double v = cm[(size_t) lag];
            if (v < 0.6 && v <= cm[(size_t) lag - 1] && v < cm[(size_t) lag + 1])
            {
                const double a = cm[(size_t) lag - 1], b = v, cc = cm[(size_t) lag + 1];
                const double den = a - 2.0 * b + cc;
                const double off = den > 1.0e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - cc) / den) : 0.0;
                const Candidate cand { (float) (lag + off), (float) std::max (0.0, b - 0.25 * (a - cc) * off) };
                if (n < kMaxCands)
                    cs[(size_t) n++] = cand;
                else
                {
                    // Keep the best few.
                    int worst = 0;
                    for (int k = 1; k < kMaxCands; ++k)
                        if (cs[(size_t) k].value > cs[(size_t) worst].value) worst = k;
                    if (cand.value < cs[(size_t) worst].value)
                        cs[(size_t) worst] = cand;
                }
            }
        }
        numCands[(size_t) f] = n;
    }

    // Most plausible path: state 0 = unvoiced, k + 1 = candidate k.
    constexpr int S = kMaxCands + 1;
    std::vector<std::array<float, S>> cost ((size_t) numFrames);
    std::vector<std::array<int8_t, S>> from ((size_t) numFrames);
    auto local = [&] (int f, int st) -> float
    {
        const float lvl = tr.level[(size_t) f];
        if (st == 0)
        {
            // Quiet frames lean unvoiced.
            const float quiet = juce::jlimit (0.0f, 1.0f, (lvl - s.silenceDb) / 18.0f);
            return s.unvoicedCost * (0.25f + 0.75f * quiet);
        }
        const int k = st - 1;
        if (k >= numCands[(size_t) f])
            return 1.0e9f;
        const auto& cs = cands[(size_t) f];
        // Subharmonic guard (YIN's "first good dip"): a longer period than a clean shorter one costs extra.
        float shortestGood = 1.0e9f;
        for (int j = 0; j < numCands[(size_t) f]; ++j)
            if (cs[(size_t) j].value < 0.25f)
                shortestGood = std::min (shortestGood, cs[(size_t) j].lag);
        float c0 = cs[(size_t) k].value;
        if (cs[(size_t) k].lag > shortestGood * 1.3f)
            c0 += 0.12f * std::log2 (cs[(size_t) k].lag / shortestGood);
        return c0;
    };
    auto trans = [&] (int fa, int a, int fb, int b) -> float
    {
        if (a == 0 && b == 0)
            return 0.0f;
        if (a == 0 || b == 0)
            return 0.25f;
        const float la = cands[(size_t) fa][(size_t) (a - 1)].lag, lb = cands[(size_t) fb][(size_t) (b - 1)].lag;
        return 0.04f * std::abs (12.0f * std::log2 (la / lb));
    };
    for (int st = 0; st < S; ++st)
    {
        cost[0][(size_t) st] = local (0, st);
        from[0][(size_t) st] = 0;
    }
    for (int f = 1; f < numFrames; ++f)
        for (int st = 0; st < S; ++st)
        {
            const float l = local (f, st);
            float best = 1.0e30f;
            int arg = 0;
            if (l < 1.0e8f)
                for (int p = 0; p < S; ++p)
                {
                    const float pc = cost[(size_t) f - 1][(size_t) p];
                    if (pc > 1.0e8f)
                        continue;
                    const float v = pc + trans (f - 1, p, f, st);
                    if (v < best) { best = v; arg = p; }
                }
            cost[(size_t) f][(size_t) st] = l < 1.0e8f ? best + l : 1.0e30f;
            from[(size_t) f][(size_t) st] = (int8_t) arg;
        }
    std::vector<int> path ((size_t) numFrames, 0);
    {
        int st = 0;
        float best = 1.0e30f;
        for (int k = 0; k < S; ++k)
            if (cost[(size_t) numFrames - 1][(size_t) k] < best) { best = cost[(size_t) numFrames - 1][(size_t) k]; st = k; }
        for (int f = numFrames - 1; f >= 0; --f)
        {
            path[(size_t) f] = st;
            st = from[(size_t) f][(size_t) st];
        }
    }

    // Refine each voiced frame's period on the full-rate signal.
    const int Wf = W * D;
    for (int f = 0; f < numFrames; ++f)
    {
        if (path[(size_t) f] == 0)
            continue;
        const float lagD = cands[(size_t) f][(size_t) (path[(size_t) f] - 1)].lag;
        const int t0 = (int) std::lround (lagD * D);
        const int lo = std::max (2, t0 - D - 1), hi = t0 + D + 1;
        const int c = f * tr.hop + tr.hop / 2;
        const int j0 = c - (Wf + hi) / 2;
        float period = lagD * (float) D;
        if (j0 >= 0 && j0 + Wf + hi + 2 < numSamples)
        {
            const float* xp = xf.data() + j0;
            int best = lo;
            double bestV = 1.0e300;
            std::array<double, 64> vals {};
            const int count = std::min (64, hi - lo + 3);
            for (int k = 0; k < count; ++k)
            {
                vals[(size_t) k] = squaredDifference (xp, Wf, lo - 1 + k);
                if (k >= 1 && k < count - 1 && vals[(size_t) k] < bestV) { bestV = vals[(size_t) k]; best = lo - 1 + k; }
            }
            const int bi = best - (lo - 1);
            if (bi >= 1 && bi < count - 1)
            {
                const double a = vals[(size_t) bi - 1], b = vals[(size_t) bi], cc = vals[(size_t) bi + 1];
                const double den = a - 2.0 * b + cc;
                const double off = den > 1.0e-18 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - cc) / den) : 0.0;
                period = (float) (best + off);
            }
        }
        const float hz = (float) (sr / period);
        tr.midi[(size_t) f] = hz >= s.minHz * 0.9f && hz <= s.maxHz * 1.1f ? 69.0f + 12.0f * std::log2 (hz / 440.0f) : 0.0f;
    }

    // Clean-up: fill one-frame dropouts inside a note, drop voiced blips under 20 ms.
    for (int f = 1; f + 1 < numFrames; ++f)
        if (tr.midi[(size_t) f] <= 0.0f && tr.midi[(size_t) f - 1] > 0.0f && tr.midi[(size_t) f + 1] > 0.0f
            && std::abs (tr.midi[(size_t) f - 1] - tr.midi[(size_t) f + 1]) < 1.0f)
            tr.midi[(size_t) f] = 0.5f * (tr.midi[(size_t) f - 1] + tr.midi[(size_t) f + 1]);
    for (int f = 0; f < numFrames;)
    {
        if (tr.midi[(size_t) f] <= 0.0f) { ++f; continue; }
        int e = f;
        while (e < numFrames && tr.midi[(size_t) e] > 0.0f) ++e;
        if (e - f < 4)
            std::fill (tr.midi.begin() + f, tr.midi.begin() + e, 0.0f);
        f = e;
    }
    // Median of three against single-frame spikes.
    {
        auto m = tr.midi;
        for (int f = 1; f + 1 < numFrames; ++f)
            if (m[(size_t) f - 1] > 0.0f && m[(size_t) f] > 0.0f && m[(size_t) f + 1] > 0.0f)
            {
                float a = m[(size_t) f - 1], b = m[(size_t) f], c = m[(size_t) f + 1];
                tr.midi[(size_t) f] = std::max (std::min (a, b), std::min (std::max (a, b), c));
            }
    }
    return tr;
}
} // namespace dd
