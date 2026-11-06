#include "Params.h"

#include <dd_dsp/dd_dsp.h>

#include "Hrtf.h"
#include "Room.h"

namespace orbit
{
namespace
{
using K = Kind;
using U = Unit;

const juce::StringArray& roomChoices()     { static const juce::StringArray s = roomNames(); return s; }
const juce::StringArray& outputChoices()   { static const juce::StringArray s { "Headphones", "Speakers" }; return s; }

const std::array<ParamSpec, (size_t) kNumParams>& table()
{
    static const std::array<ParamSpec, (size_t) kNumParams> t {{
        { P::posX,         "posX",         "Left / Right", K::Float, -1, 1, 0, 0, U::Side, nullptr },
        { P::posY,         "posY",         "Back / Front", K::Float, -1, 1, 0.5f, 0, U::Depth, nullptr },
        { P::posZ,         "posZ",         "Height",       K::Float, 0, 1, 0.4f, 0, U::Percent, nullptr },
        { P::width,        "width",        "Width",        K::Float, 0, 1, 0.3f, 0, U::Percent, nullptr },
        { P::room,         "room",         "Room",         K::Choice, 0, 13, 0, 0, U::None, &roomChoices() },
        { P::size,         "size",         "Size",         K::Float, 0, 1, 0.5f, 0, U::Scale, nullptr },
        { P::roomLevel,    "roomLevel",    "Room Level",   K::Float, 0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::motion,       "motion",       "Motion",       K::Choice, 0, 7, 0, 0, U::None, &motionNames() },
        { P::motionRate,   "motionRate",   "Motion Rate",  K::Choice, 0, 7, 4, 0, U::None, &motionRateNames() },
        { P::motionAmount, "motionAmount", "Travel",       K::Float, 0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::doppler,      "doppler",      "Doppler",      K::Float, 0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::mix,          "mix",          "Mix",          K::Float, 0, 1, 1, 0, U::Percent, nullptr },
        { P::outGain,      "outGain",      "Output",       K::Float, -24, 12, 0, 0, U::Db, nullptr },
        { P::perfOutput,   "perfOutput",   "Listen On",    K::Choice, 0, 1, 0, 0, U::None, &outputChoices() },
        { P::perfHead,     "perfHead",     "Ears",         K::Choice, 0, 3, 0, 0, U::None, &HrtfSet::headNames() },
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
        case U::Side:    return std::abs (v) < 0.005f ? juce::String ("Centre") : (v < 0.0f ? "L " : "R ") + juce::String (juce::roundToInt (std::abs (v) * 100.0f));
        case U::Depth:   return std::abs (v) < 0.005f ? juce::String ("Beside") : (v > 0.0f ? "Front " : "Back ") + juce::String (juce::roundToInt (std::abs (v) * 100.0f));
        case U::Scale:   return juce::String (sizeScale (v), 2) + "x";
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
} // namespace orbit
