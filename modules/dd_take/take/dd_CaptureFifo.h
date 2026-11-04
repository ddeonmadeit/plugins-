#pragma once

namespace dd
{
/**
    Audio thread -> worker hand-over of captured samples, each run stamped with its timeline position.
    Consecutive samples are packed into fixed packets (so a host block size of 1 still fills them); a jump
    in position (loop, seek) starts a new packet. Single producer (audio), single consumer (worker).
    About 22 s of 48 kHz audio fit, far more than the worker ever lags behind.
*/
class CaptureFifo
{
public:
    static constexpr int kPacket = 256, kPackets = 4096;

    CaptureFifo() : ring ((size_t) kPackets) {}

    /** Audio thread. */
    void push (int64_t pos, const float* x, int n) noexcept
    {
        for (int i = 0; i < n;)
        {
            if (cur.n > 0 && pos + i != cur.pos + cur.n)
                flush();
            if (cur.n == 0)
                cur.pos = pos + i;
            const int len = std::min (n - i, kPacket - cur.n);
            std::copy (x + i, x + i + len, cur.data + cur.n);
            cur.n += len;
            i += len;
            if (cur.n == kPacket)
                flush();
        }
    }

    /** Audio thread: publishes the partly filled packet. */
    void flush() noexcept
    {
        if (cur.n == 0)
            return;
        const int w = head.load (std::memory_order_relaxed);
        const int next = (w + 1) % kPackets;
        if (next == tail.load (std::memory_order_acquire))
            overflow.store (true, std::memory_order_relaxed);   // worker stalled: drop rather than block
        else
        {
            ring[(size_t) w] = cur;
            head.store (next, std::memory_order_release);
        }
        cur.n = 0;
    }

    /** Worker: hands every waiting run to f (pos, data, n). Returns how many packets it took. */
    template <typename F>
    int drain (F&& f)
    {
        int count = 0;
        for (;;)
        {
            const int r = tail.load (std::memory_order_relaxed);
            if (r == head.load (std::memory_order_acquire))
                return count;
            const auto& p = ring[(size_t) r];
            f (p.pos, p.data, p.n);
            tail.store ((r + 1) % kPackets, std::memory_order_release);
            ++count;
        }
    }

    bool isEmpty() const noexcept { return tail.load (std::memory_order_acquire) == head.load (std::memory_order_acquire); }
    bool takeOverflow() noexcept { return overflow.exchange (false); }

private:
    struct Packet
    {
        int64_t pos = 0;
        int n = 0;
        float data[kPacket] {};
    };

    std::vector<Packet> ring;
    Packet cur;
    std::atomic<int> head { 0 }, tail { 0 };
    std::atomic<bool> overflow { false };

    JUCE_DECLARE_NON_COPYABLE (CaptureFifo)
};
} // namespace dd
