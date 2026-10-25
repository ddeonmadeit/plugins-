#include "HorizonView.h"

namespace
{
namespace sunset = dd::gui::sunset;
const juce::Colour kCream { 0xffeadcc3 };
const juce::Colour kTan { 0xff9c876e };
const juce::Colour kNight { 0xff0b0705 };
const juce::Colour kRidgeNear { 0xff090503 };

juce::Path ridgePath (const std::array<float, 128>& heights, float x0, float w, float baseY, float scale)
{
    juce::Path p;
    p.startNewSubPath (x0, baseY + 2.0f);
    const auto n = heights.size();
    for (size_t i = 0; i < n; ++i)
        p.lineTo (x0 + w * (float) i / (float) (n - 1), baseY - heights[i] * scale);
    p.lineTo (x0 + w, baseY + 2.0f);
    p.closeSubPath();
    return p;
}
} // namespace

HorizonView::HorizonView (AfterglowProcessor& p) : proc (p)
{
    motion = proc.apvts.getParameter ("mMotion");
    tone = proc.apvts.getParameter ("mTone");

    // Fixed mountain shapes; the live spectrum rises on top of the near ridge.
    for (size_t i = 0; i < kRidge; ++i)
    {
        const float x = (float) i;
        nearBase[i] = 0.10f + 0.05f * std::sin (x * 0.071f + 1.3f) + 0.035f * std::sin (x * 0.19f + 0.4f)
                    + 0.015f * std::sin (x * 0.53f + 2.0f);
        midBase[i] = 0.16f + 0.06f * std::sin (x * 0.058f + 2.6f) + 0.03f * std::sin (x * 0.17f + 3.3f)
                   + 0.012f * std::sin (x * 0.47f + 0.8f);
        farBase[i] = 0.22f + 0.08f * std::sin (x * 0.045f + 4.1f) + 0.04f * std::sin (x * 0.13f + 1.1f)
                   + 0.02f * std::sin (x * 0.37f);
    }
    juce::Random rnd (1979);
    for (int i = 0; i < 70; ++i)
        stars.push_back ({ rnd.nextFloat(), rnd.nextFloat() * rnd.nextFloat() * 0.5f });

    proc.presets.addChangeListener (this);
    // Prime the ridge from whatever is already playing, so the window never opens flat.
    for (int i = 0; i < 4; ++i)
        timerCallback();
    startTimerHz (30);
}

HorizonView::~HorizonView() { proc.presets.removeChangeListener (this); }

void HorizonView::timerCallback()
{
    time += 1.0f / 30.0f;
    level = proc.displayLevel.load();

    // Live spectrum, log-spaced 40 Hz .. 16 kHz, rising fast and settling slowly.
    proc.scope.copyLatest (fftData.data(), kFftSize);
    std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) kFftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    const float sr = (float) std::max (8000.0, proc.getSampleRate());
    for (size_t i = 0; i < kRidge; ++i)
    {
        const float f0 = 40.0f * std::pow (400.0f, (float) i / (float) kRidge);
        const float f1 = 40.0f * std::pow (400.0f, (float) (i + 1) / (float) kRidge);
        const int b0 = juce::jlimit (1, kFftSize / 2 - 1, (int) (f0 * (float) kFftSize / sr));
        const int b1 = juce::jlimit (b0 + 1, kFftSize / 2, (int) (f1 * (float) kFftSize / sr) + 1);
        float m = 0.0f;
        for (int b = b0; b < b1; ++b)
            m = std::max (m, fftData[(size_t) b]);
        const float db = juce::Decibels::gainToDecibels (m / (float) (kFftSize / 4), -100.0f);
        // Tilt so a balanced mix reads level across the ridge (music falls ~4.5 dB per octave).
        const float tilted = db + 4.5f * std::log2 (f0 / 500.0f);
        const float target = juce::jlimit (0.0f, 1.0f, (tilted + 70.0f) / 60.0f);
        ridge[i] = target > ridge[i] ? ridge[i] + (target - ridge[i]) * 0.6f : ridge[i] * 0.92f + target * 0.08f;
    }
    repaint();
}

juce::Point<float> HorizonView::sunCentre() const
{
    const float m = motion != nullptr ? motion->convertFrom0to1 (motion->getValue()) : 0.0f;
    const float t = tone != nullptr ? tone->convertFrom0to1 (tone->getValue()) : 0.0f;
    const float w = (float) getWidth();
    const float top = (float) getHeight() * 0.24f, bottom = horizonY() - sunRadius() * 0.15f;
    return { w * (0.5f + 0.36f * m), bottom + (top - bottom) * (0.5f + 0.5f * t) };
}

void HorizonView::setFromPoint (juce::Point<float> p)
{
    const float w = (float) getWidth();
    const float top = (float) getHeight() * 0.24f, bottom = horizonY() - sunRadius() * 0.15f;
    const float m = juce::jlimit (-1.0f, 1.0f, (p.x / w - 0.5f) / 0.36f);
    const float t = juce::jlimit (-1.0f, 1.0f, 2.0f * (p.y - bottom) / (top - bottom) - 1.0f);
    if (motion != nullptr) motion->setValueNotifyingHost (motion->convertTo0to1 (m));
    if (tone != nullptr) tone->setValueNotifyingHost (tone->convertTo0to1 (t));
}

void HorizonView::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    if (motion != nullptr) motion->beginChangeGesture();
    if (tone != nullptr) tone->beginChangeGesture();
    setFromPoint (e.position);
}

void HorizonView::mouseDrag (const juce::MouseEvent& e) { setFromPoint (e.position); }

void HorizonView::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    if (motion != nullptr) motion->endChangeGesture();
    if (tone != nullptr) tone->endChangeGesture();
}

void HorizonView::mouseDoubleClick (const juce::MouseEvent&)
{
    for (auto* p : { motion, tone })
        if (p != nullptr)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
}

void HorizonView::mouseMove (const juce::MouseEvent& e)
{
    const bool over = e.position.getDistanceFrom (sunCentre()) < sunRadius() * 1.3f;
    if (over != overSun)
    {
        overSun = over;
        setMouseCursor (over ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
    }
}

//==============================================================================
void HorizonView::paintSky (juce::Graphics& g, float toneAmount) const
{
    const auto r = getLocalBounds().toFloat();
    const float hy = horizonY();
    // The higher (brighter) the sun, the warmer and lighter the sky near the horizon.
    const float lift = 0.5f + 0.5f * toneAmount;
    juce::ColourGradient sky (kNight, 0.0f, r.getY(), sunset::amber.darker (0.35f - 0.3f * lift), 0.0f, hy, false);
    sky.addColour (0.38, juce::Colour (0xff1e0907));
    sky.addColour (0.66, sunset::red.darker (1.25f - 0.6f * lift));
    sky.addColour (0.86, sunset::ember.darker (0.75f - 0.45f * lift));
    g.setGradientFill (sky);
    g.fillRect (r.withBottom (hy + 1.0f));

    // Atmospheric glow sitting on the horizon around the sun, so the light has somewhere to come from.
    const float sx = sunCentre().x;
    juce::ColourGradient haze (sunset::orange.withAlpha (0.28f + 0.2f * lift), sx, hy, sunset::red.withAlpha (0.0f),
                               sx + r.getWidth() * 0.55f, hy, true);
    haze.addColour (0.35, sunset::ember.withAlpha (0.12f + 0.08f * lift));
    g.setGradientFill (haze);
    g.fillRect (r.withBottom (hy + 1.0f));

    // Long thin clouds drifting across, their undersides lit by the low sun.
    for (int c = 0; c < 4; ++c)
    {
        const float cy = hy * (0.3f + 0.09f * (float) c);
        const float cw = r.getWidth() * (0.35f + 0.1f * (float) (c % 2));
        const float speed = 3.0f + 2.0f * (float) c;
        float cx = std::fmod (time * speed + (float) c * 330.0f, r.getWidth() + cw) - cw * 0.5f;
        const juce::Rectangle<float> cloud (cx - cw * 0.5f, cy, cw, 7.0f + 3.0f * (float) c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff120806).withAlpha (0.55f), 0.0f, cloud.getY(),
                                                 sunset::red.withAlpha (0.18f + 0.12f * lift), 0.0f, cloud.getBottom(), false));
        g.fillEllipse (cloud);
    }

    for (auto& s : stars)
    {
        const float twinkle = 0.5f + 0.5f * std::sin (time * 1.7f + s.x * 40.0f);
        g.setColour (kCream.withAlpha ((0.12f + 0.25f * twinkle) * (1.0f - lift * 0.6f)));
        const float d = 1.0f + s.x * 0.8f;
        g.fillEllipse (s.x * r.getWidth(), s.y * hy, d, d);
    }
}

void HorizonView::paintSun (juce::Graphics& g, juce::Point<float> c, float rad) const
{
    // Halo breathes with the output level.
    // Bloom in two layers: a wide warm corona and a tight hot glow; both breathe with the output level.
    const float glow = 0.22f + 0.55f * juce::jlimit (0.0f, 1.0f, level * 6.0f);
    juce::ColourGradient corona (sunset::orange.withAlpha (glow * 0.55f), c.x, c.y, sunset::red.withAlpha (0.0f), c.x + rad * 5.0f, c.y, true);
    corona.addColour (0.3, sunset::ember.withAlpha (glow * 0.25f));
    g.setGradientFill (corona);
    g.fillEllipse (c.x - rad * 5.0f, c.y - rad * 5.0f, rad * 10.0f, rad * 10.0f);
    // The hot glow is a ring hugging the disc, clear inside, so the sky still shows through the cuts.
    juce::ColourGradient ring (sunset::amber.withAlpha (0.0f), c.x, c.y, sunset::amber.withAlpha (0.0f), c.x + rad * 1.8f, c.y, true);
    ring.addColour (0.5, sunset::amber.withAlpha (0.0f));
    ring.addColour (0.58, sunset::amber.withAlpha (0.3f + glow * 0.45f));
    g.setGradientFill (ring);
    g.fillEllipse (c.x - rad * 1.8f, c.y - rad * 1.8f, rad * 3.6f, rad * 3.6f);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), horizonY()).toNearestInt());
    // 70s sunset: horizontal cuts through the lower half, widening towards the bottom.
    for (int k = 0; k < 5; ++k)
    {
        const float y = c.y + rad * (0.12f + 0.19f * (float) k);
        const float h = 1.5f + 1.3f * (float) k;
        g.excludeClipRegion (juce::Rectangle<float> (c.x - rad - 2.0f, y, rad * 2.0f + 4.0f, h).toNearestInt());
    }
    juce::ColourGradient disc (sunset::yellow, c.x, c.y - rad, sunset::red, c.x, c.y + rad, false);
    disc.addColour (0.4, sunset::amber);
    disc.addColour (0.7, sunset::orange);
    g.setGradientFill (disc);
    g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
    // A white-hot core high in the disc: the sun reads as a ball of light, not a flat sticker.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff0c8).withAlpha (0.8f), c.x, c.y - rad * 0.35f,
                                             sunset::yellow.withAlpha (0.0f), c.x + rad * 0.85f, c.y - rad * 0.35f, true));
    g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
}

void HorizonView::paintRidges (juce::Graphics& g, juce::Point<float> sun) const
{
    const float w = (float) getWidth(), hy = horizonY();
    const float scale = hy * 0.55f;

    std::array<float, kRidge> farH {}, midH {}, nearH {};
    for (size_t i = 0; i < kRidge; ++i)
    {
        // Spatially smoothed so the spectrum reads as hills, not bars.
        const float s = (ridge[i > 0 ? i - 1 : 0] + 2.0f * ridge[i] + ridge[std::min (i + 1, (size_t) kRidge - 1)]) * 0.25f;
        farH[i] = farBase[i] + s * 0.06f;
        midH[i] = midBase[i] + s * 0.14f;
        nearH[i] = nearBase[i] + s * 0.34f;
    }

    // Three ranges in aerial perspective: the far one hazy and lit by the sky, the near one almost black.
    // Each fades darker towards its foot, so the slopes have volume instead of reading as cut-outs.
    const auto fillRange = [&] (const juce::Path& path, juce::Colour top, juce::Colour foot, float peak)
    {
        g.setGradientFill (juce::ColourGradient (top, 0.0f, hy - peak * scale, foot, 0.0f, hy, false));
        g.fillPath (path);
    };
    // Valley haze between the ranges: low-lying glow from the sun, so each range stands in front of the next.
    const auto fog = [&] (float depth, float alpha)
    {
        g.setGradientFill (juce::ColourGradient (sunset::ember.withAlpha (0.0f), 0.0f, hy - depth * scale,
                                                 sunset::ember.withAlpha (alpha), 0.0f, hy, false));
        g.fillRect (juce::Rectangle<float> (0.0f, hy - depth * scale, w, depth * scale));
    };
    const auto far = ridgePath (farH, 0.0f, w, hy, scale);
    fillRange (far, juce::Colour (0xff5a2212), juce::Colour (0xff2e110a), 0.32f);
    fog (0.18f, 0.3f);
    const auto mid = ridgePath (midH, 0.0f, w, hy, scale);
    fillRange (mid, juce::Colour (0xff2c110a), juce::Colour (0xff150806), 0.24f);
    fog (0.1f, 0.18f);
    const auto near = ridgePath (nearH, 0.0f, w, hy, scale);
    fillRange (near, juce::Colour (0xff140805), kRidgeNear, 0.2f);

    // Rim light: the ridge edges catch the sun, strongest right under it.
    juce::ColourGradient rim (sunset::amber.withAlpha (1.0f), sun.x, sun.y, sunset::orange.withAlpha (0.0f), sun.x + w * 0.45f, sun.y, true);
    rim.addColour (0.3, sunset::orange.withAlpha (0.8f));
    rim.multiplyOpacity (0.35f);
    g.setGradientFill (rim);
    g.strokePath (far, juce::PathStrokeType (1.0f));
    rim.multiplyOpacity (1.6f);
    g.setGradientFill (rim);
    g.strokePath (mid, juce::PathStrokeType (1.2f));
    rim.multiplyOpacity (1.8f);
    g.setGradientFill (rim);
    g.strokePath (near, juce::PathStrokeType (1.8f));
}

void HorizonView::paintWater (juce::Graphics& g, juce::Point<float> sun, float rad) const
{
    const auto r = getLocalBounds().toFloat();
    const float hy = horizonY();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a0f08), 0.0f, hy, juce::Colour (0xff030201), 0.0f, r.getBottom(), false));
    g.fillRect (r.withTop (hy));
    // A column of reflected light under the sun, brightest at the horizon: a radial glow stretched downwards.
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (r.withTop (hy).toNearestInt());
        const float stretch = 2.4f, colR = rad * 1.4f;
        g.addTransform (juce::AffineTransform::scale (1.0f, stretch, sun.x, hy));
        juce::ColourGradient column (sunset::orange.withAlpha (0.34f), sun.x, hy, sunset::red.withAlpha (0.0f), sun.x + colR, hy, true);
        column.addColour (0.45, sunset::ember.withAlpha (0.14f));
        g.setGradientFill (column);
        g.fillEllipse (sun.x - colR, hy - colR, colR * 2.0f, colR * 2.0f);
    }

    // The sun's reflection: broken horizontal strokes that ripple with time and swell with the level.
    const float energy = 0.6f + juce::jlimit (0.0f, 1.0f, level * 5.0f);
    const float sunLow = juce::jlimit (0.0f, 1.0f, (sun.y - r.getHeight() * 0.2f) / (hy - r.getHeight() * 0.2f));
    for (int k = 0; k < 16; ++k)
    {
        const float y = hy + 5.0f + (float) k * (4.0f + (float) k * 0.55f);
        if (y > r.getBottom() - 3.0f)
            break;
        const float fade = 1.0f - (float) k / 16.0f;
        const float wv = rad * (1.5f - 0.04f * (float) k) * (0.75f + 0.25f * std::sin (time * 1.3f + (float) k * 1.7f)) * energy;
        const float dx = std::sin (time * 0.9f + (float) k * 2.3f) * 6.0f;
        g.setColour (sunset::amber.interpolatedWith (sunset::red, (float) k / 16.0f).withAlpha (0.55f * fade * (0.35f + 0.65f * sunLow)));
        g.fillRoundedRectangle (sun.x - wv * 0.5f + dx, y, wv, 1.6f + fade, 1.0f);
    }
    g.setColour (sunset::orange.withAlpha (0.35f));
    g.drawHorizontalLine ((int) hy, r.getX(), r.getRight());
}

void HorizonView::paintText (juce::Graphics& g) const
{
    auto r = getLocalBounds().toFloat().reduced (26.0f, 20.0f);
    const auto& pm = proc.presets;
    dd::gui::drawPrinted (g, pm.getCurrentCategory().toUpperCase(), r.removeFromTop (16.0f), sunset::amber,
                          dd::gui::LookAndFeel::font (12.0f, true).withExtraKerningFactor (0.28f), juce::Justification::topLeft);
    dd::gui::drawPrinted (g, pm.getCurrentName(), r.removeFromTop (46.0f), kCream,
                          dd::gui::LookAndFeel::display (38.0f).withExtraKerningFactor (0.03f), juce::Justification::centredLeft);
    g.setColour (kTan);
    g.setFont (dd::gui::LookAndFeel::font (14.0f));
    g.drawFittedText (pm.getCurrentDescription(), r.removeFromTop (40.0f).withWidth (std::min (r.getWidth(), 520.0f)).toNearestInt(),
                      juce::Justification::topLeft, 2, 1.0f);

    // Layer badges, top right.
    auto badges = getLocalBounds().toFloat().reduced (22.0f, 20.0f).removeFromTop (24.0f);
    for (int l = 1; l >= 0; --l)
    {
        const bool on = proc.apvts.getRawParameterValue (l == 0 ? "aOn" : "bOn")->load() >= 0.5f;
        const int src = (int) std::lround (proc.apvts.getRawParameterValue (l == 0 ? "aSource" : "bSource")->load());
        const juce::String text = juce::String (l == 0 ? "A  " : "B  ") + (on ? ag::choices::sources()[src].toUpperCase() : juce::String ("OFF"));
        const auto f = dd::gui::LookAndFeel::font (12.0f, true).withExtraKerningFactor (0.15f);
        const float bw = juce::GlyphArrangement::getStringWidth (f, text) + 22.0f;
        auto pill = badges.removeFromRight (bw);
        badges.removeFromRight (8.0f);
        // Smoked-glass pill: shadow under it, a lit rim when the layer plays, a reflection across the top.
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (pill.translated (0.0f, 1.5f), 12.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a160d).withAlpha (0.92f), 0.0f, pill.getY(),
                                                 juce::Colour (0xff080403).withAlpha (0.92f), 0.0f, pill.getBottom(), false));
        g.fillRoundedRectangle (pill, 12.0f);
        if (on)
        {
            g.setColour (sunset::amber.withAlpha (0.18f));
            g.drawRoundedRectangle (pill.expanded (1.5f), 13.5f, 3.0f);
        }
        g.setColour ((on ? sunset::amber : kTan).withAlpha (on ? 0.85f : 0.35f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 12.0f, 1.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.12f), 0.0f, pill.getY(),
                                                 juce::Colours::transparentWhite, 0.0f, pill.getCentreY(), false));
        g.fillRoundedRectangle (pill.reduced (2.0f, 1.5f).withHeight (pill.getHeight() * 0.45f), 10.0f);
        g.setColour (on ? kCream : kTan);
        g.setFont (f);
        g.drawText (text, pill, juce::Justification::centred);
    }

    auto foot = getLocalBounds().toFloat().reduced (26.0f, 12.0f).removeFromBottom (16.0f);
    dd::gui::drawPrinted (g, "DRAG THE SUN    <  STILL  /  MOVING  >    HIGH = BRIGHT    LOW = DARK    DOUBLE-CLICK = RESET",
                          foot, kTan.withAlpha (dragging || overSun ? 0.9f : 0.5f),
                          dd::gui::LookAndFeel::font (10.5f, true).withExtraKerningFactor (0.18f), juce::Justification::centredRight);
}

void HorizonView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    juce::Path frame;
    frame.addRoundedRectangle (bounds, 8.0f);
    g.reduceClipRegion (frame);

    const float t = tone != nullptr ? tone->convertFrom0to1 (tone->getValue()) : 0.0f;
    const auto sun = sunCentre();
    const float rad = sunRadius();
    paintSky (g, t);
    paintSun (g, sun, rad);
    paintRidges (g, sun);
    paintWater (g, sun, rad);

    dd::gui::paintGrain (g, getLocalBounds(), 0.75f);
    juce::ColourGradient vignette (juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getCentreY(),
                                   juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    vignette.addColour (0.55, juce::Colours::transparentBlack);
    g.setGradientFill (vignette);
    g.fillRect (bounds);

    paintText (g);

    // Behind glass: deep inner shadow under the bezel and a faint diagonal reflection.
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.75f), 0.0f, 0.0f, juce::Colours::transparentBlack, 0.0f, 18.0f, false));
    g.fillRect (bounds.withHeight (18.0f));
    for (bool left : { true, false })
    {
        const float x0 = left ? 0.0f : bounds.getRight(), x1 = left ? 14.0f : bounds.getRight() - 14.0f;
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), x0, 0.0f, juce::Colours::transparentBlack, x1, 0.0f, false));
        g.fillRect (left ? bounds.withWidth (14.0f) : bounds.withLeft (bounds.getRight() - 14.0f));
    }
    juce::Path sheen;
    sheen.startNewSubPath (bounds.getWidth() * 0.08f, 0.0f);
    sheen.lineTo (bounds.getWidth() * 0.34f, 0.0f);
    sheen.lineTo (bounds.getWidth() * 0.18f, bounds.getHeight() * 0.62f);
    sheen.lineTo (0.0f, bounds.getHeight() * 0.62f);
    sheen.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.045f), 0.0f, 0.0f,
                                             juce::Colours::transparentWhite, 0.0f, bounds.getHeight() * 0.62f, false));
    g.fillPath (sheen);
    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.5f);
}
