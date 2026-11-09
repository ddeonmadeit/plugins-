#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"

/** The pitch console: sung pitch (dim) and corrected pitch (lit) scrolling over the scale's note lines. */
class PitchView : public juce::Component, private juce::Timer
{
public:
    explicit PitchView (AircheckProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    AircheckProcessor& proc;
    float centre = 60.0f;
    std::array<float, 360> in {}, out {};
    int target = -1;
};

/**
    The pitch shifter's pad: left / right is the pitch, up / down the voice's size (formants). Along the
    lit middle line the voice stays itself, only higher or lower; the dashed diagonal is where formants
    follow the pitch (a chipmunk above, a slowed-down giant below). Drag the dot, double-click resets.
*/
class ShiftView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit ShiftView (AircheckProcessor&);
    ~ShiftView() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** What a shift / formant pair sounds like, in a few words. */
    static juce::String describe (float shift, float formant);

private:
    void timerCallback() override;
    juce::Rectangle<float> plotArea() const;
    void setFrom (juce::Point<float>);
    AircheckProcessor& proc;
    juce::ParameterAttachment shiftAtt, formantAtt;
    float shift = 0.0f, formant = 0.0f;
};

/**
    The visual EQ: eight bands over a live analyzer of the voice before (faint) and after the EQ.
    Click a point to select its band, drag to move it, scroll for Q, right-click to switch it on or off,
    double-click a point to reset its gain, double-click empty space to add a band there.
*/
class EqView : public juce::Component, private juce::Timer, private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit EqView (AircheckProcessor&);
    ~EqView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void setSelected (int band);
    int getSelected() const noexcept { return selected; }
    std::function<void (int)> onSelect;

private:
    void timerCallback() override;
    void updateCurve();
    void parameterChanged (const juce::String&, float) override { curveDirty = true; }
    std::array<aircheck::BandSettings, aircheck::ProEq::kBands> readBands() const;
    juce::Rectangle<float> plot() const;
    float xForHz (float hz) const;
    float hzForX (float x) const;
    float yForDb (float db) const;
    float dbForY (float y) const;
    juce::Point<float> nodePos (int band) const;
    int nodeAt (juce::Point<float>) const;
    float value (const juce::String& id) const;
    void setValue (const juce::String& id, float v, bool gesture = false);
    void analyse (dd::ScopeBuffer& src, std::vector<float>& smooth);

    AircheckProcessor& proc;
    std::array<aircheck::BandSettings, aircheck::ProEq::kBands> bands {};
    std::vector<float> curve, bandCurve, preSpec, postSpec, fftData, window;
    juce::dsp::FFT fft { 12 };
    std::atomic<bool> curveDirty { true };   // set from whichever thread changes a band
    int hover = -1, drag = -1, selected = 0;
};

/** The selected EQ band's controls (rebuilt when another band is selected). */
class BandStrip : public juce::Component
{
public:
    explicit BandStrip (AircheckProcessor&);
    void setBand (int band);
    void resized() override;
    void paint (juce::Graphics&) override;
    std::function<void (int)> onBandChange;   // arrows on the strip

private:
    AircheckProcessor& proc;
    int band = -1;
    std::vector<std::unique_ptr<juce::Component>> controls;
    std::unique_ptr<dd::gui::Toggle> post;   // EQ before or after the dynamics (not per band)
    juce::TextButton prev { "<" }, next { ">" };
};

/** A segment column for one or two gain-reduction readings. */
class GrMeter : public juce::Component, private juce::Timer
{
public:
    GrMeter (juce::String caption, std::function<float()> first, std::function<float()> second = {})
        : label (std::move (caption)), a (std::move (first)), b (std::move (second)) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    juce::String label;
    std::function<float()> a, b;
    float va = 0.0f, vb = 0.0f;
};
