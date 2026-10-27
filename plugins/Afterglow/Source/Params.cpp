#include "Params.h"

#include <dd_dsp/dd_dsp.h>

namespace ag
{
namespace choices
{
    const juce::StringArray& sources()     { static const juce::StringArray c { "Analog", "Strings", "Choir", "Glass", "Tines", "Air", "Sine", "Pluck", "Organ", "Mallet", "Sync", "Electric" }; return c; }
    const juce::StringArray& noiseTypes()  { static const juce::StringArray c { "Hiss", "Vinyl", "Wind" }; return c; }
    const juce::StringArray& filterTypes() { static const juce::StringArray c { "Ladder", "LP 12", "BP 12", "HP 12" }; return c; }
    const juce::StringArray& voiceModes()  { static const juce::StringArray c { "Poly", "Mono", "Legato" }; return c; }
    const juce::StringArray& pumpModes()   { static const juce::StringArray c { "Duck", "Gate" }; return c; }
}

namespace
{
using K = Kind;
using U = Unit;

const std::array<ParamSpec, (size_t) kNumParams>& table()
{
    static const std::array<ParamSpec, (size_t) kNumParams> t {{
        { P::aOn,       "aOn",       "A On",          K::Bool,   0, 1, 1, 0, U::None, nullptr },
        { P::aSource,   "aSource",   "A Source",      K::Choice, 0, 11, 0, 0, U::None, &choices::sources() },
        { P::aLevel,    "aLevel",    "A Level",       K::Float,  0, 1, 0.8f, 0, U::Percent, nullptr },
        { P::aOct,      "aOct",      "A Octave",      K::Int,   -3, 3, 0, 0, U::None, nullptr },
        { P::aSemi,     "aSemi",     "A Semi",        K::Int,  -12, 12, 0, 0, U::Semis, nullptr },
        { P::aFine,     "aFine",     "A Fine",        K::Float, -50, 50, 0, 0, U::Cents, nullptr },
        { P::aVoices,   "aVoices",   "A Unison",      K::Int,    1, 7, 3, 0, U::None, nullptr },
        { P::aDetune,   "aDetune",   "A Detune",      K::Float,  0, 60, 14, 0, U::Cents, nullptr },
        { P::aSpread,   "aSpread",   "A Spread",      K::Float,  0, 1, 0.6f, 0, U::Percent, nullptr },
        { P::aShape,    "aShape",    "A Shape",       K::Float,  0, 1, 0.3f, 0, U::Percent, nullptr },
        { P::aChar,     "aChar",     "A Character",   K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::aShapeEnv, "aShapeEnv", "A Shape Env",   K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::aPan,      "aPan",      "A Pan",         K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::aDrift,    "aDrift",    "A Drift",       K::Float,  0, 1, 0.2f, 0, U::Percent, nullptr },

        { P::bOn,       "bOn",       "B On",          K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::bSource,   "bSource",   "B Source",      K::Choice, 0, 11, 2, 0, U::None, &choices::sources() },
        { P::bLevel,    "bLevel",    "B Level",       K::Float,  0, 1, 0.8f, 0, U::Percent, nullptr },
        { P::bOct,      "bOct",      "B Octave",      K::Int,   -3, 3, 0, 0, U::None, nullptr },
        { P::bSemi,     "bSemi",     "B Semi",        K::Int,  -12, 12, 0, 0, U::Semis, nullptr },
        { P::bFine,     "bFine",     "B Fine",        K::Float, -50, 50, 0, 0, U::Cents, nullptr },
        { P::bVoices,   "bVoices",   "B Unison",      K::Int,    1, 7, 3, 0, U::None, nullptr },
        { P::bDetune,   "bDetune",   "B Detune",      K::Float,  0, 60, 14, 0, U::Cents, nullptr },
        { P::bSpread,   "bSpread",   "B Spread",      K::Float,  0, 1, 0.6f, 0, U::Percent, nullptr },
        { P::bShape,    "bShape",    "B Shape",       K::Float,  0, 1, 0.3f, 0, U::Percent, nullptr },
        { P::bChar,     "bChar",     "B Character",   K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::bShapeEnv, "bShapeEnv", "B Shape Env",   K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::bPan,      "bPan",      "B Pan",         K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::bDrift,    "bDrift",    "B Drift",       K::Float,  0, 1, 0.2f, 0, U::Percent, nullptr },

        { P::noiseLevel, "noiseLevel", "Noise",          K::Float,  0, 1, 0, 0, U::Percent, nullptr },
        { P::noiseType,  "noiseType",  "Noise Type",     K::Choice, 0, 2, 0, 0, U::None, &choices::noiseTypes() },
        { P::noiseTone,  "noiseTone",  "Noise Tone",     K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },

        { P::filterType, "filterType", "Filter Type",    K::Choice, 0, 3, 0, 0, U::None, &choices::filterTypes() },
        { P::cutoff,     "cutoff",     "Cutoff",         K::Float, 20, 20000, 4000, 1000, U::Hz, nullptr },
        { P::reso,       "reso",       "Resonance",      K::Float,  0, 1, 0.1f, 0, U::Percent, nullptr },
        { P::filterDrive,"filterDrive","Filter Drive",   K::Float,  0, 1, 0.1f, 0, U::Percent, nullptr },
        { P::filterEnv,  "filterEnv",  "Filter Env",     K::Float, -6, 6, 0, 0, U::Octaves, nullptr },
        { P::keyTrack,   "keyTrack",   "Key Track",      K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::velFilter,  "velFilter",  "Velocity > Filter", K::Float, 0, 1, 0.2f, 0, U::Percent, nullptr },

        { P::aAttack,    "aAttack",    "Attack",         K::Float, 1, 20000, 400, 500, U::Ms, nullptr },
        { P::aDecay,     "aDecay",     "Decay",          K::Float, 10, 20000, 3000, 1500, U::Ms, nullptr },
        { P::aSustain,   "aSustain",   "Sustain",        K::Float,  0, 1, 0.8f, 0, U::Percent, nullptr },
        { P::aRelease,   "aRelease",   "Release",        K::Float, 10, 20000, 1500, 1500, U::Ms, nullptr },
        { P::mAttack,    "mAttack",    "Mod Attack",     K::Float, 1, 20000, 800, 500, U::Ms, nullptr },
        { P::mDecay,     "mDecay",     "Mod Decay",      K::Float, 10, 20000, 4000, 1500, U::Ms, nullptr },
        { P::mSustain,   "mSustain",   "Mod Sustain",    K::Float,  0, 1, 0.3f, 0, U::Percent, nullptr },
        { P::mRelease,   "mRelease",   "Mod Release",    K::Float, 10, 20000, 2000, 1500, U::Ms, nullptr },
        { P::velocity,   "velocity",   "Velocity",       K::Float,  0, 1, 0.4f, 0, U::Percent, nullptr },
        { P::voiceMode,  "voiceMode",  "Voice Mode",     K::Choice, 0, 2, 0, 0, U::None, &choices::voiceModes() },
        { P::glide,      "glide",      "Glide",          K::Float,  0, 2000, 0, 200, U::Ms, nullptr },
        { P::bendRange,  "bendRange",  "Bend Range",     K::Int,    0, 24, 2, 0, U::Semis, nullptr },

        { P::lfo1Shape,  "lfo1Shape",  "LFO 1 Shape",    K::Choice, 0, 4, 0, 0, U::None, &dd::Lfo::shapeNames() },
        { P::lfo1Rate,   "lfo1Rate",   "LFO 1 Rate",     K::Float, 0.02f, 20, 5, 2, U::Hz, nullptr },
        { P::lfo1Sync,   "lfo1Sync",   "LFO 1 Sync",     K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::lfo1Div,    "lfo1Div",    "LFO 1 Division", K::Choice, 0, 13, 7, 0, U::None, &dd::divisionNames() },
        { P::lfo1Delay,  "lfo1Delay",  "LFO 1 Fade In",  K::Float,  0, 5000, 600, 800, U::Ms, nullptr },
        { P::lfo1Pitch,  "lfo1Pitch",  "LFO 1 > Pitch",  K::Float,  0, 100, 0, 20, U::Cents, nullptr },
        { P::lfo1Cutoff, "lfo1Cutoff", "LFO 1 > Cutoff", K::Float,  0, 3, 0, 0, U::Octaves, nullptr },
        { P::lfo1Amp,    "lfo1Amp",    "LFO 1 > Amp",    K::Float,  0, 1, 0, 0, U::Percent, nullptr },
        { P::lfo1Pan,    "lfo1Pan",    "LFO 1 > Pan",    K::Float,  0, 1, 0, 0, U::Percent, nullptr },
        { P::lfo1Morph,  "lfo1Morph",  "LFO 1 > Shape",  K::Float,  0, 1, 0, 0, U::Percent, nullptr },
        { P::lfo2Shape,  "lfo2Shape",  "LFO 2 Shape",    K::Choice, 0, 4, 0, 0, U::None, &dd::Lfo::shapeNames() },
        { P::lfo2Rate,   "lfo2Rate",   "LFO 2 Rate",     K::Float, 0.01f, 10, 0.15f, 0.5f, U::Hz, nullptr },
        { P::lfo2Sync,   "lfo2Sync",   "LFO 2 Sync",     K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::lfo2Div,    "lfo2Div",    "LFO 2 Division", K::Choice, 0, 13, 0, 0, U::None, &dd::divisionNames() },
        { P::lfo2Cutoff, "lfo2Cutoff", "LFO 2 > Cutoff", K::Float,  0, 3, 0.3f, 0, U::Octaves, nullptr },
        { P::lfo2Morph,  "lfo2Morph",  "LFO 2 > Shape",  K::Float,  0, 1, 0, 0, U::Percent, nullptr },
        { P::lfo2Blend,  "lfo2Blend",  "LFO 2 > Blend",  K::Float,  0, 1, 0, 0, U::Percent, nullptr },

        { P::tapeOn,     "tapeOn",     "Tape",           K::Bool,   0, 1, 1, 0, U::None, nullptr },
        { P::tapeDrive,  "tapeDrive",  "Tape Drive",     K::Float,  0, 1, 0.15f, 0, U::Percent, nullptr },
        { P::tapeWow,    "tapeWow",    "Tape Wow",       K::Float,  0, 1, 0.15f, 0, U::Percent, nullptr },
        { P::tapeFlutter,"tapeFlutter","Tape Flutter",   K::Float,  0, 1, 0.1f, 0, U::Percent, nullptr },
        { P::lofi,       "lofi",       "Lo-Fi",          K::Float,  0, 1, 0, 0, U::Percent, nullptr },
        { P::ensOn,      "ensOn",      "Ensemble",       K::Bool,   0, 1, 1, 0, U::None, nullptr },
        { P::ensMode,    "ensMode",    "Ensemble Mode",  K::Choice, 0, 2, 0, 0, U::None, &dd::Ensemble::modeNames() },
        { P::ensMix,     "ensMix",     "Ensemble Mix",   K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::phaserOn,   "phaserOn",   "Phaser",         K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::phaserRate, "phaserRate", "Phaser Rate",    K::Float, 0.02f, 8, 0.25f, 0.6f, U::Hz, nullptr },
        { P::phaserDepth,"phaserDepth","Phaser Depth",   K::Float,  0, 1, 0.6f, 0, U::Percent, nullptr },
        { P::phaserFeedback,"phaserFeedback","Phaser Feedback", K::Float, 0, 0.9f, 0.4f, 0, U::Percent, nullptr },
        { P::phaserMix,  "phaserMix",  "Phaser Mix",     K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::delayOn,    "delayOn",    "Delay",          K::Bool,   0, 1, 1, 0, U::None, nullptr },
        { P::delayDiv,   "delayDiv",   "Delay Time",     K::Choice, 0, 13, 8, 0, U::None, &dd::divisionNames() },
        { P::delayFeedback,"delayFeedback","Delay Feedback", K::Float, 0, 0.95f, 0.35f, 0, U::Percent, nullptr },
        { P::delayMix,   "delayMix",   "Delay Mix",      K::Float,  0, 1, 0.15f, 0, U::Percent, nullptr },
        { P::delayTone,  "delayTone",  "Delay Tone",     K::Float,  0, 1, 0.45f, 0, U::Percent, nullptr },
        { P::delayPingPong,"delayPingPong","Ping Pong",  K::Bool,   0, 1, 1, 0, U::None, nullptr },
        { P::delayMod,   "delayMod",   "Delay Wobble",   K::Float,  0, 1, 0.2f, 0, U::Percent, nullptr },
        { P::reverbOn,   "reverbOn",   "Reverb",         K::Bool,   0, 1, 1, 0, U::None, nullptr },
        { P::reverbMix,  "reverbMix",  "Reverb Mix",     K::Float,  0, 1, 0.3f, 0, U::Percent, nullptr },
        { P::reverbSize, "reverbSize", "Reverb Size",    K::Float,  0, 1, 0.7f, 0, U::Percent, nullptr },
        { P::reverbTone, "reverbTone", "Reverb Tone",    K::Float,  0, 1, 0.6f, 0, U::Percent, nullptr },
        { P::shimmer,    "shimmer",    "Shimmer",        K::Float,  0, 1, 0, 0, U::Percent, nullptr },
        { P::pumpOn,     "pumpOn",     "Pump",           K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::pumpMode,   "pumpMode",   "Pump Mode",      K::Choice, 0, 1, 0, 0, U::None, &choices::pumpModes() },
        { P::pumpDiv,    "pumpDiv",    "Pump Rate",      K::Choice, 0, 13, 4, 0, U::None, &dd::divisionNames() },
        { P::pumpDepth,  "pumpDepth",  "Pump Depth",     K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::lowCut,     "lowCut",     "Low Cut",        K::Float, 20, 1000, 40, 120, U::Hz, nullptr },
        { P::highCut,    "highCut",    "High Cut",       K::Float, 1000, 20000, 18000, 6000, U::Hz, nullptr },
        { P::width,      "width",      "Width",          K::Float,  0, 2, 1, 0, U::Percent, nullptr },
        { P::glue,       "glue",       "Glue",           K::Float,  0, 1, 0.15f, 0, U::Percent, nullptr },
        { P::outGain,    "outGain",    "Output",         K::Float, -24, 12, 0, 0, U::Db, nullptr },

        { P::mTone,      "mTone",      "Tone",           K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::mMotion,    "mMotion",    "Motion",         K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::mSpace,     "mSpace",     "Space",          K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::mEcho,      "mEcho",      "Echo",           K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::mGrit,      "mGrit",      "Grit",           K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::mSwell,     "mSwell",     "Swell",          K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::mTail,      "mTail",      "Tail",           K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::mBlend,     "mBlend",     "Blend",          K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },

        { P::distOn,     "distOn",     "Dirt",           K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::distType,   "distType",   "Dirt Type",      K::Choice, 0, 7, 2, 0, U::None, &dd::dirtTypeNames() },
        { P::distDrive,  "distDrive",  "Dirt Drive",     K::Float,  0, 1, 0.4f, 0, U::Percent, nullptr },
        { P::distMix,    "distMix",    "Dirt Mix",       K::Float,  0, 1, 1, 0, U::Percent, nullptr },
        { P::distTone,   "distTone",   "Dirt Tone",      K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::perfPoly,   "perfPoly",   "Poly Keys",      K::Bool,   0, 1, 1, 0, U::None, nullptr },
        { P::cabOn,      "cabOn",      "Cabinet",        K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::cabType,    "cabType",    "Cabinet Type",   K::Choice, 0, 1, 0, 0, U::None, &dd::CabSim::typeNames() },
        { P::strum,      "strum",      "Strum",          K::Float,  0, 80, 0, 0, U::Ms, nullptr }
    }};
    return t;
}

juce::String formatValue (Unit unit, float v)
{
    switch (unit)
    {
        case U::Percent: return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        case U::Hz:
            if (v >= 1000.0f) return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
            if (v < 10.0f)    return juce::String (v, 2) + " Hz";
            return juce::String (juce::roundToInt (v)) + " Hz";
        case U::Ms:
            if (v >= 1000.0f) return juce::String (v / 1000.0f, 2) + " s";
            if (v < 10.0f)    return juce::String (v, 1) + " ms";
            return juce::String (juce::roundToInt (v)) + " ms";
        case U::Db:      return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
        case U::Semis:   return (v > 0.0f ? "+" : "") + (std::abs (v - std::round (v)) < 0.01f ? juce::String (juce::roundToInt (v)) : juce::String (v, 1)) + " st";
        case U::Cents:   return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct";
        case U::Octaves: return (v > 0.0f ? "+" : "") + juce::String (v, 2) + " oct";
        case U::Bipolar: return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f));
        case U::Seconds: return juce::String (v, 2) + " s";
        case U::None:    break;
    }
    return juce::String (v, 2);
}
} // namespace

const ParamSpec& spec (P p)
{
    const auto& s = table()[(size_t) p];
    jassert (s.param == p); // table order must match the enum
    return s;
}

const char* id (P p) { return spec (p).id; }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& s : table())
    {
        const juce::ParameterID pid { s.id, 1 };
        switch (s.kind)
        {
            case K::Float:
            {
                juce::NormalisableRange<float> range (s.min, s.max);
                if (s.centre > 0.0f)
                    range.setSkewForCentre (s.centre);
                const auto unit = s.unit;
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    pid, s.name, range, s.def,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                        [unit] (float v, int) { return formatValue (unit, v); })));
                break;
            }
            case K::Int:
            {
                const auto unit = s.unit;
                const bool isSigned = s.min < 0;
                layout.add (std::make_unique<juce::AudioParameterInt> (
                    pid, s.name, (int) s.min, (int) s.max, (int) s.def,
                    juce::AudioParameterIntAttributes().withStringFromValueFunction (
                        [unit, isSigned] (int v, int) {
                            if (unit != U::None)
                                return formatValue (unit, (float) v);
                            return (isSigned && v > 0 ? "+" : "") + juce::String (v);
                        })));
                break;
            }
            case K::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, s.name, s.def >= 0.5f));
                break;
            case K::Choice:
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, *s.choices, (int) s.def));
                break;
        }
    }
    return layout;
}

void ParamCache::attach (juce::AudioProcessorValueTreeState& state)
{
    for (int i = 0; i < kNumParams; ++i)
    {
        values[(size_t) i] = state.getRawParameterValue (id ((P) i));
        jassert (values[(size_t) i] != nullptr);
    }
}
} // namespace ag
