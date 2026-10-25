#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"

class SubGritEditor : public juce::AudioProcessorEditor
{
public:
    explicit SubGritEditor (SubGritProcessor&);
    ~SubGritEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseW = 1200, kBaseH = 712;

private:
    /** Everything is laid out at base size inside this, then scaled as one piece. */
    class Content : public juce::Component
    {
    public:
        explicit Content (SubGritProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        SubGritProcessor& proc;
        dd::gui::PresetBar presetBar;
        dd::gui::LampSwitch polySwitch;
        dd::gui::Panel body808, oscA, oscB, lfo, filter, env, voice, eq, dirt, fx, output, scopePanel;
        dd::gui::Scope& scope;
    };

    SubGritProcessor& proc;
    dd::gui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<Content> content;
    bool constructed = false;
};
