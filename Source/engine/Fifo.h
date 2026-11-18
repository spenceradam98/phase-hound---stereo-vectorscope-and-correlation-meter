// Lock-free single-producer (audio thread) / single-consumer (UI thread) FIFO of stereo sample pairs.
// The audio thread never blocks or allocates; when the UI is not draining (editor closed or frozen)
// new samples are dropped.
#pragma once

#include <atomic>
#include <vector>

namespace ph
{
class StereoFifo
{
public:
    explicit StereoFifo (int capacityPow2 = 1 << 15)
        : mask (capacityPow2 - 1), left ((size_t) capacityPow2), right ((size_t) capacityPow2) {}

    int capacity() const { return mask + 1; }

    // Audio thread. Returns how many pairs were stored.
    int push (const float* l, const float* r, int n)
    {
        const int w = writePos.load (std::memory_order_relaxed);
        const int rd = readPos.load (std::memory_order_acquire);
        const int free = capacity() - 1 - ((w - rd) & mask);
        const int count = n < free ? n : free;
        for (int i = 0; i < count; ++i)
        {
            const int idx = (w + i) & mask;
            left[(size_t) idx] = l[i];
            right[(size_t) idx] = r[i];
        }
        writePos.store ((w + count) & mask, std::memory_order_release);
        return count;
    }

    int ready() const
    {
        return (writePos.load (std::memory_order_acquire) - readPos.load (std::memory_order_relaxed)) & mask;
    }

    // UI thread. Copies up to maxPairs (oldest first) and returns how many were copied.
    int pull (float* l, float* r, int maxPairs)
    {
        const int rd = readPos.load (std::memory_order_relaxed);
        const int avail = ready();
        const int count = maxPairs < avail ? maxPairs : avail;
        for (int i = 0; i < count; ++i)
        {
            const int idx = (rd + i) & mask;
            l[i] = left[(size_t) idx];
            r[i] = right[(size_t) idx];
        }
        readPos.store ((rd + count) & mask, std::memory_order_release);
        return count;
    }

    // UI thread: throw away everything queued (e.g. after the display was frozen).
    void discard() { readPos.store (writePos.load (std::memory_order_acquire), std::memory_order_release); }

private:
    const int mask;
    std::vector<float> left, right;
    std::atomic<int> writePos { 0 }, readPos { 0 };
};
} // namespace ph
