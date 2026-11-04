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
    The visual EQ: the curve over a live analyzer of the voice before (faint) and after the EQ.
    Drag a node to move its band (frequency and gain), scroll on it for Q, double-click to reset.
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

private:
    struct Node { const char* name; const char* freqId; const char* gainId; const char* qId; };
    static const std::array<Node, 7>& nodes();

    void timerCallback() override;
    void updateCurve();
    void parameterChanged (const juce::String&, float) override { curveDirty = true; }
    juce::Rectangle<float> plot() const;
    float xForHz (float hz) const;
    float hzForX (float x) const;
    float yForDb (float db) const;
    float dbForY (float y) const;
    juce::Point<float> nodePos (int i) const;
    int nodeAt (juce::Point<float>) const;
    float value (const char* id) const;
    void setValue (const char* id, float v);
    void analyse (dd::ScopeBuffer& src, std::vector<float>& smooth);

    AircheckProcessor& proc;
    aircheck::VocalEq eq;
    std::vector<float> curve, preSpec, postSpec, fftData, window;
    juce::dsp::FFT fft { 12 };
    std::atomic<bool> curveDirty { true };   // set from whichever thread changes a band
    int hover = -1, drag = -1;
};

/** Gain reduction of the compressor (and the de-esser beside it), as a lit segment column. */
class GrMeter : public juce::Component, private juce::Timer
{
public:
    explicit GrMeter (AircheckProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    AircheckProcessor& proc;
    float comp = 0.0f, ess = 0.0f;
};
