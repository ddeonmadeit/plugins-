#include "PluginEditor.h"

namespace
{
namespace sunset = dd::gui::sunset;

dd::gui::Palette sunburstPalette()
{
    dd::gui::Palette p;   // brushed brown metal, silver hardware; warm colour only for things that glow
    p.accent = sunset::orange;
    p.accent2 = sunset::amber;
    return p;
}
} // namespace

//==============================================================================
namespace
{
/** What every control does, in plain words: shown under the top bar while the mouse is over it. */
std::map<juce::String, juce::String> sunburstHelp()
{
    return {
        { "guitar",      "Which guitar plays: click the arrows or the name. Six real guitars, recorded note by note." },
        { "stringNoise", "Finger squeaks, string damping, slide scrapes and pick ticks: from very real to completely clean." },
        { "pick",        "How the strings are played: soft fingers (left) to a hard pick (right)." },
        { "mute",        "Palm mute: the hand resting on the strings for a short, chunky sound. The mod wheel mutes too." },
        { "tone",        "Darker or brighter guitar." },
        { "ring",        "How long notes keep ringing after you let go." },
        { "strum",       "Chords are strummed: the time between strings. 0 plays all strings at once." },
        { "legato",      "Notes played while another is held: Hammer (hammer-on) or Slide (the pitch glides), instead of a new pick." },
        { "slideTime",   "How long a slide takes." },
        { "vibrato",     "Finger vibrato on held notes." },
        { "humanize",    "Small changes in timing and level, like a real player." },
        { "velCurve",    "How strongly your playing strength changes the sound: harder keys play harder-picked notes." },
        { "bendRange",   "How far the pitch wheel bends, in semitones." },
        { "transpose",   "Moves the guitar up or down in semitones (a baritone is about -5)." },
        { "doubleTrack", "A second take of the part, panned to the other side: wide, big guitars." },
        { "body",        "Acoustic guitars: how much of the wooden body and air you hear." },

        { "compOn",      "Compressor pedal: evens out the playing and adds sustain. Click to switch it on." },
        { "compAmount",  "How hard the compressor squeezes." },
        { "driveOn",     "Overdrive pedal: warm, crunchy breakup. Click to switch it on." },
        { "driveGain",   "How much the overdrive breaks up." },
        { "driveTone",   "Darker or brighter overdrive." },
        { "fuzzOn",      "Fuzz pedal: thick, buzzing, sustaining distortion. Click to switch it on." },
        { "fuzzGain",    "How much fuzz." },
        { "chorusOn",    "Chorus pedal: a shimmering, doubled sound. Click to switch it on." },
        { "chorusRate",  "Chorus speed." },
        { "chorusDepth", "How deep the chorus swirls." },
        { "tremOn",      "Tremolo pedal: the volume pulses in time with the song. Click to switch it on." },
        { "tremDiv",     "Tremolo speed as a note length." },
        { "tremDepth",   "How deep the tremolo pulses." },
        { "delayOn",     "Delay pedal: echoes in time with the song. Click to switch it on." },
        { "delayDiv",    "Echo time as a note length." },
        { "delayFeedback","How many times the echo repeats." },
        { "delayMix",    "How loud the echoes are." },

        { "ampOn",       "The amplifier: switch it on for electric guitar tone (off for a clean acoustic or DI)." },
        { "ampModel",    "Amp channel: Clean, Crunch (edge of breakup) or Lead (high gain)." },
        { "ampGain",     "How hard the amp is driven." },
        { "ampBass",     "Amp bass." },
        { "ampMid",      "Amp mids: where the guitar sits in a mix." },
        { "ampTreble",   "Amp treble." },
        { "ampPresence", "Amp presence: the top-end bite." },
        { "ampLevel",    "Amp volume." },
        { "cabOn",       "The speaker cabinet the amp plays through." },
        { "cabType",     "1x12 combo (tight) or 4x12 stack (big)." },
        { "cabMic",      "Where the mic is on the speaker: the centre (bright) to the edge (dark)." },
        { "reverbOn",    "The room the guitar is played in." },
        { "reverbMix",   "How much room you hear." },
        { "reverbSize",  "How big the room is." },
        { "width",       "Stereo width." },
        { "glue",        "Gentle compression that holds the sound together." },
        { "outGain",     "Final volume." },
        { "perfTapeStop","Tape stop: the guitar slows to a halt like a tape machine powering down. Automate it at the end of a beat." },
        { "perfTapeTime","How long the tape stop takes." },
    };
}
} // namespace

SunburstEditor::Content::Content (SunburstProcessor& p)
    : proc (p),
      topBar (p.presets, "SUN", "BURST", "Acoustic / Electric Guitar"),
      description (topBar.presets()),
      tapeStop (p.apvts),
      guitar (p),
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
    auto pedal = [&] (const char* name, const char* onId, C controls)
    {
        auto pv = std::make_unique<PedalView> (s, name, onId, std::move (controls));
        addAndMakeVisible (*pv);
        pedals.push_back (std::move (pv));
    };
    pedal ("Comp",   "compOn",   C { { "compAmount", "Squeeze" } });
    pedal ("Drive",  "driveOn",  C { { "driveGain", "Gain" }, { "driveTone", "Tone" } });
    pedal ("Fuzz",   "fuzzOn",   C { { "fuzzGain", "Fuzz" } });
    pedal ("Chorus", "chorusOn", C { { "chorusRate", "Rate" }, { "chorusDepth", "Depth" } });
    pedal ("Trem",   "tremOn",   C { { "tremDiv", "Rate" }, { "tremDepth", "Depth" } });
    pedal ("Delay",  "delayOn",  C { { "delayDiv", "Time" }, { "delayFeedback", "Repeats" }, { "delayMix", "Mix" } });

    topBar.addRight (tapeStop, 196);
    addAndMakeVisible (topBar);
    description.setHint (juce::String ("ddeonmadeit  v") + DD_PLUGIN_VERSION);
    description.setHelpTable (sunburstHelp());
    addAndMakeVisible (description);
    addAndMakeVisible (guitar);
    addAndMakeVisible (playing);
    addAndMakeVisible (amp);
    addAndMakeVisible (room);
    addAndMakeVisible (atmosphere);
}

void SunburstEditor::Content::paint (juce::Graphics& g)
{
    dd::gui::paintBackdrop (g, getLocalBounds(), sunburstPalette());
    dd::gui::paintPanelShadows (g, *this);
    dd::gui::paintBezel (g, guitar.getBounds().toFloat(), 10.0f, 7.0f);   // the stage, seen through a window in the chassis

    // The pedalboard: a long recessed tray the pedals stand in.
    dd::gui::paintTray (g, pedalboard.toFloat(), 14.0f, 1.0f);
    dd::gui::drawCaption (g, "Pedalboard", pedalboard.toFloat().reduced (18.0f, 0.0f).withHeight (30.0f));
}

void SunburstEditor::Content::resized()
{
    topBar.setBounds (0, 8, getWidth(), dd::gui::TopBar::kHeight);
    description.setBounds (26, topBar.getBottom() + 6, getWidth() - 52, 22);
    atmosphere.setBounds (getLocalBounds());

    auto r = getLocalBounds().reduced (12, 0).withTrimmedTop (description.getBottom() + 6).withTrimmedBottom (14);
    auto top = r.removeFromTop (playing.preferredSize().y);
    playing.setBounds (top.removeFromRight (playing.preferredSize().x));
    top.removeFromRight (14);
    guitar.setBounds (top.reduced (2, 0));
    r.removeFromTop (12);

    auto bottom = r.removeFromBottom (amp.preferredSize().y);
    room.setBounds (bottom.removeFromRight (room.preferredSize().x));
    bottom.removeFromRight (10);
    amp.setBounds (bottom.removeFromLeft (amp.preferredSize().x));
    r.removeFromBottom (12);

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
