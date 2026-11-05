#include "RoomView.h"

namespace
{
using orbit::Vec3;
namespace sunset = dd::gui::sunset;

constexpr float kCamDist = 3.6f;
constexpr float kOrbRadius = 0.055f;    // display units
constexpr float kHeadRadius = 0.065f;

// The scene stays dark in both themes (a lit room at night).
const juce::Colour kNight { 0xff0b0908 }, kFloorNear { 0xff2b231c }, kFloorFar { 0xff120f0c };
const juce::Colour kCream { 0xffe9dfcf }, kCreamDim { 0xffa39a8c }, kGlass { 0xff8c8070 };

Vec3 cross (Vec3 a, Vec3 b) noexcept { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

const char* viewNames[] { "BEHIND", "ABOVE", "SIDE" };

float levelToUnit (float rms) noexcept { return juce::jlimit (0.0f, 1.0f, (dd::gainToDb (rms) + 50.0f) / 44.0f); }
} // namespace

RoomView::RoomView (OrbitProcessor& p) : proc (p)
{
    auto& s = p.apvts;
    pX = s.getParameter ("posX");
    pY = s.getParameter ("posY");
    pZ = s.getParameter ("posZ");
    pWidth = s.getParameter ("width");
    pRoom = s.getParameter ("room");
    pSize = s.getParameter ("size");
    pMotion = s.getParameter ("motion");
    pAmount = s.getParameter ("motionAmount");
    pOutput = s.getParameter ("perfOutput");

    dd::gui::setHelp (*this, "Room", "Drag the light to place the sound around your head. Shift-drag or scroll to raise it, "
                                     "right-drag to turn the view, double-click the light to bring it back in front of you.");
    for (int i = 0; i < 3; ++i)
    {
        auto* k = viewKeys.add (new dd::gui::Key (viewNames[i], i == 0));
        k->setGlowMargin (6);
        k->setFlickerPeriod (5.1 + 0.6 * i);
        k->onClick = [this, i] { setView (i); };
        dd::gui::setHelp (*k, juce::String (viewNames[i]) + " view",
                          i == 0 ? "See the room from just behind your own head." : i == 1 ? "Look down on the room from above, like a map."
                                                                                           : "See the room from the side: height is easiest to set here.");
        addAndMakeVisible (k);
    }
    room = orbit::RoomState::make ((int) std::lround (pRoom->convertFrom0to1 (pRoom->getValue())), pSize->convertFrom0to1 (pSize->getValue()));
    heard = orbit::movedPosition ({ pX->convertFrom0to1 (pX->getValue()), pY->convertFrom0to1 (pY->getValue()), pZ->convertFrom0to1 (pZ->getValue()) },
                                  (int) std::lround (pMotion->convertFrom0to1 (pMotion->getValue())), proc.telemetry.phase.load(),
                                  pAmount->convertFrom0to1 (pAmount->getValue()));
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setOpaque (false);
    startTimerHz (60);
}

RoomView::~RoomView() { stopTimer(); }

void RoomView::resized()
{
    stage = getLocalBounds().toFloat().reduced (8.0f);
    const int kw = 82, kh = 34;
    auto row = getLocalBounds().reduced (18).removeFromBottom (kh);
    for (int i = 2; i >= 0; --i)
        viewKeys[i]->setBounds (row.removeFromRight (kw));
    fitFrame();
    frameScale = frameScaleTarget;
    frameOffset = frameOffsetTarget;
}

void RoomView::setView (int v)
{
    view = v;
    static const Camera presets[] { { 0.0f, 0.42f }, { 0.0f, 1.5698f }, { -juce::MathConstants<float>::halfPi, 0.16f } };
    camTarget = presets[juce::jlimit (0, 2, v)];
    for (int i = 0; i < 3; ++i)
        viewKeys[i]->setLit (i == v);
}

//==============================================================================
float RoomView::unit() const noexcept { return 1.0f / std::max (0.5f * room.dims.x, 0.5f * room.dims.y); }

void RoomView::cameraBasis (Vec3& pos, Vec3& fwd, Vec3& right, Vec3& up) const noexcept
{
    const float cp = std::cos (cam.pitch), sp = std::sin (cam.pitch);
    fwd = { std::sin (cam.yaw) * cp, std::cos (cam.yaw) * cp, -sp };
    right = { std::cos (cam.yaw), -std::sin (cam.yaw), 0.0f };
    up = cross (right, fwd);
    const Vec3 target { 0.0f, 0.0f, room.dims.z * unit() * 0.35f };
    pos = target - fwd * kCamDist;
}

RoomView::Projected RoomView::project (Vec3 d) const noexcept
{
    Vec3 pos, fwd, right, up;
    cameraBasis (pos, fwd, right, up);
    const auto v = d - pos;
    const float z = std::max (0.05f, v.dot (fwd));
    Projected p;
    p.depth = z;
    p.scale = frameScale / z;
    p.p = { frameOffset.x + p.scale * v.dot (right), frameOffset.y - p.scale * v.dot (up) };
    return p;
}

bool RoomView::rayToPlaneZ (juce::Point<float> s, float zPlane, Vec3& hit) const noexcept
{
    Vec3 pos, fwd, right, up;
    cameraBasis (pos, fwd, right, up);
    const auto dir = fwd + right * ((s.x - frameOffset.x) / frameScale) + up * ((frameOffset.y - s.y) / frameScale);
    if (std::abs (dir.z) < 1.0e-5f)
        return false;
    const float t = (zPlane - pos.z) / dir.z;
    if (t <= 0.0f)
        return false;
    hit = pos + dir * t;
    return true;
}

bool RoomView::rayToPlaneX (juce::Point<float> s, float xPlane, Vec3& hit) const noexcept
{
    Vec3 pos, fwd, right, up;
    cameraBasis (pos, fwd, right, up);
    const auto dir = fwd + right * ((s.x - frameOffset.x) / frameScale) + up * ((frameOffset.y - s.y) / frameScale);
    if (std::abs (dir.x) < 1.0e-5f)
        return false;
    const float t = (xPlane - pos.x) / dir.x;
    if (t <= 0.0f)
        return false;
    hit = pos + dir * t;
    return true;
}

void RoomView::fitFrame()
{
    if (stage.isEmpty())
        return;
    // Frame the room's eight corners in the window, leaving room for the readouts.
    const float u = unit();
    const float hx = 0.5f * room.dims.x * u, hy = 0.5f * room.dims.y * u, hz = room.dims.z * u;
    Vec3 pos, fwd, right, up;
    cameraBasis (pos, fwd, right, up);
    float x0 = 1.0e9f, x1 = -1.0e9f, y0 = 1.0e9f, y1 = -1.0e9f;
    for (int i = 0; i < 8; ++i)
    {
        const Vec3 c { (i & 1) ? hx : -hx, (i & 2) ? hy : -hy, (i & 4) ? hz : 0.0f };
        const auto v = c - pos;
        const float z = std::max (0.05f, v.dot (fwd));
        const float x = v.dot (right) / z, y = v.dot (up) / z;
        x0 = std::min (x0, x); x1 = std::max (x1, x);
        y0 = std::min (y0, y); y1 = std::max (y1, y);
    }
    const auto area = stage.reduced (30.0f).withTrimmedTop (44.0f).withTrimmedBottom (40.0f);
    frameScaleTarget = std::min (area.getWidth() / std::max (0.01f, x1 - x0), area.getHeight() / std::max (0.01f, y1 - y0));
    frameOffsetTarget = { area.getCentreX() - frameScaleTarget * 0.5f * (x0 + x1),
                          area.getCentreY() + frameScaleTarget * 0.5f * (y0 + y1) };
}

//==============================================================================
void RoomView::timerCallback()
{
    // The room glides to its new shape; the camera eases to its view.
    const auto target = orbit::RoomState::make ((int) std::lround (pRoom->convertFrom0to1 (pRoom->getValue())),
                                                pSize->convertFrom0to1 (pSize->getValue()));
    auto glide = [] (float& v, float t, float k) { v += (t - v) * k; };
    glide (room.dims.x, target.dims.x, 0.16f); glide (room.dims.y, target.dims.y, 0.16f); glide (room.dims.z, target.dims.z, 0.16f);
    glide (room.rt60, target.rt60, 0.16f);
    glide (room.wallR, target.wallR, 0.16f); glide (room.floorR, target.floorR, 0.16f); glide (room.ceilR, target.ceilR, 0.16f);
    if (drag != Drag::Orbit)
    {
        glide (cam.yaw, camTarget.yaw, 0.12f);
        glide (cam.pitch, camTarget.pitch, 0.12f);
    }
    fitFrame();
    glide (frameScale, frameScaleTarget, 0.18f);
    glide (frameOffset.x, frameOffsetTarget.x, 0.18f);
    glide (frameOffset.y, frameOffsetTarget.y, 0.18f);

    // Where the sound is now: the placed position plus motion at the engine's phase (follows a drag even
    // when no audio is running).
    const Vec3 placed { pX->convertFrom0to1 (pX->getValue()), pY->convertFrom0to1 (pY->getValue()), pZ->convertFrom0to1 (pZ->getValue()) };
    const int mode = (int) std::lround (pMotion->convertFrom0to1 (pMotion->getValue()));
    const auto moved = orbit::movedPosition (placed, mode, proc.telemetry.phase.load(), pAmount->convertFrom0to1 (pAmount->getValue()));
    const float kPos = drag == Drag::None ? 0.35f : 0.6f;
    glide (heard.x, moved.x, kPos); glide (heard.y, moved.y, kPos); glide (heard.z, moved.z, kPos);

    const float newLevel = levelToUnit (proc.telemetry.input.load());
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (newLevel > levelPeak + 0.07f && newLevel > 0.25f && now - lastRing > 0.11)
    {
        rings.push_back ({ room.toMetres (heard), now, newLevel });
        lastRing = now;
        if (rings.size() > 10)
            rings.erase (rings.begin());
    }
    levelPeak = std::max (newLevel, levelPeak - 0.012f);
    level += (newLevel - level) * (newLevel > level ? 0.5f : 0.12f);
    late += (juce::jlimit (0.0f, 1.0f, proc.telemetry.late.load() * 3.0f) - late) * 0.1f;
    rings.erase (std::remove_if (rings.begin(), rings.end(), [now] (const Ring& r) { return now - r.born > 1.4; }), rings.end());

    trail.push_back (room.toMetres (heard));
    if (trail.size() > 80)
        trail.erase (trail.begin());
    repaint();
}

//==============================================================================
juce::Path RoomView::polyline (const std::vector<Vec3>& pts) const
{
    juce::Path path;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const auto p = project (pts[i]).p;
        if (i == 0)
            path.startNewSubPath (p);
        else
            path.lineTo (p);
    }
    return path;
}

void RoomView::paint (juce::Graphics& g)
{
    dd::gui::paintBezel (g, stage, 10.0f, 6.0f);
    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path window;
        window.addRoundedRectangle (stage, 10.0f);
        g.reduceClipRegion (window);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17120f), stage.getCentreX(), stage.getY() + stage.getHeight() * 0.35f,
                                                 kNight, stage.getX(), stage.getBottom(), true));
        g.fillRect (stage);

        const auto srcM = room.toMetres (heard);
        paintRoom (g);
        paintRays (g, srcM, level);
        paintPath (g);
        paintWidth (g, srcM);
        paintRings (g);
        paintTrail (g);

        // Head and lamp, farther one first.
        const float headDepth = project (toDisplay (room.listener())).depth;
        const float srcDepth = project (toDisplay (srcM)).depth;
        if (srcDepth > headDepth)
        {
            paintSource (g, srcM, level);
            paintListener (g);
        }
        else
        {
            paintListener (g);
            paintSource (g, srcM, level);
        }

        // The near walls: glass in front of everything.
        {
            const float u = unit();
            const float hx = 0.5f * room.dims.x * u, hy = 0.5f * room.dims.y * u, hz = room.dims.z * u;
            Vec3 pos, fwd, right, up;
            cameraBasis (pos, fwd, right, up);
            struct Wall { Vec3 a, b, c, d, inward; };
            const Wall walls[] {
                { { -hx, -hy, 0 }, { hx, -hy, 0 }, { hx, -hy, hz }, { -hx, -hy, hz }, { 0, 1, 0 } },
                { { -hx, hy, 0 }, { hx, hy, 0 }, { hx, hy, hz }, { -hx, hy, hz }, { 0, -1, 0 } },
                { { -hx, -hy, 0 }, { -hx, hy, 0 }, { -hx, hy, hz }, { -hx, -hy, hz }, { 1, 0, 0 } },
                { { hx, -hy, 0 }, { hx, hy, 0 }, { hx, hy, hz }, { hx, -hy, hz }, { -1, 0, 0 } },
            };
            for (auto& w : walls)
            {
                if ((pos - w.a).dot (w.inward) >= 0.0f)
                    continue;   // far wall: painted with the room
                juce::Path p;
                p.startNewSubPath (project (w.a).p); p.lineTo (project (w.b).p); p.lineTo (project (w.c).p); p.lineTo (project (w.d).p);
                p.closeSubPath();
                g.setColour (kGlass.withAlpha (0.025f + 0.05f * late));
                g.fillPath (p);
                g.setColour (kCream.withAlpha (0.10f));
                g.strokePath (p, juce::PathStrokeType (1.0f));
            }
        }

        paintReadouts (g, srcM);
        dd::gui::paintGrain (g, stage.toNearestInt(), 0.15f);
    }
    dd::gui::paintGlassReflection (g, stage, 10.0f);
}

void RoomView::paintRoom (juce::Graphics& g)
{
    const float u = unit();
    const float hx = 0.5f * room.dims.x * u, hy = 0.5f * room.dims.y * u, hz = room.dims.z * u;
    Vec3 pos, fwd, right, up;
    cameraBasis (pos, fwd, right, up);

    // Floor.
    juce::Path floor;
    floor.startNewSubPath (project ({ -hx, -hy, 0 }).p);
    floor.lineTo (project ({ hx, -hy, 0 }).p);
    floor.lineTo (project ({ hx, hy, 0 }).p);
    floor.lineTo (project ({ -hx, hy, 0 }).p);
    floor.closeSubPath();
    const auto nearP = project ({ 0, -hy, 0 }).p, farP = project ({ 0, hy, 0 }).p;
    g.setGradientFill (juce::ColourGradient (kFloorNear, nearP, kFloorFar, farP, false));
    g.fillPath (floor);

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (floor);
        // The lamp lights the floor under it: brighter the lower and louder it hangs.
        const auto srcD = toDisplay (room.toMetres (heard));
        const auto under = project ({ srcD.x, srcD.y, 0.0f });
        const float heightK = 1.0f / (1.0f + 2.5f * srcD.z);
        const float radius = under.scale * (0.45f + 0.5f * srcD.z);
        g.setGradientFill (juce::ColourGradient (sunset::orange.withAlpha ((0.10f + 0.22f * level) * heightK + 0.04f), under.p,
                                                 sunset::red.withAlpha (0.0f), under.p.translated (radius, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f * 0.75f).withCentre (under.p));

        // Grid: eight squares each way, the listener's cross a little brighter.
        for (int i = 0; i <= 8; ++i)
        {
            const float fx = -hx + 2.0f * hx * (float) i / 8.0f, fy = -hy + 2.0f * hy * (float) i / 8.0f;
            const float a = i == 4 ? 0.16f : 0.075f;
            g.setColour (kCream.withAlpha (a));
            g.drawLine (juce::Line<float> (project ({ fx, -hy, 0 }).p, project ({ fx, hy, 0 }).p), i == 4 ? 1.0f : 0.8f);
            g.drawLine (juce::Line<float> (project ({ -hx, fy, 0 }).p, project ({ hx, fy, 0 }).p), i == 4 ? 1.0f : 0.8f);
        }

        // The lamp's reflection in the polished floor.
        {
            const auto mirror = project ({ srcD.x, srcD.y, -srcD.z });
            const float mr = mirror.scale * kOrbRadius * 2.2f;
            g.setGradientFill (juce::ColourGradient (sunset::orange.withAlpha (0.16f + 0.2f * level), mirror.p,
                                                     sunset::red.withAlpha (0.0f), mirror.p.translated (mr, 0.0f), true));
            g.fillEllipse (juce::Rectangle<float> (mr * 2.0f, mr * 2.0f).withCentre (mirror.p));
        }

        // Distance rings around the listener, labelled in metres.
        const float maxR = std::min (0.5f * room.dims.x, 0.5f * room.dims.y);
        static const float radii[] { 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f };
        int drawn = 0;
        for (float rm : radii)
        {
            if (rm > maxR * 0.97f || drawn >= 4)
                continue;
            if (rm < maxR * 0.12f)
                continue;
            ++drawn;
            std::vector<Vec3> pts;
            for (int k = 0; k <= 96; ++k)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) k / 96.0f;
                pts.push_back ({ rm * u * std::sin (a), rm * u * std::cos (a), 0.0f });
            }
            const float dash[] { 3.0f, 5.0f };
            juce::Path dashed;
            juce::PathStrokeType (0.9f).createDashedStroke (dashed, polyline (pts), dash, 2);
            g.setColour (kCream.withAlpha (0.22f));
            g.fillPath (dashed);
            const auto lp = project ({ 0.0f, rm * u, 0.0f }).p;
            g.setFont (dd::gui::LookAndFeel::font (10.0f, 600));
            g.setColour (kCreamDim.withAlpha (0.7f));
            g.drawText (juce::String (rm, rm < 1.0f ? 1 : 0) + " m", juce::Rectangle<float> (40.0f, 12.0f).withCentre (lp.translated (0.0f, -7.0f)),
                        juce::Justification::centred, false);
        }
    }

    // Far walls: smoked glass that warms while the room rings.
    struct Wall { Vec3 a, b, c, d, inward; };
    Wall walls[] {
        { { -hx, -hy, 0 }, { hx, -hy, 0 }, { hx, -hy, hz }, { -hx, -hy, hz }, { 0, 1, 0 } },
        { { -hx, hy, 0 }, { hx, hy, 0 }, { hx, hy, hz }, { -hx, hy, hz }, { 0, -1, 0 } },
        { { -hx, -hy, 0 }, { -hx, hy, 0 }, { -hx, hy, hz }, { -hx, -hy, hz }, { 1, 0, 0 } },
        { { hx, -hy, 0 }, { hx, hy, 0 }, { hx, hy, hz }, { hx, -hy, hz }, { -1, 0, 0 } },
    };
    const bool open = room.wallR < 0.05f;
    for (auto& w : walls)
    {
        if ((pos - w.a).dot (w.inward) < 0.0f)
            continue;   // near wall: painted last, in front
        juce::Path p;
        p.startNewSubPath (project (w.a).p); p.lineTo (project (w.b).p); p.lineTo (project (w.c).p); p.lineTo (project (w.d).p);
        p.closeSubPath();
        const auto bottom = project ((w.a + w.b) * 0.5f).p, top = project ((w.c + w.d) * 0.5f).p;
        if (! open)
        {
            g.setGradientFill (juce::ColourGradient (kGlass.withAlpha (0.12f).interpolatedWith (sunset::orange.withAlpha (0.2f), late), bottom,
                                                     kGlass.withAlpha (0.03f), top, false));
            g.fillPath (p);
            // The lamp lights the wall nearest to it.
            const auto srcD = toDisplay (room.toMetres (heard));
            Vec3 nearest = srcD - w.inward * (srcD - w.a).dot (w.inward);
            nearest.x = juce::jlimit (-hx, hx, nearest.x);
            nearest.y = juce::jlimit (-hy, hy, nearest.y);
            nearest.z = juce::jlimit (0.0f, hz, nearest.z);
            const float dist = (srcD - nearest).length();
            const auto np = project (nearest);
            const float glow = (0.10f + 0.25f * level) / (1.0f + 6.0f * dist * dist);
            const float gr = np.scale * (0.25f + 0.6f * dist);
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (p);
            g.setGradientFill (juce::ColourGradient (sunset::orange.withAlpha (glow), np.p, sunset::red.withAlpha (0.0f), np.p.translated (gr, 0.0f), true));
            g.fillEllipse (juce::Rectangle<float> (gr * 2.0f, gr * 2.0f).withCentre (np.p));
        }
        g.setColour (kCream.withAlpha (open ? 0.05f : 0.16f + 0.1f * late));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }
    // The floor's edge catches the light; the ceiling is a faint outline.
    g.setColour (kCream.withAlpha (0.28f));
    g.strokePath (floor, juce::PathStrokeType (1.3f));
    if (room.ceilR > 0.05f)
    {
        juce::Path ceil;
        ceil.startNewSubPath (project ({ -hx, -hy, hz }).p);
        ceil.lineTo (project ({ hx, -hy, hz }).p);
        ceil.lineTo (project ({ hx, hy, hz }).p);
        ceil.lineTo (project ({ -hx, hy, hz }).p);
        ceil.closeSubPath();
        const float dash[] { 6.0f, 6.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, ceil, dash, 2);
        g.setColour (kCream.withAlpha (0.12f));
        g.fillPath (dashed);
    }
}

void RoomView::paintRays (juce::Graphics& g, Vec3 s, float lvl)
{
    // First reflections: the sound bounces once off each surface on its way to the listener.
    const auto L = room.listener();
    const float W = room.dims.x, Ln = room.dims.y, H = room.dims.z;
    struct Plane { int axis; float at; float reflect; };
    const Plane planes[] { { 0, -0.5f * W, room.wallR }, { 0, 0.5f * W, room.wallR }, { 1, -0.5f * Ln, room.wallR },
                           { 1, 0.5f * Ln, room.wallR }, { 2, 0.0f, room.floorR }, { 2, H, room.ceilR } };
    auto coord = [] (Vec3 v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; };
    const auto srcP = project (toDisplay (s)).p, earP = project (toDisplay (L)).p;
    for (auto& pl : planes)
    {
        if (pl.reflect < 0.05f)
            continue;
        Vec3 img = s;
        const float mirrored = 2.0f * pl.at - coord (s, pl.axis);
        (pl.axis == 0 ? img.x : pl.axis == 1 ? img.y : img.z) = mirrored;
        const float denom = coord (img, pl.axis) - coord (L, pl.axis);
        if (std::abs (denom) < 1.0e-4f)
            continue;
        const float t = (pl.at - coord (L, pl.axis)) / denom;
        if (t <= 0.0f || t >= 1.0f)
            continue;
        const auto hit = L + (img - L) * t;
        const auto hp = project (toDisplay (hit)).p;
        const float a = (0.05f + 0.35f * lvl) * pl.reflect;
        juce::Path ray;
        ray.startNewSubPath (srcP);
        ray.lineTo (hp);
        ray.lineTo (earP);
        g.setColour (sunset::orange.withAlpha (a * 0.55f));
        g.strokePath (ray, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float spot = 5.0f + 9.0f * lvl;
        g.setGradientFill (juce::ColourGradient (sunset::amber.withAlpha (std::min (1.0f, a * 1.6f)), hp,
                                                 sunset::red.withAlpha (0.0f), hp.translated (spot, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (spot * 2.0f, spot * 2.0f).withCentre (hp));
    }
}

void RoomView::paintWidth (juce::Graphics& g, Vec3 s)
{
    // Width: the arc of light the sound spans, as seen from the listener's head.
    const float width = pWidth->convertFrom0to1 (pWidth->getValue());
    const auto L = room.listener();
    const auto c = orbit::toPolar (s - L);
    const float spread = width * juce::MathConstants<float>::halfPi;
    if (spread < 0.01f)
        return;
    std::vector<Vec3> pts;
    const int steps = 48;
    auto at = [&] (float az)
    {
        auto p = L + orbit::fromPolar ({ az, c.el, c.r });
        p.x = juce::jlimit (-0.5f * room.dims.x, 0.5f * room.dims.x, p.x);
        p.y = juce::jlimit (-0.5f * room.dims.y, 0.5f * room.dims.y, p.y);
        p.z = juce::jlimit (0.0f, room.dims.z, p.z);
        return toDisplay (p);
    };
    for (int i = 0; i <= steps; ++i)
        pts.push_back (at (c.az - spread + 2.0f * spread * (float) i / (float) steps));
    const auto path = polyline (pts);
    const float sc = project (toDisplay (s)).scale;
    g.setColour (sunset::orange.withAlpha (0.10f + 0.10f * level));
    g.strokePath (path, juce::PathStrokeType (std::max (4.0f, sc * kOrbRadius * 2.4f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (sunset::amber.withAlpha (0.45f));
    g.strokePath (path, juce::PathStrokeType (1.4f));

    // The two sides (left and right of the input) as small lamps at the ends.
    for (int side = 0; side < 2; ++side)
    {
        const auto e = project (pts[side == 0 ? 0 : pts.size() - 1]);
        const float r = std::max (3.0f, e.scale * kOrbRadius * 0.55f);
        g.setGradientFill (juce::ColourGradient (sunset::yellow.withAlpha (0.9f), e.p, sunset::red.withAlpha (0.0f), e.p.translated (r * 2.2f, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (r * 4.4f, r * 4.4f).withCentre (e.p));
        g.setFont (dd::gui::LookAndFeel::font (9.5f, 700));
        g.setColour (kCream.withAlpha (0.75f));
        g.drawText (side == 0 ? "L" : "R", juce::Rectangle<float> (14.0f, 12.0f).withCentre (e.p.translated (0.0f, -r - 9.0f)),
                    juce::Justification::centred, false);
    }
}

void RoomView::paintPath (juce::Graphics& g)
{
    const int mode = (int) std::lround (pMotion->convertFrom0to1 (pMotion->getValue()));
    if (mode == 0)
        return;
    const Vec3 placed { pX->convertFrom0to1 (pX->getValue()), pY->convertFrom0to1 (pY->getValue()), pZ->convertFrom0to1 (pZ->getValue()) };
    const float amount = pAmount->convertFrom0to1 (pAmount->getValue());
    const double cycles = mode == 6 ? 4.0 : mode == 7 ? 3.0 : 1.0;
    std::vector<Vec3> pts;
    const int steps = (int) (160 * cycles);
    for (int i = 0; i <= steps; ++i)
        pts.push_back (toDisplay (room.toMetres (orbit::movedPosition (placed, mode, cycles * i / steps, amount))));
    const float dash[] { 2.0f, 5.0f };
    juce::Path dashed;
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, polyline (pts), dash, 2);
    g.setColour (kCream.withAlpha (0.32f));
    g.fillPath (dashed);

    // Where it was put: the centre of its travels.
    const auto anchor = project (toDisplay (room.toMetres (placed)));
    const float r = std::max (3.0f, anchor.scale * kOrbRadius * 0.6f);
    g.setColour (kCream.withAlpha (0.4f));
    g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (anchor.p), 1.0f);
}

void RoomView::paintTrail (juce::Graphics& g)
{
    if (trail.size() < 2)
        return;
    for (size_t i = 1; i < trail.size(); ++i)
    {
        const float a = (float) i / (float) trail.size();
        const auto p0 = project (toDisplay (trail[i - 1])), p1 = project (toDisplay (trail[i]));
        if (p0.p.getDistanceFrom (p1.p) < 0.2f)
            continue;
        g.setColour (sunset::orange.withAlpha (0.45f * a * a));
        g.drawLine (juce::Line<float> (p0.p, p1.p), 1.0f + p1.scale * kOrbRadius * 0.5f * a);
    }
}

void RoomView::paintRings (juce::Graphics& g)
{
    // Every hit sends a ring of light out through the room at the sound's height.
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float u = unit();
    const float hx = 0.5f * room.dims.x * u, hy = 0.5f * room.dims.y * u;
    for (auto& ring : rings)
    {
        const float age = (float) (now - ring.born);
        const float radius = age * 1.35f;   // display units per second
        const float fade = juce::jlimit (0.0f, 1.0f, 1.0f - age / 1.4f);
        const auto c = toDisplay (ring.centre);
        juce::Path path;
        bool drawing = false;
        for (int k = 0; k <= 96; ++k)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) k / 96.0f;
            const Vec3 p { c.x + radius * std::sin (a), c.y + radius * std::cos (a), c.z };
            if (std::abs (p.x) > hx || std::abs (p.y) > hy)
            {
                drawing = false;
                continue;
            }
            const auto sp = project (p).p;
            if (! drawing)
                path.startNewSubPath (sp);
            else
                path.lineTo (sp);
            drawing = true;
        }
        g.setColour (sunset::amber.withAlpha (0.5f * ring.strength * fade * fade));
        g.strokePath (path, juce::PathStrokeType (1.2f + 1.5f * fade));
    }
}

void RoomView::paintListener (juce::Graphics& g)
{
    const auto earM = room.listener();
    const auto c = toDisplay (earM);
    const auto head = project (c);
    const float r = head.scale * kHeadRadius;

    // Shadow on the floor.
    const auto under = project ({ c.x, c.y, 0.0f });
    const float sr = under.scale * kHeadRadius * 1.5f;
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), under.p, juce::Colours::transparentBlack,
                                             under.p.translated (sr, 0.0f), true));
    g.fillEllipse (juce::Rectangle<float> (sr * 2.0f, sr * 1.3f).withCentre (under.p));
    g.setColour (kCream.withAlpha (0.15f));
    g.drawLine (juce::Line<float> (under.p, head.p), 1.0f);

    // Light falls on the head from the sound: the side facing the lamp is lit.
    const auto lampP = project (toDisplay (room.toMetres (heard))).p;
    auto toLamp = lampP - head.p;
    const float len = std::max (1.0f, toLamp.getDistanceFromOrigin());
    toLamp = toLamp / len;

    auto feature = [&] (Vec3 offset, float size, juce::Colour col)
    {
        const auto p = project (c + offset);
        const float fr = p.scale * size;
        g.setColour (col);
        g.fillEllipse (juce::Rectangle<float> (fr * 2.0f, fr * 2.0f).withCentre (p.p));
        return p.depth;
    };
    const bool headphones = (int) std::lround (pOutput->convertFrom0to1 (pOutput->getValue())) == 0;
    const Vec3 nose { 0.0f, kHeadRadius * 1.05f, 0.0f }, earL { -kHeadRadius * 1.02f, 0.0f, 0.0f }, earR { kHeadRadius * 1.02f, 0.0f, 0.0f };
    const juce::Colour skin (0xff3a322b), cup (0xff1c1916);
    // Parts behind the head first.
    auto behind = [&] (Vec3 o) { return project (c + o).depth > head.depth; };
    if (behind (nose)) feature (nose, kHeadRadius * 0.28f, skin);
    if (behind (earL)) feature (earL, kHeadRadius * (headphones ? 0.48f : 0.3f), headphones ? cup : skin);
    if (behind (earR)) feature (earR, kHeadRadius * (headphones ? 0.48f : 0.3f), headphones ? cup : skin);

    juce::ColourGradient shade (juce::Colour (0xff5a4c40), head.p + toLamp * r * 0.55f, juce::Colour (0xff120f0d), head.p - toLamp * r * 1.1f, true);
    g.setGradientFill (shade);
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (head.p));
    g.setColour (sunset::orange.withAlpha (0.25f + 0.35f * level));
    g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (head.p), 1.0f);

    if (! behind (nose)) feature (nose, kHeadRadius * 0.28f, juce::Colour (0xff4a3f35));
    if (! behind (earL)) feature (earL, kHeadRadius * (headphones ? 0.48f : 0.3f), headphones ? cup : skin);
    if (! behind (earR)) feature (earR, kHeadRadius * (headphones ? 0.48f : 0.3f), headphones ? cup : skin);
    if (headphones)
    {
        // Headband over the top, a lit LED on each cup.
        std::vector<Vec3> band;
        for (int k = 0; k <= 16; ++k)
        {
            const float a = juce::MathConstants<float>::pi * (float) k / 16.0f;
            band.push_back (c + Vec3 { -std::cos (a) * kHeadRadius * 1.08f, 0.0f, std::sin (a) * kHeadRadius * 1.12f });
        }
        g.setColour (juce::Colour (0xff2a2520));
        g.strokePath (polyline (band), juce::PathStrokeType (std::max (2.0f, r * 0.22f)));
        for (auto o : { earL, earR })
        {
            const auto p = project (c + o * 1.25f);
            g.setColour (sunset::amber.withAlpha (0.8f));
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (p.p));
        }
    }
    else
    {
        // Two monitors in front of the listener.
        for (int side = -1; side <= 1; side += 2)
        {
            const auto p = project (c + Vec3 { 0.22f * (float) side, 0.30f, -0.02f });
            const float w = p.scale * 0.05f, h = p.scale * 0.075f;
            auto box = juce::Rectangle<float> (w * 2.0f, h * 2.0f).withCentre (p.p);
            g.setColour (juce::Colour (0xff1a1714));
            g.fillRoundedRectangle (box, 2.0f);
            g.setColour (kCream.withAlpha (0.2f));
            g.drawRoundedRectangle (box, 2.0f, 0.8f);
            g.setColour (sunset::orange.withAlpha (0.25f + 0.5f * level));
            g.drawEllipse (juce::Rectangle<float> (w * 1.2f, w * 1.2f).withCentre (box.getCentre().translated (0.0f, h * 0.35f)), 1.0f);
        }
    }
}

void RoomView::paintSource (juce::Graphics& g, Vec3 s, float lvl)
{
    const auto d = toDisplay (s);
    const auto orb = project (d);
    const auto floorP = project ({ d.x, d.y, 0.0f });

    // Shadow and height stem.
    const float sr = floorP.scale * kOrbRadius * 1.3f;
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), floorP.p, juce::Colours::transparentBlack,
                                             floorP.p.translated (sr, 0.0f), true));
    g.fillEllipse (juce::Rectangle<float> (sr * 2.0f, sr * 1.2f).withCentre (floorP.p));
    {
        const float dash[] { 3.0f, 3.0f };
        juce::Path stem, dashed;
        stem.startNewSubPath (floorP.p);
        stem.lineTo (orb.p);
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, stem, dash, 2);
        g.setColour (kCream.withAlpha (0.35f));
        g.fillPath (dashed);
        g.setColour (kCream.withAlpha (0.5f));
        g.drawEllipse (juce::Rectangle<float> (6.0f, 3.5f).withCentre (floorP.p), 1.0f);
    }

    // The lamp: bloom, a hot core, a glint.
    const float r = orb.scale * kOrbRadius * (1.0f + 0.3f * lvl);
    const float bloom = r * (3.2f + 2.0f * lvl);
    g.setGradientFill (juce::ColourGradient (sunset::orange.withAlpha (0.30f + 0.35f * lvl), orb.p,
                                             sunset::red.withAlpha (0.0f), orb.p.translated (bloom, 0.0f), true));
    g.fillEllipse (juce::Rectangle<float> (bloom * 2.0f, bloom * 2.0f).withCentre (orb.p));
    juce::ColourGradient core (sunset::yellow.brighter (0.3f), orb.p.translated (-r * 0.25f, -r * 0.3f), sunset::red.darker (0.4f), orb.p.translated (r, r), true);
    core.addColour (0.55, sunset::orange);
    g.setGradientFill (core);
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (orb.p));
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillEllipse (juce::Rectangle<float> (r * 0.5f, r * 0.35f).withCentre (orb.p.translated (-r * 0.35f, -r * 0.45f)));
    if (overSource || drag == Drag::Place || drag == Drag::Height)
    {
        g.setColour (kCream.withAlpha (0.6f));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f + 8.0f, r * 2.0f + 8.0f).withCentre (orb.p), 1.0f);
    }
}

void RoomView::paintReadouts (juce::Graphics& g, Vec3 s)
{
    const auto area = stage.reduced (20.0f, 14.0f);
    const int roomIndex = (int) std::lround (pRoom->convertFrom0to1 (pRoom->getValue()));
    const auto target = orbit::RoomState::make (roomIndex, pSize->convertFrom0to1 (pSize->getValue()));

    g.setFont (dd::gui::LookAndFeel::display (22.0f));
    g.setColour (kCream);
    g.drawText (orbit::rooms()[(size_t) roomIndex].name, area.withHeight (26.0f), juce::Justification::topLeft, false);
    auto dims = juce::String (target.dims.x, 1) + " x " + juce::String (target.dims.y, 1) + " x " + juce::String (target.dims.z, 1) + " M";
    if (target.wallR > 0.05f)
        dims << "    ECHO " << juce::String (target.rt60, target.rt60 < 1.0f ? 2 : 1) << " S";
    g.setFont (dd::gui::LookAndFeel::font (10.5f, 700).withExtraKerningFactor (0.12f));
    g.setColour (kCreamDim);
    g.drawText (dims, area.withTrimmedTop (28.0f).withHeight (14.0f), juce::Justification::topLeft, false);

    // Where it is, from the listener's seat.
    const auto pol = orbit::toPolar (s - room.listener());
    const int az = juce::roundToInt (juce::radiansToDegrees (pol.az)), el = juce::roundToInt (juce::radiansToDegrees (pol.el));
    juce::String dir = std::abs (az) <= 3 ? "AHEAD" : std::abs (az) >= 177 ? "BEHIND"
                     : juce::String (std::abs (az)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")) + (az > 0 ? " RIGHT" : " LEFT");
    juce::String height = std::abs (el) <= 3 ? "EAR LEVEL" : juce::String (std::abs (el)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")) + (el > 0 ? " UP" : " DOWN");
    const auto line = dir + "     " + height + "     " + (pol.r < 10.0f ? juce::String (pol.r, 1) : juce::String (juce::roundToInt (pol.r))) + " M";
    dd::gui::drawGlowing (g, line, area.withHeight (20.0f), sunset::amber, dd::gui::LookAndFeel::font (13.0f, 700).withExtraKerningFactor (0.1f),
                          juce::Justification::topRight);
    const int mode = (int) std::lround (pMotion->convertFrom0to1 (pMotion->getValue()));
    if (mode > 0)
    {
        g.setFont (dd::gui::LookAndFeel::font (10.5f, 700).withExtraKerningFactor (0.12f));
        g.setColour (kCreamDim);
        g.drawText ("MOTION  " + orbit::motionNames()[mode].toUpperCase(), area.withTrimmedTop (24.0f).withHeight (14.0f), juce::Justification::topRight, false);
    }

    g.setFont (dd::gui::LookAndFeel::font (10.0f, 700).withExtraKerningFactor (0.12f));
    g.setColour (kCreamDim.withAlpha (0.75f));
    g.drawText ("DRAG THE LIGHT  /  SHIFT-DRAG OR SCROLL = HEIGHT  /  RIGHT-DRAG = TURN", area.withTrimmedTop (area.getHeight() - 24.0f),
                juce::Justification::bottomLeft, false);
}

//==============================================================================
juce::Point<float> RoomView::sourceScreen() const { return project (toDisplay (room.toMetres (heard))).p; }

void RoomView::setParam (juce::RangedAudioParameter* p, float value)
{
    p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (value)));
}

void RoomView::placeFromMouse (const juce::MouseEvent& e)
{
    const float m = std::min (0.1f, 0.2f * room.dims.z);
    const Vec3 placed { pX->convertFrom0to1 (pX->getValue()), pY->convertFrom0to1 (pY->getValue()), pZ->convertFrom0to1 (pZ->getValue()) };
    const auto placedD = toDisplay (room.toMetres (placed));
    Vec3 hit;
    if (view == 2)
    {
        // Side view: the mouse moves it front / back and up / down.
        if (! rayToPlaneX (e.position, placedD.x, hit))
            return;
        const auto hm = hit * (1.0f / unit());
        setParam (pY, hm.y / std::max (0.01f, 0.5f * room.dims.y - m));
        setParam (pZ, (hm.z - m) / std::max (0.01f, room.dims.z - 2.0f * m));
        return;
    }
    if (! rayToPlaneZ (e.position, placedD.z, hit))
        return;
    const auto hm = hit * (1.0f / unit());
    setParam (pX, hm.x / std::max (0.01f, 0.5f * room.dims.x - m));
    setParam (pY, hm.y / std::max (0.01f, 0.5f * room.dims.y - m));
}

void RoomView::mouseMove (const juce::MouseEvent& e)
{
    const auto sp = sourceScreen();
    const float r = std::max (12.0f, project (toDisplay (room.toMetres (heard))).scale * kOrbRadius * 1.6f);
    const bool over = e.position.getDistanceFrom (sp) < r;
    if (over != overSource)
    {
        overSource = over;
        setMouseCursor (over ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
    }
}

void RoomView::mouseDown (const juce::MouseEvent& e)
{
    dragStart = e.position;
    if (e.mods.isPopupMenu() || e.mods.isCtrlDown())
    {
        drag = Drag::Orbit;
        dragStartCam = cam;
        return;
    }
    if (e.mods.isShiftDown())
    {
        drag = Drag::Height;
        dragStartZ = pZ->convertFrom0to1 (pZ->getValue());
        pZ->beginChangeGesture();
        return;
    }
    drag = Drag::Place;
    pX->beginChangeGesture();
    pY->beginChangeGesture();
    if (view == 2)
        pZ->beginChangeGesture();
    if (! overSource)
        placeFromMouse (e);   // a click on the floor puts it there
}

void RoomView::mouseDrag (const juce::MouseEvent& e)
{
    switch (drag)
    {
        case Drag::Place:  placeFromMouse (e); break;
        case Drag::Height: setParam (pZ, dragStartZ - (e.position.y - dragStart.y) / 220.0f); break;
        case Drag::Orbit:
            cam.yaw = dragStartCam.yaw + (e.position.x - dragStart.x) * 0.008f;
            cam.pitch = juce::jlimit (0.05f, 1.5698f, dragStartCam.pitch + (e.position.y - dragStart.y) * 0.006f);
            camTarget = cam;
            for (auto* k : viewKeys)
                k->setLit (false);
            break;
        case Drag::None: break;
    }
}

void RoomView::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::Place)
    {
        pX->endChangeGesture();
        pY->endChangeGesture();
        if (view == 2)
            pZ->endChangeGesture();
    }
    else if (drag == Drag::Height)
        pZ->endChangeGesture();
    drag = Drag::None;
}

void RoomView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.position.getDistanceFrom (sourceScreen()) < 24.0f)
    {
        for (auto* p : { pX, pY, pZ })
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
        return;
    }
    setView (view);
}

void RoomView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float z = pZ->convertFrom0to1 (pZ->getValue());
    pZ->beginChangeGesture();
    setParam (pZ, z + w.deltaY * 0.12f);
    pZ->endChangeGesture();
}
