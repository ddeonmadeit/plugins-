#include "PluginEditor.h"

MirageEditor::MirageEditor (MirageProcessor& p) : AudioProcessorEditor (p), proc (p), view (p)
{
    const int w = p.editorWidth.load(), h = p.editorHeight.load();   // read first: resized() stores every size
    setLookAndFeel (&view.getPanelLook());   // the resize grip in the panel's colours
    addAndMakeVisible (view);
    setResizable (true, true);
    setResizeLimits (600, 200, 4096, 1400);
    // Never open wider or taller than the screen (the default 1600 is wider than a 13" laptop).
    const auto area = dd::gui::screenArea();
    setSize (juce::jlimit (600, std::max (600, (int) (area.getWidth() * 0.92f)), w),
             juce::jlimit (200, std::max (200, (int) (area.getHeight() * 0.6f)), h));
}

void MirageEditor::resized()
{
    view.setBounds (getLocalBounds());
    proc.editorWidth.store (getWidth());
    proc.editorHeight.store (getHeight());
}
