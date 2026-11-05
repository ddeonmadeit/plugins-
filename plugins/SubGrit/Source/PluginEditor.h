#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"
#include "SubView.h"

class SubGritEditor : public juce::AudioProcessorEditor
{
public:
    explicit SubGritEditor (SubGritProcessor&);
    ~SubGritEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseW = 1200, kBaseH = 738;

private:
    /** Everything is laid out at base size inside this, then scaled as one piece. */
    class Content : public juce::Component
    {
    public:
        explicit Content (SubGritProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        /** Simple: the speaker and six macros. All knobs: every engine control. Remembered per session. */
        void showAllKnobs (bool all);

        SubGritProcessor& proc;
        dd::gui::TopBar topBar;
        dd::gui::DescriptionLine description;
        dd::gui::LampSwitch polySwitch;
        dd::gui::Key viewKey { "All Knobs" };
        dd::gui::Panel body808, oscA, oscB, lfo, filter, env, voice, eq, dirt, fx, output, scopePanel;
        dd::gui::Scope& scope;

        // The simple panel.
        SubView sub;
        dd::gui::Scope simpleScope;
        std::vector<std::unique_ptr<dd::gui::Knob>> macros;
        juce::Rectangle<int> deck;

        dd::gui::Atmosphere atmosphere;
    };

    SubGritProcessor& proc;
    dd::gui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<Content> content;
    bool constructed = false;
};
