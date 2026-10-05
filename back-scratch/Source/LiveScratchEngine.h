#pragma once

#include "ScratchPattern.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace backScratch {

// A causal, full-band time remapper. The optional protected bass is an exact
// complement of the scratch band at the same source time. Every path, including
// bypass, carries latencySamples(); controls describe that delayed source time.
class LiveScratchEngine {
public:
    static constexpr double crossoverHz = 200.0;
    static constexpr int maximumHistorySeconds = 128;
    static constexpr int defaultHistorySeconds = 16;
    static constexpr int interpolationGuardSamples = 32;
    // A host tempo change converts a beat-locked delay into a different number
    // of samples. Small, ramp-like changes glide the conversion so the readhead
    // bends smoothly (at most this many samples of drift per sample); larger
    // changes snap once and use the ordinary jump crossfade.
    static constexpr double tempoGlideDrift = 0.25;
    static constexpr double tempoSnapFraction = 0.005;

    // Allocate only when the host prepares playback. Production uses the full
    // 128 seconds: a stretched 16-bar phrase at 20 BPM can look back 120 s.
    // A smaller budget remains useful for short standalone renders/tests.
    // Never throws. If the memory is unavailable it retains 64, 32, 16, 8, 4,
    // 2 or 1 seconds, and finally only the latency pipeline (0 s); the
    // reported latency is unchanged in every case.
    void prepare(double sampleRate, int channels, double historySeconds = defaultHistorySeconds) noexcept;
    void reset() noexcept;
    // Begin a new musical history after a seek, transport start/stop or state
    // restore without interrupting the dry latency pipeline or the crossover.
    // New gestures can only read audio that arrives from now on; an audible
    // scratch fades out over the usual 3 ms instead of stopping abruptly.
    void invalidateHistory() noexcept;
    int latencySamples() const noexcept { return crossoverDelay + interpolationGuardSamples; }
    // Seconds of lookback actually retained by the last prepare().
    double historySeconds() const noexcept { return retainedSeconds; }
    // Test/diagnostic hook: treat history allocations above this size as failed.
    void setHistoryMemoryLimit(std::size_t bytes) noexcept { memoryLimit = bytes; }
    // Number of readhead discontinuities that started a jump crossfade.
    std::uint64_t readheadJumps() const noexcept { return jumps; }
    // Last processed target, for the processor's audible display snapshot.
    // A requested stroke can lack history immediately after a start or seek.
    bool isHistoryReady() const noexcept { return historyReady; }
    bool isScratchActive() const noexcept { return scratchActive; }
    std::array<float, 2> process(float left, float right, const PatternSample&,
                               double samplesPerBeat, bool keepSub, bool bypass) noexcept;

private:
    // Symmetric FIR with a duplicated ring: paired samples share one multiply,
    // and no allocation or modulo operation occurs inside the convolution.
    struct LinearPhaseFilter {
        std::vector<double> coefficients, samples;
        int length = 0, cursor = 0;
        void prepare(int halfLength, double normalizedCutoff);
        double process(double input) noexcept;
        void reset() noexcept;
    };
    struct BassExtractor {
        static constexpr int maximumStages = 5;
        struct Stage {
            LinearPhaseFilter down, up;
            bool even = true;
        };
        std::array<Stage, maximumStages> stages;
        LinearPhaseFilter base;
        int stageCount = 0, delay = 0;
        void prepare(double sampleRate);
        void reset() noexcept;
        double process(double input, int level = 0) noexcept;
    };
    std::array<std::vector<float>, 4> history; // Full L/R, complementary upper L/R.
    std::array<BassExtractor, 2> bass;
    std::int64_t writeFrame = 0, validFrom = 0;
    int capacity = 0, busChannels = 2, crossoverDelay = 0;
    double rate = 48000.0, retainedSeconds = 0.0;
    std::size_t memoryLimit = std::numeric_limits<std::size_t>::max();
    double fadeStep = 1.0 / 144.0, controlCoefficient = 0.01;
    double wetAmount = 0, gateAmount = 1, motionAmount = 1, subAmount = 1;
    double previousTarget = 0, previousRate = 1;
    double previousDelayBeats = 0, previousSamplesPerBeat = 0, glideSamplesPerBeat = 0;
    double outgoingFrame = 0, outgoingRate = 1, jumpFade = 1;
    double lastReadFrame = 0, lastReadRate = 1, fallbackFrame = 0, fallbackRate = 1;
    std::array<double, 4> lastRead {}, frozenOutgoing {};
    std::uint64_t jumps = 0;
    bool haveTarget = false, freezeOutgoing = false, firstSample = true, fallbackActive = false;
    bool historyReady = false, scratchActive = false;

    bool allocateHistory(double seconds) noexcept;
    // Audio that is still physically stored in the ring.
    bool available(double frame) const noexcept;
    // Stored audio that also arrived after the last invalidateHistory().
    bool readable(double frame) const noexcept;
    // Moves frame into the readable window. allowRetained permits the stored
    // pre-invalidation audio only when no new audio is readable yet; it is used
    // solely to fade out the scratch that was already audible.
    bool clampToReadable(double& frame, bool allowRetained) const noexcept;
    std::array<double, 4> read(double frame, double speed) const noexcept;
};

} // namespace backScratch
