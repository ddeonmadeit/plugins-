#pragma once

#include <dd_dsp/dd_dsp.h>

#include "Hrtf.h"
#include "Room.h"

namespace orbit
{
/** Everything the engine needs for one chunk. */
struct SpatialParams
{
    Vec3 position { 0.0f, 0.5f, 0.4f };   // normalised, motion already applied
    float width = 0.3f;                    // 0 = a point, 1 = spread across half the horizon
    int room = 0;
    float size = 0.5f;
    float roomLevel = 0.5f;                // 0 = dry, 0.5 = as the room really sounds, 1 = drenched
    float doppler = 0.5f;                  // share of the travel time that is heard (pitch bends as it moves)
    bool speakers = false;                 // amplitude panning instead of binaural filters
    int head = 0;
};

/**
    Places a stereo input (two virtual sources, spread by Width) in a room, heard from the listener's head.

      direct     travel delay (Doppler), distance loss, air absorption, near-field level difference,
                 interaural delay and an interpolated head-related filter per ear (crossfaded per chunk)
      early      24 image sources of a shoebox room (first and second order) with per-surface reflection
                 and per-bounce darkening, delayed from the same lines as the direct sound
      late       16-line feedback delay network, decay from the room's RT60 (Sabine), darker per pass
      rendering  reflections and the late field are panned onto 16 fixed directions around the head,
                 each rendered with its own head-related filter: a diffuse field with real cues

    Loudness is normalised per room (a sound at the room's usual distance has the level of the input),
    so moving a sound changes how far away it sounds while switching rooms keeps the mix level.
    Any block size; targets update every kMaxChunk samples. No allocation after prepare().
*/
class Spatializer
{
public:
    static constexpr int kMaxChunk = 64;
    static constexpr int kSources = 2, kImages = 24, kBuses = 16, kLines = 16;

    void prepare (double sampleRate);
    void reset();
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const SpatialParams& p);

    /** Smoothed position actually heard (normalised), for the display. */
    Vec3 heardPosition() const noexcept { return posN; }
    float lateLevel() const noexcept { return lateRms; }

private:
    struct Image { Vec3 pos; float reflect; int order; };
    struct Target
    {
        float delay = 1.0f, gain = 0.0f, lpG = 0.5f;
        std::array<float, kBuses> pan {};
    };

    void updateTargets (const SpatialParams& p);
    void imagesFor (Vec3 src, std::array<Image, kImages>& out) const noexcept;
    void busPan (Vec3 dir, std::array<float, kBuses>& out) const noexcept;
    float distanceGain (float r) const noexcept;
    void renderSegment (const float* inL, const float* inR, float* outL, float* outR, int n, int intervalOffset);

    double sr = 48000.0;
    HrtfSet hrtf;
    int taps = 64, maxItd = 32;
    bool fresh = true, speakersNow = false;
    int intervalPos = kMaxChunk;   // samples into the current target interval (kMaxChunk = update next)

    // Smoothed state (per chunk).
    RoomState room;
    Vec3 posN { 0.0f, 0.5f, 0.4f };
    float width = 0.3f, makeup = 1.0f, makeupPrev = 1.0f, lateGain = 0.0f, lateGainPrev = 0.0f;
    float lateRms = 0.0f;

    // Per virtual source.
    struct Source
    {
        dd::DelayBuffer line;
        std::array<float, 2> earDelay {}, earDelayPrev {}, earGain {}, earGainPrev {};
        std::array<dd::OnePole, 2> air;
        std::vector<float> hPrev[2], hNext[2];      // head filters per ear
        std::vector<float> hist[2];                  // [taps - 1 + chunk] linear history per ear
        bool filterMoved = true;
        float lastAz = 99.0f, lastEl = 99.0f;
        std::array<float, 2> pan {}, panPrev {};     // speakers mode
        float heightCue = 0.0f;
        dd::OnePole heightHp[2];
        std::array<Target, kImages> refl, reflPrev;
        std::array<dd::OnePole, kImages> reflLp;
        float lateTap = 1.0f, lateTapPrev = 1.0f;
    };
    std::array<Source, kSources> src;

    // Late field.
    std::array<dd::DelayBuffer, kLines> lines;
    std::array<float, kLines> lineLen {}, lineGain {}, lineDampA {}, lineState {}, lineOut {};
    std::array<double, kLines> modPhase {};
    std::array<float, kLines> modRate {}, modPrev {}, modNext {};

    // Direction buses.
    std::array<Vec3, kBuses> busDir {};
    std::array<std::vector<float>, kBuses> busH[2];       // head filter per bus per ear
    std::array<int, kBuses> busItd[2] {};                  // whole-sample interaural delay per bus per ear
    std::array<std::array<float, 2>, kBuses> busSpk {};    // speakers mode gains
    std::array<std::vector<float>, kBuses> busHist;        // [taps - 1 + maxItd + chunk]
    std::array<dd::Biquad, kBuses> busLowCut, busShelf;    // small rooms add no boom or box; big ones keep their weight
    float busEqSize = -1.0f;
    int lastHead = -1;
    bool lastSpeakers = false;

    // Output: keep the sub mono.
    dd::Biquad sideHp1, sideHp2;

    std::array<float, kMaxChunk> tmp {}, tmp2 {};
    std::array<std::array<float, kMaxChunk>, kBuses> bus {};
    std::array<float, kMaxChunk> wetL {}, wetR {};
    std::array<std::array<float, kMaxChunk>, kSources> lateIn {};
};
} // namespace orbit
