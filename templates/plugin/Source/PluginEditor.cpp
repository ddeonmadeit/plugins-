#include "PluginEditor.h"

namespace
{
dd::gui::Palette palette()
{
    dd::gui::Palette p;
    p.accent = juce::Colour (0xff3fd0c9); // pick this plugin's identity colour
    return p;
}
} // namespace

__NAME__Editor::__NAME__Editor (__NAME__Processor& p)
    : AudioProcessorEditor (p), lnf (palette()), presetBar (p.presets),
//@SYNTH
      main ("__NAME__", 5, 2, palette().accent)
//@END
//@EFFECT
      main ("__NAME__", 4, 2, palette().accent)
//@END
{
    setLookAndFeel (&lnf);
    auto& s = p.apvts;
    const auto a = palette().accent;
//@SYNTH
    main.add<dd::gui::Choice> (s, "wave", "Wave");
    main.add<dd::gui::Knob> (s, "cutoff", "Cutoff", a);
    main.add<dd::gui::Knob> (s, "resonance", "Reso", a);
    main.add<dd::gui::Knob> (s, "level", "Level", a);
    main.add<dd::gui::Knob> (s, "outGain", "Output", a);
    main.add<dd::gui::Knob> (s, "attack", "Attack", a);
    main.add<dd::gui::Knob> (s, "decay", "Decay", a);
    main.add<dd::gui::Knob> (s, "sustain", "Sustain", a);
    main.add<dd::gui::Knob> (s, "release", "Release", a);
//@END
//@EFFECT
    main.add<dd::gui::Choice> (s, "dirtType", "Type");
    main.add<dd::gui::Knob> (s, "drive", "Drive", a);
    main.add<dd::gui::Knob> (s, "tone", "Tone", a);
    main.add<dd::gui::Knob> (s, "mix", "Mix", a);
    main.add<dd::gui::Knob> (s, "outGain", "Output", a);
//@END
    scope = &main.add<dd::gui::Scope> (p.scope);
//@EFFECT
    main.place (1, 1, 3, 1);
//@END
    addAndMakeVisible (presetBar);
    addAndMakeVisible (main);
    setSize (std::max (700, main.preferredSize().x + 24), main.preferredSize().y + 100);
}

__NAME__Editor::~__NAME__Editor() { setLookAndFeel (nullptr); }

void __NAME__Editor::paint (juce::Graphics& g)
{
    g.fillAll (lnf.palette().background);
    g.setColour (lnf.palette().text);
    g.setFont (dd::gui::LookAndFeel::font (24.0f, true));
    g.drawText ("__NAME__", getLocalBounds().removeFromTop (56).reduced (14, 0), juce::Justification::centredLeft);
}

void __NAME__Editor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto head = r.removeFromTop (56);
    head.removeFromLeft (180);
    presetBar.setBounds (head);
    r.removeFromTop (8);
    main.setBounds (r);
}
