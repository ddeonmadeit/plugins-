#include "PluginEditor.h"

MirageEditor::MirageEditor (MirageProcessor& p) : AudioProcessorEditor (p), proc (p), view (p)
{
    const int w = p.editorWidth.load(), h = p.editorHeight.load();   // read first: resized() stores every size
    setLookAndFeel (&look);
    addAndMakeVisible (view);
    setResizable (true, true);
    setResizeLimits (900, 250, 4096, 1400);
    setSize (juce::jlimit (900, 4096, w), juce::jlimit (250, 1400, h));
}

void MirageEditor::resized()
{
    view.setBounds (getLocalBounds());
    proc.editorWidth.store (getWidth());
    proc.editorHeight.store (getHeight());
}
