#include "Params.h"

#include <dd_dsp/dd_dsp.h>

#include "Chain.h"

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
        { P::deessFreq,     "deessFreq",     "Ess Freq",    K::Float, 3000, 12000, 6500, 6000, U::Hz, nullptr },

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
    }};
    return t;
}

juce::String formatValue (Unit unit, float v)
{
    switch (unit)
    {
        case U::Percent: return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        case U::Hz:      return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
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
