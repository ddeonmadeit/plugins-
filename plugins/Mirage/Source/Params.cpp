#include "Params.h"


namespace mirage
{
namespace
{
using K = Kind;
using U = Unit;

const std::array<ParamSpec, (size_t) kNumParams>& table()
{
    static const std::array<ParamSpec, (size_t) kNumParams> t {{
        { P::react,   "react",   "React",   K::Float, 0, 1, 0.65f, 0, U::Percent, nullptr },
        { P::grain,   "grain",   "Grain",   K::Float, 0, 1, 0.7f, 0, U::Percent, nullptr },
        { P::digital, "digital", "Digital", K::Float, 0, 1, 0.6f, 0, U::Percent, nullptr },
        { P::speed,   "speed",   "Speed",   K::Float, 0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::eclipse, "eclipse", "Eclipse", K::Float, 0, 1, 0.25f, 0, U::Percent, nullptr },
        { P::dunes,   "dunes",   "Dunes",   K::Float, 0, 1, 0.6f, 0, U::Percent, nullptr },
        { P::heat,    "heat",    "Heat",    K::Float, 0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::hue,     "hue",     "Hue",     K::Float, -180, 180, 0, 0, U::Deg, nullptr },
        { P::scene,   "scene",   "Scene",   K::Choice, 0, 4, 0, 0, U::None, &sceneNames() },
        { P::style,   "style",   "Style",   K::Choice, 0, 2, 0, 0, U::None, &styleNames() },
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
        case U::Deg:     return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("\xc2\xb0");
        case U::None:    break;
    }
    return juce::String (v, 2);
}
} // namespace

const juce::StringArray& sceneNames()
{
    static const juce::StringArray n { "Dunes", "Burst", "Terrain", "Figure", "Tunnel" };
    return n;
}

const juce::StringArray& styleNames()
{
    static const juce::StringArray n { "Film", "CRT", "VHS" };
    return n;
}

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
} // namespace mirage
