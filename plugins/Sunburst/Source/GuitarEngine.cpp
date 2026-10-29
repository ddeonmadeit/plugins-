#include "GuitarEngine.h"

namespace sunburst
{
namespace
{
constexpr int kOpen[GuitarEngine::kStrings] = { 40, 45, 50, 55, 59, 64 };   // E2 A2 D3 G3 B3 E4
constexpr int kMaxFret = 22;

inline float hermite (const int16_t* d, int frames, double pos) noexcept
{
    const int i = (int) pos;
    const float f = (float) (pos - (double) i);
    auto at = [d, frames] (int k) { return k < 0 || k >= frames ? 0.0f : (float) d[k] * (1.0f / 32767.0f); };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b = w + a;
    return ((a * f - b) * f + c) * f + x0;
}

inline float coefFor (float seconds, double sr) noexcept
{
    return seconds <= 0.0f ? 0.0f : std::exp (-1.0f / (seconds * (float) sr));
}
} // namespace

void GuitarEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    reset();
}

void GuitarEngine::reset()
{
    for (auto& v : voices)
        v.active = false;
    queued = numPending = 0;
    clock = 0;
    groupStart = -(1 << 30);
    groupDown = true;
    stringNote.fill (-1);
    lastFret.fill (0);
    lastRelease.fill (-(1 << 30));
    levels.fill (0.0f);
    frets.fill (-1);
    noteString.fill (-1);
    sustained.fill (false);
    sustainDown = false;
    lastNote = -1;
    rr.clear();
    random.setSeed (0x5b5b);   // the same performance renders the same way (offline bounces, QA)
}

GuitarEngine::Voice* GuitarEngine::allocate()
{
    Voice* best = nullptr;
    for (auto& v : voices)
        if (! v.active)
            return &v;
    // Steal the quietest releasing voice, else the oldest.
    for (auto& v : voices)
        if (! v.gate && (best == nullptr || v.env < best->env))
            best = &v;
    if (best == nullptr)
        for (auto& v : voices)
            if (best == nullptr || v.age < best->age)
                best = &v;
    return best;
}

int GuitarEngine::chooseString (int note, int avoidHeld) const
{
    int best = -1;
    float bestCost = 1.0e9f;
    for (int s = 0; s < kStrings; ++s)
    {
        int fret = note - kOpen[s];
        float cost = 0.0f;
        if (fret < 0)
        {
            if (s != 0 || fret < -5) continue;      // the low string can be tuned down a few steps
            cost += 6.0f - (float) fret;
            fret = 0;
        }
        if (fret > kMaxFret) continue;
        cost += (float) fret + (fret > 12 ? (float) (fret - 12) * 1.5f : 0.0f);
        if (stringNote[(size_t) s] >= 0) cost += avoidHeld ? 1000.0f : 30.0f;
        for (auto& v : voices)
            if (v.active && v.kind == Kind::Note && v.string == s && ! v.gate)
            {
                cost += 2.0f;   // still ringing out: fine to stop, but a free string is nicer
                break;
            }
        if (cost < bestCost) { bestCost = cost; best = s; }
    }
    return best < 0 ? (note < kOpen[0] ? 0 : kStrings - 1) : best;
}

void GuitarEngine::stopString (int track, int string, float fadeMs)
{
    const float c = coefFor (fadeMs * 0.001f / 4.6f, sr);
    for (auto& v : voices)
        if (v.active && v.kind == Kind::Note && v.track == track && v.string == string)
        {
            v.stopping = true;
            v.gate = false;
            v.releaseCoef = std::min (v.releaseCoef, c);
        }
}

const Sample* GuitarEngine::pickTake (const std::vector<Layer>& layers, float velocity, const PlayParams& p, float& gainDb, int key)
{
    if (layers.empty())
        return nullptr;
    const float curve = std::exp2 (p.velCurve);
    const float v = std::pow (juce::jlimit (0.02f, 1.0f, velocity), curve);
    const float target = layers.back().loudnessDb + 40.0f * std::log10 (v);
    size_t li = 0;
    for (size_t i = 1; i < layers.size(); ++i)
        if (std::abs (layers[i].loudnessDb - target) < std::abs (layers[li].loudnessDb - target))
            li = i;
    const auto& takes = layers[li].takes;
    if (takes.empty())
        return nullptr;
    auto& counter = rr[key * 8 + (int) li];
    counter = (counter + 1 + (takes.size() > 2 ? random.nextInt (2) : 0)) % (int) takes.size();
    const auto& s = takes[(size_t) counter];
    gainDb = juce::jlimit (-30.0f, 4.0f, target - s.loudnessDb);
    return &s;
}

//==============================================================================
void GuitarEngine::noteOn (int note, float velocity, const InstrumentData* g, const PlayParams& p)
{
    if (g == nullptr)
        return;
    sustained[(size_t) note] = false;

    // Humanized timing and strums go through the queue; everything else starts right away.
    const int64_t jitter = p.humanize > 0.0f ? (int64_t) (random.nextFloat() * p.humanize * 0.008 * sr) : 0;
    if (p.strumMs <= 0.0f)
    {
        if (jitter <= 0 || queued == (int) queue.size())
            startNote (note, velocity, g, p);
        else
            queue[(size_t) queued++] = { note, velocity, clock + jitter };
        return;
    }

    if (clock - groupStart > (int64_t) (0.03 * sr))
    {
        // A new chord: whatever is still waiting from the last one plays now.
        while (queued > 0)
        {
            const auto q = queue[0];
            for (int k = 0; k < queued - 1; ++k) queue[(size_t) k] = queue[(size_t) k + 1];
            --queued;
            startNote (q.note, q.velocity, g, p);
        }
        // Strokes alternate down / up on 8th-note strumming; a pause resets to a down stroke.
        groupDown = clock - groupStart < (int64_t) (0.47 * sr) ? ! groupDown : true;
        groupStart = clock;
        groupStarted = 0;
    }
    for (int i = 0; i < queued; ++i)
        if (queue[(size_t) i].note == note)
        {
            for (int k = i; k < queued - 1; ++k) queue[(size_t) k] = queue[(size_t) k + 1];
            --queued;
            --i;
        }
    if (queued == (int) queue.size())
    {
        startNote (note, velocity, g, p);
        return;
    }
    queue[(size_t) queued++] = { note, velocity * (groupDown ? 1.0f : 0.85f), 0 };

    // Re-space the chord's waiting strings in stroke order (up strokes are quicker and lighter).
    const bool down = groupDown;
    std::sort (queue.begin(), queue.begin() + queued,
               [down] (const Queued& a, const Queued& b) { return down ? a.note < b.note : a.note > b.note; });
    const double step = p.strumMs * 0.001 * sr * (down ? 1.0 : 0.7);
    for (int i = 0; i < queued; ++i)
        queue[(size_t) i].startAt = groupStart + (int64_t) ((groupStarted + i) * step)
                                  + (int64_t) (random.nextFloat() * p.humanize * 0.004 * sr);
}

void GuitarEngine::startNote (int rawNote, float velocity, const InstrumentData* g, const PlayParams& p)
{
    if (g == nullptr || g->sustain.empty())
        return;
    const int note = juce::jlimit (0, 127, rawNote + p.transpose);
    ++groupStarted;

    // Legato: the previous note is still held, close by, and playable on its string.
    int string = -1;
    bool legato = false;
    if (p.legato > 0 && lastNote >= 0 && noteString[(size_t) lastNote] >= 0 && std::abs (note - lastNote) <= 5)
    {
        const int s = noteString[(size_t) lastNote];
        const int fret = note - kOpen[s];
        if (fret >= 0 && fret <= kMaxFret)
        {
            string = s;
            legato = true;
        }
    }
    if (string < 0)
        string = chooseString (note, 1);

    const int fret = std::max (0, note - kOpen[string]);
    const float noise = p.stringNoise;
    if (! legato && noise > 0.001f)
    {
        // Moving the fretting hand along a wound string squeaks.
        const int64_t sinceRelease = clock - lastRelease[(size_t) string];
        if (string <= 3 && sinceRelease < (int64_t) (0.25 * sr) && std::abs (fret - lastFret[(size_t) string]) >= 3
            && random.nextFloat() < 0.6f)
            playSqueak (noise * (0.25f + 0.25f * random.nextFloat()), string);
        // The pick brushing the strings before it lands.
        if (g->family == "electric" && random.nextFloat() < 0.3f && ! g->noises[(size_t) NoiseKind::Pick].empty())
        {
            const auto& pool = g->noises[(size_t) NoiseKind::Pick];
            playNoise (&pool[(size_t) random.nextInt ((int) pool.size())].sample, noise * 0.3f * velocity);
        }
    }

    if (legato && p.legato == 2 && noise > 0.001f)
        playScrape (noise * 0.5f * velocity, (float) lastNote, (float) note, p.slideMs * 0.001f);

    // Whatever this string was holding stops (or, sliding, becomes the new note).
    if (stringNote[(size_t) string] >= 0 && stringNote[(size_t) string] != note)
        noteString[(size_t) stringNote[(size_t) string]] = -1;
    stringNote[(size_t) string] = note;
    noteString[(size_t) note] = string;
    lastFret[(size_t) string] = fret;
    frets[(size_t) string] = fret;
    lastNote = note;
    lastNoteTime = clock;

    startTake (0, string, note, velocity, g, p, legato, 0);
    if (p.doubleTrack > 0.001f && numPending < (int) pending.size())
        pending[(size_t) numPending++] = { 1, string, note, velocity * (0.9f + 0.1f * random.nextFloat()), legato,
                                           clock + (int64_t) ((0.012 + 0.016 * random.nextFloat()) * sr) };
}

void GuitarEngine::startTake (int track, int string, int note, float velocity, const InstrumentData* g,
                              const PlayParams& p, bool legato, int delaySamples)
{
    juce::ignoreUnused (delaySamples);
    if (legato && p.legato == 2)
    {
        // Slide: the ringing voice glides to the new pitch.
        for (auto& v : voices)
            if (v.active && v.kind == Kind::Note && v.track == track && v.string == string && ! v.stopping)
            {
                v.targetPitch += (float) (note - v.note);
                v.glideCoef = coefFor (p.slideMs * 0.001f / 3.0f, sr);
                v.note = note;
                v.gate = true;
                v.releasing = false;
                v.releaseCoef = 1.0f;
                return;
            }
    }
    stopString (track, string, legato ? 6.0f : 12.0f);

    const NoteMap& map = legato && ! g->hammer.empty() ? g->hammer : g->sustain;
    const auto* layers = map.find (note);
    if (layers == nullptr)
        return;
    float gainDb = 0.0f;
    const Sample* s = pickTake (*layers, legato ? velocity * 0.8f : velocity, p, gainDb, note + track * 128);
    if (s == nullptr)
        return;

    auto* v = allocate();
    *v = Voice();
    v->active = v->gate = true;
    v->kind = Kind::Note;
    v->track = track;
    v->string = string;
    v->note = note;
    v->sample = s;
    v->age = ++ageCounter;

    const float base = (float) map.sampledNote (note) + s->cents * 0.01f;
    v->detune = (random.nextFloat() * 2.0f - 1.0f) * p.humanize * 0.04f + (track == 1 ? (random.nextBool() ? 0.05f : -0.05f) : 0.0f);
    v->pitch = v->targetPitch = (float) note - base;
    v->gain = dd::dbToGain (gainDb);
    if (g->sustain.notes.begin()->second.size() == 1)   // one recorded layer: soft notes also get darker
        v->lpHz = 2500.0f + 17500.0f * std::pow (juce::jlimit (0.0f, 1.0f, velocity), 1.5f);

    // Pick: soft playing skips into the note and eases the attack; hard picking adds a little snap.
    const float soft = juce::jlimit (0.0f, 1.0f, (0.5f - p.pick) * 2.0f);
    const float hard = juce::jlimit (0.0f, 1.0f, (p.pick - 0.5f) * 2.0f);
    const double srcRatio = g->sampleRate / sr;
    if (legato)
    {
        // Hammer-on / pull-off without a recorded one: start past the pick and fade in.
        if (g->hammer.empty())
            v->pos = 0.022 * g->sampleRate;
        v->gain *= dd::dbToGain (-2.5f);
        v->fadeIn = 0.0f;
        v->fadeInc = 1.0f / (float) (0.004 * sr);
    }
    else
    {
        v->pos = soft * 0.006 * g->sampleRate;
        v->fadeIn = soft > 0.0f ? 0.0f : 1.0f;
        v->fadeInc = 1.0f / (float) ((0.0015 + soft * 0.012) * sr);
        v->boost = hard * 0.6f;
        v->boostCoef = coefFor (0.012f, sr);
    }
    juce::ignoreUnused (srcRatio);
    v->lp1.reset();
    v->lp2.reset();
}

void GuitarEngine::releaseVoice (Voice& v, const InstrumentData* g, const PlayParams& p, bool damp)
{
    if (! v.gate)
        return;
    v.gate = false;
    v.releasing = true;
    const float seconds = 0.07f + 2.9f * std::pow (p.ring, 1.6f);
    v.releaseCoef = coefFor (seconds / 6.9f, sr);

    // Lifting the fretting finger damps the string with a little noise of its own.
    if (damp && v.track == 0 && g != nullptr && ! g->release.empty() && p.stringNoise > 0.001f)
        if (const auto* layers = g->release.find (v.note))
        {
            float gainDb = 0.0f;
            if (const auto* s = pickTake (*layers, 0.8f, p, gainDb, 1000 + v.note))
                playNoise (s, p.stringNoise * (1.0f - 0.7f * p.ring) * std::min (1.0f, v.gain * 1.5f) * 0.8f);
        }
}

void GuitarEngine::noteOff (int rawNote, const InstrumentData* g, const PlayParams& p)
{
    // A string still waiting in a strum sounds before it is let go.
    for (int i = 0; i < queued; ++i)
        if (queue[(size_t) i].note == rawNote)
        {
            const auto q = queue[(size_t) i];
            for (int k = i; k < queued - 1; ++k) queue[(size_t) k] = queue[(size_t) k + 1];
            --queued;
            startNote (q.note, q.velocity, g, p);
            break;
        }

    const int note = juce::jlimit (0, 127, rawNote + p.transpose);
    if (sustainDown)
    {
        sustained[(size_t) note] = true;
        return;
    }
    for (auto& v : voices)
        if (v.active && v.kind == Kind::Note && v.note == note && v.gate)
            releaseVoice (v, g, p, true);
    for (int i = 0; i < numPending; ++i)
        if (pending[(size_t) i].note == note)
            pending[(size_t) i].velocity = -1.0f;   // the double never starts
    if (const int s = noteString[(size_t) note]; s >= 0)
    {
        stringNote[(size_t) s] = -1;
        lastRelease[(size_t) s] = clock;
        noteString[(size_t) note] = -1;
    }
}

void GuitarEngine::setSustain (bool down, const InstrumentData* g, const PlayParams& p)
{
    sustainDown = down;
    if (down)
        return;
    for (int n = 0; n < 128; ++n)
        if (sustained[(size_t) n])
        {
            sustained[(size_t) n] = false;
            noteOff (n - p.transpose, g, p);
        }
}

void GuitarEngine::allNotesOff()
{
    for (auto& v : voices)
        if (v.active)
        {
            v.gate = false;
            v.stopping = true;
            v.releaseCoef = std::min (v.releaseCoef, coefFor (0.004f, sr));
        }
    queued = numPending = 0;
    stringNote.fill (-1);
    noteString.fill (-1);
    sustained.fill (false);
    lastNote = -1;
}

void GuitarEngine::fxKey (int key, float velocity, const InstrumentData* g)
{
    if (g == nullptr)
        return;
    const float gain = std::pow (velocity, 1.5f) * 1.2f;
    auto play = [&] (NoiseKind k)
    {
        const auto& pool = g->noises[(size_t) k];
        if (pool.empty())
            return false;
        playNoise (&pool[(size_t) random.nextInt ((int) pool.size())].sample, gain);
        return true;
    };
    switch (key - kFxLow)
    {
        case 0: if (! play (NoiseKind::Strum)) play (NoiseKind::Mute); break;
        case 1: if (! play (NoiseKind::Knock)) play (NoiseKind::Strum); break;
        case 2: play (NoiseKind::Mute); break;
        case 3: if (! play (NoiseKind::Squeak)) playSqueak (gain, random.nextInt (4)); break;
        case 4: if (! play (NoiseKind::Pick)) playSqueak (gain * 0.5f, 5); break;
        default: break;
    }
}

void GuitarEngine::playNoise (const Sample* s, float gain, int delaySamples)
{
    juce::ignoreUnused (delaySamples);
    if (s == nullptr || gain <= 0.0001f)
        return;
    auto* v = allocate();
    *v = Voice();
    v->active = true;
    v->kind = Kind::Noise;
    v->sample = s;
    v->gain = gain;
    v->age = ++ageCounter;
}

void GuitarEngine::playSqueak (float gain, int string)
{
    if (gain <= 0.0001f)
        return;
    auto* v = allocate();
    *v = Voice();
    v->active = true;
    v->kind = Kind::Squeak;
    v->gain = gain * 0.12f;
    v->string = string;
    v->age = ++ageCounter;
    // A short whistle that sweeps as the finger travels along the winding.
    const bool up = random.nextBool();
    v->bandHz = (up ? 1300.0f : 2600.0f) * (1.0f + 0.15f * (float) (3 - std::min (3, string)));
    v->bandTarget = up ? v->bandHz * 1.9f : v->bandHz * 0.55f;
    v->length = (int) ((0.06 + 0.07 * random.nextFloat()) * sr);
    v->bandCoef = coefFor ((float) v->length / (float) sr / 3.0f, sr);
}

void GuitarEngine::playScrape (float gain, float fromNote, float toNote, float seconds)
{
    if (gain <= 0.0001f)
        return;
    auto* v = allocate();
    *v = Voice();
    v->active = true;
    v->kind = Kind::Scrape;
    v->gain = gain * 0.08f;
    v->age = ++ageCounter;
    // Rubbing over the windings: brighter the further and faster the slide goes.
    const float span = std::abs (toNote - fromNote);
    v->bandHz = 900.0f + 120.0f * span;
    v->bandTarget = v->bandHz * (toNote > fromNote ? 1.6f : 0.65f);
    v->length = (int) (std::max (0.04f, seconds) * 1.2f * (float) sr);
    v->bandCoef = coefFor (std::max (0.04f, seconds) / 2.0f, sr);
}

//==============================================================================
void GuitarEngine::renderVoice (Voice& v, float* out, int n, double srcRate, const PlayParams& p) noexcept
{
    if (v.kind == Kind::Squeak || v.kind == Kind::Scrape)
    {
        for (int i = 0; i < n && v.active; ++i)
        {
            v.bandHz = v.bandTarget + (v.bandHz - v.bandTarget) * v.bandCoef;
            if ((v.elapsed & 15) == 0)
                v.band.set (v.bandHz, v.kind == Kind::Squeak ? 6.0f : 1.8f, sr);
            const float t = (float) v.elapsed / (float) std::max (1, v.length);
            const float env = v.kind == Kind::Squeak ? std::min (1.0f, t * 8.0f) * (1.0f - t) * (1.0f - t)
                                                     : std::sin (juce::MathConstants<float>::pi * std::min (1.0f, t));
            float lo, bp, hi;
            v.band.process (rng.bi(), lo, bp, hi);
            out[i] += bp * env * v.gain;
            if (++v.elapsed >= v.length)
                v.active = false;
        }
        return;
    }

    const Sample& s = *v.sample;
    const double baseRatio = srcRate / sr;
    if (v.kind == Kind::Noise)
    {
        for (int i = 0; i < n; ++i)
        {
            if (v.pos >= (double) (s.frames - 2)) { v.active = false; return; }
            out[i] += hermite (s.pcm.data(), s.frames, v.pos) * v.gain;
            v.pos += baseRatio;
        }
        return;
    }

    // Palm mute: the note dies fast and loses its top; the mod wheel can lean on ringing notes too.
    const float mute = v.gate ? p.mute : p.mute * 0.5f;
    const float muteHz = mute > 0.001f ? 18000.0f * std::pow (0.05f, std::pow (mute, 0.8f)) : 20000.0f;
    const float muteDecay = mute > 0.001f ? coefFor (0.12f + 3.0f * std::pow (1.0f - mute, 3.0f), sr) : 1.0f;
    const float cutoff = std::min (v.lpHz, muteHz);
    const bool filter = cutoff < 19000.0f;
    if (filter && std::abs (cutoff - v.curHz) > 1.0f)
    {
        v.curHz = cutoff;
        v.lp1.setCutoff (cutoff, sr);
        v.lp2.setCutoff (std::min (cutoff * 1.4f, 20000.0f), sr);
    }

    const float vibDepth = p.vibrato * 0.35f;
    float ratio = (float) baseRatio * std::exp2 ((v.pitch + p.bend + v.detune) / 12.0f);
    for (int i = 0; i < n; ++i)
    {
        if ((i & 7) == 0)
        {
            if (v.glideCoef > 0.0f)
                for (int k = 0; k < 8; ++k)
                    v.pitch = v.targetPitch + (v.pitch - v.targetPitch) * v.glideCoef;
            float vib = 0.0f;
            if (vibDepth > 0.0f && v.gate)
            {
                const float held = (float) v.heldSamples / (float) sr;
                const float ramp = juce::jlimit (0.0f, 1.0f, (held - 0.35f) / 0.4f);
                vib = std::sin (v.vibPhase) * vibDepth * ramp;
                v.vibPhase += juce::MathConstants<float>::twoPi * 5.3f * 8.0f / (float) sr;
                if (v.vibPhase > juce::MathConstants<float>::twoPi) v.vibPhase -= juce::MathConstants<float>::twoPi;
            }
            ratio = (float) baseRatio * std::exp2 ((v.pitch + p.bend + v.detune + vib) / 12.0f);
        }
        if (v.pos >= (double) (s.frames - 2))
        {
            v.active = false;
            break;
        }
        float x = hermite (s.pcm.data(), s.frames, v.pos);
        v.pos += ratio;
        if (v.boost > 0.0005f)
        {
            x *= 1.0f + v.boost;
            v.boost *= v.boostCoef;
        }
        if (filter)
            x = v.lp2.lp (v.lp1.lp (x));
        if (v.fadeIn < 1.0f)
            v.fadeIn = std::min (1.0f, v.fadeIn + v.fadeInc);
        if (v.releasing || v.stopping)
            v.env *= v.releaseCoef;
        if (mute > 0.001f && v.elapsed > (int) (0.008 * sr))
            v.env *= muteDecay;
        out[i] += x * v.gain * v.env * v.fadeIn;
        ++v.elapsed;
        if (v.gate)
            ++v.heldSamples;
        if (v.env < 1.0e-4f)
        {
            v.active = false;
            break;
        }
    }
}

void GuitarEngine::render (float* main, float* dbl, int n, const InstrumentData* g, const PlayParams& p) noexcept
{
    int done = 0;
    while (done < n)
    {
        // Next event (a strummed / humanized note or a double take) inside this block.
        int64_t next = clock + (n - done);
        for (int i = 0; i < queued; ++i)
            next = std::min (next, std::max (clock, queue[(size_t) i].startAt));
        for (int i = 0; i < numPending; ++i)
            next = std::min (next, std::max (clock, pending[(size_t) i].at));
        const int seg = (int) (next - clock);

        if (seg > 0)
        {
            const double srcRate = g != nullptr ? g->sampleRate : 44100.0;
            for (auto& v : voices)
                if (v.active)
                    renderVoice (v, v.track == 1 ? dbl + done : main + done, seg, srcRate, p);
            clock += seg;
            done += seg;
        }

        for (int i = 0; i < queued; ++i)
            if (queue[(size_t) i].startAt <= clock)
            {
                const auto q = queue[(size_t) i];
                for (int k = i; k < queued - 1; ++k) queue[(size_t) k] = queue[(size_t) k + 1];
                --queued;
                --i;
                startNote (q.note, q.velocity, g, p);
            }
        for (int i = 0; i < numPending; ++i)
            if (pending[(size_t) i].at <= clock)
            {
                const auto q = pending[(size_t) i];
                pending[(size_t) i] = pending[(size_t) --numPending];
                --i;
                if (q.velocity > 0.0f && g != nullptr && stringNote[(size_t) q.string] == q.note)
                    startTake (q.track, q.string, q.note, q.velocity, g, p, q.legato, 0);
            }
    }

    // String activity for the UI (track 0 notes).
    std::array<float, kStrings> lv {};
    for (auto& v : voices)
        if (v.active && v.kind == Kind::Note && v.track == 0 && v.string >= 0)
            lv[(size_t) v.string] = std::max (lv[(size_t) v.string], v.env * std::min (1.0f, v.gain) * (v.gate ? 1.0f : 0.6f));
    for (int s = 0; s < kStrings; ++s)
    {
        levels[(size_t) s] = std::max (lv[(size_t) s], levels[(size_t) s] * 0.9f);
        if (lv[(size_t) s] <= 0.0f && stringNote[(size_t) s] < 0)
            frets[(size_t) s] = -1;
    }
}
} // namespace sunburst
