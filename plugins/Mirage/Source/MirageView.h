#pragma once

#include <dd_gui/dd_gui.h>

#include "Analysis.h"
#include "DotFont.h"
#include "PluginProcessor.h"

namespace mirage
{
/**
    The only colours: one ramp from black through deep red, red and orange to a little Tarantino yellow,
    turned around the colour wheel by the Hue control. The turn keeps each step's brightness, so the
    picture reads the same in any hue (blue runs out of brightness sooner, so its top end pales to white,
    like a lit tube). CRT and VHS also let the very top bloom to white.
*/
struct Palette
{
    std::array<juce::PixelARGB, 1024> lut {};
    juce::Colour orange, red, dim, yellow, ink, panel;
    float hue = 1.0e9f, white = -1.0f;

    /** Rebuilds for a hue (degrees) and how far the top end goes to white (0..1). True if anything changed. */
    bool build (float hueDegrees, float whiteTop);
    static int index (float t) noexcept { return juce::jlimit (0, 1023, (int) (t * 1023.0f)); }
    juce::PixelARGB at (float t) const noexcept { return lut[(size_t) index (t)]; }
    juce::Colour colour (float t) const noexcept
    {
        const auto p = at (t);
        return juce::Colour (p.getRed(), p.getGreen(), p.getBlue());
    }
    /** Any colour turned like the ramp (brightness kept). */
    juce::Colour tint (juce::Colour c) const;
    /** The ramp before any turn. */
    static juce::Colour base (float t);
    static juce::Colour turn (juce::Colour c, float degrees);
};

/** The panel's look for everything JUCE draws itself: the resize grip, menus and the colour wheel's bubble. */
class PanelLook : public juce::LookAndFeel_V4
{
public:
    void setPalette (const Palette&);
    void drawCornerResizer (juce::Graphics&, int w, int h, bool over, bool dragging) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    int getCallOutBoxBorderSize (const juce::CallOutBox&) override { return 14; }

private:
    juce::Colour accent { 0xffff6a1f }, ink { 0xff0a0302 };
};

/** A control drawn as a segmented arc gauge on the panel: drag up / down, double-click resets. */
class HudDial : public juce::Component
{
public:
    HudDial (juce::RangedAudioParameter& p, const juce::String& label, const Palette&);
    void setLabel (const juce::String& l);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String label, paramId;
    const Palette& pal;
    float value = 0.0f, dragStart = 0.0f;
};

/** A dot-matrix key on the header: framed text, lit under the mouse, does something when clicked. */
class HeaderKey : public juce::Component
{
public:
    HeaderKey (const Palette& p, std::function<juce::String()> textFn) : pal (p), text (std::move (textFn))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (onClick && e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()))
            onClick();
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    std::function<void()> onClick;
    bool framed = true, bright = false;
    float dot = 1.5f, inset = 0.0f;

private:
    const Palette& pal;
    std::function<juce::String()> text;
};

/** The little glowing dot next to DEON: click it for the colour wheel. */
class HueDot : public juce::Component
{
public:
    explicit HueDot (const Palette& p) : pal (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (onClick && e.mouseWasClicked())
            onClick();
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    std::function<void()> onClick;
    float phase = 0.0f;

private:
    const Palette& pal;
};

/** The colour wheel: drag around the ring to turn the hue of the whole panel, double-click for the original. */
class HueWheel : public juce::Component
{
public:
    HueWheel (juce::RangedAudioParameter& hueParam, int size);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void setFrom (juce::Point<float>);
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::Image ring;
    float hue = 0.0f;
};

/**
    The whole visualiser, drawn wide to sit under the tracks: a control-panel header (DEON and the hue dot,
    the scene and style keys, the preset selector in the middle, the transport on the right), a big window,
    a deck of instrument panels (levels, scope, scrolling waveform, spectrum, goniometer, controls) and a
    thermal camera strip of the spectrum at the bottom.

    The window draws a scene into a buffer of brightness (Dunes, Burst, Terrain, Figure, Tunnel), then a style
    turns it into pixels (Film: grain and a phosphor mesh; CRT: a curved tube with phosphor trails, bloom and
    scanlines; VHS: tape smear, tracking tears and dropouts). Every pixel goes through one colour ramp.
*/
class MirageView : public juce::Component, private juce::Timer
{
public:
    explicit MirageView (MirageProcessor&);
    ~MirageView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    juce::LookAndFeel& getPanelLook() noexcept { return look; }

private:
    void timerCallback() override;
    void advance (float dt);
    void updatePalette();
    void updateSceneLabels();
    void renderWindow();
    void renderThermal();
    void buildStatics();
    void buildFrame();
    void buildChrome (float scale);

    // Scenes (Scenes.cpp): each fills `buf` (window size) with brightness, about 0..1, more for highlights.
    void sceneDunes();
    void sceneBurst();
    void sceneTerrain();
    void sceneFigure();
    void sceneTunnel();
    void polarMaps (float cx, float cy);
    void addLine (float x0, float y0, float x1, float y1, float b);

    // Styles (Styles.cpp): `buf` to pixels in windowImg.
    void styleFilm();
    void styleCrt();
    void styleVhs();
    void buildTube();
    /** Window image pixel to where it shows on screen (follows the tube's curve in CRT). */
    juce::Point<float> toScreen (float ix, float iy) const noexcept;

    void drawFrame (juce::Graphics&);
    void drawHeader (juce::Graphics&);
    void drawWindowHud (juce::Graphics&);
    void drawDunesHud (juce::Graphics&);
    void drawBurstHud (juce::Graphics&);
    void drawTerrainHud (juce::Graphics&);
    void drawFigureHud (juce::Graphics&);
    void drawTunnelHud (juce::Graphics&);
    void drawTapeHud (juce::Graphics&);
    void drawPanel (juce::Graphics&, juce::Rectangle<float> r, const juce::String& title);
    void drawLevels (juce::Graphics&, juce::Rectangle<float>);
    void drawScope (juce::Graphics&, juce::Rectangle<float>);
    void drawWave (juce::Graphics&, juce::Rectangle<float>);
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>);
    void drawReticle (juce::Graphics&, juce::Rectangle<float>);
    void drawThermalHud (juce::Graphics&);
    void showPresetMenu();
    void showChoiceMenu (const char* paramId, juce::Component& target);
    void openHueWheel();
    float param (const char* id) const noexcept { return proc.apvts.getRawParameterValue (id)->load(); }
    int scene() const noexcept { return juce::roundToInt (param ("scene")); }
    int style() const noexcept { return juce::roundToInt (param ("style")); }

    MirageProcessor& proc;
    Analyzer an;
    Palette pal;
    PanelLook look;
    juce::Random rng;
    std::vector<float> noise;         // 512 x 512 grain tile, -1..1
    std::vector<float> smoothNoise;   // 256 x 256 soft hills, 0..1, tiles
    std::vector<float> crest;         // [layer][x] screen y of each dune's ridge
    std::vector<float> expLut, colCrest, colLit, colTop, colGlow, shimSin, shimCos, meshCol, smooth;
    std::vector<float> buf, persist, frameVals, thermVals;
    std::vector<float> polarA, polarR;   // per window pixel: angle and distance from a scene's centre
    std::vector<float> mapA, mapB, mapC; // per window pixel: whatever the scene keeps fixed (glows, depth, fog)
    std::vector<juce::uint16> angIdx;    // per window pixel: angle as a 0..1023 bin
    std::array<float, 1024> angLutA {}, angLutB {};
    std::array<juce::uint8, 1024> rayNear {}, rayNext {};
    float polarCx = -1.0f, polarCy = -1.0f;
    std::vector<int> warp;               // CRT: window pixel -> source pixel (-1 = the tube's black edge)
    std::vector<float> tube, glare, bloom, bloomTmp, bloomXw;
    std::vector<int> bloomX0;
    int tubeW = 0, tubeH = 0;
    std::vector<float> specHist;         // Terrain: spectrum rows, newest first in a ring
    int specHead = 0;
    float travel = 0.0f, travelRows = 0.0f, spin = 0.0f, tunnelDepth = 0.0f, tearPos = 0.3f, frameDt = 1.0f / 60.0f;
    std::array<float, 64> ringVals {};   // Tunnel: how bright each ring was born
    int lastRing = 0;
    std::vector<float> rowShift, colNoise;
    juce::Point<float> burstTip, burstSource;
    double lastTick = 0.0;
    int frames = 0, lastScene = -1, lastStyle = -1;
    float sunPulse = 0.0f, scan = 0.0f, thermalCarry = 0.0f, flash = 0.0f;

    float s = 1.0f;                   // scale: height / 440
    juce::Rectangle<float> header, window, deck, thermal;
    juce::Rectangle<float> pLevels, pScope, pWave, pSpectrum, pReticle, pControls;
    juce::Image windowImg, thermalImg, frameImg, chromeImg;
    float chromeScale = 0.0f;

    juce::OwnedArray<HudDial> dials;
    HueDot hueDot { pal };
    HeaderKey sceneKey, styleKey, prevKey, nextKey, nameKey;
    dd::gui::HoverHelp help { *this };
    juce::Component::SafePointer<juce::CallOutBox> wheelBox;
};
} // namespace mirage
