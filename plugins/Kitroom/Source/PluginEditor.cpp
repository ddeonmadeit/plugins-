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

namespace
{
/** What every control does, in plain words: shown under the top bar while the mouse is over it. */
std::map<juce::String, juce::String> kitroomHelp()
{
    std::map<juce::String, juce::String> h {
        { "masterClose",     "All close mics: the punchy, dry sound of each drum." },
        { "masterOverheads", "The two mics above the kit: the whole kit and the cymbals in stereo." },
        { "masterRoom",      "The room mics: the space around the kit. Up for big, down for tight." },
        { "velCurve",        "How your playing strength maps to hits: Soft makes quiet notes louder, Hard keeps the loud hits for hard notes." },
        { "humanize",        "Small random changes in level, like a real drummer." },
        { "glue",            "Bus compression that pulls the kit together and makes it punchier." },
        { "clip",            "Soft clipping on the kit: louder and denser, peaks rounded off." },
        { "width",           "Stereo width of the kit (the lows stay in the middle)." },
        { "outGain",         "Final volume." },
        { "drive",           "Distortion on the whole kit." },
        { "driveType",       "Distortion flavour: Tape (warm), Tube (rich), Fuzz, Clip (hard), Fold (metallic), Crush (lo-fi), Rectify, Amp." },
        { "crush",           "Bit crusher: gritty, lo-fi, sampler-style drums." },
        { "tone",            "Darker or brighter kit." },
        { "clapOnSnare",     "Every snare hit (not the sidestick) also plays the clap, at the same strength." },
        { "layBack",         "Plays the snare and clap behind the beat (hats and ride a little less, the kick stays on time): the lazy neo-soul pocket. 0 is right on the grid." },
        { "logVolume",       "Volume of the log drum (it plays on the keys C5 to C8)." },
        { "logTune",         "Moves the whole log drum up or down, in semitones, to sit in your song's key." },
        { "logLength",       "How long each log drum note rings: short and plucky, or long and booming." },
        { "logKnock",        "The woody knock at the start of each note: the quick pitch drop and the click of the mallet." },
        { "logTone",         "Darker or brighter log drum." },
        { "logDrive",        "Saturation on the log drum: more grit, and it cuts through on small speakers." },
        { "logSub",          "A clean sine under the log drum for weight in the low end." },
        { "logGlide",        "How long a slide takes: overlap two log drum notes and the second slides up or down into place." },
        { "logModel",        "Classic: the original FL Studio FM log drum. Woody: more mallet and a hollow knock. Deep: rounder, more sub. Bounce: the bouncy donk." },
    };
    for (int s = 0; s < kr::kNumSlots; ++s)
    {
        const juce::String id = kr::slots()[(size_t) s].id, name = juce::String (kr::slots()[(size_t) s].name).toLowerCase();
        h[id + "Volume"]    = "Volume of the " + name + ".";
        h[id + "Pan"]       = "Left or right. Auto: where the " + name + " sits in the overheads.";
        h[id + "Tune"]      = "Tunes the " + name + " up or down, in semitones.";
        h[id + "Decay"]     = "How long the " + name + " rings: Natural is the full recording, lower chokes it shorter.";
        h[id + "Close"]     = "The " + name + "'s close mic: punch and detail.";
        h[id + "Overheads"] = "How much of the " + name + " you hear in the overhead mics.";
        h[id + "Room"]      = "How much of the " + name + " you hear in the room mics.";
        h[id + "Punch"]     = "A harder attack and a tighter body on the " + name + "'s close mic; the room stays natural.";
    }
    return h;
}
} // namespace

KitroomEditor::Content::Content (KitroomProcessor& p)
    : proc (p),
      topBar (p.presets, "KIT", "ROOM", "Live Drum Kit / Multi-Mic"),
      description (topBar.presets()),
      kit (p),
      element (p),
      mix ("Mix", 7, 3, kMicAccent)
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
    mix.add<Knob> (s, "layBack", "Lay Back", kFeelAccent);
    mix.place (4, 0);
    mix.add<dd::gui::Choice> (s, "driveType", "Drive Type");
    mix.place (5, 0, 2, 1);
    mix.add<dd::gui::Scope> (p.scope);
    mix.place (4, 1, 3, 2);

    kit.onSelect = [this] (int slot) { element.setSlot (slot); };

    description.setHint (juce::String ("Click a piece to edit it  /  ddeonmadeit v") + DD_PLUGIN_VERSION);
    dd::gui::markRaised (element, 14.0f);
    addAndMakeVisible (topBar);
    description.setHelpTable (kitroomHelp());
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
