#pragma once

#include "Voice.h"

namespace ag
{
/**
    Voice management. Poly: 12 voices, oldest released voice stolen first; with glide set, new notes
    slide in from the last note played. Mono: one voice, every note restarts the envelopes and glides
    from the previous pitch. Legato: overlapping notes slide on the same voice without retriggering.
    The sustain pedal holds notes in every mode.
*/
class Engine
{
public:
    static constexpr int kMaxVoices = 12;

    enum class Mode { Poly, Mono, Legato };

    struct Settings
    {
        Mode mode = Mode::Poly;
        float glideMs = 0.0f;
        float bendRange = 2.0f;
    };

    void prepare (double sampleRate)
    {
        for (int i = 0; i < kMaxVoices; ++i)
            voices[(size_t) i].prepare (sampleRate, 0x2545F491u + (uint32_t) i * 7919u);
        held.reserve (128);
        sustained.reserve (128);
        pending.reserve (128);
        reset();
    }

    void reset()
    {
        for (auto& v : voices) v.reset();
        held.clear();
        sustained.clear();
        sustainDown = false;
        lastPitch = -1.0f;
        bendSemis = 0.0f;
    }

    void setSettings (const Settings& s) noexcept { settings = s; }
    float getBend() const noexcept               { return bendSemis; }

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

    void render (float* L, float* R, int n, const VoiceParams& vp) noexcept
    {
        for (auto& v : voices)
            if (v.isActive())
                v.render (L, R, n, vp);
    }

    int getNumActiveVoices() const noexcept
    {
        int c = 0;
        for (auto& v : voices) c += v.isActive() ? 1 : 0;
        return c;
    }

private:
    bool mono() const noexcept { return settings.mode != Mode::Poly; }

    void noteOn (int note, float velocity, const VoiceParams& vp)
    {
        sustained.erase (std::remove (sustained.begin(), sustained.end(), note), sustained.end());
        removeHeld (note);
        const bool overlapping = ! held.empty();
        held.push_back (note);

        const bool canGlide = settings.glideMs > 0.0f && lastPitch >= 0.0f;
        const float from = canGlide ? currentPitchOr (note) : (float) note;
        lastPitch = (float) note;

        if (mono())
        {
            Voice* current = newestGateOn();
            if (settings.mode == Mode::Legato && overlapping && current != nullptr)
            {
                current->legatoTo (note);
                return;
            }
            for (auto& v : voices)
                v.choke();
            startVoice (note, velocity, from, vp);
        }
        else
        {
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
            if (std::find (held.begin(), held.end(), note) != held.end()
                && std::find (sustained.begin(), sustained.end(), note) == sustained.end())
                sustained.push_back (note);
            return;
        }

        const bool wasTop = ! held.empty() && held.back() == note;
        removeHeld (note);

        if (mono())
        {
            Voice* current = newestGateOn();
            if (current == nullptr || current->getNote() != note || ! wasTop)
                return;
            if (held.empty())
            {
                current->release();
                return;
            }
            // Fall back to the most recent note still held.
            const int back = held.back();
            lastPitch = (float) back;
            if (settings.mode == Mode::Legato)
                current->legatoTo (back);
            else
            {
                const float from = settings.glideMs > 0.0f ? current->getPitch() : (float) back;
                current->choke();
                startVoice (back, 0.8f, from, vp);
            }
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
            // Steal: a choking voice first, then the oldest released one, then the oldest held one.
            auto rank = [] (const Voice& v) { return v.isChoking() ? 0 : (v.isGateOn() ? 2 : 1); };
            for (auto& v : voices)
                if (target == nullptr || rank (v) < rank (*target)
                    || (rank (v) == rank (*target) && v.getAge() < target->getAge()))
                    target = &v;
        }
        // A stolen voice restarts from its current level (no reset), so stealing never clicks.
        target->start (note, velocity, fromPitch, vp, ++stamp);
    }

    float currentPitchOr (int note) const
    {
        const Voice* best = nullptr;
        for (auto& v : voices)
            if (v.isActive() && ! v.isChoking() && (best == nullptr || v.getAge() > best->getAge()))
                best = &v;
        if (best != nullptr)
            return best->getPitch();
        return lastPitch >= 0.0f ? lastPitch : (float) note;
    }

    Voice* newestGateOn()
    {
        Voice* best = nullptr;
        for (auto& v : voices)
            if (v.isGateOn() && (best == nullptr || v.getAge() > best->getAge()))
                best = &v;
        return best;
    }

    void removeHeld (int note) { held.erase (std::remove (held.begin(), held.end(), note), held.end()); }

    std::array<Voice, kMaxVoices> voices {};
    std::vector<int> held, sustained, pending;
    Settings settings;
    bool sustainDown = false;
    float lastPitch = -1.0f, bendSemis = 0.0f;
    uint32_t stamp = 0;
};
} // namespace ag
