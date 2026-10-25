#pragma once

namespace dd::gui
{
/** Rotary knob bound to a parameter: title above, live value below, double-click resets. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title,
          juce::Colour accent = {});

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider& getSlider() noexcept { return slider; }

private:
    void updateValueText();

    juce::RangedAudioParameter* param = nullptr;
    juce::Slider slider;
    juce::String title, valueText;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** Combo box bound to a choice parameter, with a title above. */
class Choice : public juce::Component
{
public:
    Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String title;
    juce::ComboBox box;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** Toggle pill bound to a bool parameter, with a title above. */
class Toggle : public juce::Component
{
public:
    Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title,
            const juce::String& onText = "ON", const juce::String& offText = "OFF");

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String title, onText, offText;
    juce::TextButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/**
    Titled section that lays its children out on a fixed grid of cells.
    Children are added in reading order; use place() for anything spanning several cells.
*/
class Panel : public juce::Component
{
public:
    static constexpr int kCellW = 64, kCellH = 84, kPad = 8, kTitleH = 24;

    Panel (const juce::String& title, int cols, int rows, juce::Colour accent);

    /** Preferred pixel size for this grid. */
    juce::Point<int> preferredSize() const { return { cols * kCellW + kPad * 2, rows * kCellH + kTitleH + kPad }; }

    template <typename T, typename... Args>
    T& add (Args&&... args)
    {
        auto c = std::make_unique<T> (std::forward<Args> (args)...);
        auto& ref = *c;
        addAndMakeVisible (ref);
        items.push_back ({ std::move (c), next % cols, next / cols, 1, 1 });
        ++next;
        return ref;
    }

    /** Overrides the cell / span of the most recently added child. */
    void place (int col, int row, int colSpan = 1, int rowSpan = 1);
    void skip (int cells = 1) { next += cells; }

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Colour getAccent() const noexcept { return accent; }

private:
    struct Item { std::unique_ptr<juce::Component> comp; int col, row, colSpan, rowSpan; };
    juce::String title;
    int cols, rows, next = 0;
    juce::Colour accent;
    std::vector<Item> items;
};
} // namespace dd::gui
