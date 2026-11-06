#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace orbit
{
/**
    Geometry shared by the engine and the drawn room. Room coordinates are metres: the floor's centre is
    the origin, x runs to the listener's right, y forward (the way the listener faces), z up. The listener
    stands in the middle of the floor plan, ears at earHeight(). Positions in parameters are normalised:
    x and y -1..1 wall to wall, z 0..1 floor to ceiling, so (0, 0) is the listener's spot in every room.
*/
struct Vec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3 operator+ (Vec3 o) const noexcept { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator- (Vec3 o) const noexcept { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator* (float s) const noexcept { return { x * s, y * s, z * s }; }
    float dot (Vec3 o) const noexcept { return x * o.x + y * o.y + z * o.z; }
    float length() const noexcept { return std::sqrt (x * x + y * y + z * z); }
};

/** One acoustic space: dimensions at Size 50 %, how long it rings, and what its surfaces are made of. */
struct RoomDef
{
    const char* name;
    float width, length, height;   // metres
    float rt60;                     // mid-band decay, seconds (scales with the room)
    float hfRatio;                  // decay at 8 kHz relative to rt60 (soft furnishings < 0.5 < hard stone)
    float wallR, floorR, ceilR;     // pressure reflection per bounce (0 = absorbs everything)
    float erHz;                     // brightness of a single bounce (one-pole cutoff)
    float lateTrim;                 // late reverb level beside the physics (open air has next to none)
};

inline const std::array<RoomDef, 14>& rooms()
{
    //                         name           W      L      H    RT60   hf   wall  floor ceil   erHz   late
    static const std::array<RoomDef, 14> r {{
        { "Studio",           5.0f,  6.0f,  3.0f, 0.32f, 0.60f, 0.50f, 0.62f, 0.35f,  9000.0f, 1.00f },
        { "Vocal Booth",      1.8f,  2.2f,  2.4f, 0.14f, 0.45f, 0.40f, 0.65f, 0.35f,  5000.0f, 0.80f },
        { "Living Room",      5.0f,  7.0f,  2.6f, 0.55f, 0.55f, 0.56f, 0.50f, 0.66f,  7000.0f, 1.00f },
        { "Bathroom",         2.4f,  3.0f,  2.5f, 1.10f, 0.85f, 0.93f, 0.93f, 0.90f, 14000.0f, 1.30f },
        { "Car",              1.5f,  2.4f,  1.2f, 0.08f, 0.40f, 0.55f, 0.35f, 0.40f,  6000.0f, 0.70f },
        { "Club",            14.0f, 20.0f,  5.0f, 1.00f, 0.50f, 0.72f, 0.82f, 0.50f,  6000.0f, 1.00f },
        { "Warehouse",       25.0f, 40.0f,  9.0f, 2.60f, 0.70f, 0.86f, 0.90f, 0.80f, 10000.0f, 1.00f },
        { "Concert Hall",    26.0f, 40.0f, 16.0f, 2.10f, 0.65f, 0.80f, 0.62f, 0.82f,  8000.0f, 1.00f },
        { "Cathedral",       26.0f, 65.0f, 28.0f, 5.00f, 0.60f, 0.90f, 0.90f, 0.86f,  8000.0f, 1.30f },
        { "Arena",           90.0f, 110.0f, 30.0f, 3.20f, 0.50f, 0.78f, 0.60f, 0.60f,  5000.0f, 1.00f },
        { "Cave",            14.0f, 30.0f, 10.0f, 3.80f, 0.45f, 0.86f, 0.86f, 0.86f,  4500.0f, 1.00f },
        { "Open Air",        40.0f, 40.0f, 20.0f, 0.60f, 0.40f, 0.00f, 0.70f, 0.00f,  7000.0f, 0.05f },
        // 1.1: soul rooms. A concrete echo chamber like Motown's, and an old cinema turned studio like Stax's.
        { "Echo Chamber",     4.0f,  6.0f,  3.5f, 2.30f, 0.75f, 0.95f, 0.95f, 0.95f, 12000.0f, 1.30f },
        { "Theater",         22.0f, 30.0f,  9.0f, 1.40f, 0.55f, 0.70f, 0.45f, 0.75f,  6500.0f, 1.00f },
    }};
    return r;
}

inline juce::StringArray roomNames()
{
    juce::StringArray s;
    for (auto& r : rooms())
        s.add (r.name);
    return s;
}

/** Size knob (0..1, 0.5 = as listed) to a scale for every dimension: 0.4x .. 2.5x. */
inline float sizeScale (float size01) noexcept { return 0.4f * std::pow (6.25f, juce::jlimit (0.0f, 1.0f, size01)); }

/** The room actually in use: dimensions in metres and the decay that comes with them. */
struct RoomState
{
    Vec3 dims { 5.0f, 6.0f, 3.0f };   // width, length, height
    float rt60 = 0.35f, hfRatio = 0.6f;
    float wallR = 0.6f, floorR = 0.8f, ceilR = 0.5f, erHz = 9000.0f, lateTrim = 1.0f;

    static RoomState make (int index, float size01) noexcept
    {
        const auto& d = rooms()[(size_t) juce::jlimit (0, (int) rooms().size() - 1, index)];
        const float s = sizeScale (size01);
        RoomState r;
        r.dims = { d.width * s, d.length * s, d.height * s };
        r.rt60 = d.rt60 * s;   // Sabine: volume over surface grows with the room
        r.hfRatio = d.hfRatio;
        r.wallR = d.wallR; r.floorR = d.floorR; r.ceilR = d.ceilR;
        r.erHz = d.erHz; r.lateTrim = d.lateTrim;
        return r;
    }

    float earHeight() const noexcept { return std::min (1.2f, 0.7f * dims.z); }
    Vec3 listener() const noexcept { return { 0.0f, 0.0f, earHeight() }; }
    float volume() const noexcept { return dims.x * dims.y * dims.z; }
    float surface() const noexcept { return 2.0f * (dims.x * dims.y + dims.x * dims.z + dims.y * dims.z); }
    float meanFreePath() const noexcept { return 4.0f * volume() / surface(); }
    /** Where direct and reverberant sound are equally loud (Sabine room, omni source). */
    float criticalDistance() const noexcept { return 0.057f * std::sqrt (volume() / std::max (0.05f, rt60)); }

    /** Normalised position to metres, kept a hand's width inside the walls and out of the listener's head. */
    Vec3 toMetres (Vec3 n) const noexcept
    {
        const float m = std::min (0.1f, 0.2f * dims.z);
        Vec3 p { juce::jlimit (-1.0f, 1.0f, n.x) * (0.5f * dims.x - m),
                 juce::jlimit (-1.0f, 1.0f, n.y) * (0.5f * dims.y - m),
                 m + juce::jlimit (0.0f, 1.0f, n.z) * (dims.z - 2.0f * m) };
        const auto ear = listener();
        auto d = p - ear;
        const float r = d.length();
        constexpr float minR = 0.25f;
        if (r < minR)
            p = ear + (r > 1.0e-4f ? d * (minR / r) : Vec3 { 0.0f, minR, 0.0f });
        return p;
    }
};

/** Direction of a point from the listener: azimuth clockwise from straight ahead (radians, -pi..pi),
    elevation (radians), distance (metres). */
struct Polar { float az = 0.0f, el = 0.0f, r = 1.0f; };

inline Polar toPolar (Vec3 d) noexcept
{
    Polar p;
    p.r = std::max (1.0e-4f, d.length());
    p.az = std::atan2 (d.x, d.y);
    p.el = std::asin (juce::jlimit (-1.0f, 1.0f, d.z / p.r));
    return p;
}

inline Vec3 fromPolar (Polar p) noexcept
{
    const float c = std::cos (p.el);
    return { p.r * c * std::sin (p.az), p.r * c * std::cos (p.az), p.r * std::sin (p.el) };
}

//==============================================================================
/** Motion: the fourth dimension. A path traced around the placed position, locked to the song. */
inline const juce::StringArray& motionNames()
{
    static const juce::StringArray n { "Static", "Orbit", "Pendulum", "Figure 8", "Fly By", "Rise", "Spiral", "Wander" };
    return n;
}

inline const juce::StringArray& motionRateNames()
{
    static const juce::StringArray n { "16 Bars", "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8" };
    return n;
}

/** Beats for one trip around the path. */
inline double motionCycleBeats (int rate) noexcept
{
    static const double beats[] { 64.0, 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5 };
    return beats[juce::jlimit (0, 7, rate)];
}

/**
    Offset from the placed position (normalised room units) at `phase` cycles into the motion, for a
    path of size `amount` (0..1: 1 reaches from the middle of the room to a wall). Every path is smooth
    and returns to where it started, so a cycle loops without a jump.
*/
inline Vec3 motionOffset (int mode, double phase, float amount) noexcept
{
    const float a = juce::jlimit (0.0f, 1.0f, amount);
    const double frac = phase - std::floor (phase);
    const float t = (float) (frac * juce::MathConstants<double>::twoPi);
    switch (mode)
    {
        case 1:  return { a * std::sin (t), a * std::cos (t) - a, 0.0f };                       // orbit: a circle through the placed point
        case 2:  return { a * std::sin (t), -0.25f * a * (1.0f - std::cos (2.0f * t)), 0.0f };  // pendulum: side to side on an arc
        case 3:  return { a * std::sin (t), 0.5f * a * std::sin (2.0f * t), 0.0f };             // figure 8
        case 4:  return { 0.25f * a * std::sin (t), a * std::sin (t + 1.5708f), 0.0f };          // fly by: front to back, passing at the side
        case 5:  return { 0.0f, 0.0f, 0.5f * a * std::sin (t) };                                 // rise and fall
        case 6:                                                                                  // spiral: circles that climb over four turns
        {
            const float slow = (float) ((phase * 0.25 - std::floor (phase * 0.25)) * juce::MathConstants<double>::twoPi);
            const float rad = a * (0.55f + 0.45f * std::sin (slow));
            return { rad * std::sin (t), rad * std::cos (t) - a, 0.45f * a * std::sin (slow) };
        }
        case 7:                                                                                  // wander: never quite repeats
        {
            const double p = phase * juce::MathConstants<double>::twoPi;
            return { a * (float) (0.62 * std::sin (p * 1.00 + 0.3) + 0.38 * std::sin (p * 2.618 + 1.7)),
                     a * (float) (0.55 * std::sin (p * 1.414 + 2.1) + 0.45 * std::sin (p * 0.618 + 0.4)),
                     0.4f * a * (float) (0.6 * std::sin (p * 1.732 + 0.9) + 0.4 * std::sin (p * 0.809 + 2.6)) };
        }
        default: return {};
    }
}

/** Placed position plus motion, kept inside the normalised room. */
inline Vec3 movedPosition (Vec3 placed, int mode, double phase, float amount) noexcept
{
    const auto o = motionOffset (mode, phase, amount);
    return { juce::jlimit (-1.0f, 1.0f, placed.x + o.x), juce::jlimit (-1.0f, 1.0f, placed.y + o.y),
             juce::jlimit (0.0f, 1.0f, placed.z + o.z) };
}
} // namespace orbit
