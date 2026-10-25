namespace dd::gui
{
LookAndFeel::LookAndFeel (Palette p) : pal (p)
{
    setColour (juce::ResizableWindow::backgroundColourId, pal.background);
    setColour (juce::ComboBox::backgroundColourId, pal.track);
    setColour (juce::ComboBox::textColourId, pal.text);
    setColour (juce::ComboBox::outlineColourId, pal.panelEdge);
    setColour (juce::ComboBox::arrowColourId, pal.dim);
    setColour (juce::PopupMenu::backgroundColourId, pal.panel.brighter (0.05f));
    setColour (juce::PopupMenu::textColourId, pal.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pal.accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::PopupMenu::headerTextColourId, pal.accent2);
    setColour (juce::Label::textColourId, pal.text);
    setColour (juce::TextButton::buttonColourId, pal.track);
    setColour (juce::TextButton::buttonOnColourId, pal.accent);
    setColour (juce::TextButton::textColourOffId, pal.dim);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::TextEditor::backgroundColourId, pal.track);
    setColour (juce::TextEditor::textColourId, pal.text);
    setColour (juce::TextEditor::highlightColourId, pal.accent.withAlpha (0.4f));
    setColour (juce::TextEditor::outlineColourId, pal.panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, pal.accent);
    setColour (juce::AlertWindow::backgroundColourId, pal.panel);
    setColour (juce::AlertWindow::textColourId, pal.text);
    setColour (juce::AlertWindow::outlineColourId, pal.panelEdge);
    setColour (juce::Slider::textBoxTextColourId, pal.text);
    setColour (juce::TooltipWindow::backgroundColourId, pal.panel.brighter (0.1f));
    setColour (juce::TooltipWindow::textColourId, pal.text);
}

juce::Font LookAndFeel::font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float lineW = std::max (2.5f, radius * 0.16f);
    const float arcR = radius - lineW * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    // Bipolar parameters draw their arc from the centre.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
    const float fromAngle = startAngle + zeroPos * (endAngle - startAngle);

    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (pal.track);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (std::abs (angle - fromAngle) > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, std::min (fromAngle, angle), std::max (fromAngle, angle), true);
        g.setColour (s.isEnabled() ? accent : pal.dim);
        g.strokePath (val, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Body
    const float bodyR = arcR - lineW * 1.1f;
    g.setGradientFill (juce::ColourGradient (pal.panel.brighter (0.25f), c.x, c.y - bodyR,
                                             pal.panel.darker (0.4f), c.x, c.y + bodyR, false));
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (pal.panelEdge.brighter (0.2f));
    g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    // Pointer
    const float pIn = bodyR * 0.25f, pOut = bodyR * 0.85f;
    const juce::Point<float> a (c.x + std::sin (angle) * pIn, c.y - std::cos (angle) * pIn);
    const juce::Point<float> b (c.x + std::sin (angle) * pOut, c.y - std::cos (angle) * pOut);
    g.setColour (pal.text);
    g.drawLine ({ a, b }, std::max (1.8f, radius * 0.09f));
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.hasKeyboardFocus (true) ? pal.accent : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    const float aw = 6.0f, ah = 3.5f;
    const float ax = (float) w - 8.0f, ay = (float) h * 0.5f - ah * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (ax - aw * 0.5f, ay, ax + aw * 0.5f, ay, ax, ay + ah);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return font (std::min (13.0f, (float) box.getHeight() * 0.54f));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 1, box.getWidth() - 15, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setMinimumHorizontalScale (0.6f);
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Font LookAndFeel::getPopupMenuFont() { return font (14.0f); }

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? b.findColour (juce::TextButton::buttonOnColourId) : b.findColour (juce::TextButton::buttonColourId);
    if (over) fill = fill.brighter (0.08f);
    if (down) fill = fill.darker (0.1f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? fill.brighter (0.2f) : pal.panelEdge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return font (std::min (13.0f, (float) h * 0.52f), true);
}
} // namespace dd::gui
