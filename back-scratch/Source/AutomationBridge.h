#pragma once

#include <array>
#include <algorithm>

// This private bridge is written by the pinned JUCE VST3 wrapper immediately
// before it calls the processor on the same audio thread. Stock JUCE forwards
// only the last point of each VST3 parameter queue to AudioProcessor.
//
// The wrapper-facing API is unchanged (backScratchBridge::current.clear() and
// current.push(parameter, offset, value)), so the existing JUCE patch keeps
// working. Since 0.11 `current` is a stateless router rather than a
// thread_local queue: each processor owns its queue, and its automatable
// controls carry a QueueTag that points at it. Nothing is allocated on first
// use from a new audio thread, and instances never share event storage.
namespace backScratchBridge {

struct Event {
    const void* parameter = nullptr;
    int offset = 0;
    float normalizedValue = 0.0f;
};

struct Queue {
    static constexpr int capacity = 4096;
    std::array<Event, capacity> events {};
    int size = 0;
    bool overflow = false;

    void clear() noexcept { size = 0; overflow = false; }
    void push(const void* parameter, int offset, float normalizedValue) noexcept {
        if (size < capacity)
            events[static_cast<size_t>(size++)] = { parameter, offset, normalizedValue };
        else
            overflow = true;
    }
};

// Mixed into a juce::AudioProcessorParameter whose points should be delivered
// sample-accurately. The owning processor sets queue in its constructor.
class QueueTag {
public:
    virtual ~QueueTag() = default;
    Queue* queue = nullptr;
};

struct Router {
    // Each processor empties its own queue at the end of every callback.
    void clear() noexcept {}
    // parameter must point to a juce::AudioProcessorParameter. Points for
    // parameters without a QueueTag are ignored. Defined in BackScratchProcessor.cpp.
    void push(const void* parameter, int offset, float normalizedValue) noexcept;
};

inline Router current;

} // namespace backScratchBridge
