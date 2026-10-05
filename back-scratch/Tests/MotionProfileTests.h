#pragma once

#include "PatternTests.h"
#include "PatternEditing.h"
#include "LiveScratchEngine.h"

namespace backScratch {

inline void runMotionProfileTests()
{
    using namespace patternTests;
    // Golden hashes were produced from the unmodified v0.9.0 ScratchPattern
    // source, before adding motionVersion. Keep legacy sessions sample-exact,
    // including old seeded choices, cut positions, and floating-point values.
    std::uint64_t factoryHash = 1469598103934665603ull;
    std::uint64_t generatedHash = factoryHash;
    for (int index = 0; index < factoryPresetCount; ++index) {
        for (const std::uint32_t seed : {0u, 1u, 17u, 20260925u}) {
            factoryHash ^= structuralFingerprint(makePattern(index, seed, 1));
            factoryHash *= 1099511628211ull;
        }
    }
    for (std::uint32_t seed = 0; seed < 100; ++seed) {
        for (const double beats : {0.5, 4.0, 16.0, 64.0}) {
            for (const double bar : {3.0, 4.0, 7.0}) {
                generatedHash ^= structuralFingerprint(generatePattern(seed * 79421u, beats, bar, 1));
                generatedHash *= 1099511628211ull;
            }
        }
    }
    check(factoryHash == 3506170674502728222ull
              && generatedHash == 10544104326736442281ull,
          "legacy motion matches frozen v0.9.0 geometry for 300 factory variants and 1200 generated phrases");

    double largestContinuousCorner = 0.0;
    int clippedCuts = 0, movingSegments = 0;
    const auto inspect = [&](const ScratchPattern& pattern) {
        check(validateEditedPattern(pattern), "new smooth recipes remain editable and valid at every duration");
        for (int index = 0; index < pattern.count; ++index) {
            const auto& part = pattern.segments[static_cast<std::size_t>(index)];
            if (part.delayStart != part.delayEnd && part.wet > 0.0f) {
                ++movingSegments;
                check(part.curved, "new scratch strokes accelerate rather than sustaining a transposed note");
                if (part.curveStart != 0.0 || part.curveEnd != 1.0) ++clippedCuts;
            }
            if (index == 0) continue;
            const auto& previous = pattern.segments[static_cast<std::size_t>(index - 1)];
            const auto before = evaluatePatternSegment(pattern, index - 1, previous.end);
            const auto after = evaluatePatternSegment(pattern, index, part.start);
            if (std::abs(before.delayBeats - after.delayBeats) < 1.0e-10) {
                largestContinuousCorner = std::max(largestContinuousCorner,
                    std::abs(before.playbackRate - after.playbackRate));
                check(std::abs(before.playbackRate - after.playbackRate) < 1.0e-9,
                      "continuous scratch position also has continuous velocity, including crossfader cuts");
            }
        }
    };
    for (int index = 0; index < factoryPresetCount; ++index) {
        const auto source = makePattern(index);
        inspect(source);
        for (const double beats : {1.0 / 6.0, 0.5, 4.0, 32.0, 384.0})
            inspect(fitPatternToPhrase(source, beats));
    }
    for (std::uint32_t seed = 0; seed < 500; ++seed) {
        for (const double beats : {0.5, 4.0, 16.0, 64.0})
            inspect(generatePattern(seed * 79421u, beats));
    }
    check(clippedCuts > 100 && movingSegments > 1000,
          "smooth motion regression exercises gated curves and a varied generated corpus");

    const auto oldTug = makePattern(0, 0, 1);
    const auto newTug = makePattern(0, 0, 2);
    std::array<double, 2> maxStep {}, movingFraction {};
    for (int version = 0; version < 2; ++version) {
        const auto& pattern = version == 0 ? oldTug : newTug;
        double previousRate = 1.0, accelerating = 0.0, active = 0.0;
        bool previouslyActive = false;
        for (int sample = 0; sample < 100000; ++sample) {
            const auto point = evaluatePattern(pattern, pattern.beats * (sample + 0.5) / 100000.0);
            const bool isActive = point.wet > 0.5f && point.gate > 0.5f;
            if (isActive) {
                ++active;
                if (previouslyActive) {
                    const double change = std::abs(point.playbackRate - previousRate);
                    maxStep[static_cast<std::size_t>(version)] = std::max(maxStep[static_cast<std::size_t>(version)], change);
                    if (change > 1.0e-7) ++accelerating;
                }
            }
            previousRate = point.playbackRate;
            previouslyActive = isActive;
        }
        movingFraction[static_cast<std::size_t>(version)] = accelerating / active;
    }
    check(maxStep[0] > 3.0 && maxStep[1] < 0.002,
          "Sub Tug no longer jumps instantly from backward audio to a sustained high forward pitch");
    check(movingFraction[0] < 0.001 && movingFraction[1] > 0.995,
          "scratch acceleration replaces the original fixed-rate pitched plateaus");

    // Independently predict the audible phase from the graph's actual source
    // position. No secondary velocity smoothing or catch-up may make a pretty
    // curve disagree with the pitched audio produced by the resampler.
    LiveScratchEngine engine;
    constexpr double sampleRate = 24000.0, samplesPerBeat = 12000.0;
    constexpr double pi2 = 6.28318530717958647692;
    engine.prepare(sampleRate, 2, 2.0);
    double errorEnergy = 0.0, referenceEnergy = 0.0;
    int steadyFrames = 0, comparedFrames = 0;
    for (int frame = 0; frame < 72000; ++frame) {
        const int sourceFrame = frame - engine.latencySamples();
        const auto point = sourceFrame < 0 ? PatternSample {}
            : evaluatePattern(newTug, sourceFrame / samplesPerBeat);
        const float input = static_cast<float>(0.3 * std::sin(pi2 * 997.0 * frame / sampleRate));
        const auto output = engine.process(input, -input, point, samplesPerBeat, false, false);
        check(std::isfinite(output[0]) && output[0] == -output[1],
              "accelerating scratches remain finite and preserve stereo polarity");
        if (point.wet > 0.99f && point.gate > 0.99f && std::abs(point.playbackRate) > 0.15)
            ++steadyFrames;
        else steadyFrames = 0;
        if (sourceFrame > 24000 && steadyFrames > 240) {
            const double readFrame = sourceFrame - point.delayBeats * samplesPerBeat;
            const double expected = 0.3 * std::sin(pi2 * 997.0 * readFrame / sampleRate);
            errorEnergy += std::pow(output[0] - expected, 2);
            referenceEnergy += expected * expected;
            ++comparedFrames;
        }
    }
    const double audioError = std::sqrt(errorEnergy / std::max(1.0e-12, referenceEnergy));
    check(comparedFrames > 10000 && audioError < 0.0005,
          "curved playback follows the displayed read position through forward and reverse acceleration");
    std::cout << "Motion profiles: Sub Tug speed step " << maxStep[0] << " -> " << maxStep[1]
              << ", accelerating wet frames " << movingFraction[0] * 100 << "% -> "
              << movingFraction[1] * 100 << "%; " << movingSegments << " curved intervals, "
              << clippedCuts << " clipped/gated intervals, largest continuous velocity corner "
              << largestContinuousCorner << "; audio/trajectory relative error " << audioError
              << " over " << comparedFrames << " samples; legacy golden geometry exact\n";
}

} // namespace backScratch
