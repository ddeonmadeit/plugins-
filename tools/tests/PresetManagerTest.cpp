/*
    PresetManager behaviour the plugins rely on: user presets keep their category, saving over a factory
    preset stores an override that takes its place (and survives a "plugin update", i.e. a fresh manager
    reading the same folder), restoring brings the factory sound back, and edits mark the preset modified.
*/
#include <juce_audio_processors/juce_audio_processors.h>
#include <dd_core/dd_core.h>

#include <cstdlib>
#include <iostream>

namespace
{
int failures = 0;
void check (bool ok, const char* what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (! ok) ++failures;
}

struct TestProcessor : juce::AudioProcessor
{
    TestProcessor() : apvts (*this, nullptr, "T", layout()) {}
    static juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gain", 1 }, "Gain", 0.0f, 10.0f, 1.0f));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "perfHold", 1 }, "Hold", false));
        return l;
    }
    const juce::String getName() const override { return "T"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    using AudioProcessor::processBlock;
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    juce::AudioProcessorValueTreeState apvts;
};

float gainOf (TestProcessor& p) { return p.apvts.getRawParameterValue ("gain")->load(); }
void setGain (TestProcessor& p, float v) { auto* g = p.apvts.getParameter ("gain"); g->setValueNotifyingHost (g->convertTo0to1 (v)); }

std::unique_ptr<dd::PresetManager> makeManager (TestProcessor& p)
{
    auto pm = std::make_unique<dd::PresetManager> (p.apvts, juce::StringArray { "80S", "STUDIO" }, "ddtest", "T");
    pm->addFactoryPreset (R"({"name":"Air","category":"80S","params":{"gain":2}})", "01_Air.json");
    pm->addFactoryPreset (R"({"name":"Lead","category":"STUDIO","params":{"gain":3}})", "02_Lead.json");
    pm->finishLoading();
    return pm;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dd_tests_home");
    home.deleteRecursively();
    home.createDirectory();
    setenv ("HOME", home.getFullPathName().toRawUTF8(), 1);

    TestProcessor proc;
    {
        auto pm = makeManager (proc);
        std::cout << "presets\n";
        check (pm->getUserPresetDirectory().isAChildOf (home), "user folder is under the home folder");
        pm->loadPreset (0);
        check (std::abs (gainOf (proc) - 2.0f) < 1e-4f && ! pm->isModified(), "loading a factory preset applies it, unmodified");

        setGain (proc, 7.0f);
        check (pm->isModified(), "an edit marks the preset modified");
        auto* hold = proc.apvts.getParameter ("perfHold");
        pm->saveOverCurrent();
        check (! pm->isModified() && pm->get (0).isOverride, "saving over a factory preset makes an override");
        hold->setValueNotifyingHost (1.0f);
        check (! pm->isModified(), "performance switches don't mark the sound modified");

        pm->loadPreset (1);
        pm->loadPreset (0);
        check (std::abs (gainOf (proc) - 7.0f) < 1e-4f, "the override replaces the factory sound in its slot");

        setGain (proc, 5.0f);
        check (pm->saveUserPreset ("Mine", "STUDIO"), "save as new in a chosen category");
        const int mine = pm->indexOf ("Mine", "STUDIO");
        check (mine >= pm->getNumFactoryPresets() && pm->get (mine).category == "STUDIO", "the new preset keeps its category");
        check (pm->getCurrentIndex() == mine, "the new preset becomes the current one");
        setGain (proc, 6.0f);
        pm->saveOverCurrent();
        pm->loadPreset (1);
        pm->loadPreset (pm->indexOf ("Mine", "STUDIO"));
        check (std::abs (gainOf (proc) - 6.0f) < 1e-4f, "saving over a user preset rewrites it");
    }
    {
        // A plugin update: a fresh manager with the same factory list reads the same user folder.
        auto pm = makeManager (proc);
        std::cout << "after an update\n";
        check (pm->get (0).isOverride, "the factory override is still there");
        pm->loadPreset (0);
        check (std::abs (gainOf (proc) - 7.0f) < 1e-4f, "and still sounds like the user's version");
        check (pm->indexOf ("Mine", "STUDIO") >= 0, "user presets are still there");

        pm->restoreFactory (0);
        check (! pm->get (0).isOverride && std::abs (gainOf (proc) - 2.0f) < 1e-4f, "restore factory brings the original back");
        const int mine = pm->indexOf ("Mine", "STUDIO");
        pm->deleteUserPreset (mine);
        check (pm->indexOf ("Mine", "STUDIO") < 0, "delete removes a user preset");
    }
    home.deleteRecursively();
    std::cout << (failures == 0 ? "all passed\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
