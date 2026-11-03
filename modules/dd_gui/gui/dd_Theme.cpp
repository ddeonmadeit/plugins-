#if __has_include(<DDFonts.h>)
 #include <DDFonts.h>
 #define DD_HAS_EMBEDDED_FONTS 1
#else
 #define DD_HAS_EMBEDDED_FONTS 0
#endif

namespace dd::gui
{
namespace
{
    struct EmbeddedFonts
    {
        juce::Typeface::Ptr medium, semiBold, heavy;

        EmbeddedFonts()
        {
           #if DD_HAS_EMBEDDED_FONTS
            medium = juce::Typeface::createSystemTypefaceFor (DDFonts::BarlowCondensedMedium_ttf, (size_t) DDFonts::BarlowCondensedMedium_ttfSize);
            semiBold = juce::Typeface::createSystemTypefaceFor (DDFonts::BarlowCondensedSemiBold_ttf, (size_t) DDFonts::BarlowCondensedSemiBold_ttfSize);
            heavy = juce::Typeface::createSystemTypefaceFor (DDFonts::BarlowCondensedBold_ttf, (size_t) DDFonts::BarlowCondensedBold_ttfSize);
           #endif
        }
    };

    const EmbeddedFonts& fonts()
    {
        static const EmbeddedFonts f;
        return f;
    }

    /** Soft-clumped noise, so it reads as film grain rather than single-pixel digital noise. */
    const juce::Image& grainImage()
    {
        static const juce::Image image = []
        {
            constexpr int n = 192;
            std::vector<float> v ((size_t) (n * n));
            juce::Random rnd (0x5eed);
            for (auto& x : v)
                x = rnd.nextFloat() + rnd.nextFloat() - 1.0f;

            auto at = [&] (int x, int y) { return v[(size_t) (((y + n) % n) * n + (x + n) % n)]; };
            juce::Image im (juce::Image::ARGB, n, n, true, juce::SoftwareImageType());
            juce::Image::BitmapData bd (im, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                {
                    const float s = 0.5f * at (x, y) + 0.125f * (at (x + 1, y) + at (x - 1, y) + at (x, y + 1) + at (x, y - 1));
                    const float a = std::min (1.0f, std::abs (s) * 0.16f);
                    // Mostly dark specks with a few warm light ones: matte, not dusty.
                    bd.setPixelColour (x, y, s > 0.0f ? juce::Colour (0xfff0d8b8).withAlpha (a * 0.45f)
                                                      : juce::Colours::black.withAlpha (a * 1.25f));
                }
            return im;
        }();
        return image;
    }


    /** Horizontal brushing: long streaks of light and dark, periodic across x so the tile wraps cleanly. */
    const juce::Image& brushedImage()
    {
        static const juce::Image image = []
        {
            constexpr int w = 512, h = 256, k = 18;
            juce::Random rnd (0xb105);
            std::vector<float> row ((size_t) w), smooth ((size_t) w), prev ((size_t) w, 0.0f);
            juce::Image im (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
            juce::Image::BitmapData bd (im, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
            {
                for (auto& v : row)
                    v = rnd.nextFloat() * 2.0f - 1.0f;
                float acc = 0.0f;
                for (int i = -k; i <= k; ++i)
                    acc += row[(size_t) ((i + w) % w)];
                for (int x = 0; x < w; ++x)
                {
                    smooth[(size_t) x] = acc / (float) (2 * k + 1);
                    acc += row[(size_t) ((x + k + 1) % w)] - row[(size_t) ((x - k + w) % w)];
                }
                const float depth = 0.5f + rnd.nextFloat() * (rnd.nextFloat() < 0.04f ? 2.2f : 0.9f);   // the odd deeper scratch
                for (int x = 0; x < w; ++x)
                {
                    float v = smooth[(size_t) x] * 3.4f * depth + row[(size_t) x] * 0.14f;
                    v = 0.7f * v + 0.3f * prev[(size_t) x];
                    prev[(size_t) x] = v;
                    const float a = std::min (1.0f, std::abs (v));
                    // Warm-tinted streaks keep the metal's colour instead of greying it.
                    bd.setPixelColour (x, y, v > 0.0f ? juce::Colour (0xfffff0dc).withAlpha (a * 0.34f)
                                                      : juce::Colour (0xff140a04).withAlpha (a * 0.5f));
                }
            }
            return im;
        }();
        return image;
    }

    /** Spun (lathe-turned) silver for knob tops: concentric tool marks reflect the light as a bright bow-tie
        through the centre, pointing at the light and away from it. Supersampled, cached per size. */
    const juce::Image& spunSilver (int d)
    {
        static std::map<int, juce::Image> cache;
        if (auto it = cache.find (d); it != cache.end())
            return it->second;

        juce::Image im (juce::Image::ARGB, d, d, true, juce::SoftwareImageType());
        juce::Random rnd (0x5917 + d);
        std::vector<float> rings ((size_t) d + 1);
        float drift = 0.0f;
        for (auto& v : rings)
        {
            drift = 0.6f * drift + 0.4f * (rnd.nextFloat() * 2.0f - 1.0f);
            v = drift + 0.5f * (rnd.nextFloat() * 2.0f - 1.0f);
        }
        const float half = (float) d * 0.5f;
        const float toLight = -2.3f;   // up and to the left (y grows downwards)
        juce::Image::BitmapData bd (im, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < d; ++y)
            for (int x = 0; x < d; ++x)
            {
                const float dx = ((float) x + 0.5f - half) / half, dy = ((float) y + 0.5f - half) / half;
                const float r = std::sqrt (dx * dx + dy * dy);
                if (r > 1.0f + 1.0f / half)
                    continue;
                const float th = std::atan2 (dy, dx);
                const float a = std::abs (std::cos (th - toLight));
                const float fan = 0.42f * std::pow (a, 14.0f) + 0.2f * std::pow (a, 3.0f);
                float v = 0.6f - 0.1f * dy + fan + 0.03f * rings[(size_t) std::min (d, (int) (r * (float) d * 0.5f))];
                if (r > 0.9f)
                    v *= 1.0f - (r - 0.9f) * 1.6f;
                v = juce::jlimit (0.0f, 1.0f, v);
                const float alpha = juce::jlimit (0.0f, 1.0f, (1.0f - r) * half + 0.5f);
                bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (v * 0.975f, v * 0.985f, v, alpha));
            }
        return cache.emplace (d, im).first->second;
    }

    const juce::Colour kInk { 0xff1a0f08 };         // dark text on lit buttons
    const juce::Colour kSilverInk { 0xff26282c };   // engraving on bare silver parts

    /** Machined silver: light top, darker lower half, as polished aluminium under a top light. */
    juce::ColourGradient silverGradient (juce::Rectangle<float> r, bool bright = false)
    {
        juce::ColourGradient gr (juce::Colour (bright ? 0xfff4f5f6 : 0xffe9ebed), 0.0f, r.getY(),
                                 juce::Colour (0xff86898e), 0.0f, r.getBottom(), false);
        gr.addColour (0.45, juce::Colour (0xffc3c6ca));
        gr.addColour (0.55, juce::Colour (0xffa6a9ae));
        return gr;
    }
}

//==============================================================================
LookAndFeel::LookAndFeel (Palette p) : pal (p)
{
    const juce::Colour menuGlass (0xff17110d);
    setColour (juce::ResizableWindow::backgroundColourId, pal.background);
    setColour (juce::ComboBox::backgroundColourId, pal.track);
    setColour (juce::ComboBox::textColourId, pal.glass);
    setColour (juce::ComboBox::outlineColourId, pal.panelEdge);
    setColour (juce::ComboBox::arrowColourId, pal.glass.withAlpha (0.75f));
    setColour (juce::PopupMenu::backgroundColourId, menuGlass);
    setColour (juce::PopupMenu::textColourId, pal.screenText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pal.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, kInk);
    setColour (juce::PopupMenu::headerTextColourId, pal.glass);
    setColour (juce::Label::textColourId, pal.text);
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xffc9ccd0));
    setColour (juce::TextButton::buttonOnColourId, pal.accent);
    setColour (juce::TextButton::textColourOffId, kSilverInk);
    setColour (juce::TextButton::textColourOnId, pal.glass);
    setColour (juce::TextEditor::backgroundColourId, pal.track);
    setColour (juce::TextEditor::textColourId, pal.glass);
    setColour (juce::TextEditor::highlightColourId, pal.accent.withAlpha (0.4f));
    setColour (juce::TextEditor::outlineColourId, pal.panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, pal.accent);
    setColour (juce::CaretComponent::caretColourId, pal.glass);
    setColour (juce::AlertWindow::backgroundColourId, pal.panel);
    setColour (juce::AlertWindow::textColourId, pal.text);
    setColour (juce::AlertWindow::outlineColourId, pal.panelEdge);
    setColour (juce::Slider::textBoxTextColourId, pal.text);
    setColour (juce::TooltipWindow::backgroundColourId, menuGlass);
    setColour (juce::TooltipWindow::textColourId, pal.screenText);
    setColour (juce::TooltipWindow::outlineColourId, pal.accent.withAlpha (0.6f));
}

juce::Font LookAndFeel::font (float height, bool bold)
{
    const auto& f = fonts();
    if (auto tf = bold ? f.semiBold : f.medium)
        return juce::Font (juce::FontOptions (tf).withHeight (height * 1.18f));   // condensed face: a little taller reads the same
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font LookAndFeel::display (float height)
{
    if (auto tf = fonts().heavy)
        return juce::Font (juce::FontOptions (tf).withHeight (height * 1.18f));
    return juce::Font (juce::FontOptions (height, juce::Font::bold));
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f - 1.0f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    const bool enabled = s.isEnabled();

    // Bipolar parameters light their scale from the centre.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
    const float fromAngle = startAngle + zeroPos * (endAngle - startAngle);

    // Scale: a slot machined through the plate with a lamp behind it. The cut's lower lip shows bare
    // metal; the lit part of the slot glows and spills a little warm light onto the plate.
    const float arcW = std::max (2.2f, radius * 0.09f);
    const float arcR = radius - arcW * 0.5f - 0.5f;
    const auto stroke = [&] (const juce::Path& p, float width, juce::Colour colour, float dy = 0.0f)
    {
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                      juce::AffineTransform::translation (0.0f, dy));
    };
    juce::Path slot;
    slot.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    stroke (slot, arcW + 1.4f, juce::Colour (0xffd8d0c4).withAlpha (0.3f), 0.8f);
    stroke (slot, arcW + 0.6f, juce::Colours::black.withAlpha (0.55f), -0.4f);
    stroke (slot, arcW, pal.track);

    const auto col = enabled ? accent : pal.dim;
    if (std::abs (angle - fromAngle) > 0.001f)
    {
        juce::Path lit;
        lit.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, std::min (fromAngle, angle), std::max (fromAngle, angle), true);
        stroke (lit, arcW * 4.4f, col.withAlpha (0.08f));
        stroke (lit, arcW * 2.4f, col.withAlpha (0.18f));
        stroke (lit, arcW * 0.95f, col);
        stroke (lit, arcW * 0.35f, col.brighter (0.7f).withAlpha (0.9f));
    }
    else if (bipolar)
    {
        const juce::Point<float> d (c.x + std::sin (fromAngle) * arcR, c.y - std::cos (fromAngle) * arcR);
        g.setColour (col.withAlpha (0.25f));
        g.fillEllipse (d.x - arcW * 1.4f, d.y - arcW * 1.4f, arcW * 2.8f, arcW * 2.8f);
        g.setColour (col);
        g.fillEllipse (d.x - arcW * 0.45f, d.y - arcW * 0.45f, arcW * 0.9f, arcW * 0.9f);
    }

    // Knob: machined silver. Soft shadow on the plate below it, knurled skirt, a polished chamfer and a
    // spun top that keeps its reflection still while the knob turns (the pointer is what moves).
    const float skirtR = arcR - arcW * 1.75f;
    const float faceR = skirtR * 0.8f;
    for (int i = 6; i >= 1; --i)
    {
        const float sr = skirtR + (float) i * 0.7f;
        g.setColour (juce::Colours::black.withAlpha (0.09f));
        g.fillEllipse (c.x - sr, c.y - sr + skirtR * 0.12f, sr * 2.0f, sr * 2.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - skirtR - 0.6f, c.y - skirtR + 0.4f, (skirtR + 0.6f) * 2.0f, (skirtR + 0.6f) * 2.0f);

    const auto skirt = juce::Rectangle<float> (skirtR * 2.0f, skirtR * 2.0f).withCentre (c);
    g.setGradientFill (silverGradient (skirt));
    g.fillEllipse (skirt);
    const int ridges = std::max (36, (int) (skirtR * 2.4f));
    for (int i = 0; i < ridges; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / (float) ridges;
        const float sn = std::sin (a), cs = std::cos (a);
        // Ridges facing up and left catch the light; the knurl reads as alternating bright and dark teeth.
        const float facing = 0.5f + 0.5f * (cs * 0.8f - sn * 0.35f);
        g.setColour (i % 2 == 0 ? juce::Colours::white.withAlpha (0.12f + 0.45f * facing)
                                : juce::Colours::black.withAlpha (0.18f + 0.25f * (1.0f - facing)));
        g.drawLine (c.x + sn * faceR * 1.03f, c.y - cs * faceR * 1.03f, c.x + sn * (skirtR - 0.4f), c.y - cs * (skirtR - 0.4f), 0.9f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (skirt, 0.8f);

    // Polished chamfer between skirt and top: bright where it faces the light, dark opposite.
    const float chamW = std::max (1.2f, skirtR * 0.07f);
    const auto cham = juce::Rectangle<float> ((faceR + chamW * 0.5f) * 2.0f, (faceR + chamW * 0.5f) * 2.0f).withCentre (c);
    juce::ColourGradient chamGrad (juce::Colour (0xfffbfbfb), c.x - faceR, c.y - faceR, juce::Colour (0xff4a4d52), c.x + faceR, c.y + faceR, false);
    chamGrad.addColour (0.5, juce::Colour (0xffa9acb1));
    g.setGradientFill (chamGrad);
    g.drawEllipse (cham, chamW);

    const auto face = juce::Rectangle<float> (faceR * 2.0f, faceR * 2.0f).withCentre (c);
    const int px = std::max (16, juce::roundToInt (faceR * 4.0f));
    g.drawImage (spunSilver (px), face, juce::RectanglePlacement::stretchToFit);

    // Pointer: a groove cut across the top and down the skirt, filled black; its lower lip catches light.
    const float pIn = faceR * 0.18f, pOut = skirtR * 0.97f;
    const juce::Point<float> a (c.x + std::sin (angle) * pIn, c.y - std::cos (angle) * pIn);
    const juce::Point<float> b (c.x + std::sin (angle) * pOut, c.y - std::cos (angle) * pOut);
    const float pw = std::max (1.7f, radius * 0.085f);
    g.setColour (juce::Colours::white.withAlpha (0.65f));
    g.drawLine ({ a.translated (0.0f, 0.9f), b.translated (0.0f, 0.9f) }, pw);
    g.setColour (juce::Colour (0xff121315));
    g.drawLine ({ a, b }, pw);

    if (! enabled)
    {
        g.setColour (pal.panel.withAlpha (0.45f));
        g.fillEllipse (skirt);
    }
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat();
    paintInset (g, r, pal, 4.0f);
    // The display's own light: a faint warm haze behind the lettering.
    const auto glassArea = r.reduced (3.0f);
    juce::ColourGradient haze (pal.glass.withAlpha (0.07f), glassArea.getX() + glassArea.getWidth() * 0.3f, glassArea.getCentreY(),
                               pal.glass.withAlpha (0.0f), glassArea.getRight(), glassArea.getCentreY(), true);
    g.setGradientFill (haze);
    g.fillRoundedRectangle (glassArea, 3.0f);
    if (box.hasKeyboardFocus (true))
    {
        g.setColour (pal.accent.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (1.5f), 4.0f, 1.0f);
    }

    const float aw = 7.0f, ah = 4.0f;
    const float ax = (float) w - 11.0f, ay = (float) h * 0.5f - ah * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (ax - aw * 0.5f, ay, ax + aw * 0.5f, ay, ax, ay + ah);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return font (std::min (14.0f, (float) box.getHeight() * 0.56f));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (7, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setMinimumHorizontalScale (0.6f);
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Font LookAndFeel::getPopupMenuFont() { return font (15.0f); }

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h);
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    paintGrain (g, r, 0.6f);
    paintGlassReflection (g, r.toFloat(), 0.0f);
    g.setColour (juce::Colour (0xffb9b1a6).withAlpha (0.55f));
    g.drawRect (r, 1);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    constexpr float corner = 4.0f;

    // A machined silver key sitting in a cut-out: the cut's dark gap, its lower lip catching light.
    g.setColour (juce::Colour (0xffd8d0c4).withAlpha (0.28f));
    g.fillRoundedRectangle (r.translated (0.0f, 0.8f), corner + 0.5f);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillRoundedRectangle (r.withTrimmedBottom (0.8f), corner + 0.5f);

    auto cap = r.reduced (1.3f).withTrimmedBottom (1.6f);
    if (down)
        cap = cap.translated (0.0f, 0.9f);

    auto grad = silverGradient (cap, over && ! down);
    if (down)
    {
        grad = juce::ColourGradient (juce::Colour (0xff9da0a5), 0.0f, cap.getY(), juce::Colour (0xffc7cace), 0.0f, cap.getBottom(), false);
    }
    g.setGradientFill (grad);
    g.fillRoundedRectangle (cap, corner);
    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (cap, corner);
        g.reduceClipRegion (clip);
        g.setTiledImageFill (brushedImage(), (int) cap.getX(), (int) cap.getY(), 0.35f);
        g.fillRect (cap);
        if (on)
        {
            // Backlit key: warm light diffusing through the legend window.
            const auto lamp = b.findColour (juce::TextButton::buttonOnColourId);
            juce::ColourGradient glow (lamp.withAlpha (0.35f), cap.getCentreX(), cap.getCentreY(),
                                       lamp.withAlpha (0.0f), cap.getRight(), cap.getCentreY(), true);
            g.setGradientFill (glow);
            g.fillRect (cap);
        }
    }
    g.setColour (juce::Colours::white.withAlpha (down ? 0.25f : 0.85f));
    g.drawHorizontalLine ((int) cap.getY(), cap.getX() + corner, cap.getRight() - corner);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawHorizontalLine ((int) cap.getBottom() - 1, cap.getX() + corner, cap.getRight() - corner);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const auto f = getTextButtonFont (b, b.getHeight());
    const auto area = b.getLocalBounds().reduced (4, 2).translated (0, down ? 0 : -1).toFloat();
    if (b.getToggleState())
        drawGlowing (g, b.getButtonText(), area, b.findColour (juce::TextButton::textColourOnId), f);
    else
        drawPrinted (g, b.getButtonText(), area, b.findColour (juce::TextButton::textColourOffId), f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return font (std::min (14.0f, (float) h * 0.55f), true).withExtraKerningFactor (0.04f);
}

void LookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    const auto r = alert.getLocalBounds().toFloat();
    paintBrushedMetal (g, r, pal.panel, 0.0f);
    g.setColour (juce::Colour (0xffd8d0c4).withAlpha (0.45f));
    g.drawRect (r, 1.0f);
    g.setColour (alert.findColour (juce::AlertWindow::textColourId));
    layout.draw (g, textArea.withTrimmedLeft (12).toFloat());
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const int w = juce::jmin (360, (int) juce::GlyphArrangement::getStringWidth (font (14.0f), text) + 22);
    const int h = 26;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 24,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin (parent);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h);
    g.fillAll (findColour (juce::TooltipWindow::backgroundColourId));
    g.setColour (findColour (juce::TooltipWindow::outlineColourId));
    g.drawRect (r, 1);
    g.setColour (findColour (juce::TooltipWindow::textColourId));
    g.setFont (font (14.0f));
    g.drawFittedText (text, r.reduced (10, 2), juce::Justification::centredLeft, 1);
}

//==============================================================================
void paintGrain (juce::Graphics& g, juce::Rectangle<int> area, float amount)
{
    g.setTiledImageFill (grainImage(), area.getX(), area.getY(), juce::jlimit (0.0f, 1.0f, amount));
    g.fillRect (area);
}

void paintBrushedMetal (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, float corner, float brushing)
{
    juce::Graphics::ScopedSaveState state (g);
    if (corner > 0.0f)
    {
        juce::Path clip;
        clip.addRoundedRectangle (r, corner);
        g.reduceClipRegion (clip);
    }
    else
    {
        g.reduceClipRegion (r.getSmallestIntegerContainer());
    }

    // Lit from above: brighter at the top, a soft fall-off below (hue and saturation kept: lit metal of
    // a colour is that colour, brighter, not paler).
    juce::ColourGradient body (base.withMultipliedBrightness (1.32f), 0.0f, r.getY(), base.withMultipliedBrightness (0.72f), 0.0f, r.getBottom(), false);
    body.addColour (0.35, base.withMultipliedBrightness (1.08f));
    g.setGradientFill (body);
    g.fillRect (r);

    // Brushing: long horizontal tool marks.
    g.setTiledImageFill (brushedImage(), juce::roundToInt (r.getX()), juce::roundToInt (r.getY()), juce::jlimit (0.0f, 1.0f, 0.55f * brushing));
    g.fillRect (r);

    // Brushed metal smears a light into a band across the brushing: a soft vertical sheen left of
    // centre, with a tighter, brighter core (this is what makes it read as metal, not paint).
    const float bx = r.getX() + r.getWidth() * 0.32f, bw = std::max (60.0f, r.getWidth() * 0.34f);
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), bx - bw, 0.0f, juce::Colours::white.withAlpha (0.0f), bx + bw, 0.0f, false);
    sheen.addColour (0.3, juce::Colour (0xffffe2c2).withAlpha (0.05f));
    sheen.addColour (0.46, juce::Colour (0xffffe9d2).withAlpha (0.13f));
    sheen.addColour (0.5, juce::Colour (0xfffff3e4).withAlpha (0.2f));
    sheen.addColour (0.54, juce::Colour (0xffffe9d2).withAlpha (0.13f));
    sheen.addColour (0.7, juce::Colour (0xffffe2c2).withAlpha (0.05f));
    g.setGradientFill (sheen);
    g.fillRect (r);
    // A second, fainter reflection of the room further right.
    const float bx2 = r.getX() + r.getWidth() * 0.78f, bw2 = bw * 0.6f;
    juce::ColourGradient sheen2 (juce::Colours::white.withAlpha (0.0f), bx2 - bw2, 0.0f, juce::Colours::white.withAlpha (0.0f), bx2 + bw2, 0.0f, false);
    sheen2.addColour (0.5, juce::Colour (0xffffe9d2).withAlpha (0.08f));
    g.setGradientFill (sheen2);
    g.fillRect (r);
}

void paintBackdrop (juce::Graphics& g, juce::Rectangle<int> area, const Palette& pal)
{
    const auto r = area.toFloat();
    paintBrushedMetal (g, r, pal.background, 0.0f, 0.8f);

    juce::ColourGradient vignette (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                   juce::Colours::black.withAlpha (0.5f), r.getX(), r.getY(), true);
    vignette.addColour (0.6, juce::Colours::transparentBlack);
    g.setGradientFill (vignette);
    g.fillRect (area);
}

void paintFaceplate (juce::Graphics& g, juce::Rectangle<float> r, const Palette& pal, bool screws)
{
    constexpr float corner = 5.0f;
    // The plate stands off the chassis: a soft shadow falls below it.
    for (int i = 5; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.11f));
        g.fillRoundedRectangle (r.expanded ((float) i * 0.8f).translated (0.0f, (float) i * 1.2f), corner + (float) i);
    }
    g.setColour (pal.panelEdge);
    g.fillRoundedRectangle (r, corner + 1.0f);

    const auto plate = r.reduced (1.0f);
    paintBrushedMetal (g, plate, pal.panel, corner);

    // Machined edges: the cut through the anodising shows bright metal along the top and left,
    // the lower edge turns away from the light.
    juce::Path edge;
    edge.addRoundedRectangle (plate.reduced (0.5f), corner);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff3ece2).withAlpha (0.75f), 0.0f, plate.getY(),
                                             juce::Colour (0xffd8cbbb).withAlpha (0.1f), 0.0f, plate.getY() + std::min (40.0f, plate.getHeight() * 0.5f), false));
    g.strokePath (edge, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawLine (plate.getX() + corner, plate.getBottom() - 0.5f, plate.getRight() - corner, plate.getBottom() - 0.5f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (plate.getX() + corner, plate.getY() + 1.8f, plate.getRight() - corner, plate.getY() + 1.8f, 1.0f);

    if (screws)
    {
        const float in = 7.0f;
        paintScrew (g, { plate.getX() + in, plate.getY() + in }, 3.2f, 0.6f);
        paintScrew (g, { plate.getRight() - in, plate.getY() + in }, 3.2f, 2.1f);
        paintScrew (g, { plate.getX() + in, plate.getBottom() - in }, 3.2f, 1.2f);
        paintScrew (g, { plate.getRight() - in, plate.getBottom() - in }, 3.2f, 0.2f);
    }
}

void paintInset (juce::Graphics& g, juce::Rectangle<float> r, const Palette& pal, float corner)
{
    // A window milled into the plate with a chamfered edge: the chamfer below the window faces the light
    // and shows bright metal, the one above faces down into shadow.
    juce::ColourGradient chamfer (juce::Colour (0xff120d0a), 0.0f, r.getY(), juce::Colour (0xffa99d8f), 0.0f, r.getBottom(), false);
    chamfer.addColour (0.5, juce::Colour (0xff4a403a));
    g.setGradientFill (chamfer);
    g.fillRoundedRectangle (r, corner);
    const float cw = std::min (2.0f, r.getHeight() * 0.12f);
    const auto glass = r.reduced (cw);
    g.setColour (pal.track);
    g.fillRoundedRectangle (glass, std::max (0.0f, corner - 1.0f));
    // Inner shadow: deep under the top edge, lighter down the sides.
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.75f), 0.0f, glass.getY(),
                                             juce::Colours::transparentBlack, 0.0f, glass.getY() + std::min (9.0f, glass.getHeight() * 0.45f), false));
    g.fillRoundedRectangle (glass, std::max (0.0f, corner - 1.0f));
    const float side = std::min (6.0f, glass.getWidth() * 0.1f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), glass.getX(), 0.0f,
                                             juce::Colours::transparentBlack, glass.getX() + side, 0.0f, false));
    g.fillRect (glass.withWidth (side));
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), glass.getRight(), 0.0f,
                                             juce::Colours::transparentBlack, glass.getRight() - side, 0.0f, false));
    g.fillRect (glass.withLeft (glass.getRight() - side));
    paintGlassReflection (g, glass, std::max (0.0f, corner - 1.0f));
}

void paintBezel (juce::Graphics& g, juce::Rectangle<float> window, float corner, float width)
{
    const auto outer = window.expanded (width);
    for (int i = 5; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRoundedRectangle (outer.expanded ((float) i * 0.8f).translated (0.0f, (float) i * 1.2f), corner + width + (float) i);
    }
    juce::Path ring;
    ring.addRoundedRectangle (outer, corner + width);
    ring.addRoundedRectangle (window, corner);
    ring.setUsingNonZeroWinding (false);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (ring);
        g.setGradientFill (silverGradient (outer));
        g.fillRect (outer);
        g.setTiledImageFill (brushedImage(), (int) outer.getX(), (int) outer.getY(), 0.45f);
        g.fillRect (outer);
        // A machined step halfway across the frame: a fine dark cut with light on its lower side.
        juce::Path step;
        step.addRoundedRectangle (window.expanded (width * 0.5f), corner + width * 0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.strokePath (step, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (0.0f, 0.7f));
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.strokePath (step, juce::PathStrokeType (0.8f));
        // The inner chamfer runs down into the window: dark along the top, bright along the bottom.
        juce::ColourGradient inner (juce::Colours::black.withAlpha (0.55f), 0.0f, window.getY() - 1.5f,
                                    juce::Colours::white.withAlpha (0.5f), 0.0f, window.getBottom() + 1.5f, false);
        inner.addColour (0.5, juce::Colours::transparentBlack);
        g.setGradientFill (inner);
        juce::Path lip;
        lip.addRoundedRectangle (window.expanded (1.6f), corner + 1.6f);
        g.strokePath (lip, juce::PathStrokeType (2.6f));
    }
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.drawLine (outer.getX() + corner + width, outer.getY() + 0.6f, outer.getRight() - corner - width, outer.getY() + 0.6f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    juce::Path outline;
    outline.addRoundedRectangle (outer.reduced (0.4f), corner + width);
    g.strokePath (outline, juce::PathStrokeType (0.8f));
}

void paintGlassReflection (juce::Graphics& g, juce::Rectangle<float> window, float corner)
{
    juce::Graphics::ScopedSaveState state (g);
    juce::Path clip;
    clip.addRoundedRectangle (window, corner);
    g.reduceClipRegion (clip);
    // A soft diagonal reflection of the room, strongest in the upper left, fading out with no hard edge.
    const float span = std::max (window.getWidth(), window.getHeight());
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.07f), window.getX(), window.getY(),
                                juce::Colours::white.withAlpha (0.0f), window.getX() + span * 0.55f, window.getY() + span * 0.35f, false);
    sheen.addColour (0.45, juce::Colours::white.withAlpha (0.03f));
    g.setGradientFill (sheen);
    g.fillRect (window);
}

void paintScrew (juce::Graphics& g, juce::Point<float> c, float radius, float slotAngle)
{
    // Counterbore: a dark ring with its lower edge catching light.
    g.setColour (juce::Colour (0xffd8d0c4).withAlpha (0.3f));
    g.fillEllipse (c.x - radius - 1.0f, c.y - radius - 0.4f, (radius + 1.0f) * 2.0f, (radius + 1.0f) * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillEllipse (c.x - radius - 1.0f, c.y - radius - 1.2f, (radius + 1.0f) * 2.0f, (radius + 1.0f) * 2.0f);

    // Domed silver head: a hot spot up and left, rolling off into shadow at the lower right.
    juce::ColourGradient head (juce::Colour (0xfff8f8f8), c.x - radius * 0.45f, c.y - radius * 0.5f,
                               juce::Colour (0xff55585d), c.x + radius * 0.9f, c.y + radius * 0.9f, true);
    head.addColour (0.45, juce::Colour (0xffb9bcc1));
    g.setGradientFill (head);
    g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);

    // Phillips recess: two crossed grooves, lit on their lower edges.
    for (float off : { 0.0f, juce::MathConstants<float>::halfPi })
    {
        const float a = slotAngle + off;
        const float dx = std::cos (a) * radius * 0.62f, dy = std::sin (a) * radius * 0.62f;
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawLine (c.x - dx, c.y - dy + 0.5f, c.x + dx, c.y + dy + 0.5f, radius * 0.34f);
        g.setColour (juce::Colour (0xff2a2c30));
        g.drawLine (c.x - dx, c.y - dy, c.x + dx, c.y + dy, radius * 0.34f);
    }
}

void paintLamp (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour colour, float lit)
{
    lit = juce::jlimit (0.0f, 1.0f, lit);
    if (lit > 0.0f)
    {
        // Light spilling onto the metal around the lens (kept within ~2.6 radii so a lamp near the edge of
        // its component never shows a clipped, square glow).
        const float gr = radius * 2.6f;
        juce::ColourGradient halo (colour.withAlpha (0.5f * lit), c.x, c.y, colour.withAlpha (0.0f), c.x + gr, c.y, true);
        halo.addColour (0.3, colour.withAlpha (0.2f * lit));
        g.setGradientFill (halo);
        g.fillEllipse (c.x - gr, c.y - gr, gr * 2.0f, gr * 2.0f);
    }
    // Chrome bezel: polished ring, bright on top, dark below; the lens sits slightly recessed.
    const float bezel = radius + std::max (1.4f, radius * 0.38f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - bezel - 0.6f, c.y - bezel + 0.4f, (bezel + 0.6f) * 2.0f, (bezel + 0.6f) * 2.0f);
    juce::ColourGradient chrome (juce::Colour (0xfffbfbfb), c.x, c.y - bezel, juce::Colour (0xff3c3f44), c.x, c.y + bezel, false);
    chrome.addColour (0.48, juce::Colour (0xff9fa3a8));
    chrome.addColour (0.52, juce::Colour (0xff6a6e74));
    g.setGradientFill (chrome);
    g.fillEllipse (c.x - bezel, c.y - bezel, bezel * 2.0f, bezel * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillEllipse (c.x - radius - 0.6f, c.y - radius - 0.6f, (radius + 0.6f) * 2.0f, (radius + 0.6f) * 2.0f);

    // Coloured glass: dark when off; when lit, a hot centre fading to the deep tint at the rim.
    const auto off = colour.darker (2.0f);
    const auto rim = off.interpolatedWith (colour.darker (0.3f), lit);
    const auto core = off.brighter (0.35f).interpolatedWith (colour.brighter (0.3f), lit);
    g.setGradientFill (juce::ColourGradient (core, c.x - radius * 0.1f, c.y - radius * 0.15f, rim, c.x + radius, c.y + radius, true));
    g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
    // Lens: a crisp window reflection up top, and a lit lamp's filament glowing hot in the middle.
    g.setColour (juce::Colours::white.withAlpha (0.35f + 0.25f * lit));
    g.fillEllipse (c.x - radius * 0.62f, c.y - radius * 0.7f, radius * 0.6f, radius * 0.42f);
    if (lit > 0.0f)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff3d6).withAlpha (0.8f * lit), c.x, c.y,
                                                 colour.withAlpha (0.0f), c.x + radius * 0.6f, c.y, true));
        g.fillEllipse (c.x - radius * 0.6f, c.y - radius * 0.6f, radius * 1.2f, radius * 1.2f);
    }
}

namespace
{
    template <typename DrawFn>
    void engrave (juce::Graphics& g, juce::Colour colour, DrawFn&& draw)
    {
        if (colour.getPerceivedBrightness() >= 0.45f)
        {
            // Cut through dark anodising to the bare metal: the cut's top wall throws a thin shadow,
            // its lower lip catches the light.
            g.setColour (juce::Colours::white.withAlpha (0.1f));
            draw (0.0f, 1.0f);
            g.setColour (juce::Colours::black.withAlpha (0.65f));
            draw (0.0f, -0.8f);
            g.setColour (colour);
            draw (0.0f, 0.0f);
        }
        else
        {
            // Engraved into bright silver: dark letters, light along their lower edge.
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            draw (0.0f, 0.9f);
            g.setColour (colour);
            draw (0.0f, 0.0f);
        }
    }
}

void drawPrinted (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour colour,
                  const juce::Font& f, juce::Justification just)
{
    g.setFont (f);
    engrave (g, colour, [&] (float dx, float dy) { g.drawText (text, r.translated (dx, dy), just, false); });
}

void drawPrintedFitted (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Colour colour,
                        const juce::Font& f, juce::Justification just)
{
    g.setFont (f);
    engrave (g, colour, [&] (float dx, float dy)
    {
        g.drawFittedText (text, r.toFloat().translated (dx, dy).toNearestInt(), just, 1, 0.7f);
    });
}

void drawEmbossed (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, const juce::Font& f,
                   juce::Justification just)
{
    juce::GlyphArrangement ga;
    ga.addJustifiedText (f, text, r.getX(), r.getY(), r.getWidth() + 1000.0f, juce::Justification::left);
    auto bounds = ga.getBoundingBox (0, -1, true);
    float dx = r.getX() - bounds.getX();
    if (just.testFlags (juce::Justification::horizontallyCentred)) dx += (r.getWidth() - bounds.getWidth()) * 0.5f;
    else if (just.testFlags (juce::Justification::right))          dx += r.getWidth() - bounds.getWidth();
    float dy = r.getCentreY() - bounds.getCentreY();
    if (just.testFlags (juce::Justification::bottom))   dy = r.getBottom() - bounds.getBottom();
    else if (just.testFlags (juce::Justification::top)) dy = r.getY() - bounds.getY();
    juce::Path letters;
    ga.createPath (letters);
    letters.applyTransform (juce::AffineTransform::translation (dx, dy));
    const auto lb = letters.getBounds();

    // Raised polished letters: they cast a shadow down onto the plate, their edges catch the light
    // on top, and the faces mirror the room (bright sky above a dark horizon, like chrome).
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillPath (letters, juce::AffineTransform::translation (0.0f, (float) i * 0.9f));
    }
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.strokePath (letters, juce::PathStrokeType (1.4f), juce::AffineTransform::translation (0.0f, 0.6f));
    juce::ColourGradient chrome (juce::Colour (0xfffdfcfa), 0.0f, lb.getY(), juce::Colour (0xffe1dcd4), 0.0f, lb.getBottom(), false);
    chrome.addColour (0.42, juce::Colour (0xffc9c3ba));
    chrome.addColour (0.5, juce::Colour (0xff6d6862));
    chrome.addColour (0.62, juce::Colour (0xff9d978f));
    g.setGradientFill (chrome);
    g.fillPath (letters);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.strokePath (letters, juce::PathStrokeType (0.6f), juce::AffineTransform::translation (0.0f, -0.4f));
}

void drawGlowing (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour colour,
                  const juce::Font& f, juce::Justification just)
{
    g.setFont (f);
    // Light bleeding around the lettering, then the lettering itself, hottest in the middle.
    g.setColour (colour.withAlpha (0.16f));
    for (auto [dx, dy] : { std::pair { -1.2f, 0.0f }, { 1.2f, 0.0f }, { 0.0f, -1.2f }, { 0.0f, 1.2f } })
        g.drawText (text, r.translated (dx, dy), just, false);
    g.setColour (colour);
    g.drawText (text, r, just, false);
    g.setColour (colour.brighter (0.6f).withAlpha (0.5f));
    g.drawText (text, r, just, false);
}

void paintSunsetStripes (juce::Graphics& g, juce::Rectangle<float> r)
{
    // Three grooves milled through the anodising: bare metal in each, shaded under the top edge.
    const float h = r.getHeight() / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        const auto groove = juce::Rectangle<float> (r.getX(), r.getY() + h * (float) i + h * 0.2f, r.getWidth(), h * 0.6f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (groove.translated (0.0f, 0.8f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6e655d), 0.0f, groove.getY(), juce::Colour (0xffe9e2d8), 0.0f, groove.getBottom(), false));
        g.fillRect (groove);
    }
}
} // namespace dd::gui
