#pragma once

#include <dd_gui/dd_gui.h>

#include "HorizonView.h"
#include "PluginProcessor.h"

class AfterglowEditor : public juce::AudioProcessorEditor
{
public:
    explicit AfterglowEditor (AfterglowProcessor&);
    ~AfterglowEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseW = 1100, kBaseH = 700;

private:
    /** Everything is laid out at base size inside this, then scaled as one piece. */
    class Content : public juce::Component
    {
    public:
        explicit Content (AfterglowProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        AfterglowProcessor& proc;
        dd::gui::PresetBar presetBar;
        dd::gui::LampSwitch polySwitch;
        HorizonView horizon;
        std::vector<std::unique_ptr<dd::gui::Knob>> macros;
        std::vector<std::unique_ptr<dd::gui::LampSwitch>> switches;
        juce::Rectangle<int> deck;
    };

    AfterglowProcessor& proc;
    dd::gui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<Content> content;
    bool constructed = false;
};
