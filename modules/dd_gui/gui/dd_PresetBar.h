#pragma once

namespace dd::gui
{
/** A keycap (".navkey" / ".kbtn"); when lit, smoked plastic with an amber LED behind it. A new label rises in. */
class Key : public juce::Button, private juce::Timer
{
public:
    explicit Key (const juce::String& legend, bool lit = false);

    void setLabel (const juce::String& legend, bool animate = true);
    void setLit (bool shouldBeLit);
    void setFlickerPeriod (double seconds);
    /** Room left around the cap for a lit key's glow (the component is laid out this much larger). */
    void setGlowMargin (int pixels) { glowMargin = pixels; repaint(); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    bool hitTest (int x, int y) override { return getLocalBounds().reduced (glowMargin).contains (x, y); }

private:
    void timerCallback() override;
    juce::String label;
    bool lit;
    int glowMargin = 0;
    double changedAt = 0.0;
};

/** Category + preset keys in a recessed tray: prev, category, the lit preset display, next, save, menu. */
class PresetBar : public juce::Component, private juce::ChangeListener
{
public:
    explicit PresetBar (dd::PresetManager& manager);
    ~PresetBar() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::String getDescription() const { return description; }
    std::function<void()> onPresetChanged;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();
    void promptSave();
    void showMenu();
    void showCategories();
    void showPresets();

    dd::PresetManager& pm;
    Key prevKey { "<" }, catKey { "" }, nameKey { "", true }, nextKey { ">" }, saveKey { "Save" }, menuKey { "..." };
    juce::String description;
};

/**
    The instrument's top rail (".topbar"): two-tone brand plate on the left, the preset keytray in the
    centre, the plugin's own switches and DARK / LITE on the right. Its hard shadow on the interior below is
    painted by paintPanelShadows().
*/
class TopBar : public juce::Component
{
public:
    static constexpr int kHeight = 66;

    TopBar (dd::PresetManager& manager, const juce::String& brand, const juce::String& brandAccent, const juce::String& subtitle);

    /** Adds a control on the right, placed right to left before the theme switch. */
    void addRight (juce::Component& c, int width);

    PresetBar& presets() noexcept { return bar; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String brand, brandAccent, subtitle;
    PresetBar bar;
    ThemeSwitch themeSwitch;
    std::vector<std::pair<juce::Component*, int>> right;
};

/** The preset's description under the top rail, rising in when the preset changes. */
class DescriptionLine : public juce::Component, private juce::Timer
{
public:
    explicit DescriptionLine (PresetBar& bar);
    void paint (juce::Graphics&) override;
    void setHint (const juce::String& rightHint) { hint = rightHint; repaint(); }

private:
    void timerCallback() override;
    PresetBar& bar;
    juce::String text, hint;
    double changedAt = 0.0;
};
} // namespace dd::gui
