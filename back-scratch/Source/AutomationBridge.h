#pragma once

#include <array>
#include <algorithm>

// This private bridge is written by the pinned JUCE VST3 wrapper immediately
// before it calls the processor on the same audio thread. Stock JUCE forwards
// only the last point of each VST3 parameter queue to AudioProcessor.
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

inline thread_local Queue current;

} // namespace backScratchBridge
