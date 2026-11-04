#if __has_include(<DDFonts.h>)
 #include <DDFonts.h>
 #define DD_HAS_EMBEDDED_FONTS 1
#else
 #define DD_HAS_EMBEDDED_FONTS 0
#endif

namespace dd::gui
{
/** The embedded typefaces. Deleted with JUCE's other shutdown objects (before the font engine goes away),
    not by static destruction at unload, which can run after it in a plugin host. */
struct EmbeddedFonts : private juce::DeletedAtShutdown
{
    juce::Typeface::Ptr regular, medium, semiBold, bold;

    ~EmbeddedFonts() override { clearSingletonInstance(); }
    JUCE_DECLARE_SINGLETON_INLINE (EmbeddedFonts, false)

    EmbeddedFonts()
    {
       #if DD_HAS_EMBEDDED_FONTS
        regular = juce::Typeface::createSystemTypefaceFor (DDFonts::InstrumentSansRegular_ttf, (size_t) DDFonts::InstrumentSansRegular_ttfSize);
        medium = juce::Typeface::createSystemTypefaceFor (DDFonts::InstrumentSansMedium_ttf, (size_t) DDFonts::InstrumentSansMedium_ttfSize);
        semiBold = juce::Typeface::createSystemTypefaceFor (DDFonts::InstrumentSansSemiBold_ttf, (size_t) DDFonts::InstrumentSansSemiBold_ttfSize);
        bold = juce::Typeface::createSystemTypefaceFor (DDFonts::InstrumentSansBold_ttf, (size_t) DDFonts::InstrumentSansBold_ttfSize);
       #endif
    }
};

namespace
{
    const EmbeddedFonts& fonts() { return *EmbeddedFonts::getInstance(); }

    juce::Colour rgba (int r, int g, int b, float a) { return juce::Colour ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b, a); }

    Tokens makeDark()
    {
        Tokens t;
        t.light = false;
        t.room = juce::Colour (0xff110f0d);        t.roomEdge = juce::Colour (0xff232120);
        t.chassisTop = juce::Colour (0xff322f2a);  t.chassisMid = juce::Colour (0xff2b2823); t.chassisBot = juce::Colour (0xff262320);
        t.railTop = juce::Colour (0xff403d37);     t.railMid = juce::Colour (0xff363330);    t.railBot = juce::Colour (0xff302d29);
        t.metalTop = juce::Colour (0xff36332f);    t.metalMid = juce::Colour (0xff2d2b28);   t.metalBot = juce::Colour (0xff262320);
        t.metalEdge = rgba (255, 248, 235, 0.10f);
        t.faceTop = juce::Colour (0xff3c3a35);     t.faceBot = juce::Colour (0xff252320);
        t.keyTop = rgba (255, 248, 235, 0.16f);    t.keySheen = rgba (255, 248, 235, 0.05f);
        t.keyUnder = rgba (0, 0, 0, 0.50f);        t.keyBotline = rgba (0, 0, 0, 0.65f);     t.keyWall = juce::Colour (0xff141008);
        t.trayTop = juce::Colour (0xff0e0d0b);     t.trayBot = juce::Colour (0xff161412);
        t.trayRecess = rgba (0, 0, 0, 0.90f);      t.trayLip = rgba (255, 248, 235, 0.08f);
        t.edgeLight = rgba (255, 248, 235, 0.06f); t.edgeDark = rgba (0, 0, 0, 0.45f);
        t.castSoft = rgba (0, 0, 0, 0.60f);        t.castMid = rgba (0, 0, 0, 0.55f);
        t.panel900 = juce::Colour (0xff1c1b19);    t.panel700 = juce::Colour (0xff2c2a27);   t.panel600 = juce::Colour (0xff353330);
        t.cream100 = juce::Colour (0xffece6da);    t.cream300 = juce::Colour (0xffb9b2a4);
        t.cream500 = juce::Colour (0xff807a6e);    t.faint = juce::Colour (0xff5d574d);
        t.signal = juce::Colour (0xffc5602c);      t.signalDim = juce::Colour (0xff7e4a2c);  t.signalGlow = juce::Colour (0xffe0793f);
        return t;
    }

    Tokens makeLight()
    {
        Tokens t;
        t.light = true;
        t.room = juce::Colour (0xffb3ab98);        t.roomEdge = juce::Colour (0xffd4cdbd);
        t.chassisTop = juce::Colour (0xffe9e3d6);  t.chassisMid = juce::Colour (0xffe1dacb); t.chassisBot = juce::Colour (0xffd6cebd);
        t.railTop = juce::Colour (0xfffdfbf6);     t.railMid = juce::Colour (0xfff1ece1);    t.railBot = juce::Colour (0xffe9e3d5);
        t.metalTop = juce::Colour (0xfffdfbf6);    t.metalMid = juce::Colour (0xfff3efe6);   t.metalBot = juce::Colour (0xffe8e2d4);
        t.metalEdge = rgba (255, 255, 255, 0.92f);
        t.faceTop = juce::Colour (0xfffdfbf6);     t.faceBot = juce::Colour (0xffe7e1d3);
        t.keyTop = rgba (255, 255, 255, 0.95f);    t.keySheen = rgba (255, 255, 255, 0.5f);
        t.keyUnder = rgba (120, 108, 90, 0.30f);   t.keyBotline = rgba (120, 108, 90, 0.32f); t.keyWall = juce::Colour (0xffcdc6b6);
        t.trayTop = juce::Colour (0xffd3cbba);     t.trayBot = juce::Colour (0xffded6c5);
        t.trayRecess = rgba (120, 108, 90, 0.55f); t.trayLip = rgba (255, 255, 255, 0.95f);
        t.edgeLight = rgba (255, 255, 252, 0.95f); t.edgeDark = rgba (120, 108, 90, 0.30f);
        t.castSoft = rgba (150, 135, 110, 0.34f);  t.castMid = rgba (140, 124, 100, 0.38f);
        t.panel900 = juce::Colour (0xffc9c1b0);    t.panel700 = juce::Colour (0xfff0ebe0);   t.panel600 = juce::Colour (0xfff6f2e9);
        t.cream100 = juce::Colour (0xff2a2825);    t.cream300 = juce::Colour (0xff5d574d);
        t.cream500 = juce::Colour (0xff908a7d);    t.faint = juce::Colour (0xff908a7d);
        t.signal = juce::Colour (0xffc5602c);      t.signalDim = juce::Colour (0xffd7935f);  t.signalGlow = juce::Colour (0xffd4682f);
        return t;
    }

    struct ThemeState
    {
        Tokens dark = makeDark(), light = makeLight();
        bool isLight = false;
        std::vector<ThemeListener*> listeners;

        // Opened only to read or write, so no file object outlives the plugin's JUCE at unload.
        static std::unique_ptr<juce::PropertiesFile> openSettings()
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "ui";
            o.filenameSuffix = "settings";
            o.folderName = "ddeonmadeit";
            o.osxLibrarySubFolder = "Application Support";
            return std::make_unique<juce::PropertiesFile> (o);
        }

        ThemeState()
        {
            isLight = openSettings()->getValue ("theme", "dark") == "light";
            // For headless snapshots of either theme without touching the saved setting.
            const auto forced = juce::SystemStats::getEnvironmentVariable ("DD_THEME", {});
            if (forced.isNotEmpty())
                isLight = forced == "light";
        }
    };

    ThemeState& state()
    {
        static ThemeState s;
        return s;
    }

    /** Matte molded grain: fine, neutral, a few light and dark flecks. */
    const juce::Image& grainImage()
    {
        static const juce::Image image = []
        {
            constexpr int n = 160;
            std::vector<float> v ((size_t) (n * n));
            juce::Random rnd (0x5eed);
            for (auto& x : v)
                x = rnd.nextFloat() + rnd.nextFloat() - 1.0f;
            auto at = [&] (int x, int y) { return v[(size_t) (((y + n) % n) * n + (x + n) % n)]; };
            juce::Image im (juce::Image::ARGB, n, n, true, juce::SoftwareImageType());
            juce::Image::BitmapData bd (im, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                {
                    const float s = 0.6f * at (x, y) + 0.1f * (at (x + 1, y) + at (x - 1, y) + at (x, y + 1) + at (x, y - 1));
                    const float a = std::min (1.0f, std::abs (s));
                    bd.setPixelColour (x, y, s > 0.0f ? juce::Colours::white.withAlpha (a * 0.5f) : juce::Colours::black.withAlpha (a * 0.6f));
                }
            return im;
        }();
        return image;
    }

    /** The chassis' faint vertical striation: a 1 px light line every 3 px. */
    const juce::Image& striationImage()
    {
        static const juce::Image image = []
        {
            juce::Image im (juce::Image::ARGB, 3, 4, true, juce::SoftwareImageType());
            for (int y = 0; y < 4; ++y)
                im.setPixelAt (0, y, rgba (255, 248, 235, 0.028f));
            return im;
        }();
        return image;
    }

    /** A soft cast shadow below a rounded rect, built from stacked translucent layers (box-shadow blur). */
    void castShadow (juce::Graphics& g, juce::Rectangle<float> r, float corner, float dy, float blur, float spread, juce::Colour c)
    {
        const int layers = 6;
        const auto base = r.translated (0.0f, dy).expanded (spread);
        for (int i = layers; i >= 1; --i)
        {
            const float e = blur * (float) i / (float) layers;
            g.setColour (c.withMultipliedAlpha (1.0f / (float) layers));
            g.fillRoundedRectangle (base.expanded (e - blur * 0.5f), std::max (0.0f, corner + e - blur * 0.5f));
        }
    }

    /** An inset shadow along the top inside a rounded rect (box-shadow: inset 0 dy blur colour). */
    void insetTop (juce::Graphics& g, juce::Rectangle<float> r, float depth, juce::Colour c)
    {
        g.setGradientFill (juce::ColourGradient (c, 0.0f, r.getY(), c.withAlpha (0.0f), 0.0f, r.getY() + depth, false));
        g.fillRect (r.withHeight (depth));
    }

    void insetBottom (juce::Graphics& g, juce::Rectangle<float> r, float depth, juce::Colour c)
    {
        g.setGradientFill (juce::ColourGradient (c, 0.0f, r.getBottom(), c.withAlpha (0.0f), 0.0f, r.getBottom() - depth, false));
        g.fillRect (r.withTop (r.getBottom() - depth));
    }

    /** An elliptical radial glow: a circular gradient squashed to (rx, ry) around a centre. */
    void radialGlow (juce::Graphics& g, juce::Rectangle<float> clip, juce::Point<float> c, float rx, float ry,
                     std::initializer_list<std::pair<float, juce::Colour>> stops)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (clip.getSmallestIntegerContainer());
        const float sy = ry / std::max (0.001f, rx);
        g.addTransform (juce::AffineTransform::scale (1.0f, sy, c.x, c.y));
        juce::ColourGradient gr (stops.begin()->second, c.x, c.y, (stops.end() - 1)->second, c.x + rx, c.y, true);
        for (auto& [pos, col] : stops)
            if (pos > 0.0f && pos < 1.0f)
                gr.addColour (pos, col);
        g.setGradientFill (gr);
        g.fillEllipse (c.x - rx, c.y - rx, rx * 2.0f, rx * 2.0f);
    }

    const juce::Colour kLitText { 0xffffd9a0 };
}

//==============================================================================
const Tokens& tokens() { auto& s = state(); return s.isLight ? s.light : s.dark; }
bool isLightTheme() { return state().isLight; }

void setLightTheme (bool light)
{
    auto& s = state();
    if (s.isLight == light)
        return;
    const auto listeners = s.listeners;
    for (auto* l : listeners) l->themeAboutToChange();
    s.isLight = light;
    {
        auto settings = ThemeState::openSettings();
        settings->setValue ("theme", light ? "light" : "dark");
        settings->saveIfNeeded();
    }
    for (auto* l : listeners) l->themeChanged();
}

void addThemeListener (ThemeListener* l) { state().listeners.push_back (l); }
void removeThemeListener (ThemeListener* l)
{
    auto& v = state().listeners;
    v.erase (std::remove (v.begin(), v.end(), l), v.end());
}

Palette::Palette()
{
    const auto& t = tokens();
    background = t.room;
    panel = t.metalMid;
    panelEdge = t.edgeDark;
    track = t.trayTop;
    text = t.cream100;
    dim = t.cream300;
    glass = kLitText;
    screenText = juce::Colour (0xffece6da);
    accent = t.signal;
    accent2 = t.signalGlow;
}

//==============================================================================
float filamentFlicker (double seconds, double period, double delay)
{
    // Keyframes: steady, with short dips at 7, 23, 42-44, 68 and 86 % of the cycle.
    static const std::pair<float, float> dips[] = { { 0.07f, 0.80f }, { 0.23f, 0.90f }, { 0.42f, 0.68f }, { 0.44f, 0.88f },
                                                   { 0.68f, 0.78f }, { 0.86f, 0.92f } };
    if (seconds < delay)
        return 1.0f;
    const float ph = (float) std::fmod ((seconds - delay) / period, 1.0);
    for (auto [at, depth] : dips)
        if (std::abs (ph - at) < 0.01f)
            return depth + (1.0f - depth) * std::abs (ph - at) / 0.01f;
    return 1.0f;
}

float lampBloom (double seconds)
{
    const float ph = (float) std::fmod (seconds / 6.0, 1.0);
    const auto ease = [] (float x) { return 0.5f - 0.5f * std::cos (x * juce::MathConstants<float>::pi); };
    if (ph < 0.43f) return 0.85f + (0.62f - 0.85f) * ease (ph / 0.43f);
    if (ph < 0.68f) return 0.62f + (0.95f - 0.62f) * ease ((ph - 0.43f) / 0.25f);
    return 0.95f + (0.85f - 0.95f) * ease ((ph - 0.68f) / 0.32f);
}

float ledPulse (double seconds, double period)
{
    const float ph = (float) std::fmod (seconds / period, 1.0);
    return 0.675f + 0.325f * std::cos (ph * juce::MathConstants<float>::twoPi);
}

double animationTime() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

//==============================================================================
LookAndFeel::LookAndFeel (Palette)
{
    addThemeListener (this);
    applyColours();
}

LookAndFeel::~LookAndFeel() { removeThemeListener (this); }

void LookAndFeel::applyColours()
{
    const auto& t = tokens();
    const auto menu = t.light ? t.panel600 : juce::Colour (0xff242220);
    setColour (juce::ResizableWindow::backgroundColourId, t.room);
    setColour (juce::ComboBox::backgroundColourId, t.panel900);
    setColour (juce::ComboBox::textColourId, t.cream100);
    setColour (juce::ComboBox::outlineColourId, t.edgeDark);
    setColour (juce::ComboBox::arrowColourId, t.cream300);
    setColour (juce::PopupMenu::backgroundColourId, menu);
    setColour (juce::PopupMenu::textColourId, t.cream300);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.signal);
    setColour (juce::PopupMenu::highlightedTextColourId, kLitText);
    setColour (juce::PopupMenu::headerTextColourId, t.cream500);
    setColour (juce::Label::textColourId, t.cream100);
    setColour (juce::TextButton::buttonColourId, t.faceTop);
    setColour (juce::TextButton::buttonOnColourId, t.signal);
    setColour (juce::TextButton::textColourOffId, t.cream300);
    setColour (juce::TextButton::textColourOnId, kLitText);
    setColour (juce::TextEditor::backgroundColourId, t.panel900);
    setColour (juce::TextEditor::textColourId, t.cream100);
    setColour (juce::TextEditor::highlightColourId, t.signal.withAlpha (0.3f));
    setColour (juce::TextEditor::outlineColourId, t.edgeDark);
    setColour (juce::TextEditor::focusedOutlineColourId, t.signal);
    setColour (juce::CaretComponent::caretColourId, t.signalGlow);
    setColour (juce::AlertWindow::backgroundColourId, t.metalMid);
    setColour (juce::AlertWindow::textColourId, t.cream100);
    setColour (juce::AlertWindow::outlineColourId, t.edgeDark);
    setColour (juce::Slider::textBoxTextColourId, t.cream100);
    setColour (juce::TooltipWindow::backgroundColourId, menu);
    setColour (juce::TooltipWindow::textColourId, t.cream100);
    setColour (juce::TooltipWindow::outlineColourId, t.edgeDark);
}

juce::Font LookAndFeel::font (float height, int weight)
{
    const auto& f = fonts();
    juce::Typeface::Ptr tf = weight >= 700 ? f.bold : weight >= 600 ? f.semiBold : weight >= 500 ? f.medium : f.regular;
    if (tf != nullptr)
        return juce::Font (juce::FontOptions (tf).withHeight (height * 1.08f));
    return juce::Font (juce::FontOptions (height, weight >= 600 ? juce::Font::bold : juce::Font::plain));
}

juce::Font LookAndFeel::font (float height, bool bold) { return font (height, bold ? 600 : 400); }
juce::Font LookAndFeel::display (float height) { return font (height, 700); }

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider& s)
{
    const auto& t = tokens();
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float R = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f - 1.0f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool enabled = s.isEnabled();
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float fromAngle = startAngle + (bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f) * (endAngle - startAngle);

    // A round recessed tray holds the knob; the scale is a track cut into its floor (".track") that
    // lights in the signal colour like the sweep.
    const auto tray = juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c);
    {
        g.setColour (t.edgeLight);
        g.fillEllipse (tray.translated (0.0f, 1.0f));
        g.setGradientFill (juce::ColourGradient (t.trayTop, 0.0f, tray.getY(), t.trayBot, 0.0f, tray.getBottom(), false));
        g.fillEllipse (tray);
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addEllipse (tray);
        g.reduceClipRegion (clip);
        insetTop (g, tray, R * 0.5f, t.trayRecess.withMultipliedAlpha (0.8f));
        insetBottom (g, tray, R * 0.18f, t.trayLip.withMultipliedAlpha (0.5f));
    }

    const float trackW = std::max (2.0f, R * 0.085f);
    const float trackR = R - trackW * 1.25f;
    const auto stroke = [&] (float from, float to, float width, juce::Colour colour)
    {
        juce::Path p;
        p.addCentredArc (c.x, c.y, trackR, trackR, 0.0f, from, to, true);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    stroke (startAngle, endAngle, trackW, t.light ? juce::Colour (0xffbcb4a3) : juce::Colour (0xff0e0d0b));
    if (std::abs (angle - fromAngle) > 0.001f && enabled)
    {
        const float a0 = std::min (fromAngle, angle), a1 = std::max (fromAngle, angle);
        stroke (a0, a1, trackW * 3.6f, rgba (232, 118, 52, 0.12f));
        stroke (a0, a1, trackW * 2.0f, rgba (232, 118, 52, 0.3f));
        stroke (a0, a1, trackW, t.signalGlow);
        stroke (a0, a1, trackW * 0.4f, juce::Colour (0xffffc28a));
    }
    else if (bipolar && enabled)
    {
        const juce::Point<float> d (c.x + std::sin (fromAngle) * trackR, c.y - std::cos (fromAngle) * trackR);
        g.setColour (rgba (232, 118, 52, 0.35f));
        g.fillEllipse (d.x - trackW * 1.3f, d.y - trackW * 1.3f, trackW * 2.6f, trackW * 2.6f);
        g.setColour (t.signalGlow);
        g.fillEllipse (d.x - trackW * 0.5f, d.y - trackW * 0.5f, trackW, trackW);
    }

    // The knob: a round keycap standing in the tray, its wall and shadow falling to the tray floor.
    const float capR = trackR - trackW * 1.6f;
    const float wall = std::max (1.5f, capR * 0.12f);
    const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c).translated (0.0f, -wall * 0.5f);
    for (int i = 5; i >= 1; --i)
    {
        g.setColour (t.castMid.withMultipliedAlpha (0.18f));
        g.fillEllipse (cap.translated (0.0f, wall + (float) i * capR * 0.06f).expanded ((float) i * 0.6f));
    }
    g.setColour (t.keyWall);
    g.fillEllipse (cap.translated (0.0f, wall));
    g.setGradientFill (juce::ColourGradient (t.faceTop, 0.0f, cap.getY(), t.faceBot, 0.0f, cap.getBottom(), false));
    g.fillEllipse (cap);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addEllipse (cap);
        g.reduceClipRegion (clip);
        insetTop (g, cap, capR * 0.35f, t.keySheen);
        insetBottom (g, cap, capR * 0.55f, t.keyUnder);
    }
    juce::Path topLip;
    topLip.addCentredArc (cap.getCentreX(), cap.getCentreY(), capR - 0.8f, capR - 0.8f, 0.0f, -1.9f, 1.9f, true);
    g.setColour (t.keyTop);
    g.strokePath (topLip, juce::PathStrokeType (1.4f));
    juce::Path botLip;
    botLip.addCentredArc (cap.getCentreX(), cap.getCentreY(), capR - 0.7f, capR - 0.7f, 0.0f, 2.2f, 4.08f, true);
    g.setColour (t.keyBotline);
    g.strokePath (botLip, juce::PathStrokeType (1.2f));

    // Pointer: a lit line on the cap, an LED behind smoked plastic.
    const auto cc = cap.getCentre();
    const juce::Point<float> p0 (cc.x + std::sin (angle) * capR * 0.3f, cc.y - std::cos (angle) * capR * 0.3f);
    const juce::Point<float> p1 (cc.x + std::sin (angle) * capR * 0.84f, cc.y - std::cos (angle) * capR * 0.84f);
    const float pw = juce::jlimit (1.6f, 3.0f, capR * 0.11f);
    if (enabled)
    {
        g.setColour (rgba (232, 118, 52, t.light ? 0.22f : 0.35f));
        g.drawLine ({ p0, p1 }, pw * 2.6f);
        g.setColour (t.light ? t.signal : t.signalGlow);
        g.drawLine ({ p0, p1 }, pw);
        if (! t.light)   // a hot core reads as a gap on a cream cap
        {
            g.setColour (juce::Colour (0xffffd6ad));
            g.drawLine ({ p0, p1 }, pw * 0.4f);
        }
    }
    else
    {
        g.setColour (t.cream500);
        g.drawLine ({ p0, p1 }, pw);
    }
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto& t = tokens();
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat();
    // ".chip-box" / select: a recessed well with a fine border and an edge-lit lower lip.
    g.setColour (t.edgeLight);
    g.fillRoundedRectangle (r.translated (0.0f, 0.8f), 6.0f);
    g.setColour (t.panel900);
    g.fillRoundedRectangle (r.withTrimmedBottom (0.8f), 6.0f);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (r.withTrimmedBottom (0.8f), 6.0f);
        g.reduceClipRegion (clip);
        insetTop (g, r, std::min (7.0f, r.getHeight() * 0.4f), t.trayRecess.withMultipliedAlpha (0.8f));
    }
    g.setColour (box.hasKeyboardFocus (true) ? t.signal : t.edgeDark);
    g.drawRoundedRectangle (r.reduced (0.5f).withTrimmedBottom (0.8f), 6.0f, 1.0f);

    // Chevron.
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath (ax - 3.5f, ay - 1.8f);
    arrow.lineTo (ax, ay + 1.8f);
    arrow.lineTo (ax + 3.5f, ay - 1.8f);
    g.setColour (box.isMouseOver (true) ? t.cream100 : t.cream300);
    g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return font (std::min (13.0f, (float) box.getHeight() * 0.5f), 500);
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // Narrow boxes (one panel cell) squeeze long names rather than cutting them off.
    const bool narrow = box.getWidth() < 80;
    label.setBounds (narrow ? 5 : 8, 1, box.getWidth() - (narrow ? 21 : 24), box.getHeight() - 2);
    label.setBorderSize (juce::BorderSize<int> (1, narrow ? 1 : 3, 1, 1));
    label.setFont (getComboBoxFont (box));
    label.setMinimumHorizontalScale (narrow ? 0.5f : 0.6f);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setColour (juce::Label::textColourId, tokens().cream100);
}

juce::Font LookAndFeel::getPopupMenuFont() { return font (14.0f, 500); }

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    const auto& t = tokens();
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat();
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setGradientFill (juce::ColourGradient (t.keySheen, 0.0f, 0.0f, t.keySheen.withAlpha (0.0f), 0.0f, 40.0f, false));
    g.fillRect (r.withHeight (40.0f));
    g.setColour (t.edgeDark);
    g.drawRect (r, 1.0f);
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                     bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                                     const juce::Drawable* icon, const juce::Colour* textColour)
{
    const auto& t = tokens();
    if (isSeparator || ! isHighlighted || ! isActive)
    {
        LookAndFeel_V4::drawPopupMenuItem (g, area, isSeparator, isActive, false, false, hasSubMenu, text, shortcutKeyText, icon, textColour);
        if (isTicked && ! isSeparator)
        {
            // The current choice: a small lit LED instead of a tick.
            const float cy = (float) area.getCentreY();
            g.setColour (rgba (232, 118, 52, 0.4f));
            g.fillEllipse ((float) area.getX() + 8.0f, cy - 4.0f, 8.0f, 8.0f);
            g.setColour (t.signalGlow);
            g.fillEllipse ((float) area.getX() + 9.5f, cy - 2.5f, 5.0f, 5.0f);
        }
        return;
    }
    // Highlighted item: a lit pill (".pill.active").
    const auto r = area.toFloat().reduced (3.0f, 1.0f);
    paintLitKey (g, r, 5.0f, false, 1.0f, 0.0f);
    auto textArea = area.reduced (area.getHeight() / 2 + 6, 0);
    drawGlowing (g, text, textArea.toFloat(), kLitText, getPopupMenuFont(), juce::Justification::centredLeft);
    if (hasSubMenu)
    {
        const float ax = (float) area.getRight() - 12.0f, ay = (float) area.getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath (ax - 2.0f, ay - 3.5f);
        arrow.lineTo (ax + 1.8f, ay);
        arrow.lineTo (ax - 2.0f, ay + 3.5f);
        g.setColour (kLitText);
        g.strokePath (arrow, juce::PathStrokeType (1.4f));
    }
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f, 0.0f).withTrimmedBottom (4.0f);
    const float corner = std::min (8.0f, r.getHeight() * 0.3f);
    if (b.getToggleState())
        paintLitKey (g, r, corner, down, filamentFlicker (animationTime(), 4.8, 0.3), 3.0f, 1.0f);
    else
        paintKeycap (g, r, corner, down, 3.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down)
{
    const auto& t = tokens();
    const auto f = getTextButtonFont (b, b.getHeight());
    auto area = b.getLocalBounds().toFloat().reduced (4.0f, 0.0f).withTrimmedBottom (4.0f);
    if (down) area = area.translated (0.0f, 3.0f);
    const auto text = b.getButtonText().toUpperCase();
    if (b.getToggleState())
        drawGlowing (g, text, area, kLitText, f);
    else
    {
        g.setFont (f);
        g.setColour (over ? t.cream100 : t.cream300);
        g.drawText (text, area, juce::Justification::centred, false);
    }
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return font (std::min (11.0f, (float) h * 0.38f), 700).withExtraKerningFactor (0.08f);
}

void LookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    const auto r = alert.getLocalBounds().toFloat();
    paintBrushedMetal (g, r, {}, 0.0f);
    g.setColour (tokens().edgeDark);
    g.drawRect (r, 1.0f);
    g.setColour (alert.findColour (juce::AlertWindow::textColourId));
    layout.draw (g, textArea.withTrimmedLeft (12).toFloat());
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const int w = juce::jmin (380, (int) juce::GlyphArrangement::getStringWidth (font (13.0f, 500), text) + 24);
    const int h = 28;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 24,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin (parent);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h);
    g.fillAll (findColour (juce::TooltipWindow::backgroundColourId));
    g.setColour (findColour (juce::TooltipWindow::outlineColourId));
    g.drawRect (r, 1);
    g.setColour (findColour (juce::TooltipWindow::textColourId));
    g.setFont (font (13.0f, 500));
    g.drawFittedText (text, r.reduced (11, 2), juce::Justification::centredLeft, 1);
}

//==============================================================================
void paintGrain (juce::Graphics& g, juce::Rectangle<int> area, float amount)
{
    g.setTiledImageFill (grainImage(), area.getX(), area.getY(), juce::jlimit (0.0f, 1.0f, amount));
    g.fillRect (area);
}

void paintBackdrop (juce::Graphics& g, juce::Rectangle<int> area, const Palette&)
{
    const auto& t = tokens();
    const auto r = area.toFloat();
    // The room: a lit patch above the instrument falling off to the edges (".layout").
    g.setColour (t.room);
    g.fillRect (r);
    radialGlow (g, r, { r.getCentreX(), r.getY() }, r.getWidth() * 0.55f, r.getHeight() * 0.7f,
                { { 0.0f, t.roomEdge }, { 0.8f, t.room }, { 1.0f, t.room } });

    // The chassis: rounded top corners, faint vertical striation over its metal, an overhead light on the lip.
    const auto body = r.withTrimmedTop (8.0f);
    juce::Path shape;
    shape.addRoundedRectangle (body.getX(), body.getY(), body.getWidth(), body.getHeight() + 30.0f, 22.0f, 22.0f, true, true, false, false);
    g.setColour (juce::Colours::black.withAlpha (t.light ? 0.08f : 0.25f));
    g.fillPath (shape, juce::AffineTransform::translation (0.0f, -3.0f));
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (shape);
    juce::ColourGradient metal (t.chassisTop, 0.0f, body.getY(), t.chassisBot, 0.0f, body.getBottom(), false);
    metal.addColour (0.26, t.chassisMid);
    g.setGradientFill (metal);
    g.fillRect (body);
    if (! t.light)
    {
        g.setTiledImageFill (striationImage(), 0, 0, 1.0f);
        g.fillRect (body);
    }
    radialGlow (g, body, { body.getCentreX(), body.getY() }, body.getWidth() * 0.6f, 340.0f * 0.6f,
                { { 0.0f, rgba (255, 248, 235, t.light ? 0.3f : 0.05f) }, { 0.7f, rgba (255, 248, 235, 0.0f) }, { 1.0f, rgba (255, 248, 235, 0.0f) } });
    g.setColour (t.light ? rgba (255, 255, 255, 0.9f) : rgba (255, 248, 235, 0.11f));
    g.strokePath (shape, juce::PathStrokeType (2.0f), juce::AffineTransform::translation (0.0f, 1.0f));
}

void paintRaisedShadow (juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    castShadow (g, r, corner, 14.0f, 30.0f, -10.0f, tokens().castSoft);
}

void paintFaceplate (juce::Graphics& g, juce::Rectangle<float> r, const Palette&, bool)
{
    const auto& t = tokens();
    constexpr float corner = 14.0f;
    castShadow (g, r, corner, 14.0f, 30.0f, -10.0f, t.castSoft);
    paintBrushedMetal (g, r, {}, corner);
}

void paintBrushedMetal (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour, float corner, float, bool)
{
    const auto& t = tokens();
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip;
    clip.addRoundedRectangle (r, corner);
    g.reduceClipRegion (clip);
    // ".glass": metal face at 176 deg, a lit top edge, a dark lower edge, and the top sheen.
    juce::ColourGradient face (t.metalTop, r.getX(), r.getY(), t.metalBot, r.getX() + r.getHeight() * 0.07f, r.getBottom(), false);
    face.addColour (0.44, t.metalMid);
    g.setGradientFill (face);
    g.fillRect (r);
    if (! t.light)
    {
        g.setTiledImageFill (striationImage(), 0, 0, 0.7f);
        g.fillRect (r);
    }
    g.setGradientFill (juce::ColourGradient (rgba (255, 248, 235, t.light ? 0.5f : 0.09f), 0.0f, r.getY(),
                                             rgba (255, 248, 235, 0.0f), 0.0f, r.getY() + std::min (60.0f, r.getHeight() * 0.07f + 6.0f), false));
    g.fillRect (r);
    radialGlow (g, r, { r.getCentreX(), r.getY() }, r.getWidth() * 0.65f, r.getHeight() * 0.6f,
                { { 0.0f, t.keySheen }, { 0.7f, t.keySheen.withAlpha (0.0f) }, { 1.0f, t.keySheen.withAlpha (0.0f) } });
    radialGlow (g, r, { r.getX() + r.getWidth() * 0.85f, r.getY() + r.getHeight() * 0.04f }, r.getWidth() * 0.4f, r.getHeight() * 0.4f,
                { { 0.0f, rgba (255, 248, 235, t.light ? 0.2f : 0.045f) }, { 0.6f, rgba (255, 248, 235, 0.0f) }, { 1.0f, rgba (255, 248, 235, 0.0f) } });
    g.setColour (t.metalEdge);
    g.drawLine (r.getX() + corner * 0.6f, r.getY() + 0.5f, r.getRight() - corner * 0.6f, r.getY() + 0.5f, 1.0f);
    g.setColour (t.edgeDark);
    g.drawLine (r.getX() + corner * 0.6f, r.getBottom() - 0.5f, r.getRight() - corner * 0.6f, r.getBottom() - 0.5f, 1.0f);
}

void paintTray (juce::Graphics& g, juce::Rectangle<float> r, float corner, float depth)
{
    const auto& t = tokens();
    castShadow (g, r, corner, 4.0f, 12.0f, -1.0f, rgba (0, 0, 0, t.light ? 0.12f : 0.5f));
    g.setColour (t.edgeLight);
    g.fillRoundedRectangle (r.translated (0.0f, 1.0f), corner);
    g.setGradientFill (juce::ColourGradient (t.trayTop, 0.0f, r.getY(), t.trayBot, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip;
    clip.addRoundedRectangle (r, corner);
    g.reduceClipRegion (clip);
    insetTop (g, r, 18.0f * depth, t.trayRecess.withMultipliedAlpha (0.8f));
    insetTop (g, r, 5.0f * depth, t.trayRecess);
    insetBottom (g, r, 3.0f, t.trayLip);
    g.setColour (t.trayLip);
    g.drawLine (r.getX() + corner, r.getBottom() - 0.5f, r.getRight() - corner, r.getBottom() - 0.5f, 1.0f);
}

void paintInset (juce::Graphics& g, juce::Rectangle<float> r, const Palette&, float corner)
{
    paintTray (g, r, std::max (corner, 6.0f), 0.6f);
}

void paintBezel (juce::Graphics& g, juce::Rectangle<float> window, float corner, float width)
{
    // The scene sits in a deep well (".well"): the floor around it shaded from above, an edge-lit lip.
    const auto& t = tokens();
    const auto outer = window.expanded (width);
    g.setColour (t.edgeLight);
    g.fillRoundedRectangle (outer.translated (0.0f, 1.0f), corner + width);
    g.setGradientFill (juce::ColourGradient (t.light ? juce::Colour (0xffd3cbba) : juce::Colour (0xff1d1b18), 0.0f, outer.getY(),
                                             t.light ? juce::Colour (0xffded6c5) : juce::Colour (0xff211f1b), 0.0f, outer.getBottom(), false));
    g.fillRoundedRectangle (outer, corner + width);
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip;
    clip.addRoundedRectangle (outer, corner + width);
    g.reduceClipRegion (clip);
    insetTop (g, outer, width * 2.2f, rgba (0, 0, 0, t.light ? 0.2f : 0.82f));
}

void paintConsole (juce::Graphics& g, juce::Rectangle<float> r, float corner, float bloom)
{
    const auto& t = tokens();
    g.setColour (rgba (255, 248, 235, 0.05f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.0f), corner);
    g.setGradientFill (juce::ColourGradient (t.light ? juce::Colour (0xff2e261e) : juce::Colour (0xff1a0f08), 0.0f, r.getY(),
                                             t.light ? juce::Colour (0xff211a14) : juce::Colour (0xff0d0704), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip;
    clip.addRoundedRectangle (r, corner);
    g.reduceClipRegion (clip);
    radialGlow (g, r, { r.getX() + r.getWidth() * 0.28f, r.getY() + r.getHeight() * 0.55f }, r.getWidth() * 0.8f, r.getHeight() * 1.3f,
                { { 0.0f, rgba (232, 118, 52, 0.2f * bloom) }, { 0.68f, rgba (232, 118, 52, 0.0f) }, { 1.0f, rgba (232, 118, 52, 0.0f) } });
    g.setColour (rgba (0, 0, 0, 0.28f * 0.45f));
    for (float y = r.getY(); y < r.getBottom(); y += 3.0f)
        g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
    insetTop (g, r, 10.0f, rgba (0, 0, 0, t.light ? 0.35f : 0.92f));
    insetBottom (g, r, 1.5f, rgba (255, 160, 80, t.light ? 0.18f : 0.12f));
    paintGlassReflection (g, r, corner);
}

void paintGlassReflection (juce::Graphics& g, juce::Rectangle<float> window, float corner)
{
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip;
    clip.addRoundedRectangle (window, corner);
    g.reduceClipRegion (clip);
    g.setGradientFill (juce::ColourGradient (rgba (255, 255, 255, 0.06f), 0.0f, window.getY(),
                                             rgba (255, 255, 255, 0.0f), 0.0f, window.getY() + window.getHeight() * 0.46f, false));
    g.fillRect (window.withHeight (window.getHeight() * 0.46f));
}

void paintKeycap (juce::Graphics& g, juce::Rectangle<float> r, float corner, bool pressed, float wall)
{
    const auto& t = tokens();
    if (! pressed)
    {
        castShadow (g, r, corner, 7.0f + wall * 0.5f, 14.0f, -3.0f, t.castMid.withMultipliedAlpha (1.1f));
        castShadow (g, r, corner, 14.0f, 22.0f, -6.0f, t.castSoft.withMultipliedAlpha (0.7f));
        g.setColour (t.keyWall);
        g.fillRoundedRectangle (r.translated (0.0f, wall), corner);
    }
    else
    {
        castShadow (g, r.translated (0.0f, wall), corner, 2.0f, 4.0f, -2.0f, t.castMid);
    }
    const auto face = pressed ? r.translated (0.0f, wall) : r;
    g.setGradientFill (juce::ColourGradient (t.faceTop, 0.0f, face.getY(), t.faceBot, 0.0f, face.getBottom(), false));
    g.fillRoundedRectangle (face, corner);
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip;
    clip.addRoundedRectangle (face, corner);
    g.reduceClipRegion (clip);
    insetTop (g, face, 5.0f, t.keySheen);
    insetBottom (g, face, pressed ? 5.0f : 8.0f, t.keyUnder);
    g.setColour (t.keyTop);
    g.fillRect (face.getX(), face.getY(), face.getWidth(), pressed ? 1.0f : 1.5f);
    g.setColour (t.keyBotline);
    g.fillRect (face.getX(), face.getBottom() - 1.5f, face.getWidth(), 1.5f);
}

void paintLitKey (juce::Graphics& g, juce::Rectangle<float> r, float corner, bool pressed, float light, float wall, float glow)
{
    light = juce::jlimit (0.0f, 1.0f, light);
    // External glow: the LED bleeding through the plastic sides.
    if (wall > 0.0f)
    {
        if (glow >= 10.0f)
            castShadow (g, r, corner, 8.0f, 16.0f, -4.0f, rgba (0, 0, 0, 0.7f));
        else
            castShadow (g, r, corner, juce::jmin (2.0f, glow * 0.5f), juce::jmax (1.0f, glow * 0.6f), 0.0f, rgba (0, 0, 0, 0.5f));
        const float step = glow / 4.0f;
        for (int i = 4; i >= 1 && step >= 0.5f; --i)
        {
            g.setColour (rgba (200, 82, 20, 0.16f * light));
            g.fillRoundedRectangle (r.expanded ((float) i * step), corner + (float) i * step);
        }
        if (! pressed)
        {
            g.setColour (juce::Colour (0xff0d0906));
            g.fillRoundedRectangle (r.translated (0.0f, wall), corner);
        }
    }
    const auto face = pressed ? r.translated (0.0f, wall) : r;
    g.setGradientFill (juce::ColourGradient (rgba (40, 37, 33, 0.97f), 0.0f, face.getY(), rgba (25, 23, 20, 0.99f), 0.0f, face.getBottom(), false));
    g.fillRoundedRectangle (face, corner);
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip;
    clip.addRoundedRectangle (face, corner);
    g.reduceClipRegion (clip);
    // LED radiating from behind, diffused through dark plastic, plus scatter at the cap top.
    radialGlow (g, face, { face.getCentreX(), face.getY() + face.getHeight() * 0.68f }, face.getWidth() * 0.72f, face.getHeight() * 0.78f,
                { { 0.0f, rgba (255, 135, 42, 0.92f * light) }, { 0.2f, rgba (208, 78, 18, 0.78f * light) }, { 0.44f, rgba (138, 42, 8, 0.54f * light) },
                  { 0.66f, rgba (58, 16, 3, 0.28f * light) }, { 0.84f, rgba (0, 0, 0, 0.0f) }, { 1.0f, rgba (0, 0, 0, 0.0f) } });
    radialGlow (g, face, { face.getCentreX(), face.getY() + face.getHeight() * 0.22f }, face.getWidth() * 0.5f, face.getHeight() * 0.35f,
                { { 0.0f, rgba (255, 110, 30, 0.22f * light) }, { 0.7f, rgba (255, 110, 30, 0.0f) }, { 1.0f, rgba (255, 110, 30, 0.0f) } });
    insetTop (g, face, 10.0f, rgba (12, 4, 1, 0.72f));
    insetBottom (g, face, 7.0f, rgba (195, 75, 18, 0.22f * light));
    g.setColour (rgba (255, 195, 120, 0.28f));
    g.fillRect (face.getX(), face.getY(), face.getWidth(), 1.0f);
    g.setColour (rgba (0, 0, 0, 0.88f));
    g.fillRect (face.getX(), face.getBottom() - 1.0f, face.getWidth(), 1.0f);
}

void paintSwitch (juce::Graphics& g, juce::Rectangle<float> r, float position, bool on, float led)
{
    const auto& t = tokens();
    const float corner = r.getHeight() * 0.56f;
    g.setColour (t.edgeLight);
    g.fillRoundedRectangle (r.translated (0.0f, 1.0f), corner);
    g.setGradientFill (juce::ColourGradient (t.trayTop, 0.0f, r.getY(), t.trayBot, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, corner);
        g.reduceClipRegion (clip);
        insetTop (g, r, 6.0f, t.trayRecess);
        insetBottom (g, r, 2.0f, t.trayLip);
    }
    if (on)
    {
        const float d = r.getHeight() * 0.22f;
        const juce::Point<float> c (r.getX() + r.getHeight() * 0.45f, r.getCentreY());
        g.setColour (rgba (232, 118, 52, 0.45f * led));
        g.fillEllipse (c.x - d * 1.5f, c.y - d * 1.5f, d * 3.0f, d * 3.0f);
        g.setColour (t.signalGlow.withMultipliedAlpha (0.6f + 0.4f * led));
        g.fillEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d);
    }
    const float kd = r.getHeight() - 6.0f;
    const float kx = r.getX() + 3.0f + juce::jlimit (0.0f, 1.0f, position) * (r.getWidth() - kd - 6.0f);
    const auto knob = juce::Rectangle<float> (kx, r.getY() + 3.0f, kd, kd);
    g.setColour (rgba (0, 0, 0, t.light ? 0.2f : 0.45f));
    g.fillEllipse (knob.translated (0.0f, 2.0f).expanded (0.5f));
    g.setGradientFill (juce::ColourGradient (t.faceTop, 0.0f, knob.getY(), t.faceBot, 0.0f, knob.getBottom(), false));
    g.fillEllipse (knob);
    juce::Path lip;
    lip.addCentredArc (knob.getCentreX(), knob.getCentreY(), kd * 0.5f - 0.6f, kd * 0.5f - 0.6f, 0.0f, -1.6f, 1.6f, true);
    g.setColour (t.keyTop);
    g.strokePath (lip, juce::PathStrokeType (1.0f));
}

void paintScrew (juce::Graphics&, juce::Point<float>, float, float) {}

void paintLamp (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour colour, float lit)
{
    const auto& t = tokens();
    lit = juce::jlimit (0.0f, 1.0f, lit);
    const float ring = radius + std::max (1.5f, radius * 0.45f);
    const auto outer = juce::Rectangle<float> (ring * 2.0f, ring * 2.0f).withCentre (c);
    g.setColour (t.edgeLight);
    g.fillEllipse (outer.translated (0.0f, 1.0f));
    g.setGradientFill (juce::ColourGradient (t.trayTop, 0.0f, outer.getY(), t.trayBot, 0.0f, outer.getBottom(), false));
    g.fillEllipse (outer);
    const auto lens = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);
    if (lit <= 0.0f)
    {
        g.setColour (t.panel700);
        g.fillEllipse (lens);
        return;
    }
    const auto hot = colour.brighter (0.9f).interpolatedWith (juce::Colour (0xffffd6ad), 0.6f);
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (colour.withAlpha (0.16f * lit));
        g.fillEllipse (lens.expanded ((float) i * radius * 0.4f));
    }
    g.setGradientFill (juce::ColourGradient (hot, lens.getX() + lens.getWidth() * 0.42f, lens.getY() + lens.getHeight() * 0.36f,
                                             colour, lens.getRight(), lens.getBottom(), true));
    g.fillEllipse (lens);
    g.setColour (rgba (255, 235, 210, 0.3f * lit));
    g.fillEllipse (lens.getX() + radius * 0.4f, lens.getY() + radius * 0.25f, radius * 0.7f, radius * 0.5f);
}

void drawPrinted (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour colour,
                  const juce::Font& f, juce::Justification just)
{
    g.setFont (f);
    g.setColour (colour);
    g.drawText (text, r, just, false);
}

void drawPrintedFitted (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Colour colour,
                        const juce::Font& f, juce::Justification just)
{
    g.setFont (f);
    g.setColour (colour);
    g.drawFittedText (text, r, just, 1, 0.7f);
}

void drawEmbossed (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, const juce::Font& f,
                   juce::Justification just)
{
    const auto& t = tokens();
    g.setFont (f);
    g.setColour (t.edgeLight);
    g.drawText (text, r.translated (0.0f, 1.0f), just, false);
    g.setColour (t.edgeDark);
    g.drawText (text, r.translated (0.0f, -1.0f), just, false);
    g.setColour (t.cream100);
    g.drawText (text, r, just, false);
}

void drawGlowing (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour colour,
                  const juce::Font& f, juce::Justification just)
{
    g.setFont (f);
    g.setColour (rgba (255, 150, 82, 0.18f));
    for (auto [dx, dy] : { std::pair { -1.5f, 0.0f }, { 1.5f, 0.0f }, { 0.0f, -1.5f }, { 0.0f, 1.5f } })
        g.drawText (text, r.translated (dx, dy), just, false);
    g.setColour (colour);
    g.drawText (text, r, just, false);
}

void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification just, float height)
{
    g.setFont (LookAndFeel::font (height, 700).withExtraKerningFactor (0.13f));
    g.setColour (tokens().cream500);
    g.drawText (text.toUpperCase(), r, just, false);
}

void paintSunsetStripes (juce::Graphics&, juce::Rectangle<float>) {}
} // namespace dd::gui
