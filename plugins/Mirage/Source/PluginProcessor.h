#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <dd_core/dd_core.h>

#include "Params.h"

/**
    Stereo tap the audio thread writes into and the display reads from without locks. The write counter
    only grows, so the display can take exactly the samples it has not seen yet.
*/
class StereoTap
{
public:
    static constexpr int kSize = 16384;

    void push (const float* l, const float* r, int n) noexcept
    {
        auto w = written.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            const auto idx = (size_t) ((w + (juce::uint64) i) & (kSize - 1));
            left[idx] = l[i];
            right[idx] = r[i];
        }
        written.store (w + (juce::uint64) n, std::memory_order_release);
    }

    juce::uint64 total() const noexcept { return written.load (std::memory_order_acquire); }

    /** Copies samples [from, from + n) of the stream (they must still be in the ring). */
    void copy (juce::uint64 from, int n, float* l, float* r) const noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const auto idx = (size_t) ((from + (juce::uint64) i) & (kSize - 1));
            l[i] = left[idx];
            r[i] = right[idx];
        }
    }

private:
    std::array<float, kSize> left {}, right {};
    std::atomic<juce::uint64> written { 0 };
};

/** A visualiser: audio passes through untouched; the editor draws it. */
class MirageProcessor : public juce::AudioProcessor
{
public:
    MirageProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return DD_PLUGIN_NAME; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    dd::PresetManager presets;
    StereoTap tap;

    struct Transport
    {
        std::atomic<double> bpm { 120.0 }, ppq { 0.0 }, sampleRate { 44100.0 };
        std::atomic<bool> playing { false };
    };
    Transport transport;

    /** Editor size, kept with the session (the window is stretched to fit under the tracks). */
    std::atomic<int> editorWidth { 1600 }, editorHeight { 440 };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MirageProcessor)
};
