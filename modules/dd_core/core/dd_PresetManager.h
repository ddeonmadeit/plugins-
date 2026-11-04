#pragma once

namespace dd
{
/** Every parameter of `processor` as {id: value} in real units (choices by name, bools as bool). */
/** Performance parameters (ids starting with "perf", e.g. "perfPoly") belong to the plugin instance,
    not the sound: presets never apply or capture them, so they survive preset changes. */
inline bool isPerformanceParam (const juce::String& id) { return id.startsWith ("perf"); }

juce::var captureParameterValues (juce::AudioProcessor& processor);

/** Applies {id: value}; parameters not mentioned are reset to their defaults. Returns unknown ids. */
juce::StringArray applyParameterValues (juce::AudioProcessor& processor, const juce::var& values);

/**
    Use instead of apvts.replaceState() in setStateInformation(). replaceState() skips parameters whose
    snapped value already matches (e.g. a bool sitting at 0.18 reads as "off"), leaving the raw value
    wrong; this forces every parameter to exactly what the saved state says.
*/
void replaceStateExactly (juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& newState);

/** Call after restoring `savedState`: if it was saved before `paramId` existed, set the parameter to
    `oldValue` (real units), i.e. how the plugin behaved back then, instead of the new default.
    Keeps existing projects sounding the same when a new switch defaults to on. */
void keepOldDefault (juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& savedState,
                     const juce::String& paramId, float oldValue);

/**
    Factory presets are JSON files embedded with juce_add_binary_data; user presets are the same JSON
    format in ~/Music/<company>/<plugin>/Presets, a folder installers and updates never touch. JSON values
    are in real units (Hz, ms, dB, choice names) so presets are readable and diffable. Parameters a
    preset leaves out get their default.

    Saving over a factory preset writes a user file marked "overrides": it takes the factory preset's
    place (and host program slot) until it is deleted ("Restore factory version"), so updates never
    replace the user's version. User presets keep the category they were saved in.

    {
      "name": "Astro Slide", "category": "TRAVIS", "author": "...", "description": "...",
      "params": { "cutoff": 420, "dirtType": "Tape", "legato": true }
    }
*/
class PresetManager : public juce::ChangeBroadcaster,
                      private juce::AudioProcessorParameter::Listener,
                      private juce::AsyncUpdater
{
public:
    struct Preset
    {
        juce::String name, category, author, description, sortKey;
        juce::var params;
        juce::var extra;          // plugin-specific state that isn't a parameter (see onApplyExtra)
        bool isFactory = true;
        bool isOverride = false;  // a factory preset the user saved over (file holds their version)
        juce::File file;
        juce::var factoryParams, factoryExtra;
        juce::String factoryDescription;
    };

    static constexpr const char* kUserCategory = "USER";

    PresetManager (juce::AudioProcessorValueTreeState& state, juce::StringArray categoryOrder,
                   const juce::String& company, const juce::String& pluginName);
    ~PresetManager() override;

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

    /** Saves the current sound as a new user preset (category defaults to USER). */
    bool saveUserPreset (const juce::String& name, const juce::String& category = kUserCategory);
    /** Saves over the current preset: rewrites a user preset, or stores an override of a factory one. */
    bool saveOverCurrent();
    /** Deletes the user's version of an overridden factory preset and reloads the original. */
    bool restoreFactory (int index);
    /** Deletes a user preset's file. */
    bool deleteUserPreset (int index);
    juce::File getUserPresetDirectory() const { return userDir; }

    /** True once a parameter has changed since the preset was loaded or saved. */
    bool isModified() const noexcept { return modified.load(); }

    /** Current parameter values as a JSON object in real units. */
    juce::var captureParams() const;
    void applyParams (const juce::var& params);

    void writeState (juce::ValueTree& state) const;
    void readState (const juce::ValueTree& state);

    static juce::String sanitiseFileName (const juce::String& name);

    /**
        Optional hooks for state that isn't a parameter (e.g. which sample sits in a drum slot).
        Presets carry it under "extra". apply is called with an empty var for "Init".
    */
    std::function<void (const juce::var& extra)> onApplyExtra;
    std::function<juce::var()> onCaptureExtra;

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::StringArray categoryOrder;
    juce::File userDir;
    juce::Array<Preset> presets;
    int numFactory = 0;
    int currentIndex = -1;
    juce::String orphanName; // name restored from a session whose preset no longer exists
    std::vector<bool> perfParam;
    std::atomic<bool> modified { false };
    bool applying = false;

    void sortFactory();
    void apply (const Preset&);
    bool writeFile (const juce::File&, const juce::String& name, const juce::String& category,
                    const juce::String& description, bool overrides);
    void parameterValueChanged (int index, float) override;
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override { sendChangeMessage(); }
};
} // namespace dd
