#include "SoundView.h"

namespace hush
{
namespace
{
using LnF = dd::gui::LookAndFeel;
const juce::Colour kLamp { 0xffff9652 }, kHot { 0xffffd9a0 }, kCream { 0xfff2e6d6 };

constexpr const char* kHint = "Each lit sound is an S, SH or T the vocal makes. Drag its handle in the lower lane down to turn "
                              "just that sound down (Alt = fine). Click to hear it, double-click to give it back to Amount, "
                              "Shift-click or drag a box to pick several.";

void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour c, float size, int weight,
               juce::Justification just = juce::Justification::centredLeft)
{
    g.setColour (c);
    g.setFont (LnF::font (size, weight));
    g.drawFittedText (text, r.toNearestInt(), just, 2, 0.8f);
}

juce::String dbText (float db)
{
    if (std::abs (db) < 0.05f)
        return "0 dB";
    return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
}

juce::String timeText (double seconds)
{
    const int m = (int) (seconds / 60.0);
    return juce::String (m) + ":" + juce::String (seconds - m * 60.0, 2).paddedLeft ('0', 5);
}
} // namespace

//==============================================================================
SoundCanvas::SoundCanvas (HushProcessor& p, EditorState& s) : proc (p), state (s)
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    state.addChangeListener (this);
    refresh (true);
    startTimerHz (30);
}

SoundCanvas::~SoundCanvas()
{
    state.removeChangeListener (this);
}

juce::Rectangle<float> SoundCanvas::waveArea() const
{
    auto r = getLocalBounds().toFloat().withTrimmedLeft ((float) kScaleW).withTrimmedTop ((float) kRulerH);
    return r.withHeight (r.getHeight() * 0.58f);
}

juce::Rectangle<float> SoundCanvas::laneArea() const
{
    const auto w = waveArea();
    return getLocalBounds().toFloat().withTrimmedLeft ((float) kScaleW).withTop (w.getBottom() + 10.0f).withTrimmedBottom (6.0f);
}

float SoundCanvas::timeToX (double samples) const
{
    const auto a = waveArea();
    return a.getX() + (float) ((samples / sampleRate() - state.viewStart) * a.getWidth() / state.viewSeconds);
}

double SoundCanvas::xToTime (float x) const
{
    const auto a = waveArea();
    return (state.viewStart + (double) (x - a.getX()) * state.viewSeconds / a.getWidth()) * sampleRate();
}

float SoundCanvas::dbToY (float db) const
{
    const auto l = laneArea();
    return l.getY() + (kTopDb - juce::jlimit (kBottomDb, kTopDb, db)) / (kTopDb - kBottomDb) * l.getHeight();
}

float SoundCanvas::yToDb (float y) const
{
    const auto l = laneArea();
    return kTopDb - (y - l.getY()) / l.getHeight() * (kTopDb - kBottomDb);
}

void SoundCanvas::resized()
{
    if (fitPending && ! snap.sounds.empty() && getHeight() > 100)
    {
        fitPending = false;
        zoomToFit();
    }
    invalidate();
}

void SoundCanvas::refresh (bool force)
{
    auto& eng = *proc.engine;
    const auto v = eng.getVersion();
    const auto s = eng.currentSettings();
    if (! force && v == seenVersion && s == settings)
        return;
    snap = eng.snapshot();
    settings = s;
    seenVersion = v;
    std::set<uint32_t> keep;
    for (const auto& x : snap.sounds)
        if (state.selection.count (x.id) > 0)
            keep.insert (x.id);
    state.selection = keep;
    if (! snap.spans.empty())
    {
        state.takeStart = (double) snap.spans.front().start / sampleRate();
        state.takeEnd = (double) snap.spans.back().end / sampleRate();
    }
    else
        state.takeStart = state.takeEnd = 0.0;
    if (snap.spans.empty())
        fitPending = true;
    else if (fitPending && getHeight() > 100)
    {
        fitPending = false;
        zoomToFit();
    }
    state.changed();
    invalidate();
}

void SoundCanvas::timerCallback()
{
    refresh (false);
    auto& eng = *proc.engine;
    const auto ph = eng.getPlayhead();
    if (eng.isPlaying() && state.follow && drag.kind == Drag::None)
    {
        const double t = (double) ph / sampleRate();
        if (t < state.viewStart || t > state.viewStart + state.viewSeconds * 0.94)
        {
            state.viewStart = std::max (0.0, t - state.viewSeconds * 0.06);
            state.changed();
        }
    }
    if (ph != lastPlayhead)
    {
        lastPlayhead = ph;
        repaint();
    }
}

void SoundCanvas::setHint (const juce::String& h)
{
    if (state.hint != h)
    {
        state.hint = h;
        state.changed();
    }
}

void SoundCanvas::clampView()
{
    const double len = std::max (10.0, state.takeEnd - state.takeStart);
    state.viewSeconds = juce::jlimit (0.1, len * 1.6 + 5.0, state.viewSeconds);
    state.viewStart = juce::jlimit (0.0, std::max (0.0, state.takeEnd + 2.0 - state.viewSeconds * 0.5), state.viewStart);
}

void SoundCanvas::zoomToFit()
{
    if (snap.spans.empty())
        return;
    const double a = (double) snap.spans.front().start / sampleRate(), b = (double) snap.spans.back().end / sampleRate();
    state.viewSeconds = std::max (0.5, (b - a) * 1.04);
    state.viewStart = std::max (0.0, a - (b - a) * 0.02);
    clampView();
    state.changed();
    invalidate();
}

int SoundCanvas::soundAt (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 1.0e9f;
    for (int i = 0; i < (int) snap.sounds.size(); ++i)
    {
        const auto& s = snap.sounds[(size_t) i];
        float x0 = timeToX ((double) s.start), x1 = timeToX ((double) s.end);
        if (x1 - x0 < 8.0f)
        {
            const float c = (x0 + x1) * 0.5f;
            x0 = c - 4.0f;
            x1 = c + 4.0f;
        }
        if (p.x < x0 - 1.0f || p.x > x1 + 1.0f)
            continue;
        const float d = std::abs (p.x - (x0 + x1) * 0.5f);
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

//==============================================================================
void SoundCanvas::paint (juce::Graphics& g)
{
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int w = juce::jmax (1, juce::roundToInt ((float) getWidth() * scale));
    const int h = juce::jmax (1, juce::roundToInt ((float) getHeight() * scale));
    if (! cacheValid || cache.getWidth() != w || cache.getHeight() != h)
    {
        cache = juce::Image (juce::Image::ARGB, w, h, true);
        juce::Graphics cg (cache);
        cg.addTransform (juce::AffineTransform::scale (scale));
        paintStatic (cg);
        cacheValid = true;
    }
    g.drawImageTransformed (cache, juce::AffineTransform::scale (1.0f / scale));

    auto& eng = *proc.engine;
    const auto wa = waveArea();
    {
        const float cx = timeToX ((double) state.cursor);
        if (cx >= wa.getX() && cx <= wa.getRight())
        {
            juce::Path tri;
            tri.addTriangle (cx - 5.0f, 4.0f, cx + 5.0f, 4.0f, cx, (float) kRulerH - 4.0f);
            g.setColour (kCream.withAlpha (0.8f));
            g.fillPath (tri);
        }
    }
    const auto ph = eng.getPlayhead();
    const float px = timeToX ((double) ph);
    if (px >= wa.getX() && px <= wa.getRight() && (eng.isPlaying() || ph > 0))
    {
        g.setColour (kLamp.withAlpha (0.18f));
        g.fillRect (px - 3.0f, 0.0f, 6.0f, (float) getHeight());
        g.setColour (kLamp.withAlpha (eng.isPlaying() ? 0.95f : 0.5f));
        g.fillRect (px - 0.75f, 0.0f, 1.5f, (float) getHeight());
    }
    if (drag.kind == Drag::Rubber && ! drag.rubber.isEmpty())
    {
        g.setColour (kCream.withAlpha (0.08f));
        g.fillRect (drag.rubber);
        g.setColour (kCream.withAlpha (0.5f));
        g.drawRect (drag.rubber, 1.0f);
    }
    if (dropHover)
    {
        const auto a = getLocalBounds().toFloat().reduced (10.0f);
        g.setColour (kLamp.withAlpha (0.12f));
        g.fillRoundedRectangle (a, 8.0f);
        g.setColour (kLamp.withAlpha (0.8f));
        g.drawRoundedRectangle (a, 8.0f, 2.0f);
        dd::gui::drawGlowing (g, "DROP TO LOAD THIS VOCAL", a, kHot, LnF::font (18.0f, 700).withExtraKerningFactor (0.12f));
    }
}

void SoundCanvas::paintStatic (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    dd::gui::paintConsole (g, bounds, 8.0f, 0.55f);
    juce::Graphics::ScopedSaveState clipAll (g);
    juce::Path clipPath;
    clipPath.addRoundedRectangle (bounds, 8.0f);
    g.reduceClipRegion (clipPath);

    const auto wa = waveArea(), la = laneArea();
    const double sr = sampleRate();
    const double v0 = state.viewStart * sr, v1 = (state.viewStart + state.viewSeconds) * sr;

    // Ruler and time grid.
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (wa.getX(), 0.0f, wa.getWidth(), (float) kRulerH);
        const double pxPerSec = wa.getWidth() / state.viewSeconds;
        const double step = pxPerSec > 400 ? 0.1 : pxPerSec > 160 ? 0.25 : pxPerSec > 60 ? 1.0 : pxPerSec > 12 ? 5.0 : 30.0;
        for (double t = std::floor (state.viewStart / step) * step; t <= state.viewStart + state.viewSeconds; t += step)
        {
            const float x = timeToX (t * sr);
            g.setColour (juce::Colour (0xffffe8c8).withAlpha (0.05f));
            g.fillRect (x, wa.getY(), 1.0f, la.getBottom() - wa.getY());
            drawText (g, timeText (t), { x + 3.0f, 2.0f, 60.0f, (float) kRulerH - 4.0f }, kCream.withAlpha (0.5f), 10.5f, 600);
        }
    }

    // The reduction lane's scale.
    for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f })
    {
        const float y = dbToY (db);
        g.setColour (juce::Colour (0xffffe8c8).withAlpha (juce::exactlyEqual (db, 0.0f) ? 0.22f : 0.06f));
        g.fillRect (la.getX(), y, la.getWidth(), 1.0f);
        drawText (g, juce::exactlyEqual (db, 0.0f) ? "0 dB" : juce::String ((int) db), { 4.0f, y - 7.0f, (float) kScaleW - 8.0f, 14.0f },
                  kCream.withAlpha (0.45f), 9.5f, 600, juce::Justification::centredRight);
    }
    drawText (g, "VOICE", { 4.0f, wa.getY() + 4.0f, (float) kScaleW - 6.0f, 14.0f }, kCream.withAlpha (0.4f), 8.5f, 700);
    drawText (g, "CUT", { 4.0f, la.getY() - 14.0f, (float) kScaleW - 6.0f, 14.0f }, kCream.withAlpha (0.4f), 8.5f, 700);

    // Waveform (overall level) with the sibilance band inside it.
    float peak = -120.0f;
    for (const auto& pic : snap.pictures)
        for (auto v : pic.full)
            peak = std::max (peak, v);
    auto amp = [&] (float dbv) { return juce::jlimit (0.0f, 1.0f, std::sqrt (std::pow (10.0f, (dbv - peak) / 20.0f))); };
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (wa.toNearestInt());
        const float mid = wa.getCentreY(), half = wa.getHeight() * 0.46f;
        for (const auto& pic : snap.pictures)
        {
            if (pic.full.empty())
                continue;
            for (int layer = 0; layer < 2; ++layer)
            {
                const auto& v = layer == 0 ? pic.full : pic.high;
                juce::Path top, shape;
                bool started = false;
                std::vector<juce::Point<float>> pts;
                float lastX = -1.0e9f, bucket = 0.0f;
                for (size_t f = 0; f < v.size(); ++f)
                {
                    const double t = (double) (pic.start + (int64_t) f * pic.hop);
                    if (t < v0 - sr * 0.05 || t > v1 + sr * 0.05)
                        continue;
                    const float x = timeToX (t);
                    const float a = amp (v[f]);
                    if (x - lastX < 1.5f && ! pts.empty())
                    {
                        bucket = std::max (bucket, a);
                        pts.back().y = bucket;
                        continue;
                    }
                    lastX = x;
                    bucket = a;
                    pts.push_back ({ x, a });
                }
                if (pts.size() < 2)
                    continue;
                for (const auto& p : pts)
                {
                    if (! started) { shape.startNewSubPath (p.x, mid - p.y * half); started = true; }
                    else shape.lineTo (p.x, mid - p.y * half);
                }
                for (auto it = pts.rbegin(); it != pts.rend(); ++it)
                    shape.lineTo (it->x, mid + it->y * half);
                shape.closeSubPath();
                if (layer == 0)
                {
                    g.setColour (kCream.withAlpha (0.16f));
                    g.fillPath (shape);
                    g.setColour (kCream.withAlpha (0.35f));
                    g.strokePath (shape, juce::PathStrokeType (0.8f));
                }
                else
                {
                    g.setColour (kLamp.withAlpha (0.22f));
                    g.fillPath (shape);
                }
            }
        }
        // Outside what has been captured: dimmer.
        double prevEnd = v0;
        for (const auto& s : snap.spans)
        {
            if ((double) s.start > prevEnd)
            {
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (timeToX (prevEnd), wa.getY(), timeToX ((double) s.start), wa.getBottom()));
            }
            prevEnd = std::max (prevEnd, (double) s.end);
        }
    }

    // Each sound: lit where it happens, and its handle in the lane.
    const float zeroY = dbToY (0.0f);
    for (const auto& s : snap.sounds)
    {
        if ((double) s.end < v0 || (double) s.start > v1)
            continue;
        const bool sel = state.selection.count (s.id) > 0;
        float x0 = timeToX ((double) s.start), x1 = timeToX ((double) s.end);
        if (x1 - x0 < 6.0f)
        {
            const float c = (x0 + x1) * 0.5f;
            x0 = c - 3.0f;
            x1 = c + 3.0f;
        }
        const float db = effectiveDb (s);
        const float depth = juce::jlimit (0.0f, 1.0f, -db / 18.0f);

        // In the waveform.
        const juce::Rectangle<float> col (x0, wa.getY() + 18.0f, x1 - x0, wa.getHeight() - 22.0f);
        g.setGradientFill (juce::ColourGradient (kLamp.withAlpha (sel ? 0.42f : 0.10f + 0.18f * depth), 0.0f, col.getY(),
                                                 kLamp.withAlpha (sel ? 0.16f : 0.04f), 0.0f, col.getBottom(), false));
        g.fillRoundedRectangle (col, 3.0f);
        g.setColour (kLamp.withAlpha (sel ? 0.9f : 0.45f));
        g.drawRoundedRectangle (col, 3.0f, sel ? 1.2f : 0.7f);
        if (x1 - x0 >= 14.0f || sel)
        {
            const auto tagR = juce::Rectangle<float> (x0 - 10.0f, wa.getY() + 2.0f, x1 - x0 + 20.0f, 14.0f);
            if (sel)
                dd::gui::drawGlowing (g, kindNames()[s.kind], tagR, kHot, LnF::font (10.5f, 700));
            else
                drawText (g, kindNames()[s.kind], tagR, kCream.withAlpha (0.7f), 10.0f, 700, juce::Justification::centred);
        }

        // In the lane: a bar from 0 dB to its reduction, and the handle.
        const float y = dbToY (db);
        const auto bar = juce::Rectangle<float>::leftTopRightBottom (x0 + 0.5f, std::min (zeroY, y), x1 - 0.5f, std::max (zeroY, y));
        g.setGradientFill (juce::ColourGradient (kLamp.withAlpha (s.manual ? 0.75f : 0.45f), 0.0f, bar.getY(),
                                                 juce::Colour (0xff9c4a20).withAlpha (0.5f), 0.0f, bar.getBottom(), false));
        g.fillRect (bar);
        const auto cap = juce::Rectangle<float> (x0 - 2.0f, y - 3.0f, x1 - x0 + 4.0f, 6.0f);
        if (sel)
        {
            g.setColour (kLamp.withAlpha (0.3f));
            g.fillRoundedRectangle (cap.expanded (3.0f), 4.0f);
        }
        g.setColour (sel ? kHot : (s.manual ? juce::Colour (0xffffc58a) : juce::Colour (0xffd9a070)));
        g.fillRoundedRectangle (cap, 2.5f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (cap.getX() + 1.0f, cap.getBottom() - 1.0f, cap.getWidth() - 2.0f, 1.0f);
        if (sel || x1 - x0 >= 30.0f)
            drawText (g, dbText (db), { x0 - 20.0f, y + 5.0f, x1 - x0 + 40.0f, 13.0f }, sel ? kHot : kCream.withAlpha (0.6f), 9.5f, 700,
                      juce::Justification::centred);
    }

    if (snap.spans.empty())
    {
        juce::String title = "PRESS PLAY", body;
        switch (proc.engine->getStatus())
        {
            case Status::Capturing: title = "CAPTURING"; body = "Hush is recording the track as it plays. Stop playback and every sharp sound appears here."; break;
            case Status::Analysing: title = "FINDING SOUNDS"; body = "Listening for every S, SH and T..."; break;
            case Status::Missing:   title = "TAKE NOT FOUND"; body = proc.engine->getMessage(); break;
            case Status::Listening:
            case Status::Ready:
                // Ableton: the take is tied to the song's timeline, which Session View clips don't follow.
                body = juce::String ("TRANSFER is lit: start playback")
                     + (dd::host().ableton ? " in Arrangement View (Session clips have no fixed place in the song)" : "")
                     + " and Hush captures the vocal on this track (until then it de-esses it live with Amount). Stop, and every "
                       "sharp sound appears here with its own level.\n\nOr drop a vocal file here, or press LOAD.";
                break;
        }
        const auto all = wa.getUnion (la);
        const auto box = all.withSizeKeepingCentre (std::min (580.0f, all.getWidth() - 40.0f), 170.0f);
        dd::gui::drawGlowing (g, title, box.withHeight (36.0f), kHot, LnF::font (24.0f, 700).withExtraKerningFactor (0.14f));
        g.setColour (kCream.withAlpha (0.72f));
        g.setFont (LnF::font (14.0f, 500));
        g.drawFittedText (body, box.withTrimmedTop (44.0f).toNearestInt(), juce::Justification::centredTop, 7, 0.9f);
    }
}

//==============================================================================
void SoundCanvas::applySounds (std::vector<Sound> next)
{
    std::sort (next.begin(), next.end(), [] (const Sound& a, const Sound& b) { return a.start < b.start; });
    snap.sounds = next;
    proc.engine->setSounds (std::move (next));
    seenVersion = proc.engine->getVersion();
    invalidate();
}

std::vector<Sound> SoundCanvas::selectedSounds() const
{
    std::vector<Sound> out;
    for (const auto& s : snap.sounds)
        if (state.selection.count (s.id) > 0)
            out.push_back (s);
    return out;
}

void SoundCanvas::editSelection (const std::function<void (Sound&)>& fn, bool allIfNone)
{
    const auto before = snap.sounds;
    auto next = before;
    const bool all = state.selection.empty() && allIfNone;
    bool any = false;
    for (auto& s : next)
        if (all || state.selection.count (s.id) > 0)
        {
            fn (s);
            any = true;
        }
    if (! any)
        return;
    applySounds (next);
    proc.engine->commit (before);
}

void SoundCanvas::selectKind (int kind)
{
    state.selection.clear();
    for (const auto& s : snap.sounds)
        if (s.kind == kind)
            state.selection.insert (s.id);
    state.changed();
    invalidate();
}

void SoundCanvas::togglePlay()
{
    auto& eng = *proc.engine;
    if (eng.isInternalPlaying())
        eng.stopPlaying();
    else if (! eng.isPlaying())
        eng.playFrom (state.cursor);
}

void SoundCanvas::mouseMove (const juce::MouseEvent& e)
{
    const int i = e.y > kRulerH && e.x > kScaleW ? soundAt (e.position) : -1;
    setMouseCursor (i < 0 ? (e.y <= kRulerH ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor)
                          : (e.position.y >= laneArea().getY() - 6.0f ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::PointingHandCursor));
    if (i >= 0)
    {
        const auto& s = snap.sounds[(size_t) i];
        setHint (kindNames()[s.kind] + " at " + timeText ((double) s.start / sampleRate()) + "   level " + juce::String (s.peakDb, 1)
                 + " dB   cut " + dbText (effectiveDb (s)) + (s.manual ? "  (set by hand)" : "  (from Amount)"));
    }
    else
        setHint (kHint);
}

void SoundCanvas::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    drag = {};
    drag.start = e.position;
    drag.before = snap.sounds;
    if (e.y <= kRulerH && e.x > kScaleW)
    {
        state.cursor = std::max<int64_t> (0, (int64_t) xToTime (e.position.x));
        drag.kind = Drag::Ruler;
        if (proc.engine->isInternalPlaying())
            proc.engine->playFrom (state.cursor);
        repaint();
        return;
    }
    if (e.x <= kScaleW)
        return;

    const int idx = soundAt (e.position);
    if (e.mods.isPopupMenu())
    {
        if (idx >= 0 && state.selection.count (snap.sounds[(size_t) idx].id) == 0)
            state.selection = { snap.sounds[(size_t) idx].id };
        state.changed();
        juce::PopupMenu m;
        const bool hasSel = ! state.selection.empty();
        m.addItem (1, "Back to Amount (automatic)", hasSel);
        m.addItem (2, "Leave untouched (0 dB)", hasSel);
        m.addItem (3, "-6 dB", hasSel);
        m.addItem (4, "-12 dB", hasSel);
        m.addItem (5, "-24 dB (almost gone)", hasSel);
        m.addSeparator();
        m.addItem (6, "Select every S");
        m.addItem (7, "Select every SH");
        m.addItem (8, "Select every T");
        m.addItem (9, "Select all");
        juce::Component::SafePointer<SoundCanvas> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe] (int r)
        {
            if (safe == nullptr || r == 0)
                return;
            auto& c = *safe;
            auto set = [&c] (float db) { c.editSelection ([db] (Sound& s) { s.manual = true; s.userDb = db; }); };
            switch (r)
            {
                case 1: c.editSelection ([] (Sound& s) { s.manual = false; }); break;
                case 2: set (0.0f); break;
                case 3: set (-6.0f); break;
                case 4: set (-12.0f); break;
                case 5: set (-24.0f); break;
                case 6: c.selectKind (0); break;
                case 7: c.selectKind (1); break;
                case 8: c.selectKind (2); break;
                case 9: for (const auto& s : c.snap.sounds) c.state.selection.insert (s.id); c.state.changed(); c.invalidate(); break;
                default: break;
            }
        });
        return;
    }

    if (idx >= 0)
    {
        const auto& s = snap.sounds[(size_t) idx];
        if (e.mods.isShiftDown() || e.mods.isCommandDown())
        {
            if (state.selection.count (s.id) > 0) state.selection.erase (s.id);
            else state.selection.insert (s.id);
        }
        else if (state.selection.count (s.id) == 0)
            state.selection = { s.id };
        drag.anchor = s.id;
        drag.kind = Drag::Level;
        state.cursor = s.start;
        if (! proc.engine->isPlaying() || proc.engine->isInternalPlaying())
        {
            const auto pre = (int64_t) (0.15 * sampleRate());
            proc.engine->playFrom (std::max<int64_t> (0, s.start - pre), s.end + pre);   // hear it in its word
        }
    }
    else
    {
        if (! e.mods.isShiftDown() && ! e.mods.isCommandDown())
            state.selection.clear();
        drag.kind = Drag::Rubber;
        drag.baseSelection = state.selection;
        state.cursor = std::max<int64_t> (0, (int64_t) xToTime (e.position.x));
    }
    state.changed();
    invalidate();
}

void SoundCanvas::mouseDrag (const juce::MouseEvent& e)
{
    switch (drag.kind)
    {
        case Drag::None:
            return;
        case Drag::Ruler:
            state.cursor = std::max<int64_t> (0, (int64_t) xToTime (e.position.x));
            repaint();
            return;
        case Drag::Rubber:
        {
            drag.rubber = juce::Rectangle<float> (drag.start, e.position);
            state.selection = drag.baseSelection;
            for (const auto& s : snap.sounds)
            {
                const float x0 = timeToX ((double) s.start), x1 = std::max (x0 + 4.0f, timeToX ((double) s.end));
                if (drag.rubber.getRight() >= x0 && drag.rubber.getX() <= x1)
                    state.selection.insert (s.id);
            }
            state.changed();
            invalidate();
            return;
        }
        case Drag::Level:
        {
            const float dy = e.position.y - drag.start.y;
            if (! drag.changed && std::abs (dy) < 3.0f)
                return;
            proc.engine->stopPlaying();
            const float perPx = (kTopDb - kBottomDb) / laneArea().getHeight();
            float delta = -dy * perPx * (e.mods.isAltDown() ? 0.25f : 1.0f);
            if (! e.mods.isAltDown())
                delta = std::round (delta * 2.0f) / 2.0f;
            auto next = drag.before;
            float shown = 0.0f;
            for (auto& s : next)
                if (state.selection.count (s.id) > 0)
                {
                    const float start = effectiveDb (s);
                    s.userDb = juce::jlimit (kBottomDb, kTopDb, std::round ((start + delta) * 10.0f) / 10.0f);
                    s.manual = true;
                    if (s.id == drag.anchor)
                        shown = s.userDb;
                }
            setHint ("Cut " + dbText (shown) + (state.selection.size() > 1 ? "  (" + juce::String ((int) state.selection.size()) + " sounds move together)" : juce::String()));
            applySounds (next);
            drag.changed = true;
            return;
        }
    }
}

void SoundCanvas::mouseUp (const juce::MouseEvent&)
{
    if (drag.changed)
        proc.engine->commit (drag.before);
    drag.kind = Drag::None;
    drag.rubber = {};
    invalidate();
}

void SoundCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y <= kRulerH && e.x > kScaleW)
    {
        state.cursor = std::max<int64_t> (0, (int64_t) xToTime (e.position.x));
        if (! proc.engine->isPlaying() || proc.engine->isInternalPlaying())
            proc.engine->playFrom (state.cursor);
        return;
    }
    if (soundAt (e.position) >= 0)
        editSelection ([] (Sound& s) { s.manual = false; });
}

void SoundCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto wa = waveArea();
    if (e.mods.isCommandDown() || e.mods.isCtrlDown() || e.mods.isAltDown())
    {
        const double factor = std::exp (-(double) w.deltaY * 1.2);
        const double at = state.viewStart + (double) (e.position.x - wa.getX()) / wa.getWidth() * state.viewSeconds;
        state.viewSeconds *= factor;
        clampView();
        state.viewStart = at - (double) (e.position.x - wa.getX()) / wa.getWidth() * state.viewSeconds;
    }
    else
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        state.viewStart -= (double) d * state.viewSeconds * 0.6;
    }
    clampView();
    state.changed();
    invalidate();
}

void SoundCanvas::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    const auto wa = waveArea();
    const double at = state.viewStart + (double) (e.position.x - wa.getX()) / wa.getWidth() * state.viewSeconds;
    state.viewSeconds /= std::max (0.1, (double) scaleFactor);
    clampView();
    state.viewStart = at - (double) (e.position.x - wa.getX()) / wa.getWidth() * state.viewSeconds;
    clampView();
    state.changed();
    invalidate();
}

bool SoundCanvas::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    auto& eng = *proc.engine;
    if (mods.isCommandDown() && (code == 'Z' || code == 'z'))
    {
        if (mods.isShiftDown()) eng.redo(); else eng.undo();
        return true;
    }
    if (mods.isCommandDown() && (code == 'Y' || code == 'y'))
    {
        eng.redo();
        return true;
    }
    if (mods.isCommandDown() && (code == 'A' || code == 'a'))
    {
        for (const auto& s : snap.sounds)
            state.selection.insert (s.id);
        state.changed();
        invalidate();
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        editSelection ([] (Sound& s) { s.manual = true; s.userDb = 0.0f; });
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const float step = (code == juce::KeyPress::upKey ? 1.0f : -1.0f) * (mods.isAltDown() ? 0.25f : 1.0f);
        const auto st = settings;
        const float ref = snap.refDb;
        editSelection ([step, st, ref] (Sound& s)
        {
            s.userDb = juce::jlimit (kBottomDb, kTopDb, reductionDb (s, st, ref) + step);
            s.manual = true;
        });
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        if (snap.sounds.empty())
            return true;
        const bool left = code == juce::KeyPress::leftKey;
        int first = -1, last = -1;
        for (int i = 0; i < (int) snap.sounds.size(); ++i)
            if (state.selection.count (snap.sounds[(size_t) i].id) > 0)
            {
                if (first < 0) first = i;
                last = i;
            }
        const int idx = first < 0 ? 0 : juce::jlimit (0, (int) snap.sounds.size() - 1, left ? first - 1 : last + 1);
        const auto& s = snap.sounds[(size_t) idx];
        state.selection = { s.id };
        state.cursor = s.start;
        const double t = (double) s.start / sampleRate();
        if (t < state.viewStart || t > state.viewStart + state.viewSeconds * 0.9)
            state.viewStart = std::max (0.0, t - state.viewSeconds * 0.3);
        if (! eng.isPlaying() || eng.isInternalPlaying())
        {
            const auto pre = (int64_t) (0.15 * sampleRate());
            eng.playFrom (std::max<int64_t> (0, s.start - pre), s.end + pre);
        }
        state.changed();
        invalidate();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        state.selection.clear();
        state.changed();
        invalidate();
        return true;
    }
    if (key == juce::KeyPress::spaceKey)
    {
        // While the song plays, Space belongs to the DAW (it stops the transport).
        if (proc.engine->isPlaying() && ! proc.engine->isInternalPlaying())
            return false;
        togglePlay();
        return true;
    }
    return false;
}

bool SoundCanvas::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (dd::takefiles::canLoad (juce::File (f)))
            return true;
    return false;
}

void SoundCanvas::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (! dd::takefiles::canLoad (file))
            continue;
        auto& eng = *proc.engine;
        eng.loadFile (file, eng.isPlaying() ? 0 : eng.getPlayhead());
        fitPending = true;
        state.selection.clear();
        state.changed();
        break;
    }
    repaint();
}

//==============================================================================
Overview::Overview (HushProcessor& p, EditorState& s) : proc (p), state (s)
{
    startTimerHz (10);
}

void Overview::jump (float x)
{
    const double len = state.takeEnd - state.takeStart;
    if (len <= 0.0)
        return;
    const double t = state.takeStart + (double) juce::jlimit (0.0f, 1.0f, x / (float) getWidth()) * len;
    state.viewStart = std::max (0.0, t - state.viewSeconds * 0.5);
    state.changed();
}

void Overview::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    dd::gui::paintConsole (g, r, 5.0f, 0.4f);
    const double len = state.takeEnd - state.takeStart;
    if (len <= 0.0)
    {
        dd::gui::drawCaption (g, "OVERVIEW: THE WHOLE TAKE APPEARS HERE", r.reduced (10.0f, 0.0f), juce::Justification::centredLeft, 9.5f);
        return;
    }
    const auto snapshot = proc.engine->snapshot();
    const auto st = proc.engine->currentSettings();
    const double sr = snapshot.sampleRate;
    auto xOf = [&] (double seconds) { return r.getX() + (float) ((seconds - state.takeStart) / len) * r.getWidth(); };
    for (const auto& s : snapshot.sounds)
    {
        const float depth = juce::jlimit (0.0f, 1.0f, -reductionDb (s, st, snapshot.refDb) / 18.0f);
        const float x = xOf ((double) s.start / sr);
        g.setColour (kLamp.withAlpha (0.35f + 0.6f * depth));
        g.fillRect (x, r.getBottom() - 5.0f - depth * (r.getHeight() - 10.0f), 1.5f, 2.0f + depth * (r.getHeight() - 10.0f));
    }
    const float vx0 = xOf (state.viewStart), vx1 = xOf (state.viewStart + state.viewSeconds);
    const auto view = juce::Rectangle<float>::leftTopRightBottom (std::max (r.getX(), vx0), r.getY() + 1.0f, std::min (r.getRight(), vx1), r.getBottom() - 1.0f);
    g.setColour (kCream.withAlpha (0.07f));
    g.fillRoundedRectangle (view, 3.0f);
    g.setColour (kCream.withAlpha (0.45f));
    g.drawRoundedRectangle (view, 3.0f, 1.0f);
    const float px = xOf ((double) proc.engine->getPlayhead() / sr);
    if (px >= r.getX() && px <= r.getRight())
    {
        g.setColour (kLamp);
        g.fillRect (px - 0.5f, r.getY(), 1.0f, r.getHeight());
    }
}

//==============================================================================
SoundEditor::SoundEditor (HushProcessor& p, EditorState& s)
    : proc (p), state (s), canvas (p, s), overview (p, s)
{
    sKey.onClick = [this] { canvas.selectKind (0); };
    shKey.onClick = [this] { canvas.selectKind (1); };
    tKey.onClick = [this] { canvas.selectKind (2); };
    autoKey.onClick = [this] { canvas.editSelection ([] (Sound& x) { x.manual = false; }, true); };
    bypassKey.onClick = [this] { canvas.editSelection ([] (Sound& x) { x.manual = true; x.userDb = 0.0f; }); };
    undoKey.onClick = [this] { proc.engine->undo(); };
    redoKey.onClick = [this] { proc.engine->redo(); };
    fitKey.onClick = [this] { canvas.zoomToFit(); };
    followKey.onClick = [this] { state.follow = ! state.follow; state.changed(); };
    autoKey.setTooltip ("Selected sounds (or all, when none are selected) go back to being set by Amount");
    bypassKey.setTooltip ("Selected sounds are left untouched (0 dB)");
    for (auto* k : { &sKey, &shKey, &tKey, &autoKey, &bypassKey, &undoKey, &redoKey, &fitKey, &followKey })
        addAndMakeVisible (*k);
    addAndMakeVisible (overview);
    addAndMakeVisible (canvas);
    state.addChangeListener (this);
    state.hint = kHint;
    startTimerHz (8);
}

SoundEditor::~SoundEditor()
{
    state.removeChangeListener (this);
}

void SoundEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    followKey.setLit (state.follow);
    repaint (getLocalBounds().removeFromBottom (24));
    overview.repaint();
}

void SoundEditor::timerCallback()
{
    undoKey.setAlpha (proc.engine->canUndo() ? 1.0f : 0.45f);
    redoKey.setAlpha (proc.engine->canRedo() ? 1.0f : 0.45f);
}

void SoundEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().removeFromBottom (22.0f);
    g.setColour (dd::gui::tokens().cream500);
    g.setFont (LnF::font (11.5f, 500));
    g.drawFittedText (state.hint, r.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
}

void SoundEditor::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (40);
    constexpr int glow = 6;
    for (auto* k : { &sKey, &shKey, &tKey })
    {
        k->setGlowMargin (glow);
        k->setBounds (bar.removeFromLeft (70).expanded (glow, glow));
        bar.removeFromLeft (6);
    }
    bar.removeFromLeft (14);
    for (auto* k : { &autoKey, &bypassKey })
    {
        k->setGlowMargin (glow);
        k->setBounds (bar.removeFromLeft (74).expanded (glow, glow));
        bar.removeFromLeft (6);
    }
    for (auto* k : { &followKey, &fitKey, &redoKey, &undoKey })
    {
        k->setGlowMargin (glow);
        k->setBounds (bar.removeFromRight (64).expanded (glow, glow));
        bar.removeFromRight (6);
    }
    r.removeFromTop (8);
    overview.setBounds (r.removeFromTop (30));
    r.removeFromTop (6);
    r.removeFromBottom (24);
    canvas.setBounds (r);
}

//==============================================================================
Inspector::Inspector (HushProcessor& p, EditorState& s, SoundCanvas& c) : proc (p), state (s), canvas (c)
{
    auto nudge = [this] (float step)
    {
        const auto st = canvas.getSettings();
        const float ref = proc.engine->snapshot().refDb;
        canvas.editSelection ([step, st, ref] (Sound& x)
        {
            x.userDb = juce::jlimit (SoundCanvas::kBottomDb, SoundCanvas::kTopDb, reductionDb (x, st, ref) + step);
            x.manual = true;
        });
    };
    lessKey.onClick = [nudge] { nudge (-1.0f); };
    moreKey.onClick = [nudge] { nudge (1.0f); };
    for (auto* k : { &lessKey, &moreKey })
    {
        k->setGlowMargin (5);
        addAndMakeVisible (*k);
    }
    state.addChangeListener (this);
    startTimerHz (6);
}

Inspector::~Inspector()
{
    state.removeChangeListener (this);
}

void Inspector::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto keys = r.removeFromBottom (34);
    moreKey.setBounds (keys.removeFromRight (78).expanded (5));
    keys.removeFromRight (8);
    lessKey.setBounds (keys.removeFromRight (78).expanded (5));
}

void Inspector::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    dd::gui::paintConsole (g, r.withTrimmedBottom (46.0f), 7.0f, 0.5f);
    auto area = r.withTrimmedBottom (46.0f).reduced (14.0f, 10.0f);
    const auto sel = canvas.selectedSounds();
    dd::gui::drawCaption (g, sel.size() > 1 ? juce::String ((int) sel.size()) + " SOUNDS SELECTED" : "SELECTED SOUND", area.removeFromTop (14.0f),
                          juce::Justification::centredLeft, 9.5f);
    if (sel.empty())
    {
        const auto snap = proc.engine->snapshot();
        int counts[3] {};
        for (const auto& s : snap.sounds)
            ++counts[juce::jlimit (0, 2, s.kind)];
        drawText (g, snap.sounds.empty() ? juce::String ("Click a lit sound to see it here.")
                                         : juce::String ((int) snap.sounds.size()) + " sharp sounds: " + juce::String (counts[0]) + " S, "
                                               + juce::String (counts[1]) + " SH, " + juce::String (counts[2]) + " T. Click one, or use ALL S / ALL SH / ALL T.",
                  area, kCream.withAlpha (0.6f), 13.0f, 500, juce::Justification::topLeft);
        return;
    }
    const auto& s = sel.front();
    const float db = canvas.effectiveDb (s);
    dd::gui::drawGlowing (g, sel.size() > 1 ? juce::String ((int) sel.size()) + " sounds" : kindNames()[s.kind] + "   " + dbText (db),
                          area.removeFromTop (40.0f), kHot, LnF::font (28.0f, 700), juce::Justification::centredLeft);
    auto row = [&] (const juce::String& label, const juce::String& value)
    {
        auto line = area.removeFromTop (17.0f);
        drawText (g, label, line.removeFromLeft (92.0f), kCream.withAlpha (0.55f), 12.0f, 600);
        drawText (g, value, line, kCream.withAlpha (0.92f), 12.5f, 600);
    };
    const double sr = proc.engine->getModelRate();
    if (sel.size() == 1)
    {
        row ("At", timeText ((double) s.start / sr));
        row ("Length", juce::String ((double) (s.end - s.start) / sr * 1000.0, 0) + " ms");
        row ("Level", juce::String (s.peakDb, 1) + " dB  (sibilance band)");
        row ("Centre", juce::String (s.hz / 1000.0f, 1) + " kHz");
        row ("Cut", dbText (db) + (s.manual ? "  set by hand" : "  from Amount"));
    }
    else
    {
        float lo = 0.0f, hi = -100.0f;
        int manual = 0;
        for (const auto& x : sel)
        {
            const float d = canvas.effectiveDb (x);
            lo = std::min (lo, d);
            hi = std::max (hi, d);
            manual += x.manual ? 1 : 0;
        }
        row ("Cuts", dbText (lo) + " to " + dbText (hi));
        row ("By hand", juce::String (manual) + " of " + juce::String ((int) sel.size()));
        row ("Tip", "Drag one handle: they all move together");
    }
}

//==============================================================================
void SessionView::DragKey::mouseDrag (const juce::MouseEvent& e)
{
    Key::mouseDrag (e);
    if (owner.dragging || e.getDistanceFromDragStart() < 6)
        return;
    const auto f = owner.proc.engine->getLastBounce();
    if (! f.existsAsFile())
        return;
    owner.dragging = true;
    juce::Component::SafePointer<SessionView> safe (&owner);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [safe]
    {
        if (safe != nullptr)
            safe->dragging = false;
    });
}

SessionView::SessionView (HushProcessor& p, EditorState& s, SoundCanvas& c) : proc (p), state (s), canvas (c)
{
    playKey.onClick = [this] { canvas.togglePlay(); };
    loadKey.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a vocal", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.caf");
        juce::Component::SafePointer<SessionView> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || ! fc.getResult().existsAsFile())
                return;
            auto& eng = *safe->proc.engine;
            eng.loadFile (fc.getResult(), eng.isPlaying() ? 0 : eng.getPlayhead());
        });
    };
    bounceKey.onClick = [this] { proc.engine->bounce(); };
    clearKey.onClick = [this]
    {
        juce::Component::SafePointer<SessionView> safe (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Clear the take?",
                                            "Hush forgets this take and every level you set, and listens for the next playback.",
                                            "Clear", "Cancel", this,
                                            juce::ModalCallbackFunction::create ([safe] (int r)
                                            {
                                                if (safe != nullptr && r == 1)
                                                    safe->proc.engine->clearTake();
                                            }));
    };
    for (auto* k : { &playKey, &loadKey, &bounceKey, (dd::gui::Key*) &dragKey, &clearKey })
    {
        k->setGlowMargin (5);
        addAndMakeVisible (*k);
    }
    dragKey.setTooltip ("After BOUNCE: drag this into your playlist. The file starts at the song's beginning, so drop it at bar 1.");
    bounceKey.setTooltip ("Renders the de-essed take to a WAV file in Music/ddeonmadeit/Hush/Bounces");
    loadKey.setTooltip ("Load a vocal file (or drop one on the sounds view)");
    startTimerHz (8);
}

void SessionView::timerCallback()
{
    auto& eng = *proc.engine;
    playKey.setLit (eng.isInternalPlaying());
    playKey.setLabel (eng.isInternalPlaying() ? "Stop" : "Play", false);
    dragKey.setLit (eng.getBounceCount() > 0 && eng.getLastBounce().existsAsFile());
    repaint (getLocalBounds().withHeight (getHeight() - 96));
}

void SessionView::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto row2 = r.removeFromBottom (36);
    r.removeFromBottom (8);
    auto row1 = r.removeFromBottom (36);
    const int w = (row1.getWidth() - 16) / 3;
    for (auto* k : { &playKey, &loadKey, &clearKey })
    {
        k->setBounds (row1.removeFromLeft (w).expanded (5));
        row1.removeFromLeft (8);
    }
    const int w2 = (row2.getWidth() - 8) / 2;
    for (auto* k : { &bounceKey, (dd::gui::Key*) &dragKey })
    {
        k->setBounds (row2.removeFromLeft (w2).expanded (5));
        row2.removeFromLeft (8);
    }
}

void SessionView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto screen = r.withTrimmedBottom (100.0f);
    dd::gui::paintConsole (g, screen, 7.0f, 0.5f);
    auto& eng = *proc.engine;
    auto area = screen.reduced (14.0f, 10.0f);
    dd::gui::drawCaption (g, "TRANSFER", area.removeFromTop (14.0f), juce::Justification::centredLeft, 9.5f);
    juce::String word, line;
    float led = 0.0f;
    switch (eng.getStatus())
    {
        case Status::Listening: word = "LISTENING"; line = eng.hasTake() ? "Next playback replaces what it plays over." : "De-essing live. Press play to capture the vocal."; led = dd::gui::ledPulse (dd::gui::animationTime()); break;
        case Status::Capturing: word = "CAPTURING"; line = "Recording the track as it plays. Stop when done."; led = 1.0f; break;
        case Status::Analysing: word = "FINDING SOUNDS"; line = "Listening for every S, SH and T..."; led = dd::gui::ledPulse (dd::gui::animationTime(), 0.6); break;
        case Status::Ready:     word = "READY"; line = "Playback plays your levels, in time with the song."; led = 0.0f; break;
        case Status::Missing:   word = "TAKE MISSING"; line = eng.getMessage(); led = 1.0f; break;
    }
    const auto msg = eng.getMessage();
    if (msg.isNotEmpty() && eng.getStatus() != Status::Missing)
        line = msg;
    auto top = area.removeFromTop (34.0f);
    dd::gui::paintLamp (g, { top.getX() + 7.0f, top.getCentreY() }, 5.0f, kLamp, led);
    dd::gui::drawGlowing (g, word, top.withTrimmedLeft (20.0f), kHot, LnF::font (20.0f, 700).withExtraKerningFactor (0.1f), juce::Justification::centredLeft);
    drawText (g, line, area, kCream.withAlpha (0.65f), 12.0f, 500, juce::Justification::topLeft);
}

//==============================================================================
TransferKey::TransferKey (HushProcessor& p) : Key ("Transfer", true), proc (p)
{
    onClick = [this] { proc.engine->setArmed (! proc.engine->isArmed()); };
    setTooltip ("Lit: the next playback is captured (replacing what it plays over). Unlit: playback plays your levels.");
    getProperties().set ("ddFlickerPeriod", 6.1);
    setLit (proc.engine->isArmed());
    poll.fn = [this] { setLit (proc.engine->isArmed()); };
    poll.startTimerHz (10);
}
} // namespace hush
