#pragma once

#include <dd_gui/dd_gui.h>

#include "GuitarView.h"
#include "PluginProcessor.h"

class SunburstEditor : public juce::AudioProcessorEditor
{
public:
    explicit SunburstEditor (SunburstProcessor&);
    ~SunburstEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseW = 1280, kBaseH = 820;

private:
    /** Everything is laid out at base size inside this, then scaled as one piece. */
    class Content : public juce::Component
    {
    public:
        explicit Content (SunburstProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        SunburstProcessor& proc;
        dd::gui::PresetBar presetBar;
        GuitarView guitar;
        dd::gui::Panel playing, amp, room;
        std::vector<std::unique_ptr<PedalView>> pedals;
        juce::Rectangle<int> pedalboard;
    };

    SunburstProcessor& proc;
    dd::gui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<Content> content;
    bool constructed = false;
};
