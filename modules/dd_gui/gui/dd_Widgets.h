#pragma once

namespace dd::gui
{
/**
    Hover help. Controls bound to a parameter carry its id ("ddParam") and their caption ("ddTitle"); any other
    component can carry its own text with setHelp(). HoverHelp watches the mouse inside one editor window and
    reports the help for whatever is under it, looked up in the plugin's table (param id -> plain words).
*/
void setHelp (juce::Component& c, const juce::String& title, const juce::String& text);
/** Marks a custom control as bound to a parameter, so its help comes from the plugin's table. */
void tagParam (juce::Component& c, const juce::String& paramId, const juce::String& title);

/** For editors built with WANTS_KEYBOARD: only `target` takes the keyboard when clicked. Buttons and menus
    want focus by default, and once one held it Return pressed it again and the arrows changed a menu instead
    of reaching the DAW. Call once every child exists. */
void giveKeysOnlyTo (juce::Component& root, juce::Component& target);

class HoverHelp : private juce::MouseListener
{
public:
    explicit HoverHelp (juce::Component& insideEditor);
    ~HoverHelp() override;

    void setTable (std::map<juce::String, juce::String> byParamId) { table = std::move (byParamId); }
    bool isActive() const noexcept { return text.isNotEmpty(); }
    const juce::String& getTitle() const noexcept { return title; }
    const juce::String& getText() const noexcept { return text; }

    std::function<void()> onChange;

    /** Tests and headless snapshots: every HoverHelp reacts as if the mouse had moved onto `c`. */
    static void simulateHover (juce::Component& c);

private:
    void mouseEnter (const juce::MouseEvent& e) override { update (e, false); }
    void mouseMove (const juce::MouseEvent& e) override  { update (e, false); }
    void mouseDrag (const juce::MouseEvent& e) override  { update (e, false); }
    void mouseExit (const juce::MouseEvent& e) override  { update (e, true); }
    void update (const juce::MouseEvent&, bool leaving);

    juce::Component& owner;
    std::map<juce::String, juce::String> table;
    juce::String title, text;
};

/** Rotary knob bound to a parameter: caption above, live value below, double-click resets. When the value
    changes from outside (a preset, automation) the knob sweeps to it instead of jumping. */
class Knob : public juce::Component, private juce::Timer
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title,
          juce::Colour accent = {});

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider& getSlider() noexcept { return slider; }

    /** Scales the caption and value text (and the room they take) for big front-panel knobs. */
    void setLabelScale (float s) { labelScale = s; resized(); repaint(); }

private:
    void updateValueText();
    void valueChanged();
    void timerCallback() override;

    float labelScale = 1.0f;
    juce::RangedAudioParameter* param = nullptr;
    juce::Slider slider;
    juce::String title, valueText;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    float shown = 0.0f;   // drawn position 0..1, easing toward the real one
};

/** Combo box bound to a choice parameter, with a caption above. */
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

/** The rawleads switch (".switch" / ".toggle-dot"): a tray pill with a keycap knob that throws across
    in 220 ms and an LED that lights when on. Paints the switch in its bounds; owners decide labels. */
class SwitchAnimation : private juce::Timer
{
public:
    explicit SwitchAnimation (juce::Component& owner) : comp (owner) {}
    void set (bool on, bool animate);
    float position() const noexcept { return pos; }

private:
    void timerCallback() override;
    juce::Component& comp;
    float pos = 0.0f, target = 0.0f;
    double started = 0.0;
    float from = 0.0f;
};

/** On/off parameter with a caption above: a switch with ON / OFF lettering beside it. */
class Toggle : public juce::Component
{
public:
    Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title,
            const juce::String& onText = "ON", const juce::String& offText = "OFF");

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::ParameterAttachment attachment;
    juce::String title, onText, offText;
    bool on = false;
    SwitchAnimation anim { *this };
};

/** An on/off parameter as a labelled switch (".toggle": switch, then the name; the name lights when on). */
class LampSwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    LampSwitch (juce::RangedAudioParameter& p, const juce::String& label, juce::Colour colour);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

private:
    juce::ParameterAttachment attachment;
    juce::String label;
    bool on = false;
    SwitchAnimation anim { *this };
};

/**
    TAPE STOP performance control (dd::TapeStop): a switch and the stop length beside it, or below it when
    the component is taller than one row. Bound to the perf ids, so it stays with the instance and the host
    can automate the switch.
*/
class TapeStopControl : public juce::Component
{
public:
    explicit TapeStopControl (juce::AudioProcessorValueTreeState& state,
                              const juce::String& switchId = "perfTapeStop", const juce::String& timeId = "perfTapeTime");

    void resized() override;

private:
    LampSwitch lamp;
    juce::ComboBox time;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** DARK / LITE: switches the theme of every plugin's editor (".theme-toggle-wrap"). */
class ThemeSwitch : public juce::Component, public juce::SettableTooltipClient, private ThemeListener
{
public:
    ThemeSwitch();
    ~ThemeSwitch() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void themeChanged() override { anim.set (! isLightTheme(), true); repaint(); }
    SwitchAnimation anim { *this };
};

/**
    Titled section (".glass" panel with a caption) that lays its children out on a fixed grid of cells.
    Children are added in reading order; use place() for anything spanning several cells. Its deep cast
    shadow falls outside its bounds: the editor paints it with paintPanelShadows().
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

/** Paints the soft cast shadow of every Panel (and any child marked raised) of `parent` onto it. */
void paintPanelShadows (juce::Graphics&, juce::Component& parent);
/** Marks a component as a raised panel so paintPanelShadows() casts its shadow. */
void markRaised (juce::Component&, float corner = 14.0f);

/**
    The air over the instrument, added last to an editor's content: matte grain and vignette, the cursor's
    shadow (the pointer floats above the machine), a 280 ms crossfade when the theme changes, a fade-in when
    the editor opens, and the filament flicker of every lit key.
*/
class Atmosphere : public juce::Component, private juce::Timer, private ThemeListener
{
public:
    Atmosphere();
    ~Atmosphere() override;

    void paint (juce::Graphics&) override;
    void parentHierarchyChanged() override;

private:
    void timerCallback() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent& e) override { mouseMove (e); }
    void mouseExit (const juce::MouseEvent&) override;
    void themeAboutToChange() override;
    void themeChanged() override;
    void collectLit (juce::Component&);

    juce::Point<float> cursor;
    bool cursorInside = false;
    juce::Image fadeFrom;
    double fadeStart = 0.0, openedAt = 0.0;
    std::vector<juce::Component::SafePointer<juce::Component>> lit;
    int litScanCountdown = 0;
};
} // namespace dd::gui
