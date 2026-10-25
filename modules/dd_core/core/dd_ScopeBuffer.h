#pragma once

namespace dd
{
/**
    Single-producer ring buffer the audio thread writes into and the UI reads from without locks.
    A torn read only ever produces a slightly wrong scope frame, which is fine for display.
*/
class ScopeBuffer
{
public:
    static constexpr int kSize = 4096;

    void push (const float* left, const float* right, int n) noexcept
    {
        int w = writePos.load (std::memory_order_relaxed);
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float m = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
            buf[(size_t) w] = m;
            w = (w + 1) & (kSize - 1);
            pk = std::max (pk, std::max (std::abs (left[i]), right != nullptr ? std::abs (right[i]) : 0.0f));
        }
        writePos.store (w, std::memory_order_release);

        float old = peak.load (std::memory_order_relaxed);
        while (pk > old && ! peak.compare_exchange_weak (old, pk)) {}
    }

    /** Copies the newest n samples (oldest first). */
    void copyLatest (float* dest, int n) const noexcept
    {
        n = std::min (n, kSize);
        const int w = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
            dest[i] = buf[(size_t) ((w - n + i + kSize) & (kSize - 1))];
    }

    /** Returns the peak since the last call and resets it. */
    float takePeak() noexcept { return peak.exchange (0.0f); }

private:
    std::array<float, kSize> buf {};
    std::atomic<int> writePos { 0 };
    std::atomic<float> peak { 0.0f };
};
} // namespace dd
