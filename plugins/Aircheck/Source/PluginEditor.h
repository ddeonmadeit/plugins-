#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"
#include "Views.h"

class AircheckEditor : public juce::AudioProcessorEditor
{
public:
    explicit AircheckEditor (AircheckProcessor&);
    ~AircheckEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseW = 1280, kBaseH = 868;

private:
    /** Everything is laid out at base size inside this, then scaled as one piece. */
    class Content : public juce::Component
    {
    public:
        explicit Content (AircheckProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        AircheckProcessor& proc;
        dd::gui::TopBar topBar;
        dd::gui::DescriptionLine description;
        dd::gui::Panel tune, shift, eq, voice, comp, deess, smooth, air, dbl, chorus, vocoder, delay, reverb, tapeP, output;
        dd::gui::Atmosphere atmosphere;
    };

    AircheckProcessor& proc;
    dd::gui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<Content> content;
    bool constructed = false;
};
