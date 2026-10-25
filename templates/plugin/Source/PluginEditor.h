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
    dd::gui::PresetBar presetBar;
    dd::gui::Panel main;
    dd::gui::Scope* scope = nullptr;
};
