#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"

/**
    The stage: the selected guitar drawn under a warm spotlight. Its six strings light up and shimmer
    while they ring, and a glowing dot marks the fret each string is played at. Arrows (or a click on
    the name plate) switch guitars.
*/
class GuitarView : public juce::Component, private juce::Timer, private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit GuitarView (SunburstProcessor&);
    ~GuitarView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    enum class Shape { Archtop, Classical, Solid };
    struct Look { Shape shape; juce::Colour finish, centre, binding; bool burst, fHoles, metalPickups; };

    void timerCallback() override;
    void parameterChanged (const juce::String&, float) override;
    void step (int delta);
    Look look() const;
    int guitarIndex() const;

    void paintStage (juce::Graphics&, juce::Rectangle<float>);
    void paintGuitar (juce::Graphics&, juce::Rectangle<float>, const Look&);
    juce::Path bodyPath (Shape, juce::Rectangle<float>) const;

    SunburstProcessor& proc;
    juce::Rectangle<float> stage, namePlate, prevArrow, nextArrow;
    juce::Rectangle<float> fretboard;   // nut .. end of the neck, for string / fret geometry
    float bridgeX = 0.0f;
    std::array<float, 6> levels {};
    std::array<int, 6> frets {};
    float phase = 0.0f;
    juce::Image stageLight;
};

/** A stompbox: enclosure in its own colour, its controls up top, a footswitch with a jewel lamp. */
class PedalView : public juce::Component
{
public:
    PedalView (juce::AudioProcessorValueTreeState& s, const juce::String& name, const juce::String& onId,
               std::vector<std::pair<juce::String, juce::String>> controls, juce::Colour body);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    int preferredWidth() const { return 28 + 74 * (int) widgets.size(); }

private:
    juce::String name;
    juce::Colour body;
    juce::ParameterAttachment onAttach;
    bool on = false;
    std::vector<std::unique_ptr<juce::Component>> widgets;
    juce::Rectangle<float> footswitch;
};
