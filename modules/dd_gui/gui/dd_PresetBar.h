#pragma once

namespace dd::gui
{
/** Category + preset selectors, prev/next, save and a small menu. Works with any dd::PresetManager. */
class PresetBar : public juce::Component, private juce::ChangeListener
{
public:
    explicit PresetBar (dd::PresetManager& manager);
    ~PresetBar() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();
    void promptSave();
    void showMenu();

    dd::PresetManager& pm;
    juce::ComboBox categoryBox, presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, menuButton { "..." };
    juce::Array<int> shownIndices;
    juce::String description;
};
} // namespace dd::gui
