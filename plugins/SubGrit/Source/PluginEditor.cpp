#include "PluginEditor.h"

namespace
{
dd::gui::Palette subGritPalette()
{
    dd::gui::Palette p;   // brushed brown metal, silver hardware; warm colour only for things that glow
    p.accent = dd::gui::sunset::orange;
    p.accent2 = dd::gui::sunset::amber;
    return p;
}

const juce::Colour kSourceAccent = dd::gui::sunset::yellow;
const juce::Colour kToneAccent   = dd::gui::sunset::orange;
const juce::Colour kDirtAccent   = dd::gui::sunset::red;
const juce::Colour kFxAccent     = dd::gui::sunset::amber;
const juce::Colour kModAccent    = dd::gui::sunset::ember;

using dd::gui::Knob;
using dd::gui::Choice;
using dd::gui::Toggle;
} // namespace

SubGritEditor::Content::Content (SubGritProcessor& p)
    : proc (p),
      topBar (p.presets, "SUB", "GRIT", "808 + Dirt Bass Engine"),
      description (topBar.presets()),
      polySwitch (*p.apvts.getParameter ("perfPoly"), "Poly", dd::gui::sunset::amber),
      body808 ("808 Body", 4, 2, kSourceAccent),
      oscA ("Osc A", 4, 2, kSourceAccent),
      oscB ("Osc B", 4, 2, kSourceAccent),
      lfo ("LFO", 5, 2, kModAccent),
      filter ("Filter", 4, 2, kToneAccent),
      env ("Amp / Filter Env", 4, 2, kToneAccent),
      voice ("Voice", 6, 2, kModAccent),
      eq ("EQ", 2, 2, kToneAccent),
      dirt ("Dirt", 4, 2, kDirtAccent),
      fx ("FX", 6, 2, kFxAccent),
      output ("Output", 3, 2, kDirtAccent),
      scopePanel ("Scope", 4, 2, kFxAccent),
      scope (scopePanel.add<dd::gui::Scope> (p.scope))
{
    auto& s = p.apvts;
    scopePanel.place (0, 0, 4, 2);

    body808.add<Knob> (s, "bodyLevel", "Level", kSourceAccent);
    body808.add<Knob> (s, "bodyDrive", "Warmth", kSourceAccent);
    body808.add<Knob> (s, "pitchAmt", "Punch", kSourceAccent);
    body808.add<Knob> (s, "pitchTime", "Punch Time", kSourceAccent);
    body808.add<Knob> (s, "clickLevel", "Knock", kSourceAccent);
    body808.add<Knob> (s, "clickTone", "Knock Tone", kSourceAccent);
    body808.add<Knob> (s, "noiseLevel", "Noise", kSourceAccent);
    body808.add<Knob> (s, "noiseColor", "Noise Tone", kSourceAccent);

    for (auto [panel, x] : { std::pair { &oscA, juce::String ("oscA") }, std::pair { &oscB, juce::String ("oscB") } })
    {
        panel->add<Choice> (s, x + "Wave", "Wave");
        panel->add<Knob> (s, x + "Level", "Level", kSourceAccent);
        panel->add<Knob> (s, x + "Oct", "Octave", kSourceAccent);
        panel->add<Knob> (s, x + "Semi", "Semi", kSourceAccent);
        panel->add<Knob> (s, x + "Voices", "Unison", kSourceAccent);
        panel->add<Knob> (s, x + "Detune", "Detune", kSourceAccent);
        panel->add<Knob> (s, x + "Width", "Width", kSourceAccent);
        panel->add<Knob> (s, x + "Fine", "Fine", kSourceAccent);
    }

    lfo.add<Choice> (s, "lfoShape", "Shape");
    lfo.add<Toggle> (s, "lfoSync", "Sync", "SYNC", "FREE");
    lfo.add<Knob> (s, "lfoRate", "Rate", kModAccent);
    lfo.add<Choice> (s, "lfoDiv", "Division");
    lfo.add<Knob> (s, "lfoFade", "Fade In", kModAccent);
    lfo.add<Knob> (s, "lfoPitch", "> Pitch", kModAccent);
    lfo.add<Knob> (s, "lfoCutoff", "> Cutoff", kModAccent);
    lfo.add<Knob> (s, "lfoAmp", "> Amp", kModAccent);
    lfo.add<Knob> (s, "lfoDirt", "> Dirt", kModAccent);

    filter.add<Choice> (s, "filterMode", "Mode");
    filter.add<Knob> (s, "cutoff", "Cutoff", kToneAccent);
    filter.add<Knob> (s, "resonance", "Reso", kToneAccent);
    filter.add<Knob> (s, "filterDrive", "Drive", kToneAccent);
    filter.add<Toggle> (s, "bodyToFilter", "808 Thru", "THRU", "BYPASS");
    filter.add<Knob> (s, "filterEnv", "Env Amt", kToneAccent);
    filter.add<Knob> (s, "keyTrack", "Key Track", kToneAccent);

    env.add<Knob> (s, "aAttack", "Attack", kToneAccent);
    env.add<Knob> (s, "aDecay", "Decay", kToneAccent);
    env.add<Knob> (s, "aSustain", "Sustain", kToneAccent);
    env.add<Knob> (s, "aRelease", "Release", kToneAccent);
    env.add<Knob> (s, "fAttack", "F Attack", kModAccent);
    env.add<Knob> (s, "fDecay", "F Decay", kModAccent);
    env.add<Knob> (s, "fSustain", "F Sustain", kModAccent);
    env.add<Knob> (s, "fRelease", "F Release", kModAccent);

    voice.add<Choice> (s, "voiceMode", "Mode");
    voice.add<Toggle> (s, "legato", "Legato");
    voice.add<Choice> (s, "glideMode", "Glide Mode");
    voice.add<Knob> (s, "glide", "Glide", kModAccent);
    voice.add<Knob> (s, "diveAmt", "Dive", kModAccent);
    voice.add<Knob> (s, "diveTime", "Dive Time", kModAccent);
    voice.add<Knob> (s, "octave", "Octave", kModAccent);
    voice.add<Knob> (s, "tune", "Tune", kModAccent);
    voice.add<Knob> (s, "fine", "Fine", kModAccent);
    voice.add<Knob> (s, "bendRange", "Bend", kModAccent);
    voice.add<Knob> (s, "velocity", "Velocity", kModAccent);
    voice.add<Knob> (s, "drift", "Drift", kModAccent);

    eq.add<Knob> (s, "eqSub", "Sub", kToneAccent);
    eq.add<Knob> (s, "eqMidGain", "Mid", kToneAccent);
    eq.add<Knob> (s, "eqMidFreq", "Mid Freq", kToneAccent);
    eq.add<Knob> (s, "eqHighCut", "High Cut", kToneAccent);

    dirt.add<Choice> (s, "dirtType", "Type");
    dirt.add<Knob> (s, "dirtDrive", "Drive", kDirtAccent);
    dirt.add<Knob> (s, "dirtBias", "Character", kDirtAccent);
    dirt.add<Knob> (s, "dirtTone", "Tone", kDirtAccent);
    dirt.add<Knob> (s, "dirtMix", "Mix", kDirtAccent);
    dirt.add<Knob> (s, "subKeep", "Sub Keep", kDirtAccent);
    dirt.add<Knob> (s, "xover", "X-Over", kDirtAccent);

    fx.add<Knob> (s, "chorusMix", "Chorus", kFxAccent);
    fx.add<Knob> (s, "chorusRate", "Rate", kFxAccent);
    fx.add<Knob> (s, "chorusDepth", "Depth", kFxAccent);
    fx.add<Knob> (s, "spaceMix", "Space", kFxAccent);
    fx.add<Knob> (s, "spaceSize", "Size", kFxAccent);
    fx.skip();
    fx.add<Knob> (s, "delayMix", "Delay", kFxAccent);
    fx.add<Choice> (s, "delayDiv", "Time");
    fx.add<Knob> (s, "delayFeedback", "Feedback", kFxAccent);
    fx.add<Knob> (s, "delayTone", "Tone", kFxAccent);
    fx.add<Toggle> (s, "delayPingPong", "Ping Pong");

    output.add<Knob> (s, "glue", "Glue", kDirtAccent);
    output.add<Knob> (s, "clip", "Clip", kDirtAccent);
    output.add<Knob> (s, "outGain", "Output", kDirtAccent);
    output.add<Knob> (s, "width", "Width", kDirtAccent);

    polySwitch.setTooltip ("Poly: play chords with any preset. Off: each preset's own mode (mono 808 slides). Stays as you set it when you change presets.");
    topBar.addRight (polySwitch, 96);
    addAndMakeVisible (topBar);
    description.setHint (juce::String ("ddeonmadeit  v") + DD_PLUGIN_VERSION);
    addAndMakeVisible (description);
    for (auto* c : { &body808, &oscA, &oscB, &lfo, &filter, &env, &voice, &eq, &dirt, &fx, &output, &scopePanel })
        addAndMakeVisible (c);
    addAndMakeVisible (atmosphere);
}

void SubGritEditor::Content::paint (juce::Graphics& g)
{
    dd::gui::paintBackdrop (g, getLocalBounds(), subGritPalette());
    dd::gui::paintPanelShadows (g, *this);
}

void SubGritEditor::Content::resized()
{
    topBar.setBounds (0, 8, getWidth(), dd::gui::TopBar::kHeight);
    description.setBounds (26, topBar.getBottom() + 6, getWidth() - 52, 22);
    atmosphere.setBounds (getLocalBounds());

    auto r = getLocalBounds().reduced (14, 0).withTrimmedTop (description.getBottom() + 6).withTrimmedBottom (14);

    const int gap = 8;
    auto layoutRow = [&] (std::initializer_list<dd::gui::Panel*> panels)
    {
        auto row = r.removeFromTop (panels.begin()[0]->preferredSize().y);
        r.removeFromTop (gap);
        int total = 0;
        for (auto* p : panels) total += p->preferredSize().x;
        const int avail = row.getWidth() - gap * ((int) panels.size() - 1);
        int x = row.getX();
        int i = 0;
        for (auto* p : panels)
        {
            const bool last = ++i == (int) panels.size();
            const int w = last ? row.getRight() - x : (int) std::round ((double) p->preferredSize().x * avail / total);
            p->setBounds (x, row.getY(), w, row.getHeight());
            x += w + gap;
        }
    };

    layoutRow ({ &body808, &oscA, &oscB, &lfo });
    layoutRow ({ &filter, &env, &voice, &eq });
    layoutRow ({ &dirt, &fx, &output, &scopePanel });
}

//==============================================================================
SubGritEditor::SubGritEditor (SubGritProcessor& p)
    : AudioProcessorEditor (p), proc (p), lnf (subGritPalette())
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

SubGritEditor::~SubGritEditor()
{
    content.reset();
    setLookAndFeel (nullptr);
}

void SubGritEditor::paint (juce::Graphics& g)
{
    g.fillAll (lnf.palette().background);
}

void SubGritEditor::resized()
{
    const float scale = (float) getWidth() / (float) kBaseW;
    content->setTransform (juce::AffineTransform::scale (scale));
    if (constructed)
        proc.editorScale = scale;
}
