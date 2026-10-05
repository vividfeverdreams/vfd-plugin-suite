#pragma once

#include "ScratchPattern.h"
#include <array>
#include <cstdint>
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

    // Allocate only when the host prepares playback. Production uses the full
    // 128 seconds: a stretched 16-bar phrase at 20 BPM can look back 120 s.
    // A smaller budget remains useful for short standalone renders/tests.
    void prepare(double sampleRate, int channels, double historySeconds = defaultHistorySeconds);
    void reset() noexcept;
    int latencySamples() const noexcept { return crossoverDelay + interpolationGuardSamples; }
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
    std::int64_t writeFrame = 0;
    int capacity = 0, busChannels = 2, crossoverDelay = 0;
    double rate = 48000.0;
    double fadeStep = 1.0 / 144.0, controlCoefficient = 0.01;
    double wetAmount = 0, gateAmount = 1, motionAmount = 1, subAmount = 1;
    double previousTarget = 0, previousRate = 1;
    double outgoingFrame = 0, outgoingRate = 1, jumpFade = 1;
    std::array<double, 4> lastRead {}, frozenOutgoing {};
    bool haveTarget = false, freezeOutgoing = false, firstSample = true;
    bool historyReady = false, scratchActive = false;

    bool readable(double frame) const noexcept;
    std::array<double, 4> read(double frame, double speed) const noexcept;
};

} // namespace backScratch
