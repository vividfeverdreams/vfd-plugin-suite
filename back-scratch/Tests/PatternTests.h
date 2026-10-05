#pragma once

#include "ScratchPattern.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace backScratch {
namespace patternTests {

inline void check(bool passed, const char* message)
{
    if (!passed) throw std::runtime_error(message);
}

inline std::uint64_t fingerprint(const ScratchPattern& pattern)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (int i = 0; i < 4096; ++i) {
        const auto value = evaluatePattern(pattern, pattern.beats * (i + 0.31) / 4096.0);
        for (const double field : {value.delayBeats, value.playbackRate,
                                    static_cast<double>(value.gate), static_cast<double>(value.wet)}) {
            const auto quantized = static_cast<std::int64_t>(std::llround(field * 1000000.0));
            hash ^= static_cast<std::uint64_t>(quantized);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

inline std::uint64_t structuralFingerprint(const ScratchPattern& pattern)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        hash ^= bits;
        hash *= 1099511628211ull;
    };
    mix(pattern.beats);
    mix(pattern.count);
    for (int i = 0; i < pattern.count; ++i) {
        const auto& part = pattern.segments[static_cast<std::size_t>(i)];
        for (const double value : {part.start, part.end, part.delayStart, part.delayEnd,
                                  static_cast<double>(part.gateStart), static_cast<double>(part.gateEnd),
                                  static_cast<double>(part.wet), part.curved ? 1.0 : 0.0}) mix(value);
    }
    return hash;
}

inline void checkPattern(const ScratchPattern& pattern, int sampleCount = 8192, double minimumWet = 0.12, double maximumDelay = 3.0)
{
    check(pattern.count > 0 && pattern.count <= ScratchPattern::maximumSegments,
          "scratch patterns fit their allocation-free fixed segment bank");
    check(std::isfinite(pattern.beats) && pattern.beats >= 1.0 / 6.0 && pattern.beats <= 384.0,
          "scratch phrases support host meter within the bounded phrase range");
    double end = 0.0;
    double wetBeats = 0.0;
    bool hasReverse = false;
    for (int i = 0; i < pattern.count; ++i) {
        const auto& part = pattern.segments[static_cast<std::size_t>(i)];
        check(std::abs(part.start - end) < 1.0e-10 && part.end > part.start,
              "scratch pattern segments cover the phrase without gaps or overlap");
        end = part.end;
        wetBeats += (part.end - part.start) * part.wet;
        check(std::isfinite(part.start) && std::isfinite(part.end)
                  && std::isfinite(part.delayStart) && std::isfinite(part.delayEnd),
              "every segment has finite timing and source positions");
        const double domainPeak = std::clamp(0.5, part.curveStart, part.curveEnd);
        const double peakDerivative = (part.curved
            ? 6.0 * domainPeak * (1.0 - domainPeak) * (part.curveEnd - part.curveStart) : 1.0)
            * (part.delayEnd - part.delayStart) / (part.end - part.start);
        check(1.0 - peakDerivative >= -2.0 - 1.0e-9
                  && 1.0 - peakDerivative <= 2.0 + 1.0e-9,
              "analytic rate extrema are bounded even between sample probes");
        const double actualStart = evaluatePatternSegment(pattern, i, part.start).delayBeats;
        const double actualEnd = evaluatePatternSegment(pattern, i, part.end).delayBeats;
        check(std::min(actualStart, actualEnd) >= 0.0
                  && std::max(actualStart, actualEnd) <= maximumDelay + 1.0e-9,
              "entire monotonic segment delay remains inside its scaled history budget");
        hasReverse = hasReverse || (1.0 - peakDerivative < -0.1 && part.wet > 0.5f
                                    && (part.gateStart > 0.5f || part.gateEnd > 0.5f));
    }
    check(std::abs(end - pattern.beats) < 1.0e-10, "scratch pattern ends on its loop boundary");
    check(evaluatePattern(pattern, 0.0).wet == 0.0f
              && evaluatePattern(pattern, std::min(0.125, pattern.segments[0].end * 0.5)).wet == 0.0f,
          "every factory pattern opens dry so incoming audio can build history");
    check(wetBeats >= pattern.beats * minimumWet && wetBeats <= pattern.beats * 0.85,
          "factory patterns make audible changes while preserving some source rhythm");

    for (int i = 0; i < sampleCount; ++i) {
        const double phase = pattern.beats * (i + 0.37) / sampleCount;
        const auto value = evaluatePattern(pattern, phase);
        check(std::isfinite(value.delayBeats) && std::isfinite(value.playbackRate)
                  && std::isfinite(value.gate) && std::isfinite(value.wet),
              "scratch curves contain only finite values");
        check(value.delayBeats >= -1.0e-10 && value.delayBeats <= maximumDelay + 1.0e-9,
              "scratch curves read bounded past audio, never future audio");
        check(value.delayBeats <= phase + 1.0e-9,
              "the dry opening builds enough history even on the first pattern cycle");
        check(value.playbackRate >= -2.0 - 1.0e-9 && value.playbackRate <= 2.0 + 1.0e-9,
              "scratch playback speed stays between minus and plus two times");
        check(value.gate >= 0.0f && value.gate <= 1.0f && value.wet >= 0.0f && value.wet <= 1.0f,
              "scratch gates and wet amounts stay within gain bounds");
        hasReverse = hasReverse || (value.playbackRate < -0.1 && value.gate > 0.5f && value.wet > 0.5f);

        // Read position is t-delay(t). Analytic resampler rates must describe
        // that trajectory exactly, not merely report a separately clamped rate.
        const auto& part = pattern.segments[static_cast<std::size_t>(value.segment)];
        const double epsilon = std::min(1.0e-5 * std::max(1.0, pattern.beats), (part.end - part.start) * 1.0e-4);
        if (phase > part.start + epsilon && phase < part.end - epsilon) {
            const auto before = evaluatePattern(pattern, phase - epsilon);
            const auto after = evaluatePattern(pattern, phase + epsilon);
            const double numericalRate = 1.0 - (after.delayBeats - before.delayBeats) / (2.0 * epsilon);
            check(std::abs(numericalRate - value.playbackRate) < 1.0e-6,
                  "resampler rate agrees with the actual time-remapping derivative");
        }
        const auto looped = evaluatePattern(pattern, phase + pattern.beats * 13.0);
        const auto negative = evaluatePattern(pattern, phase - pattern.beats * 7.0);
        check(std::abs(looped.delayBeats - value.delayBeats) < 1.0e-10
                  && std::abs(negative.delayBeats - value.delayBeats) < 1.0e-10
                  && looped.wet == value.wet && std::abs(negative.gate - value.gate) < 1.0e-6f,
              "positive and negative song positions repeat the same rhythmic phrase");
    }
    check(hasReverse, "each scratch recipe includes an audible backward stroke");
}

// Duration must retime the existing phrase, including long and sub-beat
// choices. This checks geometry and audible evaluation, not just its endpoint.
inline void checkRetime(const ScratchPattern& source, double beats)
{
    const auto fitted = fitPatternToPhrase(source, beats);
    const double scale = beats / source.beats;
    check(fitted.beats == beats && fitted.count == source.count
              && fitted.gridOffset == wrapPatternGridOffset(source.gridOffset),
          "retiming preserves the exact number of strokes and dry regions");
    for (int index = 0; index < source.count; ++index) {
        const auto& before = source.segments[static_cast<std::size_t>(index)];
        const auto& after = fitted.segments[static_cast<std::size_t>(index)];
        check(std::abs(after.start - before.start * scale) < 1.0e-10
                  && std::abs(after.end - before.end * scale) < 1.0e-10
                  && after.delayStart == before.delayStart * scale
                  && after.delayEnd == before.delayEnd * scale
                  && after.gateStart == before.gateStart && after.gateEnd == before.gateEnd
                  && after.wet == before.wet && after.curved == before.curved
                  && after.curveStart == before.curveStart && after.curveEnd == before.curveEnd,
              "duration scales each original segment once without repeating, trimming, or recomposing");
        for (const double fraction : {0.01, 0.13, 0.37, 0.5, 0.81, 0.99}) {
            const double beat = before.start + fraction * (before.end - before.start);
            const auto a = evaluatePattern(source, beat);
            const auto b = evaluatePattern(fitted, beat * scale);
            check(std::abs(b.delayBeats - a.delayBeats * scale) < 1.0e-9
                      && std::abs(b.playbackRate - a.playbackRate) < 1.0e-9
                      && std::abs(b.gate - a.gate) < 1.0e-6 && b.wet == a.wet
                      && b.segment == a.segment,
                  "retimed audio keeps the same source speed, gate, and ordered gesture at every phase");
        }
    }
    checkPattern(fitted, 256, 0.12, 3.0 * scale);
}

inline void checkSameSample(const PatternSample& a, const PatternSample& b, const char* message)
{
    check(std::abs(a.delayBeats - b.delayBeats) < 1.0e-8
              && std::abs(a.playbackRate - b.playbackRate) < 1.0e-8
              && std::abs(a.gate - b.gate) < 1.0e-6f && a.wet == b.wet
              && a.segment == b.segment, message);
}

inline void checkGridShift(const ScratchPattern& source)
{
    check(source.gridOffset == 0, "new factory and generated patterns have no hidden phase offset");
    const double step = source.beats / 16.0;
    check(patternGridStepBeats(source) == step,
          "one grid shift always equals one of the sixteen visible divisions");
    for (const int offset : {-33, -17, -16, -1, 0, 1, 7, 15, 16, 17, 33}) {
        auto shifted = source;
        shifted.gridOffset = offset;
        check(structuralFingerprint(shifted) == structuralFingerprint(source),
              "shifting leaves every original curve, gate, and segment boundary untouched");
        const int normalized = (offset % 16 + 16) % 16;
        check(wrapPatternGridOffset(offset) == normalized
                  && patternOffsetBeats(shifted) == normalized * step,
              "left and right shifts wrap exactly at the visible phrase edges");
        for (const int cycle : {-13, -1, 0, 1, 9}) {
            for (int probe = 0; probe < 257; ++probe) {
                const double beat = source.beats * (cycle + (probe + 0.371) / 257.0);
                const auto heard = evaluatePattern(shifted, beat);
                const auto expected = evaluatePattern(source, beat - normalized * step);
                checkSameSample(heard, expected,
                                "a shift moves the whole time effect with identical rates, gates, and wet regions");
                const double originalPhase = patternPhaseBeats(shifted, beat);
                check(originalPhase >= 0.0 && originalPhase < source.beats,
                      "shared drawing and playback coordinates remain inside the original phrase");
                if (normalized == 0) {
                    const auto original = evaluatePattern(source, beat);
                    check(heard.delayBeats == original.delayBeats
                              && heard.playbackRate == original.playbackRate
                              && heard.gate == original.gate && heard.wet == original.wet,
                          "sixteen clicks restore bit-identical playback, including negative transport positions");
                }
            }
        }
        for (const double duration : {1.0 / 6.0, 1.0 / 3.0, 0.75, 6.0, 96.0, 384.0}) {
            const auto retimed = fitPatternToPhrase(shifted, duration);
            check(retimed.gridOffset == normalized && retimed.beats == duration,
                  "dotted and triplet duration changes retain the chosen grid offset at extreme meters");
            for (int probe = 0; probe < 67; ++probe) {
                const double fraction = (probe + 0.317) / 67.0;
                auto expected = evaluatePattern(shifted, fraction * source.beats);
                expected.delayBeats *= duration / source.beats;
                checkSameSample(evaluatePattern(retimed, fraction * duration), expected,
                                "a shifted gesture retimes once without altering its pitch trajectory or gate");
            }
        }
    }
}

inline void checkWrappedCurve()
{
    ScratchPattern source;
    source.count = 1;
    source.segments[0] = {0.0, 4.0, 0.0, 2.0, 0.15f, 0.9f, 1.0f, true};
    auto shifted = source;
    shifted.gridOffset = 7;
    // The middle of this curved ramp crosses the display edge. Splitting its
    // geometry into two newly normalized smoothsteps would change the sound.
    const double epsilon = 1.0e-5;
    const auto before = evaluatePattern(shifted, -epsilon);
    const auto seam = evaluatePattern(shifted, 0.0);
    const auto after = evaluatePattern(shifted, epsilon);
    checkSameSample(seam, evaluatePattern(source, 2.25),
                    "the display seam samples the original curve rather than restarting a partial smoothstep");
    check(std::abs(1.0 - (after.delayBeats - before.delayBeats) / (2.0 * epsilon)
                         - seam.playbackRate) < 1.0e-8
              && std::abs(after.gate - before.gate) < 1.0e-5,
          "a wrapped curve has continuous source speed and gate through the phrase boundary");
    checkSameSample(evaluatePattern(shifted, 1.75), evaluatePattern(source, 0.0),
                    "the original retrigger moves exactly to its shifted grid location");
    ScratchPattern invalid;
    invalid.beats = std::numeric_limits<double>::quiet_NaN();
    check(patternGridStepBeats(invalid) == 0.0 && patternOffsetBeats(invalid) == 0.0
              && patternPhaseBeats(invalid, 1.0) == 0.0
              && patternPhaseBeats(source, std::numeric_limits<double>::infinity()) == 0.0,
          "invalid display coordinates fail safely without creating non-finite geometry");
}

inline double adjacentBeat(double beat, bool later, int ulps)
{
    const double direction = later ? std::numeric_limits<double>::infinity()
                                   : -std::numeric_limits<double>::infinity();
    while (ulps-- > 0) beat = std::nextafter(beat, direction);
    return beat;
}

inline void checkCanonicalBoundaries()
{
    // This exact pair came from absolute host PPQ versus block PPQ plus sample
    // offset in a 257-sample callback. Previously the values chose two sides
    // of a gate and caused an audible 0.00837569 output discrepancy.
    auto regression = fitPatternToPhrase(makePattern(8), 1.0 / 3.0);
    regression.gridOffset = 5;
    const auto a = evaluatePattern(regression, 2.7604166666666665);
    const auto b = evaluatePattern(regression, 2.760416666666667);
    check(a.segment == 21 && a.segment == b.segment && a.delayBeats == b.delayBeats
              && a.playbackRate == b.playbackRate && a.gate == b.gate && a.wet == b.wet,
          "absolute and callback-relative PPQ select the same exact right-hand gate at a numeric knot");
    for (int recipe = 0; recipe < factoryPresetCount + 5; ++recipe) {
        const auto original = recipe < factoryPresetCount ? makePattern(recipe)
            : generatePattern(static_cast<std::uint32_t>(recipe * 997), 16.0);
        for (const double duration : {1.0 / 6.0, 1.0 / 3.0, 0.75, 6.0, 96.0, 384.0}) {
            auto pattern = fitPatternToPhrase(original, duration);
            for (const int offset : {0, 1, 7, 15}) {
                pattern.gridOffset = offset;
                for (int segment = 0; segment < pattern.count; ++segment) {
                    const auto& part = pattern.segments[static_cast<size_t>(segment)];
                    const auto expected = evaluatePatternSegment(pattern, segment, part.start);
                    for (const int cycle : {-3, 0, 5}) {
                        const double knot = part.start + patternOffsetBeats(pattern) + cycle * duration;
                        for (const int ulps : {0, 1, 4}) {
                            for (const bool later : {false, true}) {
                                const auto actual = evaluatePattern(pattern, adjacentBeat(knot, later, ulps));
                                check(actual.segment == expected.segment
                                          && actual.delayBeats == expected.delayBeats
                                          && actual.playbackRate == expected.playbackRate
                                          && actual.gate == expected.gate && actual.wet == expected.wet,
                                      "shifted and retimed knots are right-continuous for PPQ values separated by a few ulps");
                            }
                        }
                    }
                    const auto endpoint = evaluatePatternSegment(pattern, segment, part.end);
                    const double endShape = part.curved
                        ? part.curveEnd * part.curveEnd * (3.0 - 2.0 * part.curveEnd) : 1.0;
                    check(endpoint.segment == segment
                              && std::abs(endpoint.delayBeats - (part.delayStart
                                  + (part.delayEnd - part.delayStart) * endShape)) < 1e-12
                              && std::abs(endpoint.gate - part.gateEnd) < 1e-6f
                              && endpoint.wet == part.wet,
                          "explicit graph endpoints retain the left segment's exact curve and gate at a retrigger");
                    const double inside = part.start + (part.end - part.start) * 0.371;
                    checkSameSample(evaluatePatternSegment(pattern, segment, inside),
                                    evaluatePattern(pattern, inside + patternOffsetBeats(pattern)),
                                    "shared segment math keeps graph and playback identical away from microscopic boundaries");
                }
                for (const int cycle : {-5, -1, 0, 1, 7}) {
                    const double boundary = cycle * duration;
                    for (const bool later : {false, true})
                        check(patternDisplayPhase(pattern, adjacentBeat(boundary, later, 4)) == 0.0,
                              "display clock canonicalizes phrase wraps independently of the pattern shift");
                }
                check(std::abs(patternDisplayPhase(pattern, duration * 0.371) - 0.371) < 1e-12,
                      "display phase remains unchanged away from the numeric phrase boundary");
            }
        }
    }
    ScratchPattern curve;
    curve.count = 2;
    curve.segments[0] = {0.0, 1.0, 0.0, 0.5, 1.0f, 0.75f, 1.0f, true};
    curve.segments[1] = {1.0, 4.0, 0.0, 0.0, 0.0f, 0.0f, 0.0f, false};
    const auto drawnEnd = evaluatePatternSegment(curve, 0, 1.0);
    const auto heardStart = evaluatePattern(curve, 1.0);
    check(drawnEnd.delayBeats == 0.5 && drawnEnd.gate == 0.75f && drawnEnd.playbackRate == 1.0
              && heardStart.segment == 1 && heardStart.delayBeats == 0.0 && heardStart.wet == 0.0f,
          "one-sided drawing and right-continuous playback preserve both sides of a discontinuous curve");
    check(evaluatePattern(curve, 1.0 - 1e-9).segment == 0
              && evaluatePattern(curve, 1.0 + 1e-9).segment == 1,
          "boundary tolerance never moves gestures by a meaningful fraction of an audio sample");
}

} // namespace patternTests

inline void runPatternTests()
{
    using namespace patternTests;
    const auto& bank = factoryPresets();
    check(bank.size() == 75, "exactly seventy-five curated factory presets are available");
    std::array<std::uint64_t, factoryPresetCount> fingerprints {};
    int bass = 0, drums = 0, vocals = 0;
    for (int index = 0; index < factoryPresetCount; ++index) {
        const auto& preset = bank[static_cast<std::size_t>(index)];
        check(preset.name != nullptr && preset.name[0] != '\0'
                  && preset.description != nullptr && std::strlen(preset.description) > 25,
              "every factory recipe has a useful name and musical description");
        bass += std::strcmp(preset.category, "BASS") == 0;
        drums += std::strcmp(preset.category, "DRUMS") == 0;
        vocals += std::strcmp(preset.category, "VOCALS") == 0;
        const auto original = makePattern(index);
        checkPattern(original);
        const auto hash = fingerprint(original);
        for (int earlier = 0; earlier < index; ++earlier) {
            check(hash != fingerprints[static_cast<std::size_t>(earlier)],
                  "all seventy-five factory presets have distinct audible trajectories");
            check(std::strcmp(preset.name, bank[static_cast<std::size_t>(earlier)].name) != 0,
                  "factory preset names are unique");
        }
        fingerprints[static_cast<std::size_t>(index)] = hash;
        for (const std::uint32_t seed : {1u, 42u, 987654321u, 0xffffffffu}) {
            const auto variation = makePattern(index, seed);
            checkPattern(variation);
            check(fingerprint(variation) == fingerprint(makePattern(index, seed)),
                  "saved random seeds reproduce exactly the same scratch recipe");
            check(fingerprint(variation) != hash,
                  "randomization changes every recipe without erasing its identity");
        }
        check(fingerprint(makePattern(index, 42u)) != fingerprint(makePattern(index, 43u)),
              "different random seeds make different repeatable variations");
    }
    check(bass == 25 && drums == 25 && vocals == 25, "the factory bank covers bass, drums, and vocals");
    check(fingerprint(makePattern(-1)) == fingerprint(makePattern(0))
              && fingerprint(makePattern(factoryPresetCount)) == fingerprint(makePattern(factoryPresetCount - 1)),
          "out-of-range preset indices safely clamp to the factory bank");
    const ScratchPattern empty;
    check(evaluatePattern(empty, 1.0).wet == 0.0f
              && evaluatePattern(makePattern(0), std::numeric_limits<double>::quiet_NaN()).wet == 0.0f,
          "empty patterns and invalid host positions fail safely to dry");

    constexpr std::array<double, 5> lengths {4.0, 8.0, 16.0, 32.0, 64.0};
    std::array<std::uint64_t, 5000> generatedHashes {};
    std::size_t generatedCount = 0;
    int mostSegments = 0;
    for (const double beats : lengths) {
        std::array<std::uint64_t, factoryPresetCount> fittedFactoryHashes {};
        for (int index = 0; index < factoryPresetCount; ++index) {
            const auto fitted = fitPatternToPhrase(makePattern(index), beats);
            checkRetime(makePattern(index), beats);
            fittedFactoryHashes[static_cast<std::size_t>(index)] = structuralFingerprint(fitted);
        }
        for (std::uint32_t seed = 0; seed < 1000; ++seed) {
            const auto generated = generatePattern(seed, beats);
            checkPattern(generated, 512);
            check(generated.beats == beats && evaluatePattern(generated, beats - 0.0625).wet == 0.0f,
                  "new compositions fill the selected phrase and return to dry before the downbeat");
            const auto hash = structuralFingerprint(generated);
            check(hash == structuralFingerprint(generatePattern(seed, beats)),
                  "a saved seed and phrase length reproduce every segment exactly");
            for (const auto factoryHash : fittedFactoryHashes)
                check(hash != factoryHash, "randomize composes new phrases independently of every factory preset");
            generatedHashes[generatedCount++] = hash;
            mostSegments = std::max(mostSegments, generated.count);
            if (beats > 4.0) {
                bool laterBarDiffers = false;
                for (int sample = 0; sample < 128; ++sample) {
                    const double beat = (sample + 0.37) * 4.0 / 128.0;
                    const auto firstBar = evaluatePattern(generated, beat);
                    const auto lastBar = evaluatePattern(generated, beat + beats - 4.0);
                    laterBarDiffers = laterBarDiffers || std::abs(firstBar.delayBeats - lastBar.delayBeats) > 1.0e-6
                                                       || firstBar.wet != lastBar.wet;
                }
                check(laterBarDiffers, "multi-bar generation develops the phrase rather than repeating its first bar");
            }
        }
    }
    std::sort(generatedHashes.begin(), generatedHashes.end());
    check(std::adjacent_find(generatedHashes.begin(), generatedHashes.end()) == generatedHashes.end(),
          "five thousand generated seed/length pairs all produce distinct phrases");
    check(mostSegments < ScratchPattern::maximumSegments - 1,
          "stress-tested compositions fit the fixed bank without truncating later bars");
    for (const double invalid : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::infinity()}) {
        checkPattern(generatePattern(42u, invalid), 512);
        check(fitPatternToPhrase(empty, invalid).segments[0].wet == 0.0f,
              "an unavailable factory trajectory safely produces a complete dry phrase");
    }
    for (const double barBeats : {0.25, 3.0, 3.5, 5.0, 6.0, 16.0}) {
        for (const int bars : {1, 2, 4, 8, 16}) {
            const double beats = barBeats * bars;
            for (std::uint32_t seed = 0; seed < 25; ++seed) {
                const auto generated = generatePattern(seed, beats, barBeats);
                check(generated.beats == beats, "generated phrase follows exact host bar length, including odd meters");
                checkPattern(generated, 512);
                check(structuralFingerprint(generated) == structuralFingerprint(generatePattern(seed, beats, barBeats)),
                      "host-meter generation is exactly repeatable");
            }
            for (int index = 0; index < factoryPresetCount; ++index) {
                const auto fitted = fitPatternToPhrase(makePattern(index), beats);
                check(fitted.beats == beats, "factory pattern length follows host meter exactly");
                checkRetime(makePattern(index), beats);
            }
        }
    }
    // Factory and generated patterns retain their complete identity from a
    // half-beat pulse to sixteen bars in the largest supported host meter.
    constexpr std::array<double, 11> durations {0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 8.0, 16.0, 32.0, 64.0, 256.0};
    for (const double beats : durations) {
        std::array<std::uint64_t, factoryPresetCount> normalized {};
        for (int index = 0; index < factoryPresetCount; ++index) {
            const auto source = makePattern(index);
            checkRetime(source, beats);
            const auto hash = fingerprint(fitPatternToPhrase(source, beats));
            for (int earlier = 0; earlier < index; ++earlier)
                check(hash != normalized[static_cast<std::size_t>(earlier)],
                      "factory recipes remain distinct at every common duration, not just through different native lengths");
            normalized[static_cast<std::size_t>(index)] = hash;
        }
        for (const double originalBeats : {0.5, 4.0, 8.0, 64.0, 256.0}) {
            for (std::uint32_t seed = 0; seed < 100; ++seed)
                checkRetime(generatePattern(seed, originalBeats), beats);
        }
    }
    for (int index = 0; index < factoryPresetCount; ++index) {
        const auto source = makePattern(index);
        checkGridShift(source);
        checkRetime(source, 1.0 / 6.0);
        checkRetime(source, 384.0);
    }
    for (std::uint32_t seed = 0; seed < 25; ++seed)
        checkGridShift(generatePattern(seed, 16.0));
    checkWrappedCurve();
    checkCanonicalBoundaries();
    std::cout << "BACK-SCRATCH 75 factory patterns, 5,000 complete generated phrases, exact duration scaling, and circular grid shifts passed; maximum "
              << mostSegments << " segments\n";
}

} // namespace backScratch
