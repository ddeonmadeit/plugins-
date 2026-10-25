#pragma once

namespace dd::gui
{
/** Triggered oscilloscope + peak meter reading a dd::ScopeBuffer at 30 fps. */
class Scope : public juce::Component, private juce::Timer
{
public:
    explicit Scope (dd::ScopeBuffer& source);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    dd::ScopeBuffer& buffer;
    std::array<float, 2048> samples {};
    float meter = 0.0f, hold = 0.0f;
    int holdFrames = 0;
};
} // namespace dd::gui
