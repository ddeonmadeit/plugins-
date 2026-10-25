#include "PluginEditor.h"

namespace
{
dd::gui::Palette kitroomPalette()
{
    dd::gui::Palette p;
    p.background = juce::Colour (0xff0d0b0a);
    p.panel      = juce::Colour (0xff171412);
    p.panelEdge  = juce::Colour (0xff2c2622);
    p.track      = juce::Colour (0xff2e2925);
    p.accent     = juce::Colour (0xffe8a33d);   // brass
    p.accent2    = juce::Colour (0xffc9774a);   // copper
    return p;
}

const juce::Colour kMicAccent  { 0xffe8a33d };
const juce::Colour kBusAccent  { 0xffc9774a };
const juce::Colour kFeelAccent { 0xff8fb3a0 };

using dd::gui::Knob;
} // namespace

KitroomEditor::Content::Content (KitroomProcessor& p)
    : proc (p),
      presetBar (p.presets),
      kit (p),
      element (p),
      mix ("Mix", 6, 3, kMicAccent)
{
    auto& s = p.apvts;
    // Left block: mic buses, then the master bus, then playing feel. The scope takes the right half.
    const std::tuple<const char*, const char*, juce::Colour> knobs[] = {
        { "masterClose", "Close Mics", kMicAccent }, { "masterOverheads", "Overheads", kMicAccent }, { "masterRoom", "Room", kMicAccent },
        { "width", "Width", kBusAccent },            { "glue", "Glue", kBusAccent },                 { "clip", "Clip", kBusAccent },
        { "velCurve", "Velocity", kFeelAccent },     { "humanize", "Humanize", kFeelAccent },        { "outGain", "Output", kBusAccent } };
    int i = 0;
    for (auto& [id, title, colour] : knobs)
    {
        mix.add<Knob> (s, id, title, colour);
        mix.place (i % 3, i / 3);
        ++i;
    }
    mix.add<dd::gui::Scope> (p.scope);
    mix.place (3, 0, 3, 3);

    kit.onSelect = [this] (int slot) { element.setSlot (slot); };

    addAndMakeVisible (presetBar);
    addAndMakeVisible (kit);
    addAndMakeVisible (element);
    addAndMakeVisible (mix);
}

void KitroomEditor::Content::paint (juce::Graphics& g)
{
    const auto pal = kitroomPalette();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a1410), 0.0f, 0.0f,
                                             pal.background, 0.0f, (float) getHeight() * 0.5f, false));
    g.fillAll();

    auto head = getLocalBounds().removeFromTop (12 + 64).reduced (16, 0).withTrimmedTop (12);
    auto logo = head.removeFromLeft (300);
    g.setColour (pal.text);
    g.setFont (dd::gui::LookAndFeel::font (34.0f, true).withExtraKerningFactor (0.06f));
    g.drawText ("KITROOM", logo.removeFromTop (40), juce::Justification::bottomLeft);
    g.setColour (pal.accent);
    g.setFont (dd::gui::LookAndFeel::font (11.0f, true).withExtraKerningFactor (0.25f));
    g.drawText ("LIVE DRUM KIT  /  MULTI-MIC", logo.removeFromTop (18), juce::Justification::topLeft);

    auto right = head.removeFromRight (170);
    g.setColour (pal.dim);
    g.setFont (dd::gui::LookAndFeel::font (11.0f));
    g.drawText (juce::String ("ddeonmadeit  v") + DD_PLUGIN_VERSION, right.removeFromTop (32), juce::Justification::bottomRight);
    g.drawText ("click a piece to edit it", right, juce::Justification::topRight);
}

void KitroomEditor::Content::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto head = r.removeFromTop (64);
    head.removeFromLeft (300);
    head.removeFromRight (170);
    presetBar.setBounds (head.withSizeKeepingCentre (std::min (head.getWidth(), 640), 54));
    r.removeFromTop (8);

    kit.setBounds (r.removeFromTop (KitView::kH).withSizeKeepingCentre (KitView::kW, KitView::kH));
    r.removeFromTop (10);
    mix.setBounds (r.removeFromRight (mix.preferredSize().x + 8));
    r.removeFromRight (8);
    element.setBounds (r);
}

//==============================================================================
KitroomEditor::KitroomEditor (KitroomProcessor& p)
    : AudioProcessorEditor (p), proc (p), lnf (kitroomPalette())
{
    setLookAndFeel (&lnf);
    content = std::make_unique<Content> (p);
    content->setBounds (0, 0, kBaseW, kBaseH);
    addAndMakeVisible (*content);

    // setResizeLimits() triggers resized(), so read the saved zoom first.
    const float savedScale = proc.editorScale;
    setResizable (true, true);
    setResizeLimits ((int) (kBaseW * 0.6f), (int) (kBaseH * 0.6f), (int) (kBaseW * 1.8f), (int) (kBaseH * 1.8f));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setSize ((int) std::round (kBaseW * savedScale), (int) std::round (kBaseH * savedScale));
    constructed = true;
}

KitroomEditor::~KitroomEditor()
{
    content.reset();
    setLookAndFeel (nullptr);
}

void KitroomEditor::paint (juce::Graphics& g)
{
    g.fillAll (lnf.palette().background);
}

void KitroomEditor::resized()
{
    const float scale = (float) getWidth() / (float) kBaseW;
    content->setTransform (juce::AffineTransform::scale (scale));
    if (constructed)
        proc.editorScale = scale;
}
