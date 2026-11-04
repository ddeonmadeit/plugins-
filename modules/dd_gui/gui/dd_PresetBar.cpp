namespace dd::gui
{
//==============================================================================
Key::Key (const juce::String& legend, bool isLit) : juce::Button (legend), label (legend), lit (isLit)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    getProperties().set ("ddLit", isLit);
    getProperties().set ("ddFlickerPeriod", 5.7);
}

void Key::setLabel (const juce::String& legend, bool animate)
{
    if (legend == label)
        return;
    label = legend;
    if (animate && isShowing())
    {
        changedAt = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
    }
    repaint();
}

void Key::setLit (bool shouldBeLit)
{
    lit = shouldBeLit;
    getProperties().set ("ddLit", shouldBeLit);
    repaint();
}

void Key::setFlickerPeriod (double seconds) { getProperties().set ("ddFlickerPeriod", seconds); }

void Key::timerCallback()
{
    if (juce::Time::getMillisecondCounterHiRes() - changedAt > 300.0)
        stopTimer();
    repaint();
}

void Key::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto& t = tokens();
    constexpr float wall = 3.0f;
    const auto r = getLocalBounds().reduced (glowMargin).toFloat().withTrimmedBottom (wall);
    const float corner = std::min (11.0f, r.getHeight() * 0.3f);
    const double period = getProperties().contains ("ddFlickerPeriod") ? (double) getProperties()["ddFlickerPeriod"] : 5.7;
    const float light = lit ? filamentFlicker (animationTime(), period, 0.3) : 1.0f;   // same curve the Atmosphere repaints on
    if (lit)
        paintLitKey (g, r, corner, down, light, wall, (float) glowMargin);
    else
        paintKeycap (g, r, corner, down, wall);

    // Label: uppercase, wide-tracked; a new one rises 6 px into place (".rise").
    const float rise = juce::jlimit (0.0f, 1.0f, (float) ((juce::Time::getMillisecondCounterHiRes() - changedAt) / 260.0));
    auto area = r.reduced (10.0f, 0.0f).translated (0.0f, (down ? wall : 0.0f) + 6.0f * (1.0f - rise));
    const auto f = LookAndFeel::font (label.length() <= 2 ? 15.0f : 11.0f, 700).withExtraKerningFactor (label.length() <= 2 ? 0.0f : 0.11f);
    const auto shown = label.toUpperCase();
    juce::Graphics::ScopedSaveState ss (g);
    g.setOpacity (rise);
    if (lit)
    {
        g.setFont (f);
        g.setColour (juce::Colour (0xffff9652).withAlpha (0.2f * light));
        for (auto [dx, dy] : { std::pair { -1.5f, 0.0f }, { 1.5f, 0.0f }, { 0.0f, -1.5f }, { 0.0f, 1.5f } })
            g.drawFittedText (shown, area.translated (dx, dy).toNearestInt(), juce::Justification::centred, 1, 0.6f);
        g.setColour (juce::Colour (0xffffd9a0).withMultipliedBrightness (0.85f + 0.15f * light));
        g.drawFittedText (shown, area.toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }
    else
    {
        g.setFont (f);
        g.setColour (over ? t.cream100 : t.cream300);
        g.drawFittedText (shown, area.toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }
}

//==============================================================================
PresetBar::PresetBar (dd::PresetManager& manager) : pm (manager)
{
    for (auto* k : { &prevKey, &catKey, &nameKey, &nextKey, &saveKey, &menuKey })
        addAndMakeVisible (k);
    prevKey.setTooltip ("Previous preset");
    nextKey.setTooltip ("Next preset");
    catKey.setTooltip ("Preset category");
    nameKey.setTooltip ("Preset: click for the list");
    saveKey.setTooltip ("Save the current sound as a user preset");
    prevKey.onClick = [this] { pm.loadRelative (-1); };
    nextKey.onClick = [this] { pm.loadRelative (1); };
    catKey.onClick = [this] { showCategories(); };
    nameKey.onClick = [this] { showPresets(); };
    saveKey.onClick = [this] { promptSave(); };
    menuKey.onClick = [this] { showMenu(); };
    pm.addChangeListener (this);
    refresh();
}

PresetBar::~PresetBar() { pm.removeChangeListener (this); }

void PresetBar::refresh()
{
    const auto cat = pm.getCurrentCategory();
    catKey.setLabel (cat.isEmpty() ? juce::String ("-") : cat, false);
    nameKey.setLabel (pm.getCurrentName());
    description = pm.getCurrentDescription();
    if (onPresetChanged)
        onPresetChanged();
    repaint();
}

void PresetBar::showCategories()
{
    juce::PopupMenu m;
    const auto cats = pm.getCategories();
    for (int i = 0; i < cats.size(); ++i)
        m.addItem (i + 1, cats[i], true, cats[i] == pm.getCurrentCategory());
    juce::Component::SafePointer<PresetBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&catKey), [safe, cats] (int r)
    {
        if (safe == nullptr || r <= 0)
            return;
        const auto idx = safe->pm.getIndicesInCategory (cats[r - 1]);
        if (! idx.isEmpty())
            safe->pm.loadPreset (idx.getFirst());
    });
}

void PresetBar::showPresets()
{
    juce::PopupMenu m;
    const auto cats = pm.getCategories();
    const auto current = pm.getCurrentCategory();
    // The current category's presets first; every other category one level down.
    for (int idx : pm.getIndicesInCategory (current))
        m.addItem (idx + 1, pm.get (idx).name, true, idx == pm.getCurrentIndex());
    m.addSeparator();
    for (auto& c : cats)
    {
        if (c == current)
            continue;
        juce::PopupMenu sub;
        for (int idx : pm.getIndicesInCategory (c))
            sub.addItem (idx + 1, pm.get (idx).name, true, idx == pm.getCurrentIndex());
        m.addSubMenu (c, sub);
    }
    juce::Component::SafePointer<PresetBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameKey), [safe] (int r)
    {
        if (safe != nullptr && r > 0)
            safe->pm.loadPreset (r - 1);
    });
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
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuKey), [safe] (int r)
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
    auto r = getLocalBounds().reduced (7, 6);
    const int small = r.getHeight();
    prevKey.setBounds (r.removeFromLeft (small));
    r.removeFromLeft (5);
    menuKey.setBounds (r.removeFromRight (small + 4));
    r.removeFromRight (5);
    saveKey.setBounds (r.removeFromRight (64));
    r.removeFromRight (5);
    nextKey.setBounds (r.removeFromRight (small));
    r.removeFromRight (5);
    catKey.setBounds (r.removeFromLeft (juce::jlimit (90, 150, r.getWidth() / 3)));
    r.removeFromLeft (5);
    // The lit key overlaps its neighbours' gaps so its glow isn't cut off; it sits behind them.
    nameKey.setGlowMargin (5);
    nameKey.setBounds (r.expanded (5));
    nameKey.toBack();
}

void PresetBar::paint (juce::Graphics& g)
{
    paintTray (g, getLocalBounds().toFloat(), 14.0f, 0.7f);
}

//==============================================================================
TopBar::TopBar (dd::PresetManager& manager, const juce::String& b, const juce::String& ba, const juce::String& sub)
    : brand (b), brandAccent (ba), subtitle (sub), bar (manager)
{
    addAndMakeVisible (bar);
    addAndMakeVisible (themeSwitch);
    getProperties().set ("ddRail", true);
}

void TopBar::addRight (juce::Component& c, int width)
{
    addAndMakeVisible (c);
    right.push_back ({ &c, width });
    resized();
}

void TopBar::paint (juce::Graphics& g)
{
    const auto& t = tokens();
    const auto r = getLocalBounds().toFloat();
    juce::Path rail;
    rail.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 22.0f, 22.0f, true, true, false, false);
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (rail);
    juce::ColourGradient grad (t.railTop, 0.0f, r.getY(), t.railBot, 0.0f, r.getBottom(), false);
    grad.addColour (0.55, t.railMid);
    g.setGradientFill (grad);
    g.fillRect (r);
    g.setColour (t.light ? juce::Colours::white.withAlpha (0.9f) : juce::Colour ((juce::uint8) 255, (juce::uint8) 248, (juce::uint8) 235, 0.12f));
    g.strokePath (rail, juce::PathStrokeType (2.0f), juce::AffineTransform::translation (0.0f, 1.0f));
    g.setColour (t.edgeDark);
    g.fillRect (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);

    // Brand plate: heavy uppercase, the second half in the signal colour (".brand-name" / ".brand-accent").
    const auto f = LookAndFeel::font (15.0f, 700).withExtraKerningFactor (0.01f);
    auto plate = r.withTrimmedLeft (26.0f).withWidth (260.0f);
    auto line = plate.withSizeKeepingCentre (plate.getWidth(), 20.0f).translated (0.0f, -6.0f);
    const float w1 = juce::GlyphArrangement::getStringWidth (f, brand);
    g.setFont (f);
    for (auto [dy, c] : { std::pair { 1.0f, t.edgeLight }, { -1.0f, t.edgeDark } })
    {
        g.setColour (c);
        g.drawText (brand + brandAccent, line.translated (0.0f, dy), juce::Justification::centredLeft, false);
    }
    g.setColour (t.cream100);
    g.drawText (brand, line, juce::Justification::centredLeft, false);
    g.setColour (t.signalGlow);
    g.drawText (brandAccent, line.withTrimmedLeft (w1), juce::Justification::centredLeft, false);
    g.setFont (LookAndFeel::font (8.5f, 700).withExtraKerningFactor (0.14f));
    g.setColour (t.cream500);
    g.drawText (subtitle.toUpperCase(), line.translated (0.0f, 17.0f), juce::Justification::centredLeft, false);
}

void TopBar::resized()
{
    auto r = getLocalBounds();
    auto rightArea = r.withTrimmedRight (22);
    themeSwitch.setBounds (rightArea.removeFromRight (84).withSizeKeepingCentre (84, 26));
    int rightEdge = rightArea.getRight() - 18;
    for (auto& [c, w] : right)
    {
        c->setBounds (rightEdge - w, r.getCentreY() - 15, w, 30);
        rightEdge -= w + 14;
    }
    // The brand plate's measured width, so narrow brands leave more room for the preset keys.
    const float brandW = juce::GlyphArrangement::getStringWidth (LookAndFeel::font (15.0f, 700).withExtraKerningFactor (0.01f), brand + brandAccent);
    const float subW = juce::GlyphArrangement::getStringWidth (LookAndFeel::font (8.5f, 700).withExtraKerningFactor (0.14f), subtitle.toUpperCase());
    const int left = 26 + (int) std::ceil (std::max (brandW, subW)) + 28;
    const int avail = rightEdge - left;
    const int w = std::min (640, avail);
    const int x = juce::jlimit (left, rightEdge - w, getWidth() / 2 - w / 2);
    bar.setBounds (x, (getHeight() - 52) / 2, w, 52);
}

//==============================================================================
DescriptionLine::DescriptionLine (PresetBar& b) : bar (b)
{
    text = bar.getDescription();
    bar.onPresetChanged = [this]
    {
        text = bar.getDescription();
        changedAt = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
        repaint();
    };
    setInterceptsMouseClicks (false, false);
}

void DescriptionLine::timerCallback()
{
    if (juce::Time::getMillisecondCounterHiRes() - changedAt > 300.0)
        stopTimer();
    repaint();
}

void DescriptionLine::paint (juce::Graphics& g)
{
    const auto& t = tokens();
    const float rise = juce::jlimit (0.0f, 1.0f, (float) ((juce::Time::getMillisecondCounterHiRes() - changedAt) / 280.0));
    auto r = getLocalBounds().toFloat();
    if (hint.isNotEmpty())
    {
        const auto f = LookAndFeel::font (9.0f, 700).withExtraKerningFactor (0.12f);
        const auto shown = hint.toUpperCase();
        g.setFont (f);
        g.setColour (t.cream500);
        g.drawText (shown, r.removeFromRight (juce::GlyphArrangement::getStringWidth (f, shown) + 4.0f), juce::Justification::centredRight, false);
        r.removeFromRight (16.0f);
    }
    g.setOpacity (rise);
    g.setFont (LookAndFeel::font (12.5f, 400));
    g.setColour (t.cream300);
    g.drawFittedText (text, r.translated (0.0f, 6.0f * (1.0f - rise)).toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
}
} // namespace dd::gui
