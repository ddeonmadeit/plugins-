#pragma once

#include <dd_gui/dd_gui.h>

#include "ElementPanel.h"
#include "KitView.h"
#include "PluginProcessor.h"

class KitroomEditor : public juce::AudioProcessorEditor
{
public:
    explicit KitroomEditor (KitroomProcessor&);
    ~KitroomEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseW = 1200, kBaseH = 812;

private:
    /** Everything is laid out at base size inside this, then scaled as one piece. */
    class Content : public juce::Component
    {
    public:
        explicit Content (KitroomProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        KitroomProcessor& proc;
        dd::gui::PresetBar presetBar;
        KitView kit;
        ElementPanel element;
        dd::gui::Panel mix;
    };

    KitroomProcessor& proc;
    dd::gui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<Content> content;
    bool constructed = false;
};
