#include "MirageView.h"
#include "Draw.h"

namespace mirage
{
namespace
{
/** Smooth value of the band spectrum at u (0..1, low to high). */
template <typename Bands>
float bandAt (const Bands& b, float u) noexcept
{
    const float fb = juce::jlimit (0.0f, 1.0f, u) * (float) (Analyzer::kBands - 1);
    const int i = std::min ((int) fb, Analyzer::kBands - 2);
    return b[(size_t) i] + (b[(size_t) i + 1] - b[(size_t) i]) * (fb - (float) i);
}

/** Loudest band in a range (0..1), so a whole part of the spectrum drives one shape. */
template <typename Bands>
float bandMax (const Bands& b, float u0, float u1) noexcept
{
    const int i0 = juce::jlimit (0, Analyzer::kBands - 1, (int) (u0 * (float) (Analyzer::kBands - 1)));
    const int i1 = juce::jlimit (i0, Analyzer::kBands - 1, (int) (u1 * (float) (Analyzer::kBands - 1)));
    float m = 0.0f;
    for (int i = i0; i <= i1; ++i)
        m = std::max (m, b[(size_t) i]);
    return m;
}

//==============================================================================
/** The wireframe figure: lathed parts (torso and head, legs, arms), each a stack of elliptical rings. */
struct Ring { float y, cx, rx, rz; };
struct Part { std::vector<Ring> rings; int segments; };

const std::vector<Part>& figureParts()
{
    static const std::vector<Part> parts = []
    {
        // Profile points (height, centre x, radius side to side, radius front to back), filled in between.
        auto lathe = [] (std::initializer_list<Ring> profile, int segments, float stepY)
        {
            Part p;
            p.segments = segments;
            std::vector<Ring> pts (profile);
            for (size_t i = 0; i + 1 < pts.size(); ++i)
            {
                const auto& a = pts[i];
                const auto& b = pts[i + 1];
                const int n = std::max (1, (int) std::round (std::abs (b.y - a.y) / stepY));
                for (int k = 0; k < n; ++k)
                {
                    const float f = (float) k / (float) n;
                    p.rings.push_back ({ a.y + (b.y - a.y) * f, a.cx + (b.cx - a.cx) * f, a.rx + (b.rx - a.rx) * f, a.rz + (b.rz - a.rz) * f });
                }
            }
            p.rings.push_back (pts.back());
            return p;
        };
        std::vector<Part> v;
        v.push_back (lathe ({ { 0.88f, 0.0f, 0.12f, 0.08f }, { 0.96f, 0.0f, 0.18f, 0.12f }, { 1.04f, 0.0f, 0.17f, 0.11f },
                              { 1.12f, 0.0f, 0.14f, 0.10f }, { 1.24f, 0.0f, 0.16f, 0.11f }, { 1.36f, 0.0f, 0.18f, 0.12f },
                              { 1.44f, 0.0f, 0.21f, 0.11f }, { 1.48f, 0.0f, 0.15f, 0.08f }, { 1.51f, 0.0f, 0.06f, 0.055f },
                              { 1.56f, 0.0f, 0.05f, 0.05f }, { 1.60f, 0.0f, 0.075f, 0.085f }, { 1.67f, 0.0f, 0.09f, 0.10f },
                              { 1.73f, 0.0f, 0.075f, 0.085f }, { 1.77f, 0.0f, 0.03f, 0.035f } }, 14, 0.04f));
        for (float side : { -1.0f, 1.0f })
        {
            v.push_back (lathe ({ { 0.0f, side * 0.10f, 0.035f, 0.07f }, { 0.06f, side * 0.10f, 0.04f, 0.045f }, { 0.12f, side * 0.10f, 0.04f, 0.04f },
                                  { 0.34f, side * 0.10f, 0.055f, 0.055f }, { 0.50f, side * 0.095f, 0.045f, 0.047f },
                                  { 0.72f, side * 0.095f, 0.07f, 0.07f }, { 0.92f, side * 0.09f, 0.085f, 0.08f } }, 10, 0.06f));
            v.push_back (lathe ({ { 1.45f, side * 0.215f, 0.05f, 0.05f }, { 1.30f, side * 0.235f, 0.042f, 0.042f },
                                  { 1.14f, side * 0.25f, 0.036f, 0.036f }, { 0.94f, side * 0.265f, 0.03f, 0.03f },
                                  { 0.86f, side * 0.27f, 0.036f, 0.024f }, { 0.78f, side * 0.272f, 0.02f, 0.016f } }, 8, 0.06f));
        }
        return v;
    }();
    return parts;
}

struct Cam
{
    float cx, cy, focal, dist, camY, ct, st;
    /** Model point (x, y, z) of a figure turned by (c, s) to screen; false behind the camera. */
    bool project (float x, float y, float z, float c, float s, float& sx, float& sy, float& depth) const noexcept
    {
        const float xr = x * c + z * s, zr = -x * s + z * c;
        const float Y = y - camY, Z = dist + zr;
        const float yc = Y * ct + Z * st, zc = -Y * st + Z * ct;
        if (zc < 0.1f)
            return false;
        sx = cx + xr / zc * focal;
        sy = cy - yc / zc * focal;
        depth = zr;
        return true;
    }
};
} // namespace

//==============================================================================
const MirageView::PolarSet& MirageView::polarMaps (int which, float cx, float cy)
{
    const int w = windowImg.getWidth(), h = windowImg.getHeight();
    auto& p = polar[(size_t) which];
    if (std::abs (cx - p.cx) < 0.5f && std::abs (cy - p.cy) < 0.5f && p.w == w && p.h == h)
        return p;
    p.cx = cx;
    p.cy = cy;
    p.w = w;
    p.h = h;
    const size_t n = (size_t) (w * h);
    for (auto* v : { &p.a, &p.r, &p.mapA, &p.mapB, &p.mapC })
        v->resize (n);
    p.bin.resize (n);
    // The tunnel is squashed sideways so the wide window fills with rings, not one huge first ring.
    const bool tunnel = which == 1;
    const float squash = tunnel ? 0.45f : 1.0f;
    auto ex = [this] (float x) { return expLut[(size_t) juce::jlimit (0, 2047, (int) (x * 256.0f))]; };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const size_t i = (size_t) (y * w + x);
            const float dx = ((float) x + 0.5f - cx) * squash, dy = (float) y + 0.5f - cy;
            const float a = std::atan2 (dx, -dy), r = std::sqrt (dx * dx + dy * dy) / (float) h;
            p.a[i] = a;
            p.r[i] = r;
            p.bin[i] = (juce::uint16) juce::jlimit (0, 1023, (int) ((a + juce::MathConstants<float>::pi) / juce::MathConstants<float>::twoPi * 1024.0f));
            if (tunnel)
            {
                const float z = 0.85f / (r + 0.02f);
                p.mapA[i] = z;
                p.mapB[i] = 1.0f / (1.0f + 0.035f * z * z);   // fog toward the far end
                p.mapC[i] = ex (r * 9.0f);                     // the light at the end
            }
            else
            {
                p.mapA[i] = 0.07f + 0.22f * ex (r * 1.6f);   // glow around the source
                p.mapB[i] = 0.30f + 0.25f * ex (r);          // how a flash falls off
                p.mapC[i] = ex (r * 0.45f);                  // how far the wedges of light reach
            }
        }
    return p;
}

void MirageView::addLine (float x0, float y0, float x1, float y1, float b)
{
    // Antialiased and additive: where lines cross they glow brighter, like a vector screen.
    const int w = windowImg.getWidth(), h = windowImg.getHeight();
    auto plot = [&] (int x, int y, float v)
    {
        if (x >= 0 && y >= 0 && x < w && y < h)
            buf[(size_t) (y * w + x)] += v;
    };
    const float dx = x1 - x0, dy = y1 - y0;
    const int n = std::max (1, (int) std::ceil (std::max (std::abs (dx), std::abs (dy))));
    if (n > 4 * (w + h))
        return;
    const bool steep = std::abs (dy) > std::abs (dx);
    for (int i = 0; i <= n; ++i)
    {
        const float t = (float) i / (float) n;
        const float x = x0 + dx * t, y = y0 + dy * t;
        if (steep)
        {
            const float fx = fastFloor (x), f = x - fx;
            const int iy = (int) fastFloor (y + 0.5f);
            plot ((int) fx, iy, b * (1.0f - f));
            plot ((int) fx + 1, iy, b * f);
        }
        else
        {
            const float fy = fastFloor (y), f = y - fy;
            const int ix = (int) fastFloor (x + 0.5f);
            plot (ix, (int) fy, b * (1.0f - f));
            plot (ix, (int) fy + 1, b * f);
        }
    }
}

//==============================================================================
void MirageView::sceneDunes()
{
    const int w = windowImg.getWidth(), h = windowImg.getHeight();
    const float react = param ("react"), dunes = param ("dunes"), eclipse = param ("eclipse");
    const float t = (float) an.time;
    constexpr int K = Analyzer::kLayers;

    const float horizon = (float) h * 0.58f;
    const float R = (float) h * 0.33f * (1.0f + 0.10f * sunPulse * react);
    const float sunX = (float) w * 0.70f, sunY = horizon - R * 0.16f;
    const float moonR = R * 0.955f;
    const float ang = -0.75f + 0.3f * std::sin (t * 0.11f);
    const float moonDist = R * (2.2f * (1.0f - eclipse)) + R * 0.03f * std::sin (t * 0.9f) * an.mid;
    const float moonX = sunX + std::cos (ang) * moonDist, moonY = sunY + std::sin (ang) * moonDist;

    // Ridge lines: the near dune follows the live waveform, the far ones the spectrum moments ago.
    const auto& recent = an.recentL;
    for (int k = 0; k < K; ++k)
    {
        const float d = (float) k / (float) (K - 1);
        const float baseline = horizon + ((float) h - horizon) * std::pow (1.0f - d, 1.25f) * 0.98f + 4.0f * s;
        const float amp = (float) h * 0.46f * (1.0f - 0.55f * d) * (0.35f + dunes);
        const float freq = 1.1f + 2.3f * d;
        const float phase = 0.37f * (float) k + t * (0.006f + 0.003f * (float) k) * (0.6f + param ("speed"));
        float* c = crest.data() + (size_t) k * (size_t) w;
        for (int x = 0; x < w; ++x)
        {
            const float u = (float) x / (float) (w - 1);
            float sx = u * freq + phase;
            sx -= std::floor (sx);
            const float prof = sx < 0.64f ? std::sin (juce::MathConstants<float>::halfPi * sx / 0.64f)
                                          : std::cos (juce::MathConstants<float>::halfPi * (sx - 0.64f) / 0.36f);
            const float roll = 0.5f + 0.5f * std::sin (juce::MathConstants<float>::twoPi * (u * freq * 0.41f + phase * 1.7f));
            float height = amp * (0.62f * prof + 0.26f * roll);
            if (k == 0)
            {
                // The live waveform: a moving maximum of the newest samples, swept across the dune.
                const int i = juce::jlimit (0, Analyzer::kRecent - 33, (int) (u * (float) (Analyzer::kRecent - 33)));
                float m = 0.0f;
                for (int j = 0; j < 32; j += 2)
                    m = std::max (m, std::abs (recent[(size_t) (i + j)]));
                height += (float) h * 0.34f * react * std::sqrt (m);
            }
            else
                height += amp * 0.9f * react * juce::square (bandAt (an.layers[(size_t) (k - 1)], u));
            c[x] = baseline - height;
        }
        // Wind smooths the sand: two box passes so the ridges read as dunes, not as a jagged graph.
        const int radius = std::max (2, (int) ((float) w * (0.004f + 0.006f * d)));
        for (int pass = 0; pass < 2; ++pass)
        {
            smooth.resize ((size_t) w + 1);
            smooth[0] = 0.0f;
            for (int x = 0; x < w; ++x)
                smooth[(size_t) x + 1] = smooth[(size_t) x] + c[x];
            for (int x = 0; x < w; ++x)
            {
                const int a = std::max (0, x - radius), b = std::min (w, x + radius + 1);
                c[x] = (smooth[(size_t) b] - smooth[(size_t) a]) / (float) (b - a);
            }
        }
    }

    // Per column, all layers side by side (the pixel loop walks them together): ridge, how much the face
    // turns to the sun, the highest ridge, and the sun's warmth on the sand below it.
    colCrest.resize ((size_t) (w * K));
    colLit.resize ((size_t) (w * K));
    colTop.resize ((size_t) w);
    colGlow.resize ((size_t) w);
    for (int x = 0; x < w; ++x)
    {
        float top = 1.0e9f;
        for (int k = 0; k < K; ++k)
        {
            const float* c = crest.data() + (size_t) k * (size_t) w;
            const int span = std::max (2, w / 160);
            const float slope = (c[std::min (x + span, w - 1)] - c[std::max (x - span, 0)]) / (2.0f * (float) span);
            colCrest[(size_t) (x * K + k)] = c[x];
            colLit[(size_t) (x * K + k)] = juce::jlimit (0.0f, 1.0f, slope * ((float) x < sunX ? 2.2f : -2.2f) / s);
            top = std::min (top, c[x]);
        }
        colTop[(size_t) x] = top;
        colGlow[(size_t) x] = 0.20f * std::exp (-std::abs ((float) x - sunX) / (0.22f * (float) w));
    }
    shimSin.resize ((size_t) w);
    shimCos.resize ((size_t) w);
    for (int x = 0; x < w; ++x)
    {
        shimSin[(size_t) x] = std::sin ((float) x * 0.013f);
        shimCos[(size_t) x] = std::cos ((float) x * 0.013f);
    }
    std::array<float, K> invShade {};
    for (int k = 0; k < K; ++k)
        invShade[(size_t) k] = 256.0f / ((9.0f + 20.0f * (1.0f - (float) k / (float) (K - 1))) * s);

    const float glowK = 0.55f + 0.45f * an.energy;
    const float ringR = R * (1.08f + 0.5f * (1.0f - an.hit));
    const float sunBox = R * 2.8f;
    auto ex = [this] (float x) { return expLut[(size_t) juce::jlimit (0, 2047, (int) (x * 256.0f))]; };

    for (int y = 0; y < h; ++y)
    {
        const float fy = (float) y;
        const float sky = 0.035f + 0.31f * std::pow (std::min (1.0f, fy / horizon), 2.2f);
        const float shimmerAmp = juce::jlimit (0.0f, 1.0f, 1.0f - (horizon - fy) / ((float) h * 0.22f)) * 2.6f * s;
        const float rowPhase = fy * 0.35f + t * 4.0f;
        const float rowSin = std::sin (rowPhase) * shimmerAmp, rowCos = std::cos (rowPhase) * shimmerAmp;
        float* out = buf.data() + (size_t) (y * w);
        for (int x = 0; x < w; ++x)
        {
            int layer = -1;
            if (fy >= colTop[(size_t) x])
            {
                const float* cc = colCrest.data() + (size_t) (x * K);
                for (int k = 0; k < K; ++k)
                    if (cc[k] <= fy) { layer = k; break; }
            }

            float v;
            if (layer < 0)
            {
                v = sky;
                const float dy = fy - sunY;
                const float dx0 = (float) x - sunX;
                if (std::abs (dx0) < sunBox && std::abs (dy) < sunBox)
                {
                    // Heat shimmer near the horizon: sin (row + x) by angle addition, no per-pixel sine.
                    const float dx = dx0 + rowSin * shimCos[(size_t) x] + rowCos * shimSin[(size_t) x];
                    const float dist = std::sqrt (dx * dx + dy * dy);
                    const float outside = sky + 0.30f * glowK * ex ((dist - R) / (0.32f * R))
                                        + 0.22f * an.hit * ex (std::abs (dist - ringR) / (0.025f * R));
                    if (dist < R + 1.5f)
                    {
                        const float r = dist / R;
                        const float r2 = r * r;
                        float inside = 0.60f + 0.35f * std::pow (std::max (0.0f, 1.0f - r2), 1.6f) - 0.10f * r2 * r2 * r2;
                        // The moon: a dark disc with the sun's light glowing around its rim.
                        const float mx = (float) x - moonX, my = fy - moonY;
                        const float dm = std::sqrt (mx * mx + my * my);
                        if (dm < moonR + 2.0f)
                        {
                            const float cover = juce::jlimit (0.0f, 1.0f, (moonR - dm) / (0.03f * R));
                            const float rim = 0.42f * ex ((moonR - dm) / (0.03f * R));
                            inside += (0.05f + rim - inside) * cover;
                        }
                        const float edge = juce::jlimit (0.0f, 1.0f, (R - dist) / 1.5f + 0.5f);
                        v = outside + (inside - outside) * edge;
                    }
                    else
                        v = outside;
                }
            }
            else
            {
                const float d = (float) layer / (float) (K - 1);
                const float below = fy - colCrest[(size_t) (x * K + layer)];
                // Sand in low sun: faces turned to the sun glow, the lee sides fall into deep shadow,
                // far dunes sit in the haze, every crest catches a line of light.
                const float lit = colLit[(size_t) (x * K + layer)];
                const float shade = expLut[(size_t) std::min (2047, (int) (below * invShade[(size_t) layer]))];
                const float glow = colGlow[(size_t) x] * (0.4f + 0.6f * shade) * (1.0f - 0.3f * d);
                const float rim = below < 1.8f * s ? (0.34f + (layer == 0 ? 0.4f * an.energy : 0.14f)) * (1.0f - below / (1.8f * s)) : 0.0f;
                // Below the lit band the sand keeps a little of the light, so faces read as surfaces, not streaks.
                v = 0.13f + 0.17f * d + (0.28f + 0.12f * d) * lit * shade + 0.09f * shade + glow + rim;
            }
            out[x] = v;
        }
    }
}

//==============================================================================
void MirageView::sceneBurst()
{
    // Shards of light bursting up from the bottom of the screen: each ray is a part of the spectrum (the
    // middle one the low end, the outer ones the highs), a jagged fan at the base is the spectrum around a
    // circle, and wide soft wedges and a flash of the whole screen come with the hits.
    const int w = windowImg.getWidth(), h = windowImg.getHeight();
    const float cx = (float) w * 0.5f, cy = (float) h * 1.03f;
    const auto& pm = polarMaps (0, cx, cy);
    const float react = param ("react"), rays = param ("dunes"), flashAmt = param ("eclipse");
    const float t = (float) an.time;
    constexpr int N = 13;
    constexpr float spread = 1.32f;
    std::array<float, N> theta {}, len {}, width {}, bright {}, phase {};
    float best = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        const float k = std::abs ((float) i - (float) (N / 2)) / (float) (N / 2);
        const float u0 = 0.02f + 0.80f * k, u1 = u0 + 0.10f;
        const float v = bandMax (an.band, u0, u1), vf = bandMax (an.bandFast, u0, u1);
        theta[(size_t) i] = spread * (2.0f * (float) i / (float) (N - 1) - 1.0f) + 0.035f * std::sin (t * 0.7f + (float) i * 1.3f);
        len[(size_t) i] = (0.55f + 1.6f * rays) * (0.30f + 1.1f * react * std::pow (v, 0.7f)) + 0.06f;
        width[(size_t) i] = 0.035f + 0.05f * v;
        bright[(size_t) i] = 0.55f + 0.75f * vf;
        phase[(size_t) i] = (float) i * 0.37f + t * (0.4f + 0.3f * (float) (i % 3));
        if (len[(size_t) i] > best)
        {
            best = len[(size_t) i];
            burstTip = { cx + std::sin (theta[(size_t) i]) * best * (float) h, cy - std::cos (theta[(size_t) i]) * best * (float) h };
        }
    }
    burstSource = { cx, cy };
    const float edge = 1.3f / (float) h;
    const float flashNow = flashAmt * flash;
    const float wedgeA = 0.95f * std::sin (t * 0.13f), wedgeB = -0.85f * std::sin (t * 0.11f + 1.0f);
    const float wedgeK = 0.15f + 0.55f * (an.energy * 0.4f + flash * (0.4f + 0.6f * flashAmt));
    auto ex = [this] (float x) { return expLut[(size_t) juce::jlimit (0, 2047, (int) (x * 256.0f))]; };
    // Everything that only depends on the angle, once per frame: the wedges of light, the length of the
    // jagged fan (the spectrum laid around the source, sharp teeth), and which two rays are nearest.
    for (int i = 0; i < 1024; ++i)
    {
        const float a = ((float) i + 0.5f) / 1024.0f * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::pi;
        const float dA = (a - wedgeA) / 0.20f, dB = (a - wedgeB) / 0.24f;
        angLutA[(size_t) i] = wedgeK * (ex (dA * dA) + 0.8f * ex (dB * dB));
        float tooth = (a + 1.5f) / 3.0f * 63.0f * 1.6f + t * 0.3f;
        tooth -= std::floor (tooth);
        const float fanV = bandAt (an.bandFast, 0.05f + 0.85f * juce::jlimit (0.0f, 1.0f, std::abs (a) / 1.5f));
        angLutB[(size_t) i] = (0.05f + 0.30f * react * fanV) * (0.45f + 0.55f * (1.0f - std::abs (tooth * 2.0f - 1.0f)));
        int nearI = 0;
        for (int k = 1; k < N; ++k)
            if (std::abs (a - theta[(size_t) k]) < std::abs (a - theta[(size_t) nearI]))
                nearI = k;
        rayNear[(size_t) i] = (juce::uint8) nearI;
        const int nextI = a > theta[(size_t) nearI] ? nearI + 1 : nearI - 1;
        rayNext[(size_t) i] = (juce::uint8) (nextI >= 0 && nextI < N ? nextI : 255);   // 255: no second ray
    }

    for (int y = 0; y < h; ++y)
    {
        const size_t row = (size_t) (y * w);
        const float* A = pm.a.data() + row;
        const float* Rr = pm.r.data() + row;
        const float* glowMap = pm.mapA.data() + row;
        const float* flashMap = pm.mapB.data() + row;
        const float* wedgeMap = pm.mapC.data() + row;
        const juce::uint16* ai = pm.bin.data() + row;
        float* out = buf.data() + row;
        for (int x = 0; x < w; ++x)
        {
            const float a = A[x], r = Rr[x];
            const int bin = ai[x];
            float v = glowMap[x] + flashNow * flashMap[x] + angLutA[(size_t) bin] * wedgeMap[x];
            for (const int i : { (int) rayNear[(size_t) bin], (int) rayNext[(size_t) bin] })
            {
                if (i >= N)
                    continue;
                const float L = len[(size_t) i];
                if (r >= L)
                    continue;
                const float q = 1.0f - r / L;
                float saw = r * 13.0f + phase[(size_t) i];
                saw -= (float) (int) saw;
                const float hw = width[(size_t) i] * q * (0.55f + 0.45f * (1.0f - saw));
                const float lat = std::abs (a - theta[(size_t) i]) * std::max (r, 0.02f);
                if (lat < hw + edge)
                {
                    const float cover = std::min (1.0f, (hw - lat) / edge);
                    if (cover > 0.0f)
                        v += cover * bright[(size_t) i] * (0.75f + 0.5f * (1.0f - lat / std::max (hw, 1.0e-4f)));
                }
            }
            const float fl = angLutB[(size_t) bin];
            if (r < fl)
                v += 0.8f * std::min (1.0f, (fl - r) / edge);
            out[x] = v;
        }
    }
}

void MirageView::drawBurstHud (juce::Graphics& g)
{
    const auto w = window;
    const int iw = windowImg.getWidth(), ih = windowImg.getHeight();
    // A protractor around the source: where the rays can reach, with the angle marked.
    const auto src = toScreen (burstSource.x, burstSource.y);
    const float rad = (float) ih * 0.92f * w.getHeight() / (float) ih;
    juce::Path ticks;
    for (int deg = -70; deg <= 70; deg += 5)
    {
        const float a = juce::degreesToRadians ((float) deg);
        const float r0 = rad, r1 = rad + ((deg % 30) == 0 ? 9.0f : 4.0f) * s;
        ticks.startNewSubPath (src.x + std::sin (a) * r0, src.y - std::cos (a) * r0);
        ticks.lineTo (src.x + std::sin (a) * r1, src.y - std::cos (a) * r1);
    }
    g.setColour (pal.red.withAlpha (0.65f));
    g.strokePath (ticks, juce::PathStrokeType (1.0f * s));
    for (int deg = -60; deg <= 60; deg += 30)
    {
        const float a = juce::degreesToRadians ((float) deg), r = rad + 18.0f * s;
        DotFont::draw (g, juce::String (deg) + "^", juce::Rectangle<float> (40.0f * s, 10.0f * s).withCentre ({ src.x + std::sin (a) * r, src.y - std::cos (a) * r }),
                       1.0f * s, pal.red, juce::Justification::centred, 0.5f);
    }

    // The longest shard gets a readout.
    const auto tip = toScreen (juce::jlimit (0.0f, (float) iw, burstTip.x), juce::jlimit (0.0f, (float) ih, burstTip.y));
    const bool left = tip.x > w.getCentreX();
    const juce::Point<float> elbow (tip.x + (left ? -28.0f : 28.0f) * s, std::max (w.getY() + 44.0f * s, tip.y - 24.0f * s));
    leader (g, tip, elbow, (left ? -96.0f : 96.0f) * s, pal.orange, s);
    DotFont::draw (g, "PEAK " + dbText (std::max (an.holdL, an.holdR)) + " DB",
                   juce::Rectangle<float> (120.0f * s, 12.0f * s).withPosition (elbow.x + (left ? -96.0f : 4.0f) * s, elbow.y - 14.0f * s), 1.3f * s, pal.orange);

    // The live waveform along the base.
    juce::Path wave;
    const float base = w.getBottom() - 16.0f * s;
    for (int i = 0; i <= 360; ++i)
    {
        const float sample = an.recentL[(size_t) (Analyzer::kRecent - 1441 + i * 4)];
        const juce::Point<float> p (w.getX() + w.getWidth() * (float) i / 360.0f, base - sample * 22.0f * s * param ("react"));
        if (i == 0) wave.startNewSubPath (p); else wave.lineTo (p);
    }
    glowStroke (g, wave, pal.orange.withAlpha (0.55f), 1.0f * s);
}

//==============================================================================
void MirageView::sceneTerrain()
{
    // Flying low over grainy land, the sound laid into it: every column of the screen is a part of the
    // spectrum (lows at the sides, so the middle is a valley), and the spectrum of each moment stays in the
    // ground and drifts away into the distance. Ridge tops catch the light, fog swallows the far hills.
    const int w = windowImg.getWidth(), h = windowImg.getHeight();
    const float react = param ("react"), hills = param ("dunes"), fog = param ("eclipse");
    const float horizon = (float) h * 0.30f;
    const float focal = (float) h * 0.85f, cx = (float) w * 0.5f;
    const float camH = 1.5f + 0.12f * sunPulse * react;
    constexpr float zNear = 0.55f, zFar = 70.0f;
    constexpr int steps = 150;
    const float ratio = std::pow (zFar / zNear, 1.0f / (float) steps);
    const float hillAmp = 0.30f + 1.1f * hills, specAmp = (0.4f + 1.4f * hills) * react;
    const float fogK = 0.015f + 0.11f * fog;
    constexpr int rows = 320;

    auto hill = [this] (float u, float v)
    {
        // Shifted far into the positive so truncation is floor; the tile wraps every 256.
        u += 65536.0f;
        v += 65536.0f;
        const int iu = (int) u, iv = (int) v;
        const int x0 = iu & 255, y0 = iv & 255, x1 = (x0 + 1) & 255, y1 = (y0 + 1) & 255;
        const float ax = u - (float) iu, ay = v - (float) iv;
        const float* n = smoothNoise.data();
        const float top = n[y0 * 256 + x0] + (n[y0 * 256 + x1] - n[y0 * 256 + x0]) * ax;
        const float bot = n[y1 * 256 + x0] + (n[y1 * 256 + x1] - n[y1 * 256 + x0]) * ax;
        return top + (bot - top) * ay;
    };

    // Sky: dark, with haze glowing along the horizon.
    for (int y = 0; y < h; ++y)
    {
        const float d = std::abs ((float) y - horizon) / ((float) h * 0.14f);
        std::fill (buf.begin() + (ptrdiff_t) (y * w), buf.begin() + (ptrdiff_t) ((y + 1) * w), 0.03f + 0.26f * std::exp (-d) * (0.7f + 0.5f * an.energy));
    }

    // Everything that only depends on the distance, once per step: Z, the projection, the fog, the
    // spectrum rows lying there.
    struct Step { float z, dz, proj, fog; const float* r0; const float* r1; float af; };
    std::array<Step, steps + 1> st {};
    {
        float z = zNear;
        for (int i = 0; i <= steps; ++i)
        {
            auto& e = st[(size_t) i];
            e.z = z;
            e.dz = std::max (1.0e-3f, z - z / ratio);
            e.proj = focal / z;
            e.fog = std::exp (-z * fogK);
            const float ra = (z - zNear) / 0.25f - travelRows;
            if (ra >= 0.0f && ra < (float) (rows - 2))
            {
                const int a0 = (int) ra;
                e.af = ra - (float) a0;
                e.r0 = specHist.data() + (size_t) (((specHead - a0 + rows) % rows) * Analyzer::kBands);
                e.r1 = specHist.data() + (size_t) (((specHead - a0 - 1 + rows) % rows) * Analyzer::kBands);
            }
            z *= ratio;
        }
    }

    // Marched every other column and doubled: the land is smooth, the grain added later is not.
    for (int x = 0; x < w; x += 2)
    {
        const int pair = x + 1 < w ? 2 : 1;
        const float k = std::abs ((float) x - cx) / cx;
        const float bu = 0.85f * (1.0f - k);   // band position for this column: lows at the sides
        const float bf = bu * (float) (Analyzer::kBands - 1);
        const int bi = std::min ((int) bf, Analyzer::kBands - 2);
        const float bfrac = bf - (float) bi;
        const float lateral = ((float) x - cx) / focal;
        const float sideK = specAmp * (1.0f + 0.3f * k);
        int ybuf = h;
        float prevH = 0.0f;
        for (int i = 0; i <= steps && ybuf > 0; ++i)
        {
            const auto& e = st[(size_t) i];
            const float X = lateral * e.z, Zw = e.z - travel;
            float H = hillAmp * (hill (X * 8.0f + 37.0f, Zw * 8.0f) - 0.45f) * 1.6f;
            if (e.z < 12.0f)
                H += 0.16f * hillAmp * (hill (X * 32.0f, Zw * 32.0f + 91.0f) - 0.5f);
            float sv = 0.0f;
            if (e.r0 != nullptr)
            {
                const float v0 = e.r0[bi] + (e.r0[bi + 1] - e.r0[bi]) * bfrac, v1 = e.r1[bi] + (e.r1[bi + 1] - e.r1[bi]) * bfrac;
                sv = v0 + (v1 - v0) * e.af;
                H += sideK * sv * sv;
            }
            const float yf = horizon + (camH - H) * e.proj;
            const int ys = yf <= 0.0f ? 0 : std::min (h, (int) yf + 1);
            if (ys < ybuf)
            {
                const float rise = (H - prevH) / e.dz;
                const float lit = juce::jlimit (0.0f, 1.0f, 0.18f + 2.2f * rise);
                const float body = (0.04f + 0.52f * lit * lit + 0.22f * sv) * e.fog + 0.09f * (1.0f - e.fog);
                const float fall = 0.25f * body / (float) std::max (1, ybuf - ys);
                float v = body;
                for (int y = ys; y < ybuf; ++y, v -= fall)
                {
                    buf[(size_t) (y * w + x)] = v;
                    buf[(size_t) (y * w + x + pair - 1)] = v;
                }
                const float ridge = (0.14f + 0.35f * sv) * e.fog;   // the ridge top catches the light
                buf[(size_t) (ys * w + x)] += ridge;
                if (pair > 1)
                    buf[(size_t) (ys * w + x + 1)] += ridge;
                ybuf = ys;
            }
            prevH = H;
        }
    }
}

void MirageView::drawTerrainHud (juce::Graphics& g)
{
    const auto w = window;
    // A dotted trace up the left side (the newest samples, standing up), and marks along the horizon.
    juce::RectangleList<float> dots;
    const float x0 = w.getX() + 30.0f * s;
    const int n = std::max (8, (int) (w.getHeight() / (3.0f * s)));
    for (int i = 0; i < n; ++i)
    {
        const float sample = an.recentL[(size_t) (Analyzer::kRecent - 1 - i * 6)];
        const auto p = toScreen ((x0 - w.getX()) * (float) windowImg.getWidth() / w.getWidth() + sample * 18.0f * s * param ("react"),
                                 (float) windowImg.getHeight() * (float) i / (float) n);
        dots.addWithoutMerging ({ p.x - 0.8f * s, p.y, 1.6f * s, 1.6f * s });
    }
    g.setColour (pal.yellow.interpolatedWith (pal.orange, 0.4f).withAlpha (0.9f));
    g.fillRectList (dots);

    const float hy = (float) windowImg.getHeight() * 0.30f;
    juce::Path marks;
    for (float x = 64.0f * s; x < w.getWidth() - 40.0f * s; x += 64.0f * s)
    {
        const auto p = toScreen (x * (float) windowImg.getWidth() / w.getWidth(), hy);
        marks.startNewSubPath (p.x, p.y - 3.0f * s);
        marks.lineTo (p.x, p.y + 3.0f * s);
    }
    g.setColour (pal.orange.withAlpha (0.5f));
    g.strokePath (marks, juce::PathStrokeType (1.0f * s));

    const float kmh = (1.2f + 7.0f * param ("speed")) * (0.75f + 0.5f * an.energy) * 3.6f * 10.0f;
    const auto text = "ALT " + pad (juce::roundToInt ((1.15f + 0.12f * sunPulse) * 100.0f), 4) + " M   SPD " + pad (juce::roundToInt (kmh), 3)
                    + " KM/H   HDG " + pad ((int) std::fmod (travel * 3.0f, 360.0f), 3) + "^";
    const float top = style() == (int) Style::vhs ? 36.0f * s : 14.0f * s;   // below the deck's PLAY label
    DotFont::draw (g, text, { w.getRight() - 360.0f * s, w.getY() + top, 340.0f * s, 12.0f * s }, 1.3f * s, pal.orange, juce::Justification::centredRight);
}

//==============================================================================
void MirageView::sceneFigure()
{
    // Three wireframe figures turning in their cages, each lit by a part of the spectrum (lows on the left,
    // highs on the right); rings swell where the sound is, a scan plane runs up and down them.
    const int w = windowImg.getWidth(), h = windowImg.getHeight();
    const float react = param ("react"), pulse = param ("dunes");
    const float t = (float) an.time;
    for (int y = 0; y < h; ++y)
    {
        const float v = 0.045f + 0.03f * (float) y / (float) h;
        std::fill (buf.begin() + (ptrdiff_t) (y * w), buf.begin() + (ptrdiff_t) ((y + 1) * w), v);
    }

    const auto& parts = figureParts();
    const float scanY = 0.9f + 0.95f * std::sin (t * 0.7f);
    std::vector<float> px, py, pd;
    for (int f = 0; f < 3; ++f)
    {
        const float u0 = f == 0 ? 0.0f : f == 1 ? 0.28f : 0.58f, u1 = f == 0 ? 0.28f : f == 1 ? 0.58f : 0.95f;
        const float near = f == 1 ? 1.0f : 0.9f;
        // Seen a little from above, aimed at the middle of the body, so cage and floor rings fit the height.
        Cam cam { (float) w * (0.36f + 0.14f * (float) f), (float) h * 0.5f, (float) h * 1.8f * near, 4.4f, 1.35f, std::cos (0.102f), std::sin (0.102f) };
        const float ang = spin + (float) f * 2.094f;
        const float c = std::cos (ang), sn = std::sin (ang);
        const float level = bandMax (an.bandFast, u0, u1);

        // The cage: edges, a grid on its faces, little panels that light with the highs.
        auto edge3 = [&] (float ax, float ay, float az, float bx, float by, float bz, float b)
        {
            float x0, y0, d0, x1, y1, d1;
            if (cam.project (ax, ay, az, c, sn, x0, y0, d0) && cam.project (bx, by, bz, c, sn, x1, y1, d1))
                addLine (x0, y0, x1, y1, b * (0.7f + 0.3f * juce::jlimit (0.0f, 1.0f, 0.5f - 0.8f * (d0 + d1) * 0.5f)));
        };
        const float W = 0.36f, D = 0.30f, Ht = 1.95f;
        const float cageB = 0.30f + 0.45f * flash;
        const float cx4[] { -W, W, W, -W }, cz4[] { -D, -D, D, D };
        for (int i = 0; i < 4; ++i)
        {
            const int j = (i + 1) % 4;
            edge3 (cx4[i], 0.0f, cz4[i], cx4[i], Ht, cz4[i], cageB);
            for (int rung = 0; rung <= 5; ++rung)
            {
                const float yy = Ht * (float) rung / 5.0f;
                edge3 (cx4[i], yy, cz4[i], cx4[j], yy, cz4[j], rung == 0 || rung == 5 ? cageB : cageB * 0.55f);
            }
            edge3 ((cx4[i] + cx4[j]) * 0.5f, 0.0f, (cz4[i] + cz4[j]) * 0.5f, (cx4[i] + cx4[j]) * 0.5f, Ht, (cz4[i] + cz4[j]) * 0.5f, cageB * 0.45f);
        }
        juce::Random panels (77 + f);
        const float hi = bandMax (an.bandFast, 0.6f, 1.0f);
        for (int i = 0; i < 16; ++i)
        {
            const int side = panels.nextInt (4), j = (side + 1) % 4;
            const float along = 0.15f + 0.7f * panels.nextFloat(), yy = 0.1f + 1.7f * panels.nextFloat(), sz = 0.05f;
            const float ax = cx4[side] + (cx4[j] - cx4[side]) * along, az = cz4[side] + (cz4[j] - cz4[side]) * along;
            const float dx = (cx4[j] - cx4[side]) * sz / (2.0f * W), dz = (cz4[j] - cz4[side]) * sz / (2.0f * D);
            const float b = cageB * 0.6f + 0.9f * hi * (panels.nextFloat() < 0.5f ? 1.0f : 0.3f);
            edge3 (ax, yy, az, ax + dx, yy, az + dz, b);
            edge3 (ax + dx, yy, az + dz, ax + dx, yy + sz, az + dz, b);
            edge3 (ax + dx, yy + sz, az + dz, ax, yy + sz, az, b);
            edge3 (ax, yy + sz, az, ax, yy, az, b);
        }
        // Floor rings that breathe with the low end.
        for (float rr : { 0.42f, 0.55f + 0.12f * an.low })
        {
            float lx = 0.0f, ly = 0.0f;
            bool have = false;
            for (int i = 0; i <= 32; ++i)
            {
                const float a = (float) i / 32.0f * juce::MathConstants<float>::twoPi;
                float x1 = 0.0f, y1 = 0.0f, d1 = 0.0f;
                const bool ok = cam.project (std::cos (a) * rr, 0.0f, std::sin (a) * rr, 1.0f, 0.0f, x1, y1, d1);
                if (ok && have)
                    addLine (lx, ly, x1, y1, 0.22f + 0.5f * an.low);
                lx = x1; ly = y1; have = ok;
            }
        }

        // The figure.
        for (const auto& part : parts)
        {
            const int n = part.segments;
            const size_t count = part.rings.size();
            px.assign (count * (size_t) n, 0.0f);
            py.assign (count * (size_t) n, 0.0f);
            pd.assign (count * (size_t) n, 0.0f);
            std::vector<float> ringB (count);
            for (size_t r = 0; r < count; ++r)
            {
                const auto& ring = part.rings[r];
                const float hu = u0 + (u1 - u0) * juce::jlimit (0.0f, 1.0f, ring.y / 1.8f);
                const float v = bandAt (an.band, hu);
                const float swell = 1.0f + pulse * 0.55f * react * v;
                const float scanB = 0.7f * std::exp (-juce::square ((ring.y - scanY) / 0.05f));
                ringB[r] = 0.32f + 0.75f * react * v + 0.35f * level + scanB;
                for (int k = 0; k < n; ++k)
                {
                    const float a = (float) k / (float) n * juce::MathConstants<float>::twoPi;
                    const size_t idx = r * (size_t) n + (size_t) k;
                    float d;
                    cam.project (ring.cx + std::cos (a) * ring.rx * swell, ring.y, std::sin (a) * ring.rz * swell, c, sn, px[idx], py[idx], d);
                    pd[idx] = d;
                }
            }
            for (size_t r = 0; r < count; ++r)
                for (int k = 0; k < n; ++k)
                {
                    const size_t i0 = r * (size_t) n + (size_t) k, i1 = r * (size_t) n + (size_t) ((k + 1) % n);
                    const float front = juce::jlimit (0.35f, 1.0f, 0.65f - 2.2f * (pd[i0] + pd[i1]) * 0.5f);
                    addLine (px[i0], py[i0], px[i1], py[i1], ringB[r] * front);
                    if (r + 1 < count)
                    {
                        const size_t i2 = (r + 1) * (size_t) n + (size_t) k;
                        addLine (px[i0], py[i0], px[i2], py[i2], 0.6f * ringB[r] * front);
                    }
                }
        }
    }
}

void MirageView::drawFigureHud (juce::Graphics& g)
{
    const auto w = window;
    const float dot = 1.3f * s, line = 12.0f * s;
    const auto hot = pal.yellow.interpolatedWith (pal.orange, 0.4f);
    // Right: the model sheet, its index scrolling with the sound.
    {
        auto area = juce::Rectangle<float> (w.getX() + w.getWidth() * 0.80f, w.getY() + 16.0f * s, w.getWidth() * 0.18f, line);
        DotFont::draw (g, "FIGURE SCAN", area, dot * 1.25f, hot);
        area.translate (0.0f, line * 1.3f);
        DotFont::draw (g, "MODEL 03  CONFIRM 1.0", area, dot, pal.orange);
        area.translate (0.0f, line);
        DotFont::draw (g, "ROT " + pad ((int) std::fmod (juce::radiansToDegrees (spin), 360.0f), 3) + "^  LVL " + pad (juce::roundToInt (an.energy * 100.0f), 3), area, dot, pal.orange);
        area.translate (0.0f, line * 1.4f);
        DotFont::draw (g, "SPECTRAL INDEX", area, dot, pal.orange);
        area.translate (0.0f, line);
        const int rowsShown = std::max (2, (int) ((w.getBottom() - area.getY() - 10.0f * s) / line));
        static const char* sym = "0123456789+=:E";
        for (int r = 0; r < rowsShown; ++r)
        {
            juce::String row;
            for (int c = 0; c < 14; ++c)
            {
                const int b = (r * 14 + c * 5) % Analyzer::kBands;
                const int v = (int) (an.bandFast[(size_t) b] * 97.0f) + (frames / 6) * (c + 1) + r * 7;
                row << juce::String::charToString ((juce::juce_wchar) (juce::uint8) sym[(size_t) (v % 14)]);
            }
            DotFont::draw (g, row, area, dot, r == 0 ? hot : pal.red);
            area.translate (0.0f, line);
        }
    }
    // Left: the screen's own settings.
    {
        auto area = juce::Rectangle<float> (w.getX() + 22.0f * s, w.getY() + w.getHeight() * 0.42f, w.getWidth() * 0.22f, line);
        for (auto text : { "PARALLAX 2:3", "COMPOSITE 60.2", "ASPECT 1:1", "SIGNAL LOCK" })
        {
            DotFont::draw (g, text, area, dot, pal.orange);
            area.translate (0.0f, line * 1.25f);
        }
        DotFont::draw (g, "HIT " + juce::String (flash > 0.5f ? "####" : flash > 0.2f ? "##" : "-"), area, dot, flash > 0.2f ? hot : pal.red);
    }
}

//==============================================================================
void MirageView::sceneTunnel()
{
    // Flying down a tunnel: rings come out of the light at the far end, each as bright as the sound was when
    // it was born; the walls are panels lit by the spectrum around the circle, and the tunnel twists.
    const int w = windowImg.getWidth(), h = windowImg.getHeight();
    const auto& pm = polarMaps (1, (float) w * 0.5f, (float) h * 0.5f);
    const float react = param ("react"), twist = param ("eclipse"), rings = param ("dunes");
    const float t = (float) an.time;
    const float freq = 1.0f + 3.5f * rings;

    // New rings at the far end take the sound of the moment.
    const int newest = (int) std::floor (9.0f * freq + tunnelDepth);
    for (int guard = 0; lastRing < newest && guard < 64; ++guard)
    {
        ++lastRing;
        ringVals[(size_t) (lastRing & 63)] = juce::jlimit (0.0f, 1.6f, 0.15f + react * (0.6f * an.energy + 0.5f * an.low + 0.7f * an.hit));
    }
    lastRing = std::max (lastRing, newest - 63);

    std::array<float, 48> panelsLit {};
    for (int i = 0; i < 48; ++i)
        panelsLit[(size_t) i] = 0.08f + 0.55f * react * std::pow (bandAt (an.band, 0.04f + 0.86f * std::abs ((float) i / 47.0f * 2.0f - 1.0f)), 1.2f);
    const float glow = 0.25f + 0.7f * an.low * react;
    // A ring's line across its width, looked up rather than worked out per pixel.
    for (int i = 0; i < 1024; ++i)
    {
        const float rd = (float) i / 2047.0f;   // distance to the nearest ring, 0..0.5
        angLutA[(size_t) i] = std::exp (-rd * rd * 900.0f);
    }
    const float twistK = twist * 0.06f, spinU = std::fmod (t * 0.015f, 1.0f) + 0.5f;

    for (int y = 0; y < h; ++y)
    {
        const size_t row = (size_t) (y * w);
        const float* A = pm.a.data() + row;
        const float* Z = pm.mapA.data() + row;
        const float* fog = pm.mapB.data() + row;
        const float* light = pm.mapC.data() + row;
        float* out = buf.data() + row;
        for (int x = 0; x < w; ++x)
        {
            const float z = Z[x];
            float u = A[x] * (1.0f / juce::MathConstants<float>::twoPi) + spinU + twistK * z;   // never negative
            u -= (float) (int) u;
            const float pu = u * 48.0f;
            const int pi = (int) pu;
            const float seam = std::abs (pu - (float) pi - 0.5f) * 2.0f;   // 1 at panel edges
            const float rp = z * freq + tunnelDepth;
            const int ring = (int) rp;
            const float rf = rp - (float) ring;
            const float line = angLutA[(size_t) std::min (1023, (int) (std::min (rf, 1.0f - rf) * 2047.0f))];
            const float parity = (ring & 1) ? 1.0f : 0.82f;
            const float s2 = seam * seam;
            out[x] = (panelsLit[(size_t) pi] * parity * (0.8f + 0.2f * seam) + 0.10f * s2 * s2
                      + line * (0.35f + ringVals[(size_t) (ring & 63)])) * fog[x] + glow * light[x];
        }
    }
}

void MirageView::drawTunnelHud (juce::Graphics& g)
{
    const auto w = window;
    const auto c = toScreen ((float) windowImg.getWidth() * 0.5f, (float) windowImg.getHeight() * 0.5f);
    const float t = (float) an.time;
    const float rad = w.getHeight() * 0.18f;
    juce::Path ring;
    for (int i = 0; i < 4; ++i)
    {
        const float a0 = t * 0.4f + (float) i * juce::MathConstants<float>::halfPi;
        ring.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a0 + 0.9f, true);
    }
    glowStroke (g, ring, pal.orange.withAlpha (0.6f + 0.3f * an.energy), 1.1f * s);
    g.setColour (pal.red.withAlpha (0.6f));
    g.drawLine (c.x - rad * 1.5f, c.y, c.x - rad * 1.15f, c.y, 1.0f * s);
    g.drawLine (c.x + rad * 1.15f, c.y, c.x + rad * 1.5f, c.y, 1.0f * s);
    const juce::Point<float> tip (c.x + rad * 0.72f, c.y - rad * 0.72f);
    const juce::Point<float> elbow (tip.x + 30.0f * s, tip.y - 26.0f * s);
    leader (g, tip, elbow, 110.0f * s, pal.orange, s);
    DotFont::draw (g, "DEPTH " + pad ((int) (tunnelDepth * 10.0f) % 100000, 5) + " M",
                   juce::Rectangle<float> (140.0f * s, 12.0f * s).withPosition (elbow.x + 4.0f * s, elbow.y - 14.0f * s), 1.3f * s, pal.orange);
    DotFont::draw (g, "RING " + pad (lastRing % 1000, 3) + "  LOW " + pad (juce::roundToInt (an.low * 100.0f), 3) + "%",
                   juce::Rectangle<float> (180.0f * s, 12.0f * s).withPosition (elbow.x + 4.0f * s, elbow.y + 2.0f * s), 1.3f * s, pal.red);
}
} // namespace mirage
