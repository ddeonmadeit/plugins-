#pragma once

#include <dd_gui/dd_gui.h>

#include "NoteView.h"
#include "PluginProcessor.h"

class ContourEditor : public juce::AudioProcessorEditor
{
public:
    explicit ContourEditor (ContourProcessor&);
    ~ContourEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseW = 1280, kBaseH = 826;

private:
    /** Everything is laid out at base size inside this, then scaled as one piece. */
    class Content : public juce::Component
    {
    public:
        explicit Content (ContourProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        ContourProcessor& proc;
        contour::EditorState state;
        dd::gui::TopBar topBar;
        dd::gui::DescriptionLine description;
        contour::TransferKey transfer;
        dd::gui::Panel notes, correction, selected, session;
        dd::gui::Atmosphere atmosphere;
    };

    ContourProcessor& proc;
    dd::gui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<Content> content;
    bool constructed = false;
};
