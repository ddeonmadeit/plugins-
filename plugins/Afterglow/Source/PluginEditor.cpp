#include "PluginEditor.h"

namespace
{
namespace sunset = dd::gui::sunset;

dd::gui::Palette afterglowPalette()
{
    dd::gui::Palette p;   // dark brown matte hardware; highlights only from the sunset set
    p.accent = sunset::orange;
    p.accent2 = sunset::amber;
    return p;
}

constexpr int kDeckH = 224;
} // namespace

//==============================================================================
LampSwitch::LampSwitch (juce::RangedAudioParameter& p, const juce::String& l, juce::Colour c)
    : attachment (p, [this] (float v) { on = v >= 0.5f; repaint(); }), label (l), colour (c)
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("Switch " + l.toLowerCase() + " on or off");
}

void LampSwitch::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    dd::gui::paintLamp (g, { r.getX() + 12.0f, r.getCentreY() }, 5.5f, colour, on ? 1.0f : 0.0f);
    dd::gui::drawPrinted (g, label, r.withTrimmedLeft (28.0f), on || isMouseOver() ? juce::Colour (0xffeadcc3) : juce::Colour (0xff9c876e),
                          dd::gui::LookAndFeel::font (13.0f, true).withExtraKerningFactor (0.12f), juce::Justification::centredLeft);
}

void LampSwitch::mouseDown (const juce::MouseEvent&) { attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

//==============================================================================
AfterglowEditor::Content::Content (AfterglowProcessor& p)
    : proc (p), presetBar (p.presets), horizon (p)
{
    const std::tuple<const char*, const char*, juce::Colour, const char*> macroDefs[] = {
        { "mSwell", "Swell", sunset::yellow, "Slower or faster attack" },
        { "mTail",  "Tail",  sunset::amber,  "Shorter or longer release" },
        { "mBlend", "Blend", sunset::orange, "Balance between layer A and layer B" },
        { "mGrit",  "Grit",  sunset::red,    "Clean or saturated, tape and lo-fi" },
        { "mEcho",  "Echo",  sunset::ember,  "Less or more delay" },
        { "mSpace", "Space", sunset::amber,  "Smaller or bigger room" } };
    for (auto& [id, title, colour, tip] : macroDefs)
    {
        auto k = std::make_unique<dd::gui::Knob> (p.apvts, id, title, colour);
        k->setLabelScale (1.35f);
        k->getSlider().setTooltip (juce::String (tip) + ". Centre = the preset as designed (double-click).");
        addAndMakeVisible (*k);
        macros.push_back (std::move (k));
    }

    const std::tuple<const char*, const char*, juce::Colour> fxDefs[] = {
        { "tapeOn", "TAPE", sunset::red }, { "ensOn", "ENSEMBLE", sunset::orange }, { "phaserOn", "PHASER", sunset::amber },
        { "delayOn", "DELAY", sunset::ember }, { "reverbOn", "REVERB", sunset::yellow }, { "pumpOn", "PUMP", sunset::red } };
    for (auto& [id, label, colour] : fxDefs)
    {
        auto s = std::make_unique<LampSwitch> (*p.apvts.getParameter (id), label, colour);
        addAndMakeVisible (*s);
        switches.push_back (std::move (s));
    }

    addAndMakeVisible (presetBar);
    addAndMakeVisible (horizon);
}

void AfterglowEditor::Content::paint (juce::Graphics& g)
{
    const auto pal = afterglowPalette();
    dd::gui::paintBackdrop (g, getLocalBounds(), pal);

    // Wordmark with a 70s sunset stripe band running off it.
    auto head = getLocalBounds().removeFromTop (12 + 64).reduced (16, 0).withTrimmedTop (12).toFloat();
    auto logo = head.removeFromLeft (300.0f);
    const auto wordFont = dd::gui::LookAndFeel::display (40.0f).withExtraKerningFactor (0.04f);
    const float wordW = juce::GlyphArrangement::getStringWidth (wordFont, "AFTERGLOW");
    auto wordRow = logo.removeFromTop (42.0f);
    dd::gui::drawPrinted (g, "AFTERGLOW", wordRow, pal.text, wordFont, juce::Justification::bottomLeft);
    dd::gui::paintSunsetStripes (g, { wordRow.getX() + wordW + 12.0f, wordRow.getBottom() - 21.0f, 300.0f - wordW - 24.0f, 12.0f });
    dd::gui::drawPrinted (g, "PADS  /  TEXTURES  /  SOUNDSCAPES", logo.removeFromTop (18.0f), pal.accent2,
                          dd::gui::LookAndFeel::font (12.0f, true).withExtraKerningFactor (0.22f), juce::Justification::topLeft);
    auto right = head.removeFromRight (160.0f);
    dd::gui::drawPrinted (g, juce::String ("ddeonmadeit  v") + DD_PLUGIN_VERSION, right, pal.dim,
                          dd::gui::LookAndFeel::font (12.0f), juce::Justification::centredRight);

    // The macro deck: one faceplate, macros on the left, effect switches on the right.
    const auto plate = deck.toFloat();
    dd::gui::paintFaceplate (g, plate, pal);
    const auto title = dd::gui::LookAndFeel::font (13.0f, true).withExtraKerningFactor (0.1f);
    dd::gui::paintLamp (g, { plate.getX() + 22.0f, plate.getY() + 19.0f }, 3.2f, pal.accent, 1.0f);
    dd::gui::drawPrinted (g, "MACROS", { plate.getX() + 32.0f, plate.getY() + 8.0f, 200.0f, 22.0f }, pal.text, title, juce::Justification::centredLeft);
    const float split = plate.getRight() - 236.0f;
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (split, plate.getY() + 14.0f, 1.0f, plate.getHeight() - 28.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (split + 1.0f, plate.getY() + 14.0f, 1.0f, plate.getHeight() - 28.0f);
    dd::gui::paintLamp (g, { split + 24.0f, plate.getY() + 19.0f }, 3.2f, pal.accent2, 1.0f);
    dd::gui::drawPrinted (g, "EFFECTS", { split + 34.0f, plate.getY() + 8.0f, 200.0f, 22.0f }, pal.text, title, juce::Justification::centredLeft);
}

void AfterglowEditor::Content::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto head = r.removeFromTop (64);
    head.removeFromLeft (300);
    head.removeFromRight (170);
    presetBar.setBounds (head.withSizeKeepingCentre (std::min (head.getWidth(), 600), 54));
    r.removeFromTop (8);

    deck = r.removeFromBottom (kDeckH);
    r.removeFromBottom (10);
    horizon.setBounds (r);

    auto inner = deck.reduced (18, 0).withTrimmedTop (36).withTrimmedBottom (16);
    auto fx = inner.removeFromRight (236 - 18);
    fx.removeFromLeft (22);
    const int knobW = inner.getWidth() / (int) macros.size();
    for (auto& k : macros)
        k->setBounds (inner.removeFromLeft (knobW).reduced (6, 0));

    const int rowH = fx.getHeight() / 3;
    for (size_t i = 0; i < switches.size(); ++i)
    {
        const int col = (int) (i / 3), row = (int) (i % 3);
        switches[i]->setBounds (fx.getX() + col * (fx.getWidth() / 2), fx.getY() + row * rowH, fx.getWidth() / 2, rowH);
    }
}

//==============================================================================
AfterglowEditor::AfterglowEditor (AfterglowProcessor& p)
    : AudioProcessorEditor (p), proc (p), lnf (afterglowPalette())
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

AfterglowEditor::~AfterglowEditor()
{
    content.reset();
    setLookAndFeel (nullptr);
}

void AfterglowEditor::paint (juce::Graphics& g)
{
    g.fillAll (lnf.palette().background);
}

void AfterglowEditor::resized()
{
    const float scale = (float) getWidth() / (float) kBaseW;
    content->setTransform (juce::AffineTransform::scale (scale));
    if (constructed)
        proc.editorScale = scale;
}
