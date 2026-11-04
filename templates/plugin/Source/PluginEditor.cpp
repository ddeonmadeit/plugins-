#include "PluginEditor.h"

namespace
{
dd::gui::Palette palette()
{
    dd::gui::Palette p;   // house style (rawleads): colours come from the DARK / LITE theme tokens
    p.accent = dd::gui::sunset::orange;   // signal orange: only for things that glow
    return p;
}
} // namespace

__NAME__Editor::__NAME__Editor (__NAME__Processor& p)
    : AudioProcessorEditor (p), lnf (palette()),
      topBar (p.presets, "__NAME__", "", "__NAME__"),   // brand + accent half (e.g. "SUB", "GRIT") and a short subtitle
      description (topBar.presets()),
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
    description.setHint (juce::String ("ddeonmadeit  v") + DD_PLUGIN_VERSION);
    addAndMakeVisible (topBar);
    addAndMakeVisible (description);
    addAndMakeVisible (main);
    addAndMakeVisible (atmosphere);
    setSize (std::max (900, main.preferredSize().x + 28), main.preferredSize().y + 108 + 14);
}

__NAME__Editor::~__NAME__Editor() { setLookAndFeel (nullptr); }

void __NAME__Editor::paint (juce::Graphics& g)
{
    dd::gui::paintBackdrop (g, getLocalBounds(), lnf.palette());
    dd::gui::paintPanelShadows (g, *this);
}

void __NAME__Editor::resized()
{
    topBar.setBounds (0, 8, getWidth(), dd::gui::TopBar::kHeight);
    description.setBounds (26, topBar.getBottom() + 6, getWidth() - 52, 22);
    main.setBounds (getLocalBounds().reduced (14, 0).withTrimmedTop (description.getBottom() + 6).withTrimmedBottom (14));
    atmosphere.setBounds (getLocalBounds());
}
