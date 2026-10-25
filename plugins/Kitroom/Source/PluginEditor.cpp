#include "PluginEditor.h"

namespace
{
dd::gui::Palette kitroomPalette()
{
    dd::gui::Palette p;   // dark brown matte hardware; highlights only from the sunset set
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
      presetBar (p.presets),
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

    addAndMakeVisible (presetBar);
    addAndMakeVisible (kit);
    addAndMakeVisible (element);
    addAndMakeVisible (mix);
}

void KitroomEditor::Content::paint (juce::Graphics& g)
{
    const auto pal = kitroomPalette();
    dd::gui::paintBackdrop (g, getLocalBounds(), pal);

    // Wordmark with a 70s sunset stripe band running off it.
    auto head = getLocalBounds().removeFromTop (12 + 64).reduced (16, 0).withTrimmedTop (12).toFloat();
    auto logo = head.removeFromLeft (300.0f);
    const auto wordFont = dd::gui::LookAndFeel::display (40.0f).withExtraKerningFactor (0.04f);
    const float wordW = juce::GlyphArrangement::getStringWidth (wordFont, "KITROOM");
    auto wordRow = logo.removeFromTop (42.0f);
    dd::gui::drawPrinted (g, "KITROOM", wordRow, pal.text, wordFont, juce::Justification::bottomLeft);
    dd::gui::paintSunsetStripes (g, { wordRow.getX() + wordW + 12.0f, wordRow.getBottom() - 21.0f, 300.0f - wordW - 24.0f, 12.0f });
    dd::gui::drawPrinted (g, "LIVE DRUM KIT  /  MULTI-MIC", logo.removeFromTop (18.0f), pal.accent2,
                          dd::gui::LookAndFeel::font (12.0f, true).withExtraKerningFactor (0.3f), juce::Justification::topLeft);

    auto right = head.removeFromRight (170.0f);
    const auto small = dd::gui::LookAndFeel::font (12.0f);
    dd::gui::drawPrinted (g, juce::String ("ddeonmadeit  v") + DD_PLUGIN_VERSION, right.removeFromTop (32.0f), pal.dim, small,
                          juce::Justification::bottomRight);
    dd::gui::drawPrinted (g, "click a piece to edit it", right, pal.dim, small, juce::Justification::topRight);
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
