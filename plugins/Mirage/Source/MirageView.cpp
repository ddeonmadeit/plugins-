#include "MirageView.h"

namespace mirage
{
namespace
{
// The colour ramp (intensity 0..1): black-red, deep red, red, red-orange, orange, hot orange, Tarantino yellow.
struct Stop { float t; uint32_t rgb; };
constexpr Stop kStops[] { { 0.00f, 0x050100 }, { 0.16f, 0x1c0402 }, { 0.32f, 0x4f0c05 }, { 0.48f, 0x99160a },
                          { 0.62f, 0xd42c0e }, { 0.76f, 0xfb5213 }, { 0.88f, 0xff7c19 }, { 0.96f, 0xffa51c }, { 1.00f, 0xffc61e } };

std::array<juce::PixelARGB, 1024>& ramp()
{
    static std::array<juce::PixelARGB, 1024> lut = []
    {
        std::array<juce::PixelARGB, 1024> l {};
        for (int i = 0; i < 1024; ++i)
        {
            const float t = (float) i / 1023.0f;
            int k = 0;
            while (k < 7 && t > kStops[k + 1].t)
                ++k;
            const float f = (t - kStops[k].t) / (kStops[k + 1].t - kStops[k].t);
            auto ch = [&] (int shift)
            {
                const float a = (float) ((kStops[k].rgb >> shift) & 0xff), b = (float) ((kStops[k + 1].rgb >> shift) & 0xff);
                return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (a + (b - a) * f));
            };
            l[(size_t) i] = juce::PixelARGB (255, ch (16), ch (8), ch (0));
        }
        return l;
    }();
    return lut;
}

const juce::Colour kOrange { 0xffff6a1f }, kRed { 0xffd2280f }, kDim { 0xff6e1a0b }, kYellow { 0xffffc21e };
const juce::Colour kInk { 0xff0a0302 }, kPanel { 0xff120504 };

inline int lutIndex (float v) noexcept { return juce::jlimit (0, 1023, (int) (v * 1023.0f)); }

juce::String pad (int v, int digits)
{
    auto t = juce::String (std::abs (v)).paddedLeft ('0', digits);
    return v < 0 ? "-" + t : t;
}

juce::String dbText (float g)
{
    const float d = Analyzer::toDb (g);
    if (d < -60.0f)
        return "-INF";
    return juce::String (d, 1);
}

void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width)
{
    // A soft halo and a bright core: phosphor lines.
    g.setColour (c.withMultipliedAlpha (0.28f));
    g.strokePath (p, juce::PathStrokeType (width * 3.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
}
} // namespace

juce::PixelARGB heatColour (float t) noexcept { return ramp()[(size_t) lutIndex (t)]; }
juce::Colour heat (float t) noexcept
{
    const auto p = heatColour (t);
    return juce::Colour (p.getRed(), p.getGreen(), p.getBlue());
}

//==============================================================================
HudDial::HudDial (juce::RangedAudioParameter& p, const juce::String& l)
    : param (p), attachment (p, [this] (float v) { value = param.convertTo0to1 (v); repaint(); }), label (l)
{
    if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (&p))
        dd::gui::tagParam (*this, withId->paramID, l);
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void HudDial::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float dot = std::max (1.0f, r.getWidth() / 46.0f * 1.3f);
    const float size = std::min (r.getWidth(), r.getHeight() - 9.0f * dot) - 4.0f;
    const auto c = juce::Point<float> (r.getCentreX(), r.getY() + 2.0f + size * 0.5f);
    constexpr int ticks = 19;
    for (int i = 0; i < ticks; ++i)
    {
        const float f = (float) i / (float) (ticks - 1);
        const float a = juce::degreesToRadians (-135.0f + 270.0f * f);
        const bool lit = f <= value + 1.0e-3f;
        const juce::Point<float> p0 (c.x + std::sin (a) * size * 0.30f, c.y - std::cos (a) * size * 0.30f);
        const juce::Point<float> p1 (c.x + std::sin (a) * size * 0.47f, c.y - std::cos (a) * size * 0.47f);
        if (lit)
        {
            g.setColour (heat (0.55f + 0.38f * f).withAlpha (0.25f));
            g.drawLine ({ p0, p1 }, 5.0f);
        }
        g.setColour (lit ? heat (0.6f + 0.34f * f) : kDim.withAlpha (0.55f));
        g.drawLine ({ p0, p1 }, 2.0f);
    }
    DotFont::draw (g, juce::String (juce::roundToInt (value * 100.0f)), juce::Rectangle<float> (size, size).withCentre (c), dot * 0.75f,
                   kOrange, juce::Justification::centred);
    DotFont::draw (g, label.toUpperCase(), r.withTop (r.getBottom() - 8.0f * dot), dot * 0.8f,
                   isMouseOverOrDragging() ? kOrange : kRed, juce::Justification::centred, 0.5f);
}

void HudDial::mouseDown (const juce::MouseEvent&) { dragStart = value; attachment.beginGesture(); }
void HudDial::mouseDrag (const juce::MouseEvent& e)
{
    const float v = juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / 180.0f);
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (v));
}
void HudDial::mouseUp (const juce::MouseEvent&) { attachment.endGesture(); }
void HudDial::mouseDoubleClick (const juce::MouseEvent&) { attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); }

//==============================================================================
MirageView::MirageView (MirageProcessor& p) : proc (p)
{
    noise.resize (512 * 512);
    for (auto& n : noise)
        n = rng.nextFloat() * 2.0f - 1.0f;
    expLut.resize (2048);
    for (int i = 0; i < 2048; ++i)
        expLut[(size_t) i] = std::exp (-(float) i / 256.0f);   // exp (-x) for x in [0, 8)

    static const std::pair<const char*, const char*> controls[] {
        { "react", "React" }, { "grain", "Grain" }, { "digital", "Digital" }, { "speed", "Speed" },
        { "eclipse", "Eclipse" }, { "dunes", "Dunes" }, { "heat", "Heat" } };
    for (auto [id, label] : controls)
        addAndMakeVisible (dials.add (new HudDial (*p.apvts.getParameter (id), label)));

    help.setTable ({
        { "react", "How hard everything moves with the sound." },
        { "grain", "Film grain over the whole picture." },
        { "digital", "The screen: pixel mesh, scanlines and readouts over the window." },
        { "speed", "How fast the waveform and the thermal strip scroll." },
        { "eclipse", "How much of the sun the moon covers." },
        { "dunes", "How tall the dunes stand." },
        { "heat", "Hotter: more orange and a little yellow. Cooler: deeper reds." },
    });
    help.onChange = [this] { repaint(); };
    setOpaque (true);
    startTimerHz (60);
}

MirageView::~MirageView() { stopTimer(); }

void MirageView::resized()
{
    s = (float) getHeight() / 440.0f;
    const float m = 10.0f * s;
    auto r = getLocalBounds().toFloat().reduced (m);
    header = r.removeFromTop (26.0f * s);
    r.removeFromTop (6.0f * s);
    window = r.removeFromTop (222.0f * s);
    r.removeFromTop (8.0f * s);
    deck = r.removeFromTop (104.0f * s);
    r.removeFromTop (8.0f * s);
    thermal = r;

    // Deck: fixed instruments and the scrolling waveform taking what is left.
    auto d = deck;
    const float gap = 8.0f * s;
    pLevels = d.removeFromLeft (84.0f * s); d.removeFromLeft (gap);
    pScope = d.removeFromLeft (240.0f * s); d.removeFromLeft (gap);
    pControls = d.removeFromRight (7.0f * 46.0f * s + 16.0f * s); d.removeFromRight (gap);
    const bool roomy = d.getWidth() > 760.0f * s;
    pReticle = roomy ? d.removeFromRight (104.0f * s) : juce::Rectangle<float>();
    if (roomy) d.removeFromRight (gap);
    pSpectrum = d.getWidth() > 520.0f * s ? d.removeFromRight (260.0f * s) : juce::Rectangle<float>();
    if (! pSpectrum.isEmpty()) d.removeFromRight (gap);
    pWave = d;

    auto c = pControls.reduced (8.0f * s, 0.0f).withTrimmedTop (18.0f * s).withTrimmedBottom (6.0f * s);
    const float w = c.getWidth() / (float) dials.size();
    for (int i = 0; i < dials.size(); ++i)
        dials[i]->setBounds (c.withX (c.getX() + w * (float) i).withWidth (w).toNearestInt());

    buildStatics();
}

void MirageView::buildStatics()
{
    const int ww = std::max (8, (int) window.getWidth()), wh = std::max (8, (int) window.getHeight());
    windowImg = juce::Image (juce::Image::ARGB, ww, wh, true, juce::SoftwareImageType());
    crest.assign ((size_t) (Analyzer::kLayers * ww), 0.0f);
    const int tw = std::max (8, (int) thermal.getWidth()), th = std::max (8, (int) thermal.getHeight());
    thermalImg = juce::Image (juce::Image::ARGB, tw, th, true, juce::SoftwareImageType());
    {
        juce::Graphics g (thermalImg);
        g.fillAll (juce::Colour (heatColour (0.03f).getRed(), heatColour (0.03f).getGreen(), heatColour (0.03f).getBlue()));
    }

    // The chassis: dark anodised metal, brushed, in the same reds.
    frameImg = juce::Image (juce::Image::ARGB, std::max (8, getWidth()), std::max (8, getHeight()), true, juce::SoftwareImageType());
    {
        juce::Image::BitmapData bd (frameImg, juce::Image::BitmapData::writeOnly);
        std::vector<float> streak ((size_t) frameImg.getWidth());
        for (auto& v : streak)
            v = rng.nextFloat();
        for (int y = 0; y < frameImg.getHeight(); ++y)
        {
            const float vy = (float) y / (float) frameImg.getHeight();
            for (int x = 0; x < frameImg.getWidth(); ++x)
            {
                const float brush = 0.6f * streak[(size_t) x] + 0.4f * noise[(size_t) (((y * 3) & 511) * 512 + (x & 511))] * 0.5f;
                const float v = 0.085f + 0.03f * brush - 0.025f * vy + 0.02f * noise[(size_t) ((y & 511) * 512 + ((x * 7) & 511))];
                *reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y)) = heatColour (v);
            }
        }
    }
    chromeImg = {};
}

void MirageView::buildChrome (float scale)
{
    // Everything that does not move, drawn once: the chassis, seams, panel bodies, outlines and titles.
    chromeScale = scale;
    chromeImg = juce::Image (juce::Image::ARGB, std::max (8, juce::roundToInt ((float) getWidth() * scale)),
                             std::max (8, juce::roundToInt ((float) getHeight() * scale)), true);
    juce::Graphics g (chromeImg);
    g.addTransform (juce::AffineTransform::scale (scale));
    g.drawImage (frameImg, getLocalBounds().toFloat());
    drawFrame (g);
    drawPanel (g, pLevels, "LEVEL");
    drawPanel (g, pScope, "SCOPE");
    drawPanel (g, pWave, "WAVEFORM");
    if (! pSpectrum.isEmpty())
        drawPanel (g, pSpectrum, "SPECTRUM");
    if (! pReticle.isEmpty())
        drawPanel (g, pReticle, "FIELD");
    drawPanel (g, pControls, "CONTROL");
}

//==============================================================================
void MirageView::timerCallback()
{
    if (! isShowing())
        return;   // a hidden window costs nothing
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = lastTick > 0.0 ? (float) juce::jlimit (0.001, 0.1, now - lastTick) : 1.0f / 60.0f;
    lastTick = now;
    advance (dt);
    repaint();
}

void MirageView::advance (float dt)
{
    an.update (proc.tap, proc.transport.sampleRate.load(), param ("speed"), dt);
    sunPulse += (an.low - sunPulse) * 0.25f;
    scan += dt * (0.12f + 0.25f * param ("speed"));
    scan -= std::floor (scan);
    renderWindow();
    renderThermal();
    ++frames;
}

void MirageView::renderWindow()
{
    const int w = windowImg.getWidth(), h = windowImg.getHeight();
    if (w < 8 || h < 8)
        return;
    const float react = param ("react"), grain = param ("grain"), dunes = param ("dunes"), eclipse = param ("eclipse"), heatAmt = param ("heat");
    const float t = (float) an.time;
    constexpr int K = Analyzer::kLayers;

    const float horizon = (float) h * 0.58f;
    const float R = (float) h * 0.33f * (1.0f + 0.10f * sunPulse * react);
    const float sunX = (float) w * 0.70f, sunY = horizon - R * 0.16f;
    const float moonR = R * 0.955f;
    const float ang = -0.75f + 0.3f * std::sin (t * 0.11f);
    const float moonDist = R * (2.2f * (1.0f - eclipse)) + R * 0.03f * std::sin (t * 0.9f) * an.mid;
    const float moonX = sunX + std::cos (ang) * moonDist, moonY = sunY + std::sin (ang) * moonDist;

    // Ridge lines: the near dune follows the live waveform, the far ones the spectrum moments ago.
    const auto& recent = an.recentL;
    for (int k = 0; k < K; ++k)
    {
        const float d = (float) k / (float) (K - 1);
        const float baseline = horizon + ((float) h - horizon) * std::pow (1.0f - d, 1.25f) * 0.98f + 4.0f * s;
        const float amp = (float) h * 0.46f * (1.0f - 0.55f * d) * (0.35f + dunes);
        const float freq = 1.1f + 2.3f * d;
        const float phase = 0.37f * (float) k + t * (0.006f + 0.003f * (float) k) * (0.6f + param ("speed"));
        float* c = crest.data() + (size_t) k * (size_t) w;
        for (int x = 0; x < w; ++x)
        {
            const float u = (float) x / (float) (w - 1);
            float sx = u * freq + phase;
            sx -= std::floor (sx);
            const float prof = sx < 0.64f ? std::sin (juce::MathConstants<float>::halfPi * sx / 0.64f)
                                          : std::cos (juce::MathConstants<float>::halfPi * (sx - 0.64f) / 0.36f);
            const float roll = 0.5f + 0.5f * std::sin (juce::MathConstants<float>::twoPi * (u * freq * 0.41f + phase * 1.7f));
            float height = amp * (0.62f * prof + 0.26f * roll);
            if (k == 0)
            {
                // The live waveform: a moving maximum of the newest samples, swept across the dune.
                const int i = juce::jlimit (0, Analyzer::kRecent - 33, (int) (u * (float) (Analyzer::kRecent - 33)));
                float m = 0.0f;
                for (int j = 0; j < 32; j += 2)
                    m = std::max (m, std::abs (recent[(size_t) (i + j)]));
                height += (float) h * 0.34f * react * std::sqrt (m);
            }
            else
            {
                const auto& sp = an.layers[(size_t) (k - 1)];
                const float fb = u * (float) (Analyzer::kBands - 1);
                const int b = std::min ((int) fb, Analyzer::kBands - 2);
                const float v = sp[(size_t) b] + (sp[(size_t) b + 1] - sp[(size_t) b]) * (fb - (float) b);
                height += amp * 0.9f * react * v * v;
            }
            c[x] = baseline - height;
        }
        // Wind smooths the sand: two box passes so the ridges read as dunes, not as a jagged graph.
        const int radius = std::max (2, (int) ((float) w * (0.004f + 0.006f * d)));
        for (int pass = 0; pass < 2; ++pass)
        {
            smooth.resize ((size_t) w + 1);
            smooth[0] = 0.0f;
            for (int x = 0; x < w; ++x)
                smooth[(size_t) x + 1] = smooth[(size_t) x] + c[x];
            for (int x = 0; x < w; ++x)
            {
                const int a = std::max (0, x - radius), b = std::min (w, x + radius + 1);
                c[x] = (smooth[(size_t) b] - smooth[(size_t) a]) / (float) (b - a);
            }
        }
    }

    // Per column, all layers side by side (the pixel loop walks them together): ridge, how much the face
    // turns to the sun, the highest ridge, and the sun's warmth on the sand below it.
    colCrest.resize ((size_t) (w * K));
    colLit.resize ((size_t) (w * K));
    colTop.resize ((size_t) w);
    colGlow.resize ((size_t) w);
    for (int x = 0; x < w; ++x)
    {
        float top = 1.0e9f;
        for (int k = 0; k < K; ++k)
        {
            const float* c = crest.data() + (size_t) k * (size_t) w;
            const int span = std::max (2, w / 160);
            const float slope = (c[std::min (x + span, w - 1)] - c[std::max (x - span, 0)]) / (2.0f * (float) span);
            colCrest[(size_t) (x * K + k)] = c[x];
            colLit[(size_t) (x * K + k)] = juce::jlimit (0.0f, 1.0f, slope * ((float) x < sunX ? 2.2f : -2.2f) / s);
            top = std::min (top, c[x]);
        }
        colTop[(size_t) x] = top;
        colGlow[(size_t) x] = 0.20f * std::exp (-std::abs ((float) x - sunX) / (0.22f * (float) w));
    }
    shimSin.resize ((size_t) w);
    shimCos.resize ((size_t) w);
    for (int x = 0; x < w; ++x)
    {
        shimSin[(size_t) x] = std::sin ((float) x * 0.013f);
        shimCos[(size_t) x] = std::cos ((float) x * 0.013f);
    }
    std::array<float, K> invShade {};
    for (int k = 0; k < K; ++k)
        invShade[(size_t) k] = 256.0f / ((9.0f + 20.0f * (1.0f - (float) k / (float) (K - 1))) * s);

    juce::Image::BitmapData bd (windowImg, juce::Image::BitmapData::writeOnly);
    const auto& lut = ramp();
    const int ox = rng.nextInt (512), oy = rng.nextInt (512), ox2 = rng.nextInt (512), oy2 = rng.nextInt (512);
    const float heatGain = 0.86f + 0.28f * heatAmt;
    const float glowK = 0.55f + 0.45f * an.energy;
    // The screen-door mesh of a phosphor display, baked in: every third row and column a little darker.
    const int pitch = std::max (2, juce::roundToInt (3.0f * s));
    const float meshDark = 1.0f - 0.32f * param ("digital");
    meshCol.resize ((size_t) w);
    for (int x = 0; x < w; ++x)
        meshCol[(size_t) x] = (x % pitch) == 0 ? meshDark : 1.0f;
    const float ringR = R * (1.08f + 0.5f * (1.0f - an.hit));
    const float sunBox = R * 2.8f;
    auto ex = [this] (float x) { return expLut[(size_t) juce::jlimit (0, 2047, (int) (x * 256.0f))]; };

    for (int y = 0; y < h; ++y)
    {
        const float fy = (float) y;
        const float sky = 0.035f + 0.31f * std::pow (std::min (1.0f, fy / horizon), 2.2f);
        const float shimmerAmp = juce::jlimit (0.0f, 1.0f, 1.0f - (horizon - fy) / ((float) h * 0.22f)) * 2.6f * s;
        const float rowPhase = fy * 0.35f + t * 4.0f;
        const float rowSin = std::sin (rowPhase) * shimmerAmp, rowCos = std::cos (rowPhase) * shimmerAmp;
        const float vy = 1.0f - 0.35f * std::pow (std::abs (fy / (float) h - 0.45f) * 2.0f, 2.0f);
        const float* nrow = noise.data() + (size_t) (((y + oy) & 511) * 512);
        const float* nrow2 = noise.data() + (size_t) ((((y >> 1) + oy2) & 511) * 512);
        const float rowMesh = (y % pitch) == 0 ? meshDark : 1.0f;
        auto* px = reinterpret_cast<juce::PixelARGB*> (bd.getLinePointer (y));
        for (int x = 0; x < w; ++x)
        {
            int layer = -1;
            if (fy >= colTop[(size_t) x])
            {
                const float* cc = colCrest.data() + (size_t) (x * K);
                for (int k = 0; k < K; ++k)
                    if (cc[k] <= fy) { layer = k; break; }
            }

            float v;
            if (layer < 0)
            {
                v = sky;
                const float dy = fy - sunY;
                const float dx0 = (float) x - sunX;
                if (std::abs (dx0) < sunBox && std::abs (dy) < sunBox)
                {
                    // Heat shimmer near the horizon: sin (row + x) by angle addition, no per-pixel sine.
                    const float dx = dx0 + rowSin * shimCos[(size_t) x] + rowCos * shimSin[(size_t) x];
                    const float dist = std::sqrt (dx * dx + dy * dy);
                    const float outside = sky + 0.30f * glowK * ex ((dist - R) / (0.32f * R))
                                        + 0.22f * an.hit * ex (std::abs (dist - ringR) / (0.025f * R));
                    if (dist < R + 1.5f)
                    {
                        const float r = dist / R;
                        const float r2 = r * r;
                        float inside = 0.60f + 0.35f * std::pow (std::max (0.0f, 1.0f - r2), 1.6f) - 0.10f * r2 * r2 * r2;
                        // The moon: a dark disc with the sun's light glowing around its rim.
                        const float mx = (float) x - moonX, my = fy - moonY;
                        const float dm = std::sqrt (mx * mx + my * my);
                        if (dm < moonR + 2.0f)
                        {
                            const float cover = juce::jlimit (0.0f, 1.0f, (moonR - dm) / (0.03f * R));
                            const float rim = 0.42f * ex ((moonR - dm) / (0.03f * R));
                            inside += (0.05f + rim - inside) * cover;
                        }
                        const float edge = juce::jlimit (0.0f, 1.0f, (R - dist) / 1.5f + 0.5f);
                        v = outside + (inside - outside) * edge;
                    }
                    else
                        v = outside;
                }
            }
            else
            {
                const float d = (float) layer / (float) (K - 1);
                const float below = fy - colCrest[(size_t) (x * K + layer)];
                // Sand in low sun: faces turned to the sun glow orange, the lee sides fall into deep red shadow,
                // far dunes sit in the haze, every crest catches a line of light.
                const float lit = colLit[(size_t) (x * K + layer)];
                const float shade = expLut[(size_t) std::min (2047, (int) (below * invShade[(size_t) layer]))];
                const float glow = colGlow[(size_t) x] * (0.4f + 0.6f * shade) * (1.0f - 0.3f * d);
                const float rim = below < 1.8f * s ? (0.34f + (layer == 0 ? 0.4f * an.energy : 0.14f)) * (1.0f - below / (1.8f * s)) : 0.0f;
                // Below the lit band the sand keeps a little of the light, so faces read as surfaces, not streaks.
                v = 0.13f + 0.17f * d + (0.28f + 0.12f * d) * lit * shade + 0.09f * shade + glow + rim;
            }
            // Grain: fine speckle plus coarser mottling, like a risograph print.
            const float n1 = nrow[(x + ox) & 511], n2 = nrow2[((x >> 1) + ox2) & 511];
            v = (v * (1.0f + grain * 0.22f * n2) + grain * (0.085f * n1 + 0.05f * n2)) * heatGain * vy;
            v *= std::min (rowMesh, meshCol[(size_t) x]);
            px[x] = lut[(size_t) lutIndex (v)];
        }
    }
}

void MirageView::renderThermal()
{
    const int w = thermalImg.getWidth(), h = thermalImg.getHeight();
    if (w < 8 || h < 8)
        return;
    thermalCarry += 0.6f + 2.2f * param ("speed");
    const int step = std::min (w, (int) thermalCarry);
    thermalCarry -= (float) step;
    if (step <= 0)
        return;
    thermalImg.moveImageSection (0, 0, step, 0, w - step, h);
    juce::Image::BitmapData bd (thermalImg, juce::Image::BitmapData::writeOnly);
    const auto& lut = ramp();
    const float grain = param ("grain"), heatGain = 0.86f + 0.28f * param ("heat");
    for (int y = 0; y < h; ++y)
    {
        const float fb = (1.0f - (float) y / (float) (h - 1)) * (float) (Analyzer::kBands - 1);
        const int b = std::min ((int) fb, Analyzer::kBands - 2);
        const float v = an.bandFast[(size_t) b] + (an.bandFast[(size_t) b + 1] - an.bandFast[(size_t) b]) * (fb - (float) b);
        for (int x = w - step; x < w; ++x)
        {
            const float n = noise[(size_t) ((y & 511) * 512 + ((x + frames * 13) & 511))];
            const float tv = (0.05f + 0.93f * std::pow (v, 1.35f)) * heatGain + grain * 0.06f * n;
            *reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y)) = lut[(size_t) lutIndex (tv)];
        }
    }
}

//==============================================================================
void MirageView::paint (juce::Graphics& g)
{
    if (frames == 0)
        for (int i = 0; i < 4; ++i)
            advance (1.0f / 60.0f);   // a first picture straight away (and for headless snapshots)
    const float physical = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (chromeImg.isNull() || std::abs (physical - chromeScale) > 0.01f)
        buildChrome (physical);
    g.setOpacity (1.0f);
    g.drawImage (chromeImg, getLocalBounds().toFloat());
    drawHeader (g);

    // The window.
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (window, 9.0f * s);
        g.reduceClipRegion (clip);
        g.setOpacity (1.0f);   // images take the current colour's opacity
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (windowImg, window);
        drawWindowHud (g);
    }

    drawLevels (g, pLevels);
    drawScope (g, pScope);
    drawWave (g, pWave);
    if (! pSpectrum.isEmpty())
        drawSpectrum (g, pSpectrum);
    if (! pReticle.isEmpty())
        drawReticle (g, pReticle);

    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (thermal, 7.0f * s);
        g.reduceClipRegion (clip);
        g.setOpacity (1.0f);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (thermalImg, thermal);
        drawThermalHud (g);
    }
    glowStroke (g, [&] { juce::Path p; p.addRoundedRectangle (thermal, 7.0f * s); return p; }(), kRed.withAlpha (0.85f), 1.2f * s);
}

void MirageView::drawFrame (juce::Graphics& g)
{
    // Machined seams around the window and between the deck and the thermal strip, with a warm edge light.
    const auto b = getLocalBounds().toFloat();
    g.setColour (kInk.withAlpha (0.7f));
    g.drawRect (b, 2.0f * s);
    g.setColour (kRed.withAlpha (0.18f));
    g.drawLine (b.getX() + 6.0f, b.getY() + 1.5f, b.getRight() - 6.0f, b.getY() + 1.5f, 1.0f);
    for (auto r : { window.expanded (4.0f * s), thermal.expanded (3.0f * s) })
    {
        g.setColour (kInk.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 11.0f * s);
        g.setColour (kRed.withAlpha (0.14f));
        g.drawRoundedRectangle (r.translated (0.0f, 1.0f), 11.0f * s, 1.0f);
    }
}

void MirageView::drawHeader (juce::Graphics& g)
{
    const float dot = 2.4f * s;
    auto h = header;
    DotFont::draw (g, "DEON", h, dot, kOrange, juce::Justification::centredLeft);

    // Transport readouts.
    const double bpm = proc.transport.bpm.load(), ppq = std::max (0.0, proc.transport.ppq.load());
    const bool playing = proc.transport.playing.load();
    const int bar = (int) std::floor (ppq / 4.0) + 1, beat = (int) std::floor (std::fmod (ppq, 4.0)) + 1;
    const double secs = ppq * 60.0 / std::max (1.0, bpm);
    const int mm = (int) (secs / 60.0), ss = (int) std::fmod (secs, 60.0), tenths = (int) (std::fmod (secs, 1.0) * 10.0);
    const auto line = "BPM " + juce::String (bpm, 1) + "   BAR " + pad (bar, 3) + "." + juce::String (beat) + "   " + pad (mm, 2) + ":" + pad (ss, 2) + "." + juce::String (tenths);
    DotFont::draw (g, line, h.withTrimmedRight (40.0f * s), 1.5f * s, kOrange, juce::Justification::centredRight, 0.8f);
    const bool blink = playing ? std::fmod (an.time, 1.0) < 0.6 : true;
    const auto lamp = juce::Rectangle<float> (9.0f * s, 9.0f * s).withCentre ({ h.getRight() - 14.0f * s, h.getCentreY() });
    g.setColour ((playing ? kOrange : kDim).withAlpha (blink ? 1.0f : 0.25f));
    g.fillEllipse (lamp);
    g.setColour (kOrange.withAlpha (playing && blink ? 0.3f : 0.0f));
    g.fillEllipse (lamp.expanded (4.0f * s));
}

void MirageView::drawWindowHud (juce::Graphics& g)
{
    const float digital = param ("digital");
    const auto w = window;
    const float t = (float) an.time;

    if (digital > 0.01f)
    {
        // A slow bright scanline and a sweeping scan bar.
        const float sy = w.getY() + w.getHeight() * (float) std::fmod (an.time * 0.07, 1.0);
        g.setGradientFill (juce::ColourGradient (kOrange.withAlpha (0.0f), 0.0f, sy - 18.0f * s, kOrange.withAlpha (0.06f * digital), 0.0f, sy, false));
        g.fillRect (w.getX(), sy - 18.0f * s, w.getWidth(), 18.0f * s);
        const float sx = w.getX() + w.getWidth() * scan;
        g.setColour (kOrange.withAlpha (0.10f * digital));
        g.fillRect (sx - 1.0f * s, w.getY(), 2.0f * s, w.getHeight());
        g.setColour (kOrange.withAlpha (0.04f * digital));
        g.fillRect (sx - 30.0f * s, w.getY(), 30.0f * s, w.getHeight());

        // Crosshair marks across the sky.
        g.setColour (kOrange.withAlpha (0.22f * digital));
        const float stepX = 64.0f * s, stepY = 44.0f * s, arm = 3.0f * s;
        for (float y = w.getY() + 24.0f * s; y < w.getY() + w.getHeight() * 0.42f; y += stepY)
            for (float x = w.getX() + 32.0f * s; x < w.getRight() - 16.0f * s; x += stepX)
            {
                g.fillRect (x - arm, y - 0.5f * s, 2.0f * arm, 1.0f * s);
                g.fillRect (x - 0.5f * s, y - arm, 1.0f * s, 2.0f * arm);
            }
    }

    // Sun reticle: a broken ring that turns, ticks, a readout on a leader line.
    const float h = w.getHeight();
    const float R = h * 0.33f * (1.0f + 0.10f * sunPulse * param ("react"));
    const juce::Point<float> sun (w.getX() + w.getWidth() * 0.70f, w.getY() + h * 0.58f - R * 0.16f);
    {
        juce::Path ring;
        const float rr = R * 1.22f;
        for (int i = 0; i < 6; ++i)
        {
            const float a0 = t * 0.25f + (float) i * juce::MathConstants<float>::twoPi / 6.0f;
            ring.addCentredArc (sun.x, sun.y, rr, rr, 0.0f, a0, a0 + 0.62f, true);
        }
        glowStroke (g, ring, kOrange.withAlpha (0.55f + 0.3f * an.energy), 1.1f * s);
        for (int i = 0; i < 36; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::twoPi / 36.0f - t * 0.1f;
            const float r0 = R * 1.30f, r1 = R * ((i % 3) == 0 ? 1.38f : 1.34f);
            g.setColour (kRed.withAlpha (0.6f));
            g.drawLine (sun.x + std::cos (a) * r0, sun.y + std::sin (a) * r0, sun.x + std::cos (a) * r1, sun.y + std::sin (a) * r1, 1.0f * s);
        }
        const juce::Point<float> tip (sun.x - R * 0.95f, sun.y - R * 0.95f);
        const juce::Point<float> elbow (tip.x - 30.0f * s, tip.y - 22.0f * s);
        juce::Path leader;
        leader.startNewSubPath (tip);
        leader.lineTo (elbow);
        leader.lineTo (elbow.x - 70.0f * s, elbow.y);
        glowStroke (g, leader, kOrange.withAlpha (0.8f), 0.9f * s);
        g.setColour (kOrange);
        g.drawEllipse (juce::Rectangle<float> (6.0f * s, 6.0f * s).withCentre (tip), 1.0f * s);
        DotFont::draw (g, "SOLAR " + pad (juce::roundToInt (sunPulse * 100.0f), 3) + "%",
                       juce::Rectangle<float> (120.0f * s, 12.0f * s).withPosition (elbow.x - 70.0f * s, elbow.y - 14.0f * s), 1.3f * s, kOrange);
    }

    // Peak marker on the near dune, low-end marker over the far left dunes.
    {
        const int iw = windowImg.getWidth();
        const float* c0 = crest.data();
        int best = 0;
        for (int x = iw / 10; x < iw * 6 / 10; ++x)
            if (c0[x] < c0[best] || best < iw / 10)
                best = x;
        const float scaleX = w.getWidth() / (float) iw, scaleY = w.getHeight() / (float) windowImg.getHeight();
        const juce::Point<float> tip (w.getX() + (float) best * scaleX, w.getY() + c0[best] * scaleY);
        const juce::Point<float> elbow (tip.x + 26.0f * s, tip.y - 30.0f * s);
        juce::Path leader;
        leader.startNewSubPath (tip);
        leader.lineTo (elbow);
        leader.lineTo (elbow.x + 86.0f * s, elbow.y);
        glowStroke (g, leader, kOrange.withAlpha (0.8f), 0.9f * s);
        g.setColour (kOrange);
        g.drawEllipse (juce::Rectangle<float> (6.0f * s, 6.0f * s).withCentre (tip), 1.0f * s);
        DotFont::draw (g, "PEAK " + dbText (std::max (an.holdL, an.holdR)) + " DB",
                       juce::Rectangle<float> (120.0f * s, 12.0f * s).withPosition (elbow.x + 4.0f * s, elbow.y - 14.0f * s), 1.3f * s, kOrange);

        const int lowX = iw / 12;
        const float* c1 = crest.data() + (size_t) (2 * iw);
        const juce::Point<float> tip2 (w.getX() + (float) lowX * scaleX, w.getY() + c1[lowX] * scaleY);
        const juce::Point<float> elbow2 (tip2.x + 18.0f * s, tip2.y - 38.0f * s);
        juce::Path leader2;
        leader2.startNewSubPath (tip2);
        leader2.lineTo (elbow2);
        leader2.lineTo (elbow2.x + 96.0f * s, elbow2.y);
        glowStroke (g, leader2, kRed.withAlpha (0.9f), 0.9f * s);
        g.setColour (kRed);
        g.drawEllipse (juce::Rectangle<float> (6.0f * s, 6.0f * s).withCentre (tip2), 1.0f * s);
        DotFont::draw (g, "LOW END " + pad (juce::roundToInt (an.low * 100.0f), 3) + "%  " + juce::String (juce::roundToInt (an.lowHz)) + " HZ",
                       juce::Rectangle<float> (170.0f * s, 12.0f * s).withPosition (elbow2.x + 4.0f * s, elbow2.y - 14.0f * s), 1.3f * s, kRed);
    }

    // The live waveform traced along the near dune's ridge.
    {
        const int iw = windowImg.getWidth();
        const float scaleX = w.getWidth() / (float) iw, scaleY = w.getHeight() / (float) windowImg.getHeight();
        juce::Path wave;
        const int n = 360;
        for (int i = 0; i <= n; ++i)
        {
            const float u = (float) i / (float) n;
            const int x = juce::jlimit (0, iw - 1, (int) (u * (float) (iw - 1)));
            const float sample = an.recentL[(size_t) (Analyzer::kRecent - 1440 + i * 4)];
            const juce::Point<float> p (w.getX() + (float) x * scaleX, w.getY() + crest[(size_t) x] * scaleY - 6.0f * s - sample * 26.0f * s * param ("react"));
            if (i == 0)
                wave.startNewSubPath (p);
            else
                wave.lineTo (p);
        }
        glowStroke (g, wave, kOrange.withAlpha (0.85f), 1.1f * s);
    }

    // Status block, top left: what the panel is doing, or what the control under the mouse does.
    {
        auto area = juce::Rectangle<float> (w.getX() + 16.0f * s, w.getY() + 14.0f * s, w.getWidth() * 0.45f, 12.0f * s);
        DotFont::draw (g, "> SCAN " + juce::String (proc.transport.playing.load() ? "LIVE" : "IDLE") + "   SR " + juce::String (juce::roundToInt (proc.transport.sampleRate.load())) + " HZ",
                       area, 1.3f * s, kOrange);
        area.translate (0.0f, 14.0f * s);
        if (help.isActive())
            DotFont::draw (g, "> " + help.getTitle().toUpperCase() + ": " + help.getText().toUpperCase(), area.withWidth (w.getWidth() - 32.0f * s), 1.3f * s, kYellow.interpolatedWith (kOrange, 0.5f));
        else
            DotFont::draw (g, "> ENERGY " + pad (juce::roundToInt (an.energy * 100.0f), 3) + "%   HIGH " + pad (juce::roundToInt (an.high * 100.0f), 3) + "%",
                           area, 1.3f * s, kRed);
    }

    // Corner brackets and the glowing bezel.
    {
        juce::Path br;
        const float L = 18.0f * s, in = 7.0f * s;
        const auto r = w.reduced (in);
        br.startNewSubPath (r.getX(), r.getY() + L); br.lineTo (r.getX(), r.getY()); br.lineTo (r.getX() + L, r.getY());
        br.startNewSubPath (r.getRight() - L, r.getY()); br.lineTo (r.getRight(), r.getY()); br.lineTo (r.getRight(), r.getY() + L);
        br.startNewSubPath (r.getX(), r.getBottom() - L); br.lineTo (r.getX(), r.getBottom()); br.lineTo (r.getX() + L, r.getBottom());
        br.startNewSubPath (r.getRight() - L, r.getBottom()); br.lineTo (r.getRight(), r.getBottom()); br.lineTo (r.getRight(), r.getBottom() - L);
        glowStroke (g, br, kOrange.withAlpha (0.7f), 1.2f * s);
        juce::Path bezel;
        bezel.addRoundedRectangle (w.reduced (0.5f), 9.0f * s);
        glowStroke (g, bezel, kOrange.withAlpha (0.9f), 1.3f * s);
    }
}

void MirageView::drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    if (r.isEmpty())
        return;
    g.setColour (kPanel.withAlpha (0.92f));
    g.fillRoundedRectangle (r, 8.0f * s);
    juce::Path p;
    p.addRoundedRectangle (r.reduced (0.5f), 8.0f * s);
    glowStroke (g, p, kOrange.withAlpha (0.75f), 1.0f * s);
    DotFont::draw (g, title, juce::Rectangle<float> (r.getX() + 10.0f * s, r.getY() + 5.0f * s, r.getWidth(), 9.0f * s), 1.15f * s, kOrange, juce::Justification::centredLeft, 0.6f);
}

void MirageView::drawLevels (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto in = r.reduced (10.0f * s).withTrimmedTop (12.0f * s).withTrimmedBottom (12.0f * s);
    const int segs = 22;
    const float segH = in.getHeight() / (float) segs;
    auto bar = [&] (juce::Rectangle<float> col, float peak, float hold)
    {
        const float lvl = juce::jlimit (0.0f, 1.0f, (Analyzer::toDb (peak) + 54.0f) / 54.0f);
        const int holdSeg = (int) (juce::jlimit (0.0f, 1.0f, (Analyzer::toDb (hold) + 54.0f) / 54.0f) * (float) segs);
        for (int i = 0; i < segs; ++i)
        {
            const float f = (float) i / (float) (segs - 1);
            const auto seg = juce::Rectangle<float> (col.getX(), col.getBottom() - (float) (i + 1) * segH, col.getWidth(), segH * 0.68f);
            const bool on = f <= lvl || i == holdSeg - 1;
            g.setColour (on ? heat (0.55f + 0.42f * f) : kDim.withAlpha (0.35f));
            g.fillRect (seg);
        }
    };
    const float cw = in.getWidth() * 0.32f;
    bar (in.withWidth (cw), an.peakL, an.holdL);
    bar (in.withX (in.getX() + in.getWidth() * 0.4f).withWidth (cw), an.peakR, an.holdR);
    DotFont::draw (g, "L", in.withWidth (cw).withY (in.getBottom() + 2.0f * s).withHeight (9.0f * s), 1.0f * s, kRed, juce::Justification::centred, 0.4f);
    DotFont::draw (g, "R", in.withX (in.getX() + in.getWidth() * 0.4f).withWidth (cw).withY (in.getBottom() + 2.0f * s).withHeight (9.0f * s), 1.0f * s, kRed, juce::Justification::centred, 0.4f);
    // Scale ticks with the loudest reading.
    const auto text = dbText (std::max (an.holdL, an.holdR));
    DotFont::draw (g, text, juce::Rectangle<float> (in.getRight() - in.getWidth() * 0.22f, in.getY(), in.getWidth() * 0.22f, 10.0f * s), 0.9f * s, kOrange, juce::Justification::centredRight, 0.4f);
}

void MirageView::drawScope (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto in = r.reduced (10.0f * s).withTrimmedTop (10.0f * s);
    g.setColour (kRed.withAlpha (0.25f));
    g.drawHorizontalLine ((int) in.getCentreY(), in.getX(), in.getRight());
    for (int i = 1; i < 6; ++i)
    {
        const float x = in.getX() + in.getWidth() * (float) i / 6.0f;
        g.setColour (kRed.withAlpha (0.25f));
        g.fillRect (x - 2.0f * s, in.getCentreY() - 0.5f * s, 4.0f * s, 1.0f * s);
        g.fillRect (x - 0.5f * s, in.getCentreY() - 2.0f * s, 1.0f * s, 4.0f * s);
    }
    // Trigger on a rising zero crossing so the trace stands still on steady tones.
    const auto& L = an.recentL;
    const auto& Rr = an.recentR;
    const int span = 1024;
    int start = Analyzer::kRecent - span - 1;
    for (int i = Analyzer::kRecent - span - 1; i > Analyzer::kRecent - span - 1200 && i > 1; --i)
        if (L[(size_t) (i - 1)] < 0.0f && L[(size_t) i] >= 0.0f) { start = i; break; }
    const float peak = std::max (0.05f, std::max (an.peakL, an.peakR));
    for (int ch = 1; ch >= 0; --ch)
    {
        const auto& src = ch == 0 ? L : Rr;
        juce::Path p;
        for (int i = 0; i < span; i += 2)
        {
            const float x = in.getX() + in.getWidth() * (float) i / (float) span;
            const float y = in.getCentreY() - src[(size_t) (start + i)] / peak * in.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        glowStroke (g, p, ch == 0 ? kOrange : kRed.withAlpha (0.8f), 1.0f * s);
    }
}

void MirageView::drawWave (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto in = r.reduced (10.0f * s).withTrimmedTop (10.0f * s);
    const int cols = (int) (in.getWidth() / (2.0f * s));
    const float colW = in.getWidth() / (float) std::max (1, cols);
    const float mid = in.getCentreY(), half = in.getHeight() * 0.5f;
    constexpr int buckets = 12;
    std::array<juce::RectangleList<float>, buckets> lists;
    for (int i = 0; i < cols; ++i)
    {
        const int idx = (an.historyHead - cols + i + Analyzer::kHistory * 4) % Analyzer::kHistory;
        const auto& c = an.history[(size_t) idx];
        const float top = std::max (c.maxL, c.maxR), bottom = std::min (c.minL, c.minR);
        const float amp = std::max (top, -bottom);
        const float age = (float) i / (float) cols;
        const int b = juce::jlimit (0, buckets - 1, (int) (std::sqrt (amp) * (0.5f + 0.5f * age) * (float) buckets));
        lists[(size_t) b].addWithoutMerging ({ in.getX() + (float) i * colW, mid - top * half, std::max (1.0f, colW * 0.7f), std::max (1.0f, (top - bottom) * half) });
    }
    for (int b = 0; b < buckets; ++b)
    {
        g.setColour (heat (0.38f + 0.52f * ((float) b + 0.5f) / (float) buckets));
        g.fillRectList (lists[(size_t) b]);
    }
    // Now line at the right edge, time ticks along the bottom.
    g.setColour (kOrange.withAlpha (0.8f));
    g.fillRect (in.getRight() - 1.0f * s, in.getY(), 1.5f * s, in.getHeight());
    g.setColour (kRed.withAlpha (0.4f));
    for (float x = in.getRight(); x > in.getX(); x -= 60.0f * s)
        g.fillRect (x, in.getBottom() - 3.0f * s, 1.0f * s, 3.0f * s);
}

void MirageView::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto in = r.reduced (10.0f * s).withTrimmedTop (10.0f * s);
    constexpr int n = Analyzer::kLed, segs = 14;
    const float cw = in.getWidth() / (float) n, sh = in.getHeight() / (float) segs;
    std::array<juce::RectangleList<float>, segs> lit;
    juce::RectangleList<float> off;
    for (int i = 0; i < n; ++i)
    {
        const int on = (int) std::round (an.led[(size_t) i] * (float) segs);
        const int peak = (int) std::round (an.ledPeak[(size_t) i] * (float) segs);
        for (int j = 0; j < segs; ++j)
        {
            const juce::Rectangle<float> seg (in.getX() + (float) i * cw, in.getBottom() - (float) (j + 1) * sh, cw * 0.72f, sh * 0.62f);
            (j < on || j == peak - 1 ? lit[(size_t) j] : off).addWithoutMerging (seg);
        }
    }
    g.setColour (kDim.withAlpha (0.25f));
    g.fillRectList (off);
    for (int j = 0; j < segs; ++j)
    {
        g.setColour (heat (0.5f + 0.45f * (float) j / (float) (segs - 1)));
        g.fillRectList (lit[(size_t) j]);
    }
}

void MirageView::drawReticle (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto in = r.reduced (8.0f * s).withTrimmedTop (10.0f * s);
    const float rad = std::min (in.getWidth(), in.getHeight()) * 0.46f;
    const auto c = in.getCentre();
    const float t = (float) an.time;
    juce::Path ring;
    for (int i = 0; i < 4; ++i)
    {
        const float a0 = -t * 0.6f + (float) i * juce::MathConstants<float>::halfPi;
        ring.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a0 + 1.1f, true);
    }
    glowStroke (g, ring, kOrange.withAlpha (0.8f), 1.0f * s);
    g.setColour (kRed.withAlpha (0.5f));
    g.drawEllipse (juce::Rectangle<float> (rad * 1.5f, rad * 1.5f).withCentre (c), 1.0f * s);
    g.drawLine (c.x - rad, c.y, c.x + rad, c.y, 0.6f * s);
    g.drawLine (c.x, c.y - rad, c.x, c.y + rad, 0.6f * s);
    // Goniometer: the stereo field as dots (mono stands up, wide spreads out).
    const float scale = rad * 0.7f / std::max (0.05f, std::max (an.peakL, an.peakR));
    juce::RectangleList<float> dots;
    for (int i = Analyzer::kRecent - 768; i < Analyzer::kRecent; i += 3)
    {
        const float l = an.recentL[(size_t) i], rr = an.recentR[(size_t) i];
        const float x = (rr - l) * 0.7071f * scale, y = -(l + rr) * 0.7071f * scale;
        if (x * x + y * y < rad * rad)
            dots.addWithoutMerging ({ c.x + x, c.y + y, 1.4f * s, 1.4f * s });
    }
    g.setColour (kOrange.withAlpha (0.75f));
    g.fillRectList (dots);
    // The core glows with the energy.
    const float core = rad * (0.18f + 0.22f * an.energy);
    g.setGradientFill (juce::ColourGradient (heat (0.7f + 0.25f * an.hit).withAlpha (0.8f), c, kRed.withAlpha (0.0f), c.translated (core, 0.0f), true));
    g.fillEllipse (juce::Rectangle<float> (core * 2.0f, core * 2.0f).withCentre (c));
}

void MirageView::drawThermalHud (juce::Graphics& g)
{
    const auto t = thermal;
    DotFont::draw (g, "THERMAL", juce::Rectangle<float> (t.getX() + 10.0f * s, t.getY() + 5.0f * s, 120.0f * s, 9.0f * s), 1.15f * s, kOrange, juce::Justification::centredLeft, 0.7f);
    static const float marks[] { 60.0f, 250.0f, 1000.0f, 4000.0f, 12000.0f };
    static const char* labels[] { "60", "250", "1K", "4K", "12K" };
    for (int i = 0; i < 5; ++i)
    {
        const float f = std::log (marks[i] / 30.0f) / std::log (16000.0f / 30.0f);
        const float y = t.getBottom() - f * t.getHeight();
        g.setColour (kOrange.withAlpha (0.5f));
        g.fillRect (t.getRight() - 8.0f * s, y, 6.0f * s, 1.0f * s);
        DotFont::draw (g, labels[i], juce::Rectangle<float> (t.getRight() - 46.0f * s, y - 5.0f * s, 34.0f * s, 10.0f * s), 0.9f * s, kOrange, juce::Justification::centredRight, 0.5f);
    }
    const float cx = t.getX() + t.getWidth() * scan;
    g.setColour (kOrange.withAlpha (0.25f));
    g.fillRect (cx, t.getY(), 1.0f * s, t.getHeight());
}

} // namespace mirage
