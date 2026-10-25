#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Kit.h"

namespace kitroom
{
/** Per-slot parameters; ids are "<slotId><Suffix>", e.g. "snareTune". */
enum class SP { Volume, Pan, Tune, Decay, Close, Overheads, Room, count };
constexpr int kSlotParams = (int) SP::count;

/** Master parameters. Append only: ids are permanent and old sessions load the defaults for new ones. */
enum class MP { Close, Overheads, Room, VelCurve, Humanize, Glue, Clip, Width, Output, Drive, DriveType, Crush, Tone, count };
constexpr int kMasterParams = (int) MP::count;

juce::String slotParamId (int slot, SP p);
juce::String masterParamId (MP p);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Raw atomic pointers into the APVTS. Read on the audio thread. */
class ParamCache
{
public:
    void attach (juce::AudioProcessorValueTreeState& state);

    float slot (int s, SP p) const noexcept { return slotValues[(size_t) (s * kSlotParams + (int) p)]->load (std::memory_order_relaxed); }
    float master (MP p) const noexcept      { return masterValues[(size_t) p]->load (std::memory_order_relaxed); }

private:
    std::array<std::atomic<float>*, (size_t) (kr::kNumSlots * kSlotParams)> slotValues {};
    std::array<std::atomic<float>*, (size_t) kMasterParams> masterValues {};
};
} // namespace kitroom
