#pragma once

namespace dd
{
/**
    Guitar speaker cabinet and close mic as a fixed curve: the steep roll-off above ~5 kHz, the low
    resonance and the cone breakup ripple in the upper mids. Without it an amped guitar sounds like a
    fizzy synth; with it the distortion sits behind a speaker. Mono per channel, no latency.

      Combo  open-back 1x12: lighter lows, scooped low mids, a bright presence peak
      Stack  closed-back 4x12: a resonant low thump, tight low mids, an aggressive upper-mid bite
*/
class CabSim
{
public:
    enum class Type { Combo, Stack };

    static const juce::StringArray& typeNames()
    {
        static const juce::StringArray names { "1x12 Combo", "4x12 Stack" };
        return names;
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        configured = -1;
        setType (Type::Combo);
        reset();
    }

    void setType (Type t) noexcept
    {
        if ((int) t == configured)
            return;
        configured = (int) t;
        using B = Biquad::Type;
        struct Stage { B type; float hz, q, db; };
        static constexpr Stage combo[kStages] = {
            { B::HighPass, 85.0f, 0.6f, 0.0f }, { B::Peak, 160.0f, 1.0f, 1.5f }, { B::Peak, 650.0f, 0.8f, -2.0f },
            { B::Peak, 2200.0f, 1.3f, 4.0f }, { B::Peak, 3300.0f, 4.0f, -3.5f }, { B::Peak, 4100.0f, 5.0f, 2.5f },
            { B::LowPass, 5200.0f, 0.9f, 0.0f }, { B::LowPass, 5800.0f, 1.3f, 0.0f } };
        static constexpr Stage stack[kStages] = {
            { B::HighPass, 75.0f, 1.1f, 0.0f }, { B::Peak, 115.0f, 1.4f, 3.5f }, { B::Peak, 480.0f, 1.0f, -2.5f },
            { B::Peak, 1500.0f, 1.2f, 1.0f }, { B::Peak, 2800.0f, 2.0f, 2.0f }, { B::Peak, 3900.0f, 4.0f, -4.0f },
            { B::LowPass, 4600.0f, 0.9f, 0.0f }, { B::LowPass, 5200.0f, 1.4f, 0.0f } };
        const auto& curve = t == Type::Combo ? combo : stack;
        for (auto& chain : stages)
            for (size_t k = 0; k < kStages; ++k)
                chain[k].set (curve[k].type, curve[k].hz, curve[k].q, curve[k].db, sr);
    }

    void reset() noexcept
    {
        for (auto& chain : stages)
            for (auto& b : chain)
                b.reset();
    }

    float process (int ch, float x) noexcept
    {
        for (auto& b : stages[(size_t) ch])
            x = b.process (x);
        return x;
    }

private:
    static constexpr size_t kStages = 8;
    double sr = 44100.0;
    int configured = -1;
    std::array<std::array<Biquad, kStages>, 2> stages {};
};
} // namespace dd
