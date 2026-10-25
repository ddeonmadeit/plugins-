#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ag
{
/**
    Every parameter, in table order (Params.cpp is the single source of truth). The engine is deep but
    the editor only shows the macros: presets carry the rest, the macros bend the preset around.
*/
enum class P
{
    // Layers A and B
    aOn, aSource, aLevel, aOct, aSemi, aFine, aVoices, aDetune, aSpread, aShape, aChar, aShapeEnv, aPan, aDrift,
    bOn, bSource, bLevel, bOct, bSemi, bFine, bVoices, bDetune, bSpread, bShape, bChar, bShapeEnv, bPan, bDrift,
    noiseLevel, noiseType, noiseTone,

    // Voice filter and envelopes
    filterType, cutoff, reso, filterDrive, filterEnv, keyTrack, velFilter,
    aAttack, aDecay, aSustain, aRelease,
    mAttack, mDecay, mSustain, mRelease,
    velocity, voiceMode, glide, bendRange,

    // Modulation
    lfo1Shape, lfo1Rate, lfo1Sync, lfo1Div, lfo1Delay, lfo1Pitch, lfo1Cutoff, lfo1Amp, lfo1Pan, lfo1Morph,
    lfo2Shape, lfo2Rate, lfo2Sync, lfo2Div, lfo2Cutoff, lfo2Morph, lfo2Blend,

    // Effects
    tapeOn, tapeDrive, tapeWow, tapeFlutter, lofi,
    ensOn, ensMode, ensMix,
    phaserOn, phaserRate, phaserDepth, phaserFeedback, phaserMix,
    delayOn, delayDiv, delayFeedback, delayMix, delayTone, delayPingPong, delayMod,
    reverbOn, reverbMix, reverbSize, reverbTone, shimmer,
    pumpOn, pumpMode, pumpDiv, pumpDepth,
    lowCut, highCut, width, glue, outGain,

    // Macros (the front panel): bipolar, 0 = the preset as designed
    mTone, mMotion, mSpace, mEcho, mGrit, mSwell, mTail, mBlend,

    // Added in 1.1 (append only: ids are permanent)
    distOn, distType, distDrive, distMix, distTone,
    perfPoly,   // performance switch: belongs to the instance, never to a preset (see dd::isPerformanceParam)

    count
};

constexpr int kNumParams = (int) P::count;

enum class Unit { None, Percent, Hz, Ms, Db, Semis, Cents, Octaves, Bipolar, Seconds };
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

const ParamSpec& spec (P p);
const char* id (P p);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Raw atomic pointers into the APVTS, indexed by P. Read on the audio thread. */
class ParamCache
{
public:
    void attach (juce::AudioProcessorValueTreeState& state);

    float get (P p) const noexcept      { return values[(size_t) p]->load (std::memory_order_relaxed); }
    int getInt (P p) const noexcept     { return (int) std::lround (get (p)); }
    bool getBool (P p) const noexcept   { return get (p) >= 0.5f; }

private:
    std::array<std::atomic<float>*, (size_t) kNumParams> values {};
};

/** Offset from layer A's parameter to layer B's (the two blocks have the same layout). */
constexpr int kLayerStride = (int) P::bOn - (int) P::aOn;
inline P layerParam (int layer, P aParam) { return (P) ((int) aParam + layer * kLayerStride); }

namespace choices
{
    const juce::StringArray& sources();
    const juce::StringArray& noiseTypes();
    const juce::StringArray& filterTypes();
    const juce::StringArray& voiceModes();
    const juce::StringArray& pumpModes();
}
} // namespace ag
