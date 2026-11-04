#pragma once

namespace dd
{
/**
    Hands immutable snapshots (render tables etc.) from one writer thread to the audio thread without locks
    or frees on the audio thread. The reader announces what it holds in two hazard slots (the current
    snapshot and the one it is crossfading away from); the writer only deletes retired snapshots that
    neither slot names.
*/
template <typename T>
class Published
{
public:
    Published() = default;

    ~Published()
    {
        delete front.load();
        for (auto* r : retired)
            delete r;
    }

    /** Writer thread. */
    void publish (std::unique_ptr<T> next)
    {
        if (auto* old = front.exchange (next.release()))
            retired.push_back (old);
        reclaim();
    }

    /** Writer thread: the newest snapshot (the writer may read it freely; only it ever deletes). */
    const T* latest() const noexcept { return front.load(); }

    /** Writer thread: frees what the reader no longer holds. */
    void reclaim()
    {
        for (size_t i = 0; i < retired.size();)
        {
            auto* r = retired[i];
            if (r != hazard[0].load() && r != hazard[1].load())
            {
                delete r;
                retired[i] = retired.back();
                retired.pop_back();
            }
            else
                ++i;
        }
    }

    /** Reader: the newest snapshot, protected in `slot` until the slot is reused or released. */
    const T* acquire (int slot) noexcept
    {
        T* p = nullptr;
        do
        {
            p = front.load();
            hazard[slot].store (p);
        } while (p != front.load());
        return p;
    }

    /** Reader: keeps a pointer it already protects in another slot (e.g. the one being faded out). */
    void hold (int slot, const T* p) noexcept { hazard[slot].store (const_cast<T*> (p)); }
    void release (int slot) noexcept { hazard[slot].store (nullptr); }

private:
    std::atomic<T*> front { nullptr };
    std::atomic<T*> hazard[2] { nullptr, nullptr };
    std::vector<T*> retired;

    JUCE_DECLARE_NON_COPYABLE (Published)
};
} // namespace dd
