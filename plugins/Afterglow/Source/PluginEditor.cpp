#include "PluginEditor.h"

namespace
{
namespace sunset = dd::gui::sunset;

dd::gui::Palette afterglowPalette()
{
    dd::gui::Palette p;   // brushed brown metal, silver hardware; warm colour only for things that glow
    p.accent = sunset::orange;
    p.accent2 = sunset::amber;
    return p;
}

constexpr int kDeckH = 224;
constexpr int kFxW = 272;   // the effects side of the deck
} // namespace

//==============================================================================
AfterglowEditor::Content::Content (AfterglowProcessor& p)
    : proc (p),
      topBar (p.presets, "AFTER", "GLOW", "Pads / Textures / Soundscapes"),
      polySwitch (*p.apvts.getParameter ("perfPoly"), "Poly", sunset::amber),
      tapeStop (p.apvts),
      horizon (p),
      arpSwitch (*p.apvts.getParameter ("arpOn"), "ARP", sunset::amber)
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

    // The arpeggiator: on / off and its rate; the preset sets the pattern, octaves and gate.
    addAndMakeVisible (arpSwitch);
    arpRate.addItemList (ag::choices::arpRates(), 1);
    arpRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "arpRate", arpRate);
    dd::gui::tagParam (arpRate, "arpRate", "Arp rate");
    addAndMakeVisible (arpRate);

    polySwitch.setTooltip ("Poly: play chords with any preset. Off: each preset's own mode (mono / legato leads). Stays as you set it when you change presets.");
    topBar.addRight (polySwitch, 96);
    topBar.addRight (tapeStop, 196);
    addAndMakeVisible (topBar);
    addAndMakeVisible (horizon);
    addAndMakeVisible (atmosphere);
}

void AfterglowEditor::Content::paint (juce::Graphics& g)
{
    const auto& t = dd::gui::tokens();
    dd::gui::paintBackdrop (g, getLocalBounds(), afterglowPalette());
    dd::gui::paintPanelShadows (g, *this);

    // The horizon is a screen set into the chassis.
    dd::gui::paintBezel (g, horizon.getBounds().toFloat(), 10.0f, 7.0f);

    // The macro deck: one raised panel, macros on the left, effect switches on the right.
    const auto plate = deck.toFloat();
    dd::gui::paintRaisedShadow (g, plate, 14.0f);
    dd::gui::paintFaceplate (g, plate, afterglowPalette());
    // The screen's warm light spills onto the top of the deck.
    g.setGradientFill (juce::ColourGradient (t.signal.withAlpha (t.light ? 0.05f : 0.08f), 0.0f, plate.getY(),
                                             t.signal.withAlpha (0.0f), 0.0f, plate.getY() + 70.0f, false));
    g.fillRoundedRectangle (plate.reduced (1.0f).withHeight (70.0f), 13.0f);
    dd::gui::drawCaption (g, "Macros", { plate.getX() + 18.0f, plate.getY() + 10.0f, 200.0f, 18.0f });
    const float split = plate.getRight() - kFxW;
    g.setColour (t.edgeDark);
    g.fillRect (split, plate.getY() + 14.0f, 1.0f, plate.getHeight() - 28.0f);
    g.setColour (t.edgeLight);
    g.fillRect (split + 1.0f, plate.getY() + 14.0f, 1.0f, plate.getHeight() - 28.0f);
    dd::gui::drawCaption (g, "Effects", { split + 22.0f, plate.getY() + 10.0f, 200.0f, 18.0f });
    g.setFont (dd::gui::LookAndFeel::font (9.0f, 700).withExtraKerningFactor (0.12f));
    g.setColour (t.faint);
    g.drawText ((juce::String ("ddeonmadeit  v") + DD_PLUGIN_VERSION).toUpperCase(),
                juce::Rectangle<float> (plate.getRight() - 218.0f, plate.getY() + 10.0f, 200.0f, 18.0f), juce::Justification::centredRight, false);
}

void AfterglowEditor::Content::resized()
{
    topBar.setBounds (0, 8, getWidth(), dd::gui::TopBar::kHeight);
    atmosphere.setBounds (getLocalBounds());

    auto r = getLocalBounds().reduced (14, 0).withTrimmedTop (topBar.getBottom() + 14).withTrimmedBottom (14);
    deck = r.removeFromBottom (kDeckH);
    r.removeFromBottom (14);
    horizon.setBounds (r.reduced (2, 0));

    auto inner = deck.reduced (18, 0).withTrimmedTop (36).withTrimmedBottom (16);
    auto fx = inner.removeFromRight (kFxW - 18);
    fx.removeFromLeft (22);
    const int knobW = inner.getWidth() / (int) macros.size();
    for (auto& k : macros)
        k->setBounds (inner.removeFromLeft (knobW).reduced (6, 0));

    const int rowH = fx.getHeight() / 5;
    for (size_t i = 0; i < switches.size(); ++i)
    {
        const int col = (int) (i / 4), row = (int) (i % 4);
        switches[i]->setBounds (fx.getX() + col * (fx.getWidth() / 2), fx.getY() + row * rowH, fx.getWidth() / 2, rowH);
    }
    arpSwitch.setBounds (fx.getX(), fx.getY() + 4 * rowH, fx.getWidth() / 2, rowH);
    arpRate.setBounds (juce::Rectangle<int> (fx.getX() + fx.getWidth() / 2, fx.getY() + 4 * rowH, fx.getWidth() / 2 - 8, rowH).withSizeKeepingCentre (fx.getWidth() / 2 - 8, 24));
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
