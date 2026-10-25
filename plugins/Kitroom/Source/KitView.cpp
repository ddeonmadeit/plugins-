#include "KitView.h"

namespace
{
const juce::Colour kAccent { 0xffe8a33d };          // stage-light amber
const juce::Colour kChrome { 0xffd6dae0 };
const juce::Colour kChromeDark { 0xff6a7078 };
const juce::Colour kHead { 0xffece4d1 };            // coated head
const juce::Colour kBronze { 0xffbf8f35 };
constexpr float kFloorY = 396.0f;
constexpr float kPi = juce::MathConstants<float>::pi;

juce::Colour finishFromHex (const juce::String& hex, juce::Colour fallback)
{
    return hex.length() == 7 && hex.startsWithChar ('#') ? juce::Colour::fromString ("ff" + hex.substring (1)) : fallback;
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
        Piece pc { slot, shape, { x, y, w, h }, shellH, label, {} };
        pc.hit.addEllipse (pc.box);
        pc.hit.addRectangle (x, y + h * 0.5f, w, shellH);
        pc.hit.addEllipse (x, y + shellH, w, h);
        return pc;
    };
    auto cym = [] (int slot, S shape, float x, float y, float w, float h, juce::Point<float> label)
    {
        Piece pc { slot, shape, { x, y, w, h }, 0.0f, label, {} };
        pc.hit.addEllipse (pc.box.expanded (6.0f, 8.0f));
        return pc;
    };

    // Back to front.
    pieces.push_back (cym (kr::China, S::China, 40, 88, 175, 30, { 127, 66 }));
    pieces.push_back (cym (kr::Crash1, S::Crash, 215, 70, 235, 40, { 332, 48 }));
    pieces.push_back (cym (kr::Crash2, S::Crash, 735, 58, 245, 42, { 857, 36 }));
    pieces.push_back (cym (kr::RideSlot, S::Ride, 880, 150, 270, 46, { 1015, 136 }));
    pieces.push_back (drum (kr::Tom1, S::RackTom, 440, 122, 120, 34, 70, { 500, 102 }));
    pieces.push_back (drum (kr::Tom2, S::RackTom, 612, 122, 130, 36, 76, { 677, 102 }));

    Piece kick { kr::Kick, S::Kick, { 588 - 102, 290 - 102, 204, 204 }, 0.0f, { 588, 407 }, {} };
    kick.hit.addEllipse (kick.box);
    pieces.push_back (kick);

    pieces.push_back (drum (kr::Tom3, S::FloorTom, 790, 212, 170, 46, 118, { 875, 407 }));
    pieces.push_back (drum (kr::Snare, S::Snare, 300, 245, 150, 40, 50, { 375, 407 }));

    Piece hat = cym (kr::HiHat, S::HiHat, 140, 198, 175, 26, { 227, 176 });
    hat.hit.addEllipse (hat.box.translated (0, 10).expanded (6, 8));
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
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b1e15), r.getCentreX(), 120.0f,
                                             juce::Colour (0xff0c0907), r.getCentreX(), r.getBottom() + 200.0f, true));
    g.fillRect (r);

    // Warm stage light from above.
    juce::Path beam;
    beam.startNewSubPath (r.getCentreX() - 120, 0);
    beam.lineTo (r.getCentreX() + 120, 0);
    beam.lineTo (r.getCentreX() + 470, kFloorY);
    beam.lineTo (r.getCentreX() - 470, kFloorY);
    beam.closeSubPath();
    g.setGradientFill (juce::ColourGradient (kAccent.withAlpha (0.10f), r.getCentreX(), 0.0f,
                                             kAccent.withAlpha (0.0f), r.getCentreX(), kFloorY, false));
    g.fillPath (beam);

    // Floor and rug.
    g.setColour (juce::Colour (0xff0e0a08));
    g.fillRect (r.withTop (kFloorY - 30));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a2418), r.getCentreX(), kFloorY - 10,
                                             juce::Colour (0xff1d0f0b), r.getCentreX() + 520, kFloorY + 30, true));
    g.fillEllipse (r.getCentreX() - 540, kFloorY - 40, 1080, 84);
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

    juce::Path outline = p.hit;
    glowAround (g, outline, glow);

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

    // Top hoop and head
    g.setColour (kChrome);
    g.drawEllipse (b.reduced (1.0f), 3.0f);
    const auto head = kHead.interpolatedWith (kAccent.brighter (0.6f), glow * 0.45f);
    g.setGradientFill (juce::ColourGradient (head, cx, y, head.darker (0.25f), cx, y + h, false));
    g.fillEllipse (b.reduced (3.5f, 2.5f));
    g.setColour (juce::Colours::black.withAlpha (0.08f));
    g.drawEllipse (b.reduced (8.0f, 5.0f), 1.0f);
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

    g.setColour (kAccent.withAlpha (0.92f));
    g.setFont (dd::gui::LookAndFeel::font (r * 0.27f, true).withExtraKerningFactor (0.08f));
    g.drawText ("KITROOM", juce::Rectangle<float> (cx - r, cy - r * 0.32f, r * 2.0f, r * 0.34f), juce::Justification::centred);
    g.setColour (kHead.withAlpha (0.6f));
    g.setFont (dd::gui::LookAndFeel::font (r * 0.09f).withExtraKerningFactor (0.35f));
    g.drawText ("LIVE DRUMS", juce::Rectangle<float> (cx - r, cy + r * 0.02f, r * 2.0f, r * 0.14f), juce::Justification::centred);

    const float pr = r * 0.17f, px = cx + r * 0.42f, py = cy + r * 0.42f;
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

    g.setColour (juce::Colours::black.withAlpha (selected ? 0.8f : 0.55f));
    g.fillRoundedRectangle (pill, 10.0f);
    if (selected)
    {
        g.setColour (kAccent);
        g.drawRoundedRectangle (pill, 10.0f, 1.2f);
    }
    g.setColour (juce::Colour (0xffe9e4dc));
    g.setFont (nameFont);
    g.drawText (name, pill.withTrimmedLeft (9.0f), juce::Justification::centredLeft);
    g.setColour (learning ? juce::Colours::white : kAccent);
    g.setFont (keyFont);
    g.drawText (loading ? juce::String ("loading") : key, pill.withTrimmedRight (9.0f), juce::Justification::centredRight);
}

void KitView::paint (juce::Graphics& g)
{
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

        if (p.slot == hover || p.slot == proc.selectedSlot)
        {
            g.setColour (p.slot == proc.selectedSlot ? kAccent.withAlpha (0.9f) : juce::Colours::white.withAlpha (0.35f));
            g.strokePath (p.hit, juce::PathStrokeType (p.slot == proc.selectedSlot ? 1.6f : 1.0f));
        }
    }

    for (auto& p : pieces)
        drawLabel (g, p);

    if (! proc.library.isOpen())
    {
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre (560, 110);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 12);
        g.setColour (kAccent);
        g.setFont (dd::gui::LookAndFeel::font (18.0f, true));
        g.drawText ("Drum samples not found", r.removeFromTop (50), juce::Justification::centredBottom);
        g.setColour (juce::Colour (0xffe9e4dc));
        g.setFont (dd::gui::LookAndFeel::font (13.0f));
        g.drawFittedText ("Install Kitroom with its .pkg (or Install.command) so the sample library\nis in Library/Application Support/ddeonmadeit/Kitroom.",
                          r.toNearestInt().reduced (16, 6), juce::Justification::centred, 3);
    }
}
