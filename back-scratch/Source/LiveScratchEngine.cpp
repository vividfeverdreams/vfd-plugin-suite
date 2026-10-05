#include "LiveScratchEngine.h"
#include <algorithm>
#include <cmath>

namespace backScratch {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr int radius = LiveScratchEngine::interpolationGuardSamples;
constexpr int taps = radius * 2;
constexpr int phaseSteps = 256;
constexpr int phaseRows = phaseSteps + 1;
constexpr int rateSteps = 49;

// The table is built from prepare(), never from the audio callback. Rate and
// phase interpolation avoid zippering when a curved scratch changes speed.
struct KernelTable {
    std::vector<float> values;
    KernelTable() : values(static_cast<std::size_t>(rateSteps * phaseRows * taps)) {
        for (int speedRow = 0; speedRow < rateSteps; ++speedRow) {
            const double speed = 1.0 + static_cast<double>(speedRow) / (rateSteps - 1);
            const double cutoff = 1.0 / speed;
            for (int phase = 0; phase < phaseRows; ++phase) {
                const double fraction = static_cast<double>(phase) / phaseSteps;
                auto* row = values.data() + (speedRow * phaseRows + phase) * taps;
                double total = 0;
                for (int tap = 0; tap < taps; ++tap) {
                    const double distance = fraction - (tap - radius + 1);
                    const double x = pi * distance * cutoff;
                    const double sinc = std::abs(x) < 1.0e-12 ? 1.0 : std::sin(x) / x;
                    const double window = std::max(0.0, 0.42 + 0.5 * std::cos(pi * distance / radius)
                                                        + 0.08 * std::cos(2.0 * pi * distance / radius));
                    row[tap] = static_cast<float>(cutoff * sinc * window);
                    total += row[tap];
                }
                for (int tap = 0; tap < taps; ++tap)
                    row[tap] = static_cast<float>(row[tap] / total);
            }
        }
    }
};
const KernelTable& kernels() { static const KernelTable table; return table; }

double finiteOr(double value, double fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

double smoothStep(double x) noexcept { return x * x * (3.0 - 2.0 * x); }
}

void LiveScratchEngine::LinearPhaseFilter::prepare(int halfLength, double normalizedCutoff)
{
    length = 2 * halfLength + 1;
    coefficients.resize(static_cast<std::size_t>(halfLength + 1));
    samples.assign(static_cast<std::size_t>(length * 2), 0.0);
    double total = 0;
    for (int offset = 0; offset <= halfLength; ++offset) {
        const double x = 2.0 * pi * normalizedCutoff * offset;
        const double sinc = offset == 0 ? 1.0 : std::sin(x) / x;
        const double window = 0.42 + 0.5 * std::cos(pi * offset / halfLength)
                                  + 0.08 * std::cos(2.0 * pi * offset / halfLength);
        const double coefficient = 2.0 * normalizedCutoff * sinc * window;
        coefficients[static_cast<std::size_t>(offset)] = coefficient;
        total += coefficient * (offset == 0 ? 1.0 : 2.0);
    }
    for (auto& coefficient : coefficients) coefficient /= total;
    reset();
}

void LiveScratchEngine::LinearPhaseFilter::reset() noexcept
{
    std::fill(samples.begin(), samples.end(), 0.0);
    cursor = 0;
}

double LiveScratchEngine::LinearPhaseFilter::process(double input) noexcept
{
    samples[static_cast<std::size_t>(cursor)] = input;
    samples[static_cast<std::size_t>(cursor + length)] = input;
    const double* centre = samples.data() + cursor + 1 + length / 2;
    double output = coefficients[0] * centre[0];
    for (int offset = 1; offset <= length / 2; ++offset)
        output += coefficients[static_cast<std::size_t>(offset)] * (centre[-offset] + centre[offset]);
    if (++cursor == length) cursor = 0;
    return output;
}

void LiveScratchEngine::BassExtractor::prepare(double sampleRate)
{
    // Work near 12 kHz so a narrow, phase-linear bass split does not require
    // thousands of multiplies at the host rate. Anti-alias and reconstruction
    // filters run at each 2:1 boundary; their delays are included in host PDC.
    stageCount = 0;
    double baseRate = sampleRate;
    int stride = 1;
    delay = 0;
    while (baseRate > 12000.0 && stageCount < maximumStages) {
        auto& stage = stages[static_cast<std::size_t>(stageCount++)];
        constexpr int halfBandRadius = 31;
        stage.down.prepare(halfBandRadius, 0.25);
        stage.up.prepare(halfBandRadius, 0.25);
        delay += 2 * halfBandRadius * stride;
        stride *= 2;
        baseRate *= 0.5;
    }
    const int baseRadius = std::max(16, static_cast<int>(std::lround(baseRate * 128.0 / 12000.0)));
    base.prepare(baseRadius, crossoverHz / baseRate);
    delay += baseRadius * stride;
    reset();
}

void LiveScratchEngine::BassExtractor::reset() noexcept
{
    for (int index = 0; index < stageCount; ++index) {
        auto& stage = stages[static_cast<std::size_t>(index)];
        stage.down.reset();
        stage.up.reset();
        stage.even = true;
    }
    base.reset();
}

double LiveScratchEngine::BassExtractor::process(double input, int level) noexcept
{
    if (level == stageCount) return base.process(input);
    auto& stage = stages[static_cast<std::size_t>(level)];
    const double down = stage.down.process(input);
    const double inner = stage.even ? process(down, level + 1) : 0.0;
    stage.even = !stage.even;
    return stage.up.process(inner * 2.0);
}

void LiveScratchEngine::prepare(double sampleRate, int channels, double historySeconds)
{
    (void) kernels();
    rate = std::clamp(finiteOr(sampleRate, 48000.0), 8000.0, 384000.0);
    busChannels = std::clamp(channels, 1, 2);
    for (auto& channel : bass) channel.prepare(rate);
    crossoverDelay = bass[0].delay;
    const double duration = std::clamp(finiteOr(historySeconds, defaultHistorySeconds),
                                       1.0, static_cast<double>(maximumHistorySeconds));
    // Include all filter/interpolator latency in addition to the usable
    // lookback, rather than quietly shortening the requested musical span.
    capacity = static_cast<int>(std::ceil(rate * duration)) + latencySamples() + taps * 2;
    for (auto& channel : history) channel.assign(static_cast<std::size_t>(capacity), 0.0f);
    fadeStep = 1.0 / std::max(1.0, rate * 0.003);
    controlCoefficient = 1.0 - std::exp(-1.0 / (rate * 0.001));
    reset();
}

void LiveScratchEngine::reset() noexcept
{
    writeFrame = 0;
    for (auto& channel : bass) channel.reset();
    wetAmount = 0;
    gateAmount = motionAmount = subAmount = 1;
    previousTarget = outgoingFrame = 0;
    previousRate = outgoingRate = 1;
    jumpFade = 1;
    lastRead = frozenOutgoing = {};
    haveTarget = freezeOutgoing = false;
    firstSample = true;
    historyReady = scratchActive = false;
}

bool LiveScratchEngine::readable(double frame) const noexcept
{
    if (!std::isfinite(frame)) return false;
    const auto centre = static_cast<std::int64_t>(std::floor(frame));
    const auto oldest = std::max<std::int64_t>(0, writeFrame - capacity + 1);
    return centre - radius + 1 >= oldest && centre + radius <= writeFrame - crossoverDelay;
}

std::array<double, 4> LiveScratchEngine::read(double frame, double speed) const noexcept
{
    std::array<double, 4> output {};
    if (!readable(frame)) return output;
    const double speedRow = (std::clamp(std::abs(speed), 1.0, 2.0) - 1.0) * (rateSteps - 1);
    const int lowRate = std::min(static_cast<int>(speedRow), rateSteps - 2);
    const double speedFraction = speedRow - lowRate;
    const auto centre = static_cast<std::int64_t>(std::floor(frame));
    const double phaseRow = (frame - std::floor(frame)) * phaseSteps;
    const int lowPhase = std::clamp(static_cast<int>(phaseRow), 0, phaseSteps - 1);
    const double phaseFraction = phaseRow - lowPhase;
    const auto& table = kernels().values;
    const float* first = table.data() + (lowRate * phaseRows + lowPhase) * taps;
    const float* next = first + phaseRows * taps;
    int index = static_cast<int>((centre - radius + 1) % capacity);
    for (int tap = 0; tap < taps; ++tap) {
        const double a = first[tap] + phaseFraction * (first[tap + taps] - first[tap]);
        const double b = next[tap] + phaseFraction * (next[tap + taps] - next[tap]);
        const double weight = a + speedFraction * (b - a);
        for (std::size_t channel = 0; channel < output.size(); ++channel)
            output[channel] += history[channel][static_cast<std::size_t>(index)] * weight;
        if (++index == capacity) index = 0;
    }
    return output;
}

std::array<float, 2> LiveScratchEngine::process(float left, float right, const PatternSample& sample,
                                             double samplesPerBeat, bool keepSub, bool bypass) noexcept
{
    const std::array<double, 2> input {finiteOr(left, 0.0), finiteOr(busChannels == 1 ? left : right, 0.0)};
    if (capacity == 0) return {static_cast<float>(input[0]), static_cast<float>(input[1])};
    const int writeIndex = static_cast<int>(writeFrame % capacity);
    const std::int64_t splitFrame = writeFrame - crossoverDelay;
    for (int channel = 0; channel < 2; ++channel) {
        const auto c = static_cast<std::size_t>(channel);
        history[c][static_cast<std::size_t>(writeIndex)] = static_cast<float>(input[c]);
        const double lower = bass[c].process(input[c]);
        if (splitFrame >= 0) {
            const auto splitIndex = static_cast<std::size_t>(splitFrame % capacity);
            history[c + 2][splitIndex] = static_cast<float>(history[c][splitIndex] - lower);
        }
    }
    std::array<double, 4> dry {};
    const std::int64_t dryFrame = writeFrame - latencySamples();
    if (dryFrame >= 0) {
        const auto dryIndex = static_cast<std::size_t>(dryFrame % capacity);
        for (std::size_t channel = 0; channel < dry.size(); ++channel)
            dry[channel] = history[channel][dryIndex];
    }
    if (firstSample) {
        subAmount = keepSub ? 1.0 : 0.0;
        firstSample = false;
    }
    const double targetSub = keepSub ? 1.0 : 0.0;
    subAmount += std::clamp(targetSub - subAmount, -fadeStep, fadeStep);
    const double speed = std::clamp(finiteOr(sample.playbackRate, 1.0), -2.0, 2.0);
    // Keep the requested trajectory intact. Unavailable old audio follows the
    // same smooth live-input fallback as startup; clamping delay would flatten
    // an extended reverse stroke and change the sound/pitch of the pattern.
    const double delay = std::max(0.0, finiteOr(sample.delayBeats, 0.0))
        * std::clamp(finiteOr(samplesPerBeat, rate * 0.5), 1.0, rate * 60.0);
    const double target = static_cast<double>(writeFrame) - latencySamples() - delay;
    const bool hasHistory = readable(target);
    historyReady = hasHistory;
    scratchActive = !bypass && finiteOr(sample.wet, 0.0) > 0.0;
    const bool jumped = haveTarget && std::abs(target - previousTarget - previousRate) > 8.0;
    if (jumped) {
        // A second jump during a fade starts from the currently audible sample,
        // rather than snapping back to either of the earlier readheads.
        freezeOutgoing = jumpFade < 1.0;
        frozenOutgoing = lastRead;
        outgoingFrame = previousTarget + previousRate;
        outgoingRate = previousRate;
        jumpFade = 0;
    }
    const double requestedWet = bypass || !hasHistory ? 0.0 : std::clamp(finiteOr(sample.wet, 0.0), 0.0, 1.0);
    const bool needsScratch = requestedWet > 0.0 || wetAmount > 0.0;
    // Dry portions need no interpolation work. If lookback falls outside valid
    // history, retain the last wet value while the blend returns to live input.
    auto wet = hasHistory && needsScratch ? read(target, speed)
        : wetAmount > 0.0 ? lastRead : dry;
    if (jumpFade < 1.0 && needsScratch) {
        auto outgoing = freezeOutgoing || !readable(outgoingFrame) ? frozenOutgoing : read(outgoingFrame, outgoingRate);
        const double fade = smoothStep(jumpFade);
        for (int channel = 0; channel < 4; ++channel)
            wet[static_cast<std::size_t>(channel)] = outgoing[static_cast<std::size_t>(channel)]
                + fade * (wet[static_cast<std::size_t>(channel)] - outgoing[static_cast<std::size_t>(channel)]);
        outgoingFrame += outgoingRate;
        jumpFade = std::min(1.0, jumpFade + fadeStep);
    }
    if (!needsScratch) jumpFade = 1.0;
    lastRead = wet;
    previousTarget = target;
    previousRate = speed;
    haveTarget = true;
    wetAmount += std::clamp(requestedWet - wetAmount, -fadeStep, fadeStep);
    gateAmount += controlCoefficient * (std::clamp(finiteOr(sample.gate, 1.0), 0.0, 1.0) - gateAmount);
    const double targetMotion = std::clamp(std::abs(speed) / 0.025, 0.0, 1.0);
    motionAmount += controlCoefficient * (targetMotion - motionAmount);
    std::array<float, 2> output {};
    for (int channel = 0; channel < 2; ++channel) {
        const auto c = static_cast<std::size_t>(channel);
        const double scratchGain = gateAmount * motionAmount;
        const double fullScratch = wet[c] * scratchGain;
        const double protectedScratch = dry[c] - dry[c + 2] + wet[c + 2] * scratchGain;
        const double scratch = fullScratch + subAmount * (protectedScratch - fullScratch);
        // At zero delay/rate 1 the full and protected paths both equal dry.
        // Linear crossfades then stay neutral even for correlated material.
        const double result = dry[c] + wetAmount * (scratch - dry[c]);
        output[c] = static_cast<float>(bypass ? dry[c] : finiteOr(result, 0.0));
    }
    ++writeFrame;
    return output;
}

} // namespace backScratch
