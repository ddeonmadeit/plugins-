#pragma once

#include "Model.h"

/*
    Turning notes + edits into sound. The edit pass (build) runs on the worker thread and produces a
    table of grains: one per glottal pulse in voiced parts (TD-PSOLA: pulses are re-spaced to change the
    pitch, which keeps the voice's formants where they were), and short overlapping slices in unvoiced
    parts (breaths, consonants: untouched in pitch, only moved in time). The audio thread overlap-adds
    the grains straight from the take, so playback has no latency and follows edits instantly.

    With no edits, every grain sits exactly where it was read and the windows sum to one: the output is
    the recording, sample for sample.
*/
namespace contour
{
struct Grain
{
    double out = 0.0;      // timeline position the grain is centred on
    double in = 0.0;       // take position it is read from
    float left = 0.0f, right = 0.0f;   // window half-widths (samples)
    float speed = 1.0f;    // read speed inside the grain: >1 moves formants up
    float gain = 1.0f;
};

/** Everything the audio thread needs, immutable once published. */
struct Playback
{
    std::vector<Grain> grains;          // ascending by `out`
    std::vector<dd::Span> spans;        // where the take replaces the live input
    float maxHalf = 0.0f;
    float outGain = 1.0f;
};

/** The corrected pitch for the editor: one point per analysis frame, at its output time. */
struct CurvePoint
{
    int64_t t = 0;
    float midi = 0.0f;     // after edits (0 = unvoiced)
    float original = 0.0f; // as sung
    float level = -120.0f; // dBFS
};

struct DisplayCurve
{
    std::vector<CurvePoint> points;     // ascending by t
    float peakLevel = -120.0f;
};

/** Time edits: a monotonic map from take time to output time through the notes' moved edges. */
class TimeMap
{
public:
    TimeMap() = default;
    TimeMap (const std::vector<Note>& notes, const std::vector<dd::Span>& spans);

    double toOutput (double t) const noexcept { return map (t, false); }
    double toInput (double t) const noexcept { return map (t, true); }
    bool isIdentity() const noexcept { return identity; }

private:
    double map (double t, bool inverse) const noexcept;
    std::vector<std::pair<double, double>> anchors;   // (input, output), both ascending
    bool identity = true;
};

struct BuildResult
{
    std::unique_ptr<Playback> playback;
    std::shared_ptr<DisplayCurve> curve;
};

BuildResult build (const Analysis& analysis, const std::vector<Note>& notes, const Settings& settings,
                   const std::vector<dd::Span>& spans);

/** Audio-thread overlap-add of a Playback. */
class Renderer
{
public:
    static constexpr int kMaxBlock = 512;

    Renderer();

    /** Renders the take for timeline [t0, t0 + n) (n <= kMaxBlock) into `out`, and how much of each sample
        the take covers (0 = play the live input there) into `cover`. */
    void render (const Playback& pb, const dd::TakeStore& take, int64_t t0, int n, float* out, float* cover) noexcept;

private:
    float window (float u) const noexcept
    {
        const float x = u * (float) kLut;
        const int i = (int) x;
        if (i >= kLut)
            return 0.0f;
        return lut[(size_t) i] + (lut[(size_t) i + 1] - lut[(size_t) i]) * (x - (float) i);
    }

    /** Windowed mean of a grain read at a changed speed (formant shift), so its offset doesn't add up to DC. */
    float grainMean (const Grain& g, const dd::TakeStore& take) noexcept;

    static constexpr int kLut = 1024, kMeanCache = 64;
    std::array<float, kLut + 2> lut {};
    struct MeanEntry { const Grain* grain = nullptr; double in = 0.0; float speed = 0.0f, mean = 0.0f; };
    std::array<MeanEntry, kMeanCache> meanCache {};
    int meanNext = 0;
    std::array<float, kMaxBlock> num {}, den {};
};
} // namespace contour
