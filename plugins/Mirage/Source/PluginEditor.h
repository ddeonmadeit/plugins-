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

    /** Draw on the CPU, not with macOS's asynchronous (GPU-replayed) Core Graphics. Mirage repaints
        its whole window 60 times a second from its own images, so the GPU path buys little, and on
        the CI's virtual Macs it crashed the host inside the paravirtual GPU driver while pluginval
        automated the editor (AppleParavirtCommandBuffer endCurrentChunk). */
    bool wantsLayerBackedView() const override { return false; }

private:
    MirageProcessor& proc;
    mirage::MirageView view;
};
