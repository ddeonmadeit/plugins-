#pragma once

#include "MirageView.h"
#include "PluginProcessor.h"

/** A wide, resizable window: stretch it to sit under the tracks when recording the screen. */
class MirageEditor : public juce::AudioProcessorEditor
{
    /** Only the panel's colours, down to the resize grip. */
    struct Look : public juce::LookAndFeel_V4
    {
        void drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging) override
        {
            g.setColour (juce::Colour (0xffff6a1f).withAlpha (over || dragging ? 0.9f : 0.5f));
            for (float i = 0.3f; i < 1.0f; i += 0.3f)
                g.drawLine ((float) w * i, (float) h, (float) w, (float) h * i, 1.2f);
        }
    };

public:
    explicit MirageEditor (MirageProcessor&);
    ~MirageEditor() override { setLookAndFeel (nullptr); }
    void resized() override;
    void paint (juce::Graphics&) override {}

private:
    Look look;
    MirageProcessor& proc;
    mirage::MirageView view;
};
