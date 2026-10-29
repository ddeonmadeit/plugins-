#include "Params.h"

#include <dd_dsp/dd_dsp.h>

namespace sunburst
{
namespace choices
{
    const juce::StringArray& guitars()     { static const juce::StringArray c { "Acoustic", "Nylon", "Jazz Box", "Solidbody", "Orange Hollow", "Club Hollow" }; return c; }
    const juce::StringArray& guitarIds()   { static const juce::StringArray c { "acoustic", "nylon", "jazz", "sg", "gretsch", "hofner" }; return c; }
    const juce::StringArray& legatoModes() { static const juce::StringArray c { "Off", "Hammer", "Slide" }; return c; }
    const juce::StringArray& ampModels()   { static const juce::StringArray c { "Clean", "Crunch", "Lead" }; return c; }
}

namespace
{
using K = Kind;
using U = Unit;

const std::array<ParamSpec, (size_t) kNumParams>& table()
{
    static const std::array<ParamSpec, (size_t) kNumParams> t {{
        { P::guitar,        "guitar",        "Guitar",          K::Choice, 0, 5, 0, 0, U::None, &choices::guitars() },
        { P::stringNoise,   "stringNoise",   "String Noise",    K::Float,  0, 1.5f, 0.7f, 0, U::Percent, nullptr },
        { P::pick,          "pick",          "Pick",            K::Float,  0, 1, 0.6f, 0, U::Percent, nullptr },
        { P::mute,          "mute",          "Palm Mute",       K::Float,  0, 1, 0, 0, U::Percent, nullptr },
        { P::tone,          "tone",          "Tone",            K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::ring,          "ring",          "Ring",            K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::strum,         "strum",         "Strum",           K::Float,  0, 80, 0, 0, U::Ms, nullptr },
        { P::legato,        "legato",        "Legato",          K::Choice, 0, 2, 0, 0, U::None, &choices::legatoModes() },
        { P::slideTime,     "slideTime",     "Slide Time",      K::Float, 10, 500, 90, 90, U::Ms, nullptr },
        { P::vibrato,       "vibrato",       "Vibrato",         K::Float,  0, 1, 0, 0, U::Percent, nullptr },
        { P::humanize,      "humanize",      "Humanize",        K::Float,  0, 1, 0.3f, 0, U::Percent, nullptr },
        { P::velCurve,      "velCurve",      "Velocity",        K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::bendRange,     "bendRange",     "Bend Range",      K::Int,    1, 12, 2, 0, U::Semis, nullptr },
        { P::transpose,     "transpose",     "Transpose",       K::Int,  -12, 12, 0, 0, U::Semis, nullptr },
        { P::doubleTrack,   "doubleTrack",   "Double",          K::Float,  0, 1, 0, 0, U::Percent, nullptr },
        { P::body,          "body",          "Body",            K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },

        { P::compOn,        "compOn",        "Comp",            K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::compAmount,    "compAmount",    "Comp Amount",     K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::driveOn,       "driveOn",       "Drive",           K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::driveGain,     "driveGain",     "Drive Gain",      K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::driveTone,     "driveTone",     "Drive Tone",      K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::fuzzOn,        "fuzzOn",        "Fuzz",            K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::fuzzGain,      "fuzzGain",      "Fuzz Gain",       K::Float,  0, 1, 0.6f, 0, U::Percent, nullptr },
        { P::chorusOn,      "chorusOn",      "Chorus",          K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::chorusRate,    "chorusRate",    "Chorus Rate",     K::Float, 0.05f, 6, 0.6f, 0.8f, U::Hz, nullptr },
        { P::chorusDepth,   "chorusDepth",   "Chorus Depth",    K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::tremOn,        "tremOn",        "Tremolo",         K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::tremDiv,       "tremDiv",       "Trem Rate",       K::Choice, 0, 13, 10, 0, U::None, &dd::divisionNames() },
        { P::tremDepth,     "tremDepth",     "Trem Depth",      K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::delayOn,       "delayOn",       "Delay",           K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::delayDiv,      "delayDiv",      "Delay Time",      K::Choice, 0, 13, 8, 0, U::None, &dd::divisionNames() },
        { P::delayFeedback, "delayFeedback", "Delay Feedback",  K::Float,  0, 0.95f, 0.35f, 0, U::Percent, nullptr },
        { P::delayMix,      "delayMix",      "Delay Mix",       K::Float,  0, 1, 0.25f, 0, U::Percent, nullptr },

        { P::ampOn,         "ampOn",         "Amp",             K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::ampModel,      "ampModel",      "Amp Model",       K::Choice, 0, 2, 0, 0, U::None, &choices::ampModels() },
        { P::ampGain,       "ampGain",       "Gain",            K::Float,  0, 1, 0.35f, 0, U::Percent, nullptr },
        { P::ampBass,       "ampBass",       "Bass",            K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::ampMid,        "ampMid",        "Mid",             K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::ampTreble,     "ampTreble",     "Treble",          K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::ampPresence,   "ampPresence",   "Presence",        K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::ampLevel,      "ampLevel",      "Amp Level",       K::Float, -24, 12, 0, 0, U::Db, nullptr },
        { P::cabOn,         "cabOn",         "Cab",             K::Bool,   0, 1, 1, 0, U::None, nullptr },
        { P::cabType,       "cabType",       "Cab Type",        K::Choice, 0, 1, 0, 0, U::None, &dd::CabSim::typeNames() },
        { P::cabMic,        "cabMic",        "Mic",             K::Float,  0, 1, 0.3f, 0, U::Percent, nullptr },
        { P::reverbOn,      "reverbOn",      "Reverb",          K::Bool,   0, 1, 1, 0, U::None, nullptr },
        { P::reverbMix,     "reverbMix",     "Reverb Mix",      K::Float,  0, 1, 0.2f, 0, U::Percent, nullptr },
        { P::reverbSize,    "reverbSize",    "Room Size",       K::Float,  0, 1, 0.45f, 0, U::Percent, nullptr },

        { P::width,         "width",         "Width",           K::Float,  0, 1.5f, 1, 0, U::Percent, nullptr },
        { P::glue,          "glue",          "Glue",            K::Float,  0, 1, 0.2f, 0, U::Percent, nullptr },
        { P::outGain,       "outGain",       "Output",          K::Float, -24, 12, 0, 0, U::Db, nullptr },
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
        case U::Cents:   return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct";
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
} // namespace sunburst
