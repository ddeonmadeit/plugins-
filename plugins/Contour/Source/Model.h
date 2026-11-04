#pragma once

#include <dd_take/dd_take.h>

/*
    Contour's data: the notes found in a captured take, the user's edits on each, and the analysis they
    come from (pitch track + glottal pulse marks per captured span). Times are timeline samples at the
    current processing rate; saved state stores seconds.
*/
namespace contour
{
inline const juce::StringArray& keyNames()
{
    static const juce::StringArray n { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n;
}

inline const juce::StringArray& scaleNames()
{
    static const juce::StringArray n { "Chromatic", "Major", "Minor", "Harmonic Minor", "Major Pentatonic",
                                       "Minor Pentatonic", "Blues", "Dorian", "Mixolydian" };
    return n;
}

/** Bit i set = the note i semitones above the key is in the scale. */
inline uint16_t scaleMask (int scale) noexcept
{
    auto bits = [] (std::initializer_list<int> steps)
    {
        uint16_t m = 0;
        for (int s : steps) m = (uint16_t) (m | (1u << s));
        return m;
    };
    switch (scale)
    {
        case 1:  return bits ({ 0, 2, 4, 5, 7, 9, 11 });
        case 2:  return bits ({ 0, 2, 3, 5, 7, 8, 10 });
        case 3:  return bits ({ 0, 2, 3, 5, 7, 8, 11 });
        case 4:  return bits ({ 0, 2, 4, 7, 9 });
        case 5:  return bits ({ 0, 3, 5, 7, 10 });
        case 6:  return bits ({ 0, 3, 5, 6, 7, 10 });
        case 7:  return bits ({ 0, 2, 3, 5, 7, 9, 10 });
        case 8:  return bits ({ 0, 2, 4, 5, 7, 9, 10 });
        default: return 0x0fff;
    }
}

inline bool inScale (int note, int key, uint16_t mask) noexcept
{
    return (mask >> (((note - key) % 12 + 12) % 12)) & 1u;
}

/** The scale note nearest to a pitch. */
inline float snapToScale (float midi, int key, int scale) noexcept
{
    const auto mask = scaleMask (scale);
    const int r = (int) std::lround (midi);
    float best = (float) r;
    float bestD = 1.0e9f;
    for (int d = -6; d <= 6; ++d)
        if (inScale (r + d, key, mask) && std::abs ((float) (r + d) - midi) < bestD)
        {
            bestD = std::abs ((float) (r + d) - midi);
            best = (float) (r + d);
        }
    return best;
}

inline juce::String noteName (int midi)
{
    return keyNames()[((midi % 12) + 12) % 12] + juce::String (midi / 12 - 1);
}

/** One sung note and what the user did to it. Neutral edits leave the voice exactly as sung. */
struct Note
{
    uint32_t id = 0;
    int64_t start = 0, end = 0;       // as sung
    float centre = 60.0f;             // perceived pitch, MIDI (measured)

    bool moved = false;               // pitch set by hand: the global Pitch Center correction leaves it alone
    float pitch = 0.0f;               // the hand-set offset of the centre, semitones
    float drift = 1.0f, mod = 1.0f;   // slow pitch drift / vibrato kept (1 = as sung, 0 = flat, 2 = doubled)
    float formant = 0.0f;             // semitones
    float gainDb = 0.0f;
    bool muted = false;
    int64_t newStart = 0, newEnd = 0; // where it plays (time edits)

    bool timeEdited() const noexcept { return newStart != start || newEnd != end; }
    bool edited() const noexcept
    {
        return moved || ! juce::exactlyEqual (drift, 1.0f) || ! juce::exactlyEqual (mod, 1.0f)
            || ! juce::exactlyEqual (formant, 0.0f) || ! juce::exactlyEqual (gainDb, 0.0f) || muted || timeEdited();
    }
    void resetEdits() noexcept
    {
        moved = false; pitch = 0.0f; drift = 1.0f; mod = 1.0f; formant = 0.0f; gainDb = 0.0f; muted = false;
        newStart = start; newEnd = end;
    }
};

/** The global correction (parameters), applied on top of each note's own edits. */
struct Settings
{
    int key = 0, scale = 0, transpose = 0;
    float center = 0.0f, drift = 0.0f, vibrato = 1.0f, transitionMs = 60.0f, formant = 0.0f, outGain = 1.0f;

    bool operator== (const Settings& o) const noexcept
    {
        return key == o.key && scale == o.scale && transpose == o.transpose && juce::exactlyEqual (center, o.center)
            && juce::exactlyEqual (drift, o.drift) && juce::exactlyEqual (vibrato, o.vibrato)
            && juce::exactlyEqual (transitionMs, o.transitionMs) && juce::exactlyEqual (formant, o.formant)
            && juce::exactlyEqual (outGain, o.outGain);
    }
    bool operator!= (const Settings& o) const noexcept { return ! (*this == o); }
};

/** Where a note's centre ends up: hand-set, or pulled toward the scale by Pitch Center; then transposed. */
inline float targetCentre (const Note& n, const Settings& s) noexcept
{
    const float base = n.moved ? n.centre + n.pitch
                               : n.centre + s.center * (snapToScale (n.centre, s.key, s.scale) - n.centre);
    return base + (float) s.transpose;
}

/** Analysis of one captured span. */
struct RangeAnalysis
{
    dd::Span span;
    dd::PitchTrack track;
    std::vector<int64_t> marks;     // glottal pulses in voiced stretches, ascending
    std::vector<int32_t> markRun;   // voiced stretch of each mark (a run's marks are consecutive)
};

struct Analysis
{
    double sampleRate = 48000.0;
    std::vector<RangeAnalysis> ranges;   // ascending, disjoint

    const RangeAnalysis* find (int64_t t) const noexcept
    {
        for (const auto& r : ranges)
            if (r.span.contains (t))
                return &r;
        return nullptr;
    }
};

/** Analysis entry points (Analysis.cpp). */
RangeAnalysis analyseRange (const dd::TakeStore& take, dd::Span span, double sampleRate, const std::atomic<bool>* cancel = nullptr);
std::vector<Note> findNotes (const RangeAnalysis& range, uint32_t& nextId);
float measureCentre (const dd::PitchTrack& track, int64_t start, int64_t end);
/** Most likely key and scale (major / minor) from the notes, weighted by length. */
std::pair<int, int> detectKey (const std::vector<Note>& notes);
} // namespace contour
