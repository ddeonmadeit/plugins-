#include "PluginEditor.h"

namespace
{
dd::gui::Palette palette()
{
    dd::gui::Palette p;   // house style (rawleads): colours from the DARK / LITE tokens
    p.accent = dd::gui::sunset::orange;
    return p;
}

std::map<juce::String, juce::String> orbitHelp()
{
    return {
        { "posX", "Moves the sound to your left or right, wall to wall. Or just drag the light in the room." },
        { "posY", "Moves the sound in front of you (up) or behind you (down)." },
        { "posZ", "Raises the sound from the floor to the ceiling. Your ears are at about 40 %." },
        { "width", "How wide the sound is from where you sit: 0 is a single point, 100 % spreads its left and right across half the horizon." },
        { "room", "The space it plays in. Each room has its own size, walls and echo." },
        { "size", "Shrinks or grows the whole room (bigger rooms ring longer)." },
        { "roomLevel", "How much of the room you hear: 0 is bone dry, 50 % is how the room really sounds, 100 % drenches it." },
        { "motion", "Lets the sound travel on its own, in time with your song: orbit, swing, fly past, rise, spiral or wander." },
        { "motionRate", "How long one trip takes, in bars or beats of your song." },
        { "motionAmount", "How far it travels from where you put it." },
        { "doppler", "The pitch bend of a sound rushing past you (like a passing car). 0 keeps the pitch steady." },
        { "mix", "Blend of the placed sound with the untouched input." },
        { "outGain", "Final level." },
        { "perfOutput", "Off: true 3D for headphones (in front, behind, above you). On: tuned for speakers, clean on monitors. Not saved in presets." },
        { "perfHead", "Whose ears do the listening (measured ears from the Viking HRTF dataset v2, CC BY 4.0). Try each: the one where sounds behind you really sound behind you suits your ears. Not saved in presets." },
    };
}

const juce::StringArray& roomHelp()
{
    static const juce::StringArray h {
        "A treated recording room: close, clear, a short tidy echo.",
        "A tiny padded booth: almost dry, right up close.",
        "A furnished room at home: soft and warm, a little life.",
        "Hard tiles: bright, boxy and splashy, rings for about a second.",
        "Inside a car: tight and boxy, no tail at all.",
        "A dark club with a low ceiling: a solid one-second bloom.",
        "Concrete and steel: big, bright, a long hard tail.",
        "A real concert hall: wide and smooth, two seconds of lush tail.",
        "Stone and glass, huge and tall: a five-second tail.",
        "A stadium: enormous and distant, echoes slapping back.",
        "Wet rock: dark, deep and uneven.",
        "Outdoors: only the ground under you, no walls, no echo.",
    };
    return h;
}

const juce::StringArray& motionHelp()
{
    static const juce::StringArray h {
        "Stays where you put it.",
        "Circles round. From the starting spot in front of you it circles your head.",
        "Swings from side to side.",
        "Traces a figure of eight.",
        "Rushes from front to back past your side (turn up Doppler).",
        "Floats up and down.",
        "Circles and climbs over four turns.",
        "Drifts around like something alive, never quite repeating.",
    };
    return h;
}
} // namespace

//==============================================================================
ChoiceKeys::ChoiceKeys (juce::RangedAudioParameter& p, int columns, const juce::StringArray& help)
    : param (p), attachment (p, [this] (float v)
      {
          const int index = juce::roundToInt (v);
          for (int i = 0; i < keys.size(); ++i)
              keys[i]->setLit (i == index);
      }),
      cols (columns)
{
    const auto names = p.getAllValueStrings();
    for (int i = 0; i < names.size(); ++i)
    {
        auto* k = keys.add (new dd::gui::Key (names[i]));
        k->setGlowMargin (5);
        k->setFlickerPeriod (4.3 + 0.37 * (i % 5));
        k->onClick = [this, i] { attachment.setValueAsCompleteGesture ((float) i); };
        dd::gui::setHelp (*k, names[i], help[i]);
        addAndMakeVisible (k);
    }
    attachment.sendInitialUpdate();
}

void ChoiceKeys::resized()
{
    const int rows = (keys.size() + cols - 1) / cols;
    const float w = (float) getWidth() / (float) cols, h = (float) getHeight() / (float) rows;
    for (int i = 0; i < keys.size(); ++i)
        keys[i]->setBounds (juce::Rectangle<float> ((float) (i % cols) * w, (float) (i / cols) * h, w, h).toNearestInt());
}

//==============================================================================
CycleDial::CycleDial (OrbitProcessor& p) : proc (p)
{
    dd::gui::setHelp (*this, "Cycle", "Where the sound is in its trip: one turn of the ring is one trip, in time with your song.");
    startTimerHz (30);
}

void CycleDial::paint (juce::Graphics& g)
{
    const auto& t = dd::gui::tokens();
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    dd::gui::drawCaption (g, "CYCLE", r.removeFromTop (14.0f), juce::Justification::centred, 9.5f);
    const float d = std::min (r.getWidth(), r.getHeight()) - 6.0f;
    const auto ring = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
    dd::gui::paintTray (g, ring.expanded (4.0f), d * 0.5f + 4.0f, 0.8f);
    const int mode = proc.apvts.getRawParameterValue ("motion")->load() > 0.5f ? 1 : 0;
    const double phase = proc.telemetry.phase.load();
    const float frac = (float) (phase - std::floor (phase));
    const float a = juce::MathConstants<float>::twoPi * frac;
    juce::Path arc;
    arc.addCentredArc (ring.getCentreX(), ring.getCentreY(), d * 0.38f, d * 0.38f, 0.0f, 0.0f, a, true);
    g.setColour (mode ? t.signal.withAlpha (0.8f) : t.faint);
    g.strokePath (arc, juce::PathStrokeType (2.0f));
    const juce::Point<float> dot (ring.getCentreX() + d * 0.38f * std::sin (a), ring.getCentreY() - d * 0.38f * std::cos (a));
    dd::gui::paintLamp (g, dot, 4.0f, dd::gui::sunset::amber, mode ? 1.0f : 0.15f);
}

//==============================================================================
EarsControl::EarsControl (juce::AudioProcessorValueTreeState& s)
    : attachment ((box.addItemList (s.getParameter ("perfHead")->getAllValueStrings(), 1), s), "perfHead", box)
{
    dd::gui::tagParam (*this, "perfHead", "Ears");
    dd::gui::tagParam (box, "perfHead", "Ears");
    addAndMakeVisible (box);
}

void EarsControl::resized() { box.setBounds (getLocalBounds().withTrimmedLeft (46)); }

void EarsControl::paint (juce::Graphics& g)
{
    g.setFont (dd::gui::LookAndFeel::font (11.5f, 600).withExtraKerningFactor (0.06f));
    g.setColour (dd::gui::tokens().cream500);
    g.drawText ("EARS", getLocalBounds().withWidth (40), juce::Justification::centredRight, false);
}

//==============================================================================
OrbitEditor::OrbitEditor (OrbitProcessor& p)
    : AudioProcessorEditor (p), lnf (palette()),
      topBar (p.presets, "OR", "BIT", "3D ROOM PLACEMENT"),
      description (topBar.presets()),
      speakers (*p.apvts.getParameter ("perfOutput"), "Speakers", palette().accent),
      ears (p.apvts),
      scene (p),
      roomPanel ("ROOM", 5, 5, palette().accent),
      placePanel ("PLACE", 4, 2, palette().accent),
      motionPanel ("MOTION", 8, 2, palette().accent),
      outputPanel ("OUTPUT", 6, 2, palette().accent)
{
    setLookAndFeel (&lnf);
    auto& s = p.apvts;
    const auto a = palette().accent;

    roomPanel.add<ChoiceKeys> (*s.getParameter ("room"), 2, roomHelp());
    roomPanel.place (0, 0, 5, 3);
    roomPanel.add<dd::gui::Knob> (s, "size", "Size", a);
    roomPanel.place (0, 3, 2, 2);
    roomPanel.add<dd::gui::Knob> (s, "roomLevel", "Room Level", a);
    roomPanel.place (3, 3, 2, 2);

    int col = 0;
    for (auto [id, title] : { std::pair { "posX", "Side" }, { "posY", "Front" }, { "posZ", "Height" }, { "width", "Width" } })
    {
        placePanel.add<dd::gui::Knob> (s, id, title, a);
        placePanel.place (col++, 0, 1, 2);
    }

    motionPanel.add<ChoiceKeys> (*s.getParameter ("motion"), 2, motionHelp());
    motionPanel.place (0, 0, 4, 2);
    motionPanel.add<dd::gui::Choice> (s, "motionRate", "Rate");
    motionPanel.place (4, 0, 2, 1);
    motionPanel.add<CycleDial> (p);
    motionPanel.place (4, 1, 2, 1);
    motionPanel.add<dd::gui::Knob> (s, "motionAmount", "Travel", a);
    motionPanel.place (6, 0, 1, 2);
    motionPanel.add<dd::gui::Knob> (s, "doppler", "Doppler", a);
    motionPanel.place (7, 0, 1, 2);

    outputPanel.add<dd::gui::Knob> (s, "mix", "Mix", a);
    outputPanel.place (0, 0, 1, 2);
    outputPanel.add<dd::gui::Knob> (s, "outGain", "Output", a);
    outputPanel.place (1, 0, 1, 2);
    outputPanel.add<dd::gui::Scope> (p.scope);
    outputPanel.place (2, 0, 4, 2);

    for (auto* panel : { &roomPanel, &placePanel, &motionPanel, &outputPanel })
    {
        dd::gui::markRaised (*panel);
        addAndMakeVisible (*panel);
    }

    topBar.addRight (speakers, 120);
    topBar.addRight (ears, 150);
    description.setHint (juce::String ("ddeonmadeit  v") + DD_PLUGIN_VERSION);
    description.setHelpTable (orbitHelp());
    addAndMakeVisible (topBar);
    addAndMakeVisible (description);
    addAndMakeVisible (scene);
    addAndMakeVisible (atmosphere);

    const int bottomW = placePanel.preferredSize().x + motionPanel.preferredSize().x + outputPanel.preferredSize().x + 24;
    setSize (bottomW + 28, 108 + roomPanel.preferredSize().y + 12 + placePanel.preferredSize().y + 14);
}

OrbitEditor::~OrbitEditor() { setLookAndFeel (nullptr); }

void OrbitEditor::paint (juce::Graphics& g)
{
    dd::gui::paintBackdrop (g, getLocalBounds(), lnf.palette());
    dd::gui::paintPanelShadows (g, *this);
}

void OrbitEditor::resized()
{
    topBar.setBounds (0, 8, getWidth(), dd::gui::TopBar::kHeight);
    description.setBounds (26, topBar.getBottom() + 6, getWidth() - 52, 22);
    auto r = getLocalBounds().reduced (14, 0).withTrimmedTop (108).withTrimmedBottom (14);
    auto top = r.removeFromTop (roomPanel.preferredSize().y);
    roomPanel.setBounds (top.removeFromRight (roomPanel.preferredSize().x));
    top.removeFromRight (12);
    scene.setBounds (top);
    r.removeFromTop (12);
    placePanel.setBounds (r.removeFromLeft (placePanel.preferredSize().x));
    r.removeFromLeft (12);
    outputPanel.setBounds (r.removeFromRight (outputPanel.preferredSize().x));
    r.removeFromRight (12);
    motionPanel.setBounds (r);
    atmosphere.setBounds (getLocalBounds());
}
