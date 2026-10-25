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
    g.setColour (pal.background);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (pal.panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto meterArea = r.removeFromRight (14.0f).reduced (4.0f, 6.0f);
    auto wave = r.reduced (6.0f, 6.0f);

    // Centre line
    g.setColour (pal.track);
    g.drawHorizontalLine ((int) wave.getCentreY(), wave.getX(), wave.getRight());

    // Trigger on a rising zero crossing so the waveform stands still.
    const int n = (int) samples.size();
    const int window = n / 2;
    int start = 0;
    for (int i = n - window - 1; i > 1; --i)
        if (samples[(size_t) i - 1] < 0.0f && samples[(size_t) i] >= 0.0f) { start = i; break; }

    float peak = 0.05f;
    for (int i = 0; i < window; ++i)
        peak = std::max (peak, std::abs (samples[(size_t) (start + i)]));
    const float scale = 0.92f / std::max (peak, 0.25f);

    juce::Path p;
    const int pts = (int) wave.getWidth();
    for (int x = 0; x < pts; ++x)
    {
        const float v = samples[(size_t) (start + x * window / std::max (1, pts))] * scale;
        const float y = wave.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * wave.getHeight() * 0.5f;
        if (x == 0) p.startNewSubPath (wave.getX(), y);
        else        p.lineTo (wave.getX() + (float) x, y);
    }
    g.setColour (pal.accent.withAlpha (0.25f));
    g.strokePath (p, juce::PathStrokeType (4.0f));
    g.setColour (pal.accent);
    g.strokePath (p, juce::PathStrokeType (1.6f));

    // Meter: -48 dB .. 0 dB
    auto toY = [&] (float lin)
    {
        const float db = juce::Decibels::gainToDecibels (lin, -48.0f);
        return meterArea.getBottom() - (db + 48.0f) / 48.0f * meterArea.getHeight();
    };
    g.setColour (pal.track);
    g.fillRoundedRectangle (meterArea, 2.0f);
    const float my = toY (meter);
    g.setGradientFill (juce::ColourGradient (pal.accent2, 0.0f, meterArea.getBottom(),
                                             juce::Colour (0xffff2a2a), 0.0f, meterArea.getY(), false));
    g.fillRoundedRectangle (meterArea.withTop (my), 2.0f);
    g.setColour (hold > 0.99f ? juce::Colour (0xffff2a2a) : pal.text);
    g.fillRect (meterArea.getX(), toY (hold) - 1.0f, meterArea.getWidth(), 2.0f);
}
} // namespace dd::gui
