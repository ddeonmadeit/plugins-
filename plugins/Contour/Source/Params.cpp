#include "Params.h"

#include "Model.h"

namespace contour
{
namespace
{
using K = Kind;
using U = Unit;

const std::array<ParamSpec, (size_t) kNumParams>& table()
{
    static const std::array<ParamSpec, (size_t) kNumParams> t {{
        { P::center,     "center",     "Pitch Center", K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::drift,      "drift",      "Pitch Drift",  K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::vibrato,    "vibrato",    "Vibrato",      K::Float, 0, 2, 1, 0, U::Percent, nullptr },
        { P::transition, "transition", "Transition",   K::Float, 2, 250, 60, 50, U::Ms, nullptr },
        { P::formant,    "formant",    "Formant",      K::Float, -6, 6, 0, 0, U::SemisFine, nullptr },
        { P::transpose,  "transpose",  "Transpose",    K::Int, -12, 12, 0, 0, U::Semis, nullptr },
        { P::outGain,    "outGain",    "Output",       K::Float, -24, 12, 0, 0, U::Db, nullptr },
        { P::perfKey,    "perfKey",    "Key",          K::Choice, 0, 11, 0, 0, U::None, &keyNames() },
        { P::perfScale,  "perfScale",  "Scale",        K::Choice, 0, 8, 0, 0, U::None, &scaleNames() },
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
        case U::SemisFine: return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " st";
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
            {
                const auto unit = s.unit;
                layout.add (std::make_unique<juce::AudioParameterInt> (pid, s.name, (int) s.min, (int) s.max, (int) s.def,
                    juce::AudioParameterIntAttributes().withStringFromValueFunction ([unit] (int v, int) { return formatValue (unit, (float) v); })));
            }
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
} // namespace contour
