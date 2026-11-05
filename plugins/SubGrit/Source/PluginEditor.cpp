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

constexpr int kDeckH = 236;

/** What every control does, in plain words: shown under the top bar while the mouse is over it. */
std::map<juce::String, juce::String> helpTable()
{
    std::map<juce::String, juce::String> h {
        { "mBoom",   "Less or more low end: the 808's round boom and the deep sub. Centre is the preset as designed (double-click)." },
        { "mPunch",  "A softer or harder start to every note: the pitch drop, the knock and how fast it hits. Centre is the preset as designed." },
        { "mGrit",   "Cleaner or dirtier: distortion, warmth and clipping. Centre is the preset as designed." },
        { "mTone",   "Darker or brighter. Centre is the preset as designed." },
        { "mLength", "Shorter or longer notes. Centre is the preset as designed." },
        { "mGlide",  "Less or more slide from one note to the next (overlap your notes to hear it). Centre is the preset as designed." },
        { "perfPoly", "On: play chords with any preset. Off: each preset's own mode (mono 808s that slide). Stays as you set it." },

        { "voiceMode",  "Mono plays one note at a time (808 style, notes can slide into each other); Poly plays chords." },
        { "legato",     "On: a note played while another is held slides from it instead of starting over." },
        { "glide",      "How long the pitch takes to slide from one note to the next." },
        { "glideMode",  "Overlap: slide only when notes overlap. Always: slide between every note." },
        { "octave",     "Moves the whole bass up or down by octaves." },
        { "tune",       "Moves the whole bass up or down by semitones." },
        { "fine",       "Fine tuning, in hundredths of a semitone." },
        { "bendRange",  "How far the pitch wheel bends, in semitones." },
        { "velocity",   "How much harder key hits play louder. 0: every note the same." },
        { "drift",      "A tiny random pitch wander on each note, like an old analog synth." },

        { "bodyLevel",  "Volume of the 808: the round sine boom at the heart of the sound." },
        { "bodyDrive",  "Rounds the 808 toward a square: warmer, louder, more harmonics that show on small speakers." },
        { "pitchAmt",   "The 808's punch: each note starts this many semitones high and drops onto the note." },
        { "pitchTime",  "How fast that drop happens: short is a tight knock, long a falling boom." },
        { "clickLevel", "A short knock at the start of each note so the bass cuts through on phones and laptops." },
        { "clickTone",  "Dark thump or bright click." },
        { "noiseLevel", "Adds noise (breath, grit, air) through the filter." },
        { "noiseColor", "Dark rumble or bright hiss." },

        { "filterMode", "Filter type: Ladder (warm Moog low-pass), LP 12 (gentle low-pass), BP 12 (only the middle), HP 12 (removes the lows)." },
        { "cutoff",     "Filter brightness: lower is darker and rounder, higher is brighter." },
        { "resonance",  "Boosts the filter's edge: a whistling, squelchy peak as it moves." },
        { "filterDrive","Pushes the filter for warmth and growl." },
        { "filterEnv",  "How far the filter opens at the start of each note (in octaves): the 'wow' of a synth bass." },
        { "keyTrack",   "Higher notes open the filter more, so every note sounds equally bright." },
        { "bodyToFilter","Thru: the 808 goes through the filter too. Bypass: the 808 stays clean under the filtered synth." },

        { "aAttack",  "Fade-in of each note: short hits instantly, long swells in." },
        { "aDecay",   "How long each note takes to fall to the Sustain level: the length of an 808 hit." },
        { "aSustain", "The level a note holds while the key is down. 0: every note fades away by itself." },
        { "aRelease", "How long the note keeps ringing after you let go." },
        { "fAttack",  "Filter envelope: how fast the filter opens at the start of a note." },
        { "fDecay",   "Filter envelope: how fast it closes again." },
        { "fSustain", "Filter envelope: how open it stays while the key is held." },
        { "fRelease", "Filter envelope: how fast it closes after you let go." },

        { "diveAmt",  "Each note dives down this far as it fades: the falling-808 effect." },
        { "diveTime", "How long the dive takes." },

        { "lfoShape",  "The shape of the wobble: smooth sine, triangle, ramp, square (on/off) or random steps." },
        { "lfoSync",   "Sync: the wobble follows the song's tempo. Free: you set its speed in Hz." },
        { "lfoRate",   "Wobble speed when free." },
        { "lfoDiv",    "Wobble speed as a note length, when synced to the tempo." },
        { "lfoFade",   "The wobble fades in over this time after each note starts." },
        { "lfoPitch",  "How much the wobble moves the pitch (vibrato)." },
        { "lfoCutoff", "How much the wobble moves the filter (the wub-wub)." },
        { "lfoAmp",    "How much the wobble moves the volume (tremolo, or a chopped gate)." },
        { "lfoDirt",   "How much the wobble moves the distortion." },

        { "dirtType",  "Distortion flavour: Tape (warm), Tube (rich), Fuzz, Clip (hard), Fold (metallic), Crush (lo-fi), Rectify (an octave up), Amp." },
        { "dirtDrive", "How hard the distortion is pushed." },
        { "dirtTone",  "Darker or brighter distortion." },
        { "dirtBias",  "Character: makes the distortion lopsided for grittier, richer harmonics." },
        { "dirtMix",   "Blend of distorted and clean sound." },
        { "subKeep",   "Keeps the sub clean under the distortion: the lows stay round and heavy while the top gets dirty." },
        { "xover",     "Where the clean sub ends and the distorted part begins." },

        { "eqSub",     "Boosts or cuts the deep sub (around 70 Hz)." },
        { "eqMidGain", "Boosts or cuts the mids: where a bass shows up on phone speakers." },
        { "eqMidFreq", "Which middle frequency the Mid knob works on." },
        { "eqHighCut", "Removes the highs above this frequency: lower is darker and rounder." },

        { "chorusMix",   "Chorus on everything above the sub (the sub stays mono): wider and moving." },
        { "chorusRate",  "Chorus speed." },
        { "chorusDepth", "How deep the chorus swirls." },
        { "spaceMix",    "A room around the top of the bass (the sub stays dry)." },
        { "spaceSize",   "How big that room is." },
        { "delayMix",    "Echoes of the top of the bass, in time with the song." },
        { "delayDiv",    "Echo time as a note length." },
        { "delayFeedback","How many times the echo repeats." },
        { "delayTone",   "Darker or brighter echoes." },
        { "delayPingPong","Echoes bounce left and right." },

        { "glue",    "Gentle compression that holds the sound together." },
        { "clip",    "Soft clipping on the way out: louder, denser, rounder peaks." },
        { "width",   "Stereo width of everything above the sub." },
        { "outGain", "Final volume." },
    };
    for (const char* o : { "A", "B" })
    {
        const juce::String x = juce::String ("osc") + o, name = juce::String ("Oscillator ") + o;
        h[x + "Level"]  = "Volume of " + name + ", a synth layer over the 808 (saws and squares for growl and mids).";
        h[x + "Wave"]   = name + "'s waveform: Sine (pure), Tri (soft), Saw (buzzy), Square (hollow), Pulse (nasal), Thin.";
        h[x + "Oct"]    = name + "'s pitch, in octaves from the note.";
        h[x + "Semi"]   = name + "'s pitch, in semitones from the note (7 = a fifth up).";
        h[x + "Fine"]   = name + "'s fine tuning: a few cents off thickens it against the 808.";
        h[x + "Voices"] = "Unison: stacks copies of " + name + " for a thicker sound.";
        h[x + "Detune"] = "How far apart the unison copies are tuned: wider is thicker and wobblier.";
        h[x + "Width"]  = "Stereo spread of the unison copies (the sub stays mono).";
    }
    return h;
}
} // namespace

SubGritEditor::Content::Content (SubGritProcessor& p)
    : proc (p),
      topBar (p.presets, "SUB", "GRIT", "808 + Dirt Bass Engine"),
      description (topBar.presets()),
      polySwitch (*p.apvts.getParameter ("perfPoly"), "Poly", dd::gui::sunset::amber),
      body808 ("808 Body", 4, 2, kSourceAccent),
      oscA ("Osc A", 4, 2, kSourceAccent),
      oscB ("Osc B", 4, 2, kSourceAccent),
      lfo ("Wobble (LFO)", 5, 2, kModAccent),
      filter ("Filter", 4, 2, kToneAccent),
      env ("Amp / Filter Env", 4, 2, kToneAccent),
      voice ("Voice", 6, 2, kModAccent),
      eq ("EQ", 2, 2, kToneAccent),
      dirt ("Dirt", 4, 2, kDirtAccent),
      fx ("FX", 6, 2, kFxAccent),
      output ("Output", 3, 2, kDirtAccent),
      scopePanel ("Scope", 4, 2, kFxAccent),
      scope (scopePanel.add<dd::gui::Scope> (p.scope)),
      sub (p),
      simpleScope (p.scope)
{
    auto& s = p.apvts;
    scopePanel.place (0, 0, 4, 2);

    body808.add<Knob> (s, "bodyLevel", "Level", kSourceAccent);
    body808.add<Knob> (s, "bodyDrive", "Warmth", kSourceAccent);
    body808.add<Knob> (s, "pitchAmt", "Pitch Drop", kSourceAccent);
    body808.add<Knob> (s, "pitchTime", "Drop Time", kSourceAccent);
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
    lfo.add<Knob> (s, "lfoPitch", "To Pitch", kModAccent);
    lfo.add<Knob> (s, "lfoCutoff", "To Filter", kModAccent);
    lfo.add<Knob> (s, "lfoAmp", "To Volume", kModAccent);
    lfo.add<Knob> (s, "lfoDirt", "To Dirt", kModAccent);

    filter.add<Choice> (s, "filterMode", "Mode");
    filter.add<Knob> (s, "cutoff", "Cutoff", kToneAccent);
    filter.add<Knob> (s, "resonance", "Reso", kToneAccent);
    filter.add<Knob> (s, "filterDrive", "Drive", kToneAccent);
    filter.add<Toggle> (s, "bodyToFilter", "808 In Filter", "THRU", "BYPASS");
    filter.add<Knob> (s, "filterEnv", "Filter Env", kToneAccent);
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
    dirt.add<Knob> (s, "xover", "Sub Split", kDirtAccent);

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

    // The simple panel: six macros that bend the preset, each with what its two ends do.
    const std::tuple<const char*, const char*, juce::Colour> macroDefs[] = {
        { "mBoom", "Boom", dd::gui::sunset::orange }, { "mPunch", "Punch", dd::gui::sunset::yellow },
        { "mGrit", "Grit", dd::gui::sunset::red },    { "mTone", "Tone", dd::gui::sunset::amber },
        { "mLength", "Length", dd::gui::sunset::ember }, { "mGlide", "Slide", dd::gui::sunset::amber } };
    for (auto& [id, title, colour] : macroDefs)
    {
        auto k = std::make_unique<Knob> (s, id, title, colour);
        k->setLabelScale (1.35f);
        addChildComponent (*k);
        macros.push_back (std::move (k));
    }
    addChildComponent (sub);
    addChildComponent (simpleScope);

    viewKey.onClick = [this] { showAllKnobs (! proc.allKnobs); };
    dd::gui::setHelp (viewKey, "All knobs", "Switch between the simple panel (six macros) and every control of the engine.");
    topBar.addRight (viewKey, 132);

    polySwitch.setTooltip ("Poly: play chords with any preset. Off: each preset's own mode (mono 808 slides). Stays as you set it when you change presets.");
    topBar.addRight (polySwitch, 96);
    addAndMakeVisible (topBar);
    description.setHint (juce::String ("ddeonmadeit  v") + DD_PLUGIN_VERSION);
    description.setHelpTable (helpTable());
    addAndMakeVisible (description);
    for (auto* c : { &body808, &oscA, &oscB, &lfo, &filter, &env, &voice, &eq, &dirt, &fx, &output, &scopePanel })
        addChildComponent (c);
    addAndMakeVisible (atmosphere);
    showAllKnobs (proc.allKnobs);
}

void SubGritEditor::Content::showAllKnobs (bool all)
{
    proc.allKnobs = all;
    for (auto* c : { &body808, &oscA, &oscB, &lfo, &filter, &env, &voice, &eq, &dirt, &fx, &output, &scopePanel })
        c->setVisible (all);
    for (auto& k : macros)
        k->setVisible (! all);
    sub.setVisible (! all);
    simpleScope.setVisible (! all);
    viewKey.setLabel (all ? "Simple" : "All Knobs");
    viewKey.setLit (all);
    atmosphere.toFront (false);
    resized();
    repaint();
}

void SubGritEditor::Content::paint (juce::Graphics& g)
{
    dd::gui::paintBackdrop (g, getLocalBounds(), subGritPalette());
    dd::gui::paintPanelShadows (g, *this);
    if (proc.allKnobs)
        return;

    const auto& t = dd::gui::tokens();
    // The speaker's room is a window set into the chassis.
    dd::gui::paintBezel (g, sub.getBounds().toFloat(), 10.0f, 7.0f);

    // The macro deck: one raised plate, the six macros, the scope at the right.
    const auto plate = deck.toFloat();
    dd::gui::paintRaisedShadow (g, plate, 14.0f);
    dd::gui::paintFaceplate (g, plate, subGritPalette());
    g.setGradientFill (juce::ColourGradient (t.signal.withAlpha (t.light ? 0.05f : 0.08f), 0.0f, plate.getY(),
                                             t.signal.withAlpha (0.0f), 0.0f, plate.getY() + 70.0f, false));
    g.fillRoundedRectangle (plate.reduced (1.0f).withHeight (70.0f), 13.0f);
    dd::gui::drawCaption (g, "Shape the sound", { plate.getX() + 18.0f, plate.getY() + 10.0f, 220.0f, 18.0f });
    dd::gui::drawCaption (g, "Output", { (float) simpleScope.getX(), plate.getY() + 10.0f, 120.0f, 18.0f });

    // What the two ends of each macro do, under its knob.
    static const char* const ends[][2] = { { "thin", "huge" }, { "soft", "hard" }, { "clean", "dirty" },
                                           { "dark", "bright" }, { "short", "long" }, { "none", "long" } };
    const auto f = dd::gui::LookAndFeel::font (9.5f, 700).withExtraKerningFactor (0.12f);
    g.setFont (f);
    g.setColour (t.cream500);
    for (size_t i = 0; i < macros.size(); ++i)
    {
        const auto kb = macros[i]->getBounds().toFloat();
        const auto row = juce::Rectangle<float> (kb.getX() + kb.getWidth() * 0.12f, kb.getBottom() + 2.0f, kb.getWidth() * 0.76f, 14.0f);
        g.drawText (juce::String (ends[i][0]).toUpperCase(), row, juce::Justification::centredLeft, false);
        g.drawText (juce::String (ends[i][1]).toUpperCase(), row, juce::Justification::centredRight, false);
    }
}

void SubGritEditor::Content::resized()
{
    topBar.setBounds (0, 8, getWidth(), dd::gui::TopBar::kHeight);
    description.setBounds (26, topBar.getBottom() + 6, getWidth() - 52, 22);
    atmosphere.setBounds (getLocalBounds());

    auto r = getLocalBounds().reduced (14, 0).withTrimmedTop (description.getBottom() + 6).withTrimmedBottom (14);

    if (! proc.allKnobs)
    {
        auto area = r;
        deck = area.removeFromBottom (kDeckH);
        area.removeFromBottom (12);
        sub.setBounds (area.reduced (4, 2));

        auto inner = deck.reduced (18, 0).withTrimmedTop (34).withTrimmedBottom (26);
        simpleScope.setBounds (inner.removeFromRight (250).reduced (4, 6));
        inner.removeFromRight (20);
        const int knobW = inner.getWidth() / (int) macros.size();
        for (auto& k : macros)
            k->setBounds (inner.removeFromLeft (knobW).reduced (6, 0));
        return;
    }

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
