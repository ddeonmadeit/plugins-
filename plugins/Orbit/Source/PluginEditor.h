#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"
#include "RoomView.h"

/** A grid of keys for a choice parameter: the chosen one is lit. Each key can explain itself. */
class ChoiceKeys : public juce::Component
{
public:
    ChoiceKeys (juce::RangedAudioParameter& p, int columns, const juce::StringArray& helpPerOption);
    void resized() override;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::OwnedArray<dd::gui::Key> keys;
    int cols;
};

/** One trip of the motion, drawn as a ring with the sound running round it (beats of the song). */
class CycleDial : public juce::Component, private juce::Timer
{
public:
    explicit CycleDial (OrbitProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }
    OrbitProcessor& proc;
};

/** EARS: which measured pair of ears listens (top bar). */
class EarsControl : public juce::Component
{
public:
    explicit EarsControl (juce::AudioProcessorValueTreeState&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::ComboBox box;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment attachment;
};

class OrbitEditor : public juce::AudioProcessorEditor
{
public:
    explicit OrbitEditor (OrbitProcessor&);
    ~OrbitEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    dd::gui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    dd::gui::TopBar topBar;
    dd::gui::DescriptionLine description;
    dd::gui::LampSwitch speakers;
    EarsControl ears;
    RoomView scene;
    dd::gui::Panel roomPanel, placePanel, motionPanel, outputPanel;
    dd::gui::Atmosphere atmosphere;   // keep last
};
