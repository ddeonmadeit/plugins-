#include "MirageView.h"
#include "Draw.h"

namespace mirage
{
namespace
{
// The colour ramp (intensity 0..1): black-red, deep red, red, red-orange, orange, hot orange, Tarantino yellow.
struct Stop { float t; uint32_t rgb; };
constexpr Stop kStops[] { { 0.00f, 0x050100 }, { 0.16f, 0x1c0402 }, { 0.32f, 0x4f0c05 }, { 0.48f, 0x99160a },
                          { 0.62f, 0xd42c0e }, { 0.76f, 0xfb5213 }, { 0.88f, 0xff7c19 }, { 0.96f, 0xffa51c }, { 1.00f, 0xffc61e } };

const juce::Colour kOrange { 0xffff6a1f }, kRed { 0xffd2280f }, kDim { 0xff6e1a0b }, kYellow { 0xffffc21e };
const juce::Colour kInk { 0xff0a0302 }, kPanel { 0xff120504 };

float luma (float r, float g, float b) noexcept { return 0.299f * r + 0.587f * g + 0.114f * b; }
} // namespace

//==============================================================================
juce::Colour Palette::base (float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    int k = 0;
    while (k < 7 && t > kStops[k + 1].t)
        ++k;
    const float f = (t - kStops[k].t) / (kStops[k + 1].t - kStops[k].t);
    auto ch = [&] (int shift)
    {
        const float a = (float) ((kStops[k].rgb >> shift) & 0xff), b = (float) ((kStops[k + 1].rgb >> shift) & 0xff);
        return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (a + (b - a) * f));
    };
    return juce::Colour (ch (16), ch (8), ch (0));
}

juce::Colour Palette::turn (juce::Colour c, float degrees)
{
    if (std::abs (degrees) < 0.01f)
        return c;
    // Turn the hue, then bring the brightness back to what it was; a hue that cannot get that bright
    // (blue) makes up the rest with white.
    const auto t = c.withRotatedHue (degrees / 360.0f);
    const float y0 = luma (c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue());
    float r = t.getFloatRed(), g = t.getFloatGreen(), b = t.getFloatBlue();
    const float y1 = luma (r, g, b);
    if (y1 < 1.0e-5f)
        return c;
    const float k = y0 / y1;
    r *= k; g *= k; b *= k;
    float mix = 1.0f;
    for (float v : { r, g, b })
        if (v > 1.0f)
            mix = std::min (mix, (1.0f - y0) / (v - y0));
    r = y0 + (r - y0) * mix;
    g = y0 + (g - y0) * mix;
    b = y0 + (b - y0) * mix;
    return juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, r), juce::jlimit (0.0f, 1.0f, g), juce::jlimit (0.0f, 1.0f, b), c.getFloatAlpha());
}

juce::Colour Palette::tint (juce::Colour c) const { return turn (c, hue); }

bool Palette::build (float hueDegrees, float whiteTop)
{
    if (std::abs (hueDegrees - hue) < 0.05f && std::abs (whiteTop - white) < 1.0e-3f)
        return false;
    hue = hueDegrees;
    white = whiteTop;
    for (int i = 0; i < 1024; ++i)
    {
        const float t = (float) i / 1023.0f;
        auto c = turn (base (t), hue);
        if (white > 0.0f)
            c = c.interpolatedWith (juce::Colours::white, white * 0.9f * std::pow (smoothstep (0.70f, 1.0f, t), 1.4f));
        lut[(size_t) i] = juce::PixelARGB (255, c.getRed(), c.getGreen(), c.getBlue());
    }
    orange = tint (kOrange);
    red = tint (kRed);
    dim = tint (kDim);
    yellow = tint (kYellow);
    ink = tint (kInk);
    panel = tint (kPanel);
    if (white > 0.0f)
        yellow = yellow.interpolatedWith (juce::Colours::white, 0.35f * white);
    return true;
}

//==============================================================================
void PanelLook::setPalette (const Palette& p)
{
    accent = p.orange;
    ink = p.ink;
    setColour (juce::PopupMenu::backgroundColourId, p.panel);
    setColour (juce::PopupMenu::textColourId, p.orange);
    setColour (juce::PopupMenu::headerTextColourId, p.red);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, p.red.withAlpha (0.4f));
    setColour (juce::PopupMenu::highlightedTextColourId, p.yellow);
}

void PanelLook::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging)
{
    g.setColour (accent.withAlpha (over || dragging ? 0.9f : 0.5f));
    for (float i = 0.3f; i < 1.0f; i += 0.3f)
        g.drawLine ((float) w * i, (float) h, (float) w, (float) h * i, 1.2f);
}

void PanelLook::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillPath (path, juce::AffineTransform::translation (0.0f, 4.0f));
    g.setColour (ink);
    g.fillPath (path);
    g.setColour (accent.withAlpha (0.25f));
    g.strokePath (path, juce::PathStrokeType (4.0f));
    g.setColour (accent.withAlpha (0.9f));
    g.strokePath (path, juce::PathStrokeType (1.2f));
}

//==============================================================================
HudDial::HudDial (juce::RangedAudioParameter& p, const juce::String& l, const Palette& palette)
    : param (p), attachment (p, [this] (float v) { value = param.convertTo0to1 (v); repaint(); }), label (l), pal (palette)
{
    if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (&p))
        paramId = withId->paramID;
    dd::gui::tagParam (*this, paramId, l);
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void HudDial::setLabel (const juce::String& l)
{
    if (l == label)
        return;
    label = l;
    dd::gui::tagParam (*this, paramId, l);
    repaint();
}

void HudDial::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float dot = std::max (1.0f, r.getWidth() / 46.0f * 1.3f);
    const float size = std::min (r.getWidth(), r.getHeight() - 9.0f * dot) - 4.0f;
    const auto c = juce::Point<float> (r.getCentreX(), r.getY() + 2.0f + size * 0.5f);
    constexpr int ticks = 19;
    for (int i = 0; i < ticks; ++i)
    {
        const float f = (float) i / (float) (ticks - 1);
        const float a = juce::degreesToRadians (-135.0f + 270.0f * f);
        const bool lit = f <= value + 1.0e-3f;
        const juce::Point<float> p0 (c.x + std::sin (a) * size * 0.30f, c.y - std::cos (a) * size * 0.30f);
        const juce::Point<float> p1 (c.x + std::sin (a) * size * 0.47f, c.y - std::cos (a) * size * 0.47f);
        if (lit)
        {
            g.setColour (pal.colour (0.55f + 0.38f * f).withAlpha (0.25f));
            g.drawLine ({ p0, p1 }, 5.0f);
        }
        g.setColour (lit ? pal.colour (0.6f + 0.34f * f) : pal.dim.withAlpha (0.55f));
        g.drawLine ({ p0, p1 }, 2.0f);
    }
    DotFont::draw (g, juce::String (juce::roundToInt (value * 100.0f)), juce::Rectangle<float> (size, size).withCentre (c), dot * 0.75f,
                   pal.orange, juce::Justification::centred);
    DotFont::draw (g, label.toUpperCase(), r.withTop (r.getBottom() - 8.0f * dot), dot * 0.8f,
                   isMouseOverOrDragging() ? pal.orange : pal.red, juce::Justification::centred, 0.5f);
}

void HudDial::mouseDown (const juce::MouseEvent&) { dragStart = value; attachment.beginGesture(); }
void HudDial::mouseDrag (const juce::MouseEvent& e)
{
    const float v = juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / 180.0f);
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (v));
}
void HudDial::mouseUp (const juce::MouseEvent&) { attachment.endGesture(); }
void HudDial::mouseDoubleClick (const juce::MouseEvent&) { attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); }

//==============================================================================
void HeaderKey::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool over = isMouseOver (true);
    if (framed)
    {
        juce::Path p;
        p.addRoundedRectangle (r.reduced (1.5f), r.getHeight() * 0.3f);
        g.setColour (pal.ink.withAlpha (0.55f));
        g.fillPath (p);
        glowStroke (g, p, (over ? pal.orange : pal.red).withAlpha (0.85f), 1.0f);
    }
    const auto t = text().toUpperCase();
    const float room = r.getWidth() - 2.0f * inset - 6.0f;
    const float d = std::min (dot, room / std::max (1.0f, (float) t.length() * 6.0f));
    const auto c = over ? pal.yellow.interpolatedWith (pal.orange, 0.35f)
                        : bright ? pal.yellow.interpolatedWith (pal.orange, 0.55f) : pal.orange;
    DotFont::draw (g, t, r.reduced (inset, 0.0f), d, c, juce::Justification::centred, over ? 1.0f : 0.7f);
}

void HueDot::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto c = r.getCentre();
    const bool over = isMouseOver();
    const float rad = std::min (r.getWidth(), r.getHeight()) * 0.5f;
    const float core = rad * 0.34f;
    const float breathe = 0.5f + 0.5f * std::sin (phase);
    // The halo breathes; under the mouse a ring of the wheel's own colours shows what clicking does.
    g.setGradientFill (juce::ColourGradient (pal.orange.withAlpha (0.42f + 0.18f * breathe), c, pal.orange.withAlpha (0.0f), c.translated (rad, 0.0f), true));
    g.fillEllipse (r);
    if (over)
        for (int i = 0; i < 24; ++i)
        {
            const float a = (float) i / 24.0f * juce::MathConstants<float>::twoPi;
            g.setColour (Palette::turn (juce::Colour (0xffff6a1f), (float) i * 15.0f));
            g.fillEllipse (juce::Rectangle<float> (rad * 0.2f, rad * 0.2f).withCentre ({ c.x + std::sin (a) * rad * 0.78f, c.y - std::cos (a) * rad * 0.78f }));
        }
    g.setColour (pal.yellow.interpolatedWith (pal.orange, 0.45f));
    g.fillEllipse (juce::Rectangle<float> (core * 2.0f, core * 2.0f).withCentre (c));
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillEllipse (juce::Rectangle<float> (core * 0.8f, core * 0.8f).withCentre (c.translated (-core * 0.3f, -core * 0.3f)));
}

//==============================================================================
HueWheel::HueWheel (juce::RangedAudioParameter& p, int size)
    : param (p), attachment (p, [this] (float v) { hue = v; repaint(); })
{
    setSize (size, size);
    dd::gui::setHelp (*this, "Hue", "Drag around the ring to turn every colour of the panel. Double-click for the original red and orange.");
    // The ring: the panel's orange turned to every hue, brightness kept, the original at the top.
    ring = juce::Image (juce::Image::ARGB, size, size, true, juce::SoftwareImageType());
    juce::Image::BitmapData bd (ring, juce::Image::BitmapData::writeOnly);
    const float c = (float) size * 0.5f, r0 = (float) size * 0.33f, r1 = (float) size * 0.47f;
    std::array<juce::Colour, 360> hues;
    for (int i = 0; i < 360; ++i)
        hues[(size_t) i] = Palette::turn (juce::Colour (0xffff6a1f), (float) i);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float dx = (float) x + 0.5f - c, dy = (float) y + 0.5f - c;
            const float d = std::sqrt (dx * dx + dy * dy);
            const float a = juce::jlimit (0.0f, 1.0f, std::min (d - r0, r1 - d) + 0.5f);
            if (a <= 0.0f)
                continue;
            float deg = juce::radiansToDegrees (std::atan2 (dx, -dy));
            if (deg < 0.0f)
                deg += 360.0f;
            const auto col = hues[(size_t) juce::jlimit (0, 359, (int) deg)];
            const float shade = 0.75f + 0.25f * (d - r0) / (r1 - r0);
            *reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y))
                = juce::PixelARGB ((juce::uint8) (255.0f * a), (juce::uint8) (col.getRed() * shade * a), (juce::uint8) (col.getGreen() * shade * a), (juce::uint8) (col.getBlue() * shade * a));
        }
    attachment.sendInitialUpdate();
}

void HueWheel::paint (juce::Graphics& g)
{
    const float size = (float) getWidth(), c = size * 0.5f;
    g.setOpacity (1.0f);
    g.drawImageAt (ring, 0, 0);
    const auto accent = Palette::turn (juce::Colour (0xffff6a1f), hue);
    const auto light = Palette::turn (juce::Colour (0xffffc21e), hue);

    // The marker on the ring, and a notch where the original colours are.
    const float a = juce::degreesToRadians (hue), rm = size * 0.40f;
    const juce::Point<float> m (c + std::sin (a) * rm, c - std::cos (a) * rm);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (juce::Rectangle<float> (size * 0.13f, size * 0.13f).withCentre (m.translated (0.0f, 1.5f)));
    g.setColour (accent);
    g.fillEllipse (juce::Rectangle<float> (size * 0.11f, size * 0.11f).withCentre (m));
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.drawEllipse (juce::Rectangle<float> (size * 0.11f, size * 0.11f).withCentre (m), 1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.fillRect (c - 0.75f, size * 0.015f, 1.5f, size * 0.03f);

    // In the middle: the whole ramp as it will look, and the value.
    const float dot = std::max (1.0f, size / 140.0f);
    DotFont::draw (g, "HUE", { c - size * 0.25f, c - size * 0.17f, size * 0.5f, 9.0f * dot }, dot, accent, juce::Justification::centred, 0.6f);
    const int deg = juce::roundToInt (hue);
    DotFont::draw (g, (deg > 0 ? "+" : "") + juce::String (deg) + "^", { c - size * 0.25f, c - size * 0.05f, size * 0.5f, 9.0f * dot },
                   dot * 1.4f, light, juce::Justification::centred);
    const auto bar = juce::Rectangle<float> (size * 0.36f, size * 0.05f).withCentre ({ c, c + size * 0.12f });
    constexpr int steps = 48;
    for (int i = 0; i < steps; ++i)
    {
        g.setColour (Palette::turn (Palette::base ((float) i / (float) (steps - 1)), hue));
        g.fillRect (bar.getX() + bar.getWidth() * (float) i / (float) steps, bar.getY(), bar.getWidth() / (float) steps + 0.5f, bar.getHeight());
    }
    g.setColour (accent.withAlpha (0.6f));
    g.drawRect (bar.expanded (1.0f), 1.0f);
}

void HueWheel::setFrom (juce::Point<float> p)
{
    const float c = (float) getWidth() * 0.5f;
    float deg = juce::radiansToDegrees (std::atan2 (p.x - c, c - p.y));
    if (std::abs (deg) < 4.0f)
        deg = 0.0f;   // the original colours are easy to find again
    attachment.setValueAsPartOfGesture (deg);
}

void HueWheel::mouseDown (const juce::MouseEvent& e) { attachment.beginGesture(); setFrom (e.position); }
void HueWheel::mouseDrag (const juce::MouseEvent& e) { setFrom (e.position); }
void HueWheel::mouseUp (const juce::MouseEvent&) { attachment.endGesture(); }
void HueWheel::mouseDoubleClick (const juce::MouseEvent&) { attachment.setValueAsCompleteGesture (0.0f); }

//==============================================================================
MirageView::MirageView (MirageProcessor& p)
    : proc (p),
      sceneKey (pal, [this] { return "SCENE " + sceneNames()[juce::jlimit (0, sceneNames().size() - 1, scene())]; }),
      styleKey (pal, [this] { return "STYLE " + styleNames()[juce::jlimit (0, styleNames().size() - 1, style())]; }),
      prevKey (pal, [] { return juce::String ("<"); }),
      nextKey (pal, [] { return juce::String (">"); }),
      nameKey (pal, [this]
      {
          const auto n = proc.presets.getCurrentName();
          return (n.isEmpty() ? juce::String ("INIT") : n) + (proc.presets.isModified() ? " *" : "");
      })
{
    noise.resize (512 * 512);
    for (auto& n : noise)
        n = rng.nextFloat() * 2.0f - 1.0f;
    // Soft hills for the terrain: the grain tile blurred until only rolling shapes are left (it still tiles).
    {
        std::vector<float> a (256 * 256), b (256 * 256);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
                a[(size_t) (y * 256 + x)] = noise[(size_t) (y * 512 + x)];
        for (int pass = 0; pass < 6; ++pass)
        {
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x)
                {
                    float sum = 0.0f;
                    for (int k = -6; k <= 6; ++k)
                        sum += a[(size_t) (y * 256 + ((x + k) & 255))];
                    b[(size_t) (y * 256 + x)] = sum / 13.0f;
                }
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x)
                {
                    float sum = 0.0f;
                    for (int k = -6; k <= 6; ++k)
                        sum += b[(size_t) (((y + k) & 255) * 256 + x)];
                    a[(size_t) (y * 256 + x)] = sum / 13.0f;
                }
        }
        const auto [lo, hi] = std::minmax_element (a.begin(), a.end());
        smoothNoise.resize (a.size());
        for (size_t i = 0; i < a.size(); ++i)
            smoothNoise[i] = (a[i] - *lo) / std::max (1.0e-6f, *hi - *lo);
    }
    expLut.resize (2048);
    for (int i = 0; i < 2048; ++i)
        expLut[(size_t) i] = std::exp (-(float) i / 256.0f);   // exp (-x) for x in [0, 8)

    updatePalette();

    static const std::pair<const char*, const char*> controls[] {
        { "react", "React" }, { "grain", "Grain" }, { "digital", "Digital" }, { "speed", "Speed" },
        { "eclipse", "Eclipse" }, { "dunes", "Dunes" }, { "heat", "Heat" } };
    for (auto [id, label] : controls)
        addAndMakeVisible (dials.add (new HudDial (*p.apvts.getParameter (id), label, pal)));

    addAndMakeVisible (hueDot);
    hueDot.onClick = [this] { openHueWheel(); };
    dd::gui::tagParam (hueDot, "hue", "Hue");

    for (auto* k : { &sceneKey, &styleKey, &nameKey, &prevKey, &nextKey })
        addAndMakeVisible (*k);
    dd::gui::tagParam (sceneKey, "scene", "Scene");
    dd::gui::tagParam (styleKey, "style", "Style");
    sceneKey.onClick = [this] { showChoiceMenu ("scene", sceneKey); };
    styleKey.onClick = [this] { showChoiceMenu ("style", styleKey); };
    nameKey.bright = true;
    prevKey.framed = nextKey.framed = false;
    nameKey.onClick = [this] { showPresetMenu(); };
    prevKey.onClick = [this] { proc.presets.loadRelative (-1); };
    nextKey.onClick = [this] { proc.presets.loadRelative (1); };
    dd::gui::setHelp (nameKey, "Preset", "Click for every scene and look by name; the arrows step through them.");
    dd::gui::setHelp (prevKey, "Previous", "The previous preset.");
    dd::gui::setHelp (nextKey, "Next", "The next preset.");

    updateSceneLabels();
    help.onChange = [this] { repaint(); };
    setLookAndFeel (&look);
    setOpaque (true);
    startTimerHz (60);
}

MirageView::~MirageView()
{
    stopTimer();
    if (wheelBox != nullptr)
        wheelBox->dismiss();
    setLookAndFeel (nullptr);
}

void MirageView::updatePalette()
{
    // The ramp and the colours of everything drawn live follow at once (cheap). The chassis and the cached
    // chrome are rebuilt at most four times a second while the hue moves (dragging the wheel, automation),
    // and once more when it settles, so a moving hue never makes frames slow.
    const float white = style() == (int) Style::film ? 0.0f : 0.85f;
    if (pal.build (param ("hue"), white))
    {
        look.setPalette (pal);
        frameDirty = true;
    }
    if (frameDirty && ! frameVals.empty() && (lastFrameBuild < 0.0 || an.time - lastFrameBuild > 0.25 || frameImg.isNull()))
    {
        buildFrame();
        lastFrameBuild = an.time;
        frameDirty = false;
    }
}

void MirageView::updateSceneLabels()
{
    // The two scene dials mean something different in every scene; their captions and help follow.
    struct Pair { const char* a; const char* aHelp; const char* b; const char* bHelp; };
    static const Pair pairs[] {
        { "Eclipse", "How much of the sun the moon covers.", "Dunes", "How tall the dunes stand." },
        { "Flash", "How much the whole screen flashes on hits.", "Rays", "How far the shards of light reach." },
        { "Fog", "How quickly the land fades into the dark.", "Hills", "How tall the hills and the sound's ridges stand." },
        { "Spin", "How fast the figures turn.", "Pulse", "How much the figures swell with the sound." },
        { "Twist", "How much the tunnel turns as it goes.", "Rings", "How close together the rings come." },
    };
    const auto& pr = pairs[juce::jlimit (0, 4, scene())];
    dials[4]->setLabel (pr.a);
    dials[5]->setLabel (pr.b);
    help.setTable ({
        { "react", "How hard everything moves with the sound." },
        { "grain", "Film grain and tape noise over the whole picture." },
        { "digital", "The screen: pixel mesh or scanlines, and the readouts over the window." },
        { "speed", "How fast the pictures travel and the waveform and thermal strip scroll." },
        { "eclipse", pr.aHelp },
        { "dunes", pr.bHelp },
        { "heat", "Hotter: brighter, up into the top of the colour ramp. Cooler: deeper, darker shades." },
        { "hue", "Click for the colour wheel: it turns every colour of the panel." },
        { "scene", "What the window shows: Dunes, Burst, Terrain, Figure or Tunnel." },
        { "style", "How the screen draws it: Film grain, a glowing CRT tube, or worn VHS tape." },
    });
}

void MirageView::resized()
{
    s = (float) getHeight() / 440.0f;
    const float m = 10.0f * s;
    auto r = getLocalBounds().toFloat().reduced (m);
    header = r.removeFromTop (26.0f * s);
    r.removeFromTop (6.0f * s);
    window = r.removeFromTop (222.0f * s);
    r.removeFromTop (8.0f * s);
    deck = r.removeFromTop (104.0f * s);
    r.removeFromTop (8.0f * s);
    thermal = r;

    // Header: DEON and the hue dot, the scene and style keys, the preset selector in the middle.
    const float hh = header.getHeight(), keyH = hh - 4.0f * s;
    const float deonW = DotFont::width ("DEON", 2.4f * s);
    hueDot.setBounds (juce::Rectangle<float> (hh * 0.9f, hh * 0.9f).withCentre ({ header.getX() + deonW + 15.0f * s, header.getCentreY() }).toNearestInt());
    float x = header.getX() + deonW + 32.0f * s;
    for (auto [key, longest] : { std::pair<HeaderKey*, const char*> { &sceneKey, "SCENE TERRAIN" }, { &styleKey, "STYLE FILM" } })
    {
        key->dot = 1.3f * s;
        const float kw = DotFont::width (longest, key->dot) + 18.0f * s;
        key->setBounds (juce::Rectangle<float> (x, header.getCentreY() - keyH * 0.5f, kw, keyH).toNearestInt());
        x += kw + 8.0f * s;
    }
    const auto sel = juce::Rectangle<float> (250.0f * s, keyH).withCentre (header.getCentre());
    nameKey.dot = prevKey.dot = nextKey.dot = 1.6f * s;
    nameKey.inset = 22.0f * s;
    nameKey.setBounds (sel.toNearestInt());
    prevKey.setBounds (sel.withWidth (24.0f * s).toNearestInt());
    nextKey.setBounds (sel.withX (sel.getRight() - 24.0f * s).withWidth (24.0f * s).toNearestInt());

    // Deck: fixed instruments and the scrolling waveform taking what is left.
    auto d = deck;
    const float gap = 8.0f * s;
    pLevels = d.removeFromLeft (84.0f * s); d.removeFromLeft (gap);
    pScope = d.removeFromLeft (240.0f * s); d.removeFromLeft (gap);
    pControls = d.removeFromRight (7.0f * 46.0f * s + 16.0f * s); d.removeFromRight (gap);
    const bool roomy = d.getWidth() > 760.0f * s;
    pReticle = roomy ? d.removeFromRight (104.0f * s) : juce::Rectangle<float>();
    if (roomy) d.removeFromRight (gap);
    pSpectrum = d.getWidth() > 520.0f * s ? d.removeFromRight (260.0f * s) : juce::Rectangle<float>();
    if (! pSpectrum.isEmpty()) d.removeFromRight (gap);
    pWave = d;

    auto c = pControls.reduced (8.0f * s, 0.0f).withTrimmedTop (18.0f * s).withTrimmedBottom (6.0f * s);
    const float w = c.getWidth() / (float) dials.size();
    for (int i = 0; i < dials.size(); ++i)
        dials[i]->setBounds (c.withX (c.getX() + w * (float) i).withWidth (w).toNearestInt());

    buildStatics();
}

void MirageView::buildStatics()
{
    const int ww = std::max (8, (int) window.getWidth()), wh = std::max (8, (int) window.getHeight());
    windowImg = juce::Image (juce::Image::ARGB, ww, wh, true, juce::SoftwareImageType());
    crest.assign ((size_t) (Analyzer::kLayers * ww), 0.0f);
    buf.assign ((size_t) (ww * wh), 0.0f);
    persist.assign ((size_t) (ww * wh), 0.0f);
    tubeW = tubeH = 0;
    const int tw = std::max (8, (int) thermal.getWidth()), th = std::max (8, (int) thermal.getHeight());
    thermalImg = juce::Image (juce::Image::ARGB, tw, th, true, juce::SoftwareImageType());
    thermVals.assign ((size_t) (tw * th), 0.03f);

    // The chassis: dark anodised metal, brushed, in the same colours (kept as brightness, so a new hue is cheap).
    const int fw = std::max (8, getWidth()), fh = std::max (8, getHeight());
    frameVals.resize ((size_t) (fw * fh));
    std::vector<float> streak ((size_t) fw);
    for (auto& v : streak)
        v = rng.nextFloat();
    for (int y = 0; y < fh; ++y)
    {
        const float vy = (float) y / (float) fh;
        for (int x = 0; x < fw; ++x)
        {
            const float brush = 0.6f * streak[(size_t) x] + 0.4f * noise[(size_t) (((y * 3) & 511) * 512 + (x & 511))] * 0.5f;
            frameVals[(size_t) (y * fw + x)] = 0.085f + 0.03f * brush - 0.025f * vy + 0.02f * noise[(size_t) ((y & 511) * 512 + ((x * 7) & 511))];
        }
    }
    buildFrame();
}

void MirageView::buildFrame()
{
    const int fw = std::max (8, getWidth()), fh = std::max (8, getHeight());
    if ((int) frameVals.size() != fw * fh)
        return;
    frameImg = juce::Image (juce::Image::ARGB, fw, fh, true, juce::SoftwareImageType());
    juce::Image::BitmapData bd (frameImg, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < fh; ++y)
    {
        auto* px = reinterpret_cast<juce::PixelARGB*> (bd.getLinePointer (y));
        const float* v = frameVals.data() + (size_t) (y * fw);
        for (int x = 0; x < fw; ++x)
            px[x] = pal.at (v[x]);
    }
    chromeImg = {};
    frameDirty = false;
    lastFrameBuild = an.time;
}

void MirageView::buildChrome (float scale)
{
    // Everything that does not move, drawn once: the chassis, seams, panel bodies, outlines and titles.
    chromeScale = scale;
    chromeImg = juce::Image (juce::Image::ARGB, std::max (8, juce::roundToInt ((float) getWidth() * scale)),
                             std::max (8, juce::roundToInt ((float) getHeight() * scale)), true);
    juce::Graphics g (chromeImg);
    g.addTransform (juce::AffineTransform::scale (scale));
    g.setOpacity (1.0f);
    g.drawImage (frameImg, getLocalBounds().toFloat());
    drawFrame (g);
    drawPanel (g, pLevels, "LEVEL");
    drawPanel (g, pScope, "SCOPE");
    drawPanel (g, pWave, "WAVEFORM");
    if (! pSpectrum.isEmpty())
        drawPanel (g, pSpectrum, "SPECTRUM");
    if (! pReticle.isEmpty())
        drawPanel (g, pReticle, "FIELD");
    drawPanel (g, pControls, "CONTROL");
}

//==============================================================================
void MirageView::timerCallback()
{
    if (! isShowing())
        return;   // a hidden window costs nothing
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = lastTick > 0.0 ? (float) juce::jlimit (0.001, 0.1, now - lastTick) : 1.0f / 60.0f;
    lastTick = now;
    advance (dt);
    repaint();
}

void MirageView::advance (float dt)
{
    const float speed = param ("speed");
    frameDt = dt;
    an.update (proc.tap, proc.transport.sampleRate.load(), speed, dt);
    sunPulse += (an.low - sunPulse) * 0.25f;
    scan += dt * (0.12f + 0.25f * speed);
    scan -= std::floor (scan);
    flash = std::max (flash * std::pow (0.04f, dt), an.hit);
    spin = std::fmod (spin + dt * (0.1f + 1.4f * param ("eclipse")) * (0.6f + 0.8f * an.energy), juce::MathConstants<float>::twoPi * 64.0f);
    tunnelDepth += dt * (0.5f + 2.5f * speed) * (0.6f + 0.9f * an.energy);
    if (tunnelDepth > 4096.0f)
    {
        tunnelDepth -= 4096.0f;   // floats stay precise; the rings' memory repeats every 64, so nothing jumps
        lastRing -= 4096;
    }
    hueDot.phase = std::fmod (hueDot.phase + dt * 2.2f, juce::MathConstants<float>::twoPi);

    // The terrain's spectrum rows ride with the land: one row every quarter metre travelled.
    constexpr int rows = 320;
    if (specHist.empty())
        specHist.assign ((size_t) (rows * Analyzer::kBands), 0.0f);
    const float step = dt * (1.2f + 7.0f * speed) * (0.75f + 0.5f * an.energy);
    travel = std::fmod (travel + step, 32.0f);   // the land's texture repeats every 32 m
    travelRows += step / 0.25f;
    for (int guard = 0; travelRows >= 1.0f && guard < rows; ++guard)
    {
        specHead = (specHead + 1) % rows;
        // Blurred across the bands first, so the sound makes broad ranges of hills rather than spikes.
        std::array<float, Analyzer::kBands> tmp {}, row = an.band;
        for (int pass = 0; pass < 3; ++pass)
        {
            for (int b = 0; b < Analyzer::kBands; ++b)
            {
                float sum = 0.0f;
                for (int k = -4; k <= 4; ++k)
                    sum += row[(size_t) juce::jlimit (0, Analyzer::kBands - 1, b + k)];
                tmp[(size_t) b] = sum / 9.0f;
            }
            row = tmp;
        }
        std::copy (row.begin(), row.end(), specHist.begin() + (ptrdiff_t) specHead * Analyzer::kBands);
        travelRows -= 1.0f;
    }
    travelRows = std::min (travelRows, 1.0f);

    updatePalette();
    renderWindow();
    renderThermal();
    ++frames;
}

void MirageView::renderWindow()
{
    const int w = windowImg.getWidth(), h = windowImg.getHeight();
    if (w < 8 || h < 8 || (int) buf.size() != w * h)
        return;
    const int sc = scene(), st = style();
    if (sc != lastScene || st != lastStyle)
    {
        std::fill (persist.begin(), persist.end(), 0.0f);
        if (sc != lastScene)
        {
            lastScene = sc;
            updateSceneLabels();
        }
        lastStyle = st;
    }
    switch ((Scene) sc)
    {
        case Scene::burst:   sceneBurst(); break;
        case Scene::terrain: sceneTerrain(); break;
        case Scene::figure:  sceneFigure(); break;
        case Scene::tunnel:  sceneTunnel(); break;
        case Scene::dunes:
        default:             sceneDunes(); break;
    }
    switch ((Style) st)
    {
        case Style::crt: styleCrt(); break;
        case Style::vhs: styleVhs(); break;
        case Style::film:
        default:         styleFilm(); break;
    }
}

void MirageView::renderThermal()
{
    const int w = thermalImg.getWidth(), h = thermalImg.getHeight();
    if (w < 8 || h < 8 || (int) thermVals.size() != w * h)
        return;
    thermalCarry += 0.6f + 2.2f * param ("speed");
    const int step = std::min (w, (int) thermalCarry);
    thermalCarry -= (float) step;
    const float grain = param ("grain"), heatGain = 0.86f + 0.28f * param ("heat");
    if (step > 0)
        for (int y = 0; y < h; ++y)
        {
            float* row = thermVals.data() + (size_t) (y * w);
            std::memmove (row, row + step, sizeof (float) * (size_t) (w - step));
            const float fb = (1.0f - (float) y / (float) (h - 1)) * (float) (Analyzer::kBands - 1);
            const int b = std::min ((int) fb, Analyzer::kBands - 2);
            const float v = an.bandFast[(size_t) b] + (an.bandFast[(size_t) b + 1] - an.bandFast[(size_t) b]) * (fb - (float) b);
            for (int x = w - step; x < w; ++x)
            {
                const float n = noise[(size_t) ((y & 511) * 512 + ((x + frames * 13) & 511))];
                row[x] = (0.05f + 0.93f * std::pow (v, 1.35f)) * heatGain + grain * 0.06f * n;
            }
        }
    // Kept as brightness and coloured every frame, so a new hue reaches the whole strip at once.
    juce::Image::BitmapData bd (thermalImg, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
    {
        auto* px = reinterpret_cast<juce::PixelARGB*> (bd.getLinePointer (y));
        const float* row = thermVals.data() + (size_t) (y * w);
        for (int x = 0; x < w; ++x)
            px[x] = pal.lut[(size_t) Palette::index (row[x])];
    }
}

//==============================================================================
void MirageView::paint (juce::Graphics& g)
{
    if (frames == 0)
    {
        // A first picture straight away. Off screen (a headless snapshot, the download page's preview)
        // there is time to let the moving scenes fill in, as they would after a second or two of music.
        const int warm = isShowing() ? 6 : 90;
        for (int i = 0; i < warm; ++i)
            advance (1.0f / 60.0f);
    }
    const float physical = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (chromeImg.isNull() || std::abs (physical - chromeScale) > 0.01f)
        buildChrome (physical);
    g.setOpacity (1.0f);
    g.drawImage (chromeImg, getLocalBounds().toFloat());
    drawHeader (g);

    // The window.
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (window, 9.0f * s);
        g.reduceClipRegion (clip);
        g.setOpacity (1.0f);   // images take the current colour's opacity
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (windowImg, window);
        drawWindowHud (g);
    }

    drawLevels (g, pLevels);
    drawScope (g, pScope);
    drawWave (g, pWave);
    if (! pSpectrum.isEmpty())
        drawSpectrum (g, pSpectrum);
    if (! pReticle.isEmpty())
        drawReticle (g, pReticle);

    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (thermal, 7.0f * s);
        g.reduceClipRegion (clip);
        g.setOpacity (1.0f);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (thermalImg, thermal);
        drawThermalHud (g);
    }
    glowStroke (g, [&] { juce::Path p; p.addRoundedRectangle (thermal, 7.0f * s); return p; }(), pal.red.withAlpha (0.85f), 1.2f * s);
}

void MirageView::drawFrame (juce::Graphics& g)
{
    // Machined seams around the window and between the deck and the thermal strip, with a warm edge light.
    const auto b = getLocalBounds().toFloat();
    g.setColour (pal.ink.withAlpha (0.7f));
    g.drawRect (b, 2.0f * s);
    g.setColour (pal.red.withAlpha (0.18f));
    g.drawLine (b.getX() + 6.0f, b.getY() + 1.5f, b.getRight() - 6.0f, b.getY() + 1.5f, 1.0f);
    for (auto r : { window.expanded (4.0f * s), thermal.expanded (3.0f * s) })
    {
        g.setColour (pal.ink.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 11.0f * s);
        g.setColour (pal.red.withAlpha (0.14f));
        g.drawRoundedRectangle (r.translated (0.0f, 1.0f), 11.0f * s, 1.0f);
    }
}

void MirageView::drawHeader (juce::Graphics& g)
{
    auto h = header;
    DotFont::draw (g, "DEON", h, 2.4f * s, pal.orange, juce::Justification::centredLeft);

    // Transport readouts, as much as fits right of the preset selector.
    const double bpm = proc.transport.bpm.load(), ppq = std::max (0.0, proc.transport.ppq.load());
    const bool playing = proc.transport.playing.load();
    const int bar = (int) std::floor (ppq / 4.0) + 1, beat = (int) std::floor (std::fmod (ppq, 4.0)) + 1;
    const double secs = ppq * 60.0 / std::max (1.0, bpm);
    const int mm = (int) (secs / 60.0), ss = (int) std::fmod (secs, 60.0), tenths = (int) (std::fmod (secs, 1.0) * 10.0);
    const float dot = 1.5f * s;
    const float room = h.getRight() - 40.0f * s - ((float) nameKey.getRight() + 14.0f * s);
    auto line = "BPM " + juce::String (bpm, 1) + "   BAR " + pad (bar, 3) + "." + juce::String (beat) + "   " + pad (mm, 2) + ":" + pad (ss, 2) + "." + juce::String (tenths);
    if (DotFont::width (line, dot) > room)
        line = "BPM " + juce::String (bpm, 1) + "  BAR " + pad (bar, 3) + "." + juce::String (beat);
    if (DotFont::width (line, dot) > room)
        line = juce::String (bpm, 1);
    DotFont::draw (g, line, h.withTrimmedRight (40.0f * s), dot, pal.orange, juce::Justification::centredRight, 0.8f);
    const bool blink = playing ? std::fmod (an.time, 1.0) < 0.6 : true;
    const auto lamp = juce::Rectangle<float> (9.0f * s, 9.0f * s).withCentre ({ h.getRight() - 14.0f * s, h.getCentreY() });
    g.setColour ((playing ? pal.orange : pal.dim).withAlpha (blink ? 1.0f : 0.25f));
    g.fillEllipse (lamp);
    g.setColour (pal.orange.withAlpha (playing && blink ? 0.3f : 0.0f));
    g.fillEllipse (lamp.expanded (4.0f * s));
}

juce::Point<float> MirageView::toScreen (float ix, float iy) const noexcept
{
    const int iw = windowImg.getWidth(), ih = windowImg.getHeight();
    float x = ix, y = iy;
    if (style() == (int) Style::crt && iw > 0 && ih > 0)
    {
        // The tube bends the picture: undo the lens (a few rounds of the inverse) so marks sit on what they mark.
        const float us = (ix / (float) iw) * 2.0f - 1.0f, vs = (iy / (float) ih) * 2.0f - 1.0f;
        float u = us, v = vs;
        for (int i = 0; i < 4; ++i)
        {
            u = us / (1.0f + 0.035f * v * v);
            v = vs / (1.0f + 0.12f * u * u);
        }
        x = (u + 1.0f) * 0.5f * (float) iw;
        y = (v + 1.0f) * 0.5f * (float) ih;
    }
    return { window.getX() + x * window.getWidth() / (float) std::max (1, iw), window.getY() + y * window.getHeight() / (float) std::max (1, ih) };
}

void MirageView::drawWindowHud (juce::Graphics& g)
{
    const float digital = param ("digital");
    const auto w = window;

    if (digital > 0.01f)
    {
        // A slow bright scanline and a sweeping scan bar.
        const float sy = w.getY() + w.getHeight() * (float) std::fmod (an.time * 0.07, 1.0);
        g.setGradientFill (juce::ColourGradient (pal.orange.withAlpha (0.0f), 0.0f, sy - 18.0f * s, pal.orange.withAlpha (0.06f * digital), 0.0f, sy, false));
        g.fillRect (w.getX(), sy - 18.0f * s, w.getWidth(), 18.0f * s);
        const float sx = w.getX() + w.getWidth() * scan;
        g.setColour (pal.orange.withAlpha (0.10f * digital));
        g.fillRect (sx - 1.0f * s, w.getY(), 2.0f * s, w.getHeight());
        g.setColour (pal.orange.withAlpha (0.04f * digital));
        g.fillRect (sx - 30.0f * s, w.getY(), 30.0f * s, w.getHeight());

        // Crosshair marks across the top.
        g.setColour (pal.orange.withAlpha (0.22f * digital));
        const float stepX = 64.0f * s, stepY = 44.0f * s, arm = 3.0f * s;
        for (float y = w.getY() + 24.0f * s; y < w.getY() + w.getHeight() * 0.42f; y += stepY)
            for (float x = w.getX() + 32.0f * s; x < w.getRight() - 16.0f * s; x += stepX)
            {
                g.fillRect (x - arm, y - 0.5f * s, 2.0f * arm, 1.0f * s);
                g.fillRect (x - 0.5f * s, y - arm, 1.0f * s, 2.0f * arm);
            }
    }

    switch ((Scene) scene())
    {
        case Scene::burst:   drawBurstHud (g); break;
        case Scene::terrain: drawTerrainHud (g); break;
        case Scene::figure:  drawFigureHud (g); break;
        case Scene::tunnel:  drawTunnelHud (g); break;
        case Scene::dunes:
        default:             drawDunesHud (g); break;
    }
    if (style() != (int) Style::film)
        drawTapeHud (g);

    // Status block, top left: what the panel is doing, or what the control under the mouse does.
    {
        auto area = juce::Rectangle<float> (w.getX() + 16.0f * s, w.getY() + 14.0f * s, w.getWidth() * 0.45f, 12.0f * s);
        DotFont::draw (g, "> SCAN " + juce::String (proc.transport.playing.load() ? "LIVE" : "IDLE") + "   SR " + juce::String (juce::roundToInt (proc.transport.sampleRate.load())) + " HZ",
                       area, 1.3f * s, pal.orange);
        area.translate (0.0f, 14.0f * s);
        if (help.isActive())
            DotFont::draw (g, "> " + help.getTitle().toUpperCase() + ": " + help.getText().toUpperCase(), area.withWidth (w.getWidth() - 32.0f * s), 1.3f * s, pal.yellow.interpolatedWith (pal.orange, 0.5f));
        else
            DotFont::draw (g, "> ENERGY " + pad (juce::roundToInt (an.energy * 100.0f), 3) + "%   HIGH " + pad (juce::roundToInt (an.high * 100.0f), 3) + "%",
                           area, 1.3f * s, pal.red);
    }

    // Corner brackets and the glowing bezel.
    {
        juce::Path br;
        const float L = 18.0f * s, in = 7.0f * s;
        const auto r = w.reduced (in);
        br.startNewSubPath (r.getX(), r.getY() + L); br.lineTo (r.getX(), r.getY()); br.lineTo (r.getX() + L, r.getY());
        br.startNewSubPath (r.getRight() - L, r.getY()); br.lineTo (r.getRight(), r.getY()); br.lineTo (r.getRight(), r.getY() + L);
        br.startNewSubPath (r.getX(), r.getBottom() - L); br.lineTo (r.getX(), r.getBottom()); br.lineTo (r.getX() + L, r.getBottom());
        br.startNewSubPath (r.getRight() - L, r.getBottom()); br.lineTo (r.getRight(), r.getBottom()); br.lineTo (r.getRight(), r.getBottom() - L);
        glowStroke (g, br, pal.orange.withAlpha (0.7f), 1.2f * s);
        juce::Path bezel;
        bezel.addRoundedRectangle (w.reduced (0.5f), 9.0f * s);
        glowStroke (g, bezel, pal.orange.withAlpha (0.9f), 1.3f * s);
    }
}

void MirageView::drawTapeHud (juce::Graphics& g)
{
    // Camcorder on-screen display: the time of day with frames, and the deck's state for VHS.
    const auto w = window;
    const auto now = juce::Time::getCurrentTime();
    const int ff = (int) (std::fmod (an.time, 1.0) * 25.0);
    const auto stamp = pad (now.getHours(), 2) + ":" + pad (now.getMinutes(), 2) + ":" + pad (now.getSeconds(), 2) + ":" + pad (ff, 2);
    const float dot = 2.0f * s;
    const auto c = pal.yellow.interpolatedWith (pal.orange, 0.3f);
    DotFont::draw (g, stamp, { w.getX() + 22.0f * s, w.getBottom() - 30.0f * s, 300.0f * s, 16.0f * s }, dot, c, juce::Justification::centredLeft, 1.2f);
    if (style() == (int) Style::vhs)
    {
        const bool playing = proc.transport.playing.load();
        const bool blink = std::fmod (an.time, 1.0) < 0.55;
        const auto label = playing ? juce::String ("PLAY >") : juce::String ("PAUSE ||");
        if (playing || blink)
            DotFont::draw (g, label, { w.getRight() - 140.0f * s, w.getY() + 14.0f * s, 118.0f * s, 16.0f * s }, dot, c, juce::Justification::centredRight, 1.2f);
        DotFont::draw (g, "SP", { w.getRight() - 140.0f * s, w.getBottom() - 30.0f * s, 118.0f * s, 16.0f * s }, dot, c, juce::Justification::centredRight, 1.2f);
    }
}

void MirageView::drawDunesHud (juce::Graphics& g)
{
    const auto w = window;
    const float t = (float) an.time;
    const int iw = windowImg.getWidth(), ih = windowImg.getHeight();

    // Sun reticle: a broken ring that turns, ticks, a readout on a leader line.
    const float R = (float) ih * 0.33f * (1.0f + 0.10f * sunPulse * param ("react"));
    const auto sun = toScreen ((float) iw * 0.70f, (float) ih * 0.58f - R * 0.16f);
    const float Rs = R * w.getHeight() / (float) ih;
    {
        juce::Path ring;
        const float rr = Rs * 1.22f;
        for (int i = 0; i < 6; ++i)
        {
            const float a0 = t * 0.25f + (float) i * juce::MathConstants<float>::twoPi / 6.0f;
            ring.addCentredArc (sun.x, sun.y, rr, rr, 0.0f, a0, a0 + 0.62f, true);
        }
        glowStroke (g, ring, pal.orange.withAlpha (0.55f + 0.3f * an.energy), 1.1f * s);
        juce::Path ticks;
        for (int i = 0; i < 36; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::twoPi / 36.0f - t * 0.1f;
            const float r0 = Rs * 1.30f, r1 = Rs * ((i % 3) == 0 ? 1.38f : 1.34f);
            ticks.startNewSubPath (sun.x + std::cos (a) * r0, sun.y + std::sin (a) * r0);
            ticks.lineTo (sun.x + std::cos (a) * r1, sun.y + std::sin (a) * r1);
        }
        g.setColour (pal.red.withAlpha (0.6f));
        g.strokePath (ticks, juce::PathStrokeType (1.0f * s));
        const juce::Point<float> tip (sun.x - Rs * 0.95f, sun.y - Rs * 0.95f);
        const juce::Point<float> elbow (tip.x - 30.0f * s, tip.y - 22.0f * s);
        leader (g, tip, elbow, -70.0f * s, pal.orange, s);
        DotFont::draw (g, "SOLAR " + pad (juce::roundToInt (sunPulse * 100.0f), 3) + "%",
                       juce::Rectangle<float> (120.0f * s, 12.0f * s).withPosition (elbow.x - 70.0f * s, elbow.y - 14.0f * s), 1.3f * s, pal.orange);
    }

    // Peak marker on the near dune, low-end marker over the far left dunes.
    {
        const float* c0 = crest.data();
        int best = iw / 10;
        for (int x = iw / 10; x < iw * 6 / 10; ++x)
            if (c0[x] < c0[best])
                best = x;
        const auto tip = toScreen ((float) best, c0[best]);
        const juce::Point<float> elbow (tip.x + 26.0f * s, tip.y - 30.0f * s);
        leader (g, tip, elbow, 86.0f * s, pal.orange, s);
        DotFont::draw (g, "PEAK " + dbText (std::max (an.holdL, an.holdR)) + " DB",
                       juce::Rectangle<float> (120.0f * s, 12.0f * s).withPosition (elbow.x + 4.0f * s, elbow.y - 14.0f * s), 1.3f * s, pal.orange);

        const int lowX = iw / 12;
        const float* c1 = crest.data() + (size_t) (2 * iw);
        const auto tip2 = toScreen ((float) lowX, c1[lowX]);
        const juce::Point<float> elbow2 (tip2.x + 18.0f * s, tip2.y - 38.0f * s);
        leader (g, tip2, elbow2, 96.0f * s, pal.red, s);
        DotFont::draw (g, "LOW END " + pad (juce::roundToInt (an.low * 100.0f), 3) + "%  " + juce::String (juce::roundToInt (an.lowHz)) + " HZ",
                       juce::Rectangle<float> (170.0f * s, 12.0f * s).withPosition (elbow2.x + 4.0f * s, elbow2.y - 14.0f * s), 1.3f * s, pal.red);
    }

    // The live waveform traced along the near dune's ridge.
    {
        const float scaleY = w.getHeight() / (float) ih;
        juce::Path wave;
        const int n = 360;
        for (int i = 0; i <= n; ++i)
        {
            const float u = (float) i / (float) n;
            const int x = juce::jlimit (0, iw - 1, (int) (u * (float) (iw - 1)));
            const float sample = an.recentL[(size_t) (Analyzer::kRecent - 1441 + i * 4)];
            const auto p = toScreen ((float) x, crest[(size_t) x]).translated (0.0f, -6.0f * s - sample * 26.0f * s * param ("react") + 0.0f * scaleY);
            if (i == 0)
                wave.startNewSubPath (p);
            else
                wave.lineTo (p);
        }
        glowStroke (g, wave, pal.orange.withAlpha (0.85f), 1.1f * s);
    }
}

void MirageView::drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    if (r.isEmpty())
        return;
    g.setColour (pal.panel.withAlpha (0.92f));
    g.fillRoundedRectangle (r, 8.0f * s);
    juce::Path p;
    p.addRoundedRectangle (r.reduced (0.5f), 8.0f * s);
    glowStroke (g, p, pal.orange.withAlpha (0.75f), 1.0f * s);
    DotFont::draw (g, title, juce::Rectangle<float> (r.getX() + 10.0f * s, r.getY() + 5.0f * s, r.getWidth(), 9.0f * s), 1.15f * s, pal.orange, juce::Justification::centredLeft, 0.6f);
}

void MirageView::drawLevels (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto in = r.reduced (10.0f * s).withTrimmedTop (12.0f * s).withTrimmedBottom (12.0f * s);
    const int segs = 22;
    const float segH = in.getHeight() / (float) segs;
    auto bar = [&] (juce::Rectangle<float> col, float peak, float hold)
    {
        const float lvl = juce::jlimit (0.0f, 1.0f, (Analyzer::toDb (peak) + 54.0f) / 54.0f);
        const int holdSeg = (int) (juce::jlimit (0.0f, 1.0f, (Analyzer::toDb (hold) + 54.0f) / 54.0f) * (float) segs);
        for (int i = 0; i < segs; ++i)
        {
            const float f = (float) i / (float) (segs - 1);
            const auto seg = juce::Rectangle<float> (col.getX(), col.getBottom() - (float) (i + 1) * segH, col.getWidth(), segH * 0.68f);
            const bool on = f <= lvl || i == holdSeg - 1;
            g.setColour (on ? pal.colour (0.55f + 0.42f * f) : pal.dim.withAlpha (0.35f));
            g.fillRect (seg);
        }
    };
    const float cw = in.getWidth() * 0.32f;
    bar (in.withWidth (cw), an.peakL, an.holdL);
    bar (in.withX (in.getX() + in.getWidth() * 0.4f).withWidth (cw), an.peakR, an.holdR);
    DotFont::draw (g, "L", in.withWidth (cw).withY (in.getBottom() + 2.0f * s).withHeight (9.0f * s), 1.0f * s, pal.red, juce::Justification::centred, 0.4f);
    DotFont::draw (g, "R", in.withX (in.getX() + in.getWidth() * 0.4f).withWidth (cw).withY (in.getBottom() + 2.0f * s).withHeight (9.0f * s), 1.0f * s, pal.red, juce::Justification::centred, 0.4f);
    // Scale ticks with the loudest reading.
    const auto text = dbText (std::max (an.holdL, an.holdR));
    DotFont::draw (g, text, juce::Rectangle<float> (in.getRight() - in.getWidth() * 0.22f, in.getY(), in.getWidth() * 0.22f, 10.0f * s), 0.9f * s, pal.orange, juce::Justification::centredRight, 0.4f);
}

void MirageView::drawScope (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto in = r.reduced (10.0f * s).withTrimmedTop (10.0f * s);
    g.setColour (pal.red.withAlpha (0.25f));
    g.drawHorizontalLine ((int) in.getCentreY(), in.getX(), in.getRight());
    for (int i = 1; i < 6; ++i)
    {
        const float x = in.getX() + in.getWidth() * (float) i / 6.0f;
        g.fillRect (x - 2.0f * s, in.getCentreY() - 0.5f * s, 4.0f * s, 1.0f * s);
        g.fillRect (x - 0.5f * s, in.getCentreY() - 2.0f * s, 1.0f * s, 4.0f * s);
    }
    // Trigger on a rising zero crossing so the trace stands still on steady tones.
    const auto& L = an.recentL;
    const auto& Rr = an.recentR;
    const int span = 1024;
    int start = Analyzer::kRecent - span - 1;
    for (int i = Analyzer::kRecent - span - 1; i > Analyzer::kRecent - span - 1200 && i > 1; --i)
        if (L[(size_t) (i - 1)] < 0.0f && L[(size_t) i] >= 0.0f) { start = i; break; }
    const float peak = std::max (0.05f, std::max (an.peakL, an.peakR));
    for (int ch = 1; ch >= 0; --ch)
    {
        const auto& src = ch == 0 ? L : Rr;
        juce::Path p;
        for (int i = 0; i < span; i += 2)
        {
            const float x = in.getX() + in.getWidth() * (float) i / (float) span;
            const float y = in.getCentreY() - src[(size_t) (start + i)] / peak * in.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        glowStroke (g, p, ch == 0 ? pal.orange : pal.red.withAlpha (0.8f), 1.0f * s);
    }
}

void MirageView::drawWave (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto in = r.reduced (10.0f * s).withTrimmedTop (10.0f * s);
    const int cols = (int) (in.getWidth() / (2.0f * s));
    const float colW = in.getWidth() / (float) std::max (1, cols);
    const float mid = in.getCentreY(), half = in.getHeight() * 0.5f;
    constexpr int buckets = 12;
    std::array<juce::RectangleList<float>, buckets> lists;
    for (int i = 0; i < cols; ++i)
    {
        const int idx = (an.historyHead - cols + i + Analyzer::kHistory * 4) % Analyzer::kHistory;
        const auto& c = an.history[(size_t) idx];
        const float top = std::max (c.maxL, c.maxR), bottom = std::min (c.minL, c.minR);
        const float amp = std::max (top, -bottom);
        const float age = (float) i / (float) cols;
        const int b = juce::jlimit (0, buckets - 1, (int) (std::sqrt (amp) * (0.5f + 0.5f * age) * (float) buckets));
        lists[(size_t) b].addWithoutMerging ({ in.getX() + (float) i * colW, mid - top * half, std::max (1.0f, colW * 0.7f), std::max (1.0f, (top - bottom) * half) });
    }
    for (int b = 0; b < buckets; ++b)
    {
        g.setColour (pal.colour (0.38f + 0.52f * ((float) b + 0.5f) / (float) buckets));
        g.fillRectList (lists[(size_t) b]);
    }
    // Now line at the right edge, time ticks along the bottom.
    g.setColour (pal.orange.withAlpha (0.8f));
    g.fillRect (in.getRight() - 1.0f * s, in.getY(), 1.5f * s, in.getHeight());
    g.setColour (pal.red.withAlpha (0.4f));
    for (float x = in.getRight(); x > in.getX(); x -= 60.0f * s)
        g.fillRect (x, in.getBottom() - 3.0f * s, 1.0f * s, 3.0f * s);
}

void MirageView::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto in = r.reduced (10.0f * s).withTrimmedTop (10.0f * s);
    constexpr int n = Analyzer::kLed, segs = 14;
    const float cw = in.getWidth() / (float) n, sh = in.getHeight() / (float) segs;
    std::array<juce::RectangleList<float>, segs> lit;
    juce::RectangleList<float> off;
    for (int i = 0; i < n; ++i)
    {
        const int on = (int) std::round (an.led[(size_t) i] * (float) segs);
        const int peak = (int) std::round (an.ledPeak[(size_t) i] * (float) segs);
        for (int j = 0; j < segs; ++j)
        {
            const juce::Rectangle<float> seg (in.getX() + (float) i * cw, in.getBottom() - (float) (j + 1) * sh, cw * 0.72f, sh * 0.62f);
            (j < on || j == peak - 1 ? lit[(size_t) j] : off).addWithoutMerging (seg);
        }
    }
    g.setColour (pal.dim.withAlpha (0.25f));
    g.fillRectList (off);
    for (int j = 0; j < segs; ++j)
    {
        g.setColour (pal.colour (0.5f + 0.45f * (float) j / (float) (segs - 1)));
        g.fillRectList (lit[(size_t) j]);
    }
}

void MirageView::drawReticle (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto in = r.reduced (8.0f * s).withTrimmedTop (10.0f * s);
    const float rad = std::min (in.getWidth(), in.getHeight()) * 0.46f;
    const auto c = in.getCentre();
    const float t = (float) an.time;
    juce::Path ring;
    for (int i = 0; i < 4; ++i)
    {
        const float a0 = -t * 0.6f + (float) i * juce::MathConstants<float>::halfPi;
        ring.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a0 + 1.1f, true);
    }
    glowStroke (g, ring, pal.orange.withAlpha (0.8f), 1.0f * s);
    g.setColour (pal.red.withAlpha (0.5f));
    g.drawEllipse (juce::Rectangle<float> (rad * 1.5f, rad * 1.5f).withCentre (c), 1.0f * s);
    g.drawLine (c.x - rad, c.y, c.x + rad, c.y, 0.6f * s);
    g.drawLine (c.x, c.y - rad, c.x, c.y + rad, 0.6f * s);
    // Goniometer: the stereo field as dots (mono stands up, wide spreads out).
    const float scale = rad * 0.7f / std::max (0.05f, std::max (an.peakL, an.peakR));
    juce::RectangleList<float> dots;
    for (int i = Analyzer::kRecent - 768; i < Analyzer::kRecent; i += 3)
    {
        const float l = an.recentL[(size_t) i], rr = an.recentR[(size_t) i];
        const float x = (rr - l) * 0.7071f * scale, y = -(l + rr) * 0.7071f * scale;
        if (x * x + y * y < rad * rad)
            dots.addWithoutMerging ({ c.x + x, c.y + y, 1.4f * s, 1.4f * s });
    }
    g.setColour (pal.orange.withAlpha (0.75f));
    g.fillRectList (dots);
    // The core glows with the energy.
    const float core = rad * (0.18f + 0.22f * an.energy);
    g.setGradientFill (juce::ColourGradient (pal.colour (0.7f + 0.25f * an.hit).withAlpha (0.8f), c, pal.red.withAlpha (0.0f), c.translated (core, 0.0f), true));
    g.fillEllipse (juce::Rectangle<float> (core * 2.0f, core * 2.0f).withCentre (c));
}

void MirageView::drawThermalHud (juce::Graphics& g)
{
    const auto t = thermal;
    DotFont::draw (g, "THERMAL", juce::Rectangle<float> (t.getX() + 10.0f * s, t.getY() + 5.0f * s, 120.0f * s, 9.0f * s), 1.15f * s, pal.orange, juce::Justification::centredLeft, 0.7f);
    static const float marks[] { 60.0f, 250.0f, 1000.0f, 4000.0f, 12000.0f };
    static const char* labels[] { "60", "250", "1K", "4K", "12K" };
    for (int i = 0; i < 5; ++i)
    {
        const float f = std::log (marks[i] / 30.0f) / std::log (16000.0f / 30.0f);
        const float y = t.getBottom() - f * t.getHeight();
        g.setColour (pal.orange.withAlpha (0.5f));
        g.fillRect (t.getRight() - 8.0f * s, y, 6.0f * s, 1.0f * s);
        DotFont::draw (g, labels[i], juce::Rectangle<float> (t.getRight() - 46.0f * s, y - 5.0f * s, 34.0f * s, 10.0f * s), 0.9f * s, pal.orange, juce::Justification::centredRight, 0.5f);
    }
    const float cx = t.getX() + t.getWidth() * scan;
    g.setColour (pal.orange.withAlpha (0.25f));
    g.fillRect (cx, t.getY(), 1.0f * s, t.getHeight());
}

//==============================================================================
void MirageView::showPresetMenu()
{
    auto& pm = proc.presets;
    juce::PopupMenu m;
    m.setLookAndFeel (&look);
    const int cur = pm.getCurrentIndex();
    for (auto& cat : pm.getCategories())
    {
        const auto idx = pm.getIndicesInCategory (cat);
        if (idx.isEmpty())
            continue;
        m.addSectionHeader (cat);
        for (int i : idx)
            m.addItem (i + 1, pm.get (i).name, true, i == cur);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameKey),
                     [safe = juce::Component::SafePointer<MirageView> (this)] (int r)
                     {
                         if (safe != nullptr && r > 0)
                             safe->proc.presets.loadPreset (r - 1);
                     });
}

void MirageView::showChoiceMenu (const char* paramId, juce::Component& target)
{
    auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (paramId));
    if (p == nullptr)
        return;
    juce::PopupMenu m;
    m.setLookAndFeel (&look);
    for (int i = 0; i < p->choices.size(); ++i)
        m.addItem (i + 1, p->choices[i], true, i == p->getIndex());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                     [safe = juce::Component::SafePointer<MirageView> (this), p] (int r)
                     {
                         if (safe == nullptr || r <= 0)
                             return;
                         p->beginChangeGesture();
                         p->setValueNotifyingHost (p->convertTo0to1 ((float) (r - 1)));
                         p->endChangeGesture();
                     });
}

void MirageView::openHueWheel()
{
    if (wheelBox != nullptr)
    {
        wheelBox->dismiss();
        return;
    }
    const int size = juce::jlimit (150, 280, juce::roundToInt (200.0f * s));
    auto content = std::make_unique<HueWheel> (*proc.apvts.getParameter ("hue"), size);
    wheelBox = &juce::CallOutBox::launchAsynchronously (std::move (content), hueDot.getBounds(), this);
}
} // namespace mirage
