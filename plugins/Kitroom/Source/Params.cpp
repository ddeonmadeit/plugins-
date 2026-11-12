#include "Params.h"
#include "LogDrum.h"
#include "SteelDrum.h"

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
        case SP::Punch:     return "Punch";
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
        case MP::ClapOnSnare: return "clapOnSnare";
        case MP::LayBack:   return "layBack";
        case MP::LogVolume: return "logVolume";
        case MP::LogTune:   return "logTune";
        case MP::LogLength: return "logLength";
        case MP::LogKnock:  return "logKnock";
        case MP::LogTone:   return "logTone";
        case MP::LogDrive:  return "logDrive";
        case MP::LogSub:    return "logSub";
        case MP::LogGlide:  return "logGlide";
        case MP::LogModel:  return "logModel";
        case MP::SteelVolume:  return "steelVolume";
        case MP::SteelTune:    return "steelTune";
        case MP::SteelLength:  return "steelLength";
        case MP::SteelBright:  return "steelBright";
        case MP::SteelShimmer: return "steelShimmer";
        case MP::SteelRoll:    return "steelRoll";
        case MP::SteelModel:   return "steelModel";
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

    auto addSlotParams = [&layout] (int s)
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
    };

    // Parameter order is what hosts show; everything added after 1.11 goes at the end.
    for (int s = 0; s < kr::kNumClassicSlots; ++s)
        addSlotParams (s);

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

    // 1.12: the clap, Punch on every piece, and the clap layered on the snare.
    for (int s = kr::kNumClassicSlots; s < kr::kNumSlots; ++s)
        addSlotParams (s);
    for (int s = 0; s < kr::kNumSlots; ++s)
        layout.add (makeFloat (slotParamId (s, SP::Punch), juce::String (kr::slots()[(size_t) s].name) + " Punch", { 0.0f, 1.0f }, 0.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { masterId (MP::ClapOnSnare), 1 }, "Clap On Snare", false));
    // 1.14: the neo-soul pocket. Snare and clap land this late, hats and ride 40 % of it, the kick stays on the grid.
    layout.add (makeFloat (masterId (MP::LayBack), "Lay Back", { 0.0f, 60.0f }, 0.0f,
                           [] (float v, int) { return v < 0.5f ? juce::String ("On the grid") : juce::String (juce::roundToInt (v)) + " ms late"; }));

    // 1.15: the log drum (Amapiano / Afrobeats), a synthesised piece on its own keys.
    juce::NormalisableRange<float> logVol (-48.0f, 6.0f);
    logVol.setSkewForCentre (-12.0f);
    layout.add (makeFloat (masterId (MP::LogVolume), "Log Drum Volume", logVol, 0.0f, decibels));
    layout.add (makeFloat (masterId (MP::LogTune), "Log Drum Tune", { -12.0f, 12.0f }, 0.0f, semis));
    juce::NormalisableRange<float> logLen (40.0f, 1500.0f);
    logLen.setSkewForCentre (300.0f);
    layout.add (makeFloat (masterId (MP::LogLength), "Log Drum Length", logLen, 380.0f,
                           [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; }));
    layout.add (makeFloat (masterId (MP::LogKnock), "Log Drum Knock", { 0.0f, 1.0f }, 0.6f, percent));
    layout.add (makeFloat (masterId (MP::LogTone), "Log Drum Tone", { 0.0f, 1.0f }, 0.55f, percent));
    layout.add (makeFloat (masterId (MP::LogDrive), "Log Drum Drive", { 0.0f, 1.0f }, 0.3f, percent));
    layout.add (makeFloat (masterId (MP::LogSub), "Log Drum Sub", { 0.0f, 1.0f }, 0.35f, percent));
    juce::NormalisableRange<float> logGlide (0.0f, 400.0f);
    logGlide.setSkewForCentre (100.0f);
    layout.add (makeFloat (masterId (MP::LogGlide), "Log Drum Slide", logGlide, 70.0f,
                           [] (float v, int) { return v < 1.0f ? juce::String ("No slide") : juce::String (juce::roundToInt (v)) + " ms"; }));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { masterId (MP::LogModel), 1 }, "Log Drum Model",
                                                              logModelNames(), 0));

    // 1.16: the steel drum (a Trinidad steel pan), synthesised, on the keys above the log drum's.
    juce::NormalisableRange<float> steelVol (-48.0f, 6.0f);
    steelVol.setSkewForCentre (-12.0f);
    layout.add (makeFloat (masterId (MP::SteelVolume), "Steel Drum Volume", steelVol, 0.0f, decibels));
    layout.add (makeFloat (masterId (MP::SteelTune), "Steel Drum Tune", { -12.0f, 12.0f }, 0.0f, semis));
    juce::NormalisableRange<float> steelLen (200.0f, 4000.0f);
    steelLen.setSkewForCentre (1200.0f);
    layout.add (makeFloat (masterId (MP::SteelLength), "Steel Drum Ring", steelLen, 1400.0f,
                           [] (float v, int) { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " ms" : juce::String (v * 0.001f, 1) + " s"; }));
    layout.add (makeFloat (masterId (MP::SteelBright), "Steel Drum Bright", { 0.0f, 1.0f }, 0.55f, percent));
    layout.add (makeFloat (masterId (MP::SteelShimmer), "Steel Drum Shimmer", { 0.0f, 1.0f }, 0.4f, percent));
    layout.add (makeFloat (masterId (MP::SteelRoll), "Steel Drum Roll", { 0.0f, 1.0f }, 0.0f,
                           [] (float v, int) { return v < 0.005f ? juce::String ("Single hits") : juce::String (juce::roundToInt (v * 100.0f)) + " %"; }));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { masterId (MP::SteelModel), 1 }, "Steel Drum Model",
                                                              steelModelNames(), 0));
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
