/*
    Generic headless harness, compiled once per plugin (see cmake/DDPlugin.cmake).
    It only talks to the plugin through juce::AudioProcessor, so it works for any plugin in the repo.

      <Plugin>Harness list
      <Plugin>Harness render  --out x.wav [--program N | --preset-file p.json] [--set id=value ...]
                              [--phrase demo|single|slide|groove|chords|pads|mixed|guitar|drums|drumvel] [--sr 48000] [--block 256] [--mono]
                              [--bpm 79]   host tempo, transport playing from beat 0 (default: no host)
                              --phrase "notes:<note>,<start s>,<length s>,<velocity> ... [end=<s>]"   any notes (one-shots, loops)
      <Plugin>Harness render-all --outdir dir [--phrase demo] [--sr 48000] [--bpm 80]
      <Plugin>Harness snapshot --out ui.png [--program N] [--scale 1]
      <Plugin>Harness stress  [--seconds 20] [--sr 48000] [--block 128]
      <Plugin>Harness dump-params    current values as preset-style JSON
      <Plugin>Harness param-info     ids, ranges, kinds and choice names (used by tools/qa/preset_lint.py)
*/

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <dd_core/dd_core.h>

#include <chrono>
#include <iostream>

namespace
{
struct Note { int note; double start, length; int velocity; };

std::vector<Note> phrase (const juce::String& name, double& totalSeconds)
{
    if (name.startsWith ("notes:"))
    {
        // Ad-hoc notes, e.g. "notes:50,0,4,100 55,0.02,4,96 end=7" (default end: last note off + 3 s).
        std::vector<Note> out;
        double end = 0.0;
        for (auto& token : juce::StringArray::fromTokens (name.fromFirstOccurrenceOf (":", false, false), " ", ""))
        {
            if (token.startsWith ("end="))
            {
                end = token.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
                continue;
            }
            const auto f = juce::StringArray::fromTokens (token, ",", "");
            if (f.size() == 4)
                out.push_back ({ f[0].getIntValue(), f[1].getDoubleValue(), f[2].getDoubleValue(), juce::jlimit (1, 127, f[3].getIntValue()) });
        }
        if (end <= 0.0)
            for (auto& n : out)
                end = std::max (end, n.start + n.length + 3.0);
        totalSeconds = std::max (end, 0.1);
        return out;
    }
    if (name == "single")
    {
        totalSeconds = 3.0;
        return { { 33, 0.0, 2.2, 110 } };                       // A1, 55 Hz
    }
    if (name == "slide")
    {
        totalSeconds = 4.0;
        return { { 31, 0.0, 1.1, 110 }, { 38, 1.0, 1.0, 110 },  // overlapping -> glide up
                 { 29, 1.9, 1.2, 110 } };                       // overlapping -> glide down
    }
    if (name == "groove")
    {
        // Two bars of a 140 BPM trap line in E minor with one legato slide (for listening previews).
        const double s16 = 60.0 / 140.0 / 4.0;
        totalSeconds = 34 * s16 + 1.2;
        auto n = [s16] (int note, double start, double len, int vel) { return Note { note, start * s16, len * s16, vel }; };
        return { n (28, 0, 5, 115), n (28, 6, 2, 100), n (28, 9, 1.5, 95), n (31, 11, 4, 110), n (35, 14, 2, 110),
                 n (33, 16, 6, 115), n (33, 22.5, 1, 90), n (26, 24, 3, 110), n (38, 28, 3, 105), n (28, 31, 3, 120) };
    }
    if (name == "drums")
    {
        // Five bars at 96 BPM on the General MIDI drum map: groove with ghost notes and an open hat,
        // a ride bar with bell accents, a tom fill, then crashes ringing out.
        const double s16 = 60.0 / 96.0 / 4.0;
        std::vector<Note> out;
        auto hit = [&] (int note, double sixteenth, int vel) { out.push_back ({ note, sixteenth * s16, 0.1, vel }); };
        for (int bar = 0; bar < 2; ++bar)
        {
            const int o = bar * 16;
            for (int k = 0; k < 16; k += 2)
                hit (bar == 1 && k == 14 ? 46 : 42, o + k, k % 4 == 0 ? 96 : 72);
            hit (36, o + 0, 112); hit (36, o + 6, 90); hit (36, o + 8, 108);
            hit (38, o + 4, 115); hit (38, o + 12, 118);
            hit (38, o + 7, 34); hit (38, o + 15, 28);
            if (bar == 1) hit (44, o + 16, 70);
        }
        for (int k = 0; k < 16; k += 2)
            hit (k % 4 == 2 ? 53 : 51, 32 + k, k % 4 == 2 ? 88 : 80);
        hit (36, 32, 110); hit (36, 42, 96); hit (37, 36, 90); hit (37, 44, 95);
        const int fill[] = { 48, 48, 48, 48, 45, 45, 45, 45, 43, 43, 43, 43, 40, 40, 38, 38 };
        for (int k = 0; k < 16; ++k)
            hit (fill[k], 48 + k, 70 + (k % 4) * 12);
        hit (36, 48, 100); hit (36, 56, 100);
        hit (36, 64, 120); hit (49, 64, 120); hit (57, 72, 105); hit (52, 76, 100);
        totalSeconds = 80 * s16 + 3.0;
        return out;
    }
    if (name == "drumvel")
    {
        // Every kit piece hit 8 times from soft to hard (velocity response check).
        const int notes[] = { 36, 38, 40, 37, 42, 46, 44, 48, 45, 43, 49, 57, 51, 53, 52 };
        std::vector<Note> out;
        double t = 0.0;
        for (int note : notes)
        {
            for (int i = 0; i < 8; ++i)
            {
                out.push_back ({ note, t, 0.1, 16 + i * 16 > 127 ? 127 : 16 + i * 16 });
                t += 0.45;
            }
            t += 0.6;
        }
        totalSeconds = t + 2.0;
        return out;
    }
    if (name == "pads")
    {
        // Four chords, two bars each at 120 BPM (Dm9, Bbmaj7, Fmaj7, Am7) in pad voicings.
        // The top note comes last, so mono / legato patches play it as a melody.
        const int chords[4][5] = { { 50, 57, 60, 64, 65 }, { 46, 53, 57, 62, -1 }, { 41, 52, 57, 60, -1 }, { 45, 52, 55, 60, 64 } };
        std::vector<Note> out;
        for (int c = 0; c < 4; ++c)
            for (int note : chords[c])
                if (note >= 0)
                    out.push_back ({ note, c * 4.0, 3.85, 92 });
        totalSeconds = 16.0 + 4.0;
        return out;
    }
    if (name == "guitar")
    {
        // 80 BPM: three bars strummed on open-chord voicings (down 1, down 2, up 2&, up 3&, down 4,
        // up 4&), then a bar of single-note riff ending on a held note.
        const std::vector<std::vector<int>> chords { { 40, 47, 52, 55, 59, 64 }, { 48, 52, 55, 60, 64 },
                                                     { 43, 47, 50, 55, 59, 67 } };
        const double beat = 0.75;
        const double hits[6] = { 0.0, 1.0, 1.5, 2.5, 3.0, 3.5 };
        const bool downStroke[6] = { true, true, false, false, true, false };
        std::vector<Note> out;
        for (int bar = 0; bar < 3; ++bar)
            for (int h = 0; h < 6; ++h)
            {
                const double t = (bar * 4 + hits[h]) * beat;
                const double next = h < 5 ? (bar * 4 + hits[h + 1]) * beat : (bar + 1) * 4 * beat;
                for (int note : chords[(size_t) bar])
                    out.push_back ({ note, t, next - t - 0.03, downStroke[h] ? 104 : 84 });
            }
        const int riff[8] = { 64, 67, 69, 71, 74, 71, 69, 67 };
        for (int i = 0; i < 8; ++i)
            out.push_back ({ riff[i], (12 + i * 0.5) * beat, 0.5 * beat - 0.02, 96 });
        out.push_back ({ 64, 16 * beat, 2.0 * beat, 100 });
        totalSeconds = 16 * beat + 3.0;
        return out;
    }
    if (name == "mixed")
    {
        // Everything a pad / keys / pluck patch gets played with, 120 BPM: a held Dm9, an eighth-note
        // arpeggio over a Bb bass, Fmaj7 stabs into a held chord, then a legato melody.
        std::vector<Note> out;
        for (int note : { 50, 57, 60, 64, 65 })
            out.push_back ({ note, 0.0, 3.85, 92 });
        out.push_back ({ 46, 4.0, 3.85, 90 });
        const int arp[8] = { 58, 62, 65, 69, 70, 69, 65, 62 };
        for (int i = 0; i < 16; ++i)
            out.push_back ({ arp[i % 8], 4.0 + i * 0.25, 0.22, 80 + (i % 4 == 0 ? 30 : (i * 7) % 20) });
        for (double t : { 8.0, 8.75, 9.5 })
            for (int note : { 53, 57, 60, 64 })
                out.push_back ({ note, t, t < 9.0 ? 0.4 : 2.35, 100 });
        const std::pair<int, double> melody[5] = { { 69, 0.5 }, { 67, 0.5 }, { 65, 0.5 }, { 64, 1.0 }, { 62, 1.4 } };
        double t = 12.0;
        for (auto [note, len] : melody)
        {
            out.push_back ({ note, t, len + 0.03, 96 });
            t += len;
        }
        totalSeconds = 16.0 + 4.0;
        return out;
    }
    if (name == "chords")
    {
        totalSeconds = 4.0;
        return { { 36, 0.0, 2.0, 100 }, { 43, 0.0, 2.0, 100 }, { 48, 0.0, 2.0, 100 },
                 { 41, 2.0, 1.5, 100 }, { 48, 2.0, 1.5, 100 } };
    }
    // demo: long note, bouncy repeats, a legato slide, then a final hit with a tail
    totalSeconds = 7.5;
    return { { 36, 0.00, 1.60, 110 }, { 31, 1.80, 0.40, 100 }, { 31, 2.30, 0.20, 90 },
             { 34, 2.60, 0.80, 105 }, { 38, 3.30, 0.90, 105 }, { 29, 4.40, 0.35, 120 },
             { 29, 4.85, 0.35, 100 }, { 33, 5.30, 1.00, 110 } };
}

struct Args
{
    juce::StringArray raw;
    juce::String get (const juce::String& key, const juce::String& def = {}) const
    {
        const int i = raw.indexOf (key);
        return i >= 0 && i + 1 < raw.size() ? raw[i + 1] : def;
    }
    juce::StringArray getAll (const juce::String& key) const
    {
        juce::StringArray out;
        for (int i = 0; i + 1 < raw.size(); ++i)
            if (raw[i] == key) out.add (raw[i + 1]);
        return out;
    }
    bool has (const juce::String& key) const { return raw.contains (key); }
};

void applySets (juce::AudioProcessor& proc, const juce::StringArray& sets)
{
    if (sets.isEmpty())
        return;
    auto current = dd::captureParameterValues (proc);
    auto* obj = current.getDynamicObject();
    for (auto& s : sets)
    {
        const auto key = s.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto val = s.fromFirstOccurrenceOf ("=", false, false).trim();
        if (val == "true" || val == "false")         obj->setProperty (key, val == "true");
        else if (val.containsOnly ("-+.0123456789e")) obj->setProperty (key, val.getDoubleValue());
        else                                          obj->setProperty (key, val);
    }
    for (auto& u : dd::applyParameterValues (proc, current))
        std::cerr << "warning: unknown parameter '" << u << "'\n";
}

bool loadPresetFile (juce::AudioProcessor& proc, const juce::File& f)
{
    juce::var parsed;
    if (juce::JSON::parse (f.loadFileAsString(), parsed).failed())
    {
        std::cerr << "error: could not parse " << f.getFullPathName() << "\n";
        return false;
    }
    for (auto& u : dd::applyParameterValues (proc, parsed.getProperty ("params", juce::var())))
        std::cerr << "warning: unknown parameter '" << u << "' in " << f.getFileName() << "\n";
    return true;
}

/** Test programme for effect plugins: kick-like sine drops, noise hats and a saw chord. */
juce::AudioBuffer<float> effectTestSignal (double sr, double seconds)
{
    const int n = (int) (seconds * sr);
    juce::AudioBuffer<float> buf (2, n);
    buf.clear();
    juce::Random rnd (42);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double beat = std::fmod (t, 0.5), eighth = std::fmod (t, 0.25);
        const double kickPitch = 50.0 + 120.0 * std::exp (-beat * 40.0);
        const double kick = std::sin (juce::MathConstants<double>::twoPi * kickPitch * beat) * std::exp (-beat * 6.0) * 0.6;
        const double hat = (rnd.nextFloat() * 2.0f - 1.0f) * std::exp (-eighth * 60.0) * 0.12;
        double chord = 0.0;
        for (double f : { 110.0, 138.6, 164.8 })
            chord += (std::fmod (t * f, 1.0) * 2.0 - 1.0) * 0.06;
        const float l = (float) (kick + hat + chord), r = (float) (kick + hat * 0.7 + chord);
        buf.setSample (0, i, l);
        buf.setSample (1, i, r);
    }
    return buf;
}

// A host transport for --bpm: playing from beat 0, so tempo-synced LFOs, delays and pumps lock to the notes.
struct FixedPlayHead : juce::AudioPlayHead
{
    double bpm = 120.0, sr = 48000.0;
    juce::int64 sample = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setIsPlaying (true);
        p.setTimeInSamples (sample);
        p.setTimeInSeconds ((double) sample / sr);
        p.setPpqPosition ((double) sample / sr * bpm / 60.0);
        return p;
    }
};

juce::AudioBuffer<float> renderPhrase (juce::AudioProcessor& proc, const juce::String& phraseName, double sr, int block = 256, double bpm = 0.0)
{
    double total = 0.0;
    auto notes = phrase (phraseName, total);

    FixedPlayHead playHead;
    playHead.bpm = bpm;
    playHead.sr = sr;
    proc.setPlayHead (bpm > 0.0 ? &playHead : nullptr);

    // Effects get the test programme on their input; instruments get silence + MIDI.
    // The current bus layout is kept (see render --mono); output is always written as stereo.
    const bool isEffect = proc.getBusCount (true) > 0 && proc.getChannelCountOfBus (true, 0) > 0;
    const int numOut = std::max (1, proc.getTotalNumOutputChannels());
    const int numIn = isEffect ? proc.getTotalNumInputChannels() : 0;
    const int numBuf = std::max (numIn, numOut);
    proc.setRateAndBufferSizeDetails (sr, block);
    proc.setNonRealtime (true); // plugins that stream or load samples must finish loading first
    proc.prepareToPlay (sr, block);
    const auto input = isEffect ? effectTestSignal (sr, total) : juce::AudioBuffer<float>();

    const int totalSamples = (int) (total * sr);
    juce::AudioBuffer<float> out (2, totalSamples);
    out.clear();
    juce::AudioBuffer<float> buf (numBuf, block);
    juce::MidiBuffer midi;

    struct Ev { int sample; juce::MidiMessage msg; };
    std::vector<Ev> events;
    for (auto& n : notes)
    {
        events.push_back ({ (int) (n.start * sr), juce::MidiMessage::noteOn (1, n.note, (juce::uint8) n.velocity) });
        events.push_back ({ (int) ((n.start + n.length) * sr), juce::MidiMessage::noteOff (1, n.note) });
    }
    std::stable_sort (events.begin(), events.end(), [] (const Ev& a, const Ev& b)
    {
        if (a.sample != b.sample) return a.sample < b.sample;
        return a.msg.isNoteOff() && b.msg.isNoteOn(); // offs first at the same time
    });

    size_t ev = 0;
    for (int pos = 0; pos < totalSamples; pos += block)
    {
        const int n = std::min (block, totalSamples - pos);
        midi.clear();
        while (ev < events.size() && events[ev].sample < pos + n)
        {
            midi.addEvent (events[ev].msg, std::max (0, events[ev].sample - pos));
            ++ev;
        }
        buf.setSize (numBuf, n, false, false, true);
        buf.clear();
        if (isEffect)
            for (int ch = 0; ch < numIn; ++ch)
                buf.copyFrom (ch, 0, input, std::min (ch, 1), pos, n);
        playHead.sample = pos;
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, buf, std::min (ch, numOut - 1), 0, n);
    }
    proc.setPlayHead (nullptr);

    // Remove the reported latency so renders line up with the MIDI.
    const int lat = proc.getLatencySamples();
    if (lat > 0 && lat < totalSamples)
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = out.getWritePointer (ch);
            std::memmove (d, d + lat, (size_t) (totalSamples - lat) * sizeof (float));
            std::fill (d + totalSamples - lat, d + totalSamples, 0.0f);
        }

    proc.releaseResources();
    return out;
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buf, double sr)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return false;
    std::unique_ptr<juce::OutputStream> os (stream.release());
    auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                                                            .withNumChannels (2)
                                                                            .withBitsPerSample (32)
                                                                            .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer == nullptr)
        return false;
    return writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

juce::String safeName (const juce::String& s)
{
    return juce::File::createLegalFileName (s).replaceCharacters (" /", "__");
}

int cmdList (juce::AudioProcessor& proc)
{
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        std::cout << i << "\t" << proc.getProgramName (i) << "\n";
    return 0;
}

int cmdDumpParams (juce::AudioProcessor& proc)
{
    std::cout << juce::JSON::toString (dd::captureParameterValues (proc), false) << "\n";
    return 0;
}

int cmdParamInfo (juce::AudioProcessor& proc)
{
    juce::Array<juce::var> list;
    for (auto* p : proc.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr)
            continue;
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", rp->getParameterID());
        o->setProperty ("name", rp->getName (64));
        const auto& range = rp->getNormalisableRange();
        o->setProperty ("min", range.start);
        o->setProperty ("max", range.end);
        o->setProperty ("default", rp->convertFrom0to1 (rp->getDefaultValue()));
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (rp))
        {
            o->setProperty ("kind", "choice");
            juce::Array<juce::var> ch;
            for (auto& name : c->choices) ch.add (name);
            o->setProperty ("choices", ch);
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr) o->setProperty ("kind", "bool");
        else if (dynamic_cast<juce::AudioParameterInt*> (rp) != nullptr)  o->setProperty ("kind", "int");
        else                                                               o->setProperty ("kind", "float");
        list.add (juce::var (o));
    }
    std::cout << juce::JSON::toString (juce::var (list)) << "\n";
    return 0;
}

int cmdRender (juce::AudioProcessor& proc, const Args& a)
{
    const double sr = a.get ("--sr", "48000").getDoubleValue();
    if (a.has ("--program"))
        proc.setCurrentProgram (a.get ("--program").getIntValue());
    if (a.has ("--preset-file") && ! loadPresetFile (proc, juce::File (a.get ("--preset-file"))))
        return 1;
    applySets (proc, a.getAll ("--set"));

    if (a.has ("--mono"))
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.outputBuses.add (juce::AudioChannelSet::mono());
        if (proc.getBusCount (true) > 0)
            layout.inputBuses.add (juce::AudioChannelSet::mono());
        if (! proc.setBusesLayout (layout))
        {
            std::cerr << "error: plugin rejected a mono layout\n";
            return 1;
        }
    }
    auto buf = renderPhrase (proc, a.get ("--phrase", "demo"), sr, a.get ("--block", "256").getIntValue(), a.get ("--bpm", "0").getDoubleValue());
    const juce::File out (a.get ("--out", "render.wav"));
    if (! writeWav (out, buf, sr))
    {
        std::cerr << "error: could not write " << out.getFullPathName() << "\n";
        return 1;
    }
    std::cout << "wrote " << out.getFullPathName() << "  peak " << juce::Decibels::gainToDecibels (buf.getMagnitude (0, buf.getNumSamples())) << " dBFS\n";
    return 0;
}

int cmdRenderAll (juce::AudioProcessor& proc, const Args& a)
{
    const double sr = a.get ("--sr", "48000").getDoubleValue();
    const juce::File dir (a.get ("--outdir", "qa-out/renders"));
    const auto phraseName = a.get ("--phrase", "demo");
    dir.createDirectory();

    juce::Array<juce::var> index;
    for (int i = 0; i < proc.getNumPrograms(); ++i)
    {
        proc.setCurrentProgram (i);
        auto buf = renderPhrase (proc, phraseName, sr, 256, a.get ("--bpm", "0").getDoubleValue());
        const auto name = proc.getProgramName (i);
        const auto file = dir.getChildFile (juce::String (i).paddedLeft ('0', 3) + "_" + safeName (name) + ".wav");
        if (! writeWav (file, buf, sr))
            return 1;
        auto* o = new juce::DynamicObject();
        o->setProperty ("index", i);
        o->setProperty ("program", name);
        o->setProperty ("file", file.getFileName());
        index.add (juce::var (o));
        std::cout << "[" << i << "] " << name << "\n";
    }
    dir.getChildFile ("index.json").replaceWithText (juce::JSON::toString (juce::var (index)));
    return 0;
}

int cmdSnapshot (juce::AudioProcessor& proc, const Args& a)
{
    if (a.has ("--program"))
        proc.setCurrentProgram (a.get ("--program").getIntValue());
    applySets (proc, a.getAll ("--set"));
    proc.setPlayConfigDetails (0, 2, 48000.0, 256);
    proc.setNonRealtime (true);   // plugins that stream samples finish loading before the picture
    proc.prepareToPlay (48000.0, 256);

    // Feed some audio so meters / scopes have something to draw.
    {
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110), 0);
        for (int i = 0; i < 60; ++i) { proc.processBlock (buf, midi); midi.clear(); }
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditorAndMakeActive());
    if (ed == nullptr)
    {
        std::cerr << "error: no editor\n";
        return 1;
    }
    const float scale = a.get ("--scale", "1").getFloatValue();

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
    const juce::File out (a.get ("--out", "ui.png"));
    out.deleteFile();
    juce::FileOutputStream fos (out);
    juce::PNGImageFormat png;
    if (! fos.openedOk() || ! png.writeImageToStream (img, fos))
    {
        std::cerr << "error: could not write " << out.getFullPathName() << "\n";
        return 1;
    }
    std::cout << "wrote " << out.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
    proc.editorBeingDeleted (ed.get());
    return 0;
}

int cmdStress (juce::AudioProcessor& proc, const Args& a)
{
    const double sr = a.get ("--sr", "48000").getDoubleValue();
    const double seconds = a.get ("--seconds", "20").getDoubleValue();
    const int block = a.get ("--block", "128").getIntValue();
    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);

    juce::Random rnd (1234);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    auto& ps = proc.getParameters();
    float maxPeak = 0.0f;
    int bad = 0;
    const int blocks = (int) (seconds * sr / block);
    const bool isEffect = proc.getBusCount (true) > 0 && proc.getChannelCountOfBus (true, 0) > 0;
    if (isEffect)
    {
        proc.setPlayConfigDetails (2, 2, sr, block);
        proc.prepareToPlay (sr, block);
    }

    // One offline block first so sample-streaming plugins have their sounds loaded; the test itself is realtime.
    proc.setNonRealtime (true);
    buf.clear();
    proc.processBlock (buf, midi);
    proc.setNonRealtime (false);

    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (rnd.nextFloat() < 0.08f)
            midi.addEvent (juce::MidiMessage::noteOn (1, 20 + rnd.nextInt (60), (juce::uint8) (1 + rnd.nextInt (126))), rnd.nextInt (block));
        if (rnd.nextFloat() < 0.06f)
            midi.addEvent (juce::MidiMessage::noteOff (1, 20 + rnd.nextInt (60)), rnd.nextInt (block));
        if (rnd.nextFloat() < 0.01f)
            midi.addEvent (juce::MidiMessage::pitchWheel (1, rnd.nextInt (16384)), 0);
        if (b % 400 == 0)
            proc.setCurrentProgram (rnd.nextInt (std::max (1, proc.getNumPrograms())));
        if (rnd.nextFloat() < 0.3f)
        {
            auto* p = ps[rnd.nextInt (ps.size())];
            p->setValueNotifyingHost (rnd.nextFloat());
        }

        if (isEffect)
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, (rnd.nextFloat() * 2.0f - 1.0f) * 0.3f);
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* d = buf.getReadPointer (ch);
            for (int i = 0; i < block; ++i)
            {
                if (! std::isfinite (d[i])) ++bad;
                else maxPeak = std::max (maxPeak, std::abs (d[i]));
            }
        }
    }
    const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::cout << "stress: " << seconds << " s audio in " << elapsed << " s (" << (seconds / elapsed) << "x realtime), "
              << "peak " << juce::Decibels::gainToDecibels (maxPeak) << " dBFS, non-finite samples: " << bad << "\n";
    proc.releaseResources();
    return bad == 0 ? 0 : 2;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    Args a;
    for (int i = 1; i < argc; ++i)
        a.raw.add (juce::String::fromUTF8 (argv[i]));

    const auto cmd = a.raw.isEmpty() ? juce::String ("list") : a.raw[0];
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());

    // Render presets as designed: performance switches ("perf*", e.g. Poly) off unless --perf is given.
    if (! a.has ("--perf"))
        for (auto* p : proc->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && dd::isPerformanceParam (rp->getParameterID()))
                rp->setValueNotifyingHost (0.0f);

    int rc = 0;
    if (cmd == "list")             rc = cmdList (*proc);
    else if (cmd == "dump-params") rc = cmdDumpParams (*proc);
    else if (cmd == "param-info")  rc = cmdParamInfo (*proc);
    else if (cmd == "render")      rc = cmdRender (*proc, a);
    else if (cmd == "render-all")  rc = cmdRenderAll (*proc, a);
    else if (cmd == "snapshot")    rc = cmdSnapshot (*proc, a);
    else if (cmd == "stress")      rc = cmdStress (*proc, a);
    else
    {
        std::cerr << "unknown command '" << cmd << "' (list | dump-params | param-info | render | render-all | snapshot | stress)\n";
        rc = 64;
    }

    proc.reset();
    return rc;
}
