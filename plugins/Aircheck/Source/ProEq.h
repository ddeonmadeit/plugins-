#pragma once

#include <dd_dsp/dd_dsp.h>

#include <complex>

/*
    The vocal EQ: eight bands, each a bell, shelf, cut (12..48 dB/oct), notch, band-pass or tilt, placed on
    stereo, mid or side, and optionally dynamic (its gain moves with the level in its own band).

    Bells and shelves are magnitude-matched to their analog prototypes (poles by impulse invariance, the
    numerator fitted at DC, at the band frequency and at Nyquist; M. Vicanek, "Matched Second Order Digital
    Filters", 2016), so a 12 kHz air shelf or an 18 kHz bell keeps its analog shape instead of cramping
    toward Nyquist the way bilinear EQs do. Zero latency (minimum phase).
*/
namespace aircheck
{
inline const juce::StringArray& bandTypeNames()
{
    static const juce::StringArray n { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt" };
    return n;
}
inline const juce::StringArray& slopeNames()
{
    static const juce::StringArray n { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
    return n;
}
inline const juce::StringArray& placementNames()
{
    static const juce::StringArray n { "Stereo", "Mid", "Side" };
    return n;
}

enum class BandType { Bell, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, Tilt };

struct Coeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

/** |H| in dB at hz. */
inline double magnitudeDb (const Coeffs& c, double hz, double sr) noexcept
{
    const double w = juce::MathConstants<double>::twoPi * hz / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    const auto h = (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
    return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
}

namespace design
{
    /** |Ha(jw)|^2 of an analog biquad (c2 s^2 + c1 s + c0), w in rad/sample. */
    inline double analogMag2 (const double* num, const double* den, double w) noexcept
    {
        const std::complex<double> s (0.0, w);
        return std::norm ((num[0] * s * s + num[1] * s + num[2]) / (den[0] * s * s + den[1] * s + den[2]));
    }

    /** Magnitude-matched digital version of an analog biquad, fitted at DC, wm and Nyquist. */
    inline Coeffs matched (const double* num, const double* den, double wm) noexcept
    {
        Coeffs c;
        const double D = den[1] * den[1] - 4.0 * den[0] * den[2];
        if (D < 0.0)
        {
            const double sig = -den[1] / (2.0 * den[0]), om = std::sqrt (-D) / (2.0 * den[0]);
            c.a1 = -2.0 * std::exp (sig) * std::cos (om);
            c.a2 = std::exp (2.0 * sig);
        }
        else
        {
            const double p1 = (-den[1] + std::sqrt (D)) / (2.0 * den[0]), p2 = (-den[1] - std::sqrt (D)) / (2.0 * den[0]);
            c.a1 = -(std::exp (p1) + std::exp (p2));
            c.a2 = std::exp (p1 + p2);
        }
        const double A0 = (1.0 + c.a1 + c.a2) * (1.0 + c.a1 + c.a2);
        const double A1 = (1.0 - c.a1 + c.a2) * (1.0 - c.a1 + c.a2);
        const double A2 = -4.0 * c.a2;
        const double phi1 = std::pow (std::sin (wm * 0.5), 2.0), phi0 = 1.0 - phi1, phi2 = 4.0 * phi0 * phi1;
        const double B0 = A0 * analogMag2 (num, den, 1.0e-9);
        const double B1 = A1 * analogMag2 (num, den, juce::MathConstants<double>::pi);
        const double B2 = (analogMag2 (num, den, wm) * (A0 * phi0 + A1 * phi1 + A2 * phi2) - B0 * phi0 - B1 * phi1) / std::max (1.0e-12, phi2);
        const double r0 = std::sqrt (B0), r1 = std::sqrt (B1);
        const double W = 0.5 * (r0 + r1);
        c.b0 = 0.5 * (W + std::sqrt (std::max (0.0, W * W + B2)));
        c.b1 = 0.5 * (r0 - r1);
        c.b2 = -B2 / (4.0 * c.b0);
        return c;
    }

    inline double w0Of (double hz, double sr) { return juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sr * 0.499, hz) / sr; }

    inline Coeffs bell (double hz, double db, double q, double sr)
    {
        const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sr * 0.47, hz) / sr, A = std::pow (10.0, db / 40.0);
        const double num[3] { 1.0, A * w0 / q, w0 * w0 }, den[3] { 1.0, w0 / (A * q), w0 * w0 };
        return matched (num, den, w0);
    }

    inline Coeffs lowShelf (double hz, double db, double q, double sr)
    {
        const double w0 = w0Of (hz, sr), A = std::pow (10.0, db / 40.0), sA = std::sqrt (A);
        const double num[3] { A, A * sA * w0 / q, A * A * w0 * w0 }, den[3] { A, sA * w0 / q, w0 * w0 };
        return matched (num, den, w0);
    }

    inline Coeffs highShelf (double hz, double db, double q, double sr)
    {
        // Analog high shelf; its corner may sit above Nyquist (a 20 kHz "air" band still has a shape).
        const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, 30000.0, hz) / sr, A = std::pow (10.0, db / 40.0), sA = std::sqrt (A);
        const double num[3] { A * A, A * sA * w0 / q, A * w0 * w0 }, den[3] { 1.0, sA * w0 / q, A * w0 * w0 };
        return matched (num, den, std::min (w0, juce::MathConstants<double>::pi * 0.98));
    }

    /** RBJ (bilinear) types: cuts, notch, band-pass. */
    inline Coeffs rbj (BandType type, double hz, double q, double sr)
    {
        const double w0 = w0Of (hz, sr), cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        switch (type)
        {
            case BandType::LowCut:   b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case BandType::HighCut:  b0 = (1 - cw) / 2; b1 = 1 - cw;    b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case BandType::Notch:    b0 = 1; b1 = -2 * cw; b2 = 1;      a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case BandType::BandPass: b0 = alpha; b1 = 0; b2 = -alpha;   a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case BandType::Bell: case BandType::LowShelf: case BandType::HighShelf: case BandType::Tilt: break;   // matched designs
        }
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    /** Butterworth section Qs for 12 / 24 / 36 / 48 dB per octave. */
    inline const std::vector<double>& butterworthQs (int slope)
    {
        static const std::vector<double> q[4] { { 0.70711 }, { 0.54120, 1.30656 }, { 0.51764, 0.70711, 1.93185 }, { 0.50980, 0.60134, 0.89998, 2.56292 } };
        return q[juce::jlimit (0, 3, slope)];
    }
} // namespace design

struct BandSettings
{
    bool on = false;
    int type = 0;            // BandType
    float hz = 1000.0f, db = 0.0f, q = 1.0f;
    int slope = 1;           // cuts: index into slopeNames
    float dynRange = 0.0f;   // dB the gain may move by (0 = static); negative = cut when loud
    float dynThresh = -30.0f;
    int place = 0;           // placementNames
    bool operator== (const BandSettings&) const = default;
};

/** The sections of one band at a given gain (also used by the UI to draw the curve). */
inline int designBand (const BandSettings& s, float gainDb, double sr, Coeffs* out)
{
    const auto type = (BandType) s.type;
    switch (type)
    {
        case BandType::Bell:      out[0] = design::bell (s.hz, gainDb, s.q, sr); return 1;
        case BandType::LowShelf:  out[0] = design::lowShelf (s.hz, gainDb, std::min (s.q, 2.0f), sr); return 1;
        case BandType::HighShelf: out[0] = design::highShelf (s.hz, gainDb, std::min (s.q, 2.0f), sr); return 1;
        case BandType::Tilt:
        {
            // A gentle high shelf with half the tilt taken back off, so the curve pivots around hz.
            out[0] = design::highShelf (s.hz, gainDb, 0.5, sr);
            const double g = std::pow (10.0, -gainDb / 40.0);
            out[0].b0 *= g; out[0].b1 *= g; out[0].b2 *= g;
            return 1;
        }
        case BandType::LowCut:
        case BandType::HighCut:
        {
            const auto& qs = design::butterworthQs (s.slope);
            for (size_t i = 0; i < qs.size(); ++i)
            {
                // Q shapes the resonance at the corner (0.71 = flat Butterworth).
                const double q = i + 1 == qs.size() ? qs[i] * (double) s.q / 0.70711 : qs[i];
                out[i] = design::rbj (type, s.hz, std::max (0.3, q), sr);
            }
            return (int) qs.size();
        }
        case BandType::Notch:
        case BandType::BandPass:  out[0] = design::rbj (type, s.hz, s.q, sr); return 1;
    }
    return 0;
}

inline bool isGainType (int type) noexcept
{
    const auto t = (BandType) type;
    return t == BandType::Bell || t == BandType::LowShelf || t == BandType::HighShelf || t == BandType::Tilt;
}

//==============================================================================
class ProEq
{
public:
    static constexpr int kBands = 8;
    static constexpr int kMaxSections = 4;
    static constexpr int kUpdate = 16;   // samples between coefficient updates (smoothing + dynamics)

    void prepare (double newSr)
    {
        sr = newSr;
        smoothCoef = (float) std::exp (-(double) kUpdate / (0.03 * sr));
        envAtt = (float) std::exp (-1.0 / (0.003 * sr));
        envRel = (float) std::exp (-1.0 / (0.08 * sr));
        for (auto& b : bands)
            b.first = true;
        reset();
    }

    void reset()
    {
        for (auto& b : bands)
        {
            for (auto& ch : b.z) for (auto& sec : ch) sec[0] = sec[1] = 0.0;
            b.det1 = b.det2 = 0.0;
            b.env = 0.0f;
            b.dynDb = 0.0f;
        }
        counter = 0;
    }

    void set (const std::array<BandSettings, kBands>& s) noexcept { target = s; }

    /** Current dynamic gain offset of a band in dB (for the display). */
    float getDynamicDb (int band) const noexcept { return bands[(size_t) band].dynDb; }

    void process (float& l, float& r) noexcept
    {
        if (counter-- <= 0)
        {
            counter = kUpdate - 1;
            update();
        }
        double m = 0.5 * ((double) l + (double) r), s = 0.5 * ((double) l - (double) r);
        const double det = m;
        for (auto& b : bands)
        {
            if (! b.active)
                continue;
            if (b.dynamic)
            {
                // The band's own level, from a band-pass at its frequency on the incoming mid signal.
                const double y = b.det.b0 * det + b.det1;
                b.det1 = b.det.b1 * det - b.det.a1 * y + b.det2;
                b.det2 = b.det.b2 * det - b.det.a2 * y;
                const float a = (float) std::abs (y);
                b.env = a + (b.env - a) * (a > b.env ? envAtt : envRel);
            }
            if (b.place != 2)   // stereo or mid
                m = run (b, 0, m);
            if (b.place != 1)   // stereo or side
                s = run (b, 1, s);
        }
        l = (float) (m + s);
        r = (float) (m - s);
    }

    /** Static response in dB of the settings currently targeted (for the UI). */
    static double responseDb (const std::array<BandSettings, kBands>& s, double hz, double sampleRate)
    {
        double db = 0.0;
        Coeffs c[kMaxSections];
        for (auto& b : s)
        {
            if (! b.on)
                continue;
            const int n = designBand (b, b.db, sampleRate, c);
            for (int i = 0; i < n; ++i)
                db += magnitudeDb (c[i], hz, sampleRate);
        }
        return db;
    }

private:
    struct Band
    {
        BandSettings cur;            // smoothed toward the target
        bool first = true, active = false, dynamic = false;
        int place = 0, numSections = 0;
        Coeffs c[kMaxSections];
        double z[2][kMaxSections][2] {};
        Coeffs det;
        double det1 = 0.0, det2 = 0.0;
        float env = 0.0f, dynDb = 0.0f, lastGain = 1000.0f, lastHz = 0.0f, lastQ = 0.0f;
        int lastType = -1, lastSlope = -1;
    };

    static double run (Band& b, int ch, double x) noexcept
    {
        for (int i = 0; i < b.numSections; ++i)
        {
            const auto& c = b.c[i];
            auto& z = b.z[ch][i];
            const double y = c.b0 * x + z[0];
            z[0] = c.b1 * x - c.a1 * y + z[1];
            z[1] = c.b2 * x - c.a2 * y;
            x = y;
        }
        return x;
    }

    void update() noexcept
    {
        for (size_t i = 0; i < bands.size(); ++i)
        {
            auto& b = bands[i];
            const auto& t = target[i];
            if (b.first || t.type != b.cur.type || t.slope != b.cur.slope || ! b.cur.on)
            {
                // Type changes and switching on jump; continuous values glide.
                if (t.type != b.cur.type || t.slope != b.cur.slope)
                    for (auto& ch : b.z) for (auto& sec : ch) sec[0] = sec[1] = 0.0;
                b.cur = t;
                b.first = false;
            }
            else
            {
                b.cur.hz = (float) std::exp (std::log (t.hz) + (std::log (b.cur.hz) - std::log (t.hz)) * smoothCoef);
                b.cur.db = t.db + (b.cur.db - t.db) * smoothCoef;
                b.cur.q = t.q + (b.cur.q - t.q) * smoothCoef;
                b.cur.on = t.on;
                b.cur.dynRange = t.dynRange;
                b.cur.dynThresh = t.dynThresh;
                b.cur.place = t.place;
            }
            b.active = b.cur.on;
            b.place = b.cur.place;
            b.dynamic = b.active && isGainType (b.cur.type) && std::abs (b.cur.dynRange) > 0.05f;
            if (! b.active)
                continue;

            float gain = b.cur.db;
            if (b.dynamic)
            {
                if (std::abs (b.cur.hz - b.lastHz) > 0.5f || std::abs (b.cur.q - b.lastQ) > 0.01f || b.lastType != b.cur.type)
                    b.det = design::rbj (BandType::BandPass, b.cur.hz, std::max (0.5f, b.cur.q), sr);
                // Soft knee: the gain moves by up to dynRange as the band rises 0..12 dB over the threshold.
                const float over = dd::gainToDb (b.env * 1.41f) - b.cur.dynThresh;
                const float amount = juce::jlimit (0.0f, 1.0f, over / 12.0f);
                b.dynDb = b.cur.dynRange * amount * amount * (3.0f - 2.0f * amount);
                gain += b.dynDb;
            }
            else
                b.dynDb = 0.0f;

            if (std::abs (gain - b.lastGain) > 0.01f || std::abs (b.cur.hz - b.lastHz) > 0.05f || std::abs (b.cur.q - b.lastQ) > 0.001f
                || b.lastType != b.cur.type || b.lastSlope != b.cur.slope)
            {
                b.numSections = designBand (b.cur, gain, sr, b.c);
                b.lastGain = gain;
                b.lastHz = b.cur.hz;
                b.lastQ = b.cur.q;
                b.lastType = b.cur.type;
                b.lastSlope = b.cur.slope;
            }
        }
    }

    double sr = 48000.0;
    float smoothCoef = 0.9f, envAtt = 0.0f, envRel = 0.0f;
    int counter = 0;
    std::array<Band, kBands> bands {};
    std::array<BandSettings, kBands> target {};
};
} // namespace aircheck
