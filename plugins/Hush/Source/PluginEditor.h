#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"
#include "SoundView.h"

class HushEditor : public juce::AudioProcessorEditor
{
public:
    explicit HushEditor (HushProcessor&);
    ~HushEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseW = 1280, kBaseH = 826;

private:
    /** Everything is laid out at base size inside this, then scaled as one piece. */
    class Content : public juce::Component
    {
    public:
        explicit Content (HushProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        HushProcessor& proc;
        hush::EditorState state;
        dd::gui::TopBar topBar;
        dd::gui::DescriptionLine description;
        hush::FollowKey follow;
        dd::gui::Panel sounds, controls, selected, session;
        dd::gui::Atmosphere atmosphere;
    };

    HushProcessor& proc;
    dd::gui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<Content> content;
    bool constructed = false;
};
