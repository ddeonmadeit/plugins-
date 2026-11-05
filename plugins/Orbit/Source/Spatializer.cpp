#include "Spatializer.h"

namespace orbit
{
namespace
{
constexpr float kSpeedOfSound = 343.0f;
constexpr float kRefDistance = 1.5f;    // a sound this far away keeps the input level (before room makeup)

// Feedback delay lengths relative to the room's mean free path: spread over an octave and a half, no common factors.
constexpr std::array<float, 16> kLineRatios { 0.553f, 0.617f, 0.683f, 0.739f, 0.797f, 0.857f, 0.919f, 0.977f,
                                              1.041f, 1.109f, 1.181f, 1.249f, 1.327f, 1.403f, 1.487f, 1.583f };

float roomLevelGain (float v) noexcept
{
    v = juce::jlimit (0.0f, 1.0f, v);
    return v <= 0.5f ? std::pow (v / 0.5f, 1.5f) : 1.0f + (v - 0.5f) * 3.0f;
}

float onePoleG (float hz, double sr) noexcept
{
    const float g = dd::tptG (hz, sr);
    return g / (1.0f + g);
}

/** In-place fast Walsh-Hadamard transform of 16 values, scaled to stay energy preserving. */
void hadamard16 (float* v) noexcept
{
    for (int h = 1; h < 16; h <<= 1)
        for (int i = 0; i < 16; i += h << 1)
            for (int j = i; j < i + h; ++j)
            {
                const float a = v[j], b = v[j + h];
                v[j] = a + b;
                v[j + h] = a - b;
            }
    for (int i = 0; i < 16; ++i)
        v[i] *= 0.25f;
}

/** y[j] = sum_k h[k] x[j - k] for j in [0, n), x pointing at the first new sample (history before it).
    Written tap by tap so the inner loop is a plain multiply-add over the block, which vectorises. */
inline void fir (const float* h, int taps, const float* x, float* y, int n) noexcept
{
    std::fill (y, y + n, 0.0f);
    for (int k = 0; k < taps; ++k)
    {
        const float hk = h[k];
        const float* xp = x - k;
        for (int j = 0; j < n; ++j)
            y[j] += hk * xp[j];
    }
}
} // namespace

void Spatializer::prepare (double sampleRate)
{
    sr = sampleRate;
    hrtf.prepare (sr);
    taps = hrtf.taps();
    maxItd = (int) std::ceil (0.0008 * sr) + 2;

    for (auto& s : src)
    {
        s.line.prepare ((int) (2.3 * sr));
        for (int e = 0; e < 2; ++e)
        {
            s.hPrev[e].assign ((size_t) taps, 0.0f);
            s.hNext[e].assign ((size_t) taps, 0.0f);
            s.hist[e].assign ((size_t) (taps - 1 + kMaxChunk), 0.0f);
        }
    }
    for (int i = 0; i < kLines; ++i)
    {
        lines[(size_t) i].prepare ((int) (0.13 * sr));
        modRate[(size_t) i] = 0.07f + 0.041f * (float) i;
        modPhase[(size_t) i] = 0.37 * i;
    }

    // Sixteen directions: a ring at ear level, four above, four below.
    int b = 0;
    for (int i = 0; i < 8; ++i)
        busDir[(size_t) b++] = fromPolar ({ juce::degreesToRadians (45.0f * (float) i), 0.0f, 1.0f });
    for (int i = 0; i < 4; ++i)
        busDir[(size_t) b++] = fromPolar ({ juce::degreesToRadians (45.0f + 90.0f * (float) i), juce::degreesToRadians (45.0f), 1.0f });
    for (int i = 0; i < 4; ++i)
        busDir[(size_t) b++] = fromPolar ({ juce::degreesToRadians (90.0f * (float) i), juce::degreesToRadians (-40.0f), 1.0f });
    for (int i = 0; i < kBuses; ++i)
    {
        const auto pd = toPolar (busDir[(size_t) i]);
        const float itd = HrtfSet::itdSeconds (pd.az, pd.el) * (float) sr;
        busItd[0][(size_t) i] = (int) std::lround (std::max (0.0f, itd));
        busItd[1][(size_t) i] = (int) std::lround (std::max (0.0f, -itd));
        const float lat = std::sin (pd.az) * std::cos (pd.el);
        const float theta = (lat + 1.0f) * 0.25f * dd::kPi;
        const float rear = std::cos (pd.az) < -0.1f ? 0.85f : 1.0f;
        busSpk[(size_t) i] = { std::cos (theta) * rear, std::sin (theta) * rear };
        for (int e = 0; e < 2; ++e)
            busH[e][(size_t) i].assign ((size_t) taps, 0.0f);
        busHist[(size_t) i].assign ((size_t) (taps - 1 + maxItd + kMaxChunk), 0.0f);
    }
    lastHead = -1;
    busEqSize = -1.0f;

    sideHp1.set (dd::Biquad::Type::HighPass, 120.0f, 0.5412f, 0.0f, sr);
    sideHp2.set (dd::Biquad::Type::HighPass, 120.0f, 1.3066f, 0.0f, sr);
    reset();
}

void Spatializer::reset()
{
    for (auto& s : src)
    {
        s.line.reset();
        for (int e = 0; e < 2; ++e)
        {
            std::fill (s.hist[e].begin(), s.hist[e].end(), 0.0f);
            s.air[(size_t) e].reset();
            s.heightHp[e].reset();
        }
        for (auto& f : s.reflLp)
            f.reset();
    }
    for (auto& l : lines)
        l.reset();
    lineState.fill (0.0f);
    for (auto& h : busHist)
        std::fill (h.begin(), h.end(), 0.0f);
    for (auto& f : busLowCut)
        f.reset();
    for (auto& f : busShelf)
        f.reset();
    sideHp1.reset();
    sideHp2.reset();
    lateRms = 0.0f;
    fresh = true;
    intervalPos = kMaxChunk;
}

float Spatializer::distanceGain (float r) const noexcept
{
    return std::min (2.0f, std::pow (kRefDistance / std::max (r, 0.2f), 0.9f));
}

void Spatializer::imagesFor (Vec3 s, std::array<Image, kImages>& out) const noexcept
{
    const float W = room.dims.x, L = room.dims.y, H = room.dims.z;
    // Per axis: the one-bounce images (low wall, high wall) and the two-bounce ones (both walls).
    struct Axis { float one[2]; float two[2]; float r[2]; };
    const Axis ax[3] {
        { { -W - s.x, W - s.x }, { s.x - 2.0f * W, s.x + 2.0f * W }, { room.wallR, room.wallR } },
        { { -L - s.y, L - s.y }, { s.y - 2.0f * L, s.y + 2.0f * L }, { room.wallR, room.wallR } },
        { { -s.z, 2.0f * H - s.z }, { s.z - 2.0f * H, s.z + 2.0f * H }, { room.floorR, room.ceilR } },
    };
    auto with = [s] (int axis, float v) { Vec3 p = s; (axis == 0 ? p.x : axis == 1 ? p.y : p.z) = v; return p; };

    int k = 0;
    for (int a = 0; a < 3; ++a)
        for (int w = 0; w < 2; ++w)
            out[(size_t) k++] = { with (a, ax[a].one[w]), ax[a].r[w], 1 };
    for (int a = 0; a < 3; ++a)
        for (int w = 0; w < 2; ++w)
            out[(size_t) k++] = { with (a, ax[a].two[w]), ax[a].r[0] * ax[a].r[1], 2 };
    for (int a = 0; a < 3; ++a)
        for (int b = a + 1; b < 3; ++b)
            for (int wa = 0; wa < 2; ++wa)
                for (int wb = 0; wb < 2; ++wb)
                {
                    Vec3 p = with (a, ax[a].one[wa]);
                    (b == 1 ? p.y : p.z) = (b == 1 ? ax[1].one[wb] : ax[2].one[wb]);
                    out[(size_t) k++] = { p, ax[a].r[wa] * ax[b].r[wb], 2 };
                }
    jassert (k == kImages);
}

void Spatializer::busPan (Vec3 dir, std::array<float, kBuses>& out) const noexcept
{
    float sum = 0.0f;
    for (int b = 0; b < kBuses; ++b)
    {
        const float c = std::max (0.0f, dir.dot (busDir[(size_t) b]));
        const float c2 = c * c;
        const float w = c2 * c2 * c;   // ^5: about one neighbour on each side
        out[(size_t) b] = w;
        sum += w * w;
    }
    const float norm = sum > 1.0e-9f ? 1.0f / std::sqrt (sum) : 0.0f;
    for (auto& w : out)
        w *= norm;
}

void Spatializer::updateTargets (const SpatialParams& p)
{
    // Smooth the room (switching rooms glides instead of clicking) and the position.
    const auto target = RoomState::make (p.room, p.size);
    const float chunkSec = (float) kMaxChunk / (float) sr;
    const float kRoom = fresh ? 1.0f : 1.0f - std::exp (-chunkSec / 0.15f);
    const float kPos = fresh ? 1.0f : 1.0f - std::exp (-chunkSec / 0.04f);
    auto glide = [] (float& v, float t, float k) { v += (t - v) * k; };
    glide (room.dims.x, target.dims.x, kRoom); glide (room.dims.y, target.dims.y, kRoom); glide (room.dims.z, target.dims.z, kRoom);
    glide (room.rt60, target.rt60, kRoom); glide (room.hfRatio, target.hfRatio, kRoom);
    glide (room.wallR, target.wallR, kRoom); glide (room.floorR, target.floorR, kRoom); glide (room.ceilR, target.ceilR, kRoom);
    glide (room.erHz, target.erHz, kRoom); glide (room.lateTrim, target.lateTrim, kRoom);
    glide (posN.x, p.position.x, kPos); glide (posN.y, p.position.y, kPos); glide (posN.z, p.position.z, kPos);
    glide (width, p.width, kPos);

    const auto ear = room.listener();
    const auto centre = toPolar (room.toMetres (posN) - ear);
    const float spread = juce::jlimit (0.0f, 1.0f, width) * 0.5f * dd::kPi;
    const float roomGain = roomLevelGain (p.roomLevel);
    const float m = 0.05f;

    const bool headChanged = p.head != lastHead || p.speakers != lastSpeakers;
    if (headChanged)
    {
        for (int b = 0; b < kBuses; ++b)
        {
            const auto pd = toPolar (busDir[(size_t) b]);
            hrtf.lookup (p.head, pd.az, pd.el, busH[0][(size_t) b].data(), busH[1][(size_t) b].data());
        }
        for (auto& s : src)
            s.filterMoved = true;
    }
    if (fresh)
        for (auto& s : src)
            s.lastAz = s.lastEl = 99.0f;

    for (int si = 0; si < kSources; ++si)
    {
        auto& s = src[(size_t) si];
        Polar ps = centre;
        ps.az += si == 0 ? -spread : spread;
        Vec3 pos = ear + fromPolar (ps);
        pos.x = juce::jlimit (-0.5f * room.dims.x + m, 0.5f * room.dims.x - m, pos.x);
        pos.y = juce::jlimit (-0.5f * room.dims.y + m, 0.5f * room.dims.y - m, pos.y);
        pos.z = juce::jlimit (m, room.dims.z - m, pos.z);
        ps = toPolar (pos - ear);

        // Direct sound.
        s.earDelayPrev = s.earDelay;
        s.earGainPrev = s.earGain;
        s.panPrev = s.pan;
        const float d0 = 1.0f + juce::jlimit (0.0f, 1.0f, p.doppler) * ps.r / kSpeedOfSound * (float) sr;
        const float gd = distanceGain (ps.r);
        const float lateral = std::sin (ps.az) * std::cos (ps.el);
        const float near = juce::jlimit (0.0f, 1.0f, 1.0f - ps.r) * lateral;   // close to one ear: that ear much louder
        float airHz = 22000.0f / (1.0f + ps.r / 25.0f);
        if (p.speakers)
        {
            const float theta = (lateral + 1.0f) * 0.25f * dd::kPi;
            s.earDelay = { d0, d0 };
            s.earGain = { gd * std::cos (theta), gd * std::sin (theta) };
            s.heightCue = 0.6f * std::sin (ps.el);
            airHz *= 1.0f - 0.55f * std::max (0.0f, -std::cos (ps.az)) * std::cos (ps.el);   // behind: duller
        }
        else
        {
            const float itd = HrtfSet::itdSeconds (ps.az, ps.el) * (float) sr;
            s.earDelay = { d0 + std::max (0.0f, itd), d0 + std::max (0.0f, -itd) };
            s.earGain = { gd * dd::dbToGain (-4.0f * near), gd * dd::dbToGain (3.0f * near) };
            s.heightCue = 0.0f;
            if (headChanged || std::abs (ps.az - s.lastAz) > 1.0e-5f || std::abs (ps.el - s.lastEl) > 1.0e-5f)
            {
                hrtf.lookup (p.head, ps.az, ps.el, s.hNext[0].data(), s.hNext[1].data());
                s.filterMoved = true;
                s.lastAz = ps.az;
                s.lastEl = ps.el;
            }
        }
        for (auto& a : s.air)
            a.setCutoff (airHz, sr);
        for (auto& h : s.heightHp)
            h.setCutoff (6000.0f, sr);

        // Early reflections.
        std::array<Image, kImages> img;
        imagesFor (pos, img);
        s.reflPrev = s.refl;
        for (int i = 0; i < kImages; ++i)
        {
            auto& t = s.refl[(size_t) i];
            const auto v = img[(size_t) i].pos - ear;
            const float ri = std::max (0.2f, v.length());
            t.gain = img[(size_t) i].reflect * distanceGain (ri) * roomGain;
            t.delay = d0 + std::max (0.0f, ri - ps.r) / kSpeedOfSound * (float) sr;
            t.lpG = onePoleG (std::min (room.erHz / (float) img[(size_t) i].order, 22000.0f / (1.0f + ri / 25.0f)), sr);
            busPan (v * (1.0f / ri), t.pan);
        }

        s.lateTapPrev = s.lateTap;
        s.lateTap = d0 + 0.9f * room.meanFreePath() / kSpeedOfSound * (float) sr;
        if (fresh)
        {
            s.hPrev[0] = s.hNext[0];
            s.hPrev[1] = s.hNext[1];
            s.filterMoved = false;
            s.earDelayPrev = s.earDelay;
            s.earGainPrev = s.earGain;
            s.panPrev = s.pan;
            s.reflPrev = s.refl;
            s.lateTapPrev = s.lateTap;
        }
    }

    // Late field: decay from RT60, darker per pass by the room's high-frequency ratio.
    const float tau = juce::jlimit (0.006f, 0.07f, room.meanFreePath() / kSpeedOfSound);
    const float rt = std::max (0.05f, room.rt60);
    for (int i = 0; i < kLines; ++i)
    {
        const float len = tau * kLineRatios[(size_t) i] * (float) sr;
        lineLen[(size_t) i] = len;
        const float g = std::pow (10.0f, -3.0f * len / (rt * (float) sr));
        const float gHf = std::pow (10.0f, -3.0f * len / (rt * std::max (0.1f, room.hfRatio) * (float) sr));
        const float q = juce::jlimit (0.01f, 1.0f, gHf / g);
        lineGain[(size_t) i] = g;
        lineDampA[(size_t) i] = (1.0f - q) / (1.0f + q);
    }
    // A unit impulse into the network leaves with energy ~ rt / (13.8 tau): scale that back to 1, then
    // set the reverberant level from the critical distance (softened: real small rooms are overwhelming).
    const float fdnNorm = std::sqrt (13.8f * tau / rt);
    const float rc = room.criticalDistance();
    lateGainPrev = lateGain;
    lateGain = roomGain * room.lateTrim * std::sqrt (kRefDistance / rc) * fdnNorm;

    // Loudness: a sound at the room's usual distance (a quarter of its length, 1..6 m) comes out at input level.
    {
        const float rNom = juce::jlimit (1.0f, 6.0f, 0.25f * room.dims.y);
        const Vec3 nom = ear + Vec3 { 0.0f, rNom, 0.0f };
        std::array<Image, kImages> img;
        imagesFor (nom, img);
        float e = distanceGain (rNom) * distanceGain (rNom);
        for (auto& im : img)
        {
            const float g = im.reflect * distanceGain ((im.pos - ear).length()) * roomGain;
            e += g * g * 0.6f;   // each bounce is darker
        }
        const float late = roomGain * room.lateTrim * std::sqrt (kRefDistance / rc);
        e += 0.5f * late * late;
        makeupPrev = makeup;
        makeup = juce::jlimit (0.1f, 10.0f, 1.0f / std::sqrt (e));
        if (fresh)
            makeupPrev = makeup;
    }
    if (fresh)
        lateGainPrev = lateGain;

    // Room EQ: reflections in a small room pile up into boom and box, so cut them low; a big hall keeps its weight.
    {
        const float k = juce::jlimit (0.0f, 1.0f, std::log10 (std::max (1.0f, room.volume()) / 100.0f) / 2.5f);
        if (std::abs (k - busEqSize) > 0.01f)
        {
            busEqSize = k;
            for (int b = 0; b < kBuses; ++b)
            {
                busLowCut[(size_t) b].set (dd::Biquad::Type::HighPass, 180.0f - 120.0f * k, 0.7071f, 0.0f, sr);
                busShelf[(size_t) b].set (dd::Biquad::Type::LowShelf, 450.0f, 0.7f, -5.0f + 3.5f * k, sr);
            }
        }
    }

    // Late-line modulation (a slow wobble of each delay against metallic ringing), interpolated across the interval.
    modPrev = modNext;
    const double step = juce::MathConstants<double>::twoPi * kMaxChunk / sr;
    for (int i = 0; i < kLines; ++i)
    {
        auto& ph = modPhase[(size_t) i];
        ph += step * modRate[(size_t) i];
        if (ph > juce::MathConstants<double>::twoPi)
            ph -= juce::MathConstants<double>::twoPi;
        const float depth = std::min (0.0004f * (float) sr, 0.04f * lineLen[(size_t) i]);
        modNext[(size_t) i] = lineLen[(size_t) i] + depth * (float) std::sin (ph);
    }
    if (fresh)
        modPrev = modNext;

    lastHead = p.head;
    lastSpeakers = p.speakers;
    speakersNow = p.speakers;
}

void Spatializer::process (const float* inL, const float* inR, float* outL, float* outR, int n, const SpatialParams& p)
{
    // Targets move every kMaxChunk samples whatever the host's block size; ramps run across calls.
    int done = 0;
    while (done < n)
    {
        if (intervalPos >= kMaxChunk)
        {
            updateTargets (p);
            fresh = false;
            intervalPos = 0;
        }
        const int m = std::min (n - done, kMaxChunk - intervalPos);
        renderSegment (inL + done, inR + done, outL + done, outR + done, m, intervalPos);
        intervalPos += m;
        done += m;
        if (intervalPos >= kMaxChunk)
            for (auto& s : src)
                if (s.filterMoved)
                {
                    s.hPrev[0] = s.hNext[0];
                    s.hPrev[1] = s.hNext[1];
                    s.filterMoved = false;
                }
    }
}

void Spatializer::renderSegment (const float* inL, const float* inR, float* outL, float* outR, int n, int q)
{
    constexpr float invK = 1.0f / (float) kMaxChunk;
    auto ramp = [q, invK] (float a, float b, int j) noexcept { return a + (b - a) * (float) (q + j + 1) * invK; };
    const bool speakers = speakersNow;

    std::fill (wetL.begin(), wetL.begin() + n, 0.0f);
    std::fill (wetR.begin(), wetR.begin() + n, 0.0f);
    for (auto& b : bus)
        std::fill (b.begin(), b.begin() + n, 0.0f);
    float* wet[2] = { wetL.data(), wetR.data() };
    // Two sources at one spot add up: half each keeps a mono sound at its level, wide stereo a little under.
    const float srcScale = 0.5f + 0.25f * juce::jlimit (0.0f, 1.0f, width);

    for (int si = 0; si < kSources; ++si)
    {
        auto& s = src[(size_t) si];
        const float* in = si == 0 ? inL : inR;
        for (int j = 0; j < n; ++j)
            s.line.push (in[j] * srcScale);

        // Direct: delay (travel + interaural), air, gain, then the head filter (or panning).
        for (int e = 0; e < 2; ++e)
        {
            float* h = s.hist[(size_t) e].data();
            float* x = h + taps - 1;
            const float d0 = s.earDelayPrev[(size_t) e], d1 = s.earDelay[(size_t) e];
            const float g0 = s.earGainPrev[(size_t) e], g1 = s.earGain[(size_t) e];
            for (int j = 0; j < n; ++j)
            {
                float v = s.line.read (ramp (d0, d1, j) + (float) (n - 1 - j));
                v = s.air[(size_t) e].lp (v);
                if (speakers)
                    v += s.heightCue * s.heightHp[e].hp (v);
                x[j] = v * ramp (g0, g1, j);
            }
            if (speakers)
            {
                for (int j = 0; j < n; ++j)
                    wet[e][j] += x[j];
            }
            else
            {
                fir (s.hNext[(size_t) e].data(), taps, x, tmp.data(), n);
                if (s.filterMoved)
                {
                    fir (s.hPrev[(size_t) e].data(), taps, x, tmp2.data(), n);
                    for (int j = 0; j < n; ++j)
                        tmp[(size_t) j] = ramp (tmp2[(size_t) j], tmp[(size_t) j], j);
                }
                for (int j = 0; j < n; ++j)
                    wet[e][j] += tmp[(size_t) j];
            }
            std::memmove (h, h + n, sizeof (float) * (size_t) (taps - 1));
        }

        // Early reflections onto the direction buses.
        for (int i = 0; i < kImages; ++i)
        {
            const auto& t1 = s.refl[(size_t) i];
            const auto& t0 = s.reflPrev[(size_t) i];
            if (t1.gain < 1.0e-5f && t0.gain < 1.0e-5f)
                continue;
            auto& lp = s.reflLp[(size_t) i];
            lp.G = t1.lpG;
            for (int j = 0; j < n; ++j)
            {
                const float v = s.line.read (ramp (t0.delay, t1.delay, j) + (float) (n - 1 - j));
                tmp[(size_t) j] = lp.lp (v * ramp (t0.gain, t1.gain, j));
            }
            for (int b = 0; b < kBuses; ++b)
            {
                const float w0 = t0.pan[(size_t) b], w1 = t1.pan[(size_t) b];
                if (w0 <= 0.0f && w1 <= 0.0f)
                    continue;
                float* dst = bus[(size_t) b].data();
                for (int j = 0; j < n; ++j)
                    dst[j] += tmp[(size_t) j] * ramp (w0, w1, j);
            }
        }

        // Feed for the late field, after the first reflections have arrived.
        for (int j = 0; j < n; ++j)
            lateIn[(size_t) si][(size_t) j] = s.line.read (ramp (s.lateTapPrev, s.lateTap, j) + (float) (n - 1 - j));
    }

    // Late field: one line per direction bus.
    {
        constexpr float inj = 0.35355339f;   // 1 / sqrt (8): each source's injection has unit energy
        float v[kLines], mixed[kLines];
        float energy = 0.0f;
        for (int j = 0; j < n; ++j)
        {
            const float lg = ramp (lateGainPrev, lateGain, j);
            for (int i = 0; i < kLines; ++i)
            {
                const float out = lines[(size_t) i].read (ramp (modPrev[(size_t) i], modNext[(size_t) i], j));
                auto& st = lineState[(size_t) i];
                st = (1.0f - lineDampA[(size_t) i]) * out + lineDampA[(size_t) i] * st;
                v[i] = lineGain[(size_t) i] * st;
                mixed[i] = v[i];
                bus[(size_t) i][(size_t) j] += v[i] * lg;
                energy += v[i] * v[i];
            }
            hadamard16 (mixed);
            for (int i = 0; i < kLines; ++i)
            {
                const float sign = ((i >> 1) & 1) ? -1.0f : 1.0f;
                lines[(size_t) i].push (mixed[i] + inj * sign * lateIn[(size_t) (i & 1)][(size_t) j]);
            }
        }
        const float rms = std::sqrt (energy / (float) (n * kLines)) * lateGain * 4.0f;
        lateRms += (rms - lateRms) * 0.05f * (float) n * invK * 4.0f;
    }

    // Render the buses: a head filter per direction (or panning on speakers).
    for (int b = 0; b < kBuses; ++b)
    {
        float* x = bus[(size_t) b].data();
        auto& lowCut = busLowCut[(size_t) b];
        auto& shelf = busShelf[(size_t) b];
        for (int j = 0; j < n; ++j)
            x[j] = shelf.process (lowCut.process (x[j]));
        if (speakers)
        {
            for (int j = 0; j < n; ++j)
            {
                wetL[(size_t) j] += x[j] * busSpk[(size_t) b][0];
                wetR[(size_t) j] += x[j] * busSpk[(size_t) b][1];
            }
            continue;
        }
        auto& hist = busHist[(size_t) b];
        const int keep = taps - 1 + maxItd;
        float* hx = hist.data() + keep;
        std::copy (x, x + n, hx);
        for (int e = 0; e < 2; ++e)
        {
            fir (busH[e][(size_t) b].data(), taps, hx - busItd[e][(size_t) b], tmp.data(), n);
            for (int j = 0; j < n; ++j)
                wet[e][j] += tmp[(size_t) j];
        }
        std::memmove (hist.data(), hist.data() + n, sizeof (float) * (size_t) keep);
    }

    // Level, then the sub folded to mono (low side removed).
    for (int j = 0; j < n; ++j)
    {
        const float g = ramp (makeupPrev, makeup, j);
        const float l = wetL[(size_t) j] * g, r = wetR[(size_t) j] * g;
        const float mid = 0.5f * (l + r);
        const float side = sideHp2.process (sideHp1.process (0.5f * (l - r)));
        outL[j] = mid + side;
        outR[j] = mid - side;
    }
}
} // namespace orbit
