#include "PluginEditor.h"

namespace
{
using dd::gui::Knob;
using dd::gui::Choice;
const juce::Colour kA = dd::gui::sunset::orange;
} // namespace

ContourEditor::Content::Content (ContourProcessor& p)
    : proc (p),
      topBar (p.presets, "CON", "TOUR", "Note Editor"),
      description (topBar.presets()),
      follow (p),
      notes ("Notes", 14, 8, kA),
      correction ("Correction", 5, 2, kA),
      selected ("Selected", 5, 3, kA),
      session ("Session", 5, 2, kA)
{
    auto& s = p.apvts;

    auto& editor = notes.add<contour::NoteEditor> (p, state);
    notes.place (0, 0, 14, 8);

    correction.add<Choice> (s, "perfKey", "Key");
    correction.add<Choice> (s, "perfScale", "Scale");
    correction.place (1, 0, 2, 1);
    correction.add<Knob> (s, "transpose", "Transpose", kA);   correction.place (3, 0);
    correction.add<Knob> (s, "outGain", "Output", kA);        correction.place (4, 0);
    correction.add<Knob> (s, "center", "Center", kA).getSlider().setTooltip ("Pulls every note you haven't moved by hand toward the nearest note of the scale");
    correction.add<Knob> (s, "drift", "Drift", kA).getSlider().setTooltip ("Steadies the slow wander inside held notes (vibrato is kept)");
    correction.add<Knob> (s, "vibrato", "Vibrato", kA).getSlider().setTooltip ("Scales the singer's vibrato: 0% flat, 100% as sung, 200% doubled");
    correction.add<Knob> (s, "transition", "Transition", kA).getSlider().setTooltip ("How quickly corrected pitch moves from note to note (short = the hard-tune effect)");
    correction.add<Knob> (s, "formant", "Formant", kA).getSlider().setTooltip ("Moves the voice's character (bigger and darker / smaller and brighter) without changing the notes");

    selected.add<contour::Inspector> (p, state, editor.getCanvas());
    selected.place (0, 0, 5, 3);
    session.add<contour::SessionView> (p, state, editor.getCanvas());
    session.place (0, 0, 5, 2);

    topBar.addRight (follow, 118);
    addAndMakeVisible (topBar);
    description.setHint (juce::String ("ddeonmadeit  v") + DD_PLUGIN_VERSION);
    addAndMakeVisible (description);
    for (auto* c : { &notes, &correction, &selected, &session })
        addAndMakeVisible (c);
    addAndMakeVisible (atmosphere);
    dd::gui::giveKeysOnlyTo (*this, editor.getCanvas());
}

void ContourEditor::Content::paint (juce::Graphics& g)
{
    dd::gui::paintBackdrop (g, getLocalBounds(), {});
    dd::gui::paintPanelShadows (g, *this);
}

void ContourEditor::Content::resized()
{
    topBar.setBounds (0, 8, getWidth(), dd::gui::TopBar::kHeight);
    description.setBounds (26, topBar.getBottom() + 6, getWidth() - 52, 22);
    atmosphere.setBounds (getLocalBounds());

    auto r = getLocalBounds().reduced (14, 0).withTrimmedTop (description.getBottom() + 6).withTrimmedBottom (14);
    constexpr int gap = 10;
    const int total = notes.preferredSize().x + correction.preferredSize().x;
    const int avail = r.getWidth() - gap;
    const int wNotes = (int) std::round ((double) notes.preferredSize().x * avail / total);
    notes.setBounds (r.removeFromLeft (wNotes));
    r.removeFromLeft (gap);
    correction.setBounds (r.removeFromTop (correction.preferredSize().y));
    r.removeFromTop (gap);
    session.setBounds (r.removeFromBottom (session.preferredSize().y));
    r.removeFromBottom (gap);
    selected.setBounds (r);
}

//==============================================================================
ContourEditor::ContourEditor (ContourProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    content = std::make_unique<Content> (p);
    content->setBounds (0, 0, kBaseW, kBaseH);
    addAndMakeVisible (*content);

    // setResizeLimits() triggers resized(), so read the saved zoom first.
    const float savedScale = dd::gui::openingScale (kBaseW, kBaseH, proc.editorScale);
    setResizable (true, true);
    setResizeLimits ((int) (kBaseW * 0.5f), (int) (kBaseH * 0.5f), (int) (kBaseW * 1.8f), (int) (kBaseH * 1.8f));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setSize ((int) std::round (kBaseW * savedScale), (int) std::round (kBaseH * savedScale));
    constructed = true;
}

ContourEditor::~ContourEditor()
{
    content.reset();
    setLookAndFeel (nullptr);
}

void ContourEditor::paint (juce::Graphics& g)
{
    g.fillAll (dd::gui::tokens().room);
}

void ContourEditor::resized()
{
    const float scale = (float) getWidth() / (float) kBaseW;
    content->setTransform (juce::AffineTransform::scale (scale));
    if (constructed)
        proc.editorScale = scale;
}
