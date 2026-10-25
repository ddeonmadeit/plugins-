#include "KitView.h"

namespace
{
const juce::Colour kAccent = dd::gui::sunset::orange;   // selection, hit glow
const juce::Colour kKeyText = dd::gui::sunset::amber;
const juce::Colour kChrome { 0xffd9d0c4 };                // chrome in warm light
const juce::Colour kChromeDark { 0xff6b6158 };
const juce::Colour kHead { 0xffece2cc };                  // coated head
const juce::Colour kBronze { 0xffbf8f35 };
const juce::Colour kCream { 0xffeadcc3 };
constexpr float kFloorY = 396.0f;
constexpr float kPi = juce::MathConstants<float>::pi;

juce::Colour finishFromHex (const juce::String& hex, juce::Colour fallback)
{
    return hex.length() == 7 && hex.startsWithChar ('#') ? juce::Colour::fromString ("ff" + hex.substring (1)) : fallback;
}

/** Overhead spotlights, fixtures above the frame: apex, where the beam centre meets the floor, half-angle. */
struct Spot { float apexX, apexY, aimX, halfAngle, intensity; };
constexpr Spot kSpots[] = {
    { 127.0f, -220.0f, 127.0f, 0.17f, 0.8f },    // china
    { 300.0f, -220.0f, 300.0f, 0.25f, 1.0f },    // hi-hat, crash L, snare
    { 590.0f, -220.0f, 590.0f, 0.27f, 1.0f },    // rack toms and kick
    { 900.0f, -220.0f, 905.0f, 0.27f, 1.0f },    // crash R, floor tom
    { 1065.0f, -220.0f, 1065.0f, 0.16f, 0.7f },  // ride
};

float smoothstep (float e0, float e1, float x)
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

/** How much spotlight reaches a point, 0..1 (beams combine like light, not paint). */
float lightAt (float x, float y)
{
    float dark = 1.0f;
    for (auto& s : kSpots)
    {
        const float dx = s.aimX - s.apexX, dy = kFloorY - s.apexY;
        const float len = std::sqrt (dx * dx + dy * dy);
        const float ux = dx / len, uy = dy / len;
        const float vx = x - s.apexX, vy = y - s.apexY;
        const float along = vx * ux + vy * uy;
        if (along <= 0.0f)
            continue;
        const float t = std::atan2 (std::abs (vx * uy - vy * ux), along) / s.halfAngle;
        const float edge = smoothstep (1.0f, 0.72f, t);
        const float hot = 0.8f + 0.2f * (1.0f - std::min (1.0f, t * t));
        dark *= 1.0f - edge * hot * s.intensity;
    }
    return 1.0f - dark;
}

/** The beam of one spot between two heights, as a trapezoid. */
juce::Path beamPath (const Spot& s, float y0, float y1, float widthScale)
{
    auto halfWidthAt = [&] (float y) { return (y - s.apexY) * std::tan (s.halfAngle) * widthScale; };
    auto centreAt = [&] (float y) { return s.apexX + (s.aimX - s.apexX) * (y - s.apexY) / (kFloorY - s.apexY); };
    juce::Path p;
    p.startNewSubPath (centreAt (y0) - halfWidthAt (y0), y0);
    p.lineTo (centreAt (y0) + halfWidthAt (y0), y0);
    p.lineTo (centreAt (y1) + halfWidthAt (y1), y1);
    p.lineTo (centreAt (y1) - halfWidthAt (y1), y1);
    p.closeSubPath();
    return p;
}

void glowAround (juce::Graphics& g, const juce::Path& p, float glow)
{
    if (glow <= 0.01f)
        return;
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (kAccent.withAlpha (glow * 0.16f));
        g.strokePath (p, juce::PathStrokeType ((float) i * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
} // namespace

KitView::KitView (KitroomProcessor& p) : proc (p)
{
    using S = Shape;
    auto drum = [] (int slot, S shape, float x, float y, float w, float h, float shellH, juce::Point<float> label)
    {
        Piece pc { slot, shape, { x, y, w, h }, shellH, label, {}, {} };
        pc.hit.addEllipse (pc.box);
        pc.hit.addRectangle (x, y + h * 0.5f, w, shellH);
        pc.hit.addEllipse (x, y + shellH, w, h);
        // Top rim over the back, down one side, along the front of the bottom hoop, up the other side.
        const float cx = x + w * 0.5f, cy = y + h * 0.5f;
        pc.outline.startNewSubPath (x, cy);
        pc.outline.addCentredArc (cx, cy, w * 0.5f, h * 0.5f, 0.0f, -kPi * 0.5f, kPi * 0.5f);
        pc.outline.lineTo (x + w, cy + shellH);
        pc.outline.addCentredArc (cx, cy + shellH, w * 0.5f, h * 0.5f, 0.0f, kPi * 0.5f, kPi * 1.5f);
        pc.outline.closeSubPath();
        return pc;
    };
    auto cym = [] (int slot, S shape, float x, float y, float w, float h, juce::Point<float> label)
    {
        Piece pc { slot, shape, { x, y, w, h }, 0.0f, label, {}, {} };
        pc.hit.addEllipse (pc.box.expanded (6.0f, 8.0f));
        pc.outline.addEllipse (pc.box);
        return pc;
    };

    // Back to front.
    pieces.push_back (cym (kr::China, S::China, 40, 88, 175, 30, { 127, 66 }));
    pieces.push_back (cym (kr::Crash1, S::Crash, 215, 70, 235, 40, { 332, 48 }));
    pieces.push_back (cym (kr::Crash2, S::Crash, 735, 58, 245, 42, { 857, 36 }));
    pieces.push_back (cym (kr::RideSlot, S::Ride, 880, 150, 270, 46, { 1015, 136 }));
    pieces.push_back (drum (kr::Tom1, S::RackTom, 440, 122, 120, 34, 70, { 500, 102 }));
    pieces.push_back (drum (kr::Tom2, S::RackTom, 612, 122, 130, 36, 76, { 677, 102 }));

    Piece kick { kr::Kick, S::Kick, { 588 - 102, 290 - 102, 204, 204 }, 0.0f, { 588, 407 }, {}, {} };
    kick.hit.addEllipse (kick.box);
    kick.outline = kick.hit;
    pieces.push_back (kick);

    pieces.push_back (drum (kr::Tom3, S::FloorTom, 790, 212, 170, 46, 118, { 875, 407 }));
    pieces.push_back (drum (kr::Snare, S::Snare, 300, 245, 150, 40, 50, { 375, 407 }));

    Piece hat = cym (kr::HiHat, S::HiHat, 140, 198, 175, 26, { 227, 176 });
    hat.hit.addEllipse (hat.box.translated (0, 10).expanded (6, 8));
    hat.outline.addEllipse (hat.box.translated (0, 10));
    pieces.push_back (hat);

    startTimerHz (30);
}

juce::Colour KitView::finishFor (int slot) const
{
    const auto id = proc.getSlotSound (slot);
    const int idx = proc.library.indexOf (id);
    if (idx < 0)
        return juce::Colour (0xff3a2a22);
    const auto& kit = proc.library.getSounds()[(size_t) idx].kit;
    for (auto& k : proc.library.getKits())
        if (k.id == kit)
            return finishFromHex (k.finish, juce::Colour (0xff6e1420));
    return juce::Colour (0xff6e1420);
}

void KitView::timerCallback()
{
    bool any = false;
    for (int s = 0; s < kr::kNumSlots; ++s)
    {
        flash[(size_t) s] = std::max (flash[(size_t) s] * 0.8f, proc.takeHit (s));
        if (flash[(size_t) s] < 0.01f) flash[(size_t) s] = 0.0f;
        any = any || flash[(size_t) s] > 0.0f || proc.isSlotLoading (s);
    }
    if (any || proc.getLearnTarget() >= 0)
        repaint();
}

int KitView::pieceAt (juce::Point<float> p) const
{
    for (auto it = pieces.rbegin(); it != pieces.rend(); ++it)
        if (it->hit.contains (p))
            return it->slot;
    return -1;
}

void KitView::mouseMove (const juce::MouseEvent& e)
{
    const int h = pieceAt (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void KitView::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void KitView::mouseDown (const juce::MouseEvent& e)
{
    const int s = pieceAt (e.position);
    if (s < 0)
        return;
    proc.selectedSlot = s;
    // Shift-click plays the second articulation (rimshot, open hat, ride bell).
    const int art = e.mods.isShiftDown() && kr::slots()[(size_t) s].articulations.size() > 1 ? (s == kr::HiHat ? 2 : 1) : 0;
    proc.audition (s, art, 0.85f);
    if (onSelect)
        onSelect (s);
    repaint();
}

//==============================================================================
void KitView::drawRoom (juce::Graphics& g) const
{
    const auto r = getLocalBounds().toFloat();
    const float wallBottom = kFloorY - 30.0f;

    // Back wall: walnut slats, each its own shade, with faint grain.
    juce::Random rnd (1977);
    constexpr float slatW = 34.0f;
    for (float x = 0.0f; x < r.getRight(); x += slatW)
    {
        const auto shade = juce::Colour (0xff1e130b).interpolatedWith (juce::Colour (0xff130b06), rnd.nextFloat());
        g.setColour (shade);
        g.fillRect (x, 0.0f, slatW, wallBottom);
        for (int k = 0; k < 4; ++k)
        {
            juce::Path grainLine;
            const float gx = x + 4.0f + rnd.nextFloat() * (slatW - 8.0f), wob = 1.0f + rnd.nextFloat() * 2.5f, ph = rnd.nextFloat() * 6.0f;
            grainLine.startNewSubPath (gx, 0.0f);
            for (float y = 20.0f; y <= wallBottom; y += 20.0f)
                grainLine.lineTo (gx + std::sin (y * 0.013f + ph) * wob, y);
            g.setColour ((k % 2 ? juce::Colours::black : juce::Colour (0xffd8b48a)).withAlpha (0.05f));
            g.strokePath (grainLine, juce::PathStrokeType (1.0f));
        }
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (x, 0.0f, 1.5f, wallBottom);
        g.setColour (juce::Colour (0xffd8b48a).withAlpha (0.04f));
        g.fillRect (x + 1.5f, 0.0f, 1.0f, wallBottom);
    }

    // Floor: dark boards.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c110a), 0.0f, wallBottom, juce::Colour (0xff0b0604), 0.0f, r.getBottom(), false));
    g.fillRect (r.withTop (wallBottom));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (r.getX(), wallBottom, r.getWidth(), 2.0f);
    for (float y = wallBottom + 9.0f; y < r.getBottom(); y += 9.0f + (y - wallBottom) * 0.15f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
    }

    // Rug: deep red with a worn amber border.
    const auto rug = juce::Rectangle<float> (r.getCentreX() - 540, kFloorY - 40, 1080, 84);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5e1d12), r.getCentreX(), kFloorY - 10,
                                             juce::Colour (0xff230b07), r.getCentreX() + 520, kFloorY + 30, true));
    g.fillEllipse (rug);
    g.setColour (dd::gui::sunset::amber.withAlpha (0.18f));
    g.drawEllipse (rug.reduced (22.0f, 6.0f), 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (rug.reduced (30.0f, 8.0f), 1.0f);

    // Light from straight above: each piece throws a soft shadow right under itself.
    const std::array<juce::Rectangle<float>, 6> shadows { {
        { 588 - 130, kFloorY - 12, 260, 26 }, { 875 - 115, kFloorY - 10, 230, 22 }, { 375 - 70, kFloorY - 8, 140, 16 },
        { 227 - 60, kFloorY - 7, 120, 14 }, { 1015 - 50, kFloorY - 6, 100, 12 }, { 332 - 45, kFloorY - 6, 90, 12 } } };
    for (auto& sh : shadows)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.7f), sh.getCentreX(), sh.getCentreY(),
                                                 juce::Colours::transparentBlack, sh.getRight(), sh.getCentreY(), true));
        g.fillEllipse (sh);
    }
}

void KitView::drawSpotlights (juce::Graphics& g)
{
    // Everything outside the beams falls into darkness. The mask never changes, so it is computed once.
    if (! darkness.isValid())
    {
        darkness = juce::Image (juce::Image::ARGB, kW, kH, true, juce::SoftwareImageType());
        juce::Image::BitmapData bd (darkness, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                bd.setPixelColour (x, y, juce::Colours::black.withAlpha (0.95f * (1.0f - lightAt ((float) x, (float) y))));
    }
    g.drawImageAt (darkness, 0, 0);

    const auto warm = dd::gui::sunset::amber.interpolatedWith (juce::Colour (0xfffff0d8), 0.45f);   // tungsten
    for (auto& s : kSpots)
    {
        // Haze: nested beams, so the edges fade instead of cutting off.
        for (float w : { 0.95f, 0.8f, 0.62f, 0.4f })
        {
            const auto beam = beamPath (s, 0.0f, kFloorY + 20.0f, w);
            g.setGradientFill (juce::ColourGradient (warm.withAlpha (0.065f * s.intensity), 0.0f, 0.0f,
                                                     warm.withAlpha (0.015f * s.intensity), 0.0f, kFloorY, false));
            g.fillPath (beam);
        }

        // Pool of light where the beam lands.
        const float poolHalf = (kFloorY - s.apexY) * std::tan (s.halfAngle) * 0.9f;
        const juce::Rectangle<float> pool (s.aimX - poolHalf, kFloorY - 26.0f, poolHalf * 2.0f, 52.0f);
        g.setGradientFill (juce::ColourGradient (warm.withAlpha (0.16f * s.intensity), pool.getCentreX(), pool.getCentreY(),
                                                 warm.withAlpha (0.0f), pool.getRight(), pool.getCentreY(), true));
        g.fillEllipse (pool);

        // Dust hanging in the beam.
        juce::Random rnd ((juce::int64) (s.apexX * 13.0f));
        for (int i = 0; i < 46; ++i)
        {
            const float y = rnd.nextFloat() * kFloorY;
            const float half = (y - s.apexY) * std::tan (s.halfAngle) * 0.75f;
            const float cx = s.apexX + (s.aimX - s.apexX) * (y - s.apexY) / (kFloorY - s.apexY);
            const float x = cx + (rnd.nextFloat() * 2.0f - 1.0f) * half;
            const float d = 0.8f + rnd.nextFloat() * 1.2f;
            g.setColour (warm.withAlpha (0.12f + 0.28f * rnd.nextFloat() * lightAt (x, y)));
            g.fillEllipse (x, y, d, d);
        }
    }
}

void KitView::drawStand (juce::Graphics& g, float x, float top, float bottom, bool tripod) const
{
    g.setColour (kChromeDark);
    g.drawLine (x, top, x, bottom, 3.0f);
    g.setColour (kChrome.withAlpha (0.55f));
    g.drawLine (x - 0.8f, top, x - 0.8f, bottom, 1.0f);
    if (tripod)
    {
        g.setColour (kChromeDark);
        const float knee = bottom - 34.0f;
        g.drawLine (x, knee, x - 30, bottom, 2.2f);
        g.drawLine (x, knee, x + 30, bottom, 2.2f);
        g.drawLine (x, knee, x + 4, bottom + 4, 2.2f);
    }
}

void KitView::drawCymbal (juce::Graphics& g, juce::Rectangle<float> r, float glow, bool china) const
{
    juce::Path outline;
    outline.addEllipse (r);
    glowAround (g, outline, glow);

    if (china)
    {
        g.setColour (kBronze.darker (0.9f));
        g.fillEllipse (r.expanded (5, 3));
    }

    const auto base = kBronze.interpolatedWith (kAccent.brighter (0.4f), glow * 0.5f);
    g.setGradientFill (juce::ColourGradient (base.brighter (0.55f), r.getX(), r.getY(),
                                             base.darker (0.75f), r.getRight(), r.getBottom(), false));
    g.fillEllipse (r);

    // Lathe grooves.
    for (int k = 1; k < 9; ++k)
    {
        const float f = (float) k / 9.0f;
        auto ring = r.withSizeKeepingCentre (r.getWidth() * f, r.getHeight() * f);
        g.setColour ((k % 2 ? juce::Colours::black : juce::Colours::white).withAlpha (0.07f));
        g.drawEllipse (ring, 0.8f);
    }

    // Sheen and bell.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.28f), r.getX() + r.getWidth() * 0.3f, r.getCentreY(),
                                             juce::Colours::white.withAlpha (0.0f), r.getX() + r.getWidth() * 0.6f, r.getCentreY(), true));
    g.fillEllipse (r.reduced (r.getWidth() * 0.1f, r.getHeight() * 0.15f));
    auto bell = r.withSizeKeepingCentre (r.getWidth() * (china ? 0.24f : 0.18f), r.getHeight() * 0.42f).translated (0, -r.getHeight() * 0.06f);
    g.setGradientFill (juce::ColourGradient (base.brighter (0.8f), bell.getX(), bell.getY(),
                                             base.darker (0.4f), bell.getRight(), bell.getBottom(), false));
    g.fillEllipse (bell);
    g.setColour (base.darker (1.2f).withAlpha (0.6f));
    g.drawEllipse (r, 1.0f);
}

void KitView::drawDrum (juce::Graphics& g, const Piece& p, float glow) const
{
    const auto b = p.box;
    const float x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight(), cx = b.getCentreX();
    const auto finish = finishFor (p.slot);

    if (p.shape == Shape::FloorTom)
    {
        g.setColour (kChromeDark);
        g.drawLine (x + 10, y + h * 0.5f + p.shellH * 0.3f, x - 8, kFloorY, 3.0f);
        g.drawLine (x + w - 10, y + h * 0.5f + p.shellH * 0.3f, x + w + 8, kFloorY, 3.0f);
    }
    if (p.shape == Shape::Snare)
        drawStand (g, cx, y + h + p.shellH, kFloorY, true);
    if (p.shape == Shape::RackTom)
    {
        g.setColour (kChromeDark);
        g.drawLine (cx, y + h + p.shellH * 0.6f, 588, 200, 4.0f);
    }

    glowAround (g, p.outline, glow);

    // Shell
    juce::ColourGradient shellGrad (finish.darker (0.9f), x, 0, finish.darker (0.9f), x + w, 0, false);
    shellGrad.addColour (0.18, finish);
    shellGrad.addColour (0.34, finish.brighter (0.55f));
    shellGrad.addColour (0.5, finish);
    shellGrad.addColour (0.82, finish.darker (0.4f));
    g.setGradientFill (shellGrad);
    g.fillRect (x, y + h * 0.5f, w, p.shellH);
    g.fillEllipse (x, y + p.shellH, w, h);

    // Bottom hoop
    juce::Path lower;
    lower.addCentredArc (cx, y + p.shellH + h * 0.5f, w * 0.5f, h * 0.5f, 0.0f, kPi * 0.5f, kPi * 1.5f, true);
    g.setColour (kChrome.withAlpha (0.8f));
    g.strokePath (lower, juce::PathStrokeType (2.5f));

    // Lugs, spaced as they'd be around the visible half of the shell.
    const int lugs = p.shape == Shape::FloorTom ? 6 : 5;
    for (int i = 0; i < lugs; ++i)
    {
        const float a = kPi * ((float) i + 0.5f) / (float) lugs;
        const float lx = cx - std::cos (a) * w * 0.46f;
        const float lh = p.shellH * 0.34f;
        juce::Rectangle<float> lug (lx - 3.0f, y + h * 0.5f + p.shellH * 0.5f - lh * 0.5f + std::sin (a) * h * 0.3f, 6.0f, lh);
        g.setGradientFill (juce::ColourGradient (kChrome, lug.getX(), 0, kChromeDark, lug.getRight(), 0, false));
        g.fillRoundedRectangle (lug, 2.5f);
    }
    if (p.shape == Shape::Snare)
    {
        g.setColour (kChrome);
        g.fillRoundedRectangle (cx + w * 0.2f, y + h * 0.5f + p.shellH * 0.35f, 14.0f, 10.0f, 2.0f);
    }

    // Lit from straight above: the shell falls into shadow towards the floor.
    {
        juce::Path shell;
        shell.addRectangle (x, y + h * 0.5f, w, p.shellH);
        shell.addEllipse (x, y + p.shellH, w, h);
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, y + h * 0.5f,
                                                 juce::Colours::black.withAlpha (0.62f), 0.0f, y + p.shellH + h, false));
        g.fillPath (shell);
    }

    // Top hoop and head
    g.setColour (kChrome);
    g.drawEllipse (b.reduced (1.0f), 3.0f);
    const auto head = kHead.interpolatedWith (kAccent.brighter (0.6f), glow * 0.45f);
    g.setGradientFill (juce::ColourGradient (head, cx, y, head.darker (0.25f), cx, y + h, false));
    g.fillEllipse (b.reduced (3.5f, 2.5f));
    g.setColour (juce::Colours::black.withAlpha (0.08f));
    g.drawEllipse (b.reduced (8.0f, 5.0f), 1.0f);

    // The spot reflects off the back of the hoop and the far side of the head.
    juce::Path glint;
    glint.addCentredArc (cx, y + h * 0.5f, w * 0.5f - 1.0f, h * 0.5f - 1.0f, 0.0f, -kPi * 0.35f, kPi * 0.35f, true);
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.strokePath (glint, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), cx, y + h * 0.2f,
                                             juce::Colours::transparentWhite, cx, y + h * 0.75f, false));
    g.fillEllipse (b.reduced (w * 0.18f, 3.0f).withTrimmedBottom (h * 0.3f));
}

void KitView::drawKick (juce::Graphics& g, const Piece& p, float glow) const
{
    const auto b = p.box;
    const float cx = b.getCentreX(), cy = b.getCentreY(), r = b.getWidth() * 0.5f;
    const auto finish = finishFor (p.slot);

    g.setColour (kChromeDark);
    g.drawLine (cx - r * 0.75f, cy + r * 0.45f, cx - r * 1.05f, kFloorY, 3.5f);
    g.drawLine (cx + r * 0.75f, cy + r * 0.45f, cx + r * 1.05f, kFloorY, 3.5f);

    juce::Path outline;
    outline.addEllipse (b);
    glowAround (g, outline, glow);

    // Wooden hoop in the kit's finish.
    g.setGradientFill (juce::ColourGradient (finish.brighter (0.4f), b.getX(), b.getY(), finish.darker (0.8f), b.getRight(), b.getBottom(), false));
    g.fillEllipse (b);

    // Front head with logo and port.
    const auto headR = b.reduced (r * 0.09f);
    const auto headCol = juce::Colour (0xff141210).interpolatedWith (kAccent.darker (0.6f), glow * 0.4f);
    g.setGradientFill (juce::ColourGradient (headCol.brighter (0.25f), cx - r * 0.4f, cy - r * 0.5f, headCol.darker (0.5f), cx + r * 0.5f, cy + r * 0.6f, true));
    g.fillEllipse (headR);

    for (int i = 0; i < 10; ++i)
    {
        const float a = kPi * 2.0f * (float) i / 10.0f + 0.3f;
        const float rx = cx + std::cos (a) * r * 0.955f, ry = cy + std::sin (a) * r * 0.955f;
        g.setColour (kChrome);
        g.fillEllipse (rx - 3.5f, ry - 3.5f, 7.0f, 7.0f);
    }

    // Lit from above: the front head darkens towards the floor.
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, cx, cy - r * 0.2f,
                                             juce::Colours::black.withAlpha (0.5f), cx, cy + r, false));
    g.fillEllipse (b);
    juce::Path rimGlint;
    rimGlint.addCentredArc (cx, cy, r - 2.0f, r - 2.0f, 0.0f, -kPi * 0.3f, kPi * 0.3f, true);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.strokePath (rimGlint, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // 70s logo head: sunset stripes across the head behind the name.
    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addEllipse (headR);
        g.reduceClipRegion (clip);
        dd::gui::paintSunsetStripes (g, { headR.getX(), cy - r * 0.02f, headR.getWidth(), r * 0.2f });
    }
    g.setColour (kCream.withAlpha (0.95f));
    g.setFont (dd::gui::LookAndFeel::display (r * 0.3f).withExtraKerningFactor (0.06f));
    g.drawText ("KITROOM", juce::Rectangle<float> (cx - r, cy - r * 0.4f, r * 2.0f, r * 0.36f), juce::Justification::centred);
    g.setColour (kCream.withAlpha (0.6f));
    g.setFont (dd::gui::LookAndFeel::font (r * 0.1f, true).withExtraKerningFactor (0.35f));
    g.drawText ("LIVE DRUMS", juce::Rectangle<float> (cx - r, cy + r * 0.22f, r * 2.0f, r * 0.14f), juce::Justification::centred);

    const float pr = r * 0.15f, px = cx + r * 0.36f, py = cy + r * 0.56f;
    g.setColour (kChrome.withAlpha (0.7f));
    g.drawEllipse (px - pr, py - pr, pr * 2.0f, pr * 2.0f, 2.0f);
    g.setColour (juce::Colour (0xff050404));
    g.fillEllipse (px - pr + 2.0f, py - pr + 2.0f, pr * 2.0f - 4.0f, pr * 2.0f - 4.0f);
}

void KitView::drawLabel (juce::Graphics& g, const Piece& p) const
{
    const auto& def = kr::slots()[(size_t) p.slot];
    const bool learning = proc.getLearnTarget() >= 0 && kr::targetSlot (proc.getLearnTarget()) == p.slot;
    const juce::String key = learning ? juce::String ("press a key") : kr::flNoteName (proc.getKey (p.slot, 0));
    const juce::String name = juce::String (def.name).toUpperCase();
    const bool loading = proc.isSlotLoading (p.slot);
    const bool selected = proc.selectedSlot == p.slot;

    auto nameFont = dd::gui::LookAndFeel::font (11.0f, true).withExtraKerningFactor (0.08f);
    auto keyFont = dd::gui::LookAndFeel::font (11.0f, true);
    const float wName = juce::GlyphArrangement::getStringWidth (nameFont, name);
    const float wKey = juce::GlyphArrangement::getStringWidth (keyFont, loading ? juce::String ("loading") : key);
    const float w = wName + wKey + 22.0f;
    juce::Rectangle<float> pill (p.label.x - w * 0.5f, p.label.y - 10.0f, w, 20.0f);

    if (selected)
    {
        g.setColour (kAccent.withAlpha (0.18f));
        g.fillRoundedRectangle (pill.expanded (3.0f), 13.0f);
    }
    g.setColour (juce::Colour (0xff120b07).withAlpha (selected ? 0.92f : 0.78f));
    g.fillRoundedRectangle (pill, 10.0f);
    g.setColour (selected ? kAccent : juce::Colour (0xff9c876e).withAlpha (0.35f));
    g.drawRoundedRectangle (pill, 10.0f, selected ? 1.3f : 0.8f);
    g.setColour (kCream);
    g.setFont (nameFont);
    g.drawText (name, pill.withTrimmedLeft (9.0f), juce::Justification::centredLeft);
    g.setColour (learning ? dd::gui::sunset::yellow : kKeyText);
    g.setFont (keyFont);
    g.drawText (loading ? juce::String ("loading") : key, pill.withTrimmedRight (9.0f), juce::Justification::centredRight);
}

void KitView::paint (juce::Graphics& g)
{
    // The kit is seen through a window sunk into the chassis.
    juce::Path frame;
    frame.addRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.reduceClipRegion (frame);
    drawRoom (g);

    for (auto& p : pieces)
    {
        const float glow = flash[(size_t) p.slot];
        switch (p.shape)
        {
            case Shape::China:
            case Shape::Crash:
            case Shape::Ride:
                drawStand (g, p.box.getCentreX(), p.box.getCentreY(), kFloorY, true);
                drawCymbal (g, p.box, glow, p.shape == Shape::China);
                break;
            case Shape::HiHat:
            {
                drawStand (g, p.box.getCentreX(), p.box.getCentreY(), kFloorY, true);
                g.setColour (juce::Colour (0xff222222));
                g.fillRoundedRectangle (p.box.getCentreX() - 14, kFloorY - 6, 34, 9, 2);
                drawCymbal (g, p.box.translated (0, 10), glow * 0.6f, false);
                drawCymbal (g, p.box, glow, false);
                break;
            }
            case Shape::Kick:
                drawKick (g, p, glow);
                break;
            case Shape::Snare:
            case Shape::RackTom:
            case Shape::FloorTom:
                drawDrum (g, p, glow);
                break;
        }
    }

    drawSpotlights (g);

    for (auto& p : pieces)
        if (p.slot == hover || p.slot == proc.selectedSlot)
        {
            g.setColour (p.slot == proc.selectedSlot ? kAccent.withAlpha (0.9f) : kCream.withAlpha (0.35f));
            g.strokePath (p.outline, juce::PathStrokeType (p.slot == proc.selectedSlot ? 1.6f : 1.0f));
        }

    // Film grain and a lens vignette over the scene, then the labels on top, crisp.
    const auto area = getLocalBounds();
    dd::gui::paintGrain (g, area, 0.8f);
    juce::ColourGradient vignette (juce::Colours::transparentBlack, area.toFloat().getCentreX(), area.toFloat().getCentreY() - 40.0f,
                                   juce::Colours::black.withAlpha (0.6f), 0.0f, 0.0f, true);
    vignette.addColour (0.5, juce::Colours::transparentBlack);
    g.setGradientFill (vignette);
    g.fillRect (area);

    for (auto& p : pieces)
        drawLabel (g, p);

    if (! proc.library.isOpen())
    {
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre (560, 110);
        g.setColour (juce::Colour (0xff120b07).withAlpha (0.92f));
        g.fillRoundedRectangle (r, 12);
        g.setColour (kAccent);
        g.setFont (dd::gui::LookAndFeel::font (18.0f, true));
        g.drawText ("Drum samples not found", r.removeFromTop (50), juce::Justification::centredBottom);
        g.setColour (kCream);
        g.setFont (dd::gui::LookAndFeel::font (13.0f));
        g.drawFittedText ("Install Kitroom with its .pkg (or Install.command) so the sample library\nis in Library/Application Support/ddeonmadeit/Kitroom.",
                          r.toNearestInt().reduced (16, 6), juce::Justification::centred, 3);
    }

    // Window edge: shadow cast by the chassis lip along the top, a dark rim all round.
    const auto win = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), 0.0f, 0.0f,
                                             juce::Colours::transparentBlack, 0.0f, 14.0f, false));
    g.fillRect (win.withHeight (14.0f));
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawRoundedRectangle (win.reduced (0.5f), 8.0f, 1.5f);
}
