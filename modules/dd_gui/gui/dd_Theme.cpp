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

    const juce::Colour kInk { 0xff1a0f08 };   // dark text on lit sunset buttons
}

//==============================================================================
LookAndFeel::LookAndFeel (Palette p) : pal (p)
{
    const auto displayText = pal.text.interpolatedWith (pal.accent2, 0.4f);
    setColour (juce::ResizableWindow::backgroundColourId, pal.background);
    setColour (juce::ComboBox::backgroundColourId, pal.track);
    setColour (juce::ComboBox::textColourId, displayText);
    setColour (juce::ComboBox::outlineColourId, pal.panelEdge);
    setColour (juce::ComboBox::arrowColourId, pal.accent2.withAlpha (0.7f));
    setColour (juce::PopupMenu::backgroundColourId, pal.panel.darker (0.25f));
    setColour (juce::PopupMenu::textColourId, pal.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pal.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, kInk);
    setColour (juce::PopupMenu::headerTextColourId, pal.accent2);
    setColour (juce::Label::textColourId, pal.text);
    setColour (juce::TextButton::buttonColourId, pal.panel.brighter (0.1f));
    setColour (juce::TextButton::buttonOnColourId, pal.accent);
    setColour (juce::TextButton::textColourOffId, pal.text.withAlpha (0.85f));
    setColour (juce::TextButton::textColourOnId, kInk);
    setColour (juce::TextEditor::backgroundColourId, pal.track);
    setColour (juce::TextEditor::textColourId, displayText);
    setColour (juce::TextEditor::highlightColourId, pal.accent.withAlpha (0.4f));
    setColour (juce::TextEditor::outlineColourId, pal.panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, pal.accent);
    setColour (juce::CaretComponent::caretColourId, pal.accent2);
    setColour (juce::AlertWindow::backgroundColourId, pal.panel);
    setColour (juce::AlertWindow::textColourId, pal.text);
    setColour (juce::AlertWindow::outlineColourId, pal.panelEdge);
    setColour (juce::Slider::textBoxTextColourId, pal.text);
    setColour (juce::TooltipWindow::backgroundColourId, pal.panel.darker (0.3f));
    setColour (juce::TooltipWindow::textColourId, pal.text);
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

    // Scale: an engraved groove that lights up warm, like a backlit dial.
    const float arcW = std::max (2.0f, radius * 0.1f);
    const float arcR = radius - arcW * 0.5f;
    juce::Path groove;
    groove.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.strokePath (groove, juce::PathStrokeType (arcW + 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::translation (0.0f, 0.7f));
    g.setColour (pal.track);
    g.strokePath (groove, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (std::abs (angle - fromAngle) > 0.001f)
    {
        juce::Path lit;
        lit.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, std::min (fromAngle, angle), std::max (fromAngle, angle), true);
        const auto col = enabled ? accent : pal.dim;
        g.setColour (col.withAlpha (0.18f));
        g.strokePath (lit, juce::PathStrokeType (arcW * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col);
        g.strokePath (lit, juce::PathStrokeType (arcW * 0.75f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Knob: soft contact shadow, knurled skirt, matte bakelite cap.
    const float skirtR = arcR - arcW * 1.6f;
    const float capR = skirtR * 0.8f;
    for (int i = 3; i >= 1; --i)
    {
        const float sr = skirtR + (float) i * 1.1f;
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        g.fillEllipse (c.x - sr, c.y - sr + 2.2f, sr * 2.0f, sr * 2.0f);
    }

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a2c22), c.x, c.y - skirtR,
                                             juce::Colour (0xff100b08), c.x, c.y + skirtR, false));
    g.fillEllipse (c.x - skirtR, c.y - skirtR, skirtR * 2.0f, skirtR * 2.0f);

    const int ridges = 40;
    for (int i = 0; i < ridges; ++i)
    {
        const float ridgeAngle = juce::MathConstants<float>::twoPi * (float) i / (float) ridges;
        const float sn = std::sin (ridgeAngle), cs = std::cos (ridgeAngle);
        // Light falls from above: ridges facing up catch it, the rest stay in shadow.
        const float light = 0.5f - 0.5f * cs;
        g.setColour ((i % 2 == 0 ? juce::Colours::white.withAlpha (0.05f + 0.10f * (1.0f - light))
                                 : juce::Colours::black.withAlpha (0.35f)));
        g.drawLine (c.x + sn * capR * 1.04f, c.y - cs * capR * 1.04f, c.x + sn * (skirtR - 0.6f), c.y - cs * (skirtR - 0.6f), 1.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (c.x - skirtR, c.y - skirtR, skirtR * 2.0f, skirtR * 2.0f, 0.8f);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - capR - 0.6f, c.y - capR + 0.4f, (capR + 0.6f) * 2.0f, (capR + 0.6f) * 2.0f);
    juce::ColourGradient cap (juce::Colour (0xff4b3a2e), c.x - capR * 0.45f, c.y - capR * 0.7f,
                              juce::Colour (0xff120d09), c.x + capR * 0.55f, c.y + capR * 0.9f, true);
    cap.addColour (0.45, juce::Colour (0xff2a1f17));
    g.setGradientFill (cap);
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawEllipse (c.x - capR + 0.5f, c.y - capR + 0.5f, capR * 2.0f - 1.0f, capR * 2.0f - 1.0f, 0.8f);

    // Painted pointer line running from the cap onto the skirt.
    const float pIn = capR * 0.25f, pOut = skirtR * 0.96f;
    const juce::Point<float> a (c.x + std::sin (angle) * pIn, c.y - std::cos (angle) * pIn);
    const juce::Point<float> b (c.x + std::sin (angle) * pOut, c.y - std::cos (angle) * pOut);
    const float pw = std::max (1.8f, radius * 0.085f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawLine ({ a.translated (0.0f, 0.8f), b.translated (0.0f, 0.8f) }, pw + 1.0f);
    g.setColour (enabled ? pal.text : pal.dim);
    g.drawLine ({ a, b }, pw);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat();
    paintInset (g, r, pal, 4.0f);
    if (box.hasKeyboardFocus (true))
    {
        g.setColour (pal.accent.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    }

    const float aw = 7.0f, ah = 4.0f;
    const float ax = (float) w - 10.0f, ay = (float) h * 0.5f - ah * 0.5f;
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
    label.setBounds (6, 1, box.getWidth() - 20, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setMinimumHorizontalScale (0.6f);
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Font LookAndFeel::getPopupMenuFont() { return font (15.0f); }

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h);
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    paintGrain (g, r, 0.8f);
    g.setColour (pal.accent.withAlpha (0.35f));
    g.drawRect (r, 1);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    constexpr float corner = 4.0f;

    // The button sits in a cut-out; pressing it moves the cap down into the seat.
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.fillRoundedRectangle (r, corner + 0.5f);
    auto capArea = r.reduced (1.0f).withTrimmedBottom (1.5f);
    if (down)
        capArea = capArea.translated (0.0f, 1.0f);

    juce::Colour top, bottom;
    if (on)
    {
        const auto lamp = b.findColour (juce::TextButton::buttonOnColourId);
        top = lamp.brighter (over ? 0.3f : 0.18f);
        bottom = lamp.darker (0.45f);
    }
    else
    {
        const auto base = b.findColour (juce::TextButton::buttonColourId);
        top = base.brighter (over ? 0.32f : 0.2f);
        bottom = base.darker (0.45f);
    }
    if (down)
        std::swap (top, bottom);

    g.setGradientFill (juce::ColourGradient (top, 0.0f, capArea.getY(), bottom, 0.0f, capArea.getBottom(), false));
    g.fillRoundedRectangle (capArea, corner);
    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (capArea, corner);
        g.reduceClipRegion (clip);
        paintGrain (g, capArea.toNearestInt(), on ? 0.5f : 0.9f);
    }
    g.setColour (juce::Colours::white.withAlpha (on ? 0.3f : 0.09f));
    g.drawHorizontalLine ((int) capArea.getY() + 1, capArea.getX() + corner, capArea.getRight() - corner);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2).translated (0, down ? 0 : -1),
                      juce::Justification::centred, 1);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return font (std::min (14.0f, (float) h * 0.55f), true).withExtraKerningFactor (0.04f);
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

void paintBackdrop (juce::Graphics& g, juce::Rectangle<int> area, const Palette& pal)
{
    const auto r = area.toFloat();
    g.setGradientFill (juce::ColourGradient (pal.background.brighter (0.22f), r.getCentreX(), r.getY(),
                                             pal.background.darker (0.3f), r.getCentreX(), r.getBottom(), false));
    g.fillRect (area);
    paintGrain (g, area, 1.0f);

    juce::ColourGradient vignette (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                   juce::Colours::black.withAlpha (0.55f), r.getX(), r.getY(), true);
    vignette.addColour (0.55, juce::Colours::transparentBlack);
    g.setGradientFill (vignette);
    g.fillRect (area);
}

void paintFaceplate (juce::Graphics& g, juce::Rectangle<float> r, const Palette& pal, bool screws)
{
    constexpr float corner = 5.0f;
    g.setColour (pal.panelEdge);
    g.fillRoundedRectangle (r, corner + 1.0f);

    const auto plate = r.reduced (1.0f);
    g.setGradientFill (juce::ColourGradient (pal.panel.brighter (0.1f), 0.0f, plate.getY(),
                                             pal.panel.darker (0.18f), 0.0f, plate.getBottom(), false));
    g.fillRoundedRectangle (plate, corner);
    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (plate, corner);
        g.reduceClipRegion (clip);
        paintGrain (g, plate.toNearestInt(), 1.0f);
    }

    // Bevel: the top edge catches the room light, the bottom edge falls into shadow.
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawLine (plate.getX() + corner, plate.getY() + 0.5f, plate.getRight() - corner, plate.getY() + 0.5f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawLine (plate.getX() + corner, plate.getBottom() - 0.5f, plate.getRight() - corner, plate.getBottom() - 0.5f, 1.0f);

    if (screws)
    {
        const float in = 7.0f;
        paintScrew (g, { plate.getX() + in, plate.getY() + in }, 3.0f, 0.6f);
        paintScrew (g, { plate.getRight() - in, plate.getY() + in }, 3.0f, 2.1f);
        paintScrew (g, { plate.getX() + in, plate.getBottom() - in }, 3.0f, 1.2f);
        paintScrew (g, { plate.getRight() - in, plate.getBottom() - in }, 3.0f, 0.2f);
    }
}

void paintInset (juce::Graphics& g, juce::Rectangle<float> r, const Palette& pal, float corner)
{
    g.setColour (juce::Colours::white.withAlpha (0.07f));      // lower lip catching light
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillRoundedRectangle (r.withTrimmedBottom (1.0f), corner);
    const auto glass = r.reduced (1.0f).withTrimmedBottom (1.0f);
    g.setColour (pal.track);
    g.fillRoundedRectangle (glass, corner - 1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, glass.getY(),
                                             juce::Colours::transparentBlack, 0.0f, glass.getY() + std::min (7.0f, glass.getHeight() * 0.4f), false));
    g.fillRoundedRectangle (glass, corner - 1.0f);
}

void paintScrew (juce::Graphics& g, juce::Point<float> c, float radius, float slotAngle)
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - radius - 0.9f, c.y - radius - 0.4f, (radius + 0.9f) * 2.0f, (radius + 0.9f) * 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8c8173), c.x - radius * 0.5f, c.y - radius * 0.6f,
                                             juce::Colour (0xff2a241e), c.x + radius * 0.7f, c.y + radius * 0.8f, true));
    g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);

    const float dx = std::cos (slotAngle) * radius * 0.82f, dy = std::sin (slotAngle) * radius * 0.82f;
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawLine (c.x - dx, c.y - dy + 0.6f, c.x + dx, c.y + dy + 0.6f, radius * 0.4f);
    g.setColour (juce::Colour (0xff140f0b));
    g.drawLine (c.x - dx, c.y - dy, c.x + dx, c.y + dy, radius * 0.4f);
}

void paintLamp (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour colour, float lit)
{
    lit = juce::jlimit (0.0f, 1.0f, lit);
    for (int i = 3; i >= 1 && lit > 0.0f; --i)
    {
        const float gr = radius + (float) i * 2.2f;
        g.setColour (colour.withAlpha (0.13f * lit));
        g.fillEllipse (c.x - gr, c.y - gr, gr * 2.0f, gr * 2.0f);
    }
    const float bezel = radius + 1.4f;
    g.setColour (juce::Colour (0xff0b0806));
    g.fillEllipse (c.x - bezel, c.y - bezel, bezel * 2.0f, bezel * 2.0f);
    g.setColour (juce::Colour (0xff6a5d50));
    g.drawEllipse (c.x - bezel, c.y - bezel, bezel * 2.0f, bezel * 2.0f, 0.8f);

    // Coloured glass: dark when off; when lit, a hot centre fading to the deep tint at the rim.
    const auto off = colour.darker (2.2f);
    const auto rim = off.interpolatedWith (colour.darker (0.35f), lit);
    const auto core = off.brighter (0.3f).interpolatedWith (colour.brighter (0.25f), lit);
    g.setGradientFill (juce::ColourGradient (core, c.x - radius * 0.15f, c.y - radius * 0.2f, rim, c.x + radius, c.y + radius, true));
    g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.18f + 0.22f * lit));
    g.fillEllipse (c.x - radius * 0.6f, c.y - radius * 0.65f, radius * 0.55f, radius * 0.4f);
}

void drawPrinted (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour colour,
                  const juce::Font& f, juce::Justification just)
{
    g.setFont (f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawText (text, r.translated (0.0f, 1.0f), just, false);
    g.setColour (colour);
    g.drawText (text, r, just, false);
}

void paintSunsetStripes (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float h = r.getHeight() / 3.0f;
    const juce::Colour cols[] = { sunset::yellow, sunset::orange, sunset::red };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (cols[i].withAlpha (0.92f));
        g.fillRect (r.getX(), r.getY() + h * (float) i, r.getWidth(), h - std::min (1.0f, h * 0.25f));
    }
    paintGrain (g, r.toNearestInt(), 0.8f);
}
} // namespace dd::gui
