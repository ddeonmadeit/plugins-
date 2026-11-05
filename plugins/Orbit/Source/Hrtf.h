#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace orbit
{
/**
    Head-related impulse responses (Viking HRTF v2, CC BY 4.0; see hrtf/make_hrtf.py): minimum-phase
    filters on a 5 x 5 degree grid for a few pinna sets ("ears"), resampled to the host rate in prepare().
    The interaural time difference is not in the filters: itdSeconds() gives it for a smooth delay.
*/
class HrtfSet
{
public:
    HrtfSet();

    /** Resamples every head to `sampleRate` (allocates: call from prepareToPlay, never the audio thread). */
    void prepare (double sampleRate);

    int taps() const noexcept { return numTaps; }
    int heads() const noexcept { return numHeads; }

    /** Bilinear blend of the four nearest measured directions into left[taps()] and right[taps()].
        Azimuth clockwise from straight ahead, both in radians. Real-time safe. */
    void lookup (int head, float azimuth, float elevation, float* left, float* right) const noexcept;

    /** Far ear minus near ear, seconds, positive when the sound is on the right (Woodworth, 8.75 cm head). */
    static float itdSeconds (float azimuth, float elevation) noexcept;

    static const juce::StringArray& headNames();

private:
    int numHeads = 0, numEl = 0, numAz = 0, srcTaps = 0;
    float elMin = -45.0f, step = 5.0f, srcRate = 48000.0f;
    std::vector<float> source;      // [head][el][az][ear][srcTaps] at srcRate
    std::vector<float> table;       // the same, resampled: [head][el][az][ear][numTaps]
    int numTaps = 0;
    double preparedRate = 0.0;
};
} // namespace orbit
