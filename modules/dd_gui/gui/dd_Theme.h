#pragma once

namespace dd::gui
{
/** Warm light colours: the only saturated colours the UI uses, and only for things that glow
    (lamps, backlit scales, display text, spotlights). */
namespace sunset
{
    inline const juce::Colour red    { 0xffd04a2c };
    inline const juce::Colour ember  { 0xffe2622f };
    inline const juce::Colour orange { 0xffec7d2e };
    inline const juce::Colour amber  { 0xfff3a83b };
    inline const juce::Colour yellow { 0xfff6c95a };
}

/** Colours for one plugin: brushed brown anodised metal panels with silver hardware (knobs, screws, bezels),
    engraving that shows the bare silver metal, warm light behind dark glass. Plugins pick an accent from `sunset`. */
struct Palette
{
    juce::Colour background { 0xff29231f };   // chassis: darker brown metal
    juce::Colour panel      { 0xff47372b };   // brushed brown anodised faceplates
    juce::Colour panelEdge  { 0xff0b0705 };   // the shadow gap around a plate
    juce::Colour track      { 0xff0e0b09 };   // smoked glass: displays and backlit slots
    juce::Colour text       { 0xffe6ded2 };   // engraving: bare silver metal showing through the anodising
    juce::Colour dim        { 0xffb3a493 };   // lighter cut for secondary labels
    juce::Colour glass      { 0xffffc46e };   // text glowing behind dark glass (displays, menus)
    juce::Colour screenText { 0xfff3e9d8 };   // warm white print inside dark scenes
    juce::Colour accent     { sunset::orange };
    juce::Colour accent2    { sunset::amber };
};

/** The same palette for parts made of bare silver (pedal enclosures, badges): engraving there is dark. */
inline Palette onSilver (Palette p)
{
    p.text = juce::Colour (0xff1d1f23);
    p.dim = juce::Colour (0xff3f434a);
    return p;
}

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
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
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
// Materials shared by every editor, so all plugins read as one family of real hardware: brushed brown
// metal plates, machined silver parts.
// Light falls from above and slightly left: top edges catch it, lower edges and the plate below a
// part fall into shadow; anything cut into the metal is lit on its lower lip instead.

/** Chassis: darker brushed brown metal, finely brushed, falling off towards the corners. */
void paintBackdrop (juce::Graphics&, juce::Rectangle<int> area, const Palette&);

/** Bare metal of colour `base`, clipped to a rounded rect: the room reflected in it (polished: bright top, crisp
    horizon, bounce below; satin: a soft fall-off), hairline brushing and the light stretched across the grain. */
void paintBrushedMetal (juce::Graphics&, juce::Rectangle<float> r, juce::Colour base, float corner = 0.0f, float brushing = 1.0f,
                        bool polished = true);

/** Film grain over an area (tiled, generated once): for drawn scenes, not for metal. */
void paintGrain (juce::Graphics&, juce::Rectangle<int> area, float amount = 1.0f);

/** A raised brushed-metal faceplate screwed onto the chassis: shadow below, machined (silver) edges, four screws. */
void paintFaceplate (juce::Graphics&, juce::Rectangle<float> r, const Palette&, bool screws = true);

/** A window cut into the metal (displays, scopes, scenes): debossed edge, dark smoked glass inside. */
void paintInset (juce::Graphics&, juce::Rectangle<float> r, const Palette&, float corner = 4.0f);

/** A chamfered silver bezel framing `window` (drawn around it, outside), for big scene windows. */
void paintBezel (juce::Graphics&, juce::Rectangle<float> window, float corner = 8.0f, float width = 6.0f);

/** Glass reflection over a window: a soft sheen across the top. */
void paintGlassReflection (juce::Graphics&, juce::Rectangle<float> window, float corner = 6.0f);

/** A silver Phillips screw seated in a counterbore. */
void paintScrew (juce::Graphics&, juce::Point<float> centre, float radius, float slotAngle);

/** An indicator lamp in a chrome bezel; `lit` 0..1. Lit lamps throw warm light onto the metal around them. */
void paintLamp (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour colour, float lit);

/** Text engraved into the anodised metal: the cut shows bare silver (light colours), shaded under its top
    wall and lit on its lower lip. Dark colours are engraved into bare silver parts instead. */
void drawPrinted (juce::Graphics&, const juce::String& text, juce::Rectangle<float> r, juce::Colour colour,
                  const juce::Font& f, juce::Justification just = juce::Justification::centred);

/** drawPrinted for text that must fit a box (squeezed instead of cut). */
void drawPrintedFitted (juce::Graphics&, const juce::String& text, juce::Rectangle<int> r, juce::Colour colour,
                        const juce::Font& f, juce::Justification just = juce::Justification::centred);

/** Raised silver lettering (wordmarks): metal fill, lit top edge, shadow below. */
void drawEmbossed (juce::Graphics&, const juce::String& text, juce::Rectangle<float> r, const juce::Font& f,
                   juce::Justification just = juce::Justification::centredLeft);

/** Text glowing behind dark glass (amber display lettering, backlit legends). */
void drawGlowing (juce::Graphics&, const juce::String& text, juce::Rectangle<float> r, juce::Colour colour,
                  const juce::Font& f, juce::Justification just = juce::Justification::centred);

/** Three machined grooves cut into the metal: the decorative band next to a wordmark. */
void paintSunsetStripes (juce::Graphics&, juce::Rectangle<float> r);
} // namespace dd::gui
