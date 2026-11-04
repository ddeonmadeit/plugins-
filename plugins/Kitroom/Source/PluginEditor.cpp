#include "PluginEditor.h"

namespace
{
dd::gui::Palette kitroomPalette()
{
    dd::gui::Palette p;   // brushed brown metal, silver hardware; warm colour only for things that glow
    p.accent = dd::gui::sunset::orange;
    p.accent2 = dd::gui::sunset::amber;
    return p;
}

const juce::Colour kMicAccent  = dd::gui::sunset::orange;
const juce::Colour kBusAccent  = dd::gui::sunset::red;
const juce::Colour kFeelAccent = dd::gui::sunset::yellow;

using dd::gui::Knob;
} // namespace

KitroomEditor::Content::Content (KitroomProcessor& p)
    : proc (p),
      topBar (p.presets, "KIT", "ROOM", "Live Drum Kit / Multi-Mic"),
      description (topBar.presets()),
      kit (p),
      element (p),
      mix ("Mix", 6, 3, kMicAccent)
{
    auto& s = p.apvts;
    // Mic buses, then the bus processing, then playing feel; drive type and the scope on the right.
    const std::tuple<const char*, const char*, juce::Colour> knobs[] = {
        { "masterClose", "Close Mics", kMicAccent }, { "masterOverheads", "Overheads", kMicAccent },
        { "masterRoom", "Room", kMicAccent },        { "width", "Width", kMicAccent },
        { "glue", "Glue", kBusAccent },              { "drive", "Drive", kBusAccent },
        { "crush", "Crush", kBusAccent },            { "clip", "Clip", kBusAccent },
        { "velCurve", "Velocity", kFeelAccent },     { "humanize", "Humanize", kFeelAccent },
        { "tone", "Tone", kFeelAccent },             { "outGain", "Output", kFeelAccent } };
    int i = 0;
    for (auto& [id, title, colour] : knobs)
    {
        mix.add<Knob> (s, id, title, colour);
        mix.place (i % 4, i / 4);
        ++i;
    }
    mix.add<dd::gui::Choice> (s, "driveType", "Drive Type");
    mix.place (4, 0, 2, 1);
    mix.add<dd::gui::Scope> (p.scope);
    mix.place (4, 1, 2, 2);

    kit.onSelect = [this] (int slot) { element.setSlot (slot); };

    description.setHint (juce::String ("Click a piece to edit it  /  ddeonmadeit v") + DD_PLUGIN_VERSION);
    dd::gui::markRaised (element, 14.0f);
    addAndMakeVisible (topBar);
    addAndMakeVisible (description);
    addAndMakeVisible (kit);
    addAndMakeVisible (element);
    addAndMakeVisible (mix);
    addAndMakeVisible (atmosphere);
}

void KitroomEditor::Content::paint (juce::Graphics& g)
{
    dd::gui::paintBackdrop (g, getLocalBounds(), kitroomPalette());
    dd::gui::paintPanelShadows (g, *this);
    dd::gui::paintBezel (g, kit.getBounds().toFloat(), 10.0f, 7.0f);   // the kit is seen through a window set into the chassis
}

void KitroomEditor::Content::resized()
{
    topBar.setBounds (0, 8, getWidth(), dd::gui::TopBar::kHeight);
    description.setBounds (26, topBar.getBottom() + 6, getWidth() - 52, 22);
    atmosphere.setBounds (getLocalBounds());

    auto r = getLocalBounds().reduced (12, 0).withTrimmedTop (description.getBottom() + 6).withTrimmedBottom (12);
    kit.setBounds (r.removeFromTop (KitView::kH).withSizeKeepingCentre (KitView::kW, KitView::kH));
    r.removeFromTop (12);
    mix.setBounds (r.removeFromRight (mix.preferredSize().x + 8));
    r.removeFromRight (10);
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
