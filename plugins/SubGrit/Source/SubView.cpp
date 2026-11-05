#include "SubView.h"

namespace
{
const juce::Colour kCream { 0xffeadcc3 };
const juce::Colour kChrome { 0xffd9d0c4 };
constexpr float kPi = juce::MathConstants<float>::pi;

juce::String noteName (int n)
{
    // FL Studio naming: MIDI 60 = C5.
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n < 0 || n > 127 ? juce::String ("--") : juce::String (names[n % 12]) + juce::String (n / 12);
}
} // namespace

SubView::SubView (SubGritProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (60);
}

void SubView::resized()
{
    const auto b = getLocalBounds().toFloat();
    const float h = b.getHeight();
    const float cw = h * 0.78f, ch = h * 0.8f;
    cabinet = { b.getWidth() * 0.27f - cw * 0.5f, h * 0.88f - ch, cw, ch };
    wooferR = cw * 0.36f;
    wooferCentre = { cabinet.getCentreX(), cabinet.getY() + cabinet.getHeight() * 0.42f };
}

void SubView::timerCallback()
{
    proc.scope.copyLatest (samples.data(), (int) samples.size());
    double sum = 0.0;
    for (size_t i = samples.size() / 2; i < samples.size(); ++i)
        sum += (double) samples[i] * samples[i];
    const float rms = (float) std::sqrt (sum / (double) (samples.size() / 2));
    const float target = juce::jlimit (0.0f, 1.0f, rms * 2.6f);
    prevEnv = env;
    env += (target - env) * (target > env ? 0.55f : 0.12f);

    // The cone throbs at a visible rate that follows the pitch (a low note pumps slower than a high one).
    const int n = proc.lastNote.load (std::memory_order_relaxed);
    const bool noteChanged = n != note;
    note = n;
    float hz = 50.0f;
    if (note >= 0)
    {
        const float shift = proc.apvts.getRawParameterValue ("octave")->load() * 12.0f + proc.apvts.getRawParameterValue ("tune")->load();
        hz = 440.0f * std::exp2 (((float) note + shift - 69.0f) / 12.0f);
    }
    phase = std::fmod (phase + kPi * 2.0f * juce::jlimit (2.0f, 11.0f, hz / 6.0f) / 60.0f, kPi * 2.0f);
    excursion = env * (0.6f + 0.4f * std::sin (phase));
    lamp += (env - lamp) * 0.25f;

    // A hard hit sends a pressure ring off the cone.
    if (env - prevEnv > 0.1f)
        for (auto& r : rings)
            if (r.age >= 1.0f)
            {
                r = { 0.0f, env };
                break;
            }
    bool ringsMoving = false;
    for (auto& r : rings)
        if (r.age < 1.0f)
        {
            r.age = std::min (1.0f, r.age + 1.0f / 50.0f);
            ringsMoving = true;
        }

    if (env > 0.002f || prevEnv > 0.002f || ringsMoving || noteChanged)
        repaint();
}

void SubView::drawRoom (juce::Graphics& g) const
{
    const auto b = getLocalBounds().toFloat();
    const float floorY = b.getHeight() * 0.88f;

    // Back wall: dark walnut panelling; the floor falls away darker.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c130c), 0.0f, 0.0f, juce::Colour (0xff0d0805), 0.0f, floorY, false));
    g.fillRect (b.withBottom (floorY));
    juce::Random rnd (808);
    for (float x = 0.0f; x < b.getWidth(); x += 38.0f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (x, 0.0f, 1.5f, floorY);
        g.setColour (juce::Colour (0xffd8b48a).withAlpha (0.025f + 0.02f * rnd.nextFloat()));
        g.fillRect (x + 1.5f, 0.0f, 1.0f, floorY);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff160e08), 0.0f, floorY, juce::Colour (0xff070403), 0.0f, b.getBottom(), false));
    g.fillRect (b.withTop (floorY));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (0.0f, floorY, b.getWidth(), 2.0f);

    // A tungsten spot from above on the cabinet, breathing a little with the bass.
    const auto warm = dd::gui::sunset::amber.interpolatedWith (juce::Colour (0xfffff0d8), 0.45f);
    const float cx = cabinet.getCentreX();
    for (float w : { 1.0f, 0.8f, 0.6f })
    {
        juce::Path beam;
        beam.startNewSubPath (cx - 40.0f * w, 0.0f);
        beam.lineTo (cx + 40.0f * w, 0.0f);
        beam.lineTo (cx + cabinet.getWidth() * 0.95f * w, floorY + 10.0f);
        beam.lineTo (cx - cabinet.getWidth() * 0.95f * w, floorY + 10.0f);
        beam.closeSubPath();
        g.setGradientFill (juce::ColourGradient (warm.withAlpha (0.07f + 0.03f * lamp), 0.0f, 0.0f, warm.withAlpha (0.015f), 0.0f, floorY, false));
        g.fillPath (beam);
    }
    const juce::Rectangle<float> pool (cx - cabinet.getWidth(), floorY - 16.0f, cabinet.getWidth() * 2.0f, 40.0f);
    g.setGradientFill (juce::ColourGradient (warm.withAlpha (0.16f + 0.08f * lamp), pool.getCentreX(), pool.getCentreY(),
                                             warm.withAlpha (0.0f), pool.getRight(), pool.getCentreY(), true));
    g.fillEllipse (pool);
}

void SubView::drawWoofer (juce::Graphics& g, juce::Point<float> c, float r) const
{
    const float ex = excursion;

    // The cut-out in the baffle, with its bevel catching the light.
    g.setColour (juce::Colour (0xff050403));
    g.fillEllipse (juce::Rectangle<float> (r * 2.2f, r * 2.2f).withCentre (c));
    g.setColour (kChrome.withAlpha (0.18f));
    g.drawEllipse (juce::Rectangle<float> (r * 2.2f, r * 2.2f).withCentre (c), 1.2f);

    // Lamp ring behind the surround: the bass light.
    if (lamp > 0.01f)
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (dd::gui::sunset::orange.withAlpha (lamp * 0.12f));
            g.drawEllipse (juce::Rectangle<float> (r * 2.12f, r * 2.12f).withCentre (c), (float) i * 3.0f);
        }

    // Surround: a rubber roll that bulges as the cone moves out.
    const float sr = r * (1.0f + 0.02f * ex);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a332d), c.x - sr * 0.5f, c.y - sr * 0.7f,
                                             juce::Colour (0xff0b0908), c.x + sr * 0.4f, c.y + sr * 0.8f, false));
    g.fillEllipse (juce::Rectangle<float> (sr * 2.0f, sr * 2.0f).withCentre (c));
    {
        juce::Path hi;
        hi.addCentredArc (c.x, c.y, sr * 0.93f, sr * 0.93f, 0.0f, -kPi * 0.85f, -kPi * 0.15f, true);
        g.setColour (kCream.withAlpha (0.10f + 0.12f * ex));
        g.strokePath (hi, juce::PathStrokeType (sr * 0.05f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Cone: paper, pushed toward you (it grows a little), with the ribs of its pressing.
    const float cr = r * 0.86f * (1.0f + 0.035f * ex);
    juce::ColourGradient cone (juce::Colour (0xff2e2722), c.x - cr * 0.35f, c.y - cr * 0.35f,
                               juce::Colour (0xff0e0b09), c.x + cr, c.y + cr, true);
    g.setGradientFill (cone);
    g.fillEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c));
    for (int i = 1; i <= 4; ++i)
    {
        const float rr = cr * (0.4f + 0.14f * (float) i);
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c.translated (0.0f, 0.6f)), 0.8f);
        g.setColour (kCream.withAlpha (0.035f));
        g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 0.6f);
    }
    // The light slides across the cone as it moves.
    g.setGradientFill (juce::ColourGradient (kCream.withAlpha (0.07f + 0.08f * ex), c.x - cr * (0.45f + 0.1f * ex), c.y - cr * (0.5f + 0.1f * ex),
                                             kCream.withAlpha (0.0f), c.x, c.y, true));
    g.fillEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c));

    // Dust cap: a dome with its highlight.
    const float dr = r * 0.32f * (1.0f + 0.07f * ex);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (juce::Rectangle<float> (dr * 2.0f, dr * 2.0f).withCentre (c.translated (0.0f, dr * 0.12f)));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4038), c.x - dr * 0.4f, c.y - dr * 0.5f,
                                             juce::Colour (0xff120f0c), c.x + dr * 0.6f, c.y + dr * 0.8f, true));
    g.fillEllipse (juce::Rectangle<float> (dr * 2.0f, dr * 2.0f).withCentre (c));
    g.setColour (kCream.withAlpha (0.18f + 0.15f * ex));
    g.fillEllipse (juce::Rectangle<float> (dr * 0.55f, dr * 0.35f).withCentre (c.translated (-dr * 0.35f, -dr * (0.42f + 0.06f * ex))));
}

void SubView::drawCabinet (juce::Graphics& g) const
{
    // A whisper of shake on big hits.
    const auto box = cabinet.translated (0.0f, -0.8f * excursion);
    const float floorY = getHeight() * 0.88f;

    // Shadow on the floor.
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.75f), box.getCentreX(), floorY,
                                             juce::Colours::transparentBlack, box.getRight() + 30.0f, floorY, true));
    g.fillEllipse (box.getX() - 30.0f, floorY - 12.0f, box.getWidth() + 60.0f, 26.0f);

    // Tolex box, its edges catching the spot.
    juce::Path shell;
    shell.addRoundedRectangle (box, 10.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a231e), box.getX(), box.getY(),
                                             juce::Colour (0xff0d0a08), box.getX(), box.getBottom(), false));
    g.fillPath (shell);
    g.setColour (kChrome.withAlpha (0.16f));
    g.strokePath (shell, juce::PathStrokeType (1.2f));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (box.reduced (7.0f), 7.0f, 1.5f);
    for (int i = 0; i < 4; ++i)
    {
        // Chrome corner protectors.
        const float x = i % 2 == 0 ? box.getX() + 6.0f : box.getRight() - 6.0f;
        const float y = i < 2 ? box.getY() + 6.0f : box.getBottom() - 6.0f;
        g.setGradientFill (juce::ColourGradient (kChrome, x - 4.0f, y - 4.0f, juce::Colour (0xff5d554d), x + 4.0f, y + 4.0f, false));
        g.fillEllipse (x - 4.5f, y - 4.5f, 9.0f, 9.0f);
    }

    drawWoofer (g, wooferCentre.translated (0.0f, -0.8f * excursion), wooferR);

    // The port: a slot with a lamp deep inside it.
    const juce::Rectangle<float> port (box.getCentreX() - box.getWidth() * 0.3f, box.getBottom() - box.getHeight() * 0.17f,
                                       box.getWidth() * 0.6f, box.getHeight() * 0.07f);
    g.setColour (juce::Colour (0xff040302));
    g.fillRoundedRectangle (port, port.getHeight() * 0.5f);
    g.setGradientFill (juce::ColourGradient (dd::gui::sunset::orange.withAlpha (0.15f + 0.65f * lamp), port.getCentreX(), port.getCentreY(),
                                             dd::gui::sunset::orange.withAlpha (0.0f), port.getRight(), port.getCentreY(), true));
    g.fillRoundedRectangle (port.reduced (2.0f, 1.0f), port.getHeight() * 0.5f);
    g.setColour (kChrome.withAlpha (0.12f));
    g.drawRoundedRectangle (port, port.getHeight() * 0.5f, 0.8f);
}

void SubView::drawReadout (juce::Graphics& g) const
{
    const auto b = getLocalBounds().toFloat();
    auto r = juce::Rectangle<float> (b.getWidth() * 0.52f, b.getHeight() * 0.14f, b.getWidth() * 0.44f, b.getHeight() * 0.74f);
    const auto& pm = proc.presets;

    g.setColour (dd::gui::sunset::amber);
    g.setFont (dd::gui::LookAndFeel::font (12.0f, 700).withExtraKerningFactor (0.18f));
    g.drawText (pm.getCurrentCategory().toUpperCase(), r.removeFromTop (20.0f), juce::Justification::centredLeft);
    g.setColour (kCream);
    g.setFont (dd::gui::LookAndFeel::display (36.0f));
    g.drawFittedText (pm.getCurrentName(), r.removeFromTop (48.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

    r.removeFromTop (r.getHeight() * 0.18f);
    g.setColour (kCream.withAlpha (0.45f));
    g.setFont (dd::gui::LookAndFeel::font (11.0f, 700).withExtraKerningFactor (0.16f));
    g.drawText ("NOTE", r.removeFromTop (18.0f), juce::Justification::centredLeft);

    // The note glows like a lamp while the bass sounds.
    const auto noteRow = r.removeFromTop (78.0f);
    const auto noteText = note >= 0 ? noteName (note) : juce::String();
    const auto noteFont = dd::gui::LookAndFeel::display (68.0f);
    const float glow = 0.25f + 0.75f * lamp;
    g.setFont (noteFont);
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (dd::gui::sunset::orange.withAlpha (glow * 0.08f));
        g.drawText (noteText, noteRow.translated ((float) i * 0.6f, 0.0f), juce::Justification::centredLeft);
        g.drawText (noteText, noteRow.translated (-(float) i * 0.6f, 0.0f), juce::Justification::centredLeft);
    }
    g.setColour (kCream.interpolatedWith (dd::gui::sunset::amber, 0.3f + 0.5f * lamp));
    g.drawText (noteText, noteRow, juce::Justification::centredLeft);

    if (note >= 0)
    {
        const float shift = proc.apvts.getRawParameterValue ("octave")->load() * 12.0f + proc.apvts.getRawParameterValue ("tune")->load();
        const float hz = 440.0f * std::exp2 (((float) note + shift - 69.0f) / 12.0f);
        g.setColour (kCream.withAlpha (0.6f));
        g.setFont (dd::gui::LookAndFeel::font (14.0f, 500));
        g.drawText (juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz", r.removeFromTop (22.0f), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (kCream.withAlpha (0.45f));
        g.setFont (dd::gui::LookAndFeel::font (13.0f, 400));
        g.drawText ("Play a note: the speaker moves with the bass.", r.removeFromTop (22.0f), juce::Justification::centredLeft);
    }
}

void SubView::paint (juce::Graphics& g)
{
    juce::Path frame;
    frame.addRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.reduceClipRegion (frame);
    drawRoom (g);

    // Pressure rings rolling off the cone, behind the cabinet's face.
    for (auto& ring : rings)
        if (ring.age < 1.0f)
        {
            const float rr = wooferR * (1.15f + ring.age * 2.6f);
            const float a = ring.strength * (1.0f - ring.age) * (1.0f - ring.age);
            g.setColour (dd::gui::sunset::amber.withAlpha (a * 0.28f));
            g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (wooferCentre), 2.0f + 6.0f * (1.0f - ring.age));
        }

    drawCabinet (g);
    drawReadout (g);

    const auto area = getLocalBounds();
    dd::gui::paintGrain (g, area, 0.15f);
    juce::ColourGradient vignette (juce::Colours::transparentBlack, area.toFloat().getCentreX(), area.toFloat().getCentreY(),
                                   juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    vignette.addColour (0.55, juce::Colours::transparentBlack);
    g.setGradientFill (vignette);
    g.fillRect (area);
}
