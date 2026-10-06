#include "PhraseBuilder.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace backScratch {
namespace {
constexpr double epsilon = 1.0e-9;
}

PhraseBuilder::PhraseBuilder(double beats) noexcept
{
    pattern.beats = std::isfinite(beats) && beats > 0.0 ? beats : 1.0;
}

void PhraseBuilder::segment(double t0, double t1, double d0, double d1, bool curved,
                            float g0, float g1, float wet, double c0, double c1) noexcept
{
    t0 = std::max(std::clamp(t0, 0.0, 1.0), cursor);
    t1 = std::clamp(t1, 0.0, 1.0);
    if (t1 <= t0 + epsilon || pattern.count >= ScratchPattern::maximumSegments - 1) return;
    dryTo(t0);
    if (pattern.count >= ScratchPattern::maximumSegments - 1) return;
    const double beats = pattern.beats;
    pattern.segments[static_cast<std::size_t>(pattern.count++)] =
        {cursor * beats, t1 * beats, std::max(0.0, d0) * beats, std::max(0.0, d1) * beats,
         std::clamp(g0, 0.0f, 1.0f), std::clamp(g1, 0.0f, 1.0f), std::clamp(wet, 0.0f, 1.0f), curved, c0, c1};
    cursor = t1;
}

PhraseBuilder& PhraseBuilder::dryTo(double t) noexcept
{
    t = std::clamp(t, 0.0, 1.0);
    if (t <= cursor + epsilon) return *this;
    const double beats = pattern.beats;
    if (pattern.count > 0) {
        auto& previous = pattern.segments[static_cast<std::size_t>(pattern.count - 1)];
        if (previous.wet == 0.0f && previous.delayStart == 0.0 && previous.delayEnd == 0.0
            && previous.gateStart == 1.0f && previous.gateEnd == 1.0f) {
            previous.end = t * beats;
            cursor = t;
            return *this;
        }
    }
    if (pattern.count >= ScratchPattern::maximumSegments) return *this;
    pattern.segments[static_cast<std::size_t>(pattern.count++)] =
        {cursor * beats, t * beats, 0.0, 0.0, 1.0f, 1.0f, 0.0f, false};
    cursor = t;
    return *this;
}

PhraseBuilder& PhraseBuilder::roll(double t0, double t1, double grain, double gap) noexcept
{
    if (!(grain > epsilon)) return *this;
    gap = std::clamp(gap, 0.0, 0.9);
    for (int pass = 0; ; ++pass) {
        const double start = t0 + pass * grain;
        if (start >= t1 - epsilon || pattern.count >= ScratchPattern::maximumSegments - 3) break;
        const double end = std::min(start + grain, t1);
        const double delay = pass * grain;
        const double open = gap > 0.0 ? std::max(start, end - gap * grain) : end;
        segment(start, open, delay, delay);
        if (open < end) segment(open, end, delay, delay, false, 0.0f, 0.0f);
    }
    return *this;
}

PhraseBuilder& PhraseBuilder::accelerate(double t0, double t1, std::initializer_list<double> grains, double gap) noexcept
{
    const std::vector<double> stages(grains);
    if (stages.empty() || t1 <= t0) return *this;
    const double section = (t1 - t0) / static_cast<double>(stages.size());
    gap = std::clamp(gap, 0.0, 0.9);
    for (std::size_t index = 0; index < stages.size(); ++index) {
        const double grain = stages[index];
        if (!(grain > epsilon)) continue;
        const double from = t0 + section * static_cast<double>(index);
        const double to = index + 1 == stages.size() ? t1 : from + section;
        for (double start = from; start < to - epsilon; start += grain) {
            if (pattern.count >= ScratchPattern::maximumSegments - 3) break;
            const double end = std::min(start + grain, to);
            // Always re-read the hit at t0, so the roll speeds up on one sound.
            const double delay = start - t0;
            const double open = gap > 0.0 ? std::max(start, end - gap * grain) : end;
            segment(start, open, delay, delay);
            if (open < end) segment(open, end, delay, delay, false, 0.0f, 0.0f);
        }
    }
    return *this;
}

PhraseBuilder& PhraseBuilder::reverseRoll(double t0, double t1, double grain, double gap) noexcept
{
    if (!(grain > epsilon)) return *this;
    gap = std::clamp(gap, 0.0, 0.9);
    for (int pass = 0; ; ++pass) {
        const double start = t0 + pass * grain;
        if (start >= t1 - epsilon || pattern.count >= ScratchPattern::maximumSegments - 3) break;
        const double end = std::min(start + grain, t1);
        const double open = gap > 0.0 ? std::max(start, end - gap * grain) : end;
        if (pass == 0) {
            segment(start, open, 0.0, 0.0);
            if (open < end) segment(open, end, 0.0, 0.0, false, 0.0f, 0.0f);
            continue;
        }
        // The readhead runs from the end of the grain back to its start:
        // delay rises at twice real time, a steady 1x reverse.
        const double from = (pass - 1) * grain;
        const double at = from + 2.0 * (open - start);
        segment(start, open, from, at);
        if (open < end) segment(open, end, at, from + 2.0 * (end - start), false, 0.0f, 0.0f);
    }
    return *this;
}

PhraseBuilder& PhraseBuilder::tapeStop(double t0, double t1, double silentUntil) noexcept
{
    const double length = std::min(t1 - t0, 0.75);
    if (length <= epsilon) return *this;
    t1 = t0 + length;
    // The first half of a smoothstep slows the readhead from 1x to a stop:
    // its full-span depth must be 4/3 of the duration; half of it is reached.
    const double span = length * 4.0 / 3.0;
    segment(t0, t1, 0.0, span, true, 1.0f, 1.0f, 1.0f, 0.0, 0.5);
    if (silentUntil > t1) segment(t1, silentUntil, span * 0.5, span * 0.5, false, 0.0f, 0.0f);
    return *this;
}

PhraseBuilder& PhraseBuilder::stopStart(double t0, double stopEnd, double t1) noexcept
{
    const double stopLength = std::min(stopEnd - t0, 0.5);
    if (stopLength <= epsilon) return *this;
    stopEnd = t0 + stopLength;
    const double upLength = std::min(t1 - stopEnd, stopLength);
    const double stopSpan = stopLength * 4.0 / 3.0;
    segment(t0, stopEnd, 0.0, stopSpan, true, 1.0f, 1.0f, 1.0f, 0.0, 0.5);
    if (upLength <= epsilon) return *this;
    // The second half of a smoothstep brings the readhead back to 1x. It
    // continues from the stopped delay and stays that far behind afterwards.
    const double upSpan = upLength * 4.0 / 3.0;
    const double reached = stopSpan * 0.5;
    const double start = reached - upSpan * 0.5;
    segment(stopEnd, stopEnd + upLength, start, start + upSpan, true, 1.0f, 1.0f, 1.0f, 0.5, 1.0);
    return *this;
}

PhraseBuilder& PhraseBuilder::halfTime(double t0, double t1) noexcept
{
    segment(t0, t1, 0.0, 0.5 * (t1 - t0));
    return *this;
}

PhraseBuilder& PhraseBuilder::speed(double t0, double t1, double rate) noexcept
{
    rate = std::clamp(rate, -2.0, 2.0);
    const double length = t1 - t0;
    if (length <= epsilon) return *this;
    // delay' = 1 - rate. Faster than live must begin behind and catch up.
    const double change = (1.0 - rate) * length;
    if (change >= 0.0) segment(t0, t1, 0.0, change);
    else segment(t0, t1, -change, 0.0);
    return *this;
}

PhraseBuilder& PhraseBuilder::baby(double t0, double t1, double depth, bool cut) noexcept
{
    const double length = t1 - t0;
    if (length <= epsilon) return *this;
    const double rise = length * 0.25, fall = length - rise;
    // Smoothstep's peak slope is 1.5x its average; keep within -2x..+2x.
    depth = std::clamp(depth, 0.0, std::min(2.0 * rise, fall / 1.5) * 0.97);
    segment(t0, t0 + rise, 0.0, depth, true);
    segment(t0 + rise, t1, depth, 0.0, true, cut ? 0.0f : 1.0f, cut ? 0.0f : 1.0f);
    return *this;
}

PhraseBuilder& PhraseBuilder::reverse(double t0, double t1, double depth) noexcept
{
    const double length = t1 - t0;
    if (length <= epsilon) return *this;
    depth = std::clamp(depth, 0.0, std::min(2.0 * length * 0.97, 1.0));
    segment(t0, t1, 0.0, depth, true);
    return *this;
}

PhraseBuilder& PhraseBuilder::gate(double t0, double t1, double grain, double duty) noexcept
{
    if (!(grain > epsilon)) return *this;
    duty = std::clamp(duty, 0.05, 1.0);
    for (double start = t0; start < t1 - epsilon; start += grain) {
        if (pattern.count >= ScratchPattern::maximumSegments - 3) break;
        const double end = std::min(start + grain, t1);
        const double open = std::min(end, start + duty * grain);
        segment(start, open, 0.0, 0.0);
        if (open < end) segment(open, end, 0.0, 0.0, false, 0.0f, 0.0f);
    }
    return *this;
}

PhraseBuilder& PhraseBuilder::transformer(double t0, double t1, double depth, double grain) noexcept
{
    const double length = t1 - t0;
    if (length <= epsilon || !(grain > epsilon)) return *this;
    const double rise = length * 0.4, fall = length - rise;
    depth = std::clamp(depth, 0.0, std::min(2.0 * rise, fall / 1.5) * 0.97);
    bool open = true;
    // Chop each half of one rounded stroke without restarting its curve.
    for (const auto [from, to, d0, d1] : {std::array<double, 4> {t0, t0 + rise, 0.0, depth},
                                          std::array<double, 4> {t0 + rise, t1, depth, 0.0}}) {
        for (double start = from; start < to - epsilon; start += grain) {
            if (pattern.count >= ScratchPattern::maximumSegments - 3) break;
            const double end = std::min(start + grain, to);
            const float gain = open ? 1.0f : 0.0f;
            segment(start, end, d0, d1, true, gain, gain, 1.0f, (start - from) / (to - from), (end - from) / (to - from));
            open = !open;
        }
    }
    return *this;
}

PhraseBuilder& PhraseBuilder::echo(double t0, double t1, double back, double gap) noexcept
{
    back = std::clamp(back, 0.0, 1.0);
    if (gap <= 0.0) { segment(t0, t1, back, back); return *this; }
    const double open = std::max(t0, t1 - gap * (t1 - t0));
    segment(t0, open, back, back);
    segment(open, t1, back, back, false, 0.0f, 0.0f);
    return *this;
}

ScratchPattern PhraseBuilder::finish() noexcept
{
    dryTo(1.0);
    double visible = pattern.beats * 0.25;
    for (int index = 0; index < pattern.count; ++index) {
        const auto& part = pattern.segments[static_cast<std::size_t>(index)];
        visible = std::max({visible, evaluatePatternSegment(pattern, index, part.start).delayBeats,
                            evaluatePatternSegment(pattern, index, part.end).delayBeats});
    }
    pattern.editingRange = std::min(visible, pattern.beats);
    return pattern;
}

} // namespace backScratch
