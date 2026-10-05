#pragma once

#include "ScratchPattern.h"
#include <array>
#include <atomic>
#include <cassert>
#include <memory>

namespace backScratch {

// Immutable, fixed-capacity snapshots shared by the editor, delayed audio
// controls and audible display. A positive reference count protects the entire
// snapshot; zero is reusable, and -1 belongs exclusively to its writer.
//
// Callers serialize publish() on their non-audio edit lock. Readers never take
// that lock. Each acquired index owns one pin until release(); retain() requires
// the caller already to own (or otherwise protect) an existing pin. A full pool
// leaves the previous publication intact so the editor can coalesce previews.
class EditedPatternStore final {
public:
    static constexpr int None = -1;
    static constexpr int capacity = 32;

    EditedPatternStore() : slots(std::make_unique<Slots>()) {}
    EditedPatternStore(const EditedPatternStore&) = delete;
    EditedPatternStore& operator=(const EditedPatternStore&) = delete;

    int publish(const ScratchPattern& pattern) noexcept
    {
        for (int index = 0; index < capacity; ++index) {
            auto& slot = (*slots)[static_cast<size_t>(index)];
            int expected = 0;
            if (!slot.references.compare_exchange_strong(expected, -1,
                    std::memory_order_acquire, std::memory_order_relaxed)) continue;
            slot.pattern = pattern;
            // This first pin belongs to the published index itself, independent
            // of any reader, history frame or audible-display publication.
            slot.references.store(1, std::memory_order_release);
            const int previous = latest.exchange(index, std::memory_order_acq_rel);
            release(previous);
            return index;
        }
        return None;
    }

    int acquireLatest() const noexcept { return acquirePublished(latest); }

    // A gesture retains its original snapshot, so Escape can restore it even
    // when every slot is held by latency history. The caller owns that pin and
    // serializes this operation with publish(); publication takes its own pin.
    void publishExisting(int index) noexcept
    {
        assert(index >= 0 && index < capacity);
        retain(index);
        release(latest.exchange(index, std::memory_order_acq_rel));
    }

    // The publisher must own a pin on its published index until exchanging it
    // for another protected index. Pin-before-recheck also handles slot reuse:
    // the snapshot cannot change once a reader has incremented a positive count.
    // Bounded retries keep the audio callback finite under concurrent edits.
    // None is a transient result; audio can retain its last accepted snapshot.
    int acquirePublished(const std::atomic<int>& publication) const noexcept
    {
        for (int attempt = 0; attempt < capacity * 2; ++attempt) {
            const int index = publication.load(std::memory_order_acquire);
            if (index == None) return None;
            assert(index >= 0 && index < capacity);
            auto& references = (*slots)[static_cast<size_t>(index)].references;
            int count = references.load(std::memory_order_relaxed);
            if (count <= 0) continue;
            if (!references.compare_exchange_strong(count, count + 1,
                    std::memory_order_acquire, std::memory_order_relaxed)) continue;
            if (publication.load(std::memory_order_acquire) == index) return index;
            release(index);
        }
        return None;
    }

    void retain(int index) const noexcept
    {
        if (index == None) return;
        assert(index >= 0 && index < capacity);
        const int previous = (*slots)[static_cast<size_t>(index)].references.fetch_add(1, std::memory_order_relaxed);
        assert(previous > 0);
        (void) previous;
    }

    void release(int index) const noexcept
    {
        if (index == None) return;
        assert(index >= 0 && index < capacity);
        const int previous = (*slots)[static_cast<size_t>(index)].references.fetch_sub(1, std::memory_order_release);
        assert(previous > 0);
        (void) previous;
    }

    const ScratchPattern& get(int index) const noexcept
    {
        assert(index >= 0 && index < capacity);
        const auto& slot = (*slots)[static_cast<size_t>(index)];
        assert(slot.references.load(std::memory_order_relaxed) > 0);
        return slot.pattern;
    }

private:
    struct Slot {
        std::atomic<int> references {0};
        ScratchPattern pattern;
    };
    using Slots = std::array<Slot, capacity>;
    // Keep the fixed snapshot bank off the processor's stack. Allocation occurs
    // only during construction, never while publishing or processing audio.
    std::unique_ptr<Slots> slots;
    std::atomic<int> latest {None};
};

} // namespace backScratch
