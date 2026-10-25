#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"

/**
    Editor for the selected kit piece: swap its sound (any sound of the same kind from any kit),
    see and re-learn the key of each articulation, audition them, and mix the piece.
*/
class ElementPanel : public juce::Component, private juce::Timer
{
public:
    explicit ElementPanel (KitroomProcessor&);

    void setSlot (int slot);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshSounds();
    void refreshKeys();
    void step (int delta);

    struct ArtRow
    {
        juce::TextButton play, key;
    };

    KitroomProcessor& proc;
    int slot = -1;
    juce::ComboBox soundBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, resetKeys { "GM keys" };
    std::vector<int> soundIndices;
    std::array<ArtRow, kr::kMaxArticulations> rows;
    std::vector<std::unique_ptr<dd::gui::Knob>> knobs;
    juce::String credit;
};
