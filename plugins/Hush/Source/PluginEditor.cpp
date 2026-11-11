#include "PluginEditor.h"

namespace
{
using dd::gui::Knob;
using dd::gui::Choice;
using dd::gui::Toggle;
const juce::Colour kA = dd::gui::sunset::orange;

/** What the live de-esser (for audio not yet captured) is taking off right now. */
class LiveMeter : public juce::Component, private juce::Timer
{
public:
    explicit LiveMeter (HushProcessor& p) : proc (p) { startTimerHz (24); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (4.0f, 6.0f);
        dd::gui::drawCaption (g, "LIVE CUT (NOT YET CAPTURED)", r.removeFromTop (14.0f), juce::Justification::centredLeft, 9.0f);
        r.removeFromTop (6.0f);
        const auto well = r.withHeight (std::min (r.getHeight(), 26.0f));
        dd::gui::paintConsole (g, well, 5.0f, 0.4f);
        const float frac = juce::jlimit (0.0f, 1.0f, shown / 18.0f);
        const auto bar = well.reduced (5.0f, 8.0f).withWidth ((well.getWidth() - 10.0f) * frac);
        g.setColour (dd::gui::sunset::amber.withAlpha (0.3f));
        g.fillRoundedRectangle (bar.expanded (2.0f), 3.0f);
        g.setColour (dd::gui::sunset::orange);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (dd::gui::tokens().cream300);
        g.setFont (dd::gui::LookAndFeel::font (11.0f, 700));
        g.drawText (shown < 0.1f ? juce::String ("0 dB") : "-" + juce::String (shown, 1) + " dB", well.reduced (8.0f, 0.0f).toNearestInt(),
                    juce::Justification::centredRight);
    }

private:
    void timerCallback() override
    {
        const float target = proc.engine->getLiveReductionDb();
        shown = target > shown ? target : shown * 0.85f + target * 0.15f;
        repaint();
    }
    HushProcessor& proc;
    float shown = 0.0f;
};
} // namespace

HushEditor::Content::Content (HushProcessor& p)
    : proc (p),
      topBar (p.presets, "HU", "SH", "Per-Sound De-Esser"),
      description (topBar.presets()),
      transfer (p),
      sounds ("Sounds", 14, 8, kA),
      controls ("De-Esser", 5, 2, kA),
      selected ("Selected", 5, 3, kA),
      session ("Session", 5, 2, kA)
{
    auto& s = p.apvts;
    auto& editor = sounds.add<hush::SoundEditor> (p, state);
    sounds.place (0, 0, 14, 8);

    controls.add<Knob> (s, "amount", "Amount", kA).getSlider().setTooltip ("How far every sharp sound is turned down automatically; louder ones get more. Sounds you set by hand keep their level.");
    controls.add<Knob> (s, "sensitivity", "Sensitivity", kA).getSlider().setTooltip ("How readily a sound counts as sharp: higher finds softer S's and T's");
    controls.add<Knob> (s, "freq", "Freq", kA).getSlider().setTooltip ("Split mode turns down only what's above this frequency");
    controls.add<Knob> (s, "fade", "Fade", kA).getSlider().setTooltip ("How smoothly each cut fades in and out around its sound");
    controls.add<Choice> (s, "mode", "Mode");
    controls.add<Knob> (s, "outGain", "Output", kA);
    controls.add<Toggle> (s, "perfListen", "Listen", "ON", "OFF");
    controls.add<LiveMeter> (p);
    controls.place (2, 1, 3, 1);

    selected.add<hush::Inspector> (p, state, editor.getCanvas());
    selected.place (0, 0, 5, 3);
    session.add<hush::SessionView> (p, state, editor.getCanvas());
    session.place (0, 0, 5, 2);

    topBar.addRight (transfer, 118);
    addAndMakeVisible (topBar);
    description.setHint (juce::String ("ddeonmadeit  v") + DD_PLUGIN_VERSION);
    addAndMakeVisible (description);
    for (auto* c : { &sounds, &controls, &selected, &session })
        addAndMakeVisible (c);
    addAndMakeVisible (atmosphere);
    dd::gui::giveKeysOnlyTo (*this, editor.getCanvas());
}

void HushEditor::Content::paint (juce::Graphics& g)
{
    dd::gui::paintBackdrop (g, getLocalBounds(), {});
    dd::gui::paintPanelShadows (g, *this);
}

void HushEditor::Content::resized()
{
    topBar.setBounds (0, 8, getWidth(), dd::gui::TopBar::kHeight);
    description.setBounds (26, topBar.getBottom() + 6, getWidth() - 52, 22);
    atmosphere.setBounds (getLocalBounds());

    auto r = getLocalBounds().reduced (14, 0).withTrimmedTop (description.getBottom() + 6).withTrimmedBottom (14);
    constexpr int gap = 10;
    const int total = sounds.preferredSize().x + controls.preferredSize().x;
    const int avail = r.getWidth() - gap;
    sounds.setBounds (r.removeFromLeft ((int) std::round ((double) sounds.preferredSize().x * avail / total)));
    r.removeFromLeft (gap);
    controls.setBounds (r.removeFromTop (controls.preferredSize().y));
    r.removeFromTop (gap);
    session.setBounds (r.removeFromBottom (session.preferredSize().y));
    r.removeFromBottom (gap);
    selected.setBounds (r);
}

//==============================================================================
HushEditor::HushEditor (HushProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    content = std::make_unique<Content> (p);
    content->setBounds (0, 0, kBaseW, kBaseH);
    addAndMakeVisible (*content);
    const float savedScale = proc.editorScale;
    setResizable (true, true);
    setResizeLimits ((int) (kBaseW * 0.6f), (int) (kBaseH * 0.6f), (int) (kBaseW * 1.8f), (int) (kBaseH * 1.8f));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setSize ((int) std::round (kBaseW * savedScale), (int) std::round (kBaseH * savedScale));
    constructed = true;
}

HushEditor::~HushEditor()
{
    content.reset();
    setLookAndFeel (nullptr);
}

void HushEditor::paint (juce::Graphics& g)
{
    g.fillAll (dd::gui::tokens().room);
}

void HushEditor::resized()
{
    const float scale = (float) getWidth() / (float) kBaseW;
    content->setTransform (juce::AffineTransform::scale (scale));
    if (constructed)
        proc.editorScale = scale;
}
