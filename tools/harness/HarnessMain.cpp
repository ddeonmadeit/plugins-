/*
    Generic headless harness, compiled once per plugin (see cmake/DDPlugin.cmake).
    It only talks to the plugin through juce::AudioProcessor, so it works for any plugin in the repo.

      <Plugin>Harness list
      <Plugin>Harness render  --out x.wav [--program N | --preset-file p.json] [--set id=value ...]
                              [--phrase demo|single|slide|groove|chords] [--sr 48000] [--block 256] [--mono]
      <Plugin>Harness render-all --outdir dir [--phrase demo] [--sr 48000]
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

juce::AudioBuffer<float> renderPhrase (juce::AudioProcessor& proc, const juce::String& phraseName, double sr, int block = 256)
{
    double total = 0.0;
    auto notes = phrase (phraseName, total);

    // Effects get the test programme on their input; instruments get silence + MIDI.
    // The current bus layout is kept (see render --mono); output is always written as stereo.
    const bool isEffect = proc.getBusCount (true) > 0 && proc.getChannelCountOfBus (true, 0) > 0;
    const int numOut = std::max (1, proc.getTotalNumOutputChannels());
    const int numIn = isEffect ? proc.getTotalNumInputChannels() : 0;
    const int numBuf = std::max (numIn, numOut);
    proc.setRateAndBufferSizeDetails (sr, block);
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
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, buf, std::min (ch, numOut - 1), 0, n);
    }

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
    auto buf = renderPhrase (proc, a.get ("--phrase", "demo"), sr, a.get ("--block", "256").getIntValue());
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
        auto buf = renderPhrase (proc, phraseName, sr);
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
    proc.setPlayConfigDetails (0, 2, 48000.0, 256);
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
