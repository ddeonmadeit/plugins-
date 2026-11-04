#pragma once

namespace dd
{
/**
    A mono recording laid out on the host timeline (index = timeline sample), stored sparsely in 64k-sample
    chunks so a vocal that starts three minutes into a song costs nothing before it.

    Threading: chunks are allocated and written by one non-audio thread; any thread may read lock-free.
    A missing chunk reads as silence. clear() frees memory, so its caller must know nobody is reading.
*/
class TakeStore
{
public:
    static constexpr int kChunkBits = 16, kChunk = 1 << kChunkBits, kMaxChunks = 1 << 14;   // ~6 h at 48 kHz

    TakeStore()
    {
        for (auto& c : chunks)
            c.store (nullptr, std::memory_order_relaxed);
    }

    ~TakeStore() { clear(); }

    static constexpr int64_t capacity() noexcept { return (int64_t) kMaxChunks << kChunkBits; }

    /** Writer thread only. Samples outside the store's capacity are dropped. */
    void write (int64_t pos, const float* data, int n)
    {
        for (int i = 0; i < n;)
        {
            const int64_t p = pos + i;
            if (p < 0 || p >= capacity())
            {
                ++i;
                continue;
            }
            auto* c = ensureChunk ((int) (p >> kChunkBits));
            const int off = (int) (p & (kChunk - 1));
            const int len = std::min (n - i, kChunk - off);
            std::copy (data + i, data + i + len, c + off);
            i += len;
        }
    }

    /** Writer thread only: zeroes a span (chunks are kept). */
    void erase (int64_t start, int64_t end)
    {
        for (int64_t p = std::max<int64_t> (0, start); p < std::min (end, capacity());)
        {
            const int ci = (int) (p >> kChunkBits);
            const int off = (int) (p & (kChunk - 1));
            const int len = (int) std::min<int64_t> (end - p, kChunk - off);
            if (auto* c = chunks[(size_t) ci].load (std::memory_order_acquire))
                std::fill (c + off, c + off + len, 0.0f);
            p += len;
        }
    }

    /** Frees everything. Only when no other thread can be reading. */
    void clear()
    {
        for (auto& c : chunks)
            delete[] c.exchange (nullptr, std::memory_order_acq_rel);
    }

    bool isEmpty() const noexcept
    {
        for (auto& c : chunks)
            if (c.load (std::memory_order_relaxed) != nullptr)
                return false;
        return true;
    }

    float get (int64_t pos) const noexcept
    {
        if (pos < 0 || pos >= capacity())
            return 0.0f;
        const auto* c = chunks[(size_t) (pos >> kChunkBits)].load (std::memory_order_acquire);
        return c != nullptr ? c[pos & (kChunk - 1)] : 0.0f;
    }

    void read (int64_t pos, float* dest, int n) const noexcept
    {
        for (int i = 0; i < n;)
        {
            const int64_t p = pos + i;
            if (p < 0 || p >= capacity())
            {
                dest[i++] = 0.0f;
                continue;
            }
            const auto* c = chunks[(size_t) (p >> kChunkBits)].load (std::memory_order_acquire);
            const int off = (int) (p & (kChunk - 1));
            const int len = std::min (n - i, kChunk - off);
            if (c != nullptr)
                std::copy (c + off, c + off + len, dest + i);
            else
                std::fill (dest + i, dest + i + len, 0.0f);
            i += len;
        }
    }

    /** Cubic Hermite read between samples (exact on whole positions). */
    float hermite (double pos) const noexcept
    {
        const double fl = std::floor (pos);
        const auto i = (int64_t) fl;
        const float t = (float) (pos - fl);
        if (t <= 0.0f)
            return get (i);
        const float xm1 = get (i - 1), x0 = get (i), x1 = get (i + 1), x2 = get (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

private:
    float* ensureChunk (int index)
    {
        auto& slot = chunks[(size_t) index];
        if (auto* c = slot.load (std::memory_order_acquire))
            return c;
        auto* c = new float[(size_t) kChunk]();
        slot.store (c, std::memory_order_release);
        return c;
    }

    std::array<std::atomic<float*>, (size_t) kMaxChunks> chunks;

    JUCE_DECLARE_NON_COPYABLE (TakeStore)
};
} // namespace dd
