#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"

class __NAME__Editor : public juce::AudioProcessorEditor
{
public:
    explicit __NAME__Editor (__NAME__Processor&);
    ~__NAME__Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    dd::gui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    dd::gui::TopBar topBar;                 // brand, preset keys, DARK / LITE
    dd::gui::DescriptionLine description;   // the preset's description, rising in on change
    dd::gui::Panel main;
    dd::gui::Scope* scope = nullptr;
    dd::gui::Atmosphere atmosphere;         // vignette, grain, cursor shadow, theme crossfade, lamp flicker: keep last
};
