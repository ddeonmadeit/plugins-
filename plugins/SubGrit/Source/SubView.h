#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"

/**
    The simple panel's scene: a subwoofer in a dark room, its cone pumping with the bass you play (the
    output's envelope, throbbing at a rate that follows the note), pressure rings rolling off it on hard
    hits, a lamp inside the port that glows with the level, and the note and preset on the wall.
*/
class SubView : public juce::Component, private juce::Timer
{
public:
    explicit SubView (SubGritProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawRoom (juce::Graphics&) const;
    void drawCabinet (juce::Graphics&) const;
    void drawWoofer (juce::Graphics&, juce::Point<float> c, float r) const;
    void drawReadout (juce::Graphics&) const;

    SubGritProcessor& proc;
    std::array<float, 1024> samples {};
    juce::Rectangle<float> cabinet;
    juce::Point<float> wooferCentre;
    float wooferR = 100.0f;

    float env = 0.0f, prevEnv = 0.0f;     // output envelope, 0..1
    float phase = 0.0f;                   // the cone's visible throb
    float excursion = 0.0f;               // -1..1, where the cone is
    float lamp = 0.0f;
    struct Ring { float age = 1.0f, strength = 0.0f; };
    std::array<Ring, 5> rings {};
    int note = -1;
    double lastTick = 0.0;
};
