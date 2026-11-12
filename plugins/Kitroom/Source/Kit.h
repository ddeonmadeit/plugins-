#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace kr
{
/** Sounds are interchangeable within a category (any tom sound fits any tom slot, etc.). */
enum class Category { Kick, Snare, HiHat, Tom, Cymbal, Ride, Clap };

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

constexpr int kNumSlots = 11;
constexpr int kNumClassicSlots = 10;   // slots before the clap (1.11 and older): their parameters come first
constexpr int kMaxArticulations = 4;
constexpr int kHatOpenGroup = 1;
constexpr int kSidestickArt = 2;        // the snare articulation a layered clap does not follow

enum Slot { Kick, Snare, HiHat, Tom1, Tom2, Tom3, Crash1, Crash2, RideSlot, China, ClapSlot };

/** 1.15: the log drum, a synthesised piece after the sampled slots. It plays its own key range above the kit's
    keys and sounds three octaves lower, so MIDI 69 (A5 in FL Studio, A3 in Ableton) is the classic low A (55 Hz). */
constexpr int kLogDrum = kNumSlots;
constexpr int kLogLowKey = 60, kLogHighKey = 96, kLogKeyOffset = 36;
constexpr int kLogAuditionKey = 69;   // the classic low A

/** 1.16: the steel drum, a synthesised tenor pan on the keys above the log drum's (MIDI 97-127), sounding three
    octaves lower too: C#4 to G6, the range of a tenor pan. */
constexpr int kSteelDrum = kNumSlots + 1;
constexpr int kSteelLowKey = 97, kSteelHighKey = 127, kSteelKeyOffset = 36;
constexpr int kSteelAuditionKey = 112;   // E7 in FL names: E4 on the pan, a ringing middle note

constexpr int kNumPieces = kNumSlots + 2;
inline bool isSynthPiece (int piece) { return piece == kLogDrum || piece == kSteelDrum; }

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
        { "clap",   "Clap",      Category::Clap,   { { "hit", "Clap", { 39 }, "", 0, 0 } } },
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
        case Category::Clap:   return "clap";
    }
    return "";
}

/** Packs (slot, articulation) for the key map; -1 = unassigned. */
inline int packTarget (int slot, int art) { return slot * kMaxArticulations + art; }
inline int targetSlot (int t)             { return t / kMaxArticulations; }
inline int targetArt (int t)              { return t % kMaxArticulations; }
} // namespace kr
