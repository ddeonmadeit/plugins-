#include "GuitarView.h"

namespace
{
namespace sunset = dd::gui::sunset;

constexpr float kStringMix[6] = { 1.0f, 0.86f, 0.72f, 0.6f, 0.48f, 0.4f };   // relative thickness, low E first

/** Closed Catmull-Rom spline through points given in a unit box (x 0..1, y -1..1), mapped into `r`. */
juce::Path smoothOutline (std::initializer_list<juce::Point<float>> pts, juce::Rectangle<float> r)
{
    std::vector<juce::Point<float>> p;
    for (auto q : pts)
        p.push_back ({ r.getX() + q.x * r.getWidth(), r.getCentreY() + q.y * r.getHeight() * 0.5f });
    juce::Path path;
    const size_t n = p.size();
    path.startNewSubPath (p[0]);
    for (size_t i = 0; i < n; ++i)
    {
        const auto p0 = p[(i + n - 1) % n], p1 = p[i], p2 = p[(i + 1) % n], p3 = p[(i + 2) % n];
        path.cubicTo (p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
    }
    path.closeSubPath();
    return path;
}

void softShadow (juce::Graphics& g, const juce::Path& p, float spread, float alpha, juce::Point<float> offset)
{
    for (int i = 6; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (alpha / 6.0f));
        g.strokePath (p, juce::PathStrokeType (spread * (float) i / 3.0f), juce::AffineTransform::translation (offset));
    }
    g.setColour (juce::Colours::black.withAlpha (alpha));
    g.fillPath (p, juce::AffineTransform::translation (offset));
}
} // namespace

//==============================================================================
GuitarView::GuitarView (SunburstProcessor& p) : proc (p)
{
    proc.apvts.addParameterListener ("guitar", this);
    startTimerHz (30);
}

GuitarView::~GuitarView()
{
    proc.apvts.removeParameterListener ("guitar", this);
}

void GuitarView::parameterChanged (const juce::String&, float)
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<GuitarView> (this)] { if (safe != nullptr) safe->repaint(); });
}

int GuitarView::guitarIndex() const
{
    return juce::jlimit (0, 5, (int) std::lround (proc.apvts.getRawParameterValue ("guitar")->load()));
}

void GuitarView::step (int delta)
{
    auto* param = proc.apvts.getParameter ("guitar");
    const int n = sunburst::choices::guitars().size();
    const int next = (guitarIndex() + delta + n) % n;
    param->beginChangeGesture();
    param->setValueNotifyingHost (param->convertTo0to1 ((float) next));
    param->endChangeGesture();
}

GuitarView::Look GuitarView::look() const
{
    switch (guitarIndex())
    {
        case 0:  return { Shape::Archtop,   juce::Colour (0xff3a1608), sunset::amber,             juce::Colour (0xffe9dcc0), true,  true,  false };
        case 1:  return { Shape::Classical, juce::Colour (0xff8a4f1c), juce::Colour (0xffe0a752), juce::Colour (0xff2a160a), true,  false, false };
        case 2:  return { Shape::Archtop,   juce::Colour (0xff120a07), juce::Colour (0xff3b2216), juce::Colour (0xffe9dcc0), false, true,  true };
        case 3:  return { Shape::Solid,     juce::Colour (0xff5e0f0a), sunset::red,               juce::Colour (0xff1a0d08), false, false, true };
        case 4:  return { Shape::Archtop,   juce::Colour (0xff7a2a08), sunset::orange,            juce::Colour (0xffe9dcc0), true,  true,  true };
        default: return { Shape::Archtop,   juce::Colour (0xff0e0806), juce::Colour (0xff2c1a12), sunset::amber.withAlpha (0.9f), false, true, true };
    }
}

void GuitarView::timerCallback()
{
    bool changed = false, ringing = false;
    for (int s = 0; s < 6; ++s)
    {
        const float l = proc.getStringLevel (s);
        const int f = proc.getStringFret (s);
        changed = changed || std::abs (l - levels[(size_t) s]) > 0.004f || f != frets[(size_t) s];
        ringing = ringing || l > 0.004f;
        levels[(size_t) s] = l;
        frets[(size_t) s] = f;
    }
    if (ringing || changed)
    {
        phase += 0.9f;
        repaint (stage.getSmallestIntegerContainer());
    }
}

void GuitarView::resized()
{
    stage = getLocalBounds().toFloat().reduced (2.0f);
    namePlate = { stage.getX() + 46.0f, stage.getY() + 14.0f, 250.0f, 34.0f };
    prevArrow = { namePlate.getX() - 34.0f, namePlate.getY(), 30.0f, namePlate.getHeight() };
    nextArrow = { namePlate.getRight() + 4.0f, namePlate.getY(), 30.0f, namePlate.getHeight() };
}

void GuitarView::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.position;
    if (prevArrow.contains (pos)) { step (-1); return; }
    if (nextArrow.contains (pos)) { step (1); return; }
    if (namePlate.contains (pos))
    {
        juce::PopupMenu menu;
        const auto& names = sunburst::choices::guitars();
        for (int i = 0; i < names.size(); ++i)
            menu.addItem (i + 1, names[i], true, i == guitarIndex());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
        {
            if (r > 0)
                step (r - 1 - guitarIndex());
        });
    }
}

//==============================================================================
void GuitarView::paintStage (juce::Graphics& g, juce::Rectangle<float> r)
{
    // A dark room with one warm tungsten spotlight from above; the guitar rests in its pool of light.
    g.setColour (juce::Colour (0xff080503));
    g.fillRoundedRectangle (r, 10.0f);
    juce::ColourGradient spot (juce::Colour (0xff4a2a14), r.getCentreX() + r.getWidth() * 0.08f, r.getY() + r.getHeight() * 0.38f,
                               juce::Colour (0x00080503), r.getCentreX() + r.getWidth() * 0.08f, r.getBottom() + r.getHeight() * 0.2f, true);
    spot.addColour (0.35, juce::Colour (0xff24140a));
    g.setGradientFill (spot);
    g.fillRoundedRectangle (r, 10.0f);
    // Beam haze from the top edge.
    juce::ColourGradient beam (sunset::amber.withAlpha (0.07f), r.getCentreX() + r.getWidth() * 0.08f, r.getY(),
                               sunset::amber.withAlpha (0.0f), r.getCentreX() + r.getWidth() * 0.08f, r.getBottom(), false);
    g.setGradientFill (beam);
    juce::Path cone;
    cone.startNewSubPath (r.getCentreX() - r.getWidth() * 0.05f, r.getY());
    cone.lineTo (r.getCentreX() + r.getWidth() * 0.21f, r.getY());
    cone.lineTo (r.getRight() - r.getWidth() * 0.02f, r.getBottom());
    cone.lineTo (r.getX() + r.getWidth() * 0.22f, r.getBottom());
    cone.closeSubPath();
    g.fillPath (cone);
    dd::gui::paintGrain (g, r.getSmallestIntegerContainer(), 0.9f);
    // Vignette and a recessed rim.
    juce::ColourGradient vig (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                              juce::Colours::black.withAlpha (0.7f), r.getX(), r.getY(), true);
    g.setGradientFill (vig);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (r.reduced (2.0f).translated (0.0f, 1.0f), 9.0f, 1.0f);
}

juce::Path GuitarView::bodyPath (Shape shape, juce::Rectangle<float> b) const
{
    switch (shape)
    {
        case Shape::Archtop:
            return smoothOutline ({ { 0.00f, -0.30f }, { 0.07f, -0.60f }, { 0.21f, -0.70f }, { 0.35f, -0.60f }, { 0.45f, -0.50f },
                                    { 0.57f, -0.76f }, { 0.75f, -0.97f }, { 0.92f, -0.84f }, { 1.00f, -0.40f }, { 1.00f, 0.40f },
                                    { 0.92f, 0.84f }, { 0.75f, 0.97f }, { 0.57f, 0.78f }, { 0.45f, 0.54f }, { 0.35f, 0.64f },
                                    { 0.21f, 0.76f }, { 0.07f, 0.66f }, { 0.00f, 0.32f } }, b);
        case Shape::Classical:
            return smoothOutline ({ { 0.00f, -0.32f }, { 0.08f, -0.64f }, { 0.22f, -0.74f }, { 0.36f, -0.62f }, { 0.47f, -0.52f },
                                    { 0.58f, -0.80f }, { 0.76f, -0.98f }, { 0.93f, -0.82f }, { 1.00f, -0.38f }, { 1.00f, 0.38f },
                                    { 0.93f, 0.82f }, { 0.76f, 0.98f }, { 0.58f, 0.80f }, { 0.47f, 0.52f }, { 0.36f, 0.62f },
                                    { 0.22f, 0.74f }, { 0.08f, 0.64f }, { 0.00f, 0.32f } }, b);
        case Shape::Solid:
            return smoothOutline ({ { 0.04f, -0.24f }, { -0.07f, -0.60f }, { 0.04f, -0.80f }, { 0.22f, -0.66f }, { 0.34f, -0.62f },
                                    { 0.50f, -0.80f }, { 0.72f, -0.92f }, { 0.92f, -0.78f }, { 1.00f, -0.34f }, { 1.00f, 0.34f },
                                    { 0.92f, 0.78f }, { 0.72f, 0.92f }, { 0.50f, 0.80f }, { 0.34f, 0.62f }, { 0.22f, 0.66f },
                                    { 0.04f, 0.80f }, { -0.07f, 0.60f }, { 0.04f, 0.24f } }, b);
    }
    return {};
}

void GuitarView::paintGuitar (juce::Graphics& g, juce::Rectangle<float> r, const Look& lk)
{
    const float W = r.getWidth(), H = r.getHeight(), cy = r.getCentreY();
    const bool classical = lk.shape == Shape::Classical, solid = lk.shape == Shape::Solid;

    // Geometry: headstock | neck | body; strings from the nut to the bridge.
    const float nutX = r.getX() + W * 0.15f;
    const auto body = juce::Rectangle<float> (r.getX() + W * (solid ? 0.56f : 0.53f), cy - H * (solid ? 0.40f : 0.47f),
                                              W * (solid ? 0.43f : 0.46f), H * (solid ? 0.80f : 0.94f));
    bridgeX = body.getX() + body.getWidth() * (classical ? 0.70f : 0.66f);
    const float scale = bridgeX - nutX;
    auto fretX = [nutX, scale] (float n) { return nutX + scale * (1.0f - std::exp2 (-n / 12.0f)); };
    const int lastFret = classical ? 19 : (solid ? 22 : 20);
    const float neckHalfNut = H * 0.075f, neckHalfEnd = H * 0.098f;
    fretboard = { nutX, cy - neckHalfEnd, fretX ((float) lastFret) - nutX, neckHalfEnd * 2.0f };
    auto stringY = [&] (int s, float x)
    {
        // A right-handed guitar lying face up with the neck to the left: low E on top.
        const float t = juce::jlimit (0.0f, 1.0f, (x - nutX) / (bridgeX - nutX));
        const float half = juce::jmap (t, neckHalfNut * 0.82f, H * 0.115f);
        return cy - half * (1.0f - 2.0f * (float) s / 5.0f);
    };

    // Floor shadow.
    {
        juce::Path shadow;
        shadow.addEllipse (r.getX() + W * 0.08f, cy + H * 0.28f, W * 0.92f, H * 0.34f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (shadow, juce::AffineTransform::translation (0.0f, 6.0f));
    }

    // Body: drop shadow, finish (sunburst or solid lacquer, matte sheen), binding.
    const auto outline = bodyPath (lk.shape, body);
    softShadow (g, outline, 3.0f, 0.45f, { 6.0f, 10.0f });
    {
        juce::ColourGradient finish (lk.centre, body.getX() + body.getWidth() * 0.62f, cy - body.getHeight() * 0.05f,
                                     lk.finish, body.getRight() + body.getWidth() * 0.05f, cy, true);
        if (lk.burst)
            finish.addColour (0.55, lk.centre.interpolatedWith (lk.finish, 0.45f));
        else
            finish = juce::ColourGradient (lk.centre, body.getX() + body.getWidth() * 0.5f, body.getY(),
                                           lk.finish, body.getX() + body.getWidth() * 0.6f, body.getBottom(), false);
        g.setGradientFill (finish);
        g.fillPath (outline);
        // Spotlight sheen on the upper bout and a darker rim where the top curves away.
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), body.getX() + body.getWidth() * 0.55f, body.getY() + body.getHeight() * 0.12f,
                                                 juce::Colours::white.withAlpha (0.0f), body.getX() + body.getWidth() * 0.55f, cy, false));
        g.fillPath (outline);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.strokePath (outline, juce::PathStrokeType (body.getHeight() * 0.035f));
        g.setColour (lk.binding);
        g.strokePath (outline, juce::PathStrokeType (1.6f));
    }

    // Sound hole / f-holes, pickguard.
    if (classical)
    {
        const juce::Point<float> c { body.getX() + body.getWidth() * 0.36f, cy };
        const float rad = body.getHeight() * 0.19f;
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (i % 2 == 0 ? juce::Colour (0xff2a160a) : sunset::amber.withAlpha (0.7f));
            g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c).expanded (4.0f + 3.0f * (float) i), 1.6f);
        }
        juce::ColourGradient hole (juce::Colour (0xff000000), c.x, c.y, juce::Colour (0xff1a0e06), c.x, c.y - rad, true);
        g.setGradientFill (hole);
        g.fillEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c));
    }
    if (lk.fHoles)
    {
        for (int side = -1; side <= 1; side += 2)
        {
            const float x = body.getX() + body.getWidth() * 0.66f, y = cy + (float) side * body.getHeight() * 0.27f;
            juce::Path f;
            f.startNewSubPath (x - body.getWidth() * 0.16f, y - (float) side * body.getHeight() * 0.06f);
            f.cubicTo (x - body.getWidth() * 0.07f, y + (float) side * body.getHeight() * 0.07f,
                       x + body.getWidth() * 0.03f, y - (float) side * body.getHeight() * 0.07f,
                       x + body.getWidth() * 0.12f, y + (float) side * body.getHeight() * 0.04f);
            g.setColour (juce::Colours::black.withAlpha (0.9f));
            g.strokePath (f, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.strokePath (f, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (0.0f, 2.0f));
        }
    }
    if (! classical)
    {
        // Tortoise pickguard below the strings.
        juce::Path guard = smoothOutline ({ { 0.0f, -0.55f }, { 0.45f, -0.75f }, { 1.0f, -0.30f }, { 0.80f, 0.55f }, { 0.25f, 0.70f } },
                                          { body.getX() + body.getWidth() * (solid ? 0.30f : 0.40f), cy + body.getHeight() * 0.09f,
                                            body.getWidth() * (solid ? 0.24f : 0.28f), body.getHeight() * 0.30f });
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (guard, juce::AffineTransform::translation (2.0f, 3.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a1a0c), body.getX(), cy, juce::Colour (0xff100704), body.getRight(), body.getBottom(), false));
        g.fillPath (guard);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.strokePath (guard, juce::PathStrokeType (1.0f));
    }

    // Neck and headstock.
    {
        const float headX = r.getX() + W * 0.01f;
        juce::Path head;
        head.startNewSubPath (nutX + 2.0f, cy - neckHalfNut * 1.15f);
        head.lineTo (headX + W * 0.02f, cy - H * (classical ? 0.13f : 0.15f));
        head.quadraticTo (headX - W * 0.005f, cy, headX + W * 0.02f, cy + H * (classical ? 0.13f : 0.15f));
        head.lineTo (nutX + 2.0f, cy + neckHalfNut * 1.15f);
        head.closeSubPath();
        softShadow (g, head, 2.0f, 0.4f, { 4.0f, 7.0f });
        g.setGradientFill (juce::ColourGradient (lk.shape == Shape::Solid ? lk.finish.brighter (0.2f) : juce::Colour (0xff1c0f08), headX, cy - H * 0.15f,
                                                 juce::Colour (0xff070403), headX, cy + H * 0.15f, false));
        g.fillPath (head);
        // Spotlight sheen across the face and a darker edge where it curves away.
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.12f), headX, cy - H * 0.14f,
                                                 juce::Colours::white.withAlpha (0.0f), headX, cy + H * 0.02f, false));
        g.fillPath (head);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (head, juce::PathStrokeType (3.0f));
        g.setColour (lk.binding.withAlpha (0.8f));
        g.strokePath (head, juce::PathStrokeType (1.2f));
        if (classical)
            for (int side = -1; side <= 1; side += 2)
            {
                const auto slot = juce::Rectangle<float> (headX + W * 0.035f, cy + (float) side * H * 0.055f - H * 0.022f, W * 0.085f, H * 0.044f);
                g.setColour (juce::Colours::black);
                g.fillRoundedRectangle (slot, H * 0.02f);
            }
        // Tuners: three a side, chrome buttons with a shadow.
        for (int i = 0; i < 6; ++i)
        {
            const float tx = headX + W * (0.045f + 0.03f * (float) (i % 3));
            const float ty = cy + (i < 3 ? 1.0f : -1.0f) * H * (classical ? 0.165f : 0.19f);
            const auto knob = juce::Rectangle<float> (H * 0.05f, H * 0.05f).withCentre ({ tx, ty });
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillEllipse (knob.translated (2.0f, 3.0f));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8dccb), knob.getX(), knob.getY(), juce::Colour (0xff4a3c30), knob.getRight(), knob.getBottom(), false));
            g.fillEllipse (knob);
        }

        juce::Path neck;
        neck.startNewSubPath (nutX, cy - neckHalfNut);
        neck.lineTo (fretboard.getRight(), cy - neckHalfEnd);
        neck.lineTo (fretboard.getRight(), cy + neckHalfEnd);
        neck.lineTo (nutX, cy + neckHalfNut);
        neck.closeSubPath();
        softShadow (g, neck, 1.5f, 0.35f, { 3.0f, 5.0f });
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff24140b), 0.0f, cy - neckHalfEnd, juce::Colour (0xff0b0604), 0.0f, cy + neckHalfEnd, false));
        g.fillPath (neck);
        g.setColour (lk.binding.withAlpha (classical ? 0.0f : 0.7f));
        g.strokePath (neck, juce::PathStrokeType (1.0f));

        // Nut, frets, inlays.
        g.setColour (juce::Colour (0xffe8dcc4));
        g.fillRect (nutX - 3.0f, cy - neckHalfNut, 4.0f, neckHalfNut * 2.0f);
        for (int f = 1; f <= lastFret; ++f)
        {
            const float x = fretX ((float) f);
            const float half = juce::jmap (x, nutX, fretboard.getRight(), neckHalfNut, neckHalfEnd);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRect (x + 1.0f, cy - half, 1.5f, half * 2.0f);
            g.setColour (juce::Colour (0xffb9ab98));
            g.fillRect (x - 0.5f, cy - half, 1.5f, half * 2.0f);
        }
        if (! classical)
            for (int f : { 3, 5, 7, 9, 12, 15, 17, 19, 21 })
            {
                if (f > lastFret) break;
                const float x = (fretX ((float) f - 1.0f) + fretX ((float) f)) * 0.5f;
                const float d = H * 0.024f;
                g.setColour (juce::Colour (0xffd8cbb0).withAlpha (0.85f));
                if (f == 12)
                {
                    g.fillEllipse (x - d * 0.5f, cy - neckHalfEnd * 0.55f - d * 0.5f, d, d);
                    g.fillEllipse (x - d * 0.5f, cy + neckHalfEnd * 0.55f - d * 0.5f, d, d);
                }
                else
                    g.fillEllipse (x - d * 0.5f, cy - d * 0.5f, d, d);
            }
    }

    // Pickups and bridge hardware.
    if (lk.metalPickups)
    {
        for (int i = 0; i < 2; ++i)
        {
            const float px = solid ? bridgeX - W * (0.045f + 0.085f * (float) i) : bridgeX - W * (0.05f + 0.10f * (float) i);
            const auto pu = juce::Rectangle<float> (W * 0.032f, H * 0.29f).withCentre ({ px, cy });
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (pu.translated (2.0f, 4.0f).expanded (2.0f), 3.0f);
            g.setColour (juce::Colour (0xff0d0806));
            g.fillRoundedRectangle (pu.expanded (3.0f), 3.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffdcd0c0), pu.getX(), pu.getY(), juce::Colour (0xff5b4b3e), pu.getRight(), pu.getBottom(), false));
            g.fillRoundedRectangle (pu, 2.0f);
            for (int s = 0; s < 6; ++s)
            {
                g.setColour (juce::Colour (0xff3a2e25));
                g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ pu.getCentreX(), stringY (s, px) }));
            }
        }
    }
    {
        // Bridge: wooden saddle (acoustic / classical) or a metal tune-o-matic.
        const auto br = juce::Rectangle<float> (W * (classical ? 0.035f : 0.014f), H * (classical ? 0.30f : 0.26f)).withCentre ({ bridgeX, cy });
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (br.translated (2.0f, 4.0f), 2.0f);
        g.setGradientFill (lk.metalPickups ? juce::ColourGradient (juce::Colour (0xffe0d4c4), br.getX(), br.getY(), juce::Colour (0xff5a4a3c), br.getRight(), br.getBottom(), false)
                                           : juce::ColourGradient (juce::Colour (0xff3a2112), br.getX(), br.getY(), juce::Colour (0xff100804), br.getRight(), br.getBottom(), false));
        g.fillRoundedRectangle (br, 2.0f);
        if (! classical && ! solid)
        {
            // Trapeze tailpiece.
            juce::Path tail;
            tail.startNewSubPath (bridgeX + W * 0.02f, cy - H * 0.09f);
            tail.lineTo (body.getRight() - W * 0.006f, cy - H * 0.05f);
            tail.lineTo (body.getRight() - W * 0.006f, cy + H * 0.05f);
            tail.lineTo (bridgeX + W * 0.02f, cy + H * 0.09f);
            tail.closeSubPath();
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillPath (tail, juce::AffineTransform::translation (2.0f, 4.0f));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd7c49c), bridgeX, cy - H * 0.1f, juce::Colour (0xff5a4a30), bridgeX, cy + H * 0.1f, false));
            g.fillPath (tail);
        }
        else if (solid)
        {
            const auto stop = juce::Rectangle<float> (W * 0.012f, H * 0.24f).withCentre ({ bridgeX + W * 0.035f, cy });
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (stop.translated (2.0f, 4.0f), 3.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe0d4c4), stop.getX(), stop.getY(), juce::Colour (0xff5a4a3c), stop.getRight(), stop.getBottom(), false));
            g.fillRoundedRectangle (stop, 3.0f);
        }
    }

    // Strings: from the headstock over the nut to the bridge; ringing strings glow and shimmer.
    const juce::Colour wound = classical ? juce::Colour (0xffc9c2b8) : (lk.metalPickups ? juce::Colour (0xffc8beb2) : juce::Colour (0xffd2a464));
    const juce::Colour plain = classical ? juce::Colour (0xffe8dcc8) : juce::Colour (0xffe6ddd2);
    for (int s = 0; s < 6; ++s)
    {
        const float th = 0.8f + 1.6f * kStringMix[s];
        const auto col = (classical ? s < 3 : s < 4) ? wound : plain;
        const float lv = juce::jlimit (0.0f, 1.0f, levels[(size_t) s] * 2.5f);
        const float y0 = stringY (s, nutX), y1 = stringY (s, bridgeX);
        // Headstock run.
        g.setColour (col.withAlpha (0.55f));
        g.drawLine (nutX, y0, r.getX() + W * (0.045f + 0.03f * (float) (s < 3 ? 2 - s : s - 3)), cy + (s < 3 ? -1.0f : 1.0f) * H * 0.19f, th * 0.8f);

        const int fret = frets[(size_t) s];
        const float startX = fret > 0 ? fretX ((float) fret - 0.5f) : nutX;
        // Shadow on the fretboard / top.
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawLine (nutX, y0 + 3.0f, bridgeX, y1 + 4.0f, th);
        if (lv > 0.01f)
        {
            // Vibrating segment between the finger and the bridge: a soft glowing blur.
            juce::Path vib;
            const int steps = 48;
            for (int k = 0; k <= steps; ++k)
            {
                const float t = (float) k / (float) steps;
                const float x = startX + (bridgeX - startX) * t;
                const float amp = std::sin (juce::MathConstants<float>::pi * t) * lv * H * 0.012f
                                * std::sin (phase * (1.6f + 0.25f * (float) s) + t * 9.0f);
                const float y = stringY (s, x) + amp;
                if (k == 0) vib.startNewSubPath (x, y); else vib.lineTo (x, y);
            }
            g.setColour (sunset::amber.withAlpha (0.18f * lv));
            g.strokePath (vib, juce::PathStrokeType (th + 6.0f * lv));
            g.setColour (sunset::orange.withAlpha (0.35f * lv));
            g.strokePath (vib, juce::PathStrokeType (th + 2.5f * lv));
            g.setColour (col.interpolatedWith (sunset::yellow, 0.6f * lv));
            g.strokePath (vib, juce::PathStrokeType (th));
            if (startX > nutX)
            {
                g.setColour (col);
                g.drawLine (nutX, y0, startX, stringY (s, startX), th);
            }
        }
        else
        {
            g.setColour (col);
            g.drawLine (nutX, y0, bridgeX, y1, th);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawLine (nutX, y0 - th * 0.3f, bridgeX, y1 - th * 0.3f, 0.6f);
        }

        // The fretting finger: a glowing dot on the fret.
        if (fret > 0 && levels[(size_t) s] > 0.002f)
        {
            const juce::Point<float> c { fretX ((float) fret - 0.5f), stringY (s, fretX ((float) fret - 0.5f)) };
            for (int k = 3; k >= 1; --k)
            {
                g.setColour (sunset::amber.withAlpha (0.12f * (float) (4 - k)));
                g.fillEllipse (juce::Rectangle<float> (6.0f + 5.0f * (float) k, 6.0f + 5.0f * (float) k).withCentre (c));
            }
            g.setColour (sunset::yellow);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (c));
        }
    }
}

void GuitarView::paint (juce::Graphics& g)
{
    const auto pal = dd::gui::Palette();
    paintStage (g, stage);
    const auto lk = look();
    paintGuitar (g, stage.reduced (stage.getWidth() * 0.03f, stage.getHeight() * 0.16f).translated (0.0f, stage.getHeight() * 0.06f), lk);

    // Name plate with arrows.
    dd::gui::paintInset (g, namePlate, pal, 5.0f);
    const auto& lib = proc.getLibrary();
    const auto& ids = sunburst::choices::guitarIds();
    const auto* info = lib.find (ids[guitarIndex()]);
    juce::String name = info != nullptr ? info->name : sunburst::choices::guitars()[guitarIndex()];
    if (! lib.isOpen())
        name = "SAMPLE LIBRARY NOT FOUND";
    else if (proc.isLoading())
        name += "  ...";
    g.setColour (sunset::amber);
    g.setFont (dd::gui::LookAndFeel::font (17.0f, true).withExtraKerningFactor (0.06f));
    g.drawText (name.toUpperCase(), namePlate.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
    for (int i = 0; i < 2; ++i)
    {
        const auto a = (i == 0 ? prevArrow : nextArrow).reduced (7.0f, 9.0f);
        juce::Path tri;
        if (i == 0) tri.addTriangle (a.getRight(), a.getY(), a.getRight(), a.getBottom(), a.getX(), a.getCentreY());
        else        tri.addTriangle (a.getX(), a.getY(), a.getX(), a.getBottom(), a.getRight(), a.getCentreY());
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillPath (tri, juce::AffineTransform::translation (1.0f, 2.0f));
        g.setColour (sunset::orange);
        g.fillPath (tri);
    }
    if (info != nullptr)
    {
        g.setColour (pal.dim.withAlpha (0.8f));
        g.setFont (dd::gui::LookAndFeel::font (11.5f));
        g.drawText ("Samples: " + info->credit, stage.reduced (16.0f, 10.0f).removeFromBottom (16.0f), juce::Justification::bottomLeft);
    }
    g.setColour (pal.dim.withAlpha (0.8f));
    g.setFont (dd::gui::LookAndFeel::font (11.5f));
    g.drawText ("Noise keys C2-E2:  muted strum  /  knock  /  mute  /  squeak  /  pick", stage.reduced (16.0f, 10.0f).removeFromTop (20.0f),
                juce::Justification::topRight);
}

//==============================================================================
PedalView::PedalView (juce::AudioProcessorValueTreeState& s, const juce::String& n, const juce::String& onId,
                      std::vector<std::pair<juce::String, juce::String>> controls, juce::Colour c)
    : name (n), body (c), onAttach (*s.getParameter (onId), [this] (float v) { on = v >= 0.5f; repaint(); })
{
    onAttach.sendInitialUpdate();
    for (auto& [id, title] : controls)
    {
        std::unique_ptr<juce::Component> w;
        if (dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)) != nullptr)
            w = std::make_unique<dd::gui::Choice> (s, id, title);
        else
            w = std::make_unique<dd::gui::Knob> (s, id, title, sunset::amber);
        addAndMakeVisible (*w);
        widgets.push_back (std::move (w));
    }
}

void PedalView::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    auto top = r.removeFromTop (92);
    const int w = top.getWidth() / std::max (1, (int) widgets.size());
    for (auto& c : widgets)
    {
        auto cell = top.removeFromLeft (w);
        if (dynamic_cast<dd::gui::Choice*> (c.get()) != nullptr)
            c->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 52).translated (0, -8));
        else
            c->setBounds (cell);
    }
    footswitch = juce::Rectangle<float> (34.0f, 34.0f).withCentre ({ (float) getWidth() * 0.5f, (float) getHeight() - 30.0f });
}

void PedalView::mouseDown (const juce::MouseEvent& e)
{
    if (footswitch.expanded (10.0f).contains (e.position) || e.position.y > (float) getHeight() - 60.0f)
        onAttach.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

void PedalView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    // Cast shadow, then the die-cast enclosure: lit top-left, shadowed bottom-right, a bevelled rim.
    for (int i = 5; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.1f));
        g.fillRoundedRectangle (r.translated (2.0f, (float) i * 1.5f).expanded ((float) i * 0.6f), 9.0f);
    }
    g.setGradientFill (juce::ColourGradient (body.brighter (0.25f), r.getX(), r.getY(), body.darker (0.7f), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 1.2f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 8.0f, 1.5f);
    dd::gui::paintGrain (g, r.getSmallestIntegerContainer(), 0.6f);

    // Knob panel recess at the top.
    auto panel = r.reduced (8.0f).removeFromTop (100.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (panel, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (panel.translated (0.0f, 1.0f), 6.0f, 1.0f);

    // Bottom-plate screws and a little sunset band between the knobs and the name.
    for (auto c : { r.getTopLeft() + juce::Point<float> (9.0f, 9.0f), r.getTopRight() + juce::Point<float> (-9.0f, 9.0f),
                    r.getBottomLeft() + juce::Point<float> (9.0f, -9.0f), r.getBottomRight() + juce::Point<float> (-9.0f, -9.0f) })
        dd::gui::paintScrew (g, c, 3.2f, 0.6f);
    const float midY = (panel.getBottom() + r.getBottom() - 78.0f) * 0.5f;
    dd::gui::paintSunsetStripes (g, juce::Rectangle<float> (std::min (90.0f, r.getWidth() * 0.55f), 10.0f).withCentre ({ r.getCentreX(), midY }));

    // Name plate.
    auto label = juce::Rectangle<float> (r.getWidth() - 24.0f, 22.0f).withCentre ({ r.getCentreX(), r.getBottom() - 66.0f });
    g.setColour (juce::Colour (0xff0d0805));
    g.fillRoundedRectangle (label, 4.0f);
    dd::gui::drawPrinted (g, name.toUpperCase(), label, juce::Colour (0xffeadcc3),
                          dd::gui::LookAndFeel::display (15.0f).withExtraKerningFactor (0.12f));

    // Status jewel and the chrome footswitch.
    dd::gui::paintLamp (g, { r.getRight() - 20.0f, r.getBottom() - 30.0f }, 5.0f, dd::gui::sunset::red.brighter (0.1f), on ? 1.0f : 0.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (footswitch.translated (1.5f, 3.5f).expanded (2.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff0e6d8), footswitch.getX(), footswitch.getY(), juce::Colour (0xff5d4e40), footswitch.getRight(), footswitch.getBottom(), false));
    g.fillEllipse (footswitch);
    const auto cap = footswitch.reduced (8.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6d5c4c), cap.getX(), cap.getY(), juce::Colour (0xffd8ccbc), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap);
}
