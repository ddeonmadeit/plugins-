#include "Params.h"

#include <dd_dsp/dd_dsp.h>

namespace sg
{
namespace choices
{
    const juce::StringArray& voiceModes()  { static const juce::StringArray c { "Mono", "Poly" }; return c; }
    const juce::StringArray& glideModes()  { static const juce::StringArray c { "Overlap", "Always" }; return c; }
    const juce::StringArray& filterModes() { static const juce::StringArray c { "Ladder", "LP 12", "BP 12", "HP 12" }; return c; }
}

namespace
{
using K = Kind;
using U = Unit;

const std::array<ParamSpec, (size_t) kNumParams>& table()
{
    static const std::array<ParamSpec, (size_t) kNumParams> t {{
        { P::voiceMode,  "voiceMode",  "Voice Mode",   K::Choice, 0, 1, 0, 0, U::None, &choices::voiceModes() },
        { P::legato,     "legato",     "Legato",       K::Bool,   0, 1, 1, 0, U::None, nullptr },
        { P::glide,      "glide",      "Glide",        K::Float,  0, 1500, 0, 150, U::Ms, nullptr },
        { P::glideMode,  "glideMode",  "Glide Mode",   K::Choice, 0, 1, 0, 0, U::None, &choices::glideModes() },
        { P::octave,     "octave",     "Octave",       K::Int,   -3, 2, 0, 0, U::None, nullptr },
        { P::tune,       "tune",       "Tune",         K::Int,  -12, 12, 0, 0, U::Semis, nullptr },
        { P::fine,       "fine",       "Fine",         K::Float, -100, 100, 0, 0, U::Cents, nullptr },
        { P::bendRange,  "bendRange",  "Bend Range",   K::Int,    0, 24, 2, 0, U::Semis, nullptr },
        { P::velocity,   "velocity",   "Velocity",     K::Float,  0, 1, 0.4f, 0, U::Percent, nullptr },
        { P::drift,      "drift",      "Drift",        K::Float,  0, 1, 0.15f, 0, U::Percent, nullptr },

        { P::bodyLevel,  "bodyLevel",  "808 Level",    K::Float,  0, 1, 0.85f, 0, U::Percent, nullptr },
        { P::bodyDrive,  "bodyDrive",  "808 Warmth",   K::Float,  0, 1, 0.15f, 0, U::Percent, nullptr },
        { P::pitchAmt,   "pitchAmt",   "Punch",        K::Float, -24, 48, 9, 0, U::Semis, nullptr },
        { P::pitchTime,  "pitchTime",  "Punch Time",   K::Float,  1, 1000, 45, 80, U::Ms, nullptr },
        { P::clickLevel, "clickLevel", "Knock",        K::Float,  0, 1, 0.15f, 0, U::Percent, nullptr },
        { P::clickTone,  "clickTone",  "Knock Tone",   K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },

        { P::oscALevel,  "oscALevel",  "Osc A Level",  K::Float,  0, 1, 0, 0, U::Percent, nullptr },
        { P::oscAWave,   "oscAWave",   "Osc A Wave",   K::Choice, 0, 5, 2, 0, U::None, &dd::waveNames() },
        { P::oscAOct,    "oscAOct",    "Osc A Octave", K::Int,   -2, 2, 0, 0, U::None, nullptr },
        { P::oscASemi,   "oscASemi",   "Osc A Semi",   K::Int,  -12, 12, 0, 0, U::Semis, nullptr },
        { P::oscAFine,   "oscAFine",   "Osc A Fine",   K::Float, -50, 50, 0, 0, U::Cents, nullptr },
        { P::oscAVoices, "oscAVoices", "Osc A Voices", K::Int,    1, 7, 1, 0, U::None, nullptr },
        { P::oscADetune, "oscADetune", "Osc A Detune", K::Float,  0, 100, 18, 0, U::Cents, nullptr },
        { P::oscAWidth,  "oscAWidth",  "Osc A Width",  K::Float,  0, 1, 0.6f, 0, U::Percent, nullptr },

        { P::oscBLevel,  "oscBLevel",  "Osc B Level",  K::Float,  0, 1, 0, 0, U::Percent, nullptr },
        { P::oscBWave,   "oscBWave",   "Osc B Wave",   K::Choice, 0, 5, 3, 0, U::None, &dd::waveNames() },
        { P::oscBOct,    "oscBOct",    "Osc B Octave", K::Int,   -2, 2, 0, 0, U::None, nullptr },
        { P::oscBSemi,   "oscBSemi",   "Osc B Semi",   K::Int,  -12, 12, 0, 0, U::Semis, nullptr },
        { P::oscBFine,   "oscBFine",   "Osc B Fine",   K::Float, -50, 50, 0, 0, U::Cents, nullptr },
        { P::oscBVoices, "oscBVoices", "Osc B Voices", K::Int,    1, 7, 1, 0, U::None, nullptr },
        { P::oscBDetune, "oscBDetune", "Osc B Detune", K::Float,  0, 100, 18, 0, U::Cents, nullptr },
        { P::oscBWidth,  "oscBWidth",  "Osc B Width",  K::Float,  0, 1, 0.6f, 0, U::Percent, nullptr },

        { P::noiseLevel, "noiseLevel", "Noise",        K::Float,  0, 1, 0, 0, U::Percent, nullptr },
        { P::noiseColor, "noiseColor", "Noise Color",  K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },

        { P::filterMode, "filterMode", "Filter Mode",  K::Choice, 0, 3, 0, 0, U::None, &choices::filterModes() },
        { P::cutoff,     "cutoff",     "Cutoff",       K::Float, 20, 20000, 3000, 800, U::Hz, nullptr },
        { P::resonance,  "resonance",  "Resonance",    K::Float,  0, 1, 0.15f, 0, U::Percent, nullptr },
        { P::filterDrive,"filterDrive","Filter Drive", K::Float,  0, 1, 0.2f, 0, U::Percent, nullptr },
        { P::filterEnv,  "filterEnv",  "Env Amount",   K::Float, -6, 6, 0, 0, U::Octaves, nullptr },
        { P::keyTrack,   "keyTrack",   "Key Track",    K::Float,  0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::bodyToFilter,"bodyToFilter","808 Thru Filter", K::Bool, 0, 1, 0, 0, U::None, nullptr },

        { P::fAttack,    "fAttack",    "Filter Attack",  K::Float, 0.1f, 5000, 1, 100, U::Ms, nullptr },
        { P::fDecay,     "fDecay",     "Filter Decay",   K::Float, 5, 10000, 400, 600, U::Ms, nullptr },
        { P::fSustain,   "fSustain",   "Filter Sustain", K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::fRelease,   "fRelease",   "Filter Release", K::Float, 5, 8000, 250, 600, U::Ms, nullptr },

        { P::aAttack,    "aAttack",    "Attack",       K::Float, 0.1f, 5000, 0.5f, 100, U::Ms, nullptr },
        { P::aDecay,     "aDecay",     "Decay",        K::Float, 5, 12000, 2500, 800, U::Ms, nullptr },
        { P::aSustain,   "aSustain",   "Sustain",      K::Float, 0, 1, 0.6f, 0, U::Percent, nullptr },
        { P::aRelease,   "aRelease",   "Release",      K::Float, 5, 8000, 180, 600, U::Ms, nullptr },

        { P::diveAmt,    "diveAmt",    "Dive",         K::Float, -24, 0, 0, 0, U::Semis, nullptr },
        { P::diveTime,   "diveTime",   "Dive Time",    K::Float, 10, 3000, 400, 400, U::Ms, nullptr },

        { P::lfoShape,   "lfoShape",   "LFO Shape",    K::Choice, 0, 4, 0, 0, U::None, &dd::Lfo::shapeNames() },
        { P::lfoSync,    "lfoSync",    "LFO Sync",     K::Bool,   0, 1, 0, 0, U::None, nullptr },
        { P::lfoRate,    "lfoRate",    "LFO Rate",     K::Float, 0.05f, 20, 3, 2, U::Hz, nullptr },
        { P::lfoDiv,     "lfoDiv",     "LFO Division", K::Choice, 0, 13, 7, 0, U::None, &dd::divisionNames() },
        { P::lfoFade,    "lfoFade",    "LFO Fade In",  K::Float, 0, 3000, 0, 500, U::Ms, nullptr },
        { P::lfoPitch,   "lfoPitch",   "LFO > Pitch",  K::Float, 0, 12, 0, 1, U::Semis, nullptr },
        { P::lfoCutoff,  "lfoCutoff",  "LFO > Cutoff", K::Float, -4, 4, 0, 0, U::Octaves, nullptr },
        { P::lfoAmp,     "lfoAmp",     "LFO > Amp",    K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::lfoDirt,    "lfoDirt",    "LFO > Dirt",   K::Float, 0, 1, 0, 0, U::Percent, nullptr },

        { P::dirtType,   "dirtType",   "Dirt Type",    K::Choice, 0, 7, 0, 0, U::None, &dd::dirtTypeNames() },
        { P::dirtDrive,  "dirtDrive",  "Drive",        K::Float, 0, 1, 0.3f, 0, U::Percent, nullptr },
        { P::dirtTone,   "dirtTone",   "Dirt Tone",    K::Float, -1, 1, 0, 0, U::Bipolar, nullptr },
        { P::dirtBias,   "dirtBias",   "Character",    K::Float, 0, 1, 0.2f, 0, U::Percent, nullptr },
        { P::dirtMix,    "dirtMix",    "Dirt Mix",     K::Float, 0, 1, 1, 0, U::Percent, nullptr },
        { P::subKeep,    "subKeep",    "Sub Keep",     K::Float, 0, 1, 0.6f, 0, U::Percent, nullptr },
        { P::xover,      "xover",      "Sub X-Over",   K::Float, 40, 400, 120, 120, U::Hz, nullptr },

        { P::eqSub,      "eqSub",      "Sub Boost",    K::Float, -12, 12, 0, 0, U::Db, nullptr },
        { P::eqMidFreq,  "eqMidFreq",  "Mid Freq",     K::Float, 150, 6000, 900, 900, U::Hz, nullptr },
        { P::eqMidGain,  "eqMidGain",  "Mid Gain",     K::Float, -12, 12, 0, 0, U::Db, nullptr },
        { P::eqHighCut,  "eqHighCut",  "High Cut",     K::Float, 500, 20000, 20000, 5000, U::Hz, nullptr },

        { P::chorusMix,  "chorusMix",  "Chorus Mix",   K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::chorusRate, "chorusRate", "Chorus Rate",  K::Float, 0.05f, 5, 0.5f, 0.8f, U::Hz, nullptr },
        { P::chorusDepth,"chorusDepth","Chorus Depth", K::Float, 0, 1, 0.5f, 0, U::Percent, nullptr },

        { P::delayMix,   "delayMix",   "Delay Mix",    K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::delayDiv,   "delayDiv",   "Delay Time",   K::Choice, 0, 13, 8, 0, U::None, &dd::divisionNames() },
        { P::delayFeedback,"delayFeedback","Feedback", K::Float, 0, 0.95f, 0.35f, 0, U::Percent, nullptr },
        { P::delayTone,  "delayTone",  "Delay Tone",   K::Float, 0, 1, 0.5f, 0, U::Percent, nullptr },
        { P::delayPingPong,"delayPingPong","Ping Pong", K::Bool, 0, 1, 1, 0, U::None, nullptr },

        { P::spaceMix,   "spaceMix",   "Space Mix",    K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::spaceSize,  "spaceSize",  "Space Size",   K::Float, 0, 1, 0.5f, 0, U::Percent, nullptr },

        { P::glue,       "glue",       "Glue",         K::Float, 0, 1, 0, 0, U::Percent, nullptr },
        { P::clip,       "clip",       "Clip",         K::Float, 0, 1, 0.2f, 0, U::Percent, nullptr },
        { P::width,      "width",      "Width",        K::Float, 0, 1.5f, 1, 0, U::Percent, nullptr },
        { P::outGain,    "outGain",    "Output",       K::Float, -24, 12, 0, 0, U::Db, nullptr },
        { P::perfPoly,   "perfPoly",   "Poly Keys",    K::Bool,   0, 1, 1, 0, U::None, nullptr },
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
} // namespace sg
