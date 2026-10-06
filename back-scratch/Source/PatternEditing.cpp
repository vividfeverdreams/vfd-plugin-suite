#include "PatternEditing.h"
#include "PhraseBuilder.h"

#include <algorithm>
#include <cmath>

namespace backScratch {
namespace {

double smoothstep(double x) noexcept { return x * x * (3.0 - 2.0 * x); }

double segmentDelay(const ScratchSegment& segment, double fraction) noexcept
{
    const double shape = segment.curved
        ? smoothstep(segment.curveStart + fraction * (segment.curveEnd - segment.curveStart))
        : fraction;
    return segment.delayStart + shape * (segment.delayEnd - segment.delayStart);
}

ScratchSegment clipSegment(const ScratchSegment& source, double start, double end) noexcept
{
    auto result = source;
    const double duration = source.end - source.start;
    const double left = std::clamp((start - source.start) / duration, 0.0, 1.0);
    const double right = std::clamp((end - source.start) / duration, 0.0, 1.0);
    result.start = start;
    result.end = end;
    result.gateStart = source.gateStart + static_cast<float>(left) * (source.gateEnd - source.gateStart);
    result.gateEnd = source.gateStart + static_cast<float>(right) * (source.gateEnd - source.gateStart);
    if (source.curved) {
        // Preserve the parent polynomial rather than starting a new smoothstep
        // at each cut; doing that changes every untouched sample and its pitch.
        result.curveStart = source.curveStart + left * (source.curveEnd - source.curveStart);
        result.curveEnd = source.curveStart + right * (source.curveEnd - source.curveStart);
    } else {
        result.delayStart = segmentDelay(source, left);
        result.delayEnd = segmentDelay(source, right);
    }
    return result;
}

bool replaceSourceInterval(ScratchPattern& pattern, ScratchSegment replacement) noexcept
{
    // Generated triplets can end a few ulps away from a visible grid line.
    // Match the audio evaluator's canonical boundary instead of retaining an
    // inaudible sliver with a poorly conditioned duration/derivative.
    const double boundaryTolerance = std::max(1.0e-12, pattern.beats * 1.0e-12);
    for (int i = 0; i < pattern.count; ++i) {
        const auto& part = pattern.segments[static_cast<std::size_t>(i)];
        for (const double boundary : {part.start, part.end}) {
            if (std::abs(replacement.start - boundary) <= boundaryTolerance)
                replacement.start = boundary;
            if (std::abs(replacement.end - boundary) <= boundaryTolerance)
                replacement.end = boundary;
        }
    }
    auto result = pattern;
    result.count = 0;
    const auto append = [&result](const ScratchSegment& part) {
        if (part.end <= part.start) return true;
        // Neighboring erased cells are one continuous dry interval. Coalesce
        // these exactly, so long eraser gestures never consume extra storage.
        if (result.count > 0) {
            auto& previous = result.segments[static_cast<std::size_t>(result.count - 1)];
            if (previous.wet == 0.0f && part.wet == 0.0f
                && previous.delayStart == 0.0 && previous.delayEnd == 0.0
                && part.delayStart == 0.0 && part.delayEnd == 0.0
                && previous.gateStart == 1.0f && previous.gateEnd == 1.0f
                && part.gateStart == 1.0f && part.gateEnd == 1.0f
                && previous.end == part.start) {
                previous.end = part.end;
                return true;
            }
        }
        if (result.count >= ScratchPattern::maximumSegments) return false;
        result.segments[static_cast<std::size_t>(result.count++)] = part;
        return true;
    };
    bool inserted = false;
    for (int i = 0; i < pattern.count; ++i) {
        const auto& part = pattern.segments[static_cast<std::size_t>(i)];
        if (part.end <= replacement.start || part.start >= replacement.end) {
            if (part.start >= replacement.end && !inserted) {
                if (!append(replacement)) return false;
                inserted = true;
            }
            if (!append(part)) return false;
            continue;
        }
        if (part.start < replacement.start
            && !append(clipSegment(part, part.start, replacement.start))) return false;
        if (!inserted) {
            if (!append(replacement)) return false;
            inserted = true;
        }
        if (part.end > replacement.end
            && !append(clipSegment(part, replacement.end, part.end))) return false;
    }
    if (!inserted && !append(replacement)) return false;
    pattern = result;
    return true;
}

bool replaceVisibleInterval(ScratchPattern& pattern, const ScratchSegment& replacement) noexcept
{
    auto result = pattern;
    result.editingRange = patternEditingDelayRange(pattern);
    const double offset = patternOffsetBeats(pattern);
    // An interval crossing canonical zero consists of two exact pieces, even
    // when it appears as one continuous stroke in the shifted display.
    for (int wrap = 0; wrap <= 1; ++wrap) {
        const double start = std::max(0.0, replacement.start - offset + wrap * pattern.beats);
        const double end = std::min(pattern.beats, replacement.end - offset + wrap * pattern.beats);
        if (end <= start) continue;
        auto piece = clipSegment(replacement, start + offset - wrap * pattern.beats,
                                 end + offset - wrap * pattern.beats);
        piece.start = start;
        piece.end = end;
        if (!replaceSourceInterval(result, piece)) return false;
    }
    if (!validateEditedPattern(result)) return false;
    pattern = result;
    return true;
}

bool finiteGain(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

} // namespace

double patternEditingDelayRange(const ScratchPattern& pattern) noexcept
{
    if (!std::isfinite(pattern.beats) || pattern.beats <= 0.0) return 1.0;
    if (std::isfinite(pattern.editingRange) && pattern.editingRange > 0.0)
        return pattern.editingRange;
    double range = pattern.beats * 0.25;
    for (int i = 0; i < std::clamp(pattern.count, 0, ScratchPattern::maximumSegments); ++i) {
        const auto& part = pattern.segments[static_cast<std::size_t>(i)];
        range = std::max({range, segmentDelay(part, 0.0), segmentDelay(part, 1.0)});
    }
    return range;
}

bool validateEditedPattern(const ScratchPattern& pattern) noexcept
{
    if (!std::isfinite(pattern.beats) || pattern.beats < 1.0 / 6.0 || pattern.beats > 384.0
        || pattern.count < 1 || pattern.count > ScratchPattern::maximumSegments
        || pattern.gridOffset < 0 || pattern.gridOffset >= patternGridDivisions
        || !std::isfinite(pattern.editingRange) || pattern.editingRange < 0.0
        || pattern.editingRange > pattern.beats) return false;
    const double tolerance = pattern.beats * 1.0e-10;
    double last = 0.0;
    double maximumVisibleDelay = 0.0;
    for (int i = 0; i < pattern.count; ++i) {
        const auto& part = pattern.segments[static_cast<std::size_t>(i)];
        if (!std::isfinite(part.start) || !std::isfinite(part.end)
            || !std::isfinite(part.delayStart) || !std::isfinite(part.delayEnd)
            || !std::isfinite(part.curveStart) || !std::isfinite(part.curveEnd)
            || std::abs(part.start - last) > tolerance || part.end <= part.start
            || part.end > pattern.beats + tolerance || part.delayStart < 0.0 || part.delayEnd < 0.0
            || part.delayStart > pattern.beats || part.delayEnd > pattern.beats
            || part.curveStart < 0.0 || part.curveEnd > 1.0 || part.curveEnd <= part.curveStart
            || !finiteGain(part.gateStart) || !finiteGain(part.gateEnd) || !finiteGain(part.wet)) return false;
        const double duration = part.end - part.start;
        const double slope = (part.delayEnd - part.delayStart) / duration;
        double peakSlope = slope;
        if (part.curved) {
            const double peak = std::clamp(0.5, part.curveStart, part.curveEnd);
            peakSlope *= 6.0 * peak * (1.0 - peak) * (part.curveEnd - part.curveStart);
        }
        // The actual read position is time-delay(time). Never just clamp a
        // reported speed while leaving a steeper trajectory for the resampler.
        if (peakSlope < -1.0 - 1.0e-9 || peakSlope > 3.0 + 1.0e-9) return false;
        maximumVisibleDelay = std::max({maximumVisibleDelay, segmentDelay(part, 0.0), segmentDelay(part, 1.0)});
        last = part.end;
    }
    return std::abs(last - pattern.beats) <= tolerance
        && (pattern.editingRange == 0.0 || maximumVisibleDelay <= pattern.editingRange + tolerance);
}

bool drawPatternGridLine(ScratchPattern& pattern, int fromBoundary, double fromDelay,
                         int toBoundary, double toDelay, double* appliedToDelay) noexcept
{
    if (!validateEditedPattern(pattern) || fromBoundary < 0 || fromBoundary > patternGridDivisions
        || toBoundary < 0 || toBoundary > patternGridDivisions || fromBoundary == toBoundary
        || !std::isfinite(fromDelay) || !std::isfinite(toDelay)) return false;
    const double range = patternEditingDelayRange(pattern);
    fromDelay = std::clamp(fromDelay, 0.0, range);
    toDelay = std::clamp(toDelay, 0.0, range);
    const double from = pattern.beats * fromBoundary / patternGridDivisions;
    const double to = pattern.beats * toBoundary / patternGridDivisions;
    const double span = to - from;
    toDelay = std::clamp(toDelay, fromDelay + std::min(-span, 3.0 * span),
                         fromDelay + std::max(-span, 3.0 * span));
    ScratchSegment replacement;
    replacement.start = std::min(from, to);
    replacement.end = std::max(from, to);
    replacement.delayStart = from < to ? fromDelay : toDelay;
    replacement.delayEnd = from < to ? toDelay : fromDelay;
    if (!replaceVisibleInterval(pattern, replacement)) return false;
    if (appliedToDelay != nullptr) *appliedToDelay = toDelay;
    return true;
}

bool drawPatternLine(ScratchPattern& pattern, double fromPhase, double fromDelay,
                     double toPhase, double toDelay, bool curved, double* appliedToDelay) noexcept
{
    if (!validateEditedPattern(pattern) || !std::isfinite(fromPhase) || !std::isfinite(toPhase)
        || fromPhase < 0.0 || fromPhase > 1.0 || toPhase < 0.0 || toPhase > 1.0
        || std::abs(fromPhase - toPhase) < 1.0e-10
        || !std::isfinite(fromDelay) || !std::isfinite(toDelay)) return false;
    const double range = patternEditingDelayRange(pattern);
    fromDelay = std::clamp(fromDelay, 0.0, range);
    toDelay = std::clamp(toDelay, 0.0, range);
    const double from = pattern.beats * fromPhase;
    const double to = pattern.beats * toPhase;
    // smoothstep's peak derivative is 1.5 times its average slope.
    const double span = (to - from) / (curved ? 1.5 : 1.0);
    toDelay = std::clamp(toDelay, fromDelay + std::min(-span, 3.0 * span),
                         fromDelay + std::max(-span, 3.0 * span));
    ScratchSegment replacement;
    replacement.start = std::min(from, to);
    replacement.end = std::max(from, to);
    replacement.delayStart = from < to ? fromDelay : toDelay;
    replacement.delayEnd = from < to ? toDelay : fromDelay;
    replacement.curved = curved;
    if (!replaceVisibleInterval(pattern, replacement)) return false;
    if (appliedToDelay != nullptr) *appliedToDelay = toDelay;
    return true;
}

bool PatternFreeDrawStroke::begin(const ScratchPattern& pattern, double phase, double delay) noexcept
{
    if (!validateEditedPattern(pattern) || !std::isfinite(phase) || !std::isfinite(delay)
        || phase < 0.0 || phase > 1.0) return false;
    original = pattern;
    range = patternEditingDelayRange(pattern);
    anchorPhase = lastPhase = firstPhase = finalPhase = phase;
    anchorDepth = lastDepth = firstDepth = finalDepth = std::clamp(delay, 0.0, range);
    depths.fill(anchorDepth);
    active = true;
    return true;
}

bool PatternFreeDrawStroke::append(double phase, double delay) noexcept
{
    if (!active || !std::isfinite(phase) || !std::isfinite(delay) || phase < 0.0 || phase > 1.0)
        return false;
    delay = std::clamp(delay, 0.0, range);
    const double left = std::min(lastPhase, phase);
    const double right = std::max(lastPhase, phase);
    const auto depthAt = [&](double position) {
        if (std::abs(phase - lastPhase) < 1.0e-12) return delay;
        const double fraction = std::clamp((position - lastPhase) / (phase - lastPhase), 0.0, 1.0);
        return lastDepth + fraction * (delay - lastDepth);
    };
    for (int i = 0; i <= resolution; ++i) {
        const double position = static_cast<double>(i) / resolution;
        if (position >= left - 1.0e-12 && position <= right + 1.0e-12)
            depths[static_cast<std::size_t>(i)] = depthAt(position);
    }
    if (anchorPhase >= left - 1.0e-12 && anchorPhase <= right + 1.0e-12)
        anchorDepth = depthAt(anchorPhase);
    if (phase <= firstPhase) { firstPhase = phase; firstDepth = delay; }
    if (phase >= finalPhase) { finalPhase = phase; finalDepth = delay; }
    lastPhase = phase;
    lastDepth = delay;
    return true;
}

bool PatternFreeDrawStroke::render(ScratchPattern& destination, double* appliedDelay) const noexcept
{
    if (!active) return false;
    auto result = original;
    if (finalPhase - firstPhase < 1.0e-10) {
        // A click places ink where it happened. It does not invent a steep
        // ramp from the bottom of an unrelated grid cell.
        const double halfWidth = 0.5 / resolution;
        const double left = std::max(0.0, lastPhase - halfWidth);
        const double right = std::min(1.0, lastPhase + halfWidth);
        if (!drawPatternLine(result, left, lastDepth, right, lastDepth)) return false;
        destination = result;
        if (appliedDelay != nullptr) *appliedDelay = lastDepth;
        return true;
    }

    struct Point { double phase, depth; int priority; };
    std::array<Point, resolution + 5> points {};
    int count = 0;
    for (int i = 0; i <= resolution; ++i) {
        const double phase = static_cast<double>(i) / resolution;
        if (phase > firstPhase && phase < finalPhase)
            points[static_cast<std::size_t>(count++)] = {phase, depths[static_cast<std::size_t>(i)], 0};
    }
    points[static_cast<std::size_t>(count++)] = {firstPhase, firstDepth, 1};
    points[static_cast<std::size_t>(count++)] = {finalPhase, finalDepth, 1};
    points[static_cast<std::size_t>(count++)] = {anchorPhase, anchorDepth, 2};
    points[static_cast<std::size_t>(count++)] = {lastPhase, lastDepth, 3};
    std::sort(points.begin(), points.begin() + count, [](const Point& left, const Point& right) {
        return left.phase == right.phase ? left.priority < right.priority : left.phase < right.phase;
    });
    int unique = 0;
    for (int i = 0; i < count; ++i) {
        if (unique > 0 && points[static_cast<std::size_t>(i)].phase
            - points[static_cast<std::size_t>(unique - 1)].phase < 1.0e-10) {
            if (points[static_cast<std::size_t>(i)].priority >= points[static_cast<std::size_t>(unique - 1)].priority)
                points[static_cast<std::size_t>(unique - 1)] = points[static_cast<std::size_t>(i)];
        } else {
            points[static_cast<std::size_t>(unique++)] = points[static_cast<std::size_t>(i)];
        }
    }
    count = unique;
    std::array<double, resolution + 5> smoothed {};
    int anchor = 0;
    int current = 0;
    for (int i = 0; i < count; ++i) {
        const auto& point = points[static_cast<std::size_t>(i)];
        smoothed[static_cast<std::size_t>(i)] = point.depth;
        if (std::abs(point.phase - anchorPhase) < 1.0e-10) anchor = i;
        if (std::abs(point.phase - lastPhase) < 1.0e-10) current = i;
        if (i == 0 || i + 1 == count) continue;
        const auto& previous = points[static_cast<std::size_t>(i - 1)];
        const auto& next = points[static_cast<std::size_t>(i + 1)];
        const double fraction = (point.phase - previous.phase) / (next.phase - previous.phase);
        const double neighbourLine = previous.depth + fraction * (next.depth - previous.depth);
        // Spatial smoothing removes hand jitter without changing straight
        // strokes or adding a time-dependent lag behind the mouse.
        smoothed[static_cast<std::size_t>(i)] = 0.6 * point.depth + 0.4 * neighbourLine;
    }
    smoothed[static_cast<std::size_t>(anchor)] = points[static_cast<std::size_t>(anchor)].depth;
    for (int i = anchor + 1; i < count; ++i) {
        const double previous = smoothed[static_cast<std::size_t>(i - 1)];
        const double duration = original.beats * (points[static_cast<std::size_t>(i)].phase
                                                   - points[static_cast<std::size_t>(i - 1)].phase);
        smoothed[static_cast<std::size_t>(i)] = std::clamp(smoothed[static_cast<std::size_t>(i)],
                                                         previous - duration, previous + 3.0 * duration);
    }
    for (int i = anchor - 1; i >= 0; --i) {
        const double next = smoothed[static_cast<std::size_t>(i + 1)];
        const double duration = original.beats * (points[static_cast<std::size_t>(i + 1)].phase
                                                   - points[static_cast<std::size_t>(i)].phase);
        smoothed[static_cast<std::size_t>(i)] = std::clamp(smoothed[static_cast<std::size_t>(i)],
                                                         next - 3.0 * duration, next + duration);
    }

    // Clear the whole affected interval before inserting samples. Otherwise
    // replacing a dense old pattern could run out of slots halfway through,
    // even though the finished replacement is much smaller than the original.
    if (!drawPatternLine(result, firstPhase, 0.0, finalPhase, 0.0)) return false;
    int start = 0;
    while (start + 1 < count) {
        int end = start + 1;
        const double slope = (smoothed[static_cast<std::size_t>(end)] - smoothed[static_cast<std::size_t>(start)])
            / (points[static_cast<std::size_t>(end)].phase - points[static_cast<std::size_t>(start)].phase);
        while (end + 1 < count) {
            const double expected = smoothed[static_cast<std::size_t>(start)]
                + slope * (points[static_cast<std::size_t>(end + 1)].phase - points[static_cast<std::size_t>(start)].phase);
            if (std::abs(expected - smoothed[static_cast<std::size_t>(end + 1)]) > original.beats * 1.0e-11) break;
            ++end;
        }
        if (!drawPatternLine(result, points[static_cast<std::size_t>(start)].phase, smoothed[static_cast<std::size_t>(start)],
                              points[static_cast<std::size_t>(end)].phase, smoothed[static_cast<std::size_t>(end)])) return false;
        start = end;
    }
    destination = result;
    if (appliedDelay != nullptr) *appliedDelay = smoothed[static_cast<std::size_t>(current)];
    return true;
}

bool drawPatternRoll(ScratchPattern& pattern, double fromPhase, double toPhase, double grain, RollStyle style) noexcept
{
    if (!validateEditedPattern(pattern) || !std::isfinite(fromPhase) || !std::isfinite(toPhase) || !std::isfinite(grain)
        || grain <= 1.0e-4 || grain > 1.0) return false;
    fromPhase = std::clamp(fromPhase, 0.0, 1.0);
    toPhase = std::clamp(toPhase, 0.0, 1.0);
    if (fromPhase > toPhase) std::swap(fromPhase, toPhase);
    if (toPhase - fromPhase < 1.0e-6) return false;
    PhraseBuilder builder(pattern.beats);
    switch (style) {
        case RollStyle::repeat: builder.roll(fromPhase, toPhase, grain); break;
        case RollStyle::reverse: builder.reverseRoll(fromPhase, toPhase, grain); break;
        case RollStyle::accelerate: builder.accelerate(fromPhase, toPhase, {grain, grain * 0.5, grain * 0.25}); break;
        case RollStyle::chopped: builder.roll(fromPhase, toPhase, grain, 0.35); break;
    }
    const auto built = builder.finish();
    const double from = fromPhase * pattern.beats - 1.0e-9, to = toPhase * pattern.beats + 1.0e-9;
    double deepest = 0.0;
    for (int index = 0; index < built.count; ++index) {
        const auto& part = built.segments[static_cast<std::size_t>(index)];
        if (part.start >= from && part.end <= to)
            deepest = std::max({deepest, segmentDelay(part, 0.0), segmentDelay(part, 1.0)});
    }
    auto result = pattern;
    result.editingRange = std::min(pattern.beats, std::max(patternEditingDelayRange(pattern), deepest));
    for (int index = 0; index < built.count; ++index) {
        const auto& part = built.segments[static_cast<std::size_t>(index)];
        if (part.start < from || part.end > to) continue;
        if (!replaceVisibleInterval(result, part)) return false;
    }
    pattern = result;
    return true;
}

const char* scratchShapeName(int index) noexcept
{
    static constexpr std::array<const char*, scratchShapeCount> names {
        "Baby Scratch", "Double Push", "Slow Drag", "Rubber Band", "Chirp",
        "Reverse Swell", "Stop & Go", "Triplet Bounce", "Late Scratch", "Wobble Roll",
        "Accelerating Roll", "Triplet Roll", "Reverse Roll", "Tape Stop", "Half-Time Drag"
    };
    return names[static_cast<std::size_t>(std::clamp(index, 0, scratchShapeCount - 1))];
}

ScratchPattern makeScratchShape(const ScratchPattern& templatePattern, int index) noexcept
{
    const double beats = std::isfinite(templatePattern.beats) ? std::clamp(templatePattern.beats, 1.0 / 6.0, 384.0) : 4.0;
    if (index >= 10) {
        // Roll and tape starters, written in fractions of the phrase.
        PhraseBuilder builder(beats);
        switch (std::clamp(index, 10, scratchShapeCount - 1)) {
            case 10: builder.accelerate(0.5, 1.0, {0.125, 0.0625, 0.03125}); break;
            case 11: builder.roll(0.5, 1.0, 1.0 / 24.0); break;
            case 12: builder.reverseRoll(0.5, 1.0, 0.0625); break;
            case 13: builder.tapeStop(0.5, 1.0); break;
            default: builder.halfTime(0.5, 1.0); break;
        }
        auto shape = builder.finish();
        shape.gridOffset = wrapPatternGridOffset(templatePattern.gridOffset);
        return shape;
    }
    ScratchPattern pattern;
    pattern.beats = beats;
    pattern.gridOffset = wrapPatternGridOffset(templatePattern.gridOffset);
    pattern.editingRange = pattern.beats * 0.25;
    double position = 0.0;
    const auto append = [&](double end, double depthStart, double depthEnd, bool curved, float wet) {
        if (end <= position) return;
        auto& segment = pattern.segments[static_cast<std::size_t>(pattern.count++)];
        segment = {position * pattern.beats, end * pattern.beats,
                   depthStart * pattern.beats, depthEnd * pattern.beats, 1.0f, 1.0f, wet, curved};
        position = end;
    };
    const auto stroke = [&](double start, double peak, double end, double depth) {
        append(start, 0.0, 0.0, false, 0.0f);
        append(peak, 0.0, depth, true, 1.0f);
        append(end, depth, 0.0, true, 1.0f);
    };
    switch (std::clamp(index, 0, scratchShapeCount - 1)) {
        case 0: stroke(0.10, 0.22, 0.59, 0.18); break;
        case 1: stroke(0.07, 0.13, 0.29, 0.09); stroke(0.53, 0.59, 0.75, 0.09); break;
        case 2: stroke(0.06, 0.24, 0.70, 0.22); break;
        case 3: stroke(0.12, 0.21, 0.55, 0.16); break;
        case 4: stroke(0.12, 0.155, 0.275, 0.055); stroke(0.36, 0.395, 0.515, 0.055); break;
        case 5: stroke(0.05, 0.24, 0.86, 0.24); break;
        case 6:
            stroke(0.08, 0.125, 0.245, 0.075);
            stroke(0.41, 0.485, 0.685, 0.125);
            break;
        case 7:
            for (int repeat = 0; repeat < 3; ++repeat) {
                const double start = 0.08 + repeat / 3.0;
                stroke(start, start + 1.0 / 24.0, start + 0.15, 0.065);
            }
            break;
        case 8: stroke(0.51, 0.62, 0.97, 0.17); break;
        case 9:
            for (int repeat = 0; repeat < 4; ++repeat) {
                const double start = 0.08 + 0.21 * repeat;
                stroke(start, start + 0.025, start + 0.105, 0.04);
            }
            break;
    }
    append(1.0, 0.0, 0.0, false, 0.0f);
    return pattern;
}

bool erasePatternGridCells(ScratchPattern& pattern, int firstCell, int lastCell) noexcept
{
    if (!validateEditedPattern(pattern) || firstCell < 0 || lastCell < 0
        || firstCell >= patternGridDivisions || lastCell >= patternGridDivisions) return false;
    if (lastCell < firstCell) std::swap(firstCell, lastCell);
    ScratchSegment replacement;
    replacement.start = pattern.beats * firstCell / patternGridDivisions;
    replacement.end = pattern.beats * (lastCell + 1) / patternGridDivisions;
    replacement.wet = 0.0f;
    return replaceVisibleInterval(pattern, replacement);
}

} // namespace backScratch
