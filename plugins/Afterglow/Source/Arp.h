#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <dd_dsp/dd_dsp.h>

namespace ag
{
/**
    Arpeggiator: rewrites one block's MIDI. Held keys go in; notes come out one step at a time, in time
    with the song (locked to the host's beat grid while it plays, free-running when stopped). The first key
    of a phrase sounds at once, so playing in never waits for the grid.

      Up / Down / Up Down   held notes in pitch order across 1-4 octaves
      Random                any held note, never the same twice in a row
      As Played             the order the keys went down
      Chord                 every held note together on each step (rhythmic stabs)

    Gate is each note's length as a share of the step; swing pushes every second step later.
    Everything that is not a note passes straight through.
*/
class Arpeggiator
{
public:
    struct Settings
    {
        int mode = 0, octaves = 1;
        double stepBeats = 0.25;
        float gate = 0.6f, swing = 0.0f;
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // Room for every key and every pattern step up front: the audio thread never allocates.
        held.reserve (128);
        order.reserve (128);
        sounding.reserve (128);
        pattern.reserve (128 * 4 * 2);
        scratch.reserve (128);
        out.ensureSize (8192);
        reset();
    }

    void reset()
    {
        held.clear();
        sounding.clear();
        order.clear();
        stepIndex = 0;
        patternPos = -1;
        lastRandom = -1;
        samplesToStep = 0.0;
        samplesToOff = -1.0;
        running = false;
        rng.seed (0xA4B9u);
    }

    /** Rewrites `midi` (n samples, starting at host beat `beat`, tempo `bpm`, host playing or not). */
    void process (juce::MidiBuffer& midi, int n, double beat, double bpm, bool hostPlaying, const Settings& s)
    {
        out.clear();
        const double samplesPerBeat = sr * 60.0 / bpm;
        const double stepSamples = s.stepBeats * samplesPerBeat;

        // Locked to the song while it plays: re-aim the next step at the grid every block.
        if (hostPlaying && running)
        {
            const double idx = std::floor (beat / s.stepBeats + 1.0e-9);
            const double next = (idx + 1.0) * s.stepBeats + swingOffset (s, (int64_t) idx + 1) - beat;
            samplesToStep = std::max (0.0, next * samplesPerBeat);
            stepIndex = (int64_t) idx + 1;
        }

        auto it = midi.cbegin();
        for (int t = 0; t < n; ++t)
        {
            // Incoming events at this sample.
            for (; it != midi.cend() && (*it).samplePosition <= t; ++it)
            {
                const auto m = (*it).getMessage();
                if (m.isNoteOn())
                {
                    const bool first = held.empty();
                    addHeld (m.getNoteNumber(), m.getFloatVelocity());
                    if (first)
                    {
                        // Start the phrase on this key, now.
                        patternPos = -1;
                        running = true;
                        fireStep (t, s, stepSamples);
                        stepIndex = hostPlaying ? (int64_t) std::floor ((beat + t / samplesPerBeat) / s.stepBeats + 1.0e-9) + 1 : stepIndex + 1;
                        samplesToStep = hostPlaying ? std::max (1.0, ((double) stepIndex * s.stepBeats + swingOffset (s, stepIndex)
                                                                     - (beat + t / samplesPerBeat)) * samplesPerBeat)
                                                    : stepSamples + swingOffset (s, stepIndex) * samplesPerBeat;
                    }
                }
                else if (m.isNoteOff())
                {
                    removeHeld (m.getNoteNumber());
                    if (held.empty())
                    {
                        releaseAll (t);
                        running = false;
                    }
                }
                else if (m.isAllNotesOff() || m.isAllSoundOff())
                {
                    held.clear();
                    order.clear();
                    releaseAll (t);
                    running = false;
                    out.addEvent (m, t);
                }
                else
                    out.addEvent (m, t);
            }

            if (samplesToOff >= 0.0 && --samplesToOff < 0.0)
                releaseAll (t);

            if (running && ! held.empty())
            {
                samplesToStep -= 1.0;
                if (samplesToStep <= 0.0)
                {
                    fireStep (t, s, stepSamples);
                    ++stepIndex;
                    // The gap to the next step: a straight step, give or take the swing of the two.
                    samplesToStep += stepSamples + (swingOffset (s, stepIndex) - swingOffset (s, stepIndex - 1)) * samplesPerBeat;
                }
            }
        }
        midi.swapWith (out);
    }

private:
    struct Held { int note; float velocity; };

    double swingOffset (const Settings& s, int64_t index) const noexcept
    {
        return (index & 1) ? (double) s.swing * 0.5 * s.stepBeats : 0.0;
    }

    void addHeld (int note, float vel)
    {
        removeHeld (note);
        held.push_back ({ note, vel });
        order.push_back (note);
    }

    void removeHeld (int note)
    {
        held.erase (std::remove_if (held.begin(), held.end(), [note] (const Held& h) { return h.note == note; }), held.end());
        order.erase (std::remove (order.begin(), order.end(), note), order.end());
    }

    void releaseAll (int t)
    {
        for (int note : sounding)
            out.addEvent (juce::MidiMessage::noteOff (1, note), t);
        sounding.clear();
        samplesToOff = -1.0;
    }

    float velocityOf (int note) const noexcept
    {
        for (auto& h : held)
            if (h.note == note)
                return h.velocity;
        return held.empty() ? 0.8f : held.back().velocity;
    }

    /** The pattern for the current held notes: (note, velocity) per position. */
    void buildPattern (const Settings& s, std::vector<Held>& dest)
    {
        dest.clear();
        auto& base = scratch;   // reserved in prepare(): no allocation on the audio thread
        base.clear();
        if (s.mode == 4)   // as played
            for (int n : order)
                base.push_back ({ n, velocityOf (n) });
        else
        {
            base.insert (base.end(), held.begin(), held.end());
            std::sort (base.begin(), base.end(), [] (const Held& a, const Held& b) { return a.note < b.note; });
        }
        const int oct = juce::jlimit (1, 4, s.octaves);
        for (int o = 0; o < oct; ++o)
            for (auto& h : base)
                if (h.note + 12 * o < 128)
                    dest.push_back ({ h.note + 12 * o, h.velocity });
        if (s.mode == 1)
            std::reverse (dest.begin(), dest.end());
        else if (s.mode == 2 && dest.size() > 2)
            for (int i = (int) dest.size() - 2; i >= 1; --i)
                dest.push_back (dest[(size_t) i]);
    }

    void fireStep (int t, const Settings& s, double stepSamples)
    {
        releaseAll (t);
        if (held.empty())
            return;
        if (s.mode == 5)
        {
            // Chord: every held note on the step.
            for (auto& h : held)
            {
                out.addEvent (juce::MidiMessage::noteOn (1, h.note, juce::jlimit (0.05f, 1.0f, h.velocity)), t);
                sounding.push_back (h.note);
            }
        }
        else
        {
            buildPattern (s, pattern);
            if (pattern.empty())
                return;
            int pos;
            if (s.mode == 3)
            {
                pos = (int) (rng.uni() * (float) pattern.size()) % (int) pattern.size();
                if (pattern.size() > 1 && pos == lastRandom)
                    pos = (pos + 1) % (int) pattern.size();
                lastRandom = pos;
            }
            else
                pos = patternPos = (patternPos + 1) % (int) pattern.size();
            const auto& h = pattern[(size_t) pos];
            out.addEvent (juce::MidiMessage::noteOn (1, h.note, juce::jlimit (0.05f, 1.0f, h.velocity)), t);
            sounding.push_back (h.note);
        }
        samplesToOff = std::max (1.0, stepSamples * (double) juce::jlimit (0.05f, 1.0f, s.gate) - 1.0);
    }

    double sr = 44100.0;
    std::vector<Held> held, pattern, scratch;
    std::vector<int> order, sounding;
    juce::MidiBuffer out;
    int64_t stepIndex = 0;
    int patternPos = -1, lastRandom = -1;
    double samplesToStep = 0.0, samplesToOff = -1.0;
    bool running = false;
    dd::Rng rng;
};
} // namespace ag
