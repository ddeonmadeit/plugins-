#pragma once

namespace dd
{
/**
    What the plugin knows about the DAW it runs in, for words and key names on screen.

    Hosts disagree on what to call MIDI note 60 (middle C): FL Studio prints C5; Ableton Live, Logic,
    GarageBand, Cubase, Studio One, Bitwig and Pro Tools print C3; Reaper prints C4. Keys shown in a plugin
    must match the piano roll the user is looking at, so they go through noteName(). Unknown hosts, the
    standalone app and the QA harness use FL Studio's names (the owner's main DAW).
    DD_HOST=ableton|logic|fl|... forces a host (headless snapshots).
*/
struct Host
{
    juce::String name;            // "FL Studio", "Ableton Live", ... ("" when unknown)
    juce::String shortName;       // "FL", "Ableton", ... (for "KEYS (ABLETON NAMES)")
    int middleCOctave = 5;        // the octave number the host gives MIDI 60
    bool ableton = false;
};

const Host& host();

/** A MIDI note as the host's piano roll names it ("--" outside 0..127). */
juce::String noteName (int midi);

/** How the names on screen are meant: "FL NAMES", "ABLETON NAMES", ... */
juce::String noteNamesCaption();
} // namespace dd
