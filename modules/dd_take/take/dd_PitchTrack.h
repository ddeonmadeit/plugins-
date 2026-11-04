#pragma once

namespace dd
{
/**
    The sung pitch of a captured span, one frame every 5 ms. Found offline, so it can afford what a
    real-time tuner can't: YIN candidates for every frame, then the most plausible path through them
    (octave jumps and voiced/unvoiced flips cost extra, pYIN-style), refined to a fraction of a cent on
    the full-rate signal.
*/
struct PitchTrack
{
    double sampleRate = 48000.0;
    int hop = 240;
    int64_t start = 0;            // timeline sample at frame 0
    std::vector<float> midi;      // MIDI note number (fractional); 0 = unvoiced
    std::vector<float> level;     // frame RMS, dBFS

    int size() const noexcept { return (int) midi.size(); }
    bool voiced (int f) const noexcept { return juce::isPositiveAndBelow (f, size()) && midi[(size_t) f] > 0.0f; }
    int64_t timeOf (int f) const noexcept { return start + (int64_t) f * hop; }
    double frameOf (double t) const noexcept { return (t - (double) start) / hop; }
    int nearestFrame (double t) const noexcept { return juce::jlimit (0, std::max (0, size() - 1), (int) std::lround (frameOf (t))); }

    /** Pitch at a timeline position, interpolated between voiced frames (0 if unvoiced there). */
    float midiAt (double t) const noexcept
    {
        const double f = frameOf (t);
        const int a = (int) std::floor (f);
        const float frac = (float) (f - a);
        const bool va = voiced (a), vb = voiced (a + 1);
        if (va && vb)
            return midi[(size_t) a] + (midi[(size_t) (a + 1)] - midi[(size_t) a]) * frac;
        if (va && frac < 0.5f)
            return midi[(size_t) a];
        if (vb && frac >= 0.5f)
            return midi[(size_t) (a + 1)];
        return 0.0f;
    }

    float levelAt (double t) const noexcept
    {
        if (level.empty())
            return -120.0f;
        return level[(size_t) nearestFrame (t)];
    }
};

struct PitchTrackSettings
{
    float minHz = 60.0f, maxHz = 1100.0f;
    float unvoicedCost = 0.3f;    // higher = more frames called voiced
    float silenceDb = -60.0f;     // frames quieter than this are unvoiced
};

/** Tracks [span.start, span.end) of the take. `cancel` (optional) aborts early, returning what's done. */
PitchTrack trackPitch (const TakeStore& take, Span span, double sampleRate, const PitchTrackSettings& settings = {},
                       const std::atomic<bool>* cancel = nullptr);

/** Same, from a plain buffer (sample 0 = timeline position `start`). */
PitchTrack trackPitch (const float* data, int numSamples, int64_t start, double sampleRate, const PitchTrackSettings& settings = {},
                       const std::atomic<bool>* cancel = nullptr);
} // namespace dd
