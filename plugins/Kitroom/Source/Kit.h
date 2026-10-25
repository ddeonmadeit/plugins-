#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace kr
{
/** Sounds are interchangeable within a category (any tom sound fits any tom slot, etc.). */
enum class Category { Kick, Snare, HiHat, Tom, Cymbal, Ride };

struct ArticulationDef
{
    const char* id;            // matches the articulation ids in the library catalog
    const char* name;
    std::vector<int> keys;     // default MIDI notes (General MIDI first, then aliases)
    const char* fallback;      // articulation to use when a sound doesn't have this one ("" = silent)
    int chokeGroup;            // voices in group g are choked by articulations listing g in chokes
    int chokes;                // group this articulation chokes (0 = none)
};

struct SlotDef
{
    const char* id;
    const char* name;
    Category category;
    std::vector<ArticulationDef> articulations;
};

constexpr int kNumSlots = 10;
constexpr int kMaxArticulations = 4;
constexpr int kHatOpenGroup = 1;

enum Slot { Kick, Snare, HiHat, Tom1, Tom2, Tom3, Crash1, Crash2, RideSlot, China };

/** The kit layout. Order is permanent: parameter ids and saved sessions use the slot ids. */
inline const std::array<SlotDef, kNumSlots>& slots()
{
    static const std::array<SlotDef, kNumSlots> s {{
        { "kick",   "Kick",      Category::Kick,   { { "hit", "Kick", { 36, 35 }, "", 0, 0 } } },
        { "snare",  "Snare",     Category::Snare,  { { "center", "Center", { 38 }, "", 0, 0 },
                                                     { "rimshot", "Rimshot", { 40 }, "center", 0, 0 },
                                                     { "sidestick", "Sidestick", { 37 }, "", 0, 0 } } },
        { "hihat",  "Hi-Hat",    Category::HiHat,  { { "closed", "Closed", { 42 }, "", 0, kHatOpenGroup },
                                                     { "half", "Half Open", { 23 }, "open", kHatOpenGroup, 0 },
                                                     { "open", "Open", { 46 }, "half", kHatOpenGroup, 0 },
                                                     { "pedal", "Pedal", { 44 }, "closed", 0, kHatOpenGroup } } },
        { "tom1",   "Tom 1",     Category::Tom,    { { "hit", "Tom 1", { 48, 50 }, "", 0, 0 } } },
        { "tom2",   "Tom 2",     Category::Tom,    { { "hit", "Tom 2", { 45, 47 }, "", 0, 0 } } },
        { "tom3",   "Floor Tom", Category::Tom,    { { "hit", "Floor Tom", { 43, 41 }, "", 0, 0 } } },
        { "crash1", "Crash L",   Category::Cymbal, { { "hit", "Crash L", { 49 }, "", 0, 0 } } },
        { "crash2", "Crash R",   Category::Cymbal, { { "hit", "Crash R", { 57 }, "", 0, 0 } } },
        { "ride",   "Ride",      Category::Ride,   { { "bow", "Bow", { 51, 59 }, "", 0, 0 },
                                                     { "bell", "Bell", { 53 }, "bow", 0, 0 } } },
        { "china",  "China",     Category::Cymbal, { { "hit", "China", { 52, 55 }, "", 0, 0 } } },
    }};
    return s;
}

inline const char* categoryId (Category c)
{
    switch (c)
    {
        case Category::Kick:   return "kick";
        case Category::Snare:  return "snare";
        case Category::HiHat:  return "hihat";
        case Category::Tom:    return "tom";
        case Category::Cymbal: return "cymbal";
        case Category::Ride:   return "ride";
    }
    return "";
}

/** FL Studio note naming: MIDI 60 = C5, so MIDI 36 = C3. */
inline juce::String flNoteName (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (note < 0 || note > 127)
        return "--";
    return juce::String (names[note % 12]) + juce::String (note / 12);
}

/** Packs (slot, articulation) for the key map; -1 = unassigned. */
inline int packTarget (int slot, int art) { return slot * kMaxArticulations + art; }
inline int targetSlot (int t)             { return t / kMaxArticulations; }
inline int targetArt (int t)              { return t % kMaxArticulations; }
} // namespace kr
