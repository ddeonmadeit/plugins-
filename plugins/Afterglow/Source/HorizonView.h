#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"

/**
    The centre of the editor: a sunset horizon. The sun is the Motion / Tone orb (drag it: left-right
    is still-to-moving, high is bright, low and sinking is dark), the mountain ridge is the live
    spectrum, and the water carries the sun's reflection. Preset name, category and the two layer
    sources are printed over the sky.
*/
class HorizonView : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit HorizonView (AfterglowProcessor&);
    ~HorizonView() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    static constexpr int kRidge = 128;
    static constexpr int kFftOrder = 11;
    static constexpr int kFftSize = 1 << kFftOrder;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    float horizonY() const noexcept { return (float) getHeight() * 0.66f; }
    float sunRadius() const noexcept { return (float) getHeight() * 0.17f; }
    juce::Point<float> sunCentre() const;
    void setFromPoint (juce::Point<float>);

    void paintSky (juce::Graphics&, float toneAmount) const;
    void paintSun (juce::Graphics&, juce::Point<float> c, float r) const;
    void paintRidges (juce::Graphics&, juce::Point<float> sun) const;
    void paintWater (juce::Graphics&, juce::Point<float> sun, float r) const;
    void paintText (juce::Graphics&) const;

    AfterglowProcessor& proc;
    juce::RangedAudioParameter* motion = nullptr;
    juce::RangedAudioParameter* tone = nullptr;
    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, kFftSize * 2> fftData {};
    std::array<float, kRidge> ridge {}, nearBase {}, midBase {}, farBase {};
    std::vector<juce::Point<float>> stars;
    float level = 0.0f, time = 0.0f;
    bool dragging = false, overSun = false;
};
