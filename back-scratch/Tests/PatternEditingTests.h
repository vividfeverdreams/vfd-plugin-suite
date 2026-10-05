#pragma once

#include "PatternEditing.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace backScratch {
namespace patternEditingTests {

inline void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

inline void sameSample(const PatternSample& a, const PatternSample& b)
{
    require(std::abs(a.delayBeats - b.delayBeats) < 2.0e-9
                && std::abs(a.playbackRate - b.playbackRate) < 2.0e-9
                && std::abs(a.gate - b.gate) < 2.0e-6f && a.wet == b.wet,
            "grid edits preserve the exact audible trajectory outside the selected interval");
}

inline void preservedOutside(const ScratchPattern& before, const ScratchPattern& after,
                             double left, double right)
{
    for (int sample = 0; sample < 1024; ++sample) {
        const double beat = before.beats * (sample + 0.37) / 1024.0;
        if (beat >= left && beat < right) continue;
        sameSample(evaluatePattern(before, beat), evaluatePattern(after, beat));
    }
}

inline void analyticRates(const ScratchPattern& pattern)
{
    require(validateEditedPattern(pattern), "custom geometry remains finite, causal, completely covered and rate-bounded");
    for (int segment = 0; segment < pattern.count; ++segment) {
        const auto& part = pattern.segments[static_cast<std::size_t>(segment)];
        for (int point = 0; point < 17; ++point) {
            const double beat = part.start + (part.end - part.start) * (point + 0.37) / 17.0;
            const double epsilon = (part.end - part.start) * 1.0e-5;
            const auto value = evaluatePatternSegment(pattern, segment, beat);
            const double left = evaluatePatternSegment(pattern, segment, beat - epsilon).delayBeats;
            const double right = evaluatePatternSegment(pattern, segment, beat + epsilon).delayBeats;
            if (std::abs(value.playbackRate - (1.0 - (right - left) / (2.0 * epsilon))) >= 2.0e-7)
                std::cerr << "Derivative mismatch at " << part.start << ".." << part.end
                          << ", domain " << part.curveStart << ".." << part.curveEnd
                          << ", analytic " << value.playbackRate << ", numeric "
                          << 1.0 - (right - left) / (2.0 * epsilon) << '\n';
            require(std::abs(value.playbackRate - (1.0 - (right - left) / (2.0 * epsilon))) < 2.0e-7,
                    "drawn/clipped graph derivatives match the actual DSP readhead speed");
            require(value.playbackRate >= -2.0 - 1.0e-8 && value.playbackRate <= 2.0 + 1.0e-8,
                    "aggressive pencil motions cannot produce excessive forward or reverse pitch");
        }
    }
}

inline ScratchPattern dryPattern()
{
    ScratchPattern pattern;
    pattern.count = 1;
    pattern.segments[0] = {0.0, 4.0, 0.0, 0.0, 1.0f, 1.0f, 0.0f, false};
    return pattern;
}

} // namespace patternEditingTests

inline void runPatternEditingTests()
{
    using namespace patternEditingTests;
    for (int preset = 0; preset < factoryPresetCount; ++preset) {
        for (int shift = 0; shift < patternGridDivisions; ++shift) {
            auto original = makePattern(preset);
            original.gridOffset = shift;
            require(validateEditedPattern(original), "all existing factory trajectories are editable");
            auto erased = original;
            require(erasePatternGridCells(erased, 4, 6), "eraser accepts every shifted factory pattern");
            const double step = patternGridStepBeats(original);
            preservedOutside(original, erased, 4.0 * step, 7.0 * step);
            for (int cell = 4; cell <= 6; ++cell) {
                const auto value = evaluatePattern(erased, (cell + 0.5) * step);
                require(value.wet == 0.0f && value.gate == 1.0f && value.delayBeats == 0.0,
                        "erase restores live dry audio in the visible shifted cells");
            }
            auto drawn = original;
            const double range = patternEditingDelayRange(original);
            require(drawPatternGridLine(drawn, 5, range * 0.2, 9, range * 0.9),
                    "pencil draws across every shifted factory pattern");
            preservedOutside(original, drawn, 5.0 * step, 9.0 * step);
            analyticRates(drawn);
        }
    }

    // Cutting through the middle of a smoothstep must not re-ease its sides.
    auto curved = dryPattern();
    curved.segments[0] = {0.0, 4.0, 0.0, 2.0, 0.2f, 0.9f, 1.0f, true};
    const auto completeCurve = curved;
    require(erasePatternGridCells(curved, 4, 7), "eraser splits an original smooth curve");
    require(curved.count == 3 && curved.segments[0].curveEnd == 0.25
                && curved.segments[2].curveStart == 0.5,
            "clipped curves retain the original polynomial domain");
    preservedOutside(completeCurve, curved, 1.0, 2.0);
    analyticRates(curved);
    const auto onceClipped = curved;
    require(erasePatternGridCells(curved, 10, 11), "curves can be clipped repeatedly");
    preservedOutside(onceClipped, curved, 2.5, 3.0);
    analyticRates(curved);

    auto line = dryPattern();
    double applied = -1.0;
    require(drawPatternGridLine(line, 0, 0.0, 1, 999.0, &applied) && applied == 0.75,
            "one-cell click caps its actual rising trajectory at reverse 2x");
    require(evaluatePattern(line, 0.125).playbackRate == -2.0,
            "one-click ramp creates an immediate scratch, not a static delay");
    require(drawPatternGridLine(line, 1, applied, 2, 0.0, &applied) && applied == 0.5,
            "downward motion returns its real constrained endpoint to continue a connected stroke");
    require(drawPatternGridLine(line, 9, 0.0, 8, 1.0, &applied) && applied == 0.25,
            "backward drawing constrains speeds in chronological time");
    analyticRates(line);

    const double range = patternEditingDelayRange(line);
    require(erasePatternGridCells(line, 15, 0) && line.count == 1,
            "eraser handles a reversed drag and coalesces the completely dry phrase");
    require(line.editingRange == range && patternEditingDelayRange(line) == range,
            "erasing every stroke preserves the vertical zoom for the next drawing");

    // Grid-relative edits stay musical under every phrase length and timing
    // choice: the curve is retimed once, not repeated or recomposed.
    auto edited = makePattern(1);
    edited.gridOffset = 11;
    require(drawPatternGridLine(edited, 2, 0.1, 5, 0.65)
                && erasePatternGridCells(edited, 8, 9), "prepare a mixed custom pattern");
    for (const double beats : {1.0 / 6.0, 0.5, 2.0 / 3.0, 1.5, 4.0, 12.0, 64.0, 384.0}) {
        const auto retimed = fitPatternToPhrase(edited, beats);
        require(retimed.editingRange == edited.editingRange * beats / edited.beats,
                "retiming preserves the custom graph's visible proportions");
        analyticRates(retimed);
        for (int sample = 0; sample < 256; ++sample) {
            const double phase = (sample + 0.23) / 256.0;
            auto expected = evaluatePattern(edited, edited.beats * phase);
            expected.delayBeats *= beats / edited.beats;
            sameSample(expected, evaluatePattern(retimed, beats * phase));
        }
    }

    for (int seed = 1; seed <= 100; ++seed) {
        auto generated = generatePattern(static_cast<std::uint32_t>(seed * 817), 64.0);
        generated.gridOffset = seed % 16;
        for (int gesture = 0; gesture < 32; ++gesture) {
            const int cell = (seed * 7 + gesture * 5) % 16;
            require(drawPatternGridLine(generated, cell, 0.0, cell + 1,
                                        patternEditingDelayRange(generated)),
                    "repeated dense generated edits remain within fixed storage");
            if (gesture % 3 == 0)
                require(erasePatternGridCells(generated, cell, cell), "dense generated erases remain safe");
        }
        analyticRates(generated);
    }

    const auto intact = edited;
    require(!drawPatternGridLine(edited, 1, 0.0, 1, 1.0)
                && !drawPatternGridLine(edited, -1, 0.0, 2, 1.0)
                && !drawPatternGridLine(edited, 1, 0.0, 2, std::numeric_limits<double>::quiet_NaN())
                && !erasePatternGridCells(edited, 0, 16), "invalid edit requests fail without mutation");
    preservedOutside(intact, edited, -1.0, -0.5);
    for (int field = 0; field < 6; ++field) {
        auto invalid = intact;
        switch (field) {
            case 0: invalid.segments[0].curveStart = -0.1; break;
            case 1: invalid.segments[0].curveEnd = 2.0; break;
            case 2: invalid.segments[0].delayEnd = 1000.0; break;
            case 3: invalid.segments[0].gateStart = -0.1f; break;
            case 4: invalid.segments[0].start = 0.1; break;
            case 5: invalid.editingRange = std::numeric_limits<double>::infinity(); break;
        }
        require(!validateEditedPattern(invalid), "malformed custom states are rejected before rendering");
    }
    auto steep = dryPattern();
    steep.segments[0].delayEnd = 1.0;
    steep.segments[0].end = 0.1;
    steep.count = 2;
    steep.segments[1] = {0.1, 4.0, 0.0, 0.0, 1.0f, 1.0f, 0.0f, false};
    require(!validateEditedPattern(steep), "import validation rejects unsafe actual trajectories");

    auto full = dryPattern();
    full.count = ScratchPattern::maximumSegments;
    full.segments[0] = {0.0, 1.0, 0.0, 0.0, 1.0f, 1.0f, 1.0f, false};
    for (int i = 1; i < full.count; ++i) {
        const double start = 1.0 + 3.0 * (i - 1) / (full.count - 1);
        const double end = 1.0 + 3.0 * i / (full.count - 1);
        full.segments[static_cast<std::size_t>(i)] = {start, end, 0.0, 0.0, 1.0f, 1.0f, 1.0f, false};
    }
    const auto fullOriginal = full;
    applied = -1.0;
    require(validateEditedPattern(full) && !drawPatternGridLine(full, 1, 0.0, 2, 0.5, &applied)
                && !erasePatternGridCells(full, 1, 1) && applied == -1.0
                && full.count == fullOriginal.count && full.editingRange == 0.0,
            "fixed-storage exhaustion rejects the entire edit, including metadata and returned endpoint");
    preservedOutside(fullOriginal, full, -1.0, -0.5);
    std::cout << "Grid editing: all 75 factories at 16 shifts, exact curve clipping, safe draw slopes, dry erasing and retiming passed\n";
}

} // namespace backScratch
