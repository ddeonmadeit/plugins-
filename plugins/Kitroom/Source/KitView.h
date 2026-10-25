#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"

/**
    The drum kit, drawn from the drummer's audience-side view. Every piece is clickable (select +
    audition), flashes when it is played, and shows its key in FL Studio's note naming. Drum shells
    take the finish colour of the kit their current sound comes from, so swaps are visible.
*/
class KitView : public juce::Component, private juce::Timer
{
public:
    explicit KitView (KitroomProcessor&);

    std::function<void (int slot)> onSelect;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int kW = 1176, kH = 420;

private:
    enum class Shape { Kick, Snare, RackTom, FloorTom, HiHat, Crash, Ride, China };

    struct Piece
    {
        int slot;
        Shape shape;
        juce::Rectangle<float> box;      // drum: top-head box; cymbal: cymbal ellipse box
        float shellH;                    // drums only
        juce::Point<float> label;        // centre of the name/key pill
        juce::Path hit;                  // mouse hit area
        juce::Path outline;              // silhouette for selection and hit glow
    };

    void timerCallback() override;
    int pieceAt (juce::Point<float>) const;
    juce::Colour finishFor (int slot) const;

    void drawRoom (juce::Graphics&) const;
    void drawSpotlights (juce::Graphics&);
    void drawStand (juce::Graphics&, float x, float top, float bottom, bool tripod) const;
    void drawDrum (juce::Graphics&, const Piece&, float glow) const;
    void drawKick (juce::Graphics&, const Piece&, float glow) const;
    void drawCymbal (juce::Graphics&, juce::Rectangle<float> r, float glow, bool china) const;
    void drawLabel (juce::Graphics&, const Piece&) const;

    KitroomProcessor& proc;
    std::vector<Piece> pieces;            // back to front
    std::array<float, kr::kNumSlots> flash {};
    juce::Image darkness;                 // static light mask: black except where the spots fall
    int hover = -1;
};
