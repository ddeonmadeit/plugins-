#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace aircheck
{
/** Every parameter, in display order. The table in Params.cpp is the single source of truth. */
enum class P
{
    inGain, gate,
    tuneOn, tuneKey, tuneScale, tuneSpeed, tuneHumanize, tuneFlex, tuneVibrato, tuneAmount, tuneTranspose, tuneFormant,
    micType, preType, preDrive,
    eqLowCut, eqLsFreq, eqLsGain, eq1Freq, eq1Gain, eq1Q, eq2Freq, eq2Gain, eq2Q, eq3Freq, eq3Gain, eq3Q,
    eqHsFreq, eqHsGain, eqHighCut,
    compType, compAmount, compAttack, compRelease, compRatio, compMix,
    deess, deessFreq,
    dblMix, dblWidth,
    vocMix, vocCarrier, vocChord,
    tape,
    dlyMix, dlySync, dlyDiv, dlyMs, dlyFeedback, dlyTone, dlyPingPong, dlyDuck,
    revMix, revType, revSize, revPreDelay, revTone,
    mix, outGain,
    // 1.2: the eight-band dynamic EQ (the six legacy eq* parameters above are kept for old sessions only),
    // saturation tone / mix, rider, sidechain filter, resonance smoothing, air, chorus / dimension, limiter.
    b1On, b1Type, b1Freq, b1Gain, b1Q, b1Slope, b1Dyn, b1Thresh, b1Place,
    b2On, b2Type, b2Freq, b2Gain, b2Q, b2Slope, b2Dyn, b2Thresh, b2Place,
    b3On, b3Type, b3Freq, b3Gain, b3Q, b3Slope, b3Dyn, b3Thresh, b3Place,
    b4On, b4Type, b4Freq, b4Gain, b4Q, b4Slope, b4Dyn, b4Thresh, b4Place,
    b5On, b5Type, b5Freq, b5Gain, b5Q, b5Slope, b5Dyn, b5Thresh, b5Place,
    b6On, b6Type, b6Freq, b6Gain, b6Q, b6Slope, b6Dyn, b6Thresh, b6Place,
    b7On, b7Type, b7Freq, b7Gain, b7Q, b7Slope, b7Dyn, b7Thresh, b7Place,
    b8On, b8Type, b8Freq, b8Gain, b8Q, b8Slope, b8Dyn, b8Thresh, b8Place,
    preTone, preMix, rider, compScHpf,
    smoothDepth, smoothSharp, smoothLow, smoothHigh,
    airMid, airHigh,
    chRate, chDepth, chMix, dimMode, dimMix,
    limit, eqPost,
    count
};

constexpr int kNumParams = (int) P::count;

enum class Unit { None, Percent, Hz, Ms, Db, Semis, SemisFine, Bipolar, Ratio, Q };
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
} // namespace aircheck
