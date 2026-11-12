#include "ElementPanel.h"

namespace
{
const juce::Colour kAccent = dd::gui::sunset::orange;
constexpr int kKnobW = 64, kKnobH = 84;
}

ElementPanel::ElementPanel (KitroomProcessor& p) : proc (p)
{
    addAndMakeVisible (soundBox);
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (resetKeys);
    soundBox.setTooltip ("Swap this piece for any recorded sound of the same kind");
    resetKeys.setTooltip ("Put every key back on the General MIDI drum map");

    soundBox.onChange = [this]
    {
        const int i = soundBox.getSelectedItemIndex();
        if (juce::isPositiveAndBelow (i, (int) soundIndices.size()))
        {
            const auto& id = proc.library.getSounds()[(size_t) soundIndices[(size_t) i]].id;
            if (id != proc.getSlotSound (slot))
            {
                proc.setSlotSound (slot, id);
                proc.audition (slot, 0, 0.8f);
            }
        }
    };
    prevButton.onClick = [this] { step (-1); };
    nextButton.onClick = [this] { step (1); };
    resetKeys.onClick = [this] { proc.resetKeys(); refreshKeys(); };

    for (int a = 0; a < kr::kMaxArticulations; ++a)
    {
        auto& r = rows[(size_t) a];
        addChildComponent (r.play);
        addChildComponent (r.key);
        r.play.onClick = [this, a] { proc.audition (slot, a, 0.85f); };
        r.key.setTooltip ("Click, then press a key on your MIDI keyboard to assign it");
        r.key.onClick = [this, a]
        {
            if (isSynth())
                return;   // the log and steel drums' keys are fixed: their ranges above the kit
            const int t = kr::packTarget (slot, a);
            if (proc.getLearnTarget() == t) proc.cancelLearn();
            else proc.startLearn (slot, a);
            refreshKeys();
        };
    }

    startTimerHz (15);
    setSlot (p.selectedSlot);
}

void ElementPanel::setSlot (int s)
{
    if (s == slot)
        return;
    slot = s;
    proc.cancelLearn();

    knobs.clear();
    synthModel.reset();
    if (isSynth())
    {
        // The log and steel drums are synthesised: no sounds to swap, no mics, their own keys.
        using kitroom::MP;
        const std::vector<std::pair<MP, const char*>> defs = isLog()
            ? std::vector<std::pair<MP, const char*>> { { MP::LogVolume, "Volume" }, { MP::LogTune, "Tune" }, { MP::LogLength, "Length" }, { MP::LogKnock, "Knock" },
                                                        { MP::LogTone, "Tone" }, { MP::LogDrive, "Drive" }, { MP::LogSub, "Sub" }, { MP::LogGlide, "Slide" } }
            : std::vector<std::pair<MP, const char*>> { { MP::SteelVolume, "Volume" }, { MP::SteelTune, "Tune" }, { MP::SteelLength, "Ring" },
                                                        { MP::SteelBright, "Bright" }, { MP::SteelShimmer, "Shimmer" }, { MP::SteelRoll, "Roll" } };
        for (auto& [param, title] : defs)
        {
            auto k = std::make_unique<dd::gui::Knob> (proc.apvts, kitroom::masterParamId (param), title, kAccent);
            addAndMakeVisible (*k);
            knobs.push_back (std::move (k));
        }
        synthModel = std::make_unique<dd::gui::Choice> (proc.apvts, kitroom::masterParamId (isLog() ? MP::LogModel : MP::SteelModel), "Model");
        addAndMakeVisible (*synthModel);
        for (auto* c : { (juce::Component*) &soundBox, (juce::Component*) &prevButton, (juce::Component*) &nextButton, (juce::Component*) &resetKeys })
            c->setVisible (false);
        clapOnSnare.reset();
        for (int a = 0; a < kr::kMaxArticulations; ++a)
        {
            rows[(size_t) a].play.setVisible (a == 0);
            rows[(size_t) a].key.setVisible (a == 0);
        }
        rows[0].play.setButtonText ("> Play");
        const int lo = isLog() ? kr::kLogLowKey : kr::kSteelLowKey, hi = isLog() ? kr::kLogHighKey : kr::kSteelHighKey;
        rows[0].key.setButtonText ("Keys  " + dd::noteName (lo) + " - " + dd::noteName (hi));
        rows[0].key.setToggleState (false, juce::dontSendNotification);
        credit = isLog() ? "Synthesised in Kitroom: an FM knock with a quick pitch drop, the Amapiano way. Overlap two notes to slide."
                         : "Synthesised in Kitroom: tuned partials and a rubber-tipped stick, like a pan from Trinidad. Hold a note to roll it.";
        resized();
        repaint();
        return;
    }
    for (auto* c : { (juce::Component*) &soundBox, (juce::Component*) &prevButton, (juce::Component*) &nextButton, (juce::Component*) &resetKeys })
        c->setVisible (true);

    using kitroom::SP;
    const std::pair<SP, const char*> knobDefs[] = {
        { SP::Volume, "Volume" }, { SP::Pan, "Pan" }, { SP::Tune, "Tune" }, { SP::Decay, "Decay" },
        { SP::Close, "Close Mic" }, { SP::Overheads, "Overheads" }, { SP::Room, "Room" }, { SP::Punch, "Punch" } };
    for (auto& [param, title] : knobDefs)
    {
        auto k = std::make_unique<dd::gui::Knob> (proc.apvts, kitroom::slotParamId (slot, param), title, kAccent);
        addAndMakeVisible (*k);
        knobs.push_back (std::move (k));
    }

    clapOnSnare.reset();
    if (slot == kr::Snare || slot == kr::ClapSlot)
    {
        clapOnSnare = std::make_unique<dd::gui::Toggle> (proc.apvts, kitroom::masterParamId (kitroom::MP::ClapOnSnare),
                                                         "Clap on snare", "LAYERED", "OFF");
        addAndMakeVisible (*clapOnSnare);
    }

    const auto& arts = kr::slots()[(size_t) slot].articulations;
    for (int a = 0; a < kr::kMaxArticulations; ++a)
    {
        const bool used = a < (int) arts.size();
        rows[(size_t) a].play.setVisible (used);
        rows[(size_t) a].key.setVisible (used);
        if (used)
            rows[(size_t) a].play.setButtonText (juce::String ("> ") + arts[(size_t) a].name);
    }

    refreshSounds();
    refreshKeys();
    resized();
    repaint();
}

void ElementPanel::refreshSounds()
{
    soundIndices = proc.library.soundsFor (kr::slots()[(size_t) slot].category);
    soundBox.clear (juce::dontSendNotification);
    const auto current = proc.getSlotSound (slot);
    for (size_t i = 0; i < soundIndices.size(); ++i)
    {
        const auto& s = proc.library.getSounds()[(size_t) soundIndices[i]];
        soundBox.addItem (s.name + "   (" + proc.library.kitName (s.kit) + ")", (int) i + 1);
        if (s.id == current)
            soundBox.setSelectedItemIndex ((int) i, juce::dontSendNotification);
    }
    if (soundIndices.empty())
        soundBox.setText ("No samples installed", juce::dontSendNotification);

    credit.clear();
    if (const int idx = proc.library.indexOf (current); idx >= 0)
        for (auto& k : proc.library.getKits())
            if (k.id == proc.library.getSounds()[(size_t) idx].kit)
                credit = k.credit + "  -  " + k.license;
}

void ElementPanel::refreshKeys()
{
    if (isSynth())
        return;
    const int learning = proc.getLearnTarget();
    for (int a = 0; a < kr::kMaxArticulations; ++a)
    {
        auto& b = rows[(size_t) a].key;
        const bool isLearning = learning == kr::packTarget (slot, a);
        b.setButtonText (isLearning ? juce::String ("Press a key...") : "Key  " + dd::noteName (proc.getKey (slot, a)));
        b.setToggleState (isLearning, juce::dontSendNotification);
    }
}

void ElementPanel::step (int delta)
{
    const int n = (int) soundIndices.size();
    if (n == 0)
        return;
    const int cur = std::max (0, soundBox.getSelectedItemIndex());
    soundBox.setSelectedItemIndex (((cur + delta) % n + n) % n, juce::sendNotificationSync);
}

void ElementPanel::timerCallback()
{
    proc.pollLearn();
    if (proc.selectedSlot != slot)
        setSlot (proc.selectedSlot);
    if (isSynth())
        return;
    refreshKeys();

    // Keep the combo in sync when a preset changes the kit.
    const auto current = proc.getSlotSound (slot);
    const int sel = soundBox.getSelectedItemIndex();
    if (! juce::isPositiveAndBelow (sel, (int) soundIndices.size())
        || proc.library.getSounds()[(size_t) soundIndices[(size_t) sel]].id != current)
    {
        refreshSounds();
        repaint();
    }
}

void ElementPanel::paint (juce::Graphics& g)
{
    dd::gui::Palette pal;
    if (auto* lf = dynamic_cast<dd::gui::LookAndFeel*> (&getLookAndFeel()))
        pal = lf->palette();

    dd::gui::paintFaceplate (g, getLocalBounds().toFloat(), pal);
    if (slot < 0)
        return;

    const bool loading = ! isSynth() && proc.isSlotLoading (slot);
    dd::gui::paintLamp (g, { 24.0f, 30.0f }, 4.0f, kAccent, loading ? 0.35f : 1.0f);
    const auto title = isLog() ? juce::String ("LOG DRUM") : isSteel() ? juce::String ("STEEL DRUM")
                               : juce::String (kr::slots()[(size_t) slot].name).toUpperCase();
    dd::gui::drawPrinted (g, title, { 36.0f, 12.0f, 220.0f, 34.0f }, pal.text,
                          dd::gui::LookAndFeel::display (26.0f).withExtraKerningFactor (0.05f), juce::Justification::centredLeft);
    const auto keysIn = " (" + dd::noteNamesCaption() + ")";
    dd::gui::drawPrinted (g, (isSynth() ? "PLAYS ITS OWN KEYS" : "SOUNDS  /  KEYS") + keysIn, { 18.0f, 58.0f, 260.0f, 16.0f }, pal.dim,
                          dd::gui::LookAndFeel::font (10.5f, true).withExtraKerningFactor (0.07f), juce::Justification::centredLeft);
    if (isLog())
        dd::gui::drawPrinted (g, dd::noteName (kr::kLogLowKey) + " sounds a deep C, " + dd::noteName (kr::kLogAuditionKey) + " the classic low A (55 Hz).",
                              { 18.0f, 124.0f, 300.0f, 32.0f }, pal.dim, dd::gui::LookAndFeel::font (11.5f), juce::Justification::topLeft);
    if (isSteel())
        dd::gui::drawPrinted (g, "A tenor pan, three octaves below the keys: " + dd::noteName (kr::kSteelLowKey) + " is the pan's C#4. "
                                 "Cello and Bass play an octave and two lower.",
                              { 18.0f, 124.0f, 300.0f, 32.0f }, pal.dim, dd::gui::LookAndFeel::font (11.5f), juce::Justification::topLeft);
    dd::gui::drawPrinted (g, credit, juce::Rectangle<int> (18, getHeight() - 28, getWidth() - 36, 18).toFloat(), pal.dim,
                          dd::gui::LookAndFeel::font (11.5f), juce::Justification::centredLeft);
    if (loading)
        dd::gui::drawPrinted (g, "loading samples...", { (float) getWidth() - 200.0f, 46.0f, 182.0f, 14.0f }, pal.accent2,
                              dd::gui::LookAndFeel::font (12.0f), juce::Justification::centredRight);
}

void ElementPanel::resized()
{
    auto r = getLocalBounds().reduced (18, 12);
    auto top = r.removeFromTop (34);
    top.removeFromLeft (230);
    nextButton.setBounds (top.removeFromRight (30));
    top.removeFromRight (4);
    soundBox.setBounds (top.removeFromRight (std::min (360, top.getWidth() - 40)));
    top.removeFromRight (4);
    prevButton.setBounds (soundBox.getX() - 34, top.getY(), 30, top.getHeight());

    resetKeys.setBounds (18 + 300 - 84, 54, 84, 24);
    r.removeFromTop (30);
    auto left = r.removeFromLeft (300);
    for (int a = 0; a < kr::kMaxArticulations; ++a)
    {
        auto row = left.removeFromTop (34);
        left.removeFromTop (6);
        rows[(size_t) a].play.setBounds (row.removeFromLeft (160));
        row.removeFromLeft (8);
        rows[(size_t) a].key.setBounds (row.removeFromLeft (124));
    }

    if (synthModel != nullptr)
        synthModel->setBounds (soundBox.getX() + soundBox.getWidth() - 200, soundBox.getY() - 6, 200, 50);

    if (clapOnSnare != nullptr)
    {
        const int used = (int) kr::slots()[(size_t) slot].articulations.size();
        clapOnSnare->setBounds (left.getX(), left.getY() - (kr::kMaxArticulations - used) * 40, 160, 44);
    }

    // Knobs: two rows of 4.
    auto grid = r.withTrimmedLeft (std::max (0, r.getWidth() - 4 * kKnobW) / 2);
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (grid.getX() + (int) (i % 4) * kKnobW, grid.getY() + (int) (i / 4) * kKnobH, kKnobW, kKnobH);
}
