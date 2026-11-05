#pragma once

#include <dd_gui/dd_gui.h>

#include "Analysis.h"
#include "DotFont.h"
#include "PluginProcessor.h"

namespace mirage
{
/** The only colours: black-red through red and orange to a little Tarantino yellow at the very top. */
juce::PixelARGB heatColour (float t) noexcept;
juce::Colour heat (float t) noexcept;

/** A control drawn as a segmented arc gauge on the panel: drag up / down, double-click resets. */
class HudDial : public juce::Component
{
public:
    HudDial (juce::RangedAudioParameter& p, const juce::String& label);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String label;
    float value = 0.0f, dragStart = 0.0f;
};

/**
    The whole visualiser, drawn wide to sit under the tracks: a control-panel header, a big window with a
    grainy sunset over dunes that move with the sound (the near dune is the live waveform, the far ones are
    the spectrum of the last half second drifting away, the sun swells with the low end and can be eclipsed),
    a deck of instrument panels (levels, scope, scrolling waveform, spectrum, goniometer, controls) and a
    thermal camera strip of the spectrum at the bottom. Every pixel goes through one colour ramp, so only
    red, orange and a little yellow ever appear.
*/
class MirageView : public juce::Component, private juce::Timer
{
public:
    explicit MirageView (MirageProcessor&);
    ~MirageView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void advance (float dt);
    void renderWindow();
    void renderThermal();
    void buildStatics();
    void buildChrome (float scale);

    void drawFrame (juce::Graphics&);
    void drawHeader (juce::Graphics&);
    void drawWindowHud (juce::Graphics&);
    void drawPanel (juce::Graphics&, juce::Rectangle<float> r, const juce::String& title);
    void drawLevels (juce::Graphics&, juce::Rectangle<float>);
    void drawScope (juce::Graphics&, juce::Rectangle<float>);
    void drawWave (juce::Graphics&, juce::Rectangle<float>);
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>);
    void drawReticle (juce::Graphics&, juce::Rectangle<float>);
    void drawThermalHud (juce::Graphics&);
    float param (const char* id) const noexcept { return proc.apvts.getRawParameterValue (id)->load(); }

    MirageProcessor& proc;
    Analyzer an;
    juce::Random rng;
    std::vector<float> noise;         // 512 x 512 grain tile, -1..1
    std::vector<float> crest;         // [layer][x] screen y of each dune's ridge
    std::vector<float> expLut, colCrest, colLit, colTop, colGlow, shimSin, shimCos, meshCol, smooth;
    double lastTick = 0.0;
    int frames = 0;
    float sunPulse = 0.0f, scan = 0.0f, thermalCarry = 0.0f;

    float s = 1.0f;                   // scale: height / 440
    juce::Rectangle<float> header, window, deck, thermal;
    juce::Rectangle<float> pLevels, pScope, pWave, pSpectrum, pReticle, pControls;
    juce::Rectangle<float> prevKey, nextKey, sceneArea;
    juce::Image windowImg, thermalImg, frameImg, chromeImg;
    float chromeScale = 0.0f;

    juce::OwnedArray<HudDial> dials;
    dd::gui::HoverHelp help { *this };
};
} // namespace mirage
