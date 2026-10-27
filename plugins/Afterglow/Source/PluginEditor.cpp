#include "PluginEditor.h"

namespace
{
namespace sunset = dd::gui::sunset;

dd::gui::Palette afterglowPalette()
{
    dd::gui::Palette p;   // dark brown matte hardware; highlights only from the sunset set
    p.background = juce::Colour (0xff0c0704);   // deeper than the house chassis: the sunset glows against near-black
    p.panel = juce::Colour (0xff1d110a);
    p.accent = sunset::orange;
    p.accent2 = sunset::amber;
    return p;
}

constexpr int kDeckH = 224;
} // namespace

//==============================================================================
AfterglowEditor::Content::Content (AfterglowProcessor& p)
    : proc (p), presetBar (p.presets), polySwitch (*p.apvts.getParameter ("perfPoly"), "POLY", sunset::amber), horizon (p)
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
        { "distOn", "DIRT", sunset::red }, { "tapeOn", "TAPE", sunset::ember }, { "ensOn", "ENSEMBLE", sunset::orange },
        { "phaserOn", "PHASER", sunset::amber }, { "delayOn", "DELAY", sunset::ember }, { "reverbOn", "REVERB", sunset::yellow },
        { "pumpOn", "PUMP", sunset::red }, { "cabOn", "CAB", sunset::orange } };
    for (auto& [id, label, colour] : fxDefs)
    {
        auto s = std::make_unique<dd::gui::LampSwitch> (*p.apvts.getParameter (id), label, colour);
        s->setTooltip (juce::String ("Switch ") + juce::String (label).toLowerCase() + " on or off");
        addAndMakeVisible (*s);
        switches.push_back (std::move (s));
    }

    polySwitch.setTooltip ("Poly: play chords with any preset. Off: each preset's own mode (mono / legato leads). Stays as you set it when you change presets.");
    addAndMakeVisible (polySwitch);
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
    dd::gui::drawPrinted (g, juce::String ("ddeonmadeit  v") + DD_PLUGIN_VERSION, right.removeFromBottom (24.0f), pal.dim,
                          dd::gui::LookAndFeel::font (12.0f), juce::Justification::centredRight);

    // The horizon sits in a bezel: a soft shadow around it and a lit lower lip, so it reads as a screen set into the case.
    const auto screen = horizon.getBounds().toFloat();
    for (int i = 5; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.11f));
        g.fillRoundedRectangle (screen.expanded (2.0f + (float) i).translated (0.0f, (float) i * 0.8f), 10.0f + (float) i);
    }
    const auto bezel = screen.expanded (4.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff070403), 0.0f, bezel.getY(), juce::Colour (0xff2a1a10), 0.0f, bezel.getBottom(), false));
    g.fillRoundedRectangle (bezel, 11.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (bezel.reduced (0.5f), 11.0f, 1.0f);

    // The macro deck: one faceplate, macros on the left, effect switches on the right.
    const auto plate = deck.toFloat();
    dd::gui::paintFaceplate (g, plate, pal);
    // Sunset light from the screen above spills onto the top of the deck.
    g.setGradientFill (juce::ColourGradient (sunset::orange.withAlpha (0.09f), 0.0f, plate.getY(),
                                             sunset::orange.withAlpha (0.0f), 0.0f, plate.getY() + 70.0f, false));
    g.fillRoundedRectangle (plate.reduced (1.0f).withHeight (70.0f), 5.0f);
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
    polySwitch.setBounds (head.getRight() - 84, head.getY() + 6, 84, 26);
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

    const int rowH = fx.getHeight() / 4;
    for (size_t i = 0; i < switches.size(); ++i)
    {
        const int col = (int) (i / 4), row = (int) (i % 4);
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
