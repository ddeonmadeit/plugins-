namespace dd::gui
{
namespace
{
    void drawTitle (juce::Graphics& g, const juce::String& title, juce::Rectangle<int> r, float scale = 1.0f)
    {
        const auto f = LookAndFeel::font (9.5f * scale, 700).withExtraKerningFactor (0.1f);
        const auto text = title.toUpperCase();
        // Squeeze long captions instead of truncating them.
        const float w = juce::GlyphArrangement::getStringWidth (f, text);
        const float squeeze = w > (float) r.getWidth() - 2.0f ? ((float) r.getWidth() - 2.0f) / w : 1.0f;
        g.setFont (f.withHorizontalScale (squeeze));
        g.setColour (tokens().cream500);
        g.drawText (text, r, juce::Justification::centred, false);
    }

    const juce::Identifier kRaised ("ddRaised"), kLit ("ddLit"), kFlicker ("ddFlicker"), kPeriod ("ddFlickerPeriod");
}

//==============================================================================
namespace
{
    const juce::Identifier kParamProp ("ddParam"), kTitleProp ("ddTitle"), kHelpProp ("ddHelp");
}

void tagParam (juce::Component& c, const juce::String& paramId, const juce::String& title)
{
    c.getProperties().set (kParamProp, paramId);
    c.getProperties().set (kTitleProp, title);
}

void setHelp (juce::Component& c, const juce::String& title, const juce::String& text)
{
    c.getProperties().set (kTitleProp, title);
    c.getProperties().set (kHelpProp, text);
}

namespace
{
    juce::Array<HoverHelp*>& hoverHelpers()
    {
        static juce::Array<HoverHelp*> all;   // message thread only
        return all;
    }
}

HoverHelp::HoverHelp (juce::Component& insideEditor) : owner (insideEditor)
{
    juce::Desktop::getInstance().addGlobalMouseListener (this);
    hoverHelpers().add (this);
}

HoverHelp::~HoverHelp()
{
    hoverHelpers().removeFirstMatchingValue (this);
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
}

void HoverHelp::simulateHover (juce::Component& c)
{
    const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), c.getLocalBounds().getCentre().toFloat(),
                              juce::ModifierKeys(), juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f,
                              &c, &c, juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 0, false);
    for (auto* h : hoverHelpers())
        h->update (e, false);
}

void HoverHelp::update (const juce::MouseEvent& e, bool leaving)
{
    juce::String newTitle, newText;
    auto* c = e.eventComponent;
    // Only this editor's window: every open plugin window shares the desktop's mouse.
    const bool ours = c != nullptr && owner.getTopLevelComponent() != nullptr && c->getTopLevelComponent() == owner.getTopLevelComponent();
    if (ours && ! leaving)
    {
        for (auto* p = c; p != nullptr; p = p->getParentComponent())
        {
            const auto& props = p->getProperties();
            if (props.contains (kHelpProp))
            {
                newTitle = props[kTitleProp].toString();
                newText = props[kHelpProp].toString();
                break;
            }
            if (props.contains (kParamProp))
            {
                if (auto it = table.find (props[kParamProp].toString()); it != table.end())
                {
                    newTitle = props[kTitleProp].toString();
                    newText = it->second;
                }
                break;
            }
        }
    }
    else if (! ours && text.isEmpty())
        return;

    if (newText != text || newTitle != title)
    {
        title = newTitle;
        text = newText;
        if (onChange)
            onChange();
    }
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& t, juce::Colour accent)
    : title (t)
{
    tagParam (*this, paramId, t);
    param = state.getParameter (paramId);
    jassert (param != nullptr);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    if (! accent.isTransparent())
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { valueChanged(); };
    slider.setTooltip (param->getName (64));
    shown = (float) slider.valueToProportionOfLength (slider.getValue());
    slider.getProperties().set ("ddShownPos", shown);
    updateValueText();
}

void Knob::valueChanged()
{
    updateValueText();
    const float target = (float) slider.valueToProportionOfLength (slider.getValue());
    if (slider.isMouseButtonDown() || ! isShowing())
    {
        // Under the hand the knob follows exactly; nobody to animate for when hidden.
        shown = target;
        stopTimer();
    }
    else if (! isTimerRunning())
        startTimerHz (60);
    slider.getProperties().set ("ddShownPos", shown);
    slider.repaint();
    repaint();
}

void Knob::timerCallback()
{
    // Ease toward the value (about 60 ms time constant): a preset change sweeps, automation glides.
    const float target = (float) slider.valueToProportionOfLength (slider.getValue());
    shown += (target - shown) * 0.24f;
    if (std::abs (target - shown) < 0.002f)
    {
        shown = target;
        stopTimer();
    }
    slider.getProperties().set ("ddShownPos", shown);
    slider.repaint();
}

void Knob::updateValueText()
{
    valueText = param != nullptr ? param->getCurrentValueAsText() : juce::String();
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (juce::roundToInt (16.0f * labelScale));
    r.removeFromBottom (juce::roundToInt (18.0f * labelScale));
    const int size = std::min (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (size, size));
}

void Knob::paint (juce::Graphics& g)
{
    const auto& t = tokens();
    auto r = getLocalBounds();
    drawTitle (g, title, r.removeFromTop (juce::roundToInt (16.0f * labelScale)), labelScale);
    g.setFont (LookAndFeel::font (11.0f * labelScale, 500));
    g.setColour (slider.isMouseOverOrDragging() ? t.cream100 : t.cream300);
    g.drawFittedText (valueText, r.removeFromBottom (juce::roundToInt (18.0f * labelScale)), juce::Justification::centred, 1, 0.7f);
}

//==============================================================================
Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& t)
    : title (t)
{
    tagParam (*this, paramId, t);
    auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId));
    jassert (p != nullptr);
    if (p != nullptr)
        box.addItemList (p->choices, 1);
    addAndMakeVisible (box);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, box);
}

void Choice::resized()
{
    auto r = getLocalBounds().reduced (1, 0);
    r.removeFromTop (16);
    box.setBounds (r.withSizeKeepingCentre (r.getWidth(), 26).translated (0, -6));
}

void Choice::paint (juce::Graphics& g)
{
    drawTitle (g, title, getLocalBounds().removeFromTop (16));
}

//==============================================================================
void SwitchAnimation::set (bool on, bool animate)
{
    target = on ? 1.0f : 0.0f;
    if (! animate)
    {
        pos = target;
        stopTimer();
        comp.repaint();
        return;
    }
    from = pos;
    started = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

void SwitchAnimation::timerCallback()
{
    // 220 ms with an ease close to cubic-bezier(.45, 0, .2, 1).
    const float x = juce::jlimit (0.0f, 1.0f, (float) ((juce::Time::getMillisecondCounterHiRes() - started) / 220.0));
    const float e = x < 0.5f ? 4.0f * x * x * x : 1.0f - std::pow (-2.0f * x + 2.0f, 3.0f) * 0.5f;
    pos = from + (target - from) * e;
    if (x >= 1.0f)
    {
        pos = target;
        stopTimer();
    }
    comp.repaint();
}

//==============================================================================
Toggle::Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& t,
                const juce::String& onT, const juce::String& offT)
    : attachment (*state.getParameter (paramId), [this] (float v)
      {
          const bool now = v >= 0.5f;
          anim.set (now, now != on && isShowing());
          on = now;
          repaint();
      }),
      title (t), onText (onT), offText (offT)
{
    tagParam (*this, paramId, t);
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Toggle::paint (juce::Graphics& g)
{
    const auto& t = tokens();
    auto r = getLocalBounds();
    drawTitle (g, title, r.removeFromTop (16));
    // Switch and state word centred together; the word shrinks to fit narrow cells.
    auto f = LookAndFeel::font (11.0f, 600).withExtraKerningFactor (0.04f);
    auto wordWidth = [&] { return std::max (juce::GlyphArrangement::getStringWidth (f, onText.toUpperCase()),
                                            juce::GlyphArrangement::getStringWidth (f, offText.toUpperCase())); };
    const int room = r.getWidth() - 34 - 6;
    if (wordWidth() > (float) room)
        f = LookAndFeel::font (juce::jmax (8.0f, 11.0f * (float) room / wordWidth()), 600);
    const int total = juce::jmin (r.getWidth(), 34 + 6 + (int) std::ceil (wordWidth()));
    auto row = r.withSizeKeepingCentre (total, 20).translated (0, -6);
    paintSwitch (g, row.removeFromLeft (34).withSizeKeepingCentre (34, 18).toFloat(), anim.position(), on);
    row.removeFromLeft (6);
    g.setFont (f);
    g.setColour (on ? t.signal : t.faint);
    g.drawText ((on ? onText : offText).toUpperCase(), row.withRight (getWidth()), juce::Justification::centredLeft, false);
}

void Toggle::mouseDown (const juce::MouseEvent&) { attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

//==============================================================================
LampSwitch::LampSwitch (juce::RangedAudioParameter& p, const juce::String& l, juce::Colour)
    : attachment (p, [this] (float v)
      {
          const bool now = v >= 0.5f;
          anim.set (now, now != on && isShowing());
          on = now;
          repaint();
      }),
      label (l)
{
    if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (&p))
        tagParam (*this, withId->paramID, l);
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LampSwitch::paint (juce::Graphics& g)
{
    const auto& t = tokens();
    auto r = getLocalBounds().toFloat();
    const auto shown = label.toUpperCase();
    // In a narrow panel cell: caption on top like a knob's, the switch centred under it.
    const auto lineFont = LookAndFeel::font (11.5f, 600).withExtraKerningFactor (0.06f);
    if (r.getWidth() < 46.0f + juce::GlyphArrangement::getStringWidth (lineFont, shown) && r.getHeight() >= 44.0f)
    {
        g.setFont (LookAndFeel::font (9.5f, 700).withExtraKerningFactor (0.1f));
        g.setColour (on ? t.signal : t.cream500);
        g.drawText (shown, r.removeFromTop (16.0f), juce::Justification::centred, false);
        paintSwitch (g, juce::Rectangle<float> (34.0f, 18.0f).withCentre ({ r.getCentreX(), r.getCentreY() - 6.0f }), anim.position(), on);
        return;
    }
    paintSwitch (g, juce::Rectangle<float> (r.getX() + 4.0f, r.getCentreY() - 9.0f, 34.0f, 18.0f), anim.position(), on);
    const auto area = r.withTrimmedLeft (46.0f);
    auto f = LookAndFeel::font (11.5f, 600).withExtraKerningFactor (0.06f);
    const float w = juce::GlyphArrangement::getStringWidth (f, shown);
    if (w > area.getWidth())
        f = LookAndFeel::font (juce::jmax (8.0f, 11.5f * area.getWidth() / w), 600).withExtraKerningFactor (0.02f);
    g.setFont (f);
    g.setColour (on ? t.signal : (isMouseOver() ? t.cream300 : t.cream500));
    g.drawText (shown, area, juce::Justification::centredLeft, false);
}

void LampSwitch::mouseDown (const juce::MouseEvent&) { attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

//==============================================================================
TapeStopControl::TapeStopControl (juce::AudioProcessorValueTreeState& state, const juce::String& switchId, const juce::String& timeId)
    : lamp (*state.getParameter (switchId), "Tape Stop", sunset::red)
{
    const auto howToAutomate = dd::host().ableton
        ? "In Ableton, click it once so it appears in the track's automation chooser, then draw it in Arrangement View where the beat should stop."
        : "In FL Studio, right-click it > Create automation clip to drop it at the end of a beat.";
    lamp.setTooltip (juce::String ("Tape stop: the sound slows down to a halt like a tape machine powering off, and stays silent "
                                   "until you switch it off again. ") + howToAutomate);
    addAndMakeVisible (lamp);

    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (timeId)))
        time.addItemList (p->choices, 1);
    time.setTooltip ("How long the tape takes to stop");
    addAndMakeVisible (time);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, timeId, time);
}

void TapeStopControl::resized()
{
    auto r = getLocalBounds();
    if (r.getHeight() >= 44)
    {
        lamp.setBounds (r.removeFromTop (r.getHeight() / 2));
        time.setBounds (r.withSizeKeepingCentre (r.getWidth(), 24));
    }
    else
    {
        const int w = r.getWidth() * 2 / 5;
        time.setBounds (r.removeFromRight (w).withSizeKeepingCentre (w, 24));
        lamp.setBounds (r);
    }
}

//==============================================================================
ThemeSwitch::ThemeSwitch()
{
    addThemeListener (this);
    anim.set (! isLightTheme(), false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("Dark or light (every plugin follows)");
}

ThemeSwitch::~ThemeSwitch() { removeThemeListener (this); }

void ThemeSwitch::paint (juce::Graphics& g)
{
    const auto& t = tokens();
    auto r = getLocalBounds().toFloat();
    const auto sw = r.removeFromRight (44.0f).withSizeKeepingCentre (44.0f, 23.0f);
    // The switch is "on" in dark mode: knob thrown right, LED lit (as in rawleads).
    paintSwitch (g, sw, anim.position(), ! isLightTheme());
    r.removeFromRight (9.0f);
    g.setFont (LookAndFeel::font (8.8f, 700).withExtraKerningFactor (0.12f));
    g.setColour (t.cream500);
    g.drawText (isLightTheme() ? "LITE" : "DARK", r, juce::Justification::centredRight, false);
}

void ThemeSwitch::mouseDown (const juce::MouseEvent&) { setLightTheme (! isLightTheme()); }

//==============================================================================
Panel::Panel (const juce::String& t, int c, int r, juce::Colour a)
    : title (t), cols (c), rows (r), accent (a)
{
    getProperties().set (kRaised, 14.0f);
}

void Panel::place (int col, int row, int colSpan, int rowSpan)
{
    jassert (! items.empty());
    auto& it = items.back();
    it.col = col; it.row = row; it.colSpan = colSpan; it.rowSpan = rowSpan;
    next = row * cols + col + colSpan;
}

void Panel::paint (juce::Graphics& g)
{
    paintCached (g, getProperties(), "ddFace", getLocalBounds(), title.hashCode64(), [this] (juce::Graphics& ig)
    {
        paintBrushedMetal (ig, getLocalBounds().toFloat(), {}, 14.0f);
        drawCaption (ig, title, getLocalBounds().removeFromTop (kTitleH + 2).withTrimmedLeft (16).withTrimmedRight (16).toFloat().translated (0.0f, 2.0f));
    });
}

void Panel::resized()
{
    // Centre the grid when the panel is stretched wider than its preferred size.
    const int x0 = std::max (kPad, (getWidth() - cols * kCellW) / 2);
    for (auto& it : items)
        it.comp->setBounds (x0 + it.col * kCellW, kTitleH + it.row * kCellH, it.colSpan * kCellW, it.rowSpan * kCellH);
}

void markRaised (juce::Component& c, float corner) { c.getProperties().set (kRaised, corner); }

void paintPanelShadows (juce::Graphics& g, juce::Component& parent)
{
    // Painted once per layout and theme (the key is every raised child's place), then copied.
    juce::int64 key = 17;
    for (auto* c : parent.getChildren())
        if (c->isVisible() && (c->getProperties().contains (kRaised) || (bool) c->getProperties()["ddRail"]))
        {
            const auto b = c->getBounds();
            for (auto v : { b.getX(), b.getY(), b.getWidth(), b.getHeight() })
                key = key * 31 + v;
        }
    paintCached (g, parent.getProperties(), "ddShadows", parent.getLocalBounds(), key, [&parent] (juce::Graphics& ig)
    {
        paintPanelShadowsDirect (ig, parent);
    });
}

void paintPanelShadowsDirect (juce::Graphics& g, juce::Component& parent)
{
    for (auto* c : parent.getChildren())
    {
        if (! c->isVisible())
            continue;
        if (c->getProperties().contains (kRaised))
            paintRaisedShadow (g, c->getBounds().toFloat(), (float) c->getProperties()[kRaised]);
        if ((bool) c->getProperties()["ddRail"])
        {
            // The hard shadow line the raised rail casts on the interior below it.
            const auto b = c->getBounds().toFloat();
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (tokens().light ? 0.12f : 0.45f), 0.0f, b.getBottom(),
                                                     juce::Colours::transparentBlack, 0.0f, b.getBottom() + 8.0f, false));
            g.fillRect (b.getX(), b.getBottom(), b.getWidth(), 8.0f);
        }
    }
}

//==============================================================================
Atmosphere::Atmosphere()
{
    setInterceptsMouseClicks (false, false);
    juce::Desktop::getInstance().addGlobalMouseListener (this);
    addThemeListener (this);
    startTimerHz (30);
}

Atmosphere::~Atmosphere()
{
    removeThemeListener (this);
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
}

void Atmosphere::parentHierarchyChanged()
{
    if (auto* p = getParentComponent())
        setBounds (p->getLocalBounds());
}

void Atmosphere::paint (juce::Graphics& g)
{
    const auto& t = tokens();
    const auto r = getLocalBounds().toFloat();
    const double now = juce::Time::getMillisecondCounterHiRes();

    // The vignette and the grain only change with the size and the theme: painted once, then copied.
    paintCached (g, getProperties(), "ddOverlay", getLocalBounds(), 0, [this] (juce::Graphics& ig) { paintOverlay (ig); });

    // The pointer's shadow: it floats above the machine.
    if (cursorInside)
    {
        juce::ColourGradient cs (juce::Colours::black.withAlpha (0.3f), cursor.x + 1.5f, cursor.y + 2.5f, juce::Colours::transparentBlack, cursor.x + 1.5f + 15.0f, cursor.y + 2.5f, true);
        cs.addColour (0.42, juce::Colours::black.withAlpha (0.14f));
        cs.addColour (0.7, juce::Colours::transparentBlack);
        g.setGradientFill (cs);
        g.fillEllipse (cursor.x + 1.5f - 15.0f, cursor.y + 2.5f - 15.0f, 30.0f, 30.0f);
    }

    // Theme change: the old look fades out over 280 ms.
    if (fadeFrom.isValid())
    {
        const float p = (float) ((now - fadeStart) / 280.0);
        if (p < 1.0f)
        {
            g.setOpacity (1.0f - p);
            g.drawImage (fadeFrom, r, juce::RectanglePlacement::stretchToFit);
            g.setOpacity (1.0f);
        }
    }

    // Opening: the instrument rises into view (only on screen; offscreen snapshots show it settled).
    if (openedAt <= 0.0 && isShowing())
        openedAt = now;
    const float open = openedAt > 0.0 ? (float) ((now - openedAt) / 340.0) : 1.0f;
    if (open < 1.0f)
    {
        g.setColour (t.room.withAlpha (1.0f - open * open * (3.0f - 2.0f * open)));
        g.fillRect (r);
    }
}

void Atmosphere::paintOverlay (juce::Graphics& g)
{
    const auto& t = tokens();
    const auto r = getLocalBounds().toFloat();

    // Vignette: the room falls into shadow at the edges (light theme: soft warm grey).
    {
        juce::Graphics::ScopedSaveState ss (g);
        const juce::Point<float> c (r.getCentreX(), r.getY() + r.getHeight() * 0.4f);
        g.addTransform (juce::AffineTransform::scale (1.0f, 0.84f / 0.96f, c.x, c.y));
        const float rad = r.getWidth() * 0.6f;
        juce::ColourGradient v (juce::Colours::transparentBlack, c.x, c.y,
                                t.light ? juce::Colour ((juce::uint8) 120, (juce::uint8) 108, (juce::uint8) 90, 0.16f) : juce::Colours::black.withAlpha (0.6f),
                                c.x + rad, c.y, true);
        v.addColour (t.light ? 0.62 : 0.56, juce::Colours::transparentBlack);
        v.addColour (t.light ? 0.88 : 0.82, t.light ? juce::Colour ((juce::uint8) 120, (juce::uint8) 108, (juce::uint8) 90, 0.08f) : juce::Colours::black.withAlpha (0.3f));
        g.setGradientFill (v);
        g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
        g.setColour (t.light ? juce::Colour ((juce::uint8) 120, (juce::uint8) 108, (juce::uint8) 90, 0.16f) : juce::Colours::black.withAlpha (0.6f));
        juce::Path outside;
        outside.addRectangle (r.expanded (r.getHeight()));
        outside.addEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
        outside.setUsingNonZeroWinding (false);
        g.fillPath (outside);
    }

    // Matte grain over everything.
    paintGrain (g, getLocalBounds(), 0.11f);
}

void Atmosphere::collectLit (juce::Component& c)
{
    for (auto* child : c.getChildren())
    {
        if (child == this || ! child->isShowing())
            continue;
        const bool litButton = [&]
        {
            if (auto* b = dynamic_cast<juce::Button*> (child))
                return b->getToggleState();
            return false;
        }();
        if (litButton || (bool) child->getProperties()[kLit])
            lit.push_back (child);
        collectLit (*child);
    }
}

void Atmosphere::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (fadeFrom.isValid() && now - fadeStart > 300.0)
        fadeFrom = {};
    if (fadeFrom.isValid() || (openedAt > 0.0 && now - openedAt < 400.0))
        repaint();

    // Lit keys flicker like filaments: repaint one only when its light actually changes.
    if (--litScanCountdown <= 0)
    {
        litScanCountdown = 15;
        lit.clear();
        if (auto* p = getParentComponent())
            collectLit (*p);
    }
    const double secs = now * 0.001;
    for (auto& sp : lit)
        if (auto* c = sp.getComponent())
        {
            auto& props = c->getProperties();
            const double period = props.contains (kPeriod) ? (double) props[kPeriod] : 4.8;
            const float f = filamentFlicker (secs, period, 0.3);
            if (std::abs (f - (float) (double) props.getWithDefault (kFlicker, 1.0)) > 0.02f)
            {
                props.set (kFlicker, f);
                c->repaint();
            }
        }
}

void Atmosphere::mouseMove (const juce::MouseEvent& e)
{
    if (! isShowing())
        return;
    const auto p = getLocalPoint (nullptr, e.getScreenPosition()).toFloat();
    const bool inside = getLocalBounds().toFloat().contains (p);
    if (inside != cursorInside || inside)
    {
        repaint (juce::Rectangle<int> ((int) cursor.x - 20, (int) cursor.y - 20, 44, 46));
        cursor = p;
        cursorInside = inside;
        repaint (juce::Rectangle<int> ((int) cursor.x - 20, (int) cursor.y - 20, 44, 46));
    }
}

void Atmosphere::mouseExit (const juce::MouseEvent& e) { mouseMove (e); }

void Atmosphere::themeAboutToChange()
{
    if (auto* p = getParentComponent(); p != nullptr && p->isShowing())
        fadeFrom = p->createComponentSnapshot (p->getLocalBounds(), true, 1.0f);
}

void Atmosphere::themeChanged()
{
    fadeStart = juce::Time::getMillisecondCounterHiRes();
    if (auto* p = getParentComponent())
        p->repaint();
}

//==============================================================================
void giveKeysOnlyTo (juce::Component& root, juce::Component& target)
{
    for (auto* c : root.getChildren())
    {
        if (c == &target)
            continue;
        c->setWantsKeyboardFocus (false);
        giveKeysOnlyTo (*c, target);
    }
}

juce::Rectangle<int> screenArea()
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    if (const auto* d = displays.getDisplayForPoint (juce::Desktop::getMousePosition().toFloat()))
        return d->userBounds.toNearestInt();
    if (const auto* d = displays.getPrimaryDisplay())
        return d->userBounds.toNearestInt();
    return { 0, 0, 1440, 875 };
}

float openingScale (int baseW, int baseH, float saved)
{
    const auto area = screenArea();
    const float fit = std::min (0.8f * (float) area.getHeight() / (float) baseH, 0.9f * (float) area.getWidth() / (float) baseW);
    return juce::jlimit (0.5f, 1.8f, std::min (saved > 0.0f ? saved : 1.0f, fit));
}
} // namespace dd::gui
