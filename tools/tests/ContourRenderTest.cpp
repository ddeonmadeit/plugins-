/*
    Contour's analysis and rendering on a synthetic sung phrase with known notes: the notes are found where
    they were sung, an unedited take comes back sample-exact, a moved note lands on its new pitch, time edits,
    mutes and formant shifts render without clicks, NaNs or DC, and the output doesn't depend on block size.
*/
#include <juce_audio_processors/juce_audio_processors.h>
#include <dd_take/dd_take.h>

#include "Engine.h"
#include "Model.h"
#include "Render.h"

#include <iostream>

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (! ok) ++failures;
}

constexpr double kSr = 48000.0;
constexpr int64_t kStart = 48000;   // the take begins one second into the song
const float kNotes[] { 60.0f, 62.0f, 64.3f, 65.0f, 57.0f };
constexpr double kNoteSec = 0.5, kGapSec = 0.08;

/** A voice: band-limited pulses at the sung pitch (with vibrato) through three vowel formants, a breathy
    consonant in each gap. */
std::vector<float> sing (const float* notes = kNotes)
{
    const int len = (int) (kSr * (kNoteSec + kGapSec) * std::size (kNotes) + kSr * 0.2);
    std::vector<float> x ((size_t) len, 0.0f);
    juce::Random rnd (7);
    double phase = 0.0;
    for (int i = 0; i < len; ++i)
    {
        const double t = i / kSr;
        const int k = (int) (t / (kNoteSec + kGapSec));
        const double inNote = t - k * (kNoteSec + kGapSec);
        if (k < (int) std::size (kNotes) && inNote < kNoteSec)
        {
            const double midi = notes[k] + 0.25 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t) * std::min (1.0, inNote / 0.15);
            const double f0 = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
            phase += f0 / kSr;
            double s = 0.0;
            for (int h = 1; h * f0 < 5000.0; ++h)
            {
                const double hz = h * f0;
                const double env = 1.0 / h * (1.0 + 3.0 * std::exp (-std::pow ((hz - 700.0) / 150.0, 2))
                                                  + 2.0 * std::exp (-std::pow ((hz - 1200.0) / 200.0, 2))
                                                  + 1.0 * std::exp (-std::pow ((hz - 2600.0) / 300.0, 2)));
                s += env * std::sin (juce::MathConstants<double>::twoPi * h * phase);
            }
            const double fade = std::min ({ 1.0, inNote / 0.02, (kNoteSec - inNote) / 0.02 });
            x[(size_t) i] = (float) (0.12 * s * fade);
        }
        else if (k < (int) std::size (kNotes))
            x[(size_t) i] = (rnd.nextFloat() * 2.0f - 1.0f) * 0.03f;   // "s"
    }
    return x;
}

/** A host for the engine: parameters, a timeline it can play, stop and seek. */
struct Host : juce::AudioProcessor, juce::AudioPlayHead
{
    Host() : apvts (*this, nullptr, "Contour", contour::createLayout()) {}
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setTimeInSamples (pos);
        p.setBpm (120.0);
        p.setPpqPosition ((double) pos / kSr * 2.0);
        return p;
    }
    /** Plays `src` (timeline sample = index) from `a` to `b`, returns what the engine output. */
    std::vector<float> play (contour::Engine& e, const std::vector<float>& src, int64_t a, int64_t b, bool offline)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, 256);
        playing = true;
        for (pos = a; pos < b; pos += 256)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    buf.setSample (ch, i, pos + i < (int64_t) src.size() ? src[(size_t) (pos + i)] : 0.0f);
            e.process (buf, this, offline);
            for (int i = 0; i < 256; ++i)
                out.push_back (buf.getSample (0, i));
        }
        playing = false;
        buf.clear();
        e.process (buf, this, offline);   // transport stops: capture ends
        return out;
    }
    const juce::String getName() const override { return "Host"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    using AudioProcessor::processBlock;
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
    bool playing = false;
    int64_t pos = 0;
};

bool waitFor (const std::function<bool()>& f)
{
    for (int i = 0; i < 3000; ++i)
    {
        if (f()) return true;
        juce::Thread::sleep (2);
    }
    return false;
}

std::vector<float> renderAll (const contour::Playback& pb, const dd::TakeStore& take, int64_t from, int64_t to, juce::Random* blocks = nullptr)
{
    contour::Renderer r;
    std::vector<float> out ((size_t) (to - from)), cover ((size_t) contour::Renderer::kMaxBlock);
    for (int64_t t = from; t < to;)
    {
        int n = blocks != nullptr ? 1 + blocks->nextInt (contour::Renderer::kMaxBlock) : contour::Renderer::kMaxBlock;
        n = (int) std::min<int64_t> (n, to - t);
        std::vector<float> w ((size_t) n), c ((size_t) n);
        r.render (pb, take, t, n, w.data(), c.data());
        // As the plugin does: the take where it covers the timeline, the live input (the same vocal) elsewhere.
        for (int i = 0; i < n; ++i)
        {
            const float live = take.get (t + i);
            out[(size_t) (t - from + i)] = live * (1.0f - c[(size_t) i]) + w[(size_t) i] * c[(size_t) i];
        }
        t += n;
    }
    return out;
}

/** Pitch (MIDI) of a stretch of signal by autocorrelation. */
float pitchOf (const std::vector<float>& y, int64_t a, int64_t b)
{
    const int minLag = (int) (kSr / 900.0), maxLag = (int) (kSr / 80.0);
    const int n = (int) (b - a) - maxLag;
    double best = -1.0;
    int lag = 0;
    std::vector<double> r ((size_t) maxLag + 2);
    for (int l = minLag; l <= maxLag + 1; ++l)
    {
        double s = 0.0, e1 = 1e-12, e2 = 1e-12;
        for (int i = 0; i < n; ++i)
        {
            const double u = y[(size_t) (a + i)], v = y[(size_t) (a + i + l)];
            s += u * v; e1 += u * u; e2 += v * v;
        }
        r[(size_t) l] = s / std::sqrt (e1 * e2);
    }
    for (int l = minLag + 1; l <= maxLag; ++l)
        best = std::max (best, r[(size_t) l]);
    for (int l = minLag + 1; l <= maxLag && lag == 0; ++l)
        if (r[(size_t) l] >= 0.9 * best && r[(size_t) l] >= r[(size_t) l - 1] && r[(size_t) l] >= r[(size_t) l + 1])
            lag = l;
    if (lag == 0)
        return 0.0f;
    const double ya = r[(size_t) lag - 1], yb = r[(size_t) lag], yc = r[(size_t) lag + 1];
    const double off = 0.5 * (ya - yc) / (ya - 2 * yb + yc);
    return (float) (69.0 + 12.0 * std::log2 (kSr / (lag + off) / 440.0));
}

double rms (const std::vector<float>& y, int64_t a, int64_t b)
{
    double s = 0.0;
    for (int64_t i = a; i < b; ++i) s += (double) y[(size_t) i] * y[(size_t) i];
    return std::sqrt (s / (double) std::max<int64_t> (1, b - a));
}

float maxStep (const std::vector<float>& y)
{
    float m = 0.0f;
    for (size_t i = 1; i < y.size(); ++i) m = std::max (m, std::abs (y[i] - y[i - 1]));
    return m;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto voice = sing();
    dd::TakeStore take;
    take.write (kStart, voice.data(), (int) voice.size());
    const dd::Span span { kStart, kStart + (int64_t) voice.size() };

    std::cout << "analysis\n";
    contour::Analysis analysis;
    analysis.sampleRate = kSr;
    analysis.ranges.push_back (contour::analyseRange (take, span, kSr));
    uint32_t nextId = 1;
    auto notes = contour::findNotes (analysis.ranges.front(), nextId);
    check (notes.size() == std::size (kNotes), "found " + juce::String ((int) notes.size()) + " notes (sung " + juce::String ((int) std::size (kNotes)) + ")");
    for (size_t i = 0; i < std::min (notes.size(), std::size (kNotes)); ++i)
    {
        const double sungAt = (double) kStart + kSr * (kNoteSec + kGapSec) * (double) i;
        check (std::abs (notes[i].centre - kNotes[i]) < 0.08f,
               "note " + juce::String ((int) i + 1) + " centre " + juce::String (notes[i].centre, 2) + " (sung " + juce::String (kNotes[i], 2) + ")");
        check (std::abs ((double) notes[i].start - sungAt) < kSr * 0.03, "note " + juce::String ((int) i + 1) + " starts where it was sung");
    }
    check (! analysis.ranges.front().marks.empty(), "pulse marks found");

    const std::vector<dd::Span> spans { span };
    contour::Settings s;
    const int64_t from = span.start - 4800, to = span.end + 4800;

    std::cout << "identity\n";
    {
        const auto r = contour::build (analysis, notes, s, spans);
        const auto y = renderAll (*r.playback, take, from, to);
        float err = 0.0f;
        int64_t worst = 0;
        for (int64_t t = span.start; t < span.end; ++t)
            if (std::abs (y[(size_t) (t - from)] - voice[(size_t) (t - kStart)]) > err)
            {
                err = std::abs (y[(size_t) (t - from)] - voice[(size_t) (t - kStart)]);
                worst = t;
            }
        if (err > 1.0e-5f)
        {
            std::cout << "    worst at take sample " << (worst - kStart) << " (" << (double) (worst - kStart) / kSr << " s)\n";
            for (const auto& g : r.playback->grains)
                if (std::abs (g.out - (double) worst) < 900.0)
                    std::cout << "    grain out " << g.out - kStart << " in " << g.in - kStart << " L " << g.left << " R " << g.right << "\n";
            const auto& ra = analysis.ranges.front();
            for (size_t k = 0; k < ra.marks.size(); ++k)
                if (std::abs ((double) ra.marks[k] - (double) worst) < 900.0)
                    std::cout << "    mark " << ra.marks[k] - kStart << " run " << ra.markRun[k] << "\n";
        }
        check (err < 1.0e-5f, "unedited take is sample-exact (max error " + juce::String (err, 8) + ")");
        juce::Random rnd (3);
        const auto z = renderAll (*r.playback, take, from, to, &rnd);
        float diff = 0.0f;
        for (size_t i = 0; i < y.size(); ++i) diff = std::max (diff, std::abs (y[i] - z[i]));
        check (diff < 1.0e-6f, "random block sizes render the same");
    }

    if (notes.size() < std::size (kNotes))
    {
        std::cout << failures << " failure(s)\n";
        return 1;
    }

    std::cout << "pitch edits\n";
    {
        auto ed = notes;
        ed[2].moved = true;
        ed[2].pitch = 64.0f - ed[2].centre;   // 64.3 down to 64
        ed[2].mod = 0.0f;
        ed[2].drift = 0.0f;
        ed[4].moved = true;
        ed[4].pitch = 3.0f;                   // up a minor third
        ed[4].mod = 0.0f;
        ed[4].drift = 0.0f;
        const auto r = contour::build (analysis, ed, s, spans);
        const auto y = renderAll (*r.playback, take, from, to);
        auto mid = [&] (const contour::Note& n) { const int64_t c = (n.newStart + n.newEnd) / 2; return std::pair { c - 1200 - from, c + 1200 - from }; };
        const auto [a2, b2] = mid (ed[2]);
        const auto [a4, b4] = mid (ed[4]);
        const float p2 = pitchOf (y, a2, b2), p4 = pitchOf (y, a4, b4);
        check (std::abs (p2 - 64.0f) < 0.07f, "moved note sounds at 64.00 (measured " + juce::String (p2, 2) + ")");
        check (std::abs (p4 - (ed[4].centre + 3.0f)) < 0.1f, "note raised 3 st sounds at " + juce::String (ed[4].centre + 3.0f, 2) + " (measured " + juce::String (p4, 2) + ")");
        const auto [a0, b0] = mid (ed[0]);
        check (std::abs (rms (y, a0, b0) - rms (voice, a0 + from - kStart, b0 + from - kStart)) < 1.0e-6, "untouched notes stay exact");
        check (std::abs (20.0 * std::log10 (rms (y, a4, b4) / rms (voice, a4 + from - kStart, b4 + from - kStart))) < 2.0,
               "a moved note keeps its level (within 2 dB)");
        check (maxStep (y) < maxStep (voice) * 1.6f, "no clicks from pitch edits");
    }

    std::cout << "global correction\n";
    {
        contour::Settings g;
        g.center = 1.0f;
        g.drift = 1.0f;
        g.vibrato = 0.0f;
        g.transitionMs = 5.0f;
        const auto r = contour::build (analysis, notes, g, spans);
        const auto y = renderAll (*r.playback, take, from, to);
        const int64_t c = (notes[2].start + notes[2].end) / 2 - from;
        const float p = pitchOf (y, c - 1200, c + 1200);
        check (std::abs (p - 64.0f) < 0.07f, "Pitch Center 100% pulls 64.30 to 64.00 (measured " + juce::String (p, 2) + ")");
    }

    std::cout << "time edits, mute, formant\n";
    {
        auto ed = notes;
        const auto shift = (int64_t) (0.04 * kSr);
        ed[1].newStart += shift;
        ed[1].newEnd += shift;
        ed[3].newEnd = ed[3].newStart + (int64_t) ((double) (ed[3].end - ed[3].start) * 1.3);
        ed[0].muted = true;
        ed[4].formant = 3.0f;
        const auto r = contour::build (analysis, ed, s, spans);
        const auto y = renderAll (*r.playback, take, from, to);
        bool finite = true;
        double dc = 0.0;
        for (auto v : y) { finite = finite && std::isfinite (v); dc += v; }
        check (finite, "no NaN / Inf");
        check (std::abs (dc / (double) y.size()) < 1.0e-3, "no DC offset");
        check (maxStep (y) < maxStep (voice) * 1.6f, "no clicks from time edits (max step " + juce::String (maxStep (y), 4) + " vs " + juce::String (maxStep (voice), 4) + ")");
        const int64_t m0 = (ed[0].start + ed[0].end) / 2 - from;
        check (rms (y, m0 - 2400, m0 + 2400) < rms (voice, m0 - 2400 + from - kStart, m0 + 2400 + from - kStart) * 0.01, "muted note is silent");
        const int64_t m1 = ed[1].newStart + 4800 - from;
        const float p1 = pitchOf (y, m1, m1 + 2400);
        check (std::abs (p1 - kNotes[1]) < 0.35f, "moved note plays at its new time (pitch " + juce::String (p1, 2) + ")");
        const int64_t m3 = ed[3].newEnd - 4800 - from;
        const float p3 = pitchOf (y, m3 - 1200, m3 + 1200);
        check (std::abs (p3 - kNotes[3]) < 0.35f, "stretched note keeps its pitch (" + juce::String (p3, 2) + ")");
        const contour::TimeMap tm (ed, spans);
        check (std::abs (tm.toOutput ((double) ed[1].start) - (double) ed[1].newStart) < 1.0, "time map hits the moved note");
        check (std::abs (tm.toInput (tm.toOutput ((double) ed[3].start + 1000.0)) - ((double) ed[3].start + 1000.0)) < 0.01, "time map inverts");
    }

    std::cout << "engine: capture, edit, save, restore\n";
    {
        std::vector<float> song ((size_t) kStart, 0.0f);
        song.insert (song.end(), voice.begin(), voice.end());
        juce::ValueTree saved ("Contour");
        juce::File takeFile;
        {
            Host host;
            contour::Engine engine (host.apvts);
            engine.prepare (kSr, 256);
            check (engine.isArmed(), "a new instance listens for the first playback");
            const auto live = host.play (engine, song, 0, (int64_t) song.size(), true);
            float err = 0.0f;
            for (size_t i = 0; i < live.size() && i < song.size(); ++i) err = std::max (err, std::abs (live[i] - song[i]));
            check (err < 1.0e-6f, "while capturing, the track plays through untouched");
            check (waitFor ([&] { return engine.getStatus() == contour::Status::Ready; }), "analysis finishes and transfer disarms");
            auto snap = engine.snapshot();
            check (snap.notes.size() == std::size (kNotes), "engine found " + juce::String ((int) snap.notes.size()) + " notes");
            if (snap.notes.size() < 3)
                return 1;
            auto edited = snap.notes;
            edited[2].moved = true;
            edited[2].pitch = 64.0f - edited[2].centre;
            edited[2].mod = 0.0f;
            edited[2].drift = 0.0f;
            edited[0].gainDb = -6.0f;
            engine.setNotes (edited);
            engine.commit (snap.notes);
            check (engine.canUndo(), "an edit can be undone");
            check (waitFor ([&] { return engine.snapshot().curve != nullptr; }), "edit table rebuilt");
            juce::Thread::sleep (50);
            const auto played = host.play (engine, song, 0, (int64_t) song.size(), true);
            const int64_t c = (edited[2].start + edited[2].end) / 2;
            const float p = pitchOf (played, c - 1200, c + 1200);
            check (std::abs (p - 64.0f) < 0.07f, "playback plays the edit in time with the song (" + juce::String (p, 2) + ")");
            check (waitFor ([&] { juce::ValueTree t ("S"); engine.writeState (t); return t.getChildWithName ("Contour").getProperty ("take").toString().isNotEmpty(); }),
                   "take saved to disk");
            juce::ValueTree parent ("S");
            engine.writeState (parent);
            saved = parent.getChildWithName ("Contour").createCopy();
            takeFile = juce::File (saved.getProperty ("take").toString());
            check (takeFile.existsAsFile(), "take file exists: " + takeFile.getFileName());
            check (engine.undo() && engine.snapshot().notes[2].moved == false, "undo restores the note");
            engine.redo();
        }
        {
            Host host;
            contour::Engine engine (host.apvts);
            juce::ValueTree parent ("S");
            parent.appendChild (saved, nullptr);
            engine.readState (parent);
            engine.prepare (kSr, 256);
            check (waitFor ([&] { return engine.getStatus() == contour::Status::Ready && engine.hasTake(); }), "a reopened project finds its take");
            const auto snap = engine.snapshot();
            check (snap.notes.size() == std::size (kNotes) && snap.notes[2].moved && std::abs (snap.notes[2].centre + snap.notes[2].pitch - 64.0f) < 1.0e-3f
                       && std::abs (snap.notes[0].gainDb + 6.0f) < 1.0e-3f,
                   "every note edit comes back");
            juce::Thread::sleep (50);
            const auto played = host.play (engine, song, 0, (int64_t) song.size(), true);
            const int64_t c = (snap.notes[2].start + snap.notes[2].end) / 2;
            check (std::abs (pitchOf (played, c - 1200, c + 1200) - 64.0f) < 0.07f, "and it sounds the same");
        }
        {
            // At another sample rate the take is laid out again at the new rate.
            Host host;
            contour::Engine engine (host.apvts);
            juce::ValueTree parent ("S");
            parent.appendChild (saved, nullptr);
            engine.readState (parent);
            engine.prepare (96000.0, 256);
            check (waitFor ([&] { return engine.getStatus() == contour::Status::Ready && engine.hasTake(); }), "reopened at 96 kHz");
            const auto snap = engine.snapshot();
            check (! snap.notes.empty() && std::abs ((double) snap.notes[2].start / 96000.0 - (double) (kStart / kSr) - (kNoteSec + kGapSec) * 2.0) < 0.03,
                   "notes keep their place in time at 96 kHz");
        }
        takeFile.deleteFile();
    }

    std::cout << "engine: follows the track by itself\n";
    {
        std::vector<float> song ((size_t) kStart, 0.0f);
        song.insert (song.end(), voice.begin(), voice.end());
        Host host;
        contour::Engine engine (host.apvts);
        engine.prepare (kSr, 256);
        host.play (engine, song, 0, (int64_t) song.size(), true);
        check (waitFor ([&] { return engine.getStatus() == contour::Status::Ready; }), "first playback captured");
        auto snap = engine.snapshot();
        if (snap.notes.size() != std::size (kNotes))
            return 1;
        auto edited = snap.notes;
        edited[1].moved = true;
        edited[1].pitch = 63.0f - edited[1].centre;   // 62 up to 63
        edited[1].mod = 0.0f;
        edited[1].drift = 0.0f;
        engine.setNotes (edited);
        engine.commit (snap.notes);
        check (waitFor ([&] { return engine.snapshot().curve != nullptr; }), "edit table rebuilt");
        juce::Thread::sleep (50);
        const auto noteMid = [&] (int i) { return (edited[(size_t) i].start + edited[(size_t) i].end) / 2; };

        auto played = host.play (engine, song, 0, (int64_t) song.size(), false);
        check (engine.getFollowCaptures() == 0, "the same vocal again: nothing re-captured");
        check (std::abs (pitchOf (played, noteMid (1) - 1200, noteMid (1) + 1200) - 63.0f) < 0.07f, "and the edit plays");

        // A host that re-renders the clip on every play (Ableton's warp): same sound, different samples.
        auto warped = song;
        float xm = 0.0f, ym = 0.0f;
        for (auto& v : warped)
        {
            const float y = -0.35f * v + xm + 0.35f * ym;   // first-order all-pass: every phase moves
            xm = v;
            ym = y;
            v = y;
        }
        played = host.play (engine, warped, 0, (int64_t) warped.size(), false);
        check (engine.getFollowCaptures() == 0, "a re-warped clip (all-pass) is still recognised: nothing re-captured");
        check (std::abs (pitchOf (played, noteMid (1) - 1200, noteMid (1) + 1200) - 63.0f) < 0.07f, "and the edit still plays");

        // The clip muted over notes 2-3: those play silent, nothing is captured, the edits are kept.
        auto muted = song;
        std::fill (muted.begin() + noteMid (0) + 12000, muted.begin() + noteMid (2), 0.0f);
        played = host.play (engine, muted, 0, (int64_t) muted.size(), false);
        check (engine.getFollowCaptures() == 0, "a muted stretch is not captured");
        check (rms (played, noteMid (1) - 2400, noteMid (1) + 2400) < 1.0e-4, "and plays silent (not the old take)");
        check (engine.snapshot().notes[1].moved, "and the edit survives");

        // A new take over notes 4 and 5 (sung a fifth higher): picked up while it plays, analysed, and then
        // the new notes play; the edited note 2 earlier in the song is untouched.
        const float retake[] { kNotes[0], kNotes[1], kNotes[2], kNotes[3] + 7.0f, kNotes[4] + 7.0f };
        const auto voice2 = sing (retake);
        auto song2 = song;
        const auto from3 = (size_t) (kStart + (int64_t) (kSr * (kNoteSec + kGapSec) * 3.0));
        std::copy (voice2.begin() + (long) (from3 - (size_t) kStart), voice2.end(), song2.begin() + (long) from3);
        played = host.play (engine, song2, 0, (int64_t) song2.size(), false);
        check (engine.getFollowCaptures() >= 1, "the new take is captured by itself (" + juce::String ((int) engine.getFollowCaptures()) + " stretch(es))");
        const int64_t m4 = noteMid (4);
        check (std::abs (pitchOf (played, m4 - 1200, m4 + 1200) - (kNotes[4] + 7.0f)) < 0.3f, "while it is captured the new take plays (live)");
        check (waitFor ([&] { return engine.getStatus() == contour::Status::Ready; }), "and analysed");
        snap = engine.snapshot();
        check (snap.notes.size() == std::size (kNotes) && std::abs (snap.notes[4].centre - (kNotes[4] + 7.0f)) < 0.1f && std::abs (snap.notes[3].centre - (kNotes[3] + 7.0f)) < 0.1f,
               "notes 4 and 5 are now the new take's");
        check (snap.notes.size() > 1 && snap.notes[1].moved, "the edit on note 2 is kept");
        juce::Thread::sleep (50);
        played = host.play (engine, song2, 0, (int64_t) song2.size(), false);
        check (std::abs (pitchOf (played, noteMid (1) - 1200, noteMid (1) + 1200) - 63.0f) < 0.07f, "next playback: note 2's edit plays");
        const float p4 = pitchOf (played, m4 - 1200, m4 + 1200);
        float err4 = 0.0f;
        for (int64_t i = m4 - 4800; i < m4 + 4800; ++i) err4 = std::max (err4, std::abs (played[(size_t) i] - song2[(size_t) i]));
        check (std::abs (p4 - (kNotes[4] + 7.0f)) < 0.3f && err4 < 1.0e-4f,
               "and the new take plays from the take (" + juce::String (p4, 2) + ", max error " + juce::String (err4, 6) + ")");
        const auto before = engine.getFollowCaptures();
        host.play (engine, song2, 0, (int64_t) song2.size(), false);
        check (engine.getFollowCaptures() == before, "and nothing more is captured");
        juce::ValueTree t ("S");
        engine.writeState (t);
        juce::File (t.getChildWithName ("Contour").getProperty ("take").toString()).deleteFile();
    }

    std::cout << "key detection\n";
    {
        std::vector<contour::Note> scale;
        int64_t t = 0;
        for (int m : { 57, 59, 60, 62, 64, 65, 67, 69, 60, 64, 69, 57 })
        {
            contour::Note n;
            n.centre = (float) m;
            n.start = t;
            n.end = t + 24000;
            t += 24000;
            scale.push_back (n);
        }
        const auto [key, sc] = contour::detectKey (scale);
        check ((key == 9 && sc == 2) || (key == 0 && sc == 1), "A minor / C major phrase detected as " + contour::keyNames()[key] + " " + contour::scaleNames()[sc]);
    }

    std::cout << (failures == 0 ? "all passed\n" : juce::String (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}
