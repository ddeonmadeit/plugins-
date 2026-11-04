#include "PluginEditor.h"

namespace
{
namespace sunset = dd::gui::sunset;
using dd::gui::Knob;
using dd::gui::Choice;
using dd::gui::Toggle;
const juce::Colour kA = sunset::orange;
} // namespace

AircheckEditor::Content::Content (AircheckProcessor& p)
    : proc (p),
      topBar (p.presets, "AIR", "CHECK", "Vocal Chain + Pitch Correction"),
      description (topBar.presets()),
      tune ("Pitch Correction", 7, 4, kA),
      eq ("Equalizer", 11, 4, kA),
      voice ("Mic & Preamp", 3, 1, kA),
      comp ("Compressor", 7, 1, kA),
      deess ("De-Esser", 2, 1, kA),
      dbl ("Double", 2, 1, kA),
      vocoder ("Vocoder", 3, 1, kA),
      delay ("Tape Delay", 8, 1, kA),
      reverb ("Reverb", 5, 1, kA),
      tapeP ("Tape", 1, 1, kA),
      output ("Output", 2, 1, kA)
{
    auto& s = p.apvts;

    tune.add<PitchView> (p);
    tune.place (0, 0, 7, 2);
    tune.add<Toggle> (s, "tuneOn", "Tune");                     tune.place (0, 2);
    tune.add<Choice> (s, "tuneKey", "Key");                      tune.place (1, 2);
    tune.add<Choice> (s, "tuneScale", "Scale");                  tune.place (2, 2, 2, 1);
    tune.add<Knob> (s, "tuneSpeed", "Retune", kA).getSlider().setTooltip ("How fast notes are pulled in tune. 0 ms = the hard, robotic effect.");
    tune.place (4, 2);
    tune.add<Knob> (s, "tuneHumanize", "Humanize", kA).getSlider().setTooltip ("Corrects held notes more gently so long notes keep their life");
    tune.place (5, 2);
    tune.add<Knob> (s, "tuneFlex", "Flex", kA).getSlider().setTooltip ("Leaves notes alone the further they are from the scale (slides, bends, expression)");
    tune.place (6, 2);
    tune.add<Knob> (s, "tuneVibrato", "Vibrato", kA).getSlider().setTooltip ("How much of the singer's own vibrato is kept (0 = flattened)");
    tune.place (0, 3);
    tune.add<Knob> (s, "tuneAmount", "Amount", kA);              tune.place (1, 3);
    tune.add<Knob> (s, "tuneTranspose", "Transpose", kA);        tune.place (2, 3);
    tune.add<Knob> (s, "tuneFormant", "Formant", kA).getSlider().setTooltip ("Moves the voice's character (vowels, size) without changing the notes");
    tune.place (3, 3);
    tune.add<Knob> (s, "inGain", "Input", kA);                   tune.place (5, 3);
    tune.add<Knob> (s, "gate", "Gate", kA).getSlider().setTooltip ("Turns down breaths and room noise between phrases. Fully left = off.");
    tune.place (6, 3);

    eq.add<EqView> (p);
    eq.place (0, 0, 11, 3);
    int col = 0;
    for (auto [id, title] : { std::pair { "eqLowCut", "Low Cut" }, { "eqLsGain", "Low" }, { "eq1Freq", "Freq 1" }, { "eq1Gain", "Gain 1" },
                              { "eq2Freq", "Freq 2" }, { "eq2Gain", "Gain 2" }, { "eq3Freq", "Freq 3" }, { "eq3Gain", "Gain 3" },
                              { "eqHsFreq", "High Freq" }, { "eqHsGain", "High" }, { "eqHighCut", "High Cut" } })
    {
        eq.add<Knob> (s, id, title, kA).getSlider().setTooltip ("Or drag the points on the curve; scroll on a point for its width (Q)");
        eq.place (col++, 3);
    }

    voice.add<Choice> (s, "micType", "Mic");
    voice.add<Choice> (s, "preType", "Preamp");
    voice.add<Knob> (s, "preDrive", "Drive", kA);

    comp.add<Choice> (s, "compType", "Type");
    comp.add<Knob> (s, "compAmount", "Comp", kA);
    comp.add<Knob> (s, "compAttack", "Attack", kA);
    comp.add<Knob> (s, "compRelease", "Release", kA);
    comp.add<Knob> (s, "compRatio", "Ratio", kA);
    comp.add<Knob> (s, "compMix", "Mix", kA).getSlider().setTooltip ("Blend the compressed voice with the uncompressed one (parallel compression)");
    comp.add<GrMeter> (p);

    deess.add<Knob> (s, "deess", "Amount", kA);
    deess.add<Knob> (s, "deessFreq", "Freq", kA);
    dbl.add<Knob> (s, "dblMix", "Double", kA).getSlider().setTooltip ("Two extra takes, a little late and out of step, panned wide");
    dbl.add<Knob> (s, "dblWidth", "Width", kA);
    vocoder.add<Knob> (s, "vocMix", "Vocoder", kA).getSlider().setTooltip ("A 16-band vocoder playing the note you sing (plus the chord): no MIDI needed");
    vocoder.add<Choice> (s, "vocCarrier", "Carrier");
    vocoder.add<Choice> (s, "vocChord", "Chord");

    delay.add<Knob> (s, "dlyMix", "Delay", kA);
    delay.add<Toggle> (s, "dlySync", "Sync", "SYNC", "FREE");
    delay.add<Choice> (s, "dlyDiv", "Time");
    delay.add<Knob> (s, "dlyMs", "Time ms", kA).getSlider().setTooltip ("Delay time when Sync is off (slapback: 80-140 ms)");
    delay.add<Knob> (s, "dlyFeedback", "Repeats", kA);
    delay.add<Knob> (s, "dlyTone", "Tone", kA);
    delay.add<Toggle> (s, "dlyPingPong", "Ping Pong");
    delay.add<Knob> (s, "dlyDuck", "Duck", kA).getSlider().setTooltip ("Pushes the echoes down while you sing, so they bloom in the gaps");

    reverb.add<Knob> (s, "revMix", "Reverb", kA);
    reverb.add<Choice> (s, "revType", "Space");
    reverb.add<Knob> (s, "revSize", "Size", kA);
    reverb.add<Knob> (s, "revPreDelay", "Pre-Delay", kA);
    reverb.add<Knob> (s, "revTone", "Tone", kA);

    tapeP.add<Knob> (s, "tape", "Tape", kA).getSlider().setTooltip ("Tape machine: saturation, a softer top and a little wow and flutter");
    output.add<Knob> (s, "mix", "Mix", kA);
    output.add<Knob> (s, "outGain", "Output", kA);

    addAndMakeVisible (topBar);
    description.setHint (juce::String ("ddeonmadeit  v") + DD_PLUGIN_VERSION);
    addAndMakeVisible (description);
    for (auto* c : { &tune, &eq, &voice, &comp, &deess, &dbl, &vocoder, &delay, &reverb, &tapeP, &output })
        addAndMakeVisible (c);
    addAndMakeVisible (atmosphere);
}

void AircheckEditor::Content::paint (juce::Graphics& g)
{
    dd::gui::paintBackdrop (g, getLocalBounds(), {});
    dd::gui::paintPanelShadows (g, *this);
}

void AircheckEditor::Content::resized()
{
    topBar.setBounds (0, 8, getWidth(), dd::gui::TopBar::kHeight);
    description.setBounds (26, topBar.getBottom() + 6, getWidth() - 52, 22);
    atmosphere.setBounds (getLocalBounds());

    auto r = getLocalBounds().reduced (14, 0).withTrimmedTop (description.getBottom() + 6).withTrimmedBottom (14);
    const int gap = 10;
    auto row = [&] (std::initializer_list<dd::gui::Panel*> panels)
    {
        auto area = r.removeFromTop (panels.begin()[0]->preferredSize().y);
        r.removeFromTop (gap);
        int total = 0;
        for (auto* p : panels) total += p->preferredSize().x;
        const int avail = area.getWidth() - gap * ((int) panels.size() - 1);
        int x = area.getX(), i = 0;
        for (auto* p : panels)
        {
            const bool last = ++i == (int) panels.size();
            const int w = last ? area.getRight() - x : (int) std::round ((double) p->preferredSize().x * avail / total);
            p->setBounds (x, area.getY(), w, area.getHeight());
            x += w + gap;
        }
    };
    row ({ &tune, &eq });
    row ({ &voice, &comp, &deess, &dbl, &vocoder });
    row ({ &delay, &reverb, &tapeP, &output });
}

//==============================================================================
AircheckEditor::AircheckEditor (AircheckProcessor& p)
    : AudioProcessorEditor (p), proc (p)
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

AircheckEditor::~AircheckEditor()
{
    content.reset();
    setLookAndFeel (nullptr);
}

void AircheckEditor::paint (juce::Graphics& g)
{
    g.fillAll (dd::gui::tokens().room);
}

void AircheckEditor::resized()
{
    const float scale = (float) getWidth() / (float) kBaseW;
    content->setTransform (juce::AffineTransform::scale (scale));
    if (constructed)
        proc.editorScale = scale;
}
