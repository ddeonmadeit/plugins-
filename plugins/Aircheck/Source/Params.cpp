#include "Params.h"

#include <dd_dsp/dd_dsp.h>

#include "Chain.h"
#include "ProEq.h"

namespace aircheck
{
namespace
{
using K = Kind;
using U = Unit;

const std::array<ParamSpec, (size_t) kNumParams>& table()
{
    static const std::array<ParamSpec, (size_t) kNumParams> t {{
        { P::inGain,        "inGain",        "Input",       K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::gate,          "gate",          "Gate",        K::Float, -80, -20, -80, 0, U::Db, nullptr },

        { P::tuneOn,        "tuneOn",        "Tune",        K::Bool, 0, 1, 0, 0, U::None, nullptr },
        { P::tuneKey,       "tuneKey",       "Key",         K::Choice, 0, 11, 0, 0, U::None, &keyNames() },
        { P::tuneScale,     "tuneScale",     "Scale",       K::Choice, 0, 8, 0, 0, U::None, &scaleNames() },
        { P::tuneSpeed,     "tuneSpeed",     "Retune",      K::Float, 0, 400, 50, 60, U::Ms, nullptr },
        { P::tuneHumanize,  "tuneHumanize",  "Humanize",    K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::tuneFlex,      "tuneFlex",      "Flex",        K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::tuneVibrato,   "tuneVibrato",   "Vibrato",     K::Float, 0, 1, 1, 0, U::Percent, nullptr },
        { P::tuneAmount,    "tuneAmount",    "Amount",      K::Float, 0, 1, 1, 0, U::Percent, nullptr },
        { P::tuneTranspose, "tuneTranspose", "Transpose",   K::Int, -12, 12, 0, 0, U::Semis, nullptr },
        { P::tuneFormant,   "tuneFormant",   "Formant",     K::Float, -4, 4, 0, 0, U::SemisFine, nullptr },

        { P::micType,       "micType",       "Mic",         K::Choice, 0, 5, 0, 0, U::None, &micNames() },
        { P::preType,       "preType",       "Preamp",      K::Choice, 0, 3, 0, 0, U::None, &preampNames() },
        { P::preDrive,      "preDrive",      "Drive",       K::Float, 0, 1, 0.15f, 0, U::Percent, nullptr },

        { P::eqLowCut,      "eqLowCut",      "Low Cut",     K::Float, 20, 600, 80, 110, U::Hz, nullptr },
        { P::eqLsFreq,      "eqLsFreq",      "Low Freq",    K::Float, 40, 800, 150, 180, U::Hz, nullptr },
        { P::eqLsGain,      "eqLsGain",      "Low",         K::Float, -18, 18, 0, 0, U::Db, nullptr },
        { P::eq1Freq,       "eq1Freq",       "Freq 1",      K::Float, 80, 2000, 300, 400, U::Hz, nullptr },
        { P::eq1Gain,       "eq1Gain",       "Gain 1",      K::Float, -18, 18, 0, 0, U::Db, nullptr },
        { P::eq1Q,          "eq1Q",          "Q 1",         K::Float, 0.3f, 8, 1, 1.2f, U::Q, nullptr },
        { P::eq2Freq,       "eq2Freq",       "Freq 2",      K::Float, 300, 8000, 1500, 1500, U::Hz, nullptr },
        { P::eq2Gain,       "eq2Gain",       "Gain 2",      K::Float, -18, 18, 0, 0, U::Db, nullptr },
        { P::eq2Q,          "eq2Q",          "Q 2",         K::Float, 0.3f, 8, 1, 1.2f, U::Q, nullptr },
        { P::eq3Freq,       "eq3Freq",       "Freq 3",      K::Float, 1000, 16000, 4500, 4000, U::Hz, nullptr },
        { P::eq3Gain,       "eq3Gain",       "Gain 3",      K::Float, -18, 18, 0, 0, U::Db, nullptr },
        { P::eq3Q,          "eq3Q",          "Q 3",         K::Float, 0.3f, 8, 1, 1.2f, U::Q, nullptr },
        { P::eqHsFreq,      "eqHsFreq",      "High Freq",   K::Float, 2000, 20000, 10000, 8000, U::Hz, nullptr },
        { P::eqHsGain,      "eqHsGain",      "High",        K::Float, -18, 18, 0, 0, U::Db, nullptr },
        { P::eqHighCut,     "eqHighCut",     "High Cut",    K::Float, 2000, 22000, 22000, 8000, U::Hz, nullptr },

        { P::compType,      "compType",      "Comp Type",   K::Choice, 0, 3, 1, 0, U::None, &compNames() },
        { P::compAmount,    "compAmount",    "Comp",        K::Float, 0, 1, 0.35f, 0, U::Percent, nullptr },
        { P::compAttack,    "compAttack",    "Attack",      K::Float, 0.05f, 80, 3, 5, U::Ms, nullptr },
        { P::compRelease,   "compRelease",   "Release",     K::Float, 20, 1500, 120, 150, U::Ms, nullptr },
        { P::compRatio,     "compRatio",     "Ratio",       K::Float, 1.5f, 20, 4, 4, U::Ratio, nullptr },
        { P::compMix,       "compMix",       "Comp Mix",    K::Float, 0, 1, 1, 0, U::Percent, nullptr },

        { P::deess,         "deess",         "De-Ess",      K::Float, 0, 1, 0.3f, 0, U::Percent, nullptr },
        { P::deessFreq,     "deessFreq",     "Ess Freq",    K::Float, 3000, 16000, 6500, 6000, U::Hz, nullptr },

        { P::dblMix,        "dblMix",        "Double",      K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::dblWidth,      "dblWidth",      "Dbl Width",   K::Float, 0, 1, 0.8f, 0, U::Percent, nullptr },

        { P::vocMix,        "vocMix",        "Vocoder",     K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::vocCarrier,    "vocCarrier",    "Carrier",     K::Choice, 0, 3, 0, 0, U::None, &carrierNames() },
        { P::vocChord,      "vocChord",      "Chord",       K::Choice, 0, 4, 0, 0, U::None, &chordNames() },

        { P::tape,          "tape",          "Tape",        K::Float, 0, 1, 0, 0, U::Percent, nullptr },

        { P::dlyMix,        "dlyMix",        "Delay",       K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::dlySync,       "dlySync",       "Sync",        K::Bool, 0, 1, 1, 0, U::None, nullptr },
        { P::dlyDiv,        "dlyDiv",        "Time",        K::Choice, 0, 13, 7, 0, U::None, &dd::divisionNames() },
        { P::dlyMs,         "dlyMs",         "Time ms",     K::Float, 20, 1500, 140, 200, U::Ms, nullptr },
        { P::dlyFeedback,   "dlyFeedback",   "Repeats",     K::Float, 0, 0.95f, 0.3f, 0, U::Percent, nullptr },
        { P::dlyTone,       "dlyTone",       "Dly Tone",    K::Float, 0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::dlyPingPong,   "dlyPingPong",   "Ping Pong",   K::Bool, 0, 1, 0, 0, U::None, nullptr },
        { P::dlyDuck,       "dlyDuck",       "Duck",        K::Float, 0, 1, 0.3f, 0, U::Percent, nullptr },

        { P::revMix,        "revMix",        "Reverb",      K::Float, 0, 1, 0.12f, 0, U::Percent, nullptr },
        { P::revType,       "revType",       "Space",       K::Choice, 0, 4, 0, 0, U::None, &spaceNames() },
        { P::revSize,       "revSize",       "Size",        K::Float, 0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::revPreDelay,   "revPreDelay",   "Pre-Delay",   K::Float, 0, 200, 20, 40, U::Ms, nullptr },
        { P::revTone,       "revTone",       "Rev Tone",    K::Float, 0, 1, 0.6f, 0, U::Percent, nullptr },

        { P::mix,           "mix",           "Mix",         K::Float, 0, 1, 1, 0, U::Percent, nullptr },
        { P::outGain,       "outGain",       "Output",      K::Float, -24, 12, 0, 0, U::Db, nullptr },
        { P::b1On,     "b1On",     "Band 1 On",     K::Bool, 0, 1, 1, 0, U::None, nullptr },
        { P::b1Type,   "b1Type",   "Band 1 Type",   K::Choice, 0, 7, 3, 0, U::None, &bandTypeNames() },
        { P::b1Freq,   "b1Freq",   "Band 1 Freq",   K::Float, 20, 20000, 80, 1000, U::Hz, nullptr },
        { P::b1Gain,   "b1Gain",   "Band 1 Gain",   K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b1Q,      "b1Q",      "Band 1 Q",      K::Float, 0.1f, 18, 0.71f, 1, U::Q, nullptr },
        { P::b1Slope,  "b1Slope",  "Band 1 Slope",  K::Choice, 0, 3, 1, 0, U::None, &slopeNames() },
        { P::b1Dyn,    "b1Dyn",    "Band 1 Dynamic",K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b1Thresh, "b1Thresh", "Band 1 Threshold", K::Float, -60, 0, -30, 0, U::Db, nullptr },
        { P::b1Place,  "b1Place",  "Band 1 Stereo", K::Choice, 0, 2, 0, 0, U::None, &placementNames() },
        { P::b2On,     "b2On",     "Band 2 On",     K::Bool, 0, 1, 0, 0, U::None, nullptr },
        { P::b2Type,   "b2Type",   "Band 2 Type",   K::Choice, 0, 7, 1, 0, U::None, &bandTypeNames() },
        { P::b2Freq,   "b2Freq",   "Band 2 Freq",   K::Float, 20, 20000, 150, 1000, U::Hz, nullptr },
        { P::b2Gain,   "b2Gain",   "Band 2 Gain",   K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b2Q,      "b2Q",      "Band 2 Q",      K::Float, 0.1f, 18, 0.71f, 1, U::Q, nullptr },
        { P::b2Slope,  "b2Slope",  "Band 2 Slope",  K::Choice, 0, 3, 1, 0, U::None, &slopeNames() },
        { P::b2Dyn,    "b2Dyn",    "Band 2 Dynamic",K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b2Thresh, "b2Thresh", "Band 2 Threshold", K::Float, -60, 0, -30, 0, U::Db, nullptr },
        { P::b2Place,  "b2Place",  "Band 2 Stereo", K::Choice, 0, 2, 0, 0, U::None, &placementNames() },
        { P::b3On,     "b3On",     "Band 3 On",     K::Bool, 0, 1, 0, 0, U::None, nullptr },
        { P::b3Type,   "b3Type",   "Band 3 Type",   K::Choice, 0, 7, 0, 0, U::None, &bandTypeNames() },
        { P::b3Freq,   "b3Freq",   "Band 3 Freq",   K::Float, 20, 20000, 300, 1000, U::Hz, nullptr },
        { P::b3Gain,   "b3Gain",   "Band 3 Gain",   K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b3Q,      "b3Q",      "Band 3 Q",      K::Float, 0.1f, 18, 1.0f, 1, U::Q, nullptr },
        { P::b3Slope,  "b3Slope",  "Band 3 Slope",  K::Choice, 0, 3, 1, 0, U::None, &slopeNames() },
        { P::b3Dyn,    "b3Dyn",    "Band 3 Dynamic",K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b3Thresh, "b3Thresh", "Band 3 Threshold", K::Float, -60, 0, -30, 0, U::Db, nullptr },
        { P::b3Place,  "b3Place",  "Band 3 Stereo", K::Choice, 0, 2, 0, 0, U::None, &placementNames() },
        { P::b4On,     "b4On",     "Band 4 On",     K::Bool, 0, 1, 0, 0, U::None, nullptr },
        { P::b4Type,   "b4Type",   "Band 4 Type",   K::Choice, 0, 7, 0, 0, U::None, &bandTypeNames() },
        { P::b4Freq,   "b4Freq",   "Band 4 Freq",   K::Float, 20, 20000, 1500, 1000, U::Hz, nullptr },
        { P::b4Gain,   "b4Gain",   "Band 4 Gain",   K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b4Q,      "b4Q",      "Band 4 Q",      K::Float, 0.1f, 18, 1.0f, 1, U::Q, nullptr },
        { P::b4Slope,  "b4Slope",  "Band 4 Slope",  K::Choice, 0, 3, 1, 0, U::None, &slopeNames() },
        { P::b4Dyn,    "b4Dyn",    "Band 4 Dynamic",K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b4Thresh, "b4Thresh", "Band 4 Threshold", K::Float, -60, 0, -30, 0, U::Db, nullptr },
        { P::b4Place,  "b4Place",  "Band 4 Stereo", K::Choice, 0, 2, 0, 0, U::None, &placementNames() },
        { P::b5On,     "b5On",     "Band 5 On",     K::Bool, 0, 1, 0, 0, U::None, nullptr },
        { P::b5Type,   "b5Type",   "Band 5 Type",   K::Choice, 0, 7, 0, 0, U::None, &bandTypeNames() },
        { P::b5Freq,   "b5Freq",   "Band 5 Freq",   K::Float, 20, 20000, 4500, 1000, U::Hz, nullptr },
        { P::b5Gain,   "b5Gain",   "Band 5 Gain",   K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b5Q,      "b5Q",      "Band 5 Q",      K::Float, 0.1f, 18, 1.0f, 1, U::Q, nullptr },
        { P::b5Slope,  "b5Slope",  "Band 5 Slope",  K::Choice, 0, 3, 1, 0, U::None, &slopeNames() },
        { P::b5Dyn,    "b5Dyn",    "Band 5 Dynamic",K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b5Thresh, "b5Thresh", "Band 5 Threshold", K::Float, -60, 0, -30, 0, U::Db, nullptr },
        { P::b5Place,  "b5Place",  "Band 5 Stereo", K::Choice, 0, 2, 0, 0, U::None, &placementNames() },
        { P::b6On,     "b6On",     "Band 6 On",     K::Bool, 0, 1, 0, 0, U::None, nullptr },
        { P::b6Type,   "b6Type",   "Band 6 Type",   K::Choice, 0, 7, 2, 0, U::None, &bandTypeNames() },
        { P::b6Freq,   "b6Freq",   "Band 6 Freq",   K::Float, 20, 20000, 10000, 1000, U::Hz, nullptr },
        { P::b6Gain,   "b6Gain",   "Band 6 Gain",   K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b6Q,      "b6Q",      "Band 6 Q",      K::Float, 0.1f, 18, 0.71f, 1, U::Q, nullptr },
        { P::b6Slope,  "b6Slope",  "Band 6 Slope",  K::Choice, 0, 3, 1, 0, U::None, &slopeNames() },
        { P::b6Dyn,    "b6Dyn",    "Band 6 Dynamic",K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b6Thresh, "b6Thresh", "Band 6 Threshold", K::Float, -60, 0, -30, 0, U::Db, nullptr },
        { P::b6Place,  "b6Place",  "Band 6 Stereo", K::Choice, 0, 2, 0, 0, U::None, &placementNames() },
        { P::b7On,     "b7On",     "Band 7 On",     K::Bool, 0, 1, 0, 0, U::None, nullptr },
        { P::b7Type,   "b7Type",   "Band 7 Type",   K::Choice, 0, 7, 4, 0, U::None, &bandTypeNames() },
        { P::b7Freq,   "b7Freq",   "Band 7 Freq",   K::Float, 20, 20000, 18000, 1000, U::Hz, nullptr },
        { P::b7Gain,   "b7Gain",   "Band 7 Gain",   K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b7Q,      "b7Q",      "Band 7 Q",      K::Float, 0.1f, 18, 0.71f, 1, U::Q, nullptr },
        { P::b7Slope,  "b7Slope",  "Band 7 Slope",  K::Choice, 0, 3, 1, 0, U::None, &slopeNames() },
        { P::b7Dyn,    "b7Dyn",    "Band 7 Dynamic",K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b7Thresh, "b7Thresh", "Band 7 Threshold", K::Float, -60, 0, -30, 0, U::Db, nullptr },
        { P::b7Place,  "b7Place",  "Band 7 Stereo", K::Choice, 0, 2, 0, 0, U::None, &placementNames() },
        { P::b8On,     "b8On",     "Band 8 On",     K::Bool, 0, 1, 0, 0, U::None, nullptr },
        { P::b8Type,   "b8Type",   "Band 8 Type",   K::Choice, 0, 7, 0, 0, U::None, &bandTypeNames() },
        { P::b8Freq,   "b8Freq",   "Band 8 Freq",   K::Float, 20, 20000, 8000, 1000, U::Hz, nullptr },
        { P::b8Gain,   "b8Gain",   "Band 8 Gain",   K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b8Q,      "b8Q",      "Band 8 Q",      K::Float, 0.1f, 18, 1.0f, 1, U::Q, nullptr },
        { P::b8Slope,  "b8Slope",  "Band 8 Slope",  K::Choice, 0, 3, 1, 0, U::None, &slopeNames() },
        { P::b8Dyn,    "b8Dyn",    "Band 8 Dynamic",K::Float, -24, 24, 0, 0, U::Db, nullptr },
        { P::b8Thresh, "b8Thresh", "Band 8 Threshold", K::Float, -60, 0, -30, 0, U::Db, nullptr },
        { P::b8Place,  "b8Place",  "Band 8 Stereo", K::Choice, 0, 2, 0, 0, U::None, &placementNames() },

        { P::preTone,       "preTone",       "Sat Tone",    K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::preMix,        "preMix",        "Sat Mix",     K::Float, 0, 1, 1, 0, U::Percent, nullptr },
        { P::rider,         "rider",         "Rider",       K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::compScHpf,     "compScHpf",     "SC Filter",   K::Float, 20, 300, 80, 80, U::Hz, nullptr },
        { P::smoothDepth,   "smoothDepth",   "Smooth",      K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::smoothSharp,   "smoothSharp",   "Sharpness",   K::Float, 0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::smoothLow,     "smoothLow",     "Focus Low",   K::Float, 200, 8000, 1500, 1500, U::Hz, nullptr },
        { P::smoothHigh,    "smoothHigh",    "Focus High",  K::Float, 2000, 20000, 12000, 8000, U::Hz, nullptr },
        { P::airMid,        "airMid",        "Mid Air",     K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::airHigh,       "airHigh",       "High Air",    K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::chRate,        "chRate",        "Chorus Rate", K::Float, 0.05f, 5, 0.8f, 0.8f, U::Hz, nullptr },
        { P::chDepth,       "chDepth",       "Chorus Depth",K::Float, 0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::chMix,         "chMix",         "Chorus",      K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::dimMode,       "dimMode",       "Dimension",   K::Choice, 0, 4, 0, 0, U::None, &dimensionModeNames() },
        { P::dimMix,        "dimMix",        "Dim Mix",     K::Float, 0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::limit,         "limit",         "Limit",       K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::eqPost,        "eqPost",        "EQ Post",     K::Bool, 0, 1, 0, 0, U::None, nullptr },
    }};
    return t;
}

juce::String formatValue (Unit unit, float v)
{
    switch (unit)
    {
        case U::Percent: return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        case U::Hz:      return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : (v < 10.0f ? juce::String (v, 2) : juce::String (juce::roundToInt (v))) + " Hz";
        case U::Ms:      return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms";
        case U::Db:      return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
        case U::Semis:   return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " st";
        case U::SemisFine: return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " st";
        case U::Ratio:   return juce::String (v, v < 10.0f ? 1 : 0) + ":1";
        case U::Q:       return juce::String (v, 2);
        case U::Bipolar: return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f));
        case U::None:    break;
    }
    return juce::String (v, 2);
}
} // namespace

const char* id (P p)
{
    jassert (table()[(size_t) p].param == p); // table order must match the enum
    return table()[(size_t) p].id;
}

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
                layout.add (std::make_unique<juce::AudioParameterFloat> (pid, s.name, range, s.def,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction ([unit] (float v, int) { return formatValue (unit, v); })));
                break;
            }
            case K::Int:
                layout.add (std::make_unique<juce::AudioParameterInt> (pid, s.name, (int) s.min, (int) s.max, (int) s.def));
                break;
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
} // namespace aircheck
