#include "PluginEditor.h"

namespace
{
namespace sunset = dd::gui::sunset;

dd::gui::Palette sunburstPalette()
{
    dd::gui::Palette p;   // dark brown matte hardware; highlights only from the sunset set
    p.background = juce::Colour (0xff100906);
    p.panel = juce::Colour (0xff24170e);
    p.accent = sunset::orange;
    p.accent2 = sunset::amber;
    return p;
}
} // namespace

//==============================================================================
SunburstEditor::Content::Content (SunburstProcessor& p)
    : proc (p), presetBar (p.presets), guitar (p),
      playing ("Playing", 7, 3, sunset::amber),
      amp ("Amp", 13, 1, sunset::orange),
      room ("Room & Output", 6, 1, sunset::yellow)
{
    auto& s = p.apvts;
    const auto a = sunset::amber;

    auto& noise = playing.add<dd::gui::Knob> (s, "stringNoise", "String Noise", sunset::yellow);
    playing.place (0, 0, 2, 2);
    noise.setLabelScale (1.4f);
    noise.getSlider().setTooltip ("Fret squeaks, string damping, slide scrapes and pick noise. Turn it all the way down for clean notes.");
    playing.add<dd::gui::Knob> (s, "pick", "Pick", a).getSlider().setTooltip ("Soft fingers (left) to a hard pick (right)");
    playing.place (2, 0);
    playing.add<dd::gui::Knob> (s, "mute", "Palm Mute", a).getSlider().setTooltip ("Palm mute. The mod wheel mutes too.");
    playing.place (3, 0);
    playing.add<dd::gui::Knob> (s, "strum", "Strum", a).getSlider().setTooltip ("Strum chords: time between strings. Quick repeats alternate down and up strokes.");
    playing.place (4, 0);
    playing.add<dd::gui::Knob> (s, "ring", "Ring", a).getSlider().setTooltip ("How long notes ring after you let go");
    playing.place (5, 0);
    playing.add<dd::gui::Knob> (s, "doubleTrack", "Double", a).getSlider().setTooltip ("Double-tracked guitar: a second take panned opposite");
    playing.place (6, 0);
    playing.add<dd::gui::Choice> (s, "legato", "Legato");
    playing.place (2, 1);
    playing.add<dd::gui::Knob> (s, "slideTime", "Slide", a);
    playing.place (3, 1);
    playing.add<dd::gui::Knob> (s, "vibrato", "Vibrato", a);
    playing.place (4, 1);
    playing.add<dd::gui::Knob> (s, "humanize", "Humanize", a);
    playing.place (5, 1);
    playing.add<dd::gui::Knob> (s, "velCurve", "Velocity", a);
    playing.place (6, 1);
    playing.add<dd::gui::Knob> (s, "tone", "Tone", a);
    playing.place (0, 2);
    playing.add<dd::gui::Knob> (s, "body", "Body", a).getSlider().setTooltip ("Acoustic guitars: body resonance and air");
    playing.place (1, 2);
    playing.add<dd::gui::Knob> (s, "transpose", "Transpose", a);
    playing.place (2, 2);
    playing.add<dd::gui::Knob> (s, "bendRange", "Bend", a);
    playing.place (3, 2);
    playing.add<dd::gui::Scope> (p.scope);
    playing.place (4, 2, 3, 1);

    const juce::Colour o = sunset::orange;
    amp.add<dd::gui::LampSwitch> (*s.getParameter ("ampOn"), "AMP", sunset::red);
    amp.place (0, 0);
    amp.add<dd::gui::Choice> (s, "ampModel", "Model");
    amp.place (1, 0, 2, 1);
    int col = 3;
    for (auto [id, title] : { std::pair { "ampGain", "Gain" }, { "ampBass", "Bass" }, { "ampMid", "Mid" }, { "ampTreble", "Treble" },
                              { "ampPresence", "Presence" }, { "ampLevel", "Level" } })
    {
        amp.add<dd::gui::Knob> (s, id, title, o);
        amp.place (col++, 0);
    }
    amp.add<dd::gui::LampSwitch> (*s.getParameter ("cabOn"), "CAB", sunset::ember);
    amp.place (9, 0);
    amp.add<dd::gui::Choice> (s, "cabType", "Cabinet");
    amp.place (10, 0, 2, 1);
    amp.add<dd::gui::Knob> (s, "cabMic", "Mic", o).getSlider().setTooltip ("Mic on the cone's centre (left) to off-axis (right)");
    amp.place (12, 0);

    room.add<dd::gui::LampSwitch> (*s.getParameter ("reverbOn"), "ROOM", sunset::yellow);
    room.add<dd::gui::Knob> (s, "reverbMix", "Mix", sunset::yellow);
    room.add<dd::gui::Knob> (s, "reverbSize", "Size", sunset::yellow);
    room.add<dd::gui::Knob> (s, "width", "Width", sunset::yellow);
    room.add<dd::gui::Knob> (s, "glue", "Glue", sunset::yellow);
    room.add<dd::gui::Knob> (s, "outGain", "Output", sunset::yellow);

    using C = std::vector<std::pair<juce::String, juce::String>>;
    auto pedal = [&] (const char* name, const char* onId, C controls, juce::Colour body)
    {
        auto pv = std::make_unique<PedalView> (s, name, onId, std::move (controls), body);
        addAndMakeVisible (*pv);
        pedals.push_back (std::move (pv));
    };
    pedal ("Comp",   "compOn",   C { { "compAmount", "Squeeze" } },                                           juce::Colour (0xff6a3c12));
    pedal ("Drive",  "driveOn",  C { { "driveGain", "Gain" }, { "driveTone", "Tone" } },                      juce::Colour (0xff7a2a0c));
    pedal ("Fuzz",   "fuzzOn",   C { { "fuzzGain", "Fuzz" } },                                                juce::Colour (0xff6a140c));
    pedal ("Chorus", "chorusOn", C { { "chorusRate", "Rate" }, { "chorusDepth", "Depth" } },                  juce::Colour (0xff7a4a14));
    pedal ("Trem",   "tremOn",   C { { "tremDiv", "Rate" }, { "tremDepth", "Depth" } },                       juce::Colour (0xff8a5016));
    pedal ("Delay",  "delayOn",  C { { "delayDiv", "Time" }, { "delayFeedback", "Repeats" }, { "delayMix", "Mix" } }, juce::Colour (0xff2c1a10));

    addAndMakeVisible (presetBar);
    addAndMakeVisible (guitar);
    addAndMakeVisible (playing);
    addAndMakeVisible (amp);
    addAndMakeVisible (room);
}

void SunburstEditor::Content::paint (juce::Graphics& g)
{
    const auto pal = sunburstPalette();
    dd::gui::paintBackdrop (g, getLocalBounds(), pal);

    auto head = getLocalBounds().removeFromTop (12 + 64).reduced (16, 0).withTrimmedTop (12).toFloat();
    auto logo = head.removeFromLeft (320.0f);
    const auto wordFont = dd::gui::LookAndFeel::display (40.0f).withExtraKerningFactor (0.04f);
    const float wordW = juce::GlyphArrangement::getStringWidth (wordFont, "SUNBURST");
    auto wordRow = logo.removeFromTop (42.0f);
    dd::gui::drawPrinted (g, "SUNBURST", wordRow, pal.text, wordFont, juce::Justification::bottomLeft);
    dd::gui::paintSunsetStripes (g, { wordRow.getX() + wordW + 12.0f, wordRow.getBottom() - 21.0f, 320.0f - wordW - 24.0f, 12.0f });
    dd::gui::drawPrinted (g, "ACOUSTIC  /  ELECTRIC  GUITAR", logo.removeFromTop (18.0f), pal.accent2,
                          dd::gui::LookAndFeel::font (12.0f, true).withExtraKerningFactor (0.22f), juce::Justification::topLeft);
    auto right = head.removeFromRight (160.0f);
    dd::gui::drawPrinted (g, juce::String ("ddeonmadeit  v") + DD_PLUGIN_VERSION, right.removeFromBottom (24.0f), pal.dim,
                          dd::gui::LookAndFeel::font (12.0f), juce::Justification::centredRight);

    // The pedalboard: a dark rubber-topped board with a lip, pedals sit on it.
    const auto board = pedalboard.toFloat();
    for (int i = 5; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.1f));
        g.fillRoundedRectangle (board.translated (0.0f, (float) i * 1.4f).expanded ((float) i), 10.0f);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c120c), board.getX(), board.getY(), juce::Colour (0xff0a0604), board.getX(), board.getBottom(), false));
    g.fillRoundedRectangle (board, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (board.reduced (1.0f), 10.0f, 1.0f);
    dd::gui::paintGrain (g, pedalboard, 1.2f);
    dd::gui::paintLamp (g, { board.getX() + 20.0f, board.getY() + 17.0f }, 3.2f, sunset::ember, 1.0f);
    dd::gui::drawPrinted (g, "PEDALBOARD", { board.getX() + 30.0f, board.getY() + 6.0f, 200.0f, 22.0f }, pal.text,
                          dd::gui::LookAndFeel::font (13.0f, true).withExtraKerningFactor (0.1f), juce::Justification::centredLeft);
}

void SunburstEditor::Content::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto head = r.removeFromTop (64);
    head.removeFromLeft (320);
    head.removeFromRight (170);
    presetBar.setBounds (head.withSizeKeepingCentre (std::min (head.getWidth(), 600), 54));
    r.removeFromTop (8);

    auto top = r.removeFromTop (playing.preferredSize().y);
    playing.setBounds (top.removeFromRight (playing.preferredSize().x));
    top.removeFromRight (10);
    guitar.setBounds (top);
    r.removeFromTop (10);

    auto bottom = r.removeFromBottom (amp.preferredSize().y);
    room.setBounds (bottom.removeFromRight (room.preferredSize().x));
    bottom.removeFromRight (10);
    amp.setBounds (bottom.removeFromLeft (amp.preferredSize().x));
    r.removeFromBottom (10);

    pedalboard = r;
    auto row = r.reduced (14, 0).withTrimmedTop (30).withTrimmedBottom (10);
    int total = 0;
    for (auto& p : pedals) total += p->preferredWidth();
    const int gap = std::max (8, (row.getWidth() - total) / (int) (pedals.size() + 1));
    row.removeFromLeft (gap);
    for (auto& p : pedals)
    {
        p->setBounds (row.removeFromLeft (p->preferredWidth()));
        row.removeFromLeft (gap);
    }
}

//==============================================================================
SunburstEditor::SunburstEditor (SunburstProcessor& p)
    : AudioProcessorEditor (p), proc (p), lnf (sunburstPalette())
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

SunburstEditor::~SunburstEditor()
{
    content.reset();
    setLookAndFeel (nullptr);
}

void SunburstEditor::paint (juce::Graphics& g)
{
    g.fillAll (lnf.palette().background);
}

void SunburstEditor::resized()
{
    const float scale = (float) getWidth() / (float) kBaseW;
    content->setTransform (juce::AffineTransform::scale (scale));
    if (constructed)
        proc.editorScale = scale;
}
