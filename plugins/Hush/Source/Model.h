#pragma once

#include <dd_take/dd_take.h>

/*
    Hush's data: every sharp sound found in a captured vocal (an "s", "sh", "ch", or a "t"/"k" burst), the
    reduction each one gets, and a coarse picture of the take for the editor. Times are timeline samples at
    the current processing rate; saved state stores seconds.
*/
namespace hush
{
enum class SoundKind { S = 0, SH, T };

inline const juce::StringArray& kindNames()
{
    static const juce::StringArray n { "S", "SH", "T" };
    return n;
}

inline const juce::StringArray& modeNames()
{
    static const juce::StringArray n { "Split", "Wide" };
    return n;
}

struct Sound
{
    uint32_t id = 0;
    int64_t start = 0, end = 0;
    float peakDb = -60.0f;     // level in the sibilance band at its loudest (dBFS)
    float hz = 6000.0f;        // where its energy sits
    int kind = 0;
    bool manual = false;       // reduction set by hand (Amount leaves it alone)
    float userDb = 0.0f;       // the hand-set reduction, dB (0 = untouched)
};

struct Settings
{
    float amount = 0.5f, sensitivity = 0.5f, freq = 5000.0f, fadeMs = 5.0f, outGain = 1.0f;
    int mode = 0;
    bool listen = false;

    bool operator== (const Settings& o) const noexcept
    {
        return juce::exactlyEqual (amount, o.amount) && juce::exactlyEqual (sensitivity, o.sensitivity)
            && juce::exactlyEqual (freq, o.freq) && juce::exactlyEqual (fadeMs, o.fadeMs)
            && juce::exactlyEqual (outGain, o.outGain) && mode == o.mode && listen == o.listen;
    }
    bool operator!= (const Settings& o) const noexcept { return ! (*this == o); }
};

/** The take's typical sharp sound (30th percentile of peaks): louder ones get turned down more. */
inline float referenceDb (const std::vector<Sound>& sounds)
{
    if (sounds.empty())
        return -40.0f;
    std::vector<float> p;
    for (const auto& s : sounds)
        p.push_back (s.peakDb);
    std::sort (p.begin(), p.end());
    return p[(size_t) ((double) (p.size() - 1) * 0.3)];
}

/** How far a sound is turned down (dB, <= 0 for automatic): hand-set, or Amount scaled by how much it
    sticks out above the take's typical sharp sound. */
inline float reductionDb (const Sound& s, const Settings& st, float refDb) noexcept
{
    if (s.manual)
        return s.userDb;
    return -std::min (30.0f, st.amount * (6.0f + 0.75f * juce::jlimit (0.0f, 24.0f, s.peakDb - refDb)));
}

/** A coarse picture of one captured span: overall and sibilance-band level every 2 ms. */
struct Picture
{
    int64_t start = 0;
    int hop = 96;
    std::vector<float> full, high;   // dBFS
};

struct Analysis
{
    std::vector<Sound> sounds;
    Picture picture;
};

/** Finds the sharp sounds of [span) (Analysis.cpp). Higher sensitivity finds quieter ones. */
Analysis detectSounds (const dd::TakeStore& take, dd::Span span, double sampleRate, float sensitivity, uint32_t& nextId,
                       const std::atomic<bool>* cancel = nullptr);

/** The take below `hz`, zero-phase (no smearing, no delay): Split mode crossfades to this. */
std::shared_ptr<dd::TakeStore> lowBand (const dd::TakeStore& take, const std::vector<dd::Span>& spans, double sampleRate, float hz,
                                        const std::atomic<bool>* cancel = nullptr);
} // namespace hush
