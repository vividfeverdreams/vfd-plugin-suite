#pragma once
#include <juce_core/juce_core.h>
#include <atomic>

namespace vfd {

// The program number the host last saw, for plug-ins whose own preset browsing
// doesn't tell the host (a program-change refresh makes the DAW replace
// automation). The host's number then lags behind the preset that plays.
// Ableton Live duplicates a device by restoring the copied state and then
// sending that lagging number back, which would load another preset into the
// copy. Save saved() with the state, call restored() when a state loads, and
// on a host program change load only when accept() returns true.
class HostProgram
{
public:
    // How long after a restore the lagging number is ignored.
    static constexpr juce::uint32 echoWindowMs = 15000;

    int saved() const noexcept { return known.load(); }

    // restoredProgram: the program the restored state plays (the VST3 wrapper
    // hands the host that number next). savedHostProgram: saved() from that
    // state, or -1 when unknown.
    void restored (int restoredProgram, int savedHostProgram) noexcept
    {
        known.store (restoredProgram);
        stale.store (savedHostProgram != restoredProgram ? savedHostProgram : -1);
        restoredAt.store (juce::Time::getMillisecondCounter());
    }

    // A program change from the host: false for the lagging number arriving
    // soon after a restore, which must not load.
    bool accept (int program) noexcept
    {
        known.store (program);
        if (program == stale.load() && juce::Time::getMillisecondCounter() - restoredAt.load() < echoWindowMs)
            return false;
        stale.store (-1);
        return true;
    }

private:
    std::atomic<int> known { 0 }, stale { -1 };
    std::atomic<juce::uint32> restoredAt { 0 };
};

} // namespace vfd
