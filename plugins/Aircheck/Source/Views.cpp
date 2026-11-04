#include "Views.h"

namespace
{
const juce::Colour kTrace   { 0xffffb070 };   // hot filament
const juce::Colour kGlow    { 0xffe8763a };
const juce::Colour kLitText { 0xffffd9a0 };
const juce::Colour kDimText { 0xffa39a8c };   // consoles stay dark in both themes

juce::String noteName (int n)
{
    return aircheck::keyNames()[((n % 12) + 12) % 12] + juce::String (n / 12 - 1);
}

void glowStroke (juce::Graphics& g, const juce::Path& p, float width, float glowAlpha = 0.16f)
{
    g.setColour (kGlow.withAlpha (glowAlpha));
    g.strokePath (p, juce::PathStrokeType (width * 3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (kGlow.withAlpha (0.5f));
    g.strokePath (p, juce::PathStrokeType (width * 1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (kTrace);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace

//==============================================================================
PitchView::PitchView (AircheckProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
    centre = 60.0f;
    for (int i = 0; i < 40; ++i)   // settle on what was just sung, so the first frame is already right
        timerCallback();
    startTimerHz (30);
}

void PitchView::timerCallback()
{
    const int n = (int) in.size();
    float sum = 0.0f;
    int count = 0;
    for (int k = 0; k < n; ++k)
    {
        in[(size_t) (n - 1 - k)] = proc.pitchHistory.inAt (k);
        out[(size_t) (n - 1 - k)] = proc.pitchHistory.outAt (k);
        if (k < 120 && out[(size_t) (n - 1 - k)] > 0.0f)
        {
            sum += out[(size_t) (n - 1 - k)];
            ++count;
        }
    }
    target = proc.pitchHistory.targetAt (0);
    if (count > 10)
        centre += (sum / (float) count - centre) * 0.12f;
    repaint();
}

void PitchView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f, 4.0f);
    dd::gui::paintConsole (g, r, 12.0f, dd::gui::lampBloom (dd::gui::animationTime()));
    auto screen = r.reduced (10.0f, 10.0f);
    auto gutter = screen.removeFromLeft (30.0f);

    const auto& s = proc.apvts;
    const bool tuneOn = s.getRawParameterValue ("tuneOn")->load() >= 0.5f;
    const int key = (int) s.getRawParameterValue ("tuneKey")->load();
    const uint16_t mask = aircheck::scaleMask ((int) s.getRawParameterValue ("tuneScale")->load());
    constexpr float span = 15.0f;
    const float rowH = screen.getHeight() / span;
    auto yFor = [&] (float midi) { return screen.getCentreY() - (midi - centre) * rowH; };

    // Note lines: the scale's notes lit, the others barely there; the target note glows.
    g.setFont (dd::gui::LookAndFeel::font (8.5f, 600));
    for (int nn = (int) std::floor (centre - span * 0.5f); nn <= (int) std::ceil (centre + span * 0.5f); ++nn)
    {
        const float y = yFor ((float) nn);
        if (y < screen.getY() - 1.0f || y > screen.getBottom() + 1.0f)
            continue;
        const bool lit = aircheck::inScale (nn, key, mask);
        if (tuneOn && nn == target)
        {
            g.setColour (kGlow.withAlpha (0.11f));
            g.fillRect (screen.getX(), y - rowH * 0.5f, screen.getWidth(), rowH);
        }
        g.setColour (kGlow.withAlpha (lit ? (nn % 12 == key ? 0.26f : 0.15f) : 0.04f));
        g.fillRect (screen.getX(), y - 0.5f, screen.getWidth(), 1.0f);
        if (lit && rowH > 7.0f)
        {
            g.setColour ((tuneOn && nn == target ? kLitText : kDimText).withAlpha (tuneOn && nn == target ? 0.95f : 0.55f));
            g.drawText (noteName (nn), juce::Rectangle<float> (gutter.getX(), y - 6.0f, gutter.getWidth() - 4.0f, 12.0f), juce::Justification::centredRight);
        }
    }

    // Traces, newest on the right; gaps where the voice is unvoiced.
    const int n = (int) in.size();
    const float dx = screen.getWidth() / (float) (n - 1);
    auto trace = [&] (const std::array<float, 360>& v)
    {
        juce::Path p;
        bool pen = false;
        for (int i = 0; i < n; ++i)
        {
            if (v[(size_t) i] <= 0.0f) { pen = false; continue; }
            const float x = screen.getX() + (float) i * dx;
            const float y = juce::jlimit (screen.getY(), screen.getBottom(), yFor (v[(size_t) i]));
            if (! pen) { p.startNewSubPath (x, y); pen = true; }
            else       p.lineTo (x, y);
        }
        return p;
    };
    g.setColour (kDimText.withAlpha (tuneOn ? 0.5f : 0.0f));
    if (tuneOn)
        g.strokePath (trace (in), juce::PathStrokeType (1.2f));
    glowStroke (g, trace (tuneOn ? out : in), 1.8f);

    // Readout: the note now and how far the voice is from it.
    const float now = in[(size_t) n - 1];
    auto box = screen.removeFromRight (120.0f).removeFromTop (54.0f);
    if (now > 0.0f)
    {
        const int shown = tuneOn && target >= 0 ? target : (int) std::lround (now);
        const int cents = (int) std::lround ((now - (float) shown) * 100.0f);
        dd::gui::drawGlowing (g, noteName (shown), box.removeFromTop (30.0f), kLitText, dd::gui::LookAndFeel::font (24.0f, 700),
                              juce::Justification::centredRight);
        g.setFont (dd::gui::LookAndFeel::font (10.0f, 600).withExtraKerningFactor (0.08f));
        g.setColour (kDimText);
        g.drawText ((tuneOn ? juce::String ("SUNG ") : juce::String()) + (cents > 0 ? "+" : "") + juce::String (cents) + " CENTS",
                    box, juce::Justification::centredRight);
    }
    else
    {
        g.setFont (dd::gui::LookAndFeel::font (10.0f, 700).withExtraKerningFactor (0.12f));
        g.setColour (kDimText.withAlpha (0.6f));
        g.drawText (tuneOn ? "LISTENING" : "TUNE OFF", box.removeFromTop (30.0f), juce::Justification::centredRight);
    }
}

//==============================================================================
namespace
{
juce::String bandId (int band, const char* what) { return "b" + juce::String (band + 1) + what; }
} // namespace

EqView::EqView (AircheckProcessor& p) : proc (p)
{
    fftData.assign (8192, 0.0f);
    window.resize (4096);
    for (size_t i = 0; i < window.size(); ++i)
        window[i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (window.size() - 1));
    for (int b = 0; b < aircheck::ProEq::kBands; ++b)
        for (auto* what : { "On", "Type", "Freq", "Gain", "Q", "Slope", "Dyn", "Thresh", "Place" })
            proc.apvts.addParameterListener (bandId (b, what), this);
    startTimerHz (30);
}

EqView::~EqView()
{
    for (int b = 0; b < aircheck::ProEq::kBands; ++b)
        for (auto* what : { "On", "Type", "Freq", "Gain", "Q", "Slope", "Dyn", "Thresh", "Place" })
            proc.apvts.removeParameterListener (bandId (b, what), this);
}

juce::Rectangle<float> EqView::plot() const { return getLocalBounds().toFloat().reduced (2.0f, 4.0f).reduced (14.0f, 12.0f).withTrimmedBottom (12.0f); }
float EqView::xForHz (float hz) const   { const auto p = plot(); return p.getX() + p.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); }
float EqView::hzForX (float x) const    { const auto p = plot(); return 20.0f * std::pow (1000.0f, (x - p.getX()) / p.getWidth()); }
float EqView::yForDb (float db) const   { const auto p = plot(); return p.getCentreY() - db / 24.0f * p.getHeight() * 0.5f; }
float EqView::dbForY (float y) const    { const auto p = plot(); return (p.getCentreY() - y) / (p.getHeight() * 0.5f) * 24.0f; }
float EqView::value (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

void EqView::setValue (const juce::String& id, float v, bool gesture)
{
    if (auto* prm = proc.apvts.getParameter (id))
    {
        if (gesture) prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (prm->getNormalisableRange().snapToLegalValue (v)));
        if (gesture) prm->endChangeGesture();
    }
}

std::array<aircheck::BandSettings, aircheck::ProEq::kBands> EqView::readBands() const
{
    std::array<aircheck::BandSettings, aircheck::ProEq::kBands> out {};
    for (int b = 0; b < aircheck::ProEq::kBands; ++b)
    {
        auto& s = out[(size_t) b];
        s.on = value (bandId (b, "On")) >= 0.5f;
        s.type = (int) value (bandId (b, "Type"));
        s.hz = value (bandId (b, "Freq"));
        s.db = value (bandId (b, "Gain"));
        s.q = value (bandId (b, "Q"));
        s.slope = (int) value (bandId (b, "Slope"));
        s.dynRange = value (bandId (b, "Dyn"));
        s.dynThresh = value (bandId (b, "Thresh"));
        s.place = (int) value (bandId (b, "Place"));
    }
    return out;
}

juce::Point<float> EqView::nodePos (int b) const
{
    const auto& s = bands[(size_t) b];
    float db = s.db;
    if (! aircheck::isGainType (s.type))
        db = (float) juce::jlimit (-24.0, 0.0, aircheck::ProEq::responseDb ({ s }, s.hz, 48000.0) * 0.5);
    if (s.type == (int) aircheck::BandType::Tilt)
        db = s.db * 0.5f;
    return { juce::jlimit (plot().getX(), plot().getRight(), xForHz (s.hz)), yForDb (db) };
}

int EqView::nodeAt (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 14.0f;
    for (int b = 0; b < aircheck::ProEq::kBands; ++b)
    {
        // Off bands can still be picked up (to switch them back on), but lit ones win.
        const float d = nodePos (b).getDistanceFrom (p) + (bands[(size_t) b].on ? 0.0f : 4.0f);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void EqView::setSelected (int b)
{
    selected = juce::jlimit (0, aircheck::ProEq::kBands - 1, b);
    curveDirty = true;
    repaint();
}

void EqView::analyse (dd::ScopeBuffer& src, std::vector<float>& smooth)
{
    const int n = (int) window.size();
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    src.copyLatest (fftData.data(), n);
    for (int i = 0; i < n; ++i)
        fftData[(size_t) i] *= window[(size_t) i];
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const auto p = plot();
    const int cols = std::max (8, (int) p.getWidth() / 3);
    if ((int) smooth.size() != cols)
        smooth.assign ((size_t) cols, -120.0f);
    const float binHz = 48000.0f / (float) n;
    for (int c = 0; c < cols; ++c)
    {
        const float f0 = hzForX (p.getX() + p.getWidth() * (float) c / (float) cols);
        const float f1 = hzForX (p.getX() + p.getWidth() * (float) (c + 1) / (float) cols);
        const int b0 = std::max (1, (int) (f0 / binHz)), b1 = std::max (b0, std::min (n / 2 - 1, (int) (f1 / binHz)));
        float m = 0.0f;
        for (int b = b0; b <= b1; ++b)
            m = std::max (m, fftData[(size_t) b]);
        // Amplitude of a sine, tilted +4.5 dB / octave so a voice reads roughly flat.
        const float db = dd::gainToDb (m * 4.0f / (float) n) + 4.5f * std::log2 (std::max (20.0f, f0) / 1000.0f);
        auto& s = smooth[(size_t) c];
        s = db > s ? db : std::max (db, s - 1.6f);
    }
}

void EqView::timerCallback()
{
    analyse (proc.preEq, preSpec);
    analyse (proc.postEq, postSpec);
    repaint();
}

void EqView::updateCurve()
{
    bands = readBands();
    const auto p = plot();
    const size_t w = (size_t) std::max (2, (int) p.getWidth());
    curve.resize (w);
    bandCurve.resize (w);
    const auto& sel = bands[(size_t) selected];
    for (size_t i = 0; i < w; ++i)
    {
        const double hz = hzForX (p.getX() + (float) i);
        curve[i] = (float) aircheck::ProEq::responseDb (bands, hz, 48000.0);
        bandCurve[i] = sel.on ? (float) aircheck::ProEq::responseDb ({ sel }, hz, 48000.0) : 0.0f;
    }
    curveDirty = false;
}

void EqView::resized()
{
    curveDirty = true;
    analyse (proc.preEq, preSpec);   // so a picture taken right away already shows the voice
    analyse (proc.postEq, postSpec);
}

void EqView::paint (juce::Graphics& g)
{
    if (curveDirty)
        updateCurve();
    const auto r = getLocalBounds().toFloat().reduced (2.0f, 4.0f);
    dd::gui::paintConsole (g, r, 12.0f, dd::gui::lampBloom (dd::gui::animationTime()));
    const auto p = plot();

    // Grid: decades and the vocal landmarks, +-6 / 12 / 18 / 24 dB.
    g.setFont (dd::gui::LookAndFeel::font (8.5f, 600));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForHz (hz);
        g.setColour (kGlow.withAlpha (0.08f));
        g.fillRect (x, p.getY(), 1.0f, p.getHeight());
        g.setColour (kDimText.withAlpha (0.6f));
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (x - 20.0f, p.getBottom() + 2.0f, 40.0f, 11.0f), juce::Justification::centred);
    }
    for (int db : { -18, -12, -6, 0, 6, 12, 18 })
    {
        const float y = yForDb ((float) db);
        g.setColour (kGlow.withAlpha (db == 0 ? 0.2f : 0.07f));
        g.fillRect (p.getX(), y, p.getWidth(), 1.0f);
        if (db != 0)
        {
            g.setColour (kDimText.withAlpha (0.45f));
            g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (p.getRight() - 24.0f, y - 11.0f, 22.0f, 10.0f),
                        juce::Justification::centredRight);
        }
    }

    // Analyzer: before the EQ as a faint line, after it as a lit fill.
    auto specPath = [&] (const std::vector<float>& s, bool closed)
    {
        juce::Path path;
        const int cols = (int) s.size();
        for (int c = 0; c < cols; ++c)
        {
            const float x = p.getX() + p.getWidth() * ((float) c + 0.5f) / (float) cols;
            const float y = juce::jlimit (p.getY(), p.getBottom(), p.getBottom() - (s[(size_t) c] + 84.0f) / 84.0f * p.getHeight());
            if (c == 0) path.startNewSubPath (closed ? p.getX() : x, closed ? p.getBottom() : y);
            if (c == 0 && closed) path.lineTo (x, y);
            else if (c > 0) path.lineTo (x, y);
        }
        if (closed && cols > 0)
        {
            path.lineTo (p.getRight(), p.getBottom());
            path.closeSubPath();
        }
        return path;
    };
    if (! postSpec.empty())
    {
        auto fill = specPath (postSpec, true);
        g.setGradientFill (juce::ColourGradient (kGlow.withAlpha (0.24f), 0.0f, p.getY() + p.getHeight() * 0.3f,
                                                 kGlow.withAlpha (0.03f), 0.0f, p.getBottom(), false));
        g.fillPath (fill);
        g.setColour (kDimText.withAlpha (0.35f));
        g.strokePath (specPath (preSpec, false), juce::PathStrokeType (1.0f));
    }

    auto curvePath = [&] (const std::vector<float>& c)
    {
        juce::Path path;
        for (size_t i = 0; i < c.size(); ++i)
        {
            const float y = juce::jlimit (p.getY() - 2.0f, p.getBottom() + 2.0f, yForDb (c[i]));
            if (i == 0) path.startNewSubPath (p.getX(), y);
            else        path.lineTo (p.getX() + (float) i, y);
        }
        return path;
    };
    // The selected band's own contribution as a soft filled shape, then the total curve.
    if (bands[(size_t) selected].on && bandCurve.size() > 1)
    {
        auto shape = curvePath (bandCurve);
        shape.lineTo (p.getRight(), yForDb (0.0f));
        shape.lineTo (p.getX(), yForDb (0.0f));
        shape.closeSubPath();
        g.setColour (kGlow.withAlpha (0.12f));
        g.fillPath (shape);
    }
    if (curve.size() > 1)
        glowStroke (g, curvePath (curve), 2.0f, 0.12f);

    // Nodes: numbered LEDs; dynamic bands show how far their gain is moving right now.
    for (int b = 0; b < aircheck::ProEq::kBands; ++b)
    {
        const auto& s = bands[(size_t) b];
        const auto pos = nodePos (b);
        const bool active = b == hover || b == drag || b == selected;
        if (s.on && aircheck::isGainType (s.type) && std::abs (s.dynRange) > 0.05f)
        {
            const float moving = proc.bandDynDb[(size_t) b].load();
            g.setColour (kGlow.withAlpha (0.25f));
            g.fillRect (pos.x - 1.5f, std::min (pos.y, yForDb (s.db + s.dynRange)), 3.0f, std::abs (yForDb (s.db + s.dynRange) - pos.y));
            g.setColour (kTrace.withAlpha (0.9f));
            g.fillEllipse (pos.x - 3.0f, yForDb (s.db + moving) - 3.0f, 6.0f, 6.0f);
        }
        dd::gui::paintLamp (g, pos, active ? 7.0f : 5.5f, kGlow, s.on ? 1.0f : 0.15f);
        g.setFont (dd::gui::LookAndFeel::font (8.0f, 700));
        g.setColour (s.on ? juce::Colour (0xff1a0f08) : kDimText.withAlpha (0.6f));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (pos.x - 6.0f, pos.y - 6.0f, 12.0f, 12.0f), juce::Justification::centred);
    }
    if (const int b = drag >= 0 ? drag : hover; b >= 0)
    {
        const auto& s = bands[(size_t) b];
        juce::String text = "BAND " + juce::String (b + 1) + "  " + aircheck::bandTypeNames()[s.type].toUpperCase() + "   "
                          + (s.hz >= 1000.0f ? juce::String (s.hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (s.hz)) + " Hz");
        if (aircheck::isGainType (s.type))
            text << "   " << (s.db > 0.0f ? "+" : "") << juce::String (s.db, 1) << " dB";
        text << "   Q " << juce::String (s.q, 2);
        if (! s.on)
            text << "   (OFF)";
        const auto pos = nodePos (b);
        auto box = juce::Rectangle<float> (pos.x - 150.0f, pos.y - 30.0f, 300.0f, 16.0f).constrainedWithin (p);
        dd::gui::drawGlowing (g, text, box, kLitText, dd::gui::LookAndFeel::font (10.0f, 600).withExtraKerningFactor (0.06f));
    }
}

void EqView::mouseMove (const juce::MouseEvent& e)
{
    const int h = nodeAt (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqView::mouseExit (const juce::MouseEvent&) { hover = -1; repaint(); }

void EqView::mouseDown (const juce::MouseEvent& e)
{
    const int b = nodeAt (e.position);
    if (b < 0)
        return;
    if (e.mods.isPopupMenu())
    {
        setValue (bandId (b, "On"), bands[(size_t) b].on ? 0.0f : 1.0f, true);
        return;
    }
    setSelected (b);
    if (onSelect)
        onSelect (b);
    drag = b;
    for (auto* what : { "Freq", "Gain", "On" })
        if (auto* prm = proc.apvts.getParameter (bandId (b, what)))
            prm->beginChangeGesture();
    if (! bands[(size_t) b].on)
        setValue (bandId (b, "On"), 1.0f);   // grabbing an off band switches it on
}

void EqView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0)
        return;
    setValue (bandId (drag, "Freq"), hzForX (e.position.x));
    if (aircheck::isGainType (bands[(size_t) drag].type))
        setValue (bandId (drag, "Gain"), juce::jlimit (-24.0f, 24.0f, dbForY (e.position.y) * (bands[(size_t) drag].type == (int) aircheck::BandType::Tilt ? 2.0f : 1.0f)));
}

void EqView::mouseUp (const juce::MouseEvent&)
{
    if (drag < 0)
        return;
    for (auto* what : { "Freq", "Gain", "On" })
        if (auto* prm = proc.apvts.getParameter (bandId (drag, what)))
            prm->endChangeGesture();
    drag = -1;
}

void EqView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = nodeAt (e.position);
    if (b >= 0)
    {
        if (aircheck::isGainType (bands[(size_t) b].type))
            setValue (bandId (b, "Gain"), 0.0f, true);
        else
            setValue (bandId (b, "On"), 0.0f, true);
        return;
    }
    // Empty space: the first free band becomes a bell right there.
    for (int f = 0; f < aircheck::ProEq::kBands; ++f)
        if (! bands[(size_t) f].on)
        {
            setValue (bandId (f, "Type"), 0.0f, true);
            setValue (bandId (f, "Freq"), hzForX (e.position.x), true);
            setValue (bandId (f, "Gain"), juce::jlimit (-24.0f, 24.0f, dbForY (e.position.y)), true);
            setValue (bandId (f, "Q"), 1.0f, true);
            setValue (bandId (f, "On"), 1.0f, true);
            setSelected (f);
            if (onSelect)
                onSelect (f);
            return;
        }
}

void EqView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int b = nodeAt (e.position);
    if (b < 0)
        return;
    setValue (bandId (b, "Q"), value (bandId (b, "Q")) * std::exp (w.deltaY * 1.2f), true);
}

//==============================================================================
BandStrip::BandStrip (AircheckProcessor& p) : proc (p)
{
    for (auto* b : { &prev, &next })
    {
        addAndMakeVisible (*b);
        b->setTooltip ("Previous / next band (or click a point on the curve)");
    }
    post = std::make_unique<dd::gui::Toggle> (proc.apvts, "eqPost", "EQ", "POST", "PRE");
    addAndMakeVisible (*post);
    prev.onClick = [this] { if (onBandChange) onBandChange ((band + aircheck::ProEq::kBands - 1) % aircheck::ProEq::kBands); };
    next.onClick = [this] { if (onBandChange) onBandChange ((band + 1) % aircheck::ProEq::kBands); };
    setBand (0);
}

void BandStrip::setBand (int b)
{
    if (b == band)
        return;
    band = b;
    controls.clear();
    auto& s = proc.apvts;
    const auto a = dd::gui::sunset::orange;
    controls.push_back (std::make_unique<dd::gui::Toggle> (s, bandId (b, "On"), "On"));
    controls.push_back (std::make_unique<dd::gui::Choice> (s, bandId (b, "Type"), "Type"));
    controls.push_back (std::make_unique<dd::gui::Knob> (s, bandId (b, "Freq"), "Freq", a));
    controls.push_back (std::make_unique<dd::gui::Knob> (s, bandId (b, "Gain"), "Gain", a));
    controls.push_back (std::make_unique<dd::gui::Knob> (s, bandId (b, "Q"), "Q", a));
    controls.push_back (std::make_unique<dd::gui::Choice> (s, bandId (b, "Slope"), "Slope"));
    auto dyn = std::make_unique<dd::gui::Knob> (s, bandId (b, "Dyn"), "Dynamic", a);
    dyn->getSlider().setTooltip ("Dynamic EQ: how far the gain moves when this band gets loud (negative = cut harder when loud)");
    controls.push_back (std::move (dyn));
    controls.push_back (std::make_unique<dd::gui::Knob> (s, bandId (b, "Thresh"), "Threshold", a));
    controls.push_back (std::make_unique<dd::gui::Choice> (s, bandId (b, "Place"), "Stereo"));
    for (auto& c : controls)
        addAndMakeVisible (*c);
    resized();
    repaint();
}

void BandStrip::resized()
{
    // Cell 0: the band number with arrows; then one 64 px cell per control.
    constexpr int cw = dd::gui::Panel::kCellW;
    auto r = getLocalBounds();
    auto first = r.removeFromLeft (cw).withTrimmedTop (34).withSizeKeepingCentre (cw - 8, 22);
    prev.setBounds (first.removeFromLeft (first.getWidth() / 2).reduced (1, 0));
    next.setBounds (first.reduced (1, 0));
    for (size_t i = 0; i < controls.size(); ++i)
    {
        auto cell = r.removeFromLeft (cw);
        if (dynamic_cast<dd::gui::Choice*> (controls[i].get()) != nullptr)
            controls[i]->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 52).translated (0, -8));
        else
            controls[i]->setBounds (cell);
    }
    if (post != nullptr)
        post->setBounds (r.removeFromLeft (cw));
}

void BandStrip::paint (juce::Graphics& g)
{
    const auto cell = getLocalBounds().removeFromLeft (dd::gui::Panel::kCellW).toFloat();
    dd::gui::drawCaption (g, "Band", cell.withHeight (16.0f), juce::Justification::centred, 9.5f);
    dd::gui::drawGlowing (g, juce::String (band + 1), cell.withTrimmedTop (12.0f).withHeight (22.0f), kLitText,
                          dd::gui::LookAndFeel::font (18.0f, 700));
}

//==============================================================================
void GrMeter::timerCallback()
{
    va = std::max (a ? a() : 0.0f, va * 0.85f);
    vb = std::max (b ? b() : 0.0f, vb * 0.85f);
    repaint();
}

void GrMeter::paint (juce::Graphics& g)
{
    const auto& t = dd::gui::tokens();
    auto r = getLocalBounds().toFloat();
    g.setFont (dd::gui::LookAndFeel::font (9.5f, 700).withExtraKerningFactor (0.1f));
    g.setColour (t.cream500);
    g.drawText (label.toUpperCase(), r.removeFromTop (16.0f), juce::Justification::centred);
    auto tray = r.withSizeKeepingCentre (b ? 30.0f : 20.0f, r.getHeight() - 8.0f).withTrimmedBottom (4.0f);
    dd::gui::paintTray (g, tray, 6.0f, 0.8f);
    auto inner = tray.reduced (5.0f, 5.0f);
    constexpr int segs = 12;
    const float segH = inner.getHeight() / (float) segs;
    const int cols = b ? 2 : 1;
    for (int k = 0; k < cols; ++k)
    {
        const float v = k == 0 ? va : vb;
        const float cw = inner.getWidth() / (float) cols;
        const auto col = inner.withWidth (cw - (cols > 1 ? 1.0f : 0.0f)).translated ((float) k * cw, 0.0f);
        for (int sIdx = 0; sIdx < segs; ++sIdx)
        {
            const bool lit = v > (float) sIdx * 2.0f + 0.5f;   // 2 dB per segment, from the top down
            auto seg = col.withHeight (segH - 1.5f).translated (0.0f, (float) sIdx * segH);
            g.setColour (lit ? kTrace.interpolatedWith (kGlow, (float) sIdx / segs) : juce::Colour (0xff2a2219).withAlpha (t.light ? 0.25f : 0.8f));
            g.fillRoundedRectangle (seg, 1.0f);
        }
    }
}
