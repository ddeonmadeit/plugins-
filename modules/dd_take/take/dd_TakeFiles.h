#pragma once

namespace dd
{
/**
    Takes live on disk next to the user's presets (~/Music/ddeonmadeit/<Plugin>/Takes), referenced from the
    host project by path, the way Melodyne keeps its transfers: projects stay small, and a take survives
    plugin updates. Files are written once and never changed, so duplicated plugin instances can share one.
*/
namespace takefiles
{
    juce::File folder (const juce::String& pluginName, const juce::String& sub = "Takes");

    /** A fresh, unique file name in the plugin's take folder. */
    juce::File newTakeFile (const juce::String& pluginName, const juce::String& extension = ".flac");

    /** Writes [span) of the store as mono 24-bit FLAC (via a temporary file, so a half-written take never
        appears under its real name). */
    bool save (const TakeStore& store, Span span, double sampleRate, const juce::File& file);

    /** Writes a buffer (any channel count) as a 24-bit WAV, for bounces. */
    bool writeWav (const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::File& file);

    struct Loaded
    {
        bool ok = false;
        int64_t length = 0;          // samples written, at the target rate
        double fileRate = 0.0;
        juce::String error;
    };

    /** Reads any audio file the system can decode, mixes it to mono, converts it to `targetRate` and writes
        it into the store starting at timeline sample `at`. Worker thread. */
    Loaded load (TakeStore& store, int64_t at, const juce::File& file, double targetRate,
                 const std::atomic<bool>* cancel = nullptr);

    /** File extensions load() accepts (for drag and drop). */
    bool canLoad (const juce::File& file);
} // namespace takefiles
} // namespace dd
