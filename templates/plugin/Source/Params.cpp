#include "Params.h"

#include <dd_dsp/dd_dsp.h>

namespace __NS__
{
namespace
{
using K = Kind;
using U = Unit;

const std::array<ParamSpec, (size_t) kNumParams>& table()
{
    static const std::array<ParamSpec, (size_t) kNumParams> t {{
//@SYNTH
        { P::level,     "level",     "Level",     K::Float, 0, 1, 0.8f, 0, U::Percent, nullptr },
        { P::attack,    "attack",    "Attack",    K::Float, 0.1f, 5000, 2, 100, U::Ms, nullptr },
        { P::decay,     "decay",     "Decay",     K::Float, 5, 10000, 800, 600, U::Ms, nullptr },
        { P::sustain,   "sustain",   "Sustain",   K::Float, 0, 1, 0.6f, 0, U::Percent, nullptr },
        { P::release,   "release",   "Release",   K::Float, 5, 8000, 300, 600, U::Ms, nullptr },
        { P::wave,      "wave",      "Wave",      K::Choice, 0, 5, 2, 0, U::None, &dd::waveNames() },
        { P::cutoff,    "cutoff",    "Cutoff",    K::Float, 20, 20000, 4000, 800, U::Hz, nullptr },
        { P::resonance, "resonance", "Resonance", K::Float, 0, 1, 0.2f, 0, U::Percent, nullptr },
//@END
//@EFFECT
        { P::dirtType,  "dirtType",  "Type",      K::Choice, 0, 7, 0, 0, U::None, &dd::dirtTypeNames() },
        { P::drive,     "drive",     "Drive",     K::Float, 0, 1, 0.3f, 0, U::Percent, nullptr },
        { P::tone,      "tone",      "Tone",      K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::mix,       "mix",       "Mix",       K::Float, 0, 1, 1, 0, U::Percent, nullptr },
//@END
        { P::outGain,   "outGain",   "Output",    K::Float, -24, 12, 0, 0, U::Db, nullptr },
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
} // namespace __NS__
