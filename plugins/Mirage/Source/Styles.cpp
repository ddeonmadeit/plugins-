#include "MirageView.h"
#include "Draw.h"

namespace mirage
{
//==============================================================================
void MirageView::styleFilm()
{
    // Grain like a risograph print (fine speckle plus coarser mottling) and the screen-door mesh of a
    // phosphor display, every third row and column a little darker.
    const int w = windowImg.getWidth(), h = windowImg.getHeight();
    const float grain = param ("grain"), heatGain = 0.86f + 0.28f * param ("heat");
    const int ox = rng.nextInt (512), oy = rng.nextInt (512), ox2 = rng.nextInt (512), oy2 = rng.nextInt (512);
    const int pitch = std::max (2, juce::roundToInt (3.0f * s));
    const float meshDark = 1.0f - 0.32f * param ("digital");
    meshCol.resize ((size_t) w);
    for (int x = 0; x < w; ++x)
        meshCol[(size_t) x] = (x % pitch) == 0 ? meshDark : 1.0f;
    juce::Image::BitmapData bd (windowImg, juce::Image::BitmapData::writeOnly);
    const auto& lut = pal.lut;
    for (int y = 0; y < h; ++y)
    {
        const float fy = (float) y;
        const float vy = 1.0f - 0.35f * std::pow (std::abs (fy / (float) h - 0.45f) * 2.0f, 2.0f);
        const float* nrow = noise.data() + (size_t) (((y + oy) & 511) * 512);
        const float* nrow2 = noise.data() + (size_t) ((((y >> 1) + oy2) & 511) * 512);
        const float rowMesh = (y % pitch) == 0 ? meshDark : 1.0f;
        const float* in = buf.data() + (size_t) (y * w);
        auto* px = reinterpret_cast<juce::PixelARGB*> (bd.getLinePointer (y));
        for (int x = 0; x < w; ++x)
        {
            const float n1 = nrow[(x + ox) & 511], n2 = nrow2[((x >> 1) + ox2) & 511];
            float v = (in[x] * (1.0f + grain * 0.22f * n2) + grain * (0.085f * n1 + 0.05f * n2)) * heatGain * vy;
            v *= std::min (rowMesh, meshCol[(size_t) x]);
            px[x] = lut[(size_t) Palette::index (v)];
        }
    }
}

//==============================================================================
void MirageView::buildTube()
{
    // The CRT's glass: the picture bulges (barrel), the corners round off into the black of the tube, the
    // edges fall off, and a soft reflection sits on the glass. All fixed for a size, so worked out once.
    const int w = windowImg.getWidth(), h = windowImg.getHeight();
    tubeW = w;
    tubeH = h;
    warp.assign ((size_t) (w * h), -1);
    tube.assign ((size_t) (w * h), 0.0f);
    glare.assign ((size_t) (w * h), 0.0f);
    const float corner = 0.20f * (float) h, soft = 3.5f * s;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float u = ((float) x + 0.5f) / (float) w * 2.0f - 1.0f, v = ((float) y + 0.5f) / (float) h * 2.0f - 1.0f;
            const float us = u * (1.0f + 0.035f * v * v), vs = v * (1.0f + 0.12f * u * u);
            if (std::abs (us) >= 1.0f || std::abs (vs) >= 1.0f)
                continue;
            const float sx = (us + 1.0f) * 0.5f * (float) w, sy = (vs + 1.0f) * 0.5f * (float) h;
            // Distance to the rounded edge of the picture (negative inside).
            const float qx = std::abs (sx - 0.5f * (float) w) - (0.5f * (float) w - corner);
            const float qy = std::abs (sy - 0.5f * (float) h) - (0.5f * (float) h - corner);
            const float out = std::sqrt (juce::square (std::max (qx, 0.0f)) + juce::square (std::max (qy, 0.0f))) + std::min (std::max (qx, qy), 0.0f) - corner;
            if (out >= 0.0f)
                continue;
            const size_t o = (size_t) (y * w + x);
            warp[o] = juce::jlimit (0, h - 1, (int) sy) * w + juce::jlimit (0, w - 1, (int) sx);
            const float edge = juce::jlimit (0.0f, 1.0f, -out / soft);
            const float vig = 1.0f - 0.22f * std::pow (std::abs (u), 3.0f) - 0.30f * std::pow (std::abs (v), 2.5f);
            tube[o] = edge * vig;
            glare[o] = 0.045f * std::exp (-(juce::square ((u + 0.62f) / 0.30f) + juce::square ((v + 0.45f) / 0.55f)))
                     + 0.035f * std::exp (out / (7.0f * s));
        }
}

void MirageView::styleCrt()
{
    // A glowing tube: phosphor that keeps a moment of what was drawn (trails), light that blooms past the
    // brightest strokes, scanlines that bright lines swell into, a curved picture with dark round corners,
    // a little flicker and a soft refresh band rolling down.
    const int w = windowImg.getWidth(), h = windowImg.getHeight();
    if (tubeW != w || tubeH != h)
        buildTube();
    const float grain = param ("grain"), digital = param ("digital"), heatGain = 0.86f + 0.28f * param ("heat");
    const float decay = std::pow (0.78f, frameDt * 60.0f);

    // Phosphor persistence, and on the same pass a quarter-size copy of it for the bloom.
    constexpr int F = 4;
    const int sw = (w + F - 1) / F, sh = (h + F - 1) / F;
    bloom.assign ((size_t) (sw * sh), 0.0f);
    bloomTmp.resize ((size_t) (sw * sh));
    for (int y = 0; y < h; ++y)
    {
        const float* in = buf.data() + (size_t) (y * w);
        float* p = persist.data() + (size_t) (y * w);
        float* dst = bloom.data() + (size_t) ((y / F) * sw);
        for (int x = 0; x < w; ++x)
        {
            p[x] = std::max (in[x] * heatGain, p[x] * decay);
            dst[x / F] += p[x];
        }
    }
    // Only what is bright blooms; blurred at quarter size, it is cheap.
    for (auto& v : bloom)
        v = std::max (0.0f, v * (1.0f / (float) (F * F)) - 0.30f);
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int y = 0; y < sh; ++y)
        {
            const float* src = bloom.data() + (size_t) (y * sw);
            float* dst = bloomTmp.data() + (size_t) (y * sw);
            for (int x = 0; x < sw; ++x)
            {
                float sum = 0.0f;
                for (int k = -3; k <= 3; ++k)
                    sum += src[juce::jlimit (0, sw - 1, x + k)];
                dst[x] = sum * (1.0f / 7.0f);
            }
        }
        for (int y = 0; y < sh; ++y)
            for (int x = 0; x < sw; ++x)
            {
                float sum = 0.0f;
                for (int k = -2; k <= 2; ++k)
                    sum += bloomTmp[(size_t) (juce::jlimit (0, sh - 1, y + k) * sw + x)];
                bloom[(size_t) (y * sw + x)] = sum * 0.2f;
            }
    }
    // Laid back over the picture (bilinear) into buf, which the scene no longer needs this frame.
    const float bloomAmt = 0.9f + 0.5f * an.energy;
    colNoise.resize ((size_t) sw);
    std::vector<int>& bx0 = bloomX0;
    bloomXw.resize ((size_t) w);
    bx0.resize ((size_t) w);
    for (int x = 0; x < w; ++x)
    {
        const float fx = juce::jlimit (0.0f, (float) (sw - 1), ((float) x + 0.5f) / (float) F - 0.5f);
        bx0[(size_t) x] = std::min ((int) fx, sw - 2 >= 0 ? sw - 2 : 0);
        bloomXw[(size_t) x] = fx - (float) bx0[(size_t) x];
    }
    for (int y = 0; y < h; ++y)
    {
        const float fy = juce::jlimit (0.0f, (float) (sh - 1), ((float) y + 0.5f) / (float) F - 0.5f);
        const int y0 = (int) fy, y1 = std::min (sh - 1, y0 + 1);
        const float ay = fy - (float) y0;
        const float* r0 = bloom.data() + (size_t) (y0 * sw);
        const float* r1 = bloom.data() + (size_t) (y1 * sw);
        for (int x = 0; x < sw; ++x)
            colNoise[(size_t) x] = bloomAmt * (r0[x] + (r1[x] - r0[x]) * ay);   // this row of the bloom
        const float* p = persist.data() + (size_t) (y * w);
        float* out = buf.data() + (size_t) (y * w);
        const float* mixed = colNoise.data();
        for (int x = 0; x < w; ++x)
        {
            const int i0 = bx0[(size_t) x];
            const int i1 = std::min (sw - 1, i0 + 1);
            out[x] = p[x] + mixed[i0] + (mixed[i1] - mixed[i0]) * bloomXw[(size_t) x];
        }
    }

    // Through the glass.
    const float flicker = 1.0f + 0.03f * (rng.nextFloat() - 0.5f) + 0.05f * flash;
    const float rollY = (float) std::fmod (an.time * 0.09, 1.0) * (float) h * 1.4f - 0.2f * (float) h;
    const int pitch = std::max (2, juce::roundToInt (2.0f * s));
    const int ox = rng.nextInt (512), oy = rng.nextInt (512);
    meshCol.resize ((size_t) w);
    for (int x = 0; x < w; ++x)
        meshCol[(size_t) x] = (x % 3) == 0 ? 1.0f - 0.10f * digital : 1.0f;
    juce::Image::BitmapData bd (windowImg, juce::Image::BitmapData::writeOnly);
    const auto& lut = pal.lut;
    for (int y = 0; y < h; ++y)
    {
        const bool gap = (y % pitch) == pitch - 1;
        const float roll = 1.0f + 0.06f * digital * std::exp (-juce::square (((float) y - rollY) / (0.10f * (float) h)));
        const float* nrow = noise.data() + (size_t) (((y + oy) & 511) * 512);
        const int* wp = warp.data() + (size_t) (y * w);
        const float* tb = tube.data() + (size_t) (y * w);
        const float* gl = glare.data() + (size_t) (y * w);
        auto* px = reinterpret_cast<juce::PixelARGB*> (bd.getLinePointer (y));
        for (int x = 0; x < w; ++x)
        {
            const int src = wp[x];
            if (src < 0)
            {
                px[x] = lut[(size_t) Palette::index (0.012f)];
                continue;
            }
            float v = (buf[(size_t) src] * 1.12f + 0.015f) * tb[x] * flicker * roll;
            if (gap)
                v *= 1.0f - 0.5f * digital * (1.0f - 0.55f * std::min (1.0f, v));
            v = v * meshCol[(size_t) x] + gl[x] + grain * 0.045f * nrow[(x + ox) & 511];
            px[x] = lut[(size_t) Palette::index (v)];
        }
    }
}

//==============================================================================
void MirageView::styleVhs()
{
    // A worn tape: bright things smear to the right with a hard bright leading edge, rows slip sideways
    // (a tracking tear rolls through, harder on hits), the bottom rows jitter from head switching, a soft
    // band of noise drifts across, bright bars roll by, sparkle dropouts, crushed blacks.
    const int w = windowImg.getWidth(), h = windowImg.getHeight();
    const float grain = param ("grain"), digital = param ("digital"), react = param ("react");
    const float heatGain = 0.86f + 0.28f * param ("heat");
    const float t = (float) an.time;
    const float decay = std::pow (0.55f, frameDt * 60.0f);

    // Row slips.
    tearPos += frameDt * (0.05f + 0.6f * flash * react);
    if (tearPos > 1.25f)
        tearPos -= 1.5f;
    rowShift.assign ((size_t) h, 0.0f);
    const float tearAmp = (5.0f + 46.0f * flash * react) * s;
    float jit = 0.0f;
    for (int y = 0; y < h; ++y)
    {
        if ((y & 1) == 0)
            jit = (rng.nextFloat() * 2.0f - 1.0f) * s * (0.4f + 1.4f * grain);
        float off = jit;
        const float rel = ((float) y / (float) h - tearPos) / 0.12f;
        if (rel > 0.0f && rel < 1.0f)
            off += tearAmp * juce::square (1.0f - rel) * (1.0f + 0.4f * std::sin ((float) y * 0.9f + t * 7.0f));
        if (y > h - std::max (3, h / 28))
            off += (8.0f + 14.0f * rng.nextFloat()) * s;
        rowShift[(size_t) y] = off;
    }
    // Rolling bright bars and the drifting noise band.
    const float barA = (float) std::fmod (1.0 - an.time * 0.21, 1.0) * (float) h * 1.3f - 0.15f * (float) h;
    const float barB = (float) std::fmod (0.4 - an.time * 0.13, 1.0) * (float) h * 1.3f - 0.15f * (float) h;
    const float barK = 0.10f + 0.35f * an.energy;
    colNoise.resize ((size_t) w);
    const float bandX = (float) std::fmod (an.time * 0.045, 1.0) * (float) w * 1.3f - 0.15f * (float) w;
    for (int x = 0; x < w; ++x)
        colNoise[(size_t) x] = std::exp (-juce::square (((float) x - bandX) / (0.05f * (float) w)));

    const float smear = std::exp (-1.0f / (14.0f * s));
    const int ox = rng.nextInt (512), oy = rng.nextInt (512);
    juce::Image::BitmapData bd (windowImg, juce::Image::BitmapData::writeOnly);
    const auto& lut = pal.lut;
    for (int y = 0; y < h; ++y)
    {
        const float fy = (float) y;
        const float bars = 1.0f + barK * (std::exp (-juce::square ((fy - barA) / (0.035f * (float) h)))
                                          + 0.7f * std::exp (-juce::square ((fy - barB) / (0.06f * (float) h))));
        const float lineNoise = 0.02f * (rng.nextFloat() - 0.5f) * (0.5f + grain);
        const float interlace = (y & 1) ? 1.0f - 0.10f * digital : 1.0f;
        const int shift = (int) std::lround (rowShift[(size_t) y]);
        float* row = persist.data() + (size_t) (y * w);
        {
            const float* in = buf.data() + (size_t) (y * w);
            for (int x = 0; x < w; ++x)
                row[x] = std::max (in[x] * heatGain, row[x] * decay);   // tape keeps a moment of the last frame
        }
        const float* nrow = noise.data() + (size_t) (((y + oy) & 511) * 512);
        auto* px = reinterpret_cast<juce::PixelARGB*> (bd.getLinePointer (y));
        float acc = 0.0f, lag = 0.0f;
        for (int x = 0; x < w; ++x)
        {
            const int sx = x - shift;
            const float v = row[sx < 0 ? 0 : sx >= w ? w - 1 : sx];
            acc = std::max (v, acc * smear);
            const float edge = std::max (0.0f, v - lag);
            lag += (v - lag) * 0.3f;
            float o = 0.5f * v + 0.5f * acc + 0.6f * edge;
            o = std::max (0.0f, o - 0.045f) * 1.12f;   // crushed blacks
            const float nz = nrow[(x + ox) & 511];
            o = (o * bars + lineNoise + grain * 0.06f * nz + colNoise[(size_t) x] * (0.06f + 0.10f * grain) * (nz + 0.6f)) * interlace;
            px[x] = lut[(size_t) Palette::index (o)];
        }
    }
    // Dropouts: short bright streaks where the tape lost its coating.
    const int drops = 3 + (int) (grain * 10.0f + flash * react * 30.0f);
    for (int i = 0; i < drops; ++i)
    {
        const int y = rng.nextInt (h), x0 = rng.nextInt (w), len = (int) ((4.0f + 50.0f * rng.nextFloat()) * s);
        auto* px = reinterpret_cast<juce::PixelARGB*> (bd.getLinePointer (y));
        const float b = 0.8f + 0.2f * rng.nextFloat();
        for (int x = x0; x < std::min (w, x0 + len); ++x)
            px[x] = lut[(size_t) Palette::index (b * (1.0f - 0.6f * (float) (x - x0) / (float) len))];
    }
}
} // namespace mirage
