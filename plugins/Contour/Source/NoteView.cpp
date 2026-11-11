#include "NoteView.h"

namespace contour
{
namespace
{
namespace sunset = dd::gui::sunset;
using LnF = dd::gui::LookAndFeel;

const juce::Colour kLamp { 0xffff9652 }, kHot { 0xffffd9a0 }, kCream { 0xfff2e6d6 };

juce::String toolName (Tool t)
{
    switch (t)
    {
        case Tool::Note:    return "Note";
        case Tool::Vibrato: return "Vibrato";
        case Tool::Drift:   return "Drift";
        case Tool::Formant: return "Formant";
        case Tool::Level:   return "Level";
        case Tool::Split:   return "Split";
    }
    return {};
}

juce::String toolHint (Tool t)
{
    switch (t)
    {
        case Tool::Note:    return "NOTE: drag up/down to change pitch (snaps to the scale, hold Alt for fine), sideways to move in time, edges to stretch. Double-click snaps to the scale.";
        case Tool::Vibrato: return "VIBRATO: drag up/down on a note to deepen or flatten its vibrato. Double-click flattens / restores it.";
        case Tool::Drift:   return "DRIFT: drag up/down to steady (or exaggerate) the slow wander of a held note. Double-click steadies / restores.";
        case Tool::Formant: return "FORMANT: drag up/down to make the voice smaller and brighter or bigger and darker without changing the note.";
        case Tool::Level:   return "LEVEL: drag up/down to change a note's volume. Double-click mutes / unmutes. Delete mutes the selection.";
        case Tool::Split:   return "SPLIT: click inside a note to cut it in two. Double-click on a cut to join the notes again.";
    }
    return {};
}

juce::String noteWithCents (float midi)
{
    const int r = (int) std::lround (midi);
    const int c = juce::roundToInt ((midi - (float) r) * 100.0f);
    return noteName (r) + (c == 0 ? juce::String() : (c > 0 ? " +" : " ") + juce::String (c) + "c");
}

float stepToScale (float base, int dir, const Settings& s)
{
    if (s.scale == 0)
        return (float) std::lround (base) + (float) dir;
    const auto mask = scaleMask (s.scale);
    int c = (int) std::lround (base) + dir;
    for (int i = 0; i < 12 && ! inScale (c, s.key, mask); ++i)
        c += dir;
    return (float) c;
}

float snapPitch (float base, const Settings& s)
{
    return s.scale == 0 ? (float) std::lround (base) : snapToScale (base, s.key, s.scale);
}

void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour c, float size, int weight,
               juce::Justification just = juce::Justification::centredLeft)
{
    g.setColour (c);
    g.setFont (LnF::font (size, weight));
    g.drawFittedText (text, r.toNearestInt(), just, 2, 0.8f);
}
} // namespace

//==============================================================================
NoteCanvas::NoteCanvas (ContourProcessor& p, EditorState& s) : proc (p), state (s)
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    state.addChangeListener (this);
    refresh (true);
    startTimerHz (30);
}

NoteCanvas::~NoteCanvas()
{
    state.removeChangeListener (this);
}

juce::Rectangle<float> NoteCanvas::noteArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft ((float) kKeysW).withTrimmedTop ((float) kRulerH);
}

float NoteCanvas::timeToX (double samples) const
{
    const auto a = noteArea();
    return a.getX() + (float) ((samples / sampleRate() - state.viewStart) * a.getWidth() / state.viewSeconds);
}

double NoteCanvas::xToTime (float x) const
{
    const auto a = noteArea();
    return (state.viewStart + (double) (x - a.getX()) * state.viewSeconds / a.getWidth()) * sampleRate();
}

float NoteCanvas::midiToY (float midi) const { return noteArea().getY() + (topMidi - midi) * pxPerSemi; }
float NoteCanvas::yToMidi (float y) const    { return topMidi - (y - noteArea().getY()) / pxPerSemi; }

void NoteCanvas::resized()
{
    state.viewSeconds = std::max (0.2, state.viewSeconds);
    if (fitPending && ! snap.notes.empty() && getHeight() > kRulerH + 20)
    {
        fitPending = false;
        zoomToFit();
    }
    invalidate();
}

void NoteCanvas::refresh (bool force)
{
    auto& eng = *proc.engine;
    const auto v = eng.getVersion();
    const auto s = eng.currentSettings();
    if (! force && v == seenVersion && s == settings)
        return;
    const bool settingsChanged = s != settings;
    snap = eng.snapshot();
    settings = s;
    seenVersion = v;

    std::set<uint32_t> keep;
    for (const auto& n : snap.notes)
        if (state.selection.count (n.id) > 0)
            keep.insert (n.id);
    const bool selChanged = keep != state.selection;
    state.selection = keep;

    if (! snap.spans.empty())
    {
        state.takeStart = (double) snap.spans.front().start / sampleRate();
        state.takeEnd = (double) snap.spans.back().end / sampleRate();
    }
    else
        state.takeStart = state.takeEnd = 0.0;

    if (snap.notes.empty())
        fitPending = true;
    else if (fitPending && getWidth() > 0)
    {
        fitPending = false;
        zoomToFit();
    }
    if (selChanged || settingsChanged || force)
        state.changed();
    invalidate();
}

void NoteCanvas::timerCallback()
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

void NoteCanvas::setHint (const juce::String& h)
{
    if (state.hint != h)
    {
        state.hint = h;
        state.changed();
    }
}

double NoteCanvas::gridSamples() const
{
    const double bpm = proc.engine->getBpm();
    return bpm > 1.0 ? sampleRate() * 60.0 / bpm / 4.0 : 0.0;   // sixteenth notes
}

double NoteCanvas::gridOrigin() const
{
    const double bpm = proc.engine->getBpm();
    return bpm > 1.0 ? -proc.engine->getBeatAtZero() * 60.0 / bpm * sampleRate() : 0.0;
}

void NoteCanvas::clampView()
{
    const double takeLen = std::max (10.0, state.takeEnd - state.takeStart);
    state.viewSeconds = juce::jlimit (0.15, takeLen * 1.6 + 5.0, state.viewSeconds);
    state.viewStart = juce::jlimit (0.0, std::max (0.0, state.takeEnd + 2.0 - state.viewSeconds * 0.5), state.viewStart);
    const float rows = noteArea().getHeight() / pxPerSemi;
    topMidi = juce::jlimit (24.0f + rows, 110.0f, topMidi);
}

void NoteCanvas::zoomToFit()
{
    double t0 = 1.0e30, t1 = -1.0e30;
    float lo = 200.0f, hi = -200.0f;
    for (const auto& n : snap.notes)
    {
        t0 = std::min (t0, (double) n.newStart);
        t1 = std::max (t1, (double) n.newEnd);
        lo = std::min (lo, displayPitch (n));
        hi = std::max (hi, displayPitch (n));
    }
    if (snap.notes.empty())
    {
        if (snap.spans.empty())
            return;
        t0 = (double) snap.spans.front().start;
        t1 = (double) snap.spans.back().end;
        lo = 55.0f;
        hi = 72.0f;
    }
    const double a = t0 / sampleRate(), b = t1 / sampleRate();
    state.viewSeconds = std::max (0.5, (b - a) * 1.06);
    state.viewStart = std::max (0.0, a - (b - a) * 0.03);
    const float h = noteArea().getHeight();
    pxPerSemi = juce::jlimit (8.0f, 36.0f, h / std::max (8.0f, hi - lo + 7.0f));
    topMidi = (hi + lo) * 0.5f + h / pxPerSemi * 0.5f;
    clampView();
    state.changed();
    invalidate();
}

//==============================================================================
juce::Path NoteCanvas::blobPath (const Note& n) const
{
    juce::Path p;
    const float x0 = timeToX ((double) n.newStart), x1 = timeToX ((double) n.newEnd);
    const float yc = midiToY (displayPitch (n));
    std::vector<std::pair<float, float>> pts;   // x, half thickness
    if (snap.curve != nullptr)
    {
        const auto& cp = snap.curve->points;
        const float peak = snap.curve->peakLevel;
        auto it = std::lower_bound (cp.begin(), cp.end(), n.newStart, [] (const CurvePoint& c, int64_t t) { return c.t < t; });
        float bucketX = -1.0e9f, bucketH = 0.0f;
        for (; it != cp.end() && it->t <= n.newEnd; ++it)
        {
            const float norm = juce::jlimit (0.0f, 1.0f, (it->level - (peak - 42.0f)) / 42.0f);
            const float h = std::max (2.0f, pxPerSemi * (0.14f + 0.5f * norm));
            const float x = timeToX ((double) it->t);
            if (x - bucketX < 2.5f && ! pts.empty())
            {
                bucketH = std::max (bucketH, h);
                pts.back().second = bucketH;
                continue;
            }
            bucketX = x;
            bucketH = h;
            pts.push_back ({ x, h });
        }
    }
    if (pts.size() < 2)
    {
        p.addRoundedRectangle (x0, yc - pxPerSemi * 0.3f, std::max (2.0f, x1 - x0), pxPerSemi * 0.6f, pxPerSemi * 0.3f);
        return p;
    }
    pts.front().first = x0;
    pts.back().first = x1;
    auto taper = [&] (float x) { return std::sqrt (juce::jlimit (0.0f, 1.0f, std::min (x - x0, x1 - x) / 5.0f)); };
    p.startNewSubPath (x0, yc);
    for (const auto& [x, h] : pts)
        p.lineTo (x, yc - h * taper (x));
    p.lineTo (x1, yc);
    for (auto it = pts.rbegin(); it != pts.rend(); ++it)
        p.lineTo (it->first, yc + it->second * taper (it->first));
    p.closeSubPath();
    return p;
}

int NoteCanvas::noteAt (juce::Point<float> pos, bool* nearLeft, bool* nearRight) const
{
    int best = -1;
    float bestD = 1.0e9f;
    for (int i = 0; i < (int) snap.notes.size(); ++i)
    {
        const auto& n = snap.notes[(size_t) i];
        const float x0 = timeToX ((double) n.newStart), x1 = timeToX ((double) n.newEnd);
        if (pos.x < x0 - 2.0f || pos.x > x1 + 2.0f)
            continue;
        const float d = std::abs (pos.y - midiToY (displayPitch (n)));
        if (d <= std::max (pxPerSemi * 0.7f, 7.0f) && d < bestD)
        {
            bestD = d;
            best = i;
            const float edge = x1 - x0 > 18.0f ? 6.0f : 0.0f;
            if (nearLeft != nullptr)  *nearLeft = pos.x < x0 + edge;
            if (nearRight != nullptr) *nearRight = pos.x > x1 - edge;
        }
    }
    return best;
}

//==============================================================================
void NoteCanvas::paint (juce::Graphics& g)
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

    const auto area = noteArea();
    auto& eng = *proc.engine;

    // Play cursor (where PLAY starts) on the ruler.
    {
        const float cx = timeToX ((double) state.cursor);
        if (cx >= area.getX() && cx <= area.getRight())
        {
            juce::Path tri;
            tri.addTriangle (cx - 5.0f, 4.0f, cx + 5.0f, 4.0f, cx, (float) kRulerH - 4.0f);
            g.setColour (kCream.withAlpha (0.8f));
            g.fillPath (tri);
        }
    }

    // Playhead and the lit key of the sounding note.
    const auto ph = eng.getPlayhead();
    const float px = timeToX ((double) ph);
    if (px >= area.getX() && px <= area.getRight() && (eng.isPlaying() || ph > 0))
    {
        g.setColour (kLamp.withAlpha (0.18f));
        g.fillRect (px - 3.0f, 0.0f, 6.0f, (float) getHeight());
        g.setColour (kLamp.withAlpha (eng.isPlaying() ? 0.95f : 0.5f));
        g.fillRect (px - 0.75f, 0.0f, 1.5f, (float) getHeight());
        if (eng.isPlaying())
            for (const auto& n : snap.notes)
                if (ph >= n.newStart && ph < n.newEnd && ! n.muted)
                {
                    const float y0 = midiToY ((float) std::lround (displayPitch (n)) + 0.5f);
                    juce::Rectangle<float> key (2.0f, y0, (float) kKeysW - 4.0f, pxPerSemi);
                    g.setColour (kLamp.withAlpha (0.35f));
                    g.fillRoundedRectangle (key.expanded (2.0f), 3.0f);
                    g.setColour (kLamp);
                    g.fillRoundedRectangle (key, 2.0f);
                }
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
        g.setColour (kLamp.withAlpha (0.12f));
        g.fillRoundedRectangle (area.reduced (8.0f), 8.0f);
        g.setColour (kLamp.withAlpha (0.8f));
        g.drawRoundedRectangle (area.reduced (8.0f), 8.0f, 2.0f);
        dd::gui::drawGlowing (g, "DROP TO LOAD THIS VOCAL", area, kHot, LnF::font (18.0f, 700).withExtraKerningFactor (0.12f));
    }
}

void NoteCanvas::paintEmpty (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto& eng = *proc.engine;
    juce::String title, body;
    switch (eng.getStatus())
    {
        case Status::Capturing:
            title = "CAPTURING";
            body = "Contour is recording the track as it plays. Stop playback and the notes appear here.";
            break;
        case Status::Analysing:
            title = "FINDING NOTES";
            body = "Analysing the pitch of every moment of the take...";
            break;
        case Status::Missing:
            title = "TAKE NOT FOUND";
            body = eng.getMessage();
            break;
        case Status::Listening:
        case Status::Ready:
            title = "PRESS PLAY";
            // Ableton: the take is tied to the song's timeline, which Session View clips don't follow.
            body = juce::String ("TRANSFER is lit: start playback ")
                 + (dd::host().ableton ? "in Arrangement View (Session clips have no fixed place in the song)" : "in your DAW")
                 + " and Contour captures the vocal on this track (play the part you want to edit). Stop, and every note "
                   "appears here, ready to edit.\n\nOr drop a vocal file here, or press LOAD.";
            break;
    }
    const auto box = area.withSizeKeepingCentre (std::min (560.0f, area.getWidth() - 40.0f), 170.0f);
    dd::gui::drawGlowing (g, title, box.withHeight (36.0f), kHot, LnF::font (24.0f, 700).withExtraKerningFactor (0.14f));
    g.setColour (kCream.withAlpha (0.72f));
    g.setFont (LnF::font (14.0f, 500));
    g.drawFittedText (body, box.withTrimmedTop (44.0f).toNearestInt(), juce::Justification::centredTop, 7, 0.9f);
}

void NoteCanvas::paintStatic (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    dd::gui::paintConsole (g, bounds, 8.0f, 0.55f);
    const auto area = noteArea();
    const double sr = sampleRate();

    juce::Graphics::ScopedSaveState clipAll (g);
    juce::Path clipPath;
    clipPath.addRoundedRectangle (bounds, 8.0f);
    g.reduceClipRegion (clipPath);

    // Semitone lanes: notes of the scale a shade lighter; C lines and labels.
    const auto mask = scaleMask (settings.scale);
    const int mLo = (int) std::floor (yToMidi (area.getBottom())) - 1, mHi = (int) std::ceil (topMidi) + 1;
    for (int m = mLo; m <= mHi; ++m)
    {
        const float y0 = midiToY ((float) m + 0.5f);
        const juce::Rectangle<float> lane (area.getX(), y0, area.getWidth(), pxPerSemi);
        const bool black = ((m % 12) + 12) % 12 == 1 || ((m % 12) + 12) % 12 == 3 || ((m % 12) + 12) % 12 == 6
                        || ((m % 12) + 12) % 12 == 8 || ((m % 12) + 12) % 12 == 10;
        if (settings.scale != 0 ? inScale (m, settings.key, mask) : ! black)
        {
            g.setColour (juce::Colour (0xffffe8c8).withAlpha (settings.scale != 0 ? 0.045f : 0.025f));
            g.fillRect (lane);
        }
        if (((m % 12) + 12) % 12 == 0)
        {
            g.setColour (juce::Colour (0xffffe8c8).withAlpha (0.10f));
            g.fillRect (area.getX(), y0 + pxPerSemi - 0.5f, area.getWidth(), 1.0f);
        }
    }

    // Time grid: bars and beats when the host has told us its tempo, else seconds.
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (area.withTop (0.0f).toNearestInt());
        const double bpm = proc.engine->getBpm();
        const bool beats = bpm > 1.0 && proc.engine->getSampleRate() > 0.0;
        const double t0 = state.viewStart, t1 = state.viewStart + state.viewSeconds;
        if (beats)
        {
            const double spb = 60.0 / bpm, origin = -proc.engine->getBeatAtZero() * spb;
            const double pxPerBeat = area.getWidth() / state.viewSeconds * spb;
            const int step = pxPerBeat > 22.0 ? 1 : pxPerBeat * 4.0 > 22.0 ? 4 : 16;
            const auto b0 = (int64_t) std::floor ((t0 - origin) / spb), b1 = (int64_t) std::ceil ((t1 - origin) / spb);
            for (int64_t b = b0 - (b0 % step); b <= b1; b += step)
            {
                const float x = timeToX ((origin + (double) b * spb) * sr);
                const bool bar = b % 4 == 0;
                g.setColour (juce::Colour (0xffffe8c8).withAlpha (bar ? 0.11f : 0.045f));
                g.fillRect (x, area.getY(), 1.0f, area.getHeight());
                if (bar)
                    drawText (g, juce::String (b / 4 + 1), { x + 3.0f, 2.0f, 40.0f, (float) kRulerH - 4.0f },
                              kCream.withAlpha (0.55f), 10.5f, 600);
            }
        }
        else
        {
            const double pxPerSec = area.getWidth() / state.viewSeconds;
            const double step = pxPerSec > 160 ? 0.25 : pxPerSec > 60 ? 1.0 : pxPerSec > 12 ? 5.0 : 30.0;
            for (double t = std::floor (t0 / step) * step; t <= t1; t += step)
            {
                const float x = timeToX (t * sr);
                g.setColour (juce::Colour (0xffffe8c8).withAlpha (0.06f));
                g.fillRect (x, area.getY(), 1.0f, area.getHeight());
                const int mins = (int) (t / 60.0);
                const double secs = t - mins * 60.0;
                drawText (g, juce::String (mins) + ":" + juce::String (secs, step < 1.0 ? 2 : 0).paddedLeft ('0', step < 1.0 ? 5 : 2),
                          { x + 3.0f, 2.0f, 60.0f, (float) kRulerH - 4.0f }, kCream.withAlpha (0.5f), 10.5f, 600);
            }
        }

        // Outside what has been captured: dimmer.
        double prevEnd = t0 * sr;
        for (const auto& s : snap.spans)
        {
            if ((double) s.start > prevEnd)
            {
                g.setColour (juce::Colours::black.withAlpha (0.22f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (timeToX (prevEnd), area.getY(), timeToX ((double) s.start), area.getBottom()));
            }
            prevEnd = std::max (prevEnd, (double) s.end);
        }
        if (prevEnd < t1 * sr)
        {
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (timeToX (prevEnd), area.getY(), area.getRight(), area.getBottom()));
        }
    }

    // Notes and the pitch line.
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (area.toNearestInt());
        const double v0 = state.viewStart * sr, v1 = (state.viewStart + state.viewSeconds) * sr;
        for (const auto& n : snap.notes)
        {
            if ((double) n.newEnd < v0 || (double) n.newStart > v1)
                continue;
            const bool sel = state.selection.count (n.id) > 0;
            if (sel && (n.moved || n.timeEdited()))
            {
                // Where it was sung.
                const juce::Rectangle<float> ghost (timeToX ((double) n.start), midiToY (n.centre) - pxPerSemi * 0.32f,
                                                    timeToX ((double) n.end) - timeToX ((double) n.start), pxPerSemi * 0.64f);
                juce::Path gp;
                gp.addRoundedRectangle (ghost, pxPerSemi * 0.3f);
                juce::Path dashed;
                const float dashes[] { 4.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, gp, dashes, 2);
                g.setColour (kCream.withAlpha (0.35f));
                g.fillPath (dashed);
            }
            const auto path = blobPath (n);
            const auto b = path.getBounds();
            if (n.muted)
            {
                g.setColour (kLamp.withAlpha (0.35f));
                g.strokePath (path, juce::PathStrokeType (1.2f));
                continue;
            }
            if (sel)
            {
                g.setColour (kLamp.withAlpha (0.16f));
                g.strokePath (path, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (kLamp.withAlpha (0.28f));
                g.strokePath (path, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            juce::ColourGradient fill (sel ? kHot : juce::Colour (0xffe8833f), b.getX(), b.getY(),
                                       sel ? juce::Colour (0xffe0793f) : juce::Colour (0xff9c4a20), b.getX(), b.getBottom(), false);
            g.setGradientFill (fill);
            g.fillPath (path);
            g.setColour (juce::Colour (0xffffe0b8).withAlpha (sel ? 0.9f : 0.35f));
            g.strokePath (path, juce::PathStrokeType (sel ? 1.2f : 0.8f));
            if (n.edited() && ! sel)
            {
                g.setColour (kHot.withAlpha (0.9f));
                g.fillEllipse (b.getX() + 2.0f, b.getY() - 5.0f, 3.5f, 3.5f);
            }
        }

        if (snap.curve != nullptr)
        {
            const auto& cp = snap.curve->points;
            auto it = std::lower_bound (cp.begin(), cp.end(), (int64_t) v0, [] (const CurvePoint& c, int64_t t) { return c.t < t; });
            juce::Path line;
            bool open = false;
            int64_t lastT = 0;
            const auto hopGap = (int64_t) (sr * 0.012);
            for (; it != cp.end() && (double) it->t <= v1; ++it)
            {
                if (it->midi <= 0.0f)
                {
                    open = false;
                    continue;
                }
                if (open && it->t - lastT > hopGap)
                    open = false;
                const juce::Point<float> pt (timeToX ((double) it->t), midiToY (it->midi));
                if (open)
                    line.lineTo (pt);
                else
                    line.startNewSubPath (pt);
                open = true;
                lastT = it->t;
            }
            g.setColour (juce::Colour (0xff1a0d06).withAlpha (0.55f));
            g.strokePath (line, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (kCream.withAlpha (0.9f));
            g.strokePath (line, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    if (snap.notes.empty())
        paintEmpty (g, area);

    // Ruler.
    {
        const juce::Rectangle<float> ruler (area.getX(), 0.0f, area.getWidth(), (float) kRulerH);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (ruler);
        g.setColour (juce::Colour (0xffffe8c8).withAlpha (0.08f));
        g.fillRect (ruler.getX(), ruler.getBottom() - 1.0f, ruler.getWidth(), 1.0f);
    }

    // Keyboard: ivory keys with the black keys on top, laid along the semitone rows.
    {
        const juce::Rectangle<float> keys (0.0f, area.getY(), (float) kKeysW, area.getHeight());
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (keys.toNearestInt());
        const auto ivory = keys.withTrimmedLeft (3.0f).withTrimmedRight (2.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe6d9c6), ivory.getX(), 0.0f, juce::Colour (0xffb9a993), ivory.getRight(), 0.0f, false));
        g.fillRect (ivory);
        auto isBlack = [] (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; };
        for (int m = mLo; m <= mHi; ++m)
        {
            const int pc = ((m % 12) + 12) % 12;
            const float yTop = midiToY ((float) m + 0.5f);
            // Seams between white keys: at the row edge for E|F and B|C, mid black key elsewhere.
            if (! isBlack (pc))
            {
                const bool edgeSeam = pc == 4 || pc == 11;
                const float seam = edgeSeam ? yTop : (isBlack (pc + 1) ? yTop - pxPerSemi * 0.5f : yTop);
                g.setColour (juce::Colour (0xff6b5a47).withAlpha (0.55f));
                g.fillRect (ivory.getX(), seam - 0.5f, ivory.getWidth(), 1.0f);
            }
            if (settings.scale != 0 && inScale (m, settings.key, mask))
            {
                g.setColour (kLamp.withAlpha (0.65f));
                g.fillRect (ivory.getRight() - 3.0f, yTop + 1.0f, 3.0f, pxPerSemi - 2.0f);
            }
            if (pc == 0 && pxPerSemi >= 9.0f)
                drawText (g, noteName (m), { ivory.getX() + ivory.getWidth() * 0.5f, yTop, ivory.getWidth() * 0.5f - 4.0f, pxPerSemi },
                          juce::Colour (0xff4a3826), std::min (10.0f, pxPerSemi * 0.75f), 700, juce::Justification::centredRight);
        }
        for (int m = mLo; m <= mHi; ++m)
        {
            if (! isBlack (((m % 12) + 12) % 12))
                continue;
            const auto k = juce::Rectangle<float> (ivory.getX(), midiToY ((float) m + 0.5f) + 0.5f, ivory.getWidth() * 0.6f, pxPerSemi - 1.0f);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (k.translated (1.0f, 1.0f), 1.5f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a2a1f), k.getX(), 0.0f, juce::Colour (0xff140c07), k.getRight(), 0.0f, false));
            g.fillRoundedRectangle (k, 1.5f);
            g.setColour (juce::Colour (0xffffe8c8).withAlpha (0.12f));
            g.fillRect (k.getX() + 1.0f, k.getY() + 1.0f, k.getWidth() - 3.0f, 1.0f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect ((float) kKeysW - 1.0f, area.getY(), 1.0f, area.getHeight());
    }
}

//==============================================================================
void NoteCanvas::applyNotes (std::vector<Note> next)
{
    std::sort (next.begin(), next.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    snap.notes = next;
    proc.engine->setNotes (std::move (next));
    seenVersion = proc.engine->getVersion();
    invalidate();
}

std::vector<Note> NoteCanvas::selectedNotes() const
{
    std::vector<Note> out;
    for (const auto& n : snap.notes)
        if (state.selection.count (n.id) > 0)
            out.push_back (n);
    return out;
}

void NoteCanvas::editSelection (const std::function<void (Note&)>& fn, bool allIfNone)
{
    const auto before = snap.notes;
    auto next = before;
    const bool all = state.selection.empty() && allIfNone;
    bool any = false;
    for (auto& n : next)
        if (all || state.selection.count (n.id) > 0)
        {
            fn (n);
            any = true;
        }
    if (! any)
        return;
    applyNotes (next);
    proc.engine->commit (before);
}

void NoteCanvas::togglePlay()
{
    auto& eng = *proc.engine;
    if (eng.isInternalPlaying())
        eng.stopPlaying();
    else if (! eng.isPlaying())
        eng.playFrom (state.cursor);
}

void NoteCanvas::mouseMove (const juce::MouseEvent& e)
{
    bool l = false, r = false;
    const int i = e.y > kRulerH && e.x > kKeysW ? noteAt (e.position, &l, &r) : -1;
    using MC = juce::MouseCursor;
    if (i < 0)
        setMouseCursor (e.y <= kRulerH ? MC::IBeamCursor : MC::NormalCursor);
    else
        switch (state.tool)
        {
            case Tool::Note:    setMouseCursor (l || r ? MC::LeftRightResizeCursor : MC::DraggingHandCursor); break;
            case Tool::Split:   setMouseCursor (MC::IBeamCursor); break;
            case Tool::Vibrato:
            case Tool::Drift:
            case Tool::Formant:
            case Tool::Level:   setMouseCursor (MC::UpDownResizeCursor); break;
        }
    if (i >= 0)
    {
        const auto& n = snap.notes[(size_t) i];
        setHint (noteWithCents (displayPitch (n)) + "   (sung " + noteWithCents (n.centre) + ")   " + toolHint (state.tool));
    }
    else
        setHint (toolHint (state.tool));
}

void NoteCanvas::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    drag = {};
    drag.start = e.position;
    drag.before = snap.notes;

    if (e.y <= kRulerH && e.x > kKeysW)
    {
        state.cursor = std::max<int64_t> (0, (int64_t) xToTime (e.position.x));
        drag.kind = Drag::Ruler;
        if (proc.engine->isInternalPlaying())
            proc.engine->playFrom (state.cursor);
        repaint();
        return;
    }
    if (e.x <= kKeysW)
        return;

    if (e.mods.isPopupMenu())
    {
        const int hit = noteAt (e.position);
        if (hit >= 0 && state.selection.count (snap.notes[(size_t) hit].id) == 0)
            state.selection = { snap.notes[(size_t) hit].id };
        state.changed();
        juce::PopupMenu m;
        const bool hasSel = ! state.selection.empty();
        m.addItem (1, "Snap to scale", hasSel);
        m.addItem (2, "Flatten vibrato", hasSel);
        m.addItem (3, "Steady drift", hasSel);
        m.addItem (4, "Mute / unmute", hasSel);
        m.addItem (5, "Reset edits", hasSel);
        m.addSeparator();
        m.addItem (6, "Select all");
        m.addItem (7, "Reset every note");
        juce::Component::SafePointer<NoteCanvas> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe] (int r)
        {
            if (safe == nullptr || r == 0)
                return;
            auto& c = *safe;
            const auto s = c.settings;
            switch (r)
            {
                case 1: c.editSelection ([s] (Note& n) { n.pitch = snapPitch (targetCentre (n, s) - (float) s.transpose, s) - n.centre; n.moved = true; }, false); break;
                case 2: c.editSelection ([] (Note& n) { n.mod = 0.0f; }, false); break;
                case 3: c.editSelection ([] (Note& n) { n.drift = 0.0f; }, false); break;
                case 4: c.editSelection ([] (Note& n) { n.muted = ! n.muted; }, false); break;
                case 5: c.editSelection ([] (Note& n) { n.resetEdits(); }, false); break;
                case 6: for (const auto& n : c.snap.notes) c.state.selection.insert (n.id); c.state.changed(); c.invalidate(); break;
                case 7:
                {
                    const auto all = c.state.selection;
                    c.state.selection.clear();
                    c.editSelection ([] (Note& n) { n.resetEdits(); }, true);
                    c.state.selection = all;
                    break;
                }
                default: break;
            }
        });
        return;
    }

    bool nl = false, nr = false;
    const int idx = noteAt (e.position, &nl, &nr);
    if (idx >= 0)
    {
        const auto& n = snap.notes[(size_t) idx];
        if (e.mods.isShiftDown() || e.mods.isCommandDown())
        {
            if (state.selection.count (n.id) > 0) state.selection.erase (n.id);
            else state.selection.insert (n.id);
        }
        else if (state.selection.count (n.id) == 0)
            state.selection = { n.id };
        drag.anchor = n.id;
        state.cursor = n.newStart;

        switch (state.tool)
        {
            case Tool::Note:
                drag.kind = nl ? Drag::StretchLeft : nr ? Drag::StretchRight : Drag::Move;
                if (! proc.engine->isPlaying() || proc.engine->isInternalPlaying())
                    proc.engine->playFrom (n.newStart, n.newEnd);   // hear the note you grabbed
                break;
            case Tool::Vibrato:
            case Tool::Drift:
            case Tool::Formant:
            case Tool::Level:
                drag.kind = Drag::Param;
                break;
            case Tool::Split:
            {
                const double tOut = xToTime (e.position.x);
                const double margin = std::max (0.02 * sampleRate(), 6.0 * state.viewSeconds * sampleRate() / noteArea().getWidth());
                if (tOut - (double) n.newStart > margin && (double) n.newEnd - tOut > margin && snap.analysis != nullptr)
                {
                    const double span = (double) (n.newEnd - n.newStart);
                    const auto tIn = (int64_t) std::llround ((double) n.start + (tOut - (double) n.newStart) * (double) (n.end - n.start) / span);
                    Note a = n, b = n;
                    uint32_t maxId = 0;
                    for (const auto& x : snap.notes) maxId = std::max (maxId, x.id);
                    a.end = tIn;
                    a.newEnd = (int64_t) std::llround (tOut);
                    b.id = maxId + 1;
                    b.start = tIn;
                    b.newStart = a.newEnd;
                    if (const auto* ra = snap.analysis->find (n.start))
                    {
                        a.centre = measureCentre (ra->track, a.start, a.end);
                        b.centre = measureCentre (ra->track, b.start, b.end);
                        // Keep where each half sounds if the note had been moved by hand.
                        if (n.moved)
                        {
                            const float target = n.centre + n.pitch;
                            a.pitch = target - a.centre;
                            b.pitch = target - b.centre;
                        }
                    }
                    auto next = snap.notes;
                    next.erase (next.begin() + idx);
                    next.push_back (a);
                    next.push_back (b);
                    applyNotes (next);
                    proc.engine->commit (drag.before);
                    state.selection = { b.id };
                }
                break;
            }
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

void NoteCanvas::mouseDrag (const juce::MouseEvent& e)
{
    const auto d = e.position - drag.start;
    const double sr = sampleRate();
    const float pxPerSec = noteArea().getWidth() / (float) state.viewSeconds;
    auto beforeOf = [this] (uint32_t id) -> const Note*
    {
        for (const auto& n : drag.before)
            if (n.id == id) return &n;
        return nullptr;
    };

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
            for (const auto& n : snap.notes)
                if (blobPath (n).getBounds().expanded (0.0f, 3.0f).intersects (drag.rubber))
                    state.selection.insert (n.id);
            state.changed();
            invalidate();
            return;
        }
        case Drag::Move:
        {
            if (! drag.axisDecided)
            {
                if (d.getDistanceFromOrigin() < 4.0f)
                    return;
                drag.axisDecided = true;
                drag.vertical = std::abs (d.y) >= std::abs (d.x);
                proc.engine->stopPlaying();
            }
            const auto* anchor = beforeOf (drag.anchor);
            if (anchor == nullptr)
                return;
            auto next = drag.before;
            if (drag.vertical)
            {
                const float tr = (float) settings.transpose;
                const float baseA = targetCentre (*anchor, settings) - tr;
                const float raw = baseA - d.y / pxPerSemi;
                const float delta = (e.mods.isAltDown() ? raw : snapPitch (raw, settings)) - baseA;
                for (auto& n : next)
                    if (state.selection.count (n.id) > 0)
                    {
                        const float base = targetCentre (n, settings) - tr;
                        n.pitch = base + delta - n.centre;
                        n.moved = true;
                    }
                setHint (noteWithCents (baseA + tr) + "  ->  " + noteWithCents (baseA + delta + tr)
                         + "   (" + (delta >= 0.0f ? "+" : "") + juce::String (delta, 2) + " st)");
            }
            else
            {
                double dt = (double) d.x / pxPerSec * sr;
                const double grid = gridSamples();
                if (! e.mods.isAltDown() && grid > 0.0)
                {
                    const double origin = gridOrigin();
                    const double target = std::round (((double) anchor->newStart + dt - origin) / grid) * grid + origin;
                    dt = target - (double) anchor->newStart;
                }
                // Keep clear of the unselected neighbours and inside the captured span.
                double lo = -1.0e18, hi = 1.0e18;
                const auto gap = (int64_t) (0.002 * sr);
                for (size_t i = 0; i < drag.before.size(); ++i)
                {
                    const auto& n = drag.before[i];
                    if (state.selection.count (n.id) == 0)
                        continue;
                    for (size_t j = i; j-- > 0;)
                        if (state.selection.count (drag.before[j].id) == 0) { lo = std::max (lo, (double) (drag.before[j].newEnd + gap - n.newStart)); break; }
                    for (size_t j = i + 1; j < drag.before.size(); ++j)
                        if (state.selection.count (drag.before[j].id) == 0) { hi = std::min (hi, (double) (drag.before[j].newStart - gap - n.newEnd)); break; }
                    for (const auto& s : snap.spans)
                        if (s.contains (n.start))
                        {
                            lo = std::max (lo, (double) (s.start + gap - n.newStart));
                            hi = std::min (hi, (double) (s.end - gap - n.newEnd));
                        }
                }
                dt = juce::jlimit (std::min (lo, 0.0), std::max (hi, 0.0), dt);
                const auto shift = (int64_t) std::llround (dt);
                for (auto& n : next)
                    if (state.selection.count (n.id) > 0)
                    {
                        n.newStart += shift;
                        n.newEnd += shift;
                    }
                setHint ("Moved " + juce::String ((double) shift / sr * 1000.0, 0) + " ms" + (grid > 0.0 ? "  (snaps to 1/16, Alt = free)" : ""));
            }
            applyNotes (next);
            drag.changed = true;
            return;
        }
        case Drag::StretchLeft:
        case Drag::StretchRight:
        {
            const auto* anchor = beforeOf (drag.anchor);
            if (anchor == nullptr)
                return;
            auto next = drag.before;
            const auto minLen = (int64_t) (0.03 * sr);
            for (size_t i = 0; i < next.size(); ++i)
            {
                auto& n = next[i];
                if (n.id != drag.anchor)
                    continue;
                const auto dt = (int64_t) std::llround ((double) d.x / pxPerSec * sr);
                if (drag.kind == Drag::StretchLeft)
                {
                    const int64_t lo = i > 0 ? next[i - 1].newEnd + 1 : n.newStart - (int64_t) sr;
                    n.newStart = juce::jlimit (lo, n.newEnd - minLen, anchor->newStart + dt);
                }
                else
                {
                    const int64_t hi = i + 1 < next.size() ? next[i + 1].newStart - 1 : n.newEnd + (int64_t) sr;
                    n.newEnd = juce::jlimit (n.newStart + minLen, hi, anchor->newEnd + dt);
                }
                const double ratio = (double) (n.newEnd - n.newStart) / (double) std::max<int64_t> (1, n.end - n.start);
                setHint ("Length " + juce::String (ratio * 100.0, 0) + "% of the original");
            }
            applyNotes (next);
            drag.changed = true;
            return;
        }
        case Drag::Param:
        {
            const float up = -d.y;
            auto next = drag.before;
            juce::String shown;
            for (auto& n : next)
            {
                if (state.selection.count (n.id) == 0)
                    continue;
                const auto* b = beforeOf (n.id);
                if (b == nullptr)
                    continue;
                switch (state.tool)
                {
                    case Tool::Vibrato: n.mod = juce::jlimit (0.0f, 2.0f, b->mod + up / 100.0f);   shown = "Vibrato " + juce::String (juce::roundToInt (n.mod * 100.0f)) + "%"; break;
                    case Tool::Drift:   n.drift = juce::jlimit (0.0f, 2.0f, b->drift + up / 100.0f); shown = "Drift " + juce::String (juce::roundToInt (n.drift * 100.0f)) + "%"; break;
                    case Tool::Formant: n.formant = std::round (juce::jlimit (-6.0f, 6.0f, b->formant + up / 25.0f) * 10.0f) / 10.0f;
                                        shown = "Formant " + juce::String (n.formant, 1) + " st"; break;
                    case Tool::Level:   n.gainDb = std::round (juce::jlimit (-36.0f, 12.0f, b->gainDb + up / 5.0f) * 2.0f) / 2.0f;
                                        shown = "Level " + juce::String (n.gainDb, 1) + " dB"; break;
                    case Tool::Note:
                    case Tool::Split:   break;
                }
            }
            setHint (shown);
            applyNotes (next);
            drag.changed = true;
            return;
        }
    }
}

void NoteCanvas::mouseUp (const juce::MouseEvent&)
{
    if (drag.changed)
        proc.engine->commit (drag.before);
    drag.kind = Drag::None;
    drag.rubber = {};
    invalidate();
}

void NoteCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y <= kRulerH && e.x > kKeysW)
    {
        state.cursor = std::max<int64_t> (0, (int64_t) xToTime (e.position.x));
        if (! proc.engine->isPlaying() || proc.engine->isInternalPlaying())
            proc.engine->playFrom (state.cursor);
        return;
    }
    const auto s = settings;
    if (state.tool == Tool::Split)
    {
        // Join two touching notes at the cut nearest the click.
        for (size_t i = 0; i + 1 < snap.notes.size(); ++i)
        {
            const auto& a = snap.notes[i];
            const auto& b = snap.notes[i + 1];
            if (b.start - a.end > (int64_t) (0.012 * sampleRate()) || std::abs (e.position.x - timeToX ((double) a.newEnd)) > 8.0f)
                continue;
            Note m = a;
            m.end = b.end;
            m.newEnd = b.newEnd;
            if (snap.analysis != nullptr)
                if (const auto* ra = snap.analysis->find (a.start))
                    m.centre = measureCentre (ra->track, m.start, m.end);
            if (a.moved)
                m.pitch = a.centre + a.pitch - m.centre;
            const auto before = snap.notes;
            auto next = before;
            next.erase (next.begin() + (long) i, next.begin() + (long) i + 2);
            next.push_back (m);
            applyNotes (next);
            proc.engine->commit (before);
            state.selection = { m.id };
            state.changed();
            return;
        }
        return;
    }
    if (noteAt (e.position) < 0)
        return;
    switch (state.tool)
    {
        case Tool::Note:
            editSelection ([s] (Note& n) { n.pitch = snapPitch (targetCentre (n, s) - (float) s.transpose, s) - n.centre; n.moved = true; }, false);
            break;
        case Tool::Vibrato: editSelection ([] (Note& n) { n.mod = n.mod > 0.5f ? 0.0f : 1.0f; }, false); break;
        case Tool::Drift:   editSelection ([] (Note& n) { n.drift = n.drift > 0.5f ? 0.0f : 1.0f; }, false); break;
        case Tool::Formant: editSelection ([] (Note& n) { n.formant = 0.0f; }, false); break;
        case Tool::Level:   editSelection ([] (Note& n) { n.muted = ! n.muted; }, false); break;
        case Tool::Split:   break;
    }
}

void NoteCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto area = noteArea();
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const double factor = std::exp (-(double) w.deltaY * 1.2);
        const double at = state.viewStart + (double) (e.position.x - area.getX()) / area.getWidth() * state.viewSeconds;
        state.viewSeconds *= factor;
        clampView();
        state.viewStart = at - (double) (e.position.x - area.getX()) / area.getWidth() * state.viewSeconds;
    }
    else if (e.mods.isAltDown())
    {
        const float at = yToMidi (e.position.y);
        pxPerSemi = juce::jlimit (6.0f, 48.0f, pxPerSemi * std::exp (w.deltaY * 1.2f));
        topMidi = at + (e.position.y - area.getY()) / pxPerSemi;
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float dx = std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY;
        state.viewStart -= (double) dx * state.viewSeconds * 0.6;
    }
    else
        topMidi += w.deltaY * 14.0f;
    clampView();
    state.changed();
    invalidate();
}

void NoteCanvas::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    const auto area = noteArea();
    const double at = state.viewStart + (double) (e.position.x - area.getX()) / area.getWidth() * state.viewSeconds;
    state.viewSeconds /= std::max (0.1, (double) scaleFactor);
    clampView();
    state.viewStart = at - (double) (e.position.x - area.getX()) / area.getWidth() * state.viewSeconds;
    clampView();
    state.changed();
    invalidate();
}

bool NoteCanvas::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    auto& eng = *proc.engine;
    const auto s = settings;

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
        for (const auto& n : snap.notes)
            state.selection.insert (n.id);
        state.changed();
        invalidate();
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        bool allMuted = true;
        for (const auto& n : selectedNotes())
            allMuted = allMuted && n.muted;
        editSelection ([allMuted] (Note& n) { n.muted = ! allMuted; }, false);
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const int dir = code == juce::KeyPress::upKey ? 1 : -1;
        const bool fine = mods.isAltDown();
        editSelection ([s, dir, fine] (Note& n)
        {
            const float base = targetCentre (n, s) - (float) s.transpose;
            const float target = fine ? base + 0.1f * (float) dir : stepToScale (base, dir, s);
            n.pitch = target - n.centre;
            n.moved = true;
        }, false);
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        if (snap.notes.empty())
            return true;
        const bool left = code == juce::KeyPress::leftKey;
        int first = -1, last = -1;
        for (int i = 0; i < (int) snap.notes.size(); ++i)
            if (state.selection.count (snap.notes[(size_t) i].id) > 0)
            {
                if (first < 0) first = i;
                last = i;
            }
        const int idx = first < 0 ? 0 : juce::jlimit (0, (int) snap.notes.size() - 1, left ? first - 1 : last + 1);
        state.selection = { snap.notes[(size_t) idx].id };
        state.cursor = snap.notes[(size_t) idx].newStart;
        const double t = (double) snap.notes[(size_t) idx].newStart / sampleRate();
        if (t < state.viewStart || t > state.viewStart + state.viewSeconds * 0.9)
            state.viewStart = std::max (0.0, t - state.viewSeconds * 0.3);
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
    if (code >= '1' && code <= '6' && ! mods.isAnyModifierKeyDown())
    {
        state.tool = (Tool) (code - '1');
        state.changed();
        return true;
    }
    return false;
}

bool NoteCanvas::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (dd::takefiles::canLoad (juce::File (f)))
            return true;
    return false;
}

void NoteCanvas::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (! dd::takefiles::canLoad (file))
            continue;
        auto& eng = *proc.engine;
        // Where the host's playhead is parked (bar 1 if it never told us).
        eng.loadFile (file, eng.isPlaying() ? 0 : eng.getPlayhead());
        fitPending = true;
        state.selection.clear();
        state.changed();
        break;
    }
    repaint();
}

//==============================================================================
Overview::Overview (ContourProcessor& p, EditorState& s) : proc (p), state (s)
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
    const double sr = snapshot.sampleRate;
    const auto s = proc.engine->currentSettings();
    float lo = 200.0f, hi = -200.0f;
    for (const auto& n : snapshot.notes)
    {
        lo = std::min (lo, targetCentre (n, s));
        hi = std::max (hi, targetCentre (n, s));
    }
    auto xOf = [&] (double seconds) { return r.getX() + (float) ((seconds - state.takeStart) / len) * r.getWidth(); };
    const auto inner = r.reduced (0.0f, 5.0f);
    for (const auto& n : snapshot.notes)
    {
        const float y = hi > lo ? inner.getBottom() - (targetCentre (n, s) - lo) / (hi - lo) * inner.getHeight() : inner.getCentreY();
        const float x0 = xOf ((double) n.newStart / sr), x1 = xOf ((double) n.newEnd / sr);
        g.setColour (kLamp.withAlpha (n.muted ? 0.25f : 0.85f));
        g.fillRect (x0, y - 1.0f, std::max (1.0f, x1 - x0), 2.0f);
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
NoteEditor::NoteEditor (ContourProcessor& p, EditorState& s)
    : proc (p), state (s), canvas (p, s), overview (p, s)
{
    for (int i = 0; i < 6; ++i)
    {
        auto k = std::make_unique<dd::gui::Key> (toolName ((Tool) i), i == 0);
        k->onClick = [this, i] { state.tool = (Tool) i; state.hint = toolHint ((Tool) i); state.changed(); };
        k->getProperties().set ("ddFlickerPeriod", 4.3 + i * 0.7);
        addAndMakeVisible (*k);
        toolKeys.push_back (std::move (k));
    }
    undoKey.onClick = [this] { proc.engine->undo(); };
    redoKey.onClick = [this] { proc.engine->redo(); };
    fitKey.onClick = [this] { canvas.zoomToFit(); };
    followKey.onClick = [this] { state.follow = ! state.follow; state.changed(); };
    for (auto* k : { &undoKey, &redoKey, &fitKey, &followKey })
        addAndMakeVisible (*k);
    addAndMakeVisible (overview);
    addAndMakeVisible (canvas);
    state.addChangeListener (this);
    state.hint = toolHint (Tool::Note);
    syncKeys();
    startTimerHz (8);
}

NoteEditor::~NoteEditor()
{
    state.removeChangeListener (this);
}

void NoteEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    syncKeys();
    repaint (getLocalBounds().removeFromBottom (24));
    overview.repaint();
}

void NoteEditor::timerCallback()
{
    undoKey.setAlpha (proc.engine->canUndo() ? 1.0f : 0.45f);
    redoKey.setAlpha (proc.engine->canRedo() ? 1.0f : 0.45f);
}

void NoteEditor::syncKeys()
{
    for (int i = 0; i < (int) toolKeys.size(); ++i)
        toolKeys[(size_t) i]->setLit (state.tool == (Tool) i);
    followKey.setLit (state.follow);
}

void NoteEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().removeFromBottom (22.0f);
    g.setColour (dd::gui::tokens().cream500);
    g.setFont (LnF::font (11.5f, 500));
    g.drawFittedText (state.hint, r.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
}

void NoteEditor::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (40);
    constexpr int glow = 6;
    auto place = [&bar] (juce::Component& c, int w) { c.setBounds (bar.removeFromLeft (w).expanded (glow, glow)); bar.removeFromLeft (6); };
    for (auto& k : toolKeys)
    {
        k->setGlowMargin (glow);
        place (*k, 76);
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
Inspector::Inspector (ContourProcessor& p, EditorState& s, NoteCanvas& c) : proc (p), state (s), canvas (c)
{
    resetKey.onClick = [this] { canvas.editSelection ([] (Note& n) { n.resetEdits(); }, false); };
    snapKey.onClick = [this]
    {
        const auto st = canvas.getSettings();
        canvas.editSelection ([st] (Note& n) { n.pitch = snapPitch (targetCentre (n, st) - (float) st.transpose, st) - n.centre; n.moved = true; }, false);
    };
    for (auto* k : { &resetKey, &snapKey })
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
    snapKey.setBounds (keys.removeFromRight (78).expanded (5));
    keys.removeFromRight (8);
    resetKey.setBounds (keys.removeFromRight (78).expanded (5));
}

void Inspector::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    dd::gui::paintConsole (g, r.withTrimmedBottom (46.0f), 7.0f, 0.5f);
    auto area = r.withTrimmedBottom (46.0f).reduced (14.0f, 10.0f);
    const auto sel = canvas.selectedNotes();
    const auto s = canvas.getSettings();
    dd::gui::drawCaption (g, sel.size() > 1 ? juce::String ((int) sel.size()) + " NOTES SELECTED" : "SELECTED NOTE", area.removeFromTop (14.0f), juce::Justification::centredLeft, 9.5f);
    if (sel.empty())
    {
        drawText (g, "Click a note to see it here. Shift-click or drag a box to select several; Ctrl/Cmd+A selects all.",
                  area, kCream.withAlpha (0.6f), 13.0f, 500, juce::Justification::topLeft);
        return;
    }
    const auto& n = sel.front();
    const float target = targetCentre (n, s);
    dd::gui::drawGlowing (g, sel.size() > 1 ? juce::String ("--") : noteWithCents (target), area.removeFromTop (40.0f), kHot,
                          LnF::font (30.0f, 700), juce::Justification::centredLeft);
    auto row = [&] (const juce::String& label, const juce::String& value)
    {
        auto line = area.removeFromTop (17.0f);
        drawText (g, label, line.removeFromLeft (92.0f), kCream.withAlpha (0.55f), 12.0f, 600);
        drawText (g, value, line, kCream.withAlpha (0.92f), 12.5f, 600);
    };
    if (sel.size() == 1)
    {
        row ("Sung", noteWithCents (n.centre));
        row ("Moved", (target - n.centre >= 0.0f ? "+" : "") + juce::String (target - n.centre, 2) + " st" + (n.moved ? "" : "  (auto)"));
        row ("Vibrato", juce::String (juce::roundToInt (n.mod * s.vibrato * 100.0f)) + "%");
        row ("Drift", juce::String (juce::roundToInt (n.drift * (1.0f - s.drift) * 100.0f)) + "%");
        row ("Formant", juce::String (n.formant + s.formant, 1) + " st");
        row ("Level", n.muted ? juce::String ("muted") : juce::String (n.gainDb, 1) + " dB");
        row ("Length", juce::String ((double) (n.newEnd - n.newStart) / proc.engine->getSampleRate(), 2) + " s");
    }
    else
    {
        float minC = 200.0f, maxC = -200.0f;
        int edited = 0;
        for (const auto& x : sel)
        {
            minC = std::min (minC, targetCentre (x, s));
            maxC = std::max (maxC, targetCentre (x, s));
            edited += x.edited() ? 1 : 0;
        }
        row ("Range", noteName ((int) std::lround (minC)) + " to " + noteName ((int) std::lround (maxC)));
        row ("Edited", juce::String (edited) + " of " + juce::String ((int) sel.size()));
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

SessionView::SessionView (ContourProcessor& p, EditorState& s, NoteCanvas& c) : proc (p), state (s), canvas (c)
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
                                            "Contour forgets this take and every edit on it, and listens for the next playback.",
                                            "Clear", "Cancel", this,
                                            juce::ModalCallbackFunction::create ([safe] (int r)
                                            {
                                                if (safe != nullptr && r == 1)
                                                    safe->proc.engine->clearTake();
                                            }));
    };
    keyKey.onClick = [this]
    {
        const auto [key, scale] = detectKey (proc.engine->snapshot().notes);
        if (auto* kp = proc.apvts.getParameter ("perfKey"))   kp->setValueNotifyingHost (kp->convertTo0to1 ((float) key));
        if (auto* sp = proc.apvts.getParameter ("perfScale")) sp->setValueNotifyingHost (sp->convertTo0to1 ((float) scale));
    };
    for (auto* k : { &playKey, &loadKey, &bounceKey, (dd::gui::Key*) &dragKey, &clearKey, &keyKey })
    {
        k->setGlowMargin (5);
        addAndMakeVisible (*k);
    }
    dragKey.setTooltip ("After BOUNCE: drag this into your playlist. The file starts at the song's beginning, so drop it at bar 1.");
    bounceKey.setTooltip ("Renders the edited take to a WAV file in Music/ddeonmadeit/Contour/Bounces");
    loadKey.setTooltip ("Load a vocal file (or drop one on the note editor)");
    startTimerHz (8);
}

void SessionView::timerCallback()
{
    auto& eng = *proc.engine;
    playKey.setLit (eng.isInternalPlaying());
    playKey.setLabel (eng.isInternalPlaying() ? "Stop" : "Play", false);
    const int b = eng.getBounceCount();
    dragKey.setLit (b > 0 && eng.getLastBounce().existsAsFile());
    if (b != seenBounces)
        seenBounces = b;
    repaint (getLocalBounds().withHeight (getHeight() - 96));
}

void SessionView::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto row2 = r.removeFromBottom (36);
    r.removeFromBottom (8);
    auto row1 = r.removeFromBottom (36);
    const int w = (row1.getWidth() - 16) / 3;
    for (auto* k : { &playKey, &loadKey, &keyKey })
    {
        k->setBounds (row1.removeFromLeft (w).expanded (5));
        row1.removeFromLeft (8);
    }
    for (auto* k : { &bounceKey, (dd::gui::Key*) &dragKey, &clearKey })
    {
        k->setBounds (row2.removeFromLeft (w).expanded (5));
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
        case Status::Listening: word = "LISTENING"; line = eng.hasTake() ? "Next playback replaces what it plays over." : "Press play in your DAW to capture the vocal."; led = dd::gui::ledPulse (dd::gui::animationTime()); break;
        case Status::Capturing: word = "CAPTURING"; line = "Recording the track as it plays. Stop when done."; led = 1.0f; break;
        case Status::Analysing: word = "FINDING NOTES"; line = "Analysing the take..."; led = dd::gui::ledPulse (dd::gui::animationTime(), 0.6); break;
        case Status::Ready:     word = "READY"; line = "Playback plays your edit, in time with the song."; led = 0.0f; break;
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
TransferKey::TransferKey (ContourProcessor& p) : Key ("Transfer", true), proc (p)
{
    onClick = [this] { proc.engine->setArmed (! proc.engine->isArmed()); };
    setTooltip ("Lit: the next playback is captured (replacing what it plays over). Unlit: playback plays your edit.");
    getProperties().set ("ddFlickerPeriod", 6.1);
    setLit (proc.engine->isArmed());
    poll.fn = [this] { setLit (proc.engine->isArmed()); };
    poll.startTimerHz (10);
}
} // namespace contour
