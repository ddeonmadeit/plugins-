#pragma once

#include "Voice.h"

namespace sg
{
/**
    Voice management. Mono mode behaves like an 808 sampler with choke: a new note cuts the old one
    (4 ms fade) unless notes overlap with Legato on, in which case the same voice slides to the new
    pitch. Releasing a note while others are held returns to the most recent held note.
*/
class SynthEngine
{
public:
    static constexpr int kMaxVoices = 8;

    struct Settings
    {
        bool mono = true;
        bool legato = true;
        bool glideAlways = false;
        float glideMs = 0.0f;
        float bendRange = 2.0f;
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (int i = 0; i < kMaxVoices; ++i)
            voices[(size_t) i].prepare (sr, 0x1234567u + (uint32_t) i * 7919u);
        held.reserve (128);
        sustained.reserve (128);
        pending.reserve (128);
        reset();
    }

    void reset()
    {
        for (auto& v : voices) v.reset();
        held.clear();
        sustainDown = false;
        sustained.clear();
        lastPitch = -1.0f;
        bendSemis = 0.0f;
    }

    void setSettings (const Settings& s) noexcept { settings = s; }
    float getBend() const noexcept               { return bendSemis; }

    /** True once after a note started a fresh (non-legato) voice; used to retrigger the LFO. */
    bool takeFreshNoteFlag() noexcept { const bool f = freshNote; freshNote = false; return f; }

    void handleMidi (const juce::MidiMessage& m, const VoiceParams& vp)
    {
        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getFloatVelocity(), vp);
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber(), vp);
        else if (m.isPitchWheel())
            bendSemis = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * settings.bendRange;
        else if (m.isSustainPedalOn())
            sustainDown = true;
        else if (m.isSustainPedalOff())
        {
            sustainDown = false;
            pending.assign (sustained.begin(), sustained.end());
            sustained.clear();
            for (int note : pending)
                noteOff (note, vp);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            held.clear();
            sustained.clear();
            for (auto& v : voices)
                if (m.isAllSoundOff()) v.choke(); else if (v.isGateOn()) v.release();
        }
    }

    void render (float* L, float* R, int n, const VoiceParams& vp, const float* lfo) noexcept
    {
        for (auto& v : voices)
            if (v.isActive())
                v.render (L, R, n, vp, lfo);

        if (auto* v = newestSounding())
            lastPitch = v->getPitch();
    }

    int getNumActiveVoices() const noexcept
    {
        int c = 0;
        for (auto& v : voices) c += v.isActive() ? 1 : 0;
        return c;
    }

private:
    void noteOn (int note, float velocity, const VoiceParams& vp)
    {
        sustained.erase (std::remove (sustained.begin(), sustained.end(), note), sustained.end());
        removeHeld (note);
        const bool overlapping = ! held.empty();
        held.push_back (note);

        const bool canGlide = settings.glideMs > 0.0f && lastPitch >= 0.0f
                              && (overlapping || settings.glideAlways);

        if (settings.mono)
        {
            Voice* current = newestGateOn();
            if (overlapping && settings.legato && current != nullptr)
            {
                current->legatoTo (note, canGlide);
                return;
            }

            const float from = canGlide ? currentPitchOr (note) : (float) note;
            for (auto& v : voices)
                v.choke();
            startVoice (note, velocity, from, vp);
        }
        else
        {
            // Poly: chords never glide (every note would swoop in from the last one); single lines
            // still slide when the preset glides on every note.
            const bool polyGlide = canGlide && ! overlapping && settings.glideAlways;
            const float from = polyGlide ? currentPitchOr (note) : (float) note;
            for (auto& v : voices)
                if (v.isGateOn() && v.getNote() == note)
                    v.release();
            startVoice (note, velocity, from, vp);
        }
    }

    void noteOff (int note, const VoiceParams& vp)
    {
        if (sustainDown)
        {
            // Keep the note "held" until the pedal comes up.
            if (std::find (held.begin(), held.end(), note) != held.end()
                && std::find (sustained.begin(), sustained.end(), note) == sustained.end())
                sustained.push_back (note);
            return;
        }

        const bool wasTop = ! held.empty() && held.back() == note;
        removeHeld (note);

        if (settings.mono)
        {
            Voice* current = newestGateOn();
            if (current == nullptr || current->getNote() != note || ! wasTop)
                return;

            if (! held.empty())
            {
                const int back = held.back();
                if (settings.legato)
                    current->legatoTo (back, settings.glideMs > 0.0f);
                else
                {
                    const float from = settings.glideMs > 0.0f ? current->getPitch() : (float) back;
                    current->choke();
                    startVoice (back, 0.8f, from, vp);
                }
            }
            else
                current->release();
        }
        else
        {
            for (auto& v : voices)
                if (v.isGateOn() && v.getNote() == note)
                    v.release();
        }
    }

    void startVoice (int note, float velocity, float fromPitch, const VoiceParams& vp)
    {
        Voice* target = nullptr;
        for (auto& v : voices)
            if (! v.isActive()) { target = &v; break; }

        if (target == nullptr)
        {
            // Steal: a choking voice first, then a released one, then the oldest held one.
            auto rank = [] (const Voice& v) { return v.isChoking() ? 0 : (v.isGateOn() ? 2 : 1); };
            for (auto& v : voices)
                if (target == nullptr || rank (v) < rank (*target)
                    || (rank (v) == rank (*target) && v.getAge() < target->getAge()))
                    target = &v;
        }

        target->start (note, velocity, fromPitch, vp, ++stamp);
        freshNote = true;
    }

    float currentPitchOr (int note) const
    {
        if (auto* v = newestSounding())
            return v->getPitch();
        return lastPitch >= 0.0f ? lastPitch : (float) note;
    }

    Voice* newestGateOn()
    {
        Voice* best = nullptr;
        for (auto& v : voices)
            if (v.isGateOn() && ! v.isChoking() && (best == nullptr || v.getAge() > best->getAge()))
                best = &v;
        return best;
    }

    const Voice* newestSounding() const
    {
        const Voice* best = nullptr;
        for (auto& v : voices)
            if (v.isActive() && ! v.isChoking() && (best == nullptr || v.getAge() > best->getAge()))
                best = &v;
        return best;
    }

    void removeHeld (int note) { held.erase (std::remove (held.begin(), held.end(), note), held.end()); }

    double sr = 44100.0;
    std::array<Voice, kMaxVoices> voices;
    std::vector<int> held, sustained, pending;
    Settings settings;
    bool sustainDown = false, freshNote = false;
    float lastPitch = -1.0f, bendSemis = 0.0f;
    uint64_t stamp = 0;
};
} // namespace sg
