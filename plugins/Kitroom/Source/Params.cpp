#include "Params.h"

#include <dd_dsp/dd_dsp.h>

namespace kitroom
{
namespace
{
const char* slotSuffix (SP p)
{
    switch (p)
    {
        case SP::Volume:    return "Volume";
        case SP::Pan:       return "Pan";
        case SP::Tune:      return "Tune";
        case SP::Decay:     return "Decay";
        case SP::Close:     return "Close";
        case SP::Overheads: return "Overheads";
        case SP::Room:      return "Room";
        case SP::count:     break;
    }
    return "";
}

const char* masterId (MP p)
{
    switch (p)
    {
        case MP::Close:     return "masterClose";
        case MP::Overheads: return "masterOverheads";
        case MP::Room:      return "masterRoom";
        case MP::VelCurve:  return "velCurve";
        case MP::Humanize:  return "humanize";
        case MP::Glue:      return "glue";
        case MP::Clip:      return "clip";
        case MP::Width:     return "width";
        case MP::Output:    return "outGain";
        case MP::Drive:     return "drive";
        case MP::DriveType: return "driveType";
        case MP::Crush:     return "crush";
        case MP::Tone:      return "tone";
        case MP::count:     break;
    }
    return "";
}

juce::String percent (float v, int)   { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
juce::String decibels (float v, int)  { return v <= -47.9f ? juce::String ("-inf") : (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }
juce::String semis (float v, int)     { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " st"; }
juce::String panText (float v, int)
{
    const int p = juce::roundToInt (v * 100.0f);
    // 0 = the close mic follows the piece's place in the overheads.
    return p == 0 ? juce::String ("Auto") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
}
juce::String decayText (float v, int) { return v >= 0.995f ? juce::String ("Natural") : juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

std::unique_ptr<juce::AudioParameterFloat> makeFloat (const juce::String& id, const juce::String& name,
                                                      juce::NormalisableRange<float> range, float def,
                                                      std::function<juce::String (float, int)> text)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text)));
}
} // namespace

juce::String slotParamId (int slot, SP p)  { return juce::String (kr::slots()[(size_t) slot].id) + slotSuffix (p); }
juce::String masterParamId (MP p)          { return masterId (p); }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int s = 0; s < kr::kNumSlots; ++s)
    {
        const juce::String n = kr::slots()[(size_t) s].name;
        juce::NormalisableRange<float> vol (-48.0f, 6.0f);
        vol.setSkewForCentre (-12.0f);
        layout.add (makeFloat (slotParamId (s, SP::Volume), n + " Volume", vol, 0.0f, decibels));
        layout.add (makeFloat (slotParamId (s, SP::Pan), n + " Pan", { -1.0f, 1.0f }, 0.0f, panText));
        layout.add (makeFloat (slotParamId (s, SP::Tune), n + " Tune", { -12.0f, 12.0f }, 0.0f, semis));
        layout.add (makeFloat (slotParamId (s, SP::Decay), n + " Decay", { 0.0f, 1.0f }, 1.0f, decayText));
        layout.add (makeFloat (slotParamId (s, SP::Close), n + " Close Mic", { 0.0f, 1.5f }, 1.0f, percent));
        layout.add (makeFloat (slotParamId (s, SP::Overheads), n + " Overheads", { 0.0f, 1.5f }, 1.0f, percent));
        layout.add (makeFloat (slotParamId (s, SP::Room), n + " Room", { 0.0f, 1.5f }, 1.0f, percent));
    }

    layout.add (makeFloat (masterId (MP::Close), "Close Mics", { 0.0f, 1.5f }, 1.0f, percent));
    layout.add (makeFloat (masterId (MP::Overheads), "Overheads", { 0.0f, 1.5f }, 0.8f, percent));
    layout.add (makeFloat (masterId (MP::Room), "Room", { 0.0f, 1.5f }, 0.6f, percent));
    layout.add (makeFloat (masterId (MP::VelCurve), "Velocity Curve", { -1.0f, 1.0f }, 0.0f,
                           [] (float v, int) { return std::abs (v) < 0.01f ? juce::String ("Linear") : (v > 0.0f ? "Hard " : "Soft ") + juce::String (juce::roundToInt (std::abs (v) * 100.0f)); }));
    layout.add (makeFloat (masterId (MP::Humanize), "Humanize", { 0.0f, 1.0f }, 0.15f, percent));
    layout.add (makeFloat (masterId (MP::Glue), "Glue", { 0.0f, 1.0f }, 0.0f, percent));
    layout.add (makeFloat (masterId (MP::Clip), "Clip", { 0.0f, 1.0f }, 0.0f, percent));
    layout.add (makeFloat (masterId (MP::Width), "Width", { 0.0f, 1.5f }, 1.0f, percent));
    layout.add (makeFloat (masterId (MP::Output), "Output", { -24.0f, 12.0f }, 0.0f, decibels));
    layout.add (makeFloat (masterId (MP::Drive), "Drive", { 0.0f, 1.0f }, 0.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { masterId (MP::DriveType), 1 }, "Drive Type",
                                                              dd::dirtTypeNames(), (int) dd::DirtType::Tube));
    layout.add (makeFloat (masterId (MP::Crush), "Crush", { 0.0f, 1.0f }, 0.0f, percent));
    layout.add (makeFloat (masterId (MP::Tone), "Tone", { -1.0f, 1.0f }, 0.0f,
                           [] (float v, int) { return std::abs (v) < 0.01f ? juce::String ("Flat") : (v > 0.0f ? "Bright " : "Dark ") + juce::String (juce::roundToInt (std::abs (v) * 100.0f)); }));
    return layout;
}

void ParamCache::attach (juce::AudioProcessorValueTreeState& state)
{
    for (int s = 0; s < kr::kNumSlots; ++s)
        for (int p = 0; p < kSlotParams; ++p)
        {
            auto* v = state.getRawParameterValue (slotParamId (s, (SP) p));
            jassert (v != nullptr);
            slotValues[(size_t) (s * kSlotParams + p)] = v;
        }
    for (int p = 0; p < kMasterParams; ++p)
    {
        auto* v = state.getRawParameterValue (masterParamId ((MP) p));
        jassert (v != nullptr);
        masterValues[(size_t) p] = v;
    }
}
} // namespace kitroom
