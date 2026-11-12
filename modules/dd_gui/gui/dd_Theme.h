#pragma once

namespace dd::gui
{
/** Light colours. In the rawleads design there is one signal colour, the orange of an incandescent lamp
    behind smoked plastic; these are its shades (kept under their old names so plugin code still builds). */
namespace sunset
{
    inline const juce::Colour red    { 0xffc5602c };   // signal
    inline const juce::Colour ember  { 0xffd4682f };
    inline const juce::Colour orange { 0xffe0793f };   // signal glow
    inline const juce::Colour amber  { 0xffff9652 };   // lamp
    inline const juce::Colour yellow { 0xffffb070 };   // hot filament
}

/** The rawleads "Precision Industrial Instrument" design tokens, one set per theme (src/outreach.css). */
struct Tokens
{
    bool light = false;
    juce::Colour room, roomEdge;                         // the surface the instrument rests on
    juce::Colour chassisTop, chassisMid, chassisBot;     // rounded-top body
    juce::Colour railTop, railMid, railBot;              // top bar
    juce::Colour metalTop, metalMid, metalBot, metalEdge;// panels
    juce::Colour faceTop, faceBot, keyTop, keySheen, keyUnder, keyBotline, keyWall;   // keycaps
    juce::Colour trayTop, trayBot, trayRecess, trayLip;  // recessed trays
    juce::Colour edgeLight, edgeDark, castSoft, castMid;
    juce::Colour panel900, panel700, panel600;
    juce::Colour cream100, cream300, cream500, faint;    // text: primary, secondary, muted, faint
    juce::Colour signal, signalDim, signalGlow;
};

const Tokens& tokens();          // the current theme
bool isLightTheme();
/** Switch theme for every open editor of every plugin (saved for next time). */
void setLightTheme (bool light);

/** Lets editors follow theme switches: called just before (to snapshot for the crossfade) and after. */
struct ThemeListener
{
    virtual ~ThemeListener() = default;
    virtual void themeAboutToChange() {}
    virtual void themeChanged() = 0;
};
void addThemeListener (ThemeListener*);
void removeThemeListener (ThemeListener*);

/** Colours for one plugin, resolved from the current theme when constructed (build one per paint). */
struct Palette
{
    Palette();
    juce::Colour background, panel, panelEdge, track, text, dim, glass, screenText, accent, accent2;
};

/** Same palette for things drawn on a light-coloured part regardless of theme (kept for older code). */
inline Palette onSilver (Palette p) { return p; }

class LookAndFeel : public juce::LookAndFeel_V4, private ThemeListener
{
public:
    explicit LookAndFeel (Palette p = {});
    ~LookAndFeel() override;

    Palette palette() const { return {}; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;

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

    /** The UI typeface (Instrument Sans, embedded; an open-licence stand-in for rawleads' Braun Linear). */
    static juce::Font font (float height, bool bold = false);
    static juce::Font font (float height, int weight);   // 400, 500, 600 or 700
    /** The heavy cut, for wordmarks and headings. */
    static juce::Font display (float height);

private:
    void themeChanged() override { applyColours(); }
    void applyColours();
};

//==============================================================================
// Animation curves from the stylesheet's keyframes, as functions of time in seconds.

/** filament-flicker: steady with brief irregular dips (opacity 0.68..1). `period` 3.9..7 s. */
float filamentFlicker (double seconds, double period, double delay);
/** lamp-bloom: the warm pool of a lit display gently breathing (0.62..0.95). */
float lampBloom (double seconds);
/** pulse: LED breathing 1 -> 0.35 -> 1. */
float ledPulse (double seconds, double period = 1.4);
double animationTime();

//==============================================================================
// Materials. Light falls from above; raised parts cast shadow down, recessed parts are shaded under
// their top edge and catch light on their lower lip.

/** The room and the instrument resting in it: dark surround, rounded-top chassis with fine vertical striation. */
void paintBackdrop (juce::Graphics&, juce::Rectangle<int> area, const Palette&);
/** The same, painted directly (no cached picture). */
void paintBackdropDirect (juce::Graphics&, juce::Rectangle<int> area, const Palette&);

/**
    Draws `paint` through a picture kept in `store` (a component's properties, under `slot`) and only paints it
    again when `key`, the theme, the area or the pixel scale change. For the static layers under things that
    animate (backdrops, panel faces, shadows, the vignette): a meter's repaint then copies pixels instead of
    filling every gradient again. The picture is made at the screen's pixel scale, so it stays sharp.
*/
void paintCached (juce::Graphics& g, juce::NamedValueSet& store, const juce::Identifier& slot, juce::Rectangle<int> area,
                  juce::int64 key, const std::function<void (juce::Graphics&)>& paint);

/** Matte molded grain over an area (generated once). */
void paintGrain (juce::Graphics&, juce::Rectangle<int> area, float amount = 1.0f);

/** The deep soft shadow a raised panel casts on the chassis (box-shadow 0 22px 48px -14px). */
void paintRaisedShadow (juce::Graphics&, juce::Rectangle<float> r, float corner = 14.0f);

/** A raised panel (".glass"): metal face, lit top edge, deep soft cast shadow, top sheen. */
void paintFaceplate (juce::Graphics&, juce::Rectangle<float> r, const Palette&, bool screws = true);

/** Panel metal without the shadow, clipped to a rounded rect (pedal bodies, dialogs). */
void paintBrushedMetal (juce::Graphics&, juce::Rectangle<float> r, juce::Colour base, float corner = 0.0f, float brushing = 1.0f,
                        bool polished = true);

/** A recessed tray (".keytray" / inputs): darker inside, shaded under the top, a lit lip below. */
void paintInset (juce::Graphics&, juce::Rectangle<float> r, const Palette&, float corner = 4.0f);
void paintTray (juce::Graphics&, juce::Rectangle<float> r, float corner, float depth = 1.0f);

/** A recessed well around a scene window (the window itself is painted by the scene). */
void paintBezel (juce::Graphics&, juce::Rectangle<float> window, float corner = 8.0f, float width = 6.0f);

/** The console display (".console"): warm black glass, a breathing lamp glow, scanlines, a top sheen. */
void paintConsole (juce::Graphics&, juce::Rectangle<float> r, float corner, float bloom);
void paintGlassReflection (juce::Graphics&, juce::Rectangle<float> window, float corner = 6.0f);

/** A keycap (".navkey" / ".kbtn"): face, lit top edge, under shadow, a wall that collapses when pressed. */
void paintKeycap (juce::Graphics&, juce::Rectangle<float> r, float corner, bool pressed, float wall = 4.0f);
/** A lit key: smoked plastic with an amber LED glowing through it. `light` scales the glow (flicker).
    `glow` is how far the light and shadow spill outside `r`: keep it within the component's bounds. */
void paintLitKey (juce::Graphics&, juce::Rectangle<float> r, float corner, bool pressed, float light, float wall = 4.0f,
                  float glow = 12.0f);

/** A switch (".switch"): tray pill, keycap knob at `position` 0..1, LED when on. */
void paintSwitch (juce::Graphics&, juce::Rectangle<float> r, float position, bool on, float led = 1.0f);

/** Kept for older scene code: rawleads has no visible fasteners, so this draws nothing. */
void paintScrew (juce::Graphics&, juce::Point<float> centre, float radius, float slotAngle);

/** An LED (".led"): recessed ring, glowing lens when `lit` > 0. */
void paintLamp (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour colour, float lit);

/** Text on the instrument (with the faint edge-light lip the stylesheet uses). */
void drawPrinted (juce::Graphics&, const juce::String& text, juce::Rectangle<float> r, juce::Colour colour,
                  const juce::Font& f, juce::Justification just = juce::Justification::centred);
void drawPrintedFitted (juce::Graphics&, const juce::String& text, juce::Rectangle<int> r, juce::Colour colour,
                        const juce::Font& f, juce::Justification just = juce::Justification::centred);

/** The brand plate: heavy uppercase, edge-lit (".brand-name"). */
void drawEmbossed (juce::Graphics&, const juce::String& text, juce::Rectangle<float> r, const juce::Font& f,
                   juce::Justification just = juce::Justification::centredLeft);

/** Lit lettering (".navkey.active" text: #ffd9a0 with a warm glow). */
void drawGlowing (juce::Graphics&, const juce::String& text, juce::Rectangle<float> r, juce::Colour colour,
                  const juce::Font& f, juce::Justification just = juce::Justification::centred);

/** Section caption (".well-head" / panel captions): small, bold, wide-tracked, muted. */
void drawCaption (juce::Graphics&, const juce::String& text, juce::Rectangle<float> r,
                  juce::Justification just = juce::Justification::centredLeft, float height = 10.0f);

/** Kept for older code (the decorative band is gone in this design): draws nothing. */
void paintSunsetStripes (juce::Graphics&, juce::Rectangle<float> r);
} // namespace dd::gui
