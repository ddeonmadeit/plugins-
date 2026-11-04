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
const std::array<EqView::Node, 7>& EqView::nodes()
{
    static const std::array<Node, 7> n {{
        { "Low Cut", "eqLowCut", nullptr, nullptr }, { "Low", "eqLsFreq", "eqLsGain", nullptr },
        { "Band 1", "eq1Freq", "eq1Gain", "eq1Q" },  { "Band 2", "eq2Freq", "eq2Gain", "eq2Q" },
        { "Band 3", "eq3Freq", "eq3Gain", "eq3Q" },  { "High", "eqHsFreq", "eqHsGain", nullptr },
        { "High Cut", "eqHighCut", nullptr, nullptr } }};
    return n;
}

EqView::EqView (AircheckProcessor& p) : proc (p)
{
    eq.prepare (48000.0);
    fftData.assign (8192, 0.0f);
    window.resize (4096);
    for (size_t i = 0; i < window.size(); ++i)
        window[i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (window.size() - 1));
    for (auto& nd : nodes())
        for (auto* id : { nd.freqId, nd.gainId, nd.qId })
            if (id != nullptr)
                proc.apvts.addParameterListener (id, this);
    startTimerHz (30);
}

EqView::~EqView()
{
    for (auto& nd : nodes())
        for (auto* id : { nd.freqId, nd.gainId, nd.qId })
            if (id != nullptr)
                proc.apvts.removeParameterListener (id, this);
}

juce::Rectangle<float> EqView::plot() const { return getLocalBounds().toFloat().reduced (2.0f, 4.0f).reduced (14.0f, 12.0f).withTrimmedBottom (12.0f); }
float EqView::xForHz (float hz) const   { const auto p = plot(); return p.getX() + p.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); }
float EqView::hzForX (float x) const    { const auto p = plot(); return 20.0f * std::pow (1000.0f, (x - p.getX()) / p.getWidth()); }
float EqView::yForDb (float db) const   { const auto p = plot(); return p.getCentreY() - db / 20.0f * p.getHeight() * 0.5f; }
float EqView::dbForY (float y) const    { const auto p = plot(); return (p.getCentreY() - y) / (p.getHeight() * 0.5f) * 20.0f; }
float EqView::value (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

void EqView::setValue (const char* id, float v)
{
    if (auto* prm = proc.apvts.getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (prm->getNormalisableRange().snapToLegalValue (v)));
}

juce::Point<float> EqView::nodePos (int i) const
{
    const auto& nd = nodes()[(size_t) i];
    const float hz = value (nd.freqId);
    const float db = nd.gainId != nullptr ? value (nd.gainId) : juce::jlimit (-18.0f, 0.0f, eq.responseDb (hz));
    return { juce::jlimit (plot().getX(), plot().getRight(), xForHz (hz)), yForDb (db) };
}

int EqView::nodeAt (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 14.0f;
    for (int i = 0; i < (int) nodes().size(); ++i)
        if (const float d = nodePos (i).getDistanceFrom (p); d < bestD)
        {
            bestD = d;
            best = i;
        }
    return best;
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
    {
        aircheck::VocalEq::Settings st;
        st.lowCut = value ("eqLowCut");  st.lsHz = value ("eqLsFreq");  st.lsDb = value ("eqLsGain");
        st.p1Hz = value ("eq1Freq");     st.p1Db = value ("eq1Gain");   st.p1Q = value ("eq1Q");
        st.p2Hz = value ("eq2Freq");     st.p2Db = value ("eq2Gain");   st.p2Q = value ("eq2Q");
        st.p3Hz = value ("eq3Freq");     st.p3Db = value ("eq3Gain");   st.p3Q = value ("eq3Q");
        st.hsHz = value ("eqHsFreq");    st.hsDb = value ("eqHsGain");  st.highCut = value ("eqHighCut");
        eq.set (st);
        const auto p = plot();
        curve.resize ((size_t) std::max (2, (int) p.getWidth()));
        for (size_t i = 0; i < curve.size(); ++i)
            curve[i] = eq.responseDb (hzForX (p.getX() + (float) i));
        curveDirty = false;
    }
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

    // Grid: decades and the vocal landmarks, +-6 / 12 / 18 dB.
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
        g.setGradientFill (juce::ColourGradient (kGlow.withAlpha (0.28f), 0.0f, p.getY() + p.getHeight() * 0.3f,
                                                 kGlow.withAlpha (0.03f), 0.0f, p.getBottom(), false));
        g.fillPath (fill);
        g.setColour (kDimText.withAlpha (0.35f));
        g.strokePath (specPath (preSpec, false), juce::PathStrokeType (1.0f));
    }

    // The EQ curve.
    if (curve.size() > 1)
    {
        juce::Path c;
        for (size_t i = 0; i < curve.size(); ++i)
        {
            const float y = juce::jlimit (p.getY() - 2.0f, p.getBottom() + 2.0f, yForDb (curve[i]));
            if (i == 0) c.startNewSubPath (p.getX(), y);
            else        c.lineTo (p.getX() + (float) i, y);
        }
        glowStroke (g, c, 2.0f, 0.12f);
    }

    // Nodes: LEDs on the curve; the active one is lit and labelled.
    for (int i = 0; i < (int) nodes().size(); ++i)
    {
        const auto pos = nodePos (i);
        const bool active = i == hover || i == drag;
        const auto& nd = nodes()[(size_t) i];
        const bool engaged = nd.gainId != nullptr ? std::abs (value (nd.gainId)) > 0.05f
                                                  : (i == 0 ? value (nd.freqId) > 21.0f : value (nd.freqId) < 21500.0f);
        dd::gui::paintLamp (g, pos, active ? 6.5f : 5.0f, kGlow, engaged || active ? 1.0f : 0.25f);
    }
    if (const int i = drag >= 0 ? drag : hover; i >= 0)
    {
        const auto& nd = nodes()[(size_t) i];
        const float hz = value (nd.freqId);
        juce::String text = juce::String (nd.name).toUpperCase() + "   "
                          + (hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz");
        if (nd.gainId != nullptr)
            text << "   " << (value (nd.gainId) > 0.0f ? "+" : "") << juce::String (value (nd.gainId), 1) << " dB";
        if (nd.qId != nullptr)
            text << "   Q " << juce::String (value (nd.qId), 2);
        const auto pos = nodePos (i);
        auto box = juce::Rectangle<float> (pos.x - 110.0f, pos.y - 30.0f, 220.0f, 16.0f).constrainedWithin (p);
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
    drag = nodeAt (e.position);
    if (drag < 0)
        return;
    const auto& nd = nodes()[(size_t) drag];
    for (auto* id : { nd.freqId, nd.gainId })
        if (id != nullptr)
            if (auto* prm = proc.apvts.getParameter (id))
                prm->beginChangeGesture();
}

void EqView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0)
        return;
    const auto& nd = nodes()[(size_t) drag];
    setValue (nd.freqId, hzForX (e.position.x));
    if (nd.gainId != nullptr)
        setValue (nd.gainId, juce::jlimit (-18.0f, 18.0f, dbForY (e.position.y)));
}

void EqView::mouseUp (const juce::MouseEvent&)
{
    if (drag < 0)
        return;
    const auto& nd = nodes()[(size_t) drag];
    for (auto* id : { nd.freqId, nd.gainId })
        if (id != nullptr)
            if (auto* prm = proc.apvts.getParameter (id))
                prm->endChangeGesture();
    drag = -1;
}

void EqView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = nodeAt (e.position);
    if (i < 0)
        return;
    const auto& nd = nodes()[(size_t) i];
    auto reset = [this] (const char* id, float v)
    {
        if (auto* prm = proc.apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            setValue (id, v);
            prm->endChangeGesture();
        }
    };
    if (nd.gainId != nullptr)
        reset (nd.gainId, 0.0f);
    else
        reset (nd.freqId, i == 0 ? 20.0f : 22000.0f);   // cut filters: off
}

void EqView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = nodeAt (e.position);
    if (i < 0 || nodes()[(size_t) i].qId == nullptr)
        return;
    const char* id = nodes()[(size_t) i].qId;
    if (auto* prm = proc.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        setValue (id, value (id) * std::exp (w.deltaY * 1.2f));
        prm->endChangeGesture();
    }
}

//==============================================================================
void GrMeter::timerCallback()
{
    comp = std::max (proc.compGr.load(), comp * 0.85f);
    ess = std::max (proc.deessGr.load(), ess * 0.85f);
    repaint();
}

void GrMeter::paint (juce::Graphics& g)
{
    const auto& t = dd::gui::tokens();
    auto r = getLocalBounds().toFloat();
    g.setFont (dd::gui::LookAndFeel::font (9.5f, 700).withExtraKerningFactor (0.1f));
    g.setColour (t.cream500);
    g.drawText ("GR", r.removeFromTop (16.0f), juce::Justification::centred);
    auto tray = r.withSizeKeepingCentre (30.0f, r.getHeight() - 8.0f).withTrimmedBottom (4.0f);
    dd::gui::paintTray (g, tray, 6.0f, 0.8f);
    auto inner = tray.reduced (5.0f, 5.0f);
    constexpr int segs = 12;
    const float segH = inner.getHeight() / (float) segs;
    for (int k = 0; k < 2; ++k)
    {
        const float v = k == 0 ? comp : ess;
        const auto col = inner.withWidth (inner.getWidth() * 0.5f - 1.0f).translated ((float) k * inner.getWidth() * 0.5f + (float) k, 0.0f);
        for (int sIdx = 0; sIdx < segs; ++sIdx)
        {
            const bool lit = v > (float) sIdx * 2.0f + 0.5f;   // 2 dB per segment, from the top down
            auto seg = col.withHeight (segH - 1.5f).translated (0.0f, (float) sIdx * segH);
            g.setColour (lit ? kTrace.interpolatedWith (kGlow, (float) sIdx / segs) : juce::Colour (0xff2a2219).withAlpha (t.light ? 0.25f : 0.8f));
            g.fillRoundedRectangle (seg, 1.0f);
        }
    }
}
