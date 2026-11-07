#pragma once

#include "MirageView.h"
#include "PluginProcessor.h"

/** A wide, resizable window: stretch it to sit under the tracks when recording the screen. */
class MirageEditor : public juce::AudioProcessorEditor
{
public:
    explicit MirageEditor (MirageProcessor&);
    ~MirageEditor() override { setLookAndFeel (nullptr); }
    void resized() override;
    void paint (juce::Graphics&) override {}

private:
    MirageProcessor& proc;
    mirage::MirageView view;
};
