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
    // The console (".console"): warm black glass with a breathing lamp glow and scanlines, an amber trace,
    // and a segmented level meter (".meter").
    auto r = getLocalBounds().toFloat();
    paintConsole (g, r, 12.0f, lampBloom (animationTime()));
    auto glass = r.reduced (4.0f);
    auto meterArea = glass.removeFromRight (12.0f).reduced (2.0f, 8.0f);
    auto screen = glass.reduced (6.0f, 8.0f);

    g.setColour (juce::Colour (0xffe8763a).withAlpha (0.07f));
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine ((int) (screen.getX() + screen.getWidth() * (float) i / 8.0f), screen.getY(), screen.getBottom());
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine ((int) (screen.getY() + screen.getHeight() * (float) i / 4.0f), screen.getX(), screen.getRight());

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

    juce::Path p;
    const int pts = (int) screen.getWidth();
    for (int x = 0; x < pts; ++x)
    {
        const float v = samples[(size_t) (start + x * window / std::max (1, pts))] * scale;
        const float y = screen.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * screen.getHeight() * 0.5f;
        if (x == 0) p.startNewSubPath (screen.getX(), y);
        else        p.lineTo (screen.getX() + (float) x, y);
    }
    const auto stroke = [&] (float w, juce::Colour c)
    {
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    stroke (7.0f, juce::Colour (0xffe8763a).withAlpha (0.12f));
    stroke (3.2f, juce::Colour (0xffe8763a).withAlpha (0.3f));
    stroke (1.4f, juce::Colour (0xffffbe85));

    // Level meter, -48 .. 0 dB: a glowing amber column split into segments, hold tick on top.
    g.setColour (juce::Colour (0xff0a0503));
    g.fillRoundedRectangle (meterArea, 3.0f);
    const float levelDb = juce::Decibels::gainToDecibels (meter, -48.0f);
    const float holdDb = juce::Decibels::gainToDecibels (hold, -48.0f);
    const float lv = juce::jlimit (0.0f, 1.0f, (levelDb + 48.0f) / 48.0f);
    if (lv > 0.0f)
    {
        auto fill = meterArea.withTop (meterArea.getBottom() - meterArea.getHeight() * lv);
        juce::ColourGradient grad (juce::Colour (0xff8a3c12), 0.0f, meterArea.getBottom(), juce::Colour (0xffffc28a), 0.0f, meterArea.getY(), false);
        grad.addColour (0.78, juce::Colour (0xffe0793f));
        g.setColour (juce::Colour (0xffe8763a).withAlpha (0.25f));
        g.fillRoundedRectangle (fill.expanded (2.0f, 0.0f), 3.0f);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 2.0f);
    }
    const float hv = juce::jlimit (0.0f, 1.0f, (holdDb + 48.0f) / 48.0f);
    if (hv > 0.02f)
    {
        g.setColour (juce::Colour (0xffffc28a));
        g.fillRect (meterArea.getX(), meterArea.getBottom() - meterArea.getHeight() * hv - 1.0f, meterArea.getWidth(), 2.0f);
    }
    g.setColour (juce::Colour (0xff0a0503));
    for (float y = meterArea.getBottom() - 4.0f; y > meterArea.getY(); y -= 4.0f)
        g.fillRect (meterArea.getX(), y, meterArea.getWidth(), 1.2f);
}
} // namespace dd::gui
