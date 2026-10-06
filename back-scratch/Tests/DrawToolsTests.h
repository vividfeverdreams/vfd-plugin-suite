#pragma once

#include "PatternEditingTests.h"

namespace backScratch {

inline void runDrawToolsTests()
{
    using namespace patternEditingTests;
    // Sparse/coalesced UI events describe the same straight pointer path as
    // hundreds of small events; the resulting geometry and audio must agree.
    auto original = dryPattern();
    PatternFreeDrawStroke sparse;
    PatternFreeDrawStroke dense;
    require(sparse.begin(original, 0.173, 0.12) && dense.begin(original, 0.173, 0.12),
            "free draw starts at fractional cursor position/depth");
    require(sparse.append(0.827, 0.63), "free draw interpolates a coalesced mouse movement");
    for (int step = 1; step <= 387; ++step) {
        const double fraction = static_cast<double>(step) / 387.0;
        require(dense.append(0.173 + fraction * 0.654, 0.12 + fraction * 0.51),
                "free draw accepts high-frequency mouse events");
    }
    ScratchPattern a, b;
    double endpoint = -1.0;
    require(sparse.render(a, &endpoint) && dense.render(b), "free draw produces complete patterns");
    require(std::abs(endpoint - 0.63) < 1.0e-10 && a.count <= 4 && b.count <= 4,
            "straight freehand strokes simplify without cursor lag or extra pitch steps");
    for (int sample = 0; sample < 4096; ++sample) {
        const double beat = original.beats * (sample + 0.37) / 4096.0;
        sameSample(evaluatePattern(a, beat), evaluatePattern(b, beat));
    }
    preservedOutside(original, a, original.beats * 0.173, original.beats * 0.827);
    analyticRates(a);

    // The clicked point is a narrow horizontal mark at that exact depth. In
    // particular, it cannot become the old bottom-to-cell-top pitch spike.
    for (const double phase : {0.0, 0.333, 1.0}) {
        PatternFreeDrawStroke click;
        require(click.begin(original, phase, 0.47) && click.render(a), "click draws at both graph edges");
        const double probe = phase == 1.0 ? 1.0 - 1.0e-6 : phase + 1.0e-6;
        const auto sample = evaluatePattern(a, original.beats * probe);
        require(std::abs(sample.delayBeats - 0.47) < 1.0e-9 && sample.playbackRate == 1.0,
                "click does not reset to the baseline or invent a scratch ramp");
    }

    // Reversing direction redraws existing ink, and expanding then contracting
    // a Line/Curve preview is implemented by replacing from the original.
    PatternFreeDrawStroke backwards;
    require(backwards.begin(original, 0.82, 0.8) && backwards.append(0.15, 0.12)
                && backwards.render(a), "free draw follows a right-to-left gesture");
    analyticRates(a);
    const auto beforeRevisit = a;
    require(backwards.append(0.60, 0.3) && backwards.render(a, &endpoint),
            "returning over existing freehand ink redraws that area");
    require(std::abs(evaluatePattern(a, original.beats * 0.5).delayBeats
                         - evaluatePattern(beforeRevisit, original.beats * 0.5).delayBeats) > 0.05,
            "revisiting a stroke does not ignore the second pass");
    require(std::abs(evaluatePattern(a, original.beats * 0.60).delayBeats - endpoint) < 1.0e-8,
            "freehand cursor reports the exact constrained audible endpoint");
    preservedOutside(original, a, original.beats * 0.15, original.beats * 0.82);
    analyticRates(a);

    for (int shift = 0; shift < patternGridDivisions; ++shift) {
        auto factory = makePattern((shift * 7) % factoryPresetCount);
        factory.gridOffset = shift;
        const double range = patternEditingDelayRange(factory);
        for (const bool curved : {false, true}) {
            auto drawn = factory;
            require(drawPatternLine(drawn, 0.2137, range * 0.15, 0.6731, range * 0.88, curved),
                    "fractional Line/Curve edits accept shifted factory trajectories");
            preservedOutside(factory, drawn, factory.beats * 0.2137, factory.beats * 0.6731);
            analyticRates(drawn);
            if (curved) {
                const auto first = evaluatePattern(drawn, factory.beats * (0.2137 + 1.0e-8));
                const auto last = evaluatePattern(drawn, factory.beats * (0.6731 - 1.0e-8));
                require(std::abs(first.playbackRate - 1.0) < 1.0e-5 && std::abs(last.playbackRate - 1.0) < 1.0e-5,
                        "Curve eases its speed naturally at both ends");
            }
        }
        PatternFreeDrawStroke stroke;
        require(stroke.begin(factory, 0.183, range * 0.2), "freehand initializes from shifted factory");
        for (int point = 1; point <= 200; ++point) {
            const double phase = 0.183 + 0.621 * point / 200.0;
            const double delay = range * (0.5 + 0.4 * std::sin(point * 0.047));
            require(stroke.append(phase, delay), "freehand samples a complex gesture");
        }
        require(stroke.render(a), "complex freehand gesture fits fixed storage");
        require(a.count < 200, "spatial sampling bounds complexity independently of UI event rate");
        preservedOutside(factory, a, factory.beats * 0.183, factory.beats * 0.804);
        analyticRates(a);
    }

    // Full-phrase redraw removes the old coverage before adding its own, so a
    // previously dense phrase cannot reject a simpler new stroke mid-preview.
    auto full = original;
    full.count = ScratchPattern::maximumSegments;
    for (int i = 0; i < full.count; ++i) {
        const double start = full.beats * i / full.count;
        const double end = full.beats * (i + 1) / full.count;
        full.segments[static_cast<std::size_t>(i)] = {start, end, 0.0, 0.0, 1.0f, 1.0f, 1.0f, false};
    }
    PatternFreeDrawStroke replaceDense;
    require(replaceDense.begin(full, 0.0, 0.0) && replaceDense.append(1.0, 0.5)
                && replaceDense.render(a) && a.count == 1,
            "a complete redraw replaces even a maximum-capacity pattern transactionally");

    // Shape previews are the exact same geometry used by the processor. Each
    // contour has genuine reverse motion and smooth arrival/departure speeds.
    // The first ten are rounded turntable contours; the five 0.11 roll/tape
    // starters are checked separately below (rolls jump by design).
    constexpr int roundedShapes = 10;
    for (int shape = 0; shape < roundedShapes; ++shape) {
        require(scratchShapeName(shape)[0] != '\0', "every scratch starter has a readable name");
        for (const double beats : {1.0 / 6.0, 0.5, 4.0, 12.0, 64.0, 384.0}) {
            auto source = original;
            source.beats = beats;
            source.gridOffset = shape % patternGridDivisions;
            auto contour = makeScratchShape(source, shape);
            require(contour.beats == beats && contour.gridOffset == source.gridOffset,
                    "shape selection preserves current phrase length and shift");
            analyticRates(contour);
            bool reverses = false;
            double previousDelay = 0.0;
            for (int segment = 0; segment < contour.count; ++segment) {
                const auto& part = contour.segments[static_cast<std::size_t>(segment)];
                const auto start = evaluatePatternSegment(contour, segment, part.start);
                const auto finish = evaluatePatternSegment(contour, segment, part.end);
                require(std::abs(start.delayBeats - previousDelay) < 1.0e-9
                            && std::abs(start.playbackRate - 1.0) < 1.0e-9
                            && std::abs(finish.playbackRate - 1.0) < 1.0e-9,
                        "scratch starters have continuous source positions and eased speed at every join");
                previousDelay = finish.delayBeats;
                reverses = reverses || evaluatePatternSegment(contour, segment, (part.start + part.end) * 0.5).playbackRate < 0.0;
            }
            require(reverses && std::abs(previousDelay) < 1.0e-9,
                    "every scratch starter actually reverses and returns cleanly to live audio");
            require(drawPatternLine(contour, 0.11, 0.0, 0.28, beats * 0.1, true)
                        && erasePatternGridCells(contour, 10, 12),
                    "selected scratch starters remain fully drawable and erasable");
        }
    }
    for (int shape = roundedShapes; shape < scratchShapeCount; ++shape) {
        require(scratchShapeName(shape)[0] != '\0', "every roll starter has a readable name");
        for (const double beats : {1.0 / 6.0, 0.5, 4.0, 12.0, 64.0, 384.0}) {
            auto source = original;
            source.beats = beats;
            source.gridOffset = shape % patternGridDivisions;
            auto contour = makeScratchShape(source, shape);
            require(contour.beats == beats && contour.gridOffset == source.gridOffset && validateEditedPattern(contour),
                    "roll and tape starters keep length and shift and stay inside the speed and causality limits");
            analyticRates(contour);
            const double shifted = patternOffsetBeats(contour);
            require(evaluatePattern(contour, 0.25 * beats + shifted).wet == 0.0f
                        && evaluatePattern(contour, 0.75 * beats + shifted).wet > 0.0f,
                    "roll and tape starters leave the first half live and act on the second half");
            require(erasePatternGridCells(contour, 10, 12), "roll starters remain erasable");
        }
    }
    // The roll tool replaces only the dragged span and re-reads one slice.
    {
        auto rolled = original;
        require(drawPatternRoll(rolled, 0.5, 0.75, 1.0 / 32.0, RollStyle::repeat) && validateEditedPattern(rolled),
                "a dragged sixteenth roll is valid geometry");
        preservedOutside(original, rolled, original.beats * 0.5, original.beats * 0.75);
        for (int pass = 1; pass < 8; ++pass) {
            const double at = (0.5 + pass / 32.0 + 1.0 / 64.0) * rolled.beats;
            const auto value = evaluatePattern(rolled, at);
            require(value.wet > 0.0f && std::abs(at - value.delayBeats - (0.5 + 1.0 / 64.0) * rolled.beats) < 1.0e-9,
                    "each roll pass re-reads the slice where the roll began");
        }
        for (const auto style : {RollStyle::reverse, RollStyle::accelerate, RollStyle::chopped}) {
            auto other = original;
            require(drawPatternRoll(other, 0.25, 0.5, 1.0 / 16.0, style) && validateEditedPattern(other),
                    "reverse, accelerating and chopped rolls are valid geometry");
            analyticRates(other);
        }
        auto unchangedRoll = original;
        require(!drawPatternRoll(unchangedRoll, 0.3, 0.3, 0.1, RollStyle::repeat)
                    && !drawPatternRoll(unchangedRoll, 0.1, 0.2, 0.0, RollStyle::repeat),
                "empty rolls are rejected without changing the pattern");
    }

    // Aggressive slopes are constrained in their actual analytic geometry,
    // and invalid events never corrupt an otherwise valid preview.
    a = original;
    require(drawPatternLine(a, 0.41, 0.0, 0.46, 99.0, true, &endpoint)
                && std::abs(endpoint - original.beats * 0.1) < 1.0e-9,
            "Curve constraints account for its 1.5x peak derivative");
    analyticRates(a);
    const auto unchanged = a;
    require(!drawPatternLine(a, 0.2, 0.0, 0.2, 0.8)
                && !drawPatternLine(a, -0.1, 0.0, 0.2, 0.8)
                && !drawPatternLine(a, 0.1, 0.0, 0.2, std::numeric_limits<double>::quiet_NaN()),
            "invalid fractional strokes are rejected");
    preservedOutside(unchanged, a, -1.0, -0.5);
    require(!dense.append(0.5, std::numeric_limits<double>::infinity()) && dense.render(b),
            "invalid freehand event preserves the previous complete stroke");
    std::cout << "Drawing tools: event-independent freehand, fractional/reversed lines, eased curves, exact shifted clipping, 10 musical shapes, 5 roll/tape starters and the roll tool passed\n";
}

} // namespace backScratch
