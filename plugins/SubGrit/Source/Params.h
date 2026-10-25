#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace sg
{
/** Every parameter, in display order. The table in Params.cpp is the single source of truth. */
enum class P
{
    voiceMode, legato, glide, glideMode, octave, tune, fine, bendRange, velocity, drift,
    bodyLevel, bodyDrive, pitchAmt, pitchTime, clickLevel, clickTone,
    oscALevel, oscAWave, oscAOct, oscASemi, oscAFine, oscAVoices, oscADetune, oscAWidth,
    oscBLevel, oscBWave, oscBOct, oscBSemi, oscBFine, oscBVoices, oscBDetune, oscBWidth,
    noiseLevel, noiseColor,
    filterMode, cutoff, resonance, filterDrive, filterEnv, keyTrack, bodyToFilter,
    fAttack, fDecay, fSustain, fRelease,
    aAttack, aDecay, aSustain, aRelease,
    diveAmt, diveTime,
    lfoShape, lfoSync, lfoRate, lfoDiv, lfoFade, lfoPitch, lfoCutoff, lfoAmp, lfoDirt,
    dirtType, dirtDrive, dirtTone, dirtBias, dirtMix, subKeep, xover,
    eqSub, eqMidFreq, eqMidGain, eqHighCut,
    chorusMix, chorusRate, chorusDepth,
    delayMix, delayDiv, delayFeedback, delayTone, delayPingPong,
    spaceMix, spaceSize,
    glue, clip, width, outGain,
    perfPoly,   // performance switch: belongs to the instance, never to a preset (see dd::isPerformanceParam)
    count
};

constexpr int kNumParams = (int) P::count;

enum class Unit { None, Percent, Hz, Ms, Db, Semis, Cents, Octaves, Bipolar };
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

namespace choices
{
    const juce::StringArray& voiceModes();
    const juce::StringArray& glideModes();
    const juce::StringArray& filterModes();
}
} // namespace sg
