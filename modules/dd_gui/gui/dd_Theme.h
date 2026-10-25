#pragma once

namespace dd::gui
{
/** Colours for one plugin. Plugins override `accent` (and optionally more) to get their own identity. */
struct Palette
{
    juce::Colour background { 0xff0b0b0d };
    juce::Colour panel      { 0xff141418 };
    juce::Colour panelEdge  { 0xff24242c };
    juce::Colour track      { 0xff2a2a33 };
    juce::Colour text       { 0xffe9e9ee };
    juce::Colour dim        { 0xff8b8b97 };
    juce::Colour accent     { 0xffff5a1f };
    juce::Colour accent2    { 0xffffb020 };
};

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit LookAndFeel (Palette p = {});

    const Palette& palette() const noexcept { return pal; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    static juce::Font font (float height, bool bold = false);

private:
    Palette pal;
};
} // namespace dd::gui
