#pragma once

#include "Voice.h"

namespace ag
{
/**
    Voice management. Poly: 12 voices, oldest released voice stolen first; with glide set, new notes
    slide in from the last note played. Mono: one voice, every note restarts the envelopes and glides
    from the previous pitch. Legato: overlapping notes slide on the same voice without retriggering.
    The sustain pedal holds notes in every mode.

    Strum (poly only): notes that arrive together start one after another, lowest first, like a pick
    crossing the strings; a chord that follows quickly is an up-stroke (highest first, a little faster).
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
        float strumMs = 0.0f;
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
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
        queued = 0;
        clock = 0;
        groupStart = -(1 << 30);
        groupDown = true;
        groupStarted = 0;
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
            queued = 0;
            for (auto& v : voices)
                if (m.isAllSoundOff()) v.choke(); else if (v.isGateOn()) v.release();
        }
    }

    void render (float* L, float* R, int n, const VoiceParams& vp) noexcept
    {
        int done = 0;
        while (queued > 0)
        {
            int first = 0;
            for (int i = 1; i < queued; ++i)
                if (queue[(size_t) i].startAt < queue[(size_t) first].startAt)
                    first = i;
            const int64_t offset = queue[(size_t) first].startAt - clock;
            if (offset >= n)
                break;
            const int at = (int) std::max<int64_t> (done, offset);
            renderVoices (L + done, R + done, at - done, vp);
            done = at;
            const auto q = queue[(size_t) first];
            removeQueued (first);
            ++groupStarted;
            startVoice (q.note, q.velocity, q.from, vp);
        }
        renderVoices (L + done, R + done, n - done, vp);
        clock += n;
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

        // Chords never glide in poly (every note would swoop in from the last one); single lines do.
        const bool canGlide = settings.glideMs > 0.0f && lastPitch >= 0.0f && (mono() || ! overlapping);
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
            if (settings.strumMs > 0.0f)
                strum (note, velocity, from, vp);
            else
                startVoice (note, velocity, from, vp);
        }
    }

    void strum (int note, float velocity, float from, const VoiceParams& vp)
    {
        if (clock - groupStart > (int64_t) (0.03 * sr))
        {
            // A new chord: whatever is still waiting from the last one plays now.
            while (queued > 0)
            {
                const auto q = queue[0];
                removeQueued (0);
                startVoice (q.note, q.velocity, q.from, vp);
            }
            // Strokes alternate down / up on 8th-note strumming (down to ~65 BPM); a pause resets to down.
            groupDown = clock - groupStart < (int64_t) (0.47 * sr) ? ! groupDown : true;
            groupStart = clock;
            groupStarted = 0;
        }
        for (int i = 0; i < queued; ++i)
            if (queue[(size_t) i].note == note)
                removeQueued (i--);
        if (queued == (int) queue.size())
        {
            startVoice (note, velocity, from, vp);
            return;
        }
        queue[(size_t) queued++] = { note, velocity, from, 0 };

        // Re-space the chord's waiting strings in stroke order.
        const bool down = groupDown;
        std::sort (queue.begin(), queue.begin() + queued,
                   [down] (const Queued& a, const Queued& b) { return down ? a.note < b.note : a.note > b.note; });
        const double step = settings.strumMs * 0.001 * sr * (down ? 1.0 : 0.7);
        for (int i = 0; i < queued; ++i)
            queue[(size_t) i].startAt = groupStart + (int64_t) ((groupStarted + i) * step);
    }

    void renderVoices (float* L, float* R, int n, const VoiceParams& vp) noexcept
    {
        if (n <= 0)
            return;
        for (auto& v : voices)
            if (v.isActive())
                v.render (L, R, n, vp);
    }

    void removeQueued (int i) noexcept
    {
        for (int k = i; k < queued - 1; ++k)
            queue[(size_t) k] = queue[(size_t) k + 1];
        --queued;
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
            // A string still waiting in a strum sounds before it is let go.
            for (int i = 0; i < queued; ++i)
                if (queue[(size_t) i].note == note)
                {
                    const auto q = queue[(size_t) i];
                    removeQueued (i);
                    ++groupStarted;
                    startVoice (q.note, q.velocity, q.from, vp);
                    break;
                }
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

    struct Queued { int note; float velocity, from; int64_t startAt; };

    std::array<Voice, kMaxVoices> voices {};
    std::array<Queued, 32> queue {};
    int queued = 0, groupStarted = 0;
    int64_t clock = 0, groupStart = -(1 << 30);
    bool groupDown = true;
    double sr = 44100.0;
    std::vector<int> held, sustained, pending;
    Settings settings;
    bool sustainDown = false;
    float lastPitch = -1.0f, bendSemis = 0.0f;
    uint32_t stamp = 0;
};
} // namespace ag
