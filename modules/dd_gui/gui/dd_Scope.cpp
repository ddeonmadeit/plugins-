namespace dd::gui
{
Scope::Scope (dd::ScopeBuffer& source) : buffer (source)
{
    startTimerHz (30);
}

void Scope::timerCallback()
{
    buffer.copyLatest (samples.data(), (int) samples.size());
    const float pk = buffer.takePeak();
    meter = std::max (pk, meter * 0.82f);
    if (pk >= hold) { hold = pk; holdFrames = 45; }
    else if (--holdFrames < 0) hold *= 0.95f;
    repaint();
}

void Scope::paint (juce::Graphics& g)
{
    Palette pal;
    if (auto* lf = dynamic_cast<LookAndFeel*> (&getLookAndFeel()))
        pal = lf->palette();

    auto r = getLocalBounds().toFloat();
    paintInset (g, r, pal, 6.0f);
    auto glass = r.reduced (2.0f);

    auto meterArea = glass.removeFromRight (14.0f).reduced (3.0f, 6.0f);
    auto screen = glass.reduced (5.0f, 5.0f);

    // CRT: warm-black phosphor screen, darker towards the edges, with a printed graticule.
    juce::ColourGradient tube (juce::Colour (0xff221810), screen.getCentreX(), screen.getCentreY(),
                               juce::Colour (0xff0a0705), screen.getX(), screen.getY(), true);
    g.setGradientFill (tube);
    g.fillRoundedRectangle (screen, 5.0f);
    g.setColour (pal.accent2.withAlpha (0.08f));
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine ((int) (screen.getX() + screen.getWidth() * (float) i / 8.0f), screen.getY() + 2.0f, screen.getBottom() - 2.0f);
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine ((int) (screen.getY() + screen.getHeight() * (float) i / 4.0f), screen.getX() + 2.0f, screen.getRight() - 2.0f);
    g.setColour (pal.accent2.withAlpha (0.16f));
    g.drawHorizontalLine ((int) screen.getCentreY(), screen.getX() + 2.0f, screen.getRight() - 2.0f);

    // Trigger on a rising zero crossing so the waveform stands still.
    const int n = (int) samples.size();
    const int window = n / 2;
    int start = 0;
    for (int i = n - window - 1; i > 1; --i)
        if (samples[(size_t) i - 1] < 0.0f && samples[(size_t) i] >= 0.0f) { start = i; break; }

    float peak = 0.05f;
    for (int i = 0; i < window; ++i)
        peak = std::max (peak, std::abs (samples[(size_t) (start + i)]));
    const float scale = 0.9f / std::max (peak, 0.25f);

    auto wave = screen.reduced (3.0f, 4.0f);
    juce::Path p;
    const int pts = (int) wave.getWidth();
    for (int x = 0; x < pts; ++x)
    {
        const float v = samples[(size_t) (start + x * window / std::max (1, pts))] * scale;
        const float y = wave.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * wave.getHeight() * 0.5f;
        if (x == 0) p.startNewSubPath (wave.getX(), y);
        else        p.lineTo (wave.getX() + (float) x, y);
    }
    const auto phosphor = sunset::amber;
    g.setColour (phosphor.withAlpha (0.10f));
    g.strokePath (p, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (phosphor.withAlpha (0.25f));
    g.strokePath (p, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (phosphor.brighter (0.35f));
    g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Glass: a faint reflection across the top.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.05f), screen.getX(), screen.getY(),
                                             juce::Colours::transparentWhite, screen.getX(), screen.getCentreY(), false));
    g.fillRoundedRectangle (screen.withHeight (screen.getHeight() * 0.5f), 5.0f);

    // LED meter, -48 .. 0 dB: yellow, then orange, red at the top. Unlit segments stay visible but dark.
    const int segs = std::max (6, (int) (meterArea.getHeight() / 5.0f));
    const float segH = meterArea.getHeight() / (float) segs;
    const float levelDb = juce::Decibels::gainToDecibels (meter, -48.0f);
    const float holdDb = juce::Decibels::gainToDecibels (hold, -48.0f);
    for (int i = 0; i < segs; ++i)
    {
        const float segDb = -48.0f + 48.0f * ((float) i + 0.5f) / (float) segs;
        const auto col = segDb > -3.0f ? sunset::red : (segDb > -12.0f ? sunset::orange : sunset::yellow);
        const bool on = segDb <= levelDb || std::abs (segDb - holdDb) < 48.0f / (float) segs * 0.5f;
        juce::Rectangle<float> seg (meterArea.getX(), meterArea.getBottom() - segH * (float) (i + 1) + 0.6f,
                                    meterArea.getWidth(), segH - 1.2f);
        g.setColour (on ? col : col.darker (2.4f).withAlpha (0.6f));
        g.fillRoundedRectangle (seg, 1.0f);
    }
}
} // namespace dd::gui
