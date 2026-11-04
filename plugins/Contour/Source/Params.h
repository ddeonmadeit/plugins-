#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace contour
{
/** Every parameter, in display order. The table in Params.cpp is the single source of truth. */
enum class P
{
    center, drift, vibrato, transition, formant, transpose, outGain,
    perfKey, perfScale,
    count
};

constexpr int kNumParams = (int) P::count;

enum class Unit { None, Percent, Hz, Ms, Db, Semis, SemisFine, Bipolar };
enum class Kind { Float, Int, Bool, Choice };

struct ParamSpec
{
    P param;
    const char* id;
    const char* name;
    Kind kind;
    float min, max, def;
    float centre;                       // skew centre for Float (0 = linear)
    Unit unit;
    const juce::StringArray* choices;   // Kind::Choice only
};

const char* id (P p);
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Raw atomic pointers into the APVTS, indexed by P. Read on the audio thread. */
class ParamCache
{
public:
    void attach (juce::AudioProcessorValueTreeState& state);
    float get (P p) const noexcept    { return values[(size_t) p]->load (std::memory_order_relaxed); }
    int getInt (P p) const noexcept   { return (int) std::lround (get (p)); }
    bool getBool (P p) const noexcept { return get (p) >= 0.5f; }

private:
    std::array<std::atomic<float>*, (size_t) kNumParams> values {};
};
} // namespace contour
