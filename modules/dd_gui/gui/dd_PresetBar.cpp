namespace dd::gui
{
PresetBar::PresetBar (dd::PresetManager& manager) : pm (manager)
{
    for (auto* c : { (juce::Component*) &categoryBox, (juce::Component*) &presetBox, (juce::Component*) &prevButton,
                     (juce::Component*) &nextButton, (juce::Component*) &saveButton, (juce::Component*) &menuButton })
        addAndMakeVisible (c);

    categoryBox.setTooltip ("Preset category");
    presetBox.setTooltip ("Preset");
    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    saveButton.setTooltip ("Save the current sound as a user preset");

    categoryBox.onChange = [this]
    {
        const auto cat = categoryBox.getText();
        if (cat != pm.getCurrentCategory())
        {
            const auto idx = pm.getIndicesInCategory (cat);
            if (! idx.isEmpty())
                pm.loadPreset (idx.getFirst());
        }
    };
    presetBox.onChange = [this]
    {
        const int sel = presetBox.getSelectedItemIndex();
        if (juce::isPositiveAndBelow (sel, shownIndices.size()) && shownIndices[sel] != pm.getCurrentIndex())
            pm.loadPreset (shownIndices[sel]);
    };
    prevButton.onClick = [this] { pm.loadRelative (-1); };
    nextButton.onClick = [this] { pm.loadRelative (1); };
    saveButton.onClick = [this] { promptSave(); };
    menuButton.onClick = [this] { showMenu(); };

    pm.addChangeListener (this);
    refresh();
}

PresetBar::~PresetBar() { pm.removeChangeListener (this); }

void PresetBar::refresh()
{
    const auto cats = pm.getCategories();
    categoryBox.clear (juce::dontSendNotification);
    categoryBox.addItemList (cats, 1);

    const auto currentCat = pm.getCurrentCategory();
    const int catIdx = cats.indexOf (currentCat);
    if (catIdx >= 0)
        categoryBox.setSelectedItemIndex (catIdx, juce::dontSendNotification);
    else
        categoryBox.setText ("—", juce::dontSendNotification);

    presetBox.clear (juce::dontSendNotification);
    shownIndices = pm.getIndicesInCategory (currentCat);
    for (int i = 0; i < shownIndices.size(); ++i)
        presetBox.addItem (pm.get (shownIndices[i]).name, i + 1);

    const int sel = shownIndices.indexOf (pm.getCurrentIndex());
    if (sel >= 0)
        presetBox.setSelectedItemIndex (sel, juce::dontSendNotification);
    else
        presetBox.setText (pm.getCurrentName(), juce::dontSendNotification);

    description = pm.getCurrentDescription();
    repaint();
}

void PresetBar::promptSave()
{
    auto* w = new juce::AlertWindow ("Save Preset", "Saved to " + pm.getUserPresetDirectory().getFullPathName(),
                                     juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&getLookAndFeel());
    w->addTextEditor ("name", pm.getCurrentName() + (pm.getCurrentCategory() == dd::PresetManager::kUserCategory ? "" : " (mine)"));
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PresetBar> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (result == 1 && safe != nullptr)
            safe->pm.saveUserPreset (w->getTextEditorContents ("name"));
    }), true);
}

void PresetBar::showMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Init patch");
    m.addItem (2, "Reveal user preset folder");
    m.addItem (3, "Rescan user presets");

    juce::Component::SafePointer<PresetBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton), [safe] (int r)
    {
        if (safe == nullptr) return;
        auto& manager = safe->pm;
        if (r == 1) manager.loadInit();
        else if (r == 2)
        {
            auto dir = manager.getUserPresetDirectory();
            dir.createDirectory();
            dir.revealToUser();
        }
        else if (r == 3) manager.rescanUserPresets();
    });
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (30);
    // Category box shrinks first so the preset name keeps a usable width in small editors.
    categoryBox.setBounds (top.removeFromLeft (juce::jlimit (80, 140, (top.getWidth() - 330) / 2)));
    top.removeFromLeft (6);
    prevButton.setBounds (top.removeFromLeft (30));
    top.removeFromLeft (4);
    menuButton.setBounds (top.removeFromRight (34));
    top.removeFromRight (6);
    saveButton.setBounds (top.removeFromRight (64));
    top.removeFromRight (6);
    nextButton.setBounds (top.removeFromRight (30));
    top.removeFromRight (4);
    presetBox.setBounds (top);
}

void PresetBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    r.removeFromTop (34);
    if (auto* lf = dynamic_cast<LookAndFeel*> (&getLookAndFeel()))
        g.setColour (lf->palette().dim);
    g.setFont (LookAndFeel::font (12.0f));
    g.drawFittedText (description, r, juce::Justification::centredLeft, 1, 0.9f);
}
} // namespace dd::gui
