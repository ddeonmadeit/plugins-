#pragma once

namespace dd::gui
{
/** Warm "sunset" highlights: the only saturated colours the UI uses (lamps, arcs, displays, selections). */
namespace sunset
{
    inline const juce::Colour red    { 0xffd04a2c };
    inline const juce::Colour ember  { 0xffe2622f };
    inline const juce::Colour orange { 0xffec7d2e };
    inline const juce::Colour amber  { 0xfff3a83b };
    inline const juce::Colour yellow { 0xfff6c95a };
}

/** Colours for one plugin: dark brown matte hardware with cream print. Plugins pick an accent from `sunset`. */
struct Palette
{
    juce::Colour background { 0xff150d08 };   // chassis
    juce::Colour panel      { 0xff2e1e13 };   // matte brown faceplates
    juce::Colour panelEdge  { 0xff0a0604 };
    juce::Colour track      { 0xff0f0906 };   // recessed grooves and display windows
    juce::Colour text       { 0xffeadcc3 };   // cream print
    juce::Colour dim        { 0xff9c876e };   // faded tan print
    juce::Colour accent     { sunset::orange };
    juce::Colour accent2    { sunset::amber };
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
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getAlertWindowTitleFont() override   { return font (18.0f, true); }
    juce::Font getAlertWindowMessageFont() override { return font (14.0f); }
    juce::Font getAlertWindowFont() override        { return font (14.0f); }
    juce::Font getLabelFont (juce::Label&) override { return font (14.0f); }
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;

    /** The UI typeface (Barlow Condensed, embedded) at a given height. */
    static juce::Font font (float height, bool bold = false);
    /** The heavy cut, for wordmarks and big headings. */
    static juce::Font display (float height);

private:
    Palette pal;
};

//==============================================================================
// Surfaces shared by every editor, so all plugins read as one family of analog hardware.

/** Chassis: dark brown matte with a soft top light, film grain and a vignette. */
void paintBackdrop (juce::Graphics&, juce::Rectangle<int> area, const Palette&);

/** Film grain over an area (tiled, generated once). */
void paintGrain (juce::Graphics&, juce::Rectangle<int> area, float amount = 1.0f);

/** A screwed-on matte faceplate: grained, bevelled, with four slotted screws. */
void paintFaceplate (juce::Graphics&, juce::Rectangle<float> r, const Palette&, bool screws = true);

/** A recessed window (displays, scopes): dark glass sunk into the faceplate. */
void paintInset (juce::Graphics&, juce::Rectangle<float> r, const Palette&, float corner = 4.0f);

void paintScrew (juce::Graphics&, juce::Point<float> centre, float radius, float slotAngle);

/** A jewel indicator lamp; `lit` 0..1. */
void paintLamp (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour colour, float lit);

/** Text printed onto the faceplate: a faint dark offset makes it read as silkscreen on a matte surface. */
void drawPrinted (juce::Graphics&, const juce::String& text, juce::Rectangle<float> r, juce::Colour colour,
                  const juce::Font& f, juce::Justification just = juce::Justification::centred);

/** Three thin stripes (yellow, orange, red), the 70s hardware sunset band. */
void paintSunsetStripes (juce::Graphics&, juce::Rectangle<float> r);
} // namespace dd::gui
