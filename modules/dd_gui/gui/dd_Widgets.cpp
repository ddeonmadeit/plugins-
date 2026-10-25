namespace dd::gui
{
namespace
{
    const Palette& paletteOf (const juce::Component& c)
    {
        static const Palette fallback;
        if (auto* lf = dynamic_cast<LookAndFeel*> (&c.getLookAndFeel()))
            return lf->palette();
        return fallback;
    }

    void drawTitle (juce::Graphics& g, const juce::Component& c, const juce::String& title, juce::Rectangle<int> r)
    {
        const auto f = LookAndFeel::font (10.5f, true).withExtraKerningFactor (0.07f);
        const auto text = title.toUpperCase();
        // Squeeze long titles instead of truncating them, like hand-fitted panel lettering.
        const float w = juce::GlyphArrangement::getStringWidth (f, text);
        const float squeeze = w > (float) r.getWidth() - 2.0f ? ((float) r.getWidth() - 2.0f) / w : 1.0f;
        drawPrinted (g, text, r.toFloat(), paletteOf (c).dim, f.withHorizontalScale (squeeze), juce::Justification::centred);
    }
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& t, juce::Colour accent)
    : title (t)
{
    param = state.getParameter (paramId);
    jassert (param != nullptr);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    if (! accent.isTransparent())
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { updateValueText(); repaint(); };
    slider.setTooltip (param->getName (64));
    updateValueText();
}

void Knob::updateValueText()
{
    valueText = param != nullptr ? param->getCurrentValueAsText() : juce::String();
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (16);
    r.removeFromBottom (18);
    const int size = std::min (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (size, size));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    drawTitle (g, *this, title, r.removeFromTop (16));
    const auto& pal = paletteOf (*this);
    g.setColour (slider.isMouseOverOrDragging() ? pal.text.interpolatedWith (pal.accent2, 0.5f) : pal.text.withAlpha (0.85f));
    g.setFont (LookAndFeel::font (12.0f));
    g.drawFittedText (valueText, r.removeFromBottom (18), juce::Justification::centred, 1, 0.7f);
}

//==============================================================================
Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& t)
    : title (t)
{
    auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId));
    jassert (p != nullptr);
    if (p != nullptr)
        box.addItemList (p->choices, 1);
    addAndMakeVisible (box);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, box);
}

void Choice::resized()
{
    auto r = getLocalBounds().reduced (1, 0);
    r.removeFromTop (16);
    box.setBounds (r.withSizeKeepingCentre (r.getWidth(), 24).translated (0, -6));
}

void Choice::paint (juce::Graphics& g)
{
    drawTitle (g, *this, title, getLocalBounds().removeFromTop (16));
}

//==============================================================================
Toggle::Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& t,
                const juce::String& on, const juce::String& off)
    : title (t), onText (on), offText (off)
{
    button.setClickingTogglesState (true);
    button.onStateChange = [this] { button.setButtonText (button.getToggleState() ? onText : offText); };
    addAndMakeVisible (button);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, button);
    button.setButtonText (button.getToggleState() ? onText : offText);
}

void Toggle::resized()
{
    auto r = getLocalBounds().reduced (6, 0);
    r.removeFromTop (16);
    button.setBounds (r.withSizeKeepingCentre (std::min (r.getWidth(), 56), 22).translated (0, -6));
}

void Toggle::paint (juce::Graphics& g)
{
    drawTitle (g, *this, title, getLocalBounds().removeFromTop (16));
}

//==============================================================================
Panel::Panel (const juce::String& t, int c, int r, juce::Colour a)
    : title (t), cols (c), rows (r), accent (a)
{
}

void Panel::place (int col, int row, int colSpan, int rowSpan)
{
    jassert (! items.empty());
    auto& it = items.back();
    it.col = col; it.row = row; it.colSpan = colSpan; it.rowSpan = rowSpan;
    next = row * cols + col + colSpan;
}

void Panel::paint (juce::Graphics& g)
{
    const auto& pal = paletteOf (*this);
    paintFaceplate (g, getLocalBounds().toFloat(), pal);

    // Title: a lit jewel lamp in the section's colour, then the name printed on the plate.
    auto head = getLocalBounds().removeFromTop (kTitleH + 2).withTrimmedLeft (18).withTrimmedRight (18).toFloat();
    paintLamp (g, { head.getX() + 4.0f, head.getCentreY() + 1.0f }, 3.2f, accent, 1.0f);
    head.removeFromLeft (14.0f);
    drawPrinted (g, title.toUpperCase(), head.translated (0.0f, 1.0f), pal.text,
                 LookAndFeel::font (13.0f, true).withExtraKerningFactor (0.1f), juce::Justification::centredLeft);
}

void Panel::resized()
{
    // Centre the grid when the panel is stretched wider than its preferred size.
    const int x0 = std::max (kPad, (getWidth() - cols * kCellW) / 2);
    for (auto& it : items)
        it.comp->setBounds (x0 + it.col * kCellW, kTitleH + it.row * kCellH, it.colSpan * kCellW, it.rowSpan * kCellH);
}
} // namespace dd::gui
