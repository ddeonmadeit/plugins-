#pragma once

namespace dd
{
/** Every parameter of `processor` as {id: value} in real units (choices by name, bools as bool). */
juce::var captureParameterValues (juce::AudioProcessor& processor);

/** Applies {id: value}; parameters not mentioned are reset to their defaults. Returns unknown ids. */
juce::StringArray applyParameterValues (juce::AudioProcessor& processor, const juce::var& values);

/**
    Use instead of apvts.replaceState() in setStateInformation(). replaceState() skips parameters whose
    snapped value already matches (e.g. a bool sitting at 0.18 reads as "off"), leaving the raw value
    wrong; this forces every parameter to exactly what the saved state says.
*/
void replaceStateExactly (juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& newState);

/**
    Factory presets are JSON files embedded with juce_add_binary_data; user presets are the same JSON
    format in ~/Music/<company>/<plugin>/Presets. JSON values are in real units (Hz, ms, dB, choice
    names) so presets are readable and diffable. Parameters a preset leaves out get their default.

    {
      "name": "Astro Slide", "category": "TRAVIS", "author": "...", "description": "...",
      "params": { "cutoff": 420, "dirtType": "Tape", "legato": true }
    }
*/
class PresetManager : public juce::ChangeBroadcaster
{
public:
    struct Preset
    {
        juce::String name, category, author, description, sortKey;
        juce::var params;
        bool isFactory = true;
        juce::File file;
    };

    static constexpr const char* kUserCategory = "USER";

    PresetManager (juce::AudioProcessorValueTreeState& state, juce::StringArray categoryOrder,
                   const juce::String& company, const juce::String& pluginName);

    /** Parses and registers one embedded preset. Returns false (and logs) on malformed JSON. */
    bool addFactoryPreset (const juce::String& json, const juce::String& sortKey);

    /** Sorts factory presets by category order then sort key, and scans the user folder. */
    void finishLoading();
    void rescanUserPresets();

    int size() const noexcept                  { return presets.size(); }
    int getNumFactoryPresets() const noexcept  { return numFactory; }
    const Preset& get (int i) const            { return presets.getReference (i); }
    int indexOf (const juce::String& name, const juce::String& category) const;

    juce::StringArray getCategories() const;
    juce::Array<int> getIndicesInCategory (const juce::String& category) const;

    bool loadPreset (int index);
    void loadRelative (int delta);
    void loadInit();

    int getCurrentIndex() const noexcept { return currentIndex; }
    juce::String getCurrentName() const;
    juce::String getCurrentCategory() const;
    juce::String getCurrentDescription() const;

    bool saveUserPreset (const juce::String& name);
    juce::File getUserPresetDirectory() const { return userDir; }

    /** Current parameter values as a JSON object in real units. */
    juce::var captureParams() const;
    void applyParams (const juce::var& params);

    void writeState (juce::ValueTree& state) const;
    void readState (const juce::ValueTree& state);

    static juce::String sanitiseFileName (const juce::String& name);

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::StringArray categoryOrder;
    juce::File userDir;
    juce::Array<Preset> presets;
    int numFactory = 0;
    int currentIndex = -1;
    juce::String orphanName; // name restored from a session whose preset no longer exists

    void sortFactory();
};
} // namespace dd
