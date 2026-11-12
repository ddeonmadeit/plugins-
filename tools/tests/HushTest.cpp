/*
    Hush on a synthetic sung phrase with known consonants: the sharp sounds are found where they are and
    labelled, no reduction means sample-exact output, a cut lowers only that sound (and in Split mode only
    above Freq), the live de-esser tames esses without touching vowels, and edits survive save / restore.
*/
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <dd_take/dd_take.h>

#include "Engine.h"

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
constexpr int64_t kStart = 24000;
struct Cons { char kind; double at, len; float level; };
const Cons kCons[] { { 's', 0.10, 0.16, 0.30f }, { 's', 0.90, 0.14, 0.12f }, { 't', 1.62, 0.02, 0.40f },
                     { 'h', 2.30, 0.15, 0.20f }, { 's', 3.05, 0.15, 0.55f } };

struct Filter
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    Filter (bool hp, double hz)
    {
        const double w = juce::MathConstants<double>::twoPi * hz / kSr, cw = std::cos (w), al = std::sin (w) / 1.4142, a0 = 1 + al;
        b0 = (hp ? (1 + cw) / 2 : (1 - cw) / 2) / a0; b1 = (hp ? -(1 + cw) : (1 - cw)) / a0; b2 = b0; a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
    float run (float x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
};

std::vector<float> phrase()
{
    const int len = (int) (kSr * 3.6);
    std::vector<float> x ((size_t) len, 0.0f);
    double phase = 0.0;
    const float notes[] { 57, 60, 62, 59 };
    for (int i = 0; i < len; ++i)
    {
        const double t = i / kSr;
        const int k = std::min (3, (int) (t / 0.8));
        const double f0 = 440.0 * std::pow (2.0, (notes[k] - 69.0) / 12.0);
        phase += f0 / kSr;
        double s = 0.0;
        for (int h = 1; h * f0 < 4000.0; ++h)
            s += std::sin (juce::MathConstants<double>::twoPi * h * phase) / h * (1.0 + 2.0 * std::exp (-std::pow ((h * f0 - 700.0) / 250.0, 2)));
        x[(size_t) i] = (float) (0.1 * s);
    }
    juce::Random rnd (5);
    for (const auto& c : kCons)
    {
        Filter h1 (true, c.kind == 's' ? 5500.0 : 2200.0), h2 (true, c.kind == 's' ? 5500.0 : 2200.0);
        Filter l1 (false, c.kind == 'h' ? 6000.0 : 11000.0), l2 (false, c.kind == 'h' ? 6000.0 : 11000.0);
        const int a = (int) (c.at * kSr), n = (int) (c.len * kSr);
        for (int i = 0; i < n; ++i)
        {
            const float u = (float) i / (float) n;
            const float env = c.kind == 't' ? std::exp (-u * 4.0f) : std::sin (juce::MathConstants<float>::pi * u);
            x[(size_t) (a + i)] += l2.run (l1.run (h2.run (h1.run (rnd.nextFloat() * 2.0f - 1.0f)))) * env * c.level * 2.0f;
        }
    }
    return x;
}

double bandDb (const std::vector<float>& y, int64_t a, int64_t b, double lo, double hi)
{
    std::vector<float> x (y.begin() + (long) a, y.begin() + (long) b);
    if (lo > 0.0) { Filter f1 (true, lo), f2 (true, lo); for (auto& v : x) v = f2.run (f1.run (v)); }
    if (hi > 0.0) { Filter f1 (false, hi), f2 (false, hi); for (auto& v : x) v = f2.run (f1.run (v)); }
    double e = 0.0;
    for (auto v : x) e += (double) v * v;
    return 10.0 * std::log10 (e / (double) x.size() + 1e-20);
}

std::vector<float> renderAll (const hush::Playback& pb, const dd::TakeStore& take, int64_t from, int64_t to)
{
    hush::Renderer r;
    std::vector<float> out ((size_t) (to - from));
    std::array<float, dd::TakeEngine::kMaxBlock> w {}, c {};
    for (int64_t t = from; t < to; t += dd::TakeEngine::kMaxBlock)
    {
        const int n = (int) std::min<int64_t> (dd::TakeEngine::kMaxBlock, to - t);
        r.render (pb, take, t, n, w.data(), c.data());
        for (int i = 0; i < n; ++i)
            out[(size_t) (t - from + i)] = take.get (t + i) * (1.0f - c[(size_t) i]) + w[(size_t) i] * c[(size_t) i];
    }
    return out;
}

struct Host : juce::AudioProcessor, juce::AudioPlayHead
{
    Host() : apvts (*this, nullptr, "Hush", hush::createLayout()) {}
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setTimeInSamples (pos);
        return p;
    }
    std::vector<float> play (hush::Engine& e, const std::vector<float>& src, bool offline)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf (1, 256);
        playing = true;
        for (pos = 0; pos < (int64_t) src.size(); pos += 256)
        {
            for (int i = 0; i < 256; ++i)
                buf.setSample (0, i, pos + i < (int64_t) src.size() ? src[(size_t) (pos + i)] : 0.0f);
            e.process (buf, this, offline);
            for (int i = 0; i < 256; ++i)
                out.push_back (buf.getSample (0, i));
        }
        playing = false;
        buf.clear();
        e.process (buf, this, offline);
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
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto voice = phrase();
    dd::TakeStore take;
    take.write (kStart, voice.data(), (int) voice.size());
    const dd::Span span { kStart, kStart + (int64_t) voice.size() };

    std::cout << "detection\n";
    uint32_t id = 1;
    const auto an = hush::detectSounds (take, span, kSr, 0.5f, id);
    for (const auto& c : kCons)
    {
        const auto at = kStart + (int64_t) (c.at * kSr);
        const hush::Sound* hit = nullptr;
        for (const auto& s : an.sounds)
            if (s.start < at + (int64_t) (c.len * kSr) && at < s.end)
                hit = &s;
        const juce::String name = c.kind == 's' ? "S" : c.kind == 'h' ? "SH" : "T";
        check (hit != nullptr, name + " at " + juce::String (c.at, 2) + " s found");
        if (hit != nullptr)
        {
            check (std::abs ((double) (hit->start - at) / kSr) < 0.03, "  starts within 30 ms (" + juce::String ((double) (hit->start - at) / kSr * 1000.0, 0) + " ms)");
            check (hush::kindNames()[hit->kind] == name, "  labelled " + hush::kindNames()[hit->kind] + " (made a " + name + ")");
        }
    }
    check (an.sounds.size() <= std::size (kCons) + 1, juce::String ((int) an.sounds.size()) + " sounds found, no vowels taken for esses");

    const std::vector<dd::Span> spans { span };
    std::cout << "rendering\n";
    {
        hush::Playback pb;
        pb.spans = spans;
        const auto y = renderAll (pb, take, span.start, span.end);
        float err = 0.0f;
        for (size_t i = 0; i < y.size(); ++i) err = std::max (err, std::abs (y[i] - voice[i]));
        check (err < 1.0e-7f, "no cuts: sample-exact");
    }
    if (an.sounds.empty())
        return 1;
    const auto loud = *std::max_element (an.sounds.begin(), an.sounds.end(), [] (const hush::Sound& a, const hush::Sound& b) { return a.peakDb < b.peakDb; });
    {
        hush::Playback pb;
        pb.spans = spans;
        pb.events.push_back ({ loud.start, loud.end, juce::Decibels::decibelsToGain (-12.0f) });
        pb.low = hush::lowBand (take, spans, kSr, 5000.0f);
        pb.fade = 240;
        const auto y = renderAll (pb, take, span.start, span.end);
        const auto a = loud.start - kStart + 500, b = loud.end - kStart - 500;
        const double hiCut = bandDb (voice, a, b, 7000.0, 0.0) - bandDb (y, a, b, 7000.0, 0.0);
        const double loCut = bandDb (voice, a, b, 0.0, 2500.0) - bandDb (y, a, b, 0.0, 2500.0);
        check (std::abs (hiCut - 12.0) < 1.5, "Split -12 dB: the top of that sound down " + juce::String (hiCut, 1) + " dB");
        check (std::abs (loCut) < 0.5, "  and the voice under Freq untouched (" + juce::String (loCut, 2) + " dB)");
        float err = 0.0f;
        for (size_t i = 0; i < y.size(); ++i)
            if ((int64_t) i + kStart < loud.start - 300 || (int64_t) i + kStart > loud.end + 300)
                err = std::max (err, std::abs (y[i] - voice[i]));
        check (err < 1.0e-7f, "  everything else sample-exact");
        pb.wide = true;
        const auto z = renderAll (pb, take, span.start, span.end);
        const double wideCut = bandDb (voice, a, b, 0.0, 0.0) - bandDb (z, a, b, 0.0, 0.0);
        check (std::abs (wideCut - 12.0) < 0.3, "Wide -12 dB: the whole sound down " + juce::String (wideCut, 1) + " dB");
    }

    std::cout << "live de-esser\n";
    {
        hush::LiveDeEsser live;
        live.prepare (kSr);
        hush::Settings s;
        s.amount = 1.0f;
        juce::AudioBuffer<float> buf (1, (int) voice.size());
        buf.copyFrom (0, 0, voice.data(), (int) voice.size());
        for (int p = 0; p < buf.getNumSamples(); p += 512)
            live.process (buf, p, std::min (512, buf.getNumSamples() - p), s);
        std::vector<float> y (buf.getReadPointer (0), buf.getReadPointer (0) + buf.getNumSamples());
        const auto& c = kCons[4];
        const auto a = (int64_t) ((c.at + 0.03) * kSr), b = (int64_t) ((c.at + c.len - 0.03) * kSr);
        const double cut = bandDb (voice, a, b, 7000.0, 0.0) - bandDb (y, a, b, 7000.0, 0.0);
        check (cut > 4.0, "a loud S comes down " + juce::String (cut, 1) + " dB");
        const auto va = (int64_t) (1.2 * kSr), vb = (int64_t) (1.5 * kSr);
        const double vowel = bandDb (voice, va, vb, 0.0, 0.0) - bandDb (y, va, vb, 0.0, 0.0);
        check (std::abs (vowel) < 0.5, "a vowel stays (" + juce::String (vowel, 2) + " dB)");
    }

    std::cout << "engine: capture, set levels, save, restore\n";
    {
        std::vector<float> song ((size_t) kStart, 0.0f);
        song.insert (song.end(), voice.begin(), voice.end());
        juce::ValueTree saved;
        {
            Host host;
            hush::Engine engine (host.apvts);
            engine.prepare (kSr, 256);
            engine.prepareLive (kSr);
            host.play (engine, song, true);
            check (waitFor ([&] { return engine.getStatus() == hush::Status::Ready; }), "capture analysed");
            auto snap = engine.snapshot();
            check (snap.sounds.size() >= 4, juce::String ((int) snap.sounds.size()) + " sounds in the engine");
            if (snap.sounds.empty())
                return 1;
            auto ed = snap.sounds;
            ed[0].manual = true;
            ed[0].userDb = -20.0f;
            engine.setSounds (ed);
            engine.commit (snap.sounds);
            check (waitFor ([&] { juce::ValueTree t ("S"); engine.writeState (t); return t.getChildWithName ("Hush").getProperty ("take").toString().isNotEmpty(); }),
                   "take saved");
            juce::Thread::sleep (50);
            const auto out = host.play (engine, song, true);
            const auto a = ed[0].start + 400, b = ed[0].end - 400;
            const double cut = bandDb (song, a, b, 7000.0, 0.0) - bandDb (out, a, b, 7000.0, 0.0);
            check (cut > 15.0, "the hand-set -20 dB plays back (" + juce::String (cut, 1) + " dB off the top)");
            check (engine.getFollowCaptures() == 0, "playing the same vocal again captures nothing");

            // A new take over the end of the phrase: picked up by itself while it plays, the hand-set level
            // on the first sound is kept.
            auto song2 = song;
            const auto from2 = (size_t) (kStart + (int64_t) (2.9 * kSr));
            for (size_t i = from2; i < song2.size(); ++i)
                song2[i] = song[(size_t) kStart + (i - from2)];
            host.play (engine, song2, false);
            check (engine.getFollowCaptures() >= 1, "a new take is captured by itself");
            check (waitFor ([&] { return engine.getStatus() == hush::Status::Ready; }), "and analysed");
            const auto snap2 = engine.snapshot();
            bool foundNew = false;
            for (const auto& sd : snap2.sounds)
                foundNew = foundNew || (sd.start >= (int64_t) from2 + (int64_t) (0.05 * kSr) && sd.start < (int64_t) from2 + (int64_t) (0.3 * kSr));
            check (foundNew, "the new take's first ess is found");
            check (! snap2.sounds.empty() && snap2.sounds[0].manual && std::abs (snap2.sounds[0].userDb + 20.0f) < 1.0e-3f,
                   "the hand-set level earlier in the song is kept");
            juce::ValueTree parent ("S");
            engine.writeState (parent);
            saved = parent.getChildWithName ("Hush").createCopy();
        }
        {
            Host host;
            hush::Engine engine (host.apvts);
            juce::ValueTree parent ("S");
            parent.appendChild (saved, nullptr);
            engine.readState (parent);
            engine.prepare (kSr, 256);
            check (waitFor ([&] { return engine.getStatus() == hush::Status::Ready && engine.hasTake(); }), "reopened");
            const auto snap = engine.snapshot();
            check (! snap.sounds.empty() && snap.sounds[0].manual && std::abs (snap.sounds[0].userDb + 20.0f) < 1.0e-3f, "the hand-set level came back");
        }
        juce::File (saved.getProperty ("take").toString()).deleteFile();
    }

    std::cout << (failures == 0 ? "all passed\n" : juce::String (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}
