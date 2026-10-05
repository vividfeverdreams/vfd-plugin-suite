#pragma once
#include "LiveScratchEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

inline void runShortestPhraseEngineTests()
{
    using namespace backScratch;
    constexpr double rate = 24000.0, twoPi = 6.28318530717958647692;
    constexpr double beatFrames = rate * 60.0 / 400.0;
    constexpr int totalFrames = 24000;
    std::vector<float> source(totalFrames);
    for (int frame = 0; frame < totalFrames; ++frame) {
        const double time = frame / rate;
        source[static_cast<std::size_t>(frame)] = static_cast<float>(
            0.26 * std::sin(twoPi * 55 * time) + 0.12 * std::sin(twoPi * 231 * time)
            + 0.07 * std::sin(twoPi * 957 * time) * std::exp(-80.0 * std::fmod(time, 0.125)));
    }
    LiveScratchEngine engine;
    engine.prepare(rate, 2);
    double peak = 0, smallestChange = 10, smallestEnergy = 10, largestEnergy = 0;
    for (int preset = 0; preset < factoryPresetCount; ++preset) {
        const auto pattern = fitPatternToPhrase(makePattern(preset), 0.5);
        for (const bool keepSub : {false, true}) {
            engine.reset();
            double inputEnergy = 0, outputEnergy = 0, changedEnergy = 0;
            for (int frame = 0; frame < totalFrames; ++frame) {
                const int sourceFrame = frame - engine.latencySamples();
                const auto point = sourceFrame >= 0
                    ? evaluatePattern(pattern, sourceFrame / beatFrames) : PatternSample {};
                const float input = source[static_cast<std::size_t>(frame)];
                const auto output = engine.process(input, -input, point, beatFrames, keepSub, false);
                if (!std::isfinite(output[0]) || output[0] != -output[1]) {
                    std::cerr << "FAIL shortest phrase: finite, linked stereo output\n";
                    std::exit(1);
                }
                peak = std::max(peak, std::abs(static_cast<double>(output[0])));
                if (sourceFrame >= 2000) {
                    const float reference = source[static_cast<std::size_t>(sourceFrame)];
                    inputEnergy += reference * reference;
                    outputEnergy += output[0] * output[0];
                    changedEnergy += std::pow(output[0] - reference, 2);
                }
            }
            smallestChange = std::min(smallestChange, std::sqrt(changedEnergy / inputEnergy));
            smallestEnergy = std::min(smallestEnergy, outputEnergy / inputEnergy);
            largestEnergy = std::max(largestEnergy, outputEnergy / inputEnergy);
        }
    }
    if (peak >= 0.85 || smallestChange <= 0.01 || smallestEnergy <= 0.1 || largestEnergy >= 2.0) {
        std::cerr << "FAIL shortest phrase: useful source change and bounded output level\n";
        std::exit(1);
    }
    std::cout << factoryPresetCount << " shortest half-beat phrases at 400 BPM, both sub modes: peak "
              << peak << ", smallest source change " << smallestChange << ", energy ratio "
              << smallestEnergy << ".." << largestEnergy << '\n';
}

inline void runLiveEngineTests()
{
    using backScratch::LiveScratchEngine;
    using backScratch::PatternSample;
    constexpr double sampleRate = 48000.0;
    constexpr double twoPi = 6.28318530717958647692;
    constexpr double samplesPerBeat = sampleRate * 0.5;
    const auto check = [](bool condition, const char* message) {
        if (!condition) { std::cerr << "FAIL live engine: " << message << '\n'; std::exit(1); }
    };
    const auto tone = [&](double hz, int frame, double amplitude = 0.4) {
        return static_cast<float>(amplitude * std::sin(twoPi * hz * frame / sampleRate));
    };
    const auto scratch = [&](int frame) {
        const double phase = std::fmod(frame / samplesPerBeat, 1.0);
        PatternSample p;
        p.delayBeats = phase < 0.5 ? phase * 2.0 : 1.5 - phase;
        p.playbackRate = phase < 0.5 ? -1.0 : 2.0;
        p.gate = phase > 0.8 && phase < 0.9 ? 0.0f : 1.0f;
        p.wet = 1;
        return p;
    };

    // Regression for the v0.4 hollow/comb-filtered sound: a pattern with no
    // time movement must null against one delayed copy of the complete input,
    // at every wet blend and with either sub setting. Previously wet=.5 lost
    // 16.9 dB at 750 Hz, even though this pattern did no scratching at all.
    double worstNeutralError = 0, worstNeutralGainError = 0;
    for (bool keepSub : {false, true}) {
        for (float wet : {0.0f, 0.25f, 0.5f, 1.0f}) {
            for (double hz : {5.0, 50.0, 100.0, 150.0, 250.0, 500.0, 750.0,
                              1000.0, 2000.0, 5000.0, 10000.0, 18000.0}) {
                LiveScratchEngine neutral;
                neutral.prepare(sampleRate, 2);
                PatternSample p;
                p.wet = wet;
                double error = 0, energy = 0, referenceEnergy = 0;
                for (int frame = 0; frame < 48000; ++frame) {
                    const float input = tone(hz, frame);
                    const auto output = neutral.process(input, -input, p, samplesPerBeat, keepSub, false);
                    const float reference = frame >= neutral.latencySamples()
                        ? tone(hz, frame - neutral.latencySamples()) : 0.0f;
                    check(std::abs(output[0] + output[1]) < 1.0e-7f,
                          "neutral stereo signal keeps its phase relationship");
                    if (frame > 24000) {
                        error += std::pow(output[0] - reference, 2);
                        energy += output[0] * output[0];
                        referenceEnergy += reference * reference;
                    }
                }
                worstNeutralError = std::max(worstNeutralError, std::sqrt(error / referenceEnergy));
                worstNeutralGainError = std::max(worstNeutralGainError,
                    std::abs(std::sqrt(energy / referenceEnergy) - 1.0));
            }
        }
    }
    check(worstNeutralError < 2.0e-7 && worstNeutralGainError < 1.0e-7,
          "neutral time mapping stays phase/level transparent from 5 Hz to 18 kHz at every blend");

    // Changing a neutral wet blend or the Keep Sub toggle must not create a
    // chorus/comb filter on transients or broadband material either.
    LiveScratchEngine transparent;
    transparent.prepare(sampleRate, 2);
    std::vector<float> broadband(48000);
    std::uint32_t broadbandState = 38745;
    double largestNeutralStepError = 0;
    for (int frame = 0; frame < 48000; ++frame) {
        broadbandState = broadbandState * 1664525u + 1013904223u;
        broadband[static_cast<std::size_t>(frame)] = static_cast<float>(
            0.25 * (static_cast<double>(broadbandState >> 8) / 8388608.0 - 1.0));
        PatternSample p;
        p.wet = static_cast<float>((frame / 1000) % 5) * 0.25f;
        const auto output = transparent.process(broadband[static_cast<std::size_t>(frame)],
            -broadband[static_cast<std::size_t>(frame)], p, samplesPerBeat, (frame / 3500) % 2 == 0, false);
        const float reference = frame >= transparent.latencySamples()
            ? broadband[static_cast<std::size_t>(frame - transparent.latencySamples())] : 0.0f;
        largestNeutralStepError = std::max(largestNeutralStepError, std::abs(static_cast<double>(output[0] - reference)));
    }
    check(largestNeutralStepError < 1.0e-7,
          "neutral blend and sub-toggle transitions reproduce delayed broadband transients without comb filtering");

    // The crossover delay includes every decimation/reconstruction stage.
    // Validate the timing contract at host rates with different stage counts.
    for (double hostRate : {8000.0, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0}) {
        LiveScratchEngine aligned;
        aligned.prepare(hostRate, 2);
        PatternSample neutral;
        neutral.wet = 1;
        int impulses = 0;
        for (int frame = 0; frame < aligned.latencySamples() + 1000; ++frame) {
            const auto output = aligned.process(frame == 100 ? 0.75f : 0.0f,
                frame == 103 ? -0.4f : 0.0f, neutral, hostRate * 0.5, true, false);
            const float expectedLeft = frame == 100 + aligned.latencySamples() ? 0.75f : 0.0f;
            const float expectedRight = frame == 103 + aligned.latencySamples() ? -0.4f : 0.0f;
            check(std::abs(output[0] - expectedLeft) < 1.0e-7f
                  && std::abs(output[1] - expectedRight) < 1.0e-7f,
                  "neutral stereo impulses arrive only at the declared latency at every sample rate");
            if (output[0] == 0.75f) ++impulses;
        }
        check(impulses == 1, "aligned crossover does not duplicate or smear a neutral transient");
    }

    // A gated upper band leaves a bass wave in the original phase, not an
    // all-pass-rotated version. Also measure unwanted multirate image/alias
    // leakage into that protected band from higher-frequency source audio.
    double bassQuadrature = 0, bassInPhase = 0, worstBassLeakage = 0;
    for (double hz : {100.0, 11900.0, 12100.0, 23900.0}) {
        LiveScratchEngine split;
        split.prepare(sampleRate, 2);
        PatternSample muted;
        muted.wet = 1;
        muted.gate = 0;
        double energy = 0;
        for (int frame = 0; frame < 96000; ++frame) {
            const auto output = split.process(tone(hz, frame), tone(hz, frame), muted, samplesPerBeat, true, false);
            if (frame >= 48000) {
                energy += output[0] * output[0];
                if (hz == 100.0) {
                    const double angle = twoPi * hz * (frame - split.latencySamples()) / sampleRate;
                    bassInPhase += output[0] * std::sin(angle);
                    bassQuadrature += output[0] * std::cos(angle);
                }
            }
        }
        if (hz > 1000.0) worstBassLeakage = std::max(worstBassLeakage, std::sqrt(energy / 48000));
    }
    check(bassInPhase > 9500 && std::abs(bassQuadrature / bassInPhase) < 1.0e-6,
          "protected bass keeps its original phase at the reported latency");
    check(worstBassLeakage < 0.0001,
          "multirate bass extraction does not alias treble into the protected low band");

    // Keep Sub protects the original bass; turning it off scratches the full
    // signal. It never permanently high-passes dry or full-range scratch audio.
    LiveScratchEngine keep, full, noScratch;
    keep.prepare(sampleRate, 2);
    full.prepare(sampleRate, 2);
    noScratch.prepare(sampleRate, 2);
    double keptEnergy = 0, fullEnergy = 0, unchangedEnergy = 0;
    double peak = 0;
    for (int frame = 0; frame < 144000; ++frame) {
        const float input = tone(45, frame);
        const auto kept = keep.process(input, -input, scratch(frame), samplesPerBeat, true, false);
        const auto scratched = full.process(input, -input, scratch(frame), samplesPerBeat, false, false);
        const auto unchanged = noScratch.process(input, -input, {}, samplesPerBeat, true, false);
        check(std::isfinite(kept[0]) && std::isfinite(scratched[0]), "scratch output stays finite");
        check(std::abs(kept[0] + kept[1]) < 1.0e-7f, "stereo channels share exactly the same read trajectory");
        if (frame > 48000) {
            keptEnergy += kept[0] * kept[0];
            fullEnergy += scratched[0] * scratched[0];
            unchangedEnergy += unchanged[0] * unchanged[0];
            peak = std::max(peak, static_cast<double>(std::abs(kept[0])));
        }
    }
    const double keepRatio = std::sqrt(keptEnergy / unchangedEnergy);
    const double fullRatio = std::sqrt(fullEnergy / unchangedEnergy);
    check(keepRatio > 0.995 && keepRatio < 1.005,
          "Keep Sub retains original deep bass energy through reversals and gates");
    check(fullRatio > 0.85 && fullRatio < 1.05,
          "Keep Sub off scratches bass at full level instead of removing the low band");
    check(peak < 0.405, "protected sub has no unintended gain boost");

    keep.reset();
    full.reset();
    double kept100 = 0, full100 = 0, source100 = 0;
    for (int frame = 0; frame < 96000; ++frame) {
        const float input = tone(100, frame);
        const auto kept = keep.process(input, input, scratch(frame), samplesPerBeat, true, false);
        const auto scratched = full.process(input, input, scratch(frame), samplesPerBeat, false, false);
        if (frame >= 48000) {
            kept100 += kept[0] * kept[0];
            full100 += scratched[0] * scratched[0];
            source100 += input * input;
        }
    }
    const double keep100Ratio = std::sqrt(kept100 / source100);
    const double full100Ratio = std::sqrt(full100 / source100);
    check(keep100Ratio > 0.96 && keep100Ratio < 1.01, "100 Hz bass remains strong during scratch fills");
    check(full100Ratio > 0.85 && full100Ratio < 1.05, "unprotected 100 Hz bass is scratched at full level");

    // Check fidelity while the readhead really moves, not just identity at 1x.
    // Compare the output with the analytical delayed/resampled waveform. This
    // covers bass, low mids and vocal/body frequencies in both directions.
    double worstWarpError = 0;
    for (double speed : {-2.0, -1.0, 0.5, 2.0}) {
        for (double hz : {45.0, 100.0, 250.0, 1000.0, 6000.0}) {
            full.reset();
            double error = 0, referenceEnergy = 0;
            for (int frame = 0; frame < 55000; ++frame) {
                PatternSample p;
                p.wet = frame >= 48000 ? 1.0f : 0.0f;
                p.playbackRate = speed;
                const double delayFrames = frame < 48000 ? 12000.0
                    : 12000.0 + (frame - 48000) * (1.0 - speed);
                p.delayBeats = delayFrames / samplesPerBeat;
                const auto output = full.process(tone(hz, frame), tone(hz, frame), p, samplesPerBeat, false, false);
                if (frame >= 50000) {
                    const double sourceFrame = frame - full.latencySamples() - delayFrames;
                    const double reference = 0.4 * std::sin(twoPi * hz * sourceFrame / sampleRate);
                    error += std::pow(output[0] - reference, 2);
                    referenceEnergy += reference * reference;
                }
            }
            worstWarpError = std::max(worstWarpError, std::sqrt(error / referenceEnergy));
        }
    }
    check(worstWarpError < 0.0005,
          "full-range forward/reverse time warping retains bass-to-treble fidelity without extra filtering");

    // Warmup must use incoming sound, and reset must never revive old history.
    LiveScratchEngine fresh;
    fresh.prepare(sampleRate, 2);
    PatternSample delayed;
    delayed.delayBeats = 1;
    delayed.wet = 1;
    double firstEnergy = 0;
    for (int frame = 0; frame < 12000; ++frame) {
        const auto output = fresh.process(tone(1000, frame), tone(1500, frame), delayed, samplesPerBeat, true, false);
        if (frame > 1000) firstEnergy += output[0] * output[0];
    }
    check(firstEnergy > 500, "unavailable lookback history falls back to audible incoming sound");
    fresh.reset();
    for (int frame = 0; frame < 48000; ++frame) {
        const auto output = fresh.process(0, 0, delayed, samplesPerBeat, true, false);
        check(output[0] == 0 && output[1] == 0, "seeking invalidates old history without stale phrase audio");
    }

    fresh.reset();
    delayed.delayBeats = 0.25;
    double impulseEnergy = 0;
    for (int frame = 0; frame < 20000; ++frame) {
        const auto output = fresh.process(frame == 10000 ? 0.5f : 0.0f, 0, delayed, samplesPerBeat, false, false);
        if (frame < 16000 + fresh.latencySamples())
            check(std::abs(output[0]) < 1.0e-10f, "causal lookback cannot read an impulse before its delayed arrival");
        else impulseEnergy += output[0] * output[0];
    }
    check(impulseEnergy > 0.15, "valid delayed transient reaches the scratch readhead");

    // Silent startup, bypass, and malicious/non-finite host values.
    fresh.reset();
    for (int frame = 0; frame < 100; ++frame) {
        const auto output = fresh.process(0, 0, scratch(frame), samplesPerBeat, true, false);
        check(output[0] == 0 && output[1] == 0, "silent startup stays silent");
    }
    fresh.reset();
    for (int frame = 0; frame < 2000; ++frame) {
        const float left = tone(41, frame), right = tone(611, frame);
        const auto output = fresh.process(left, right, scratch(frame), samplesPerBeat, false, true);
        const float delayedLeft = frame >= fresh.latencySamples() ? tone(41, frame - fresh.latencySamples()) : 0.0f;
        const float delayedRight = frame >= fresh.latencySamples() ? tone(611, frame - fresh.latencySamples()) : 0.0f;
        check(output[0] == delayedLeft && output[1] == delayedRight,
              "stopped transport bypass returns the exact input at the declared latency");
    }
    PatternSample invalid;
    invalid.delayBeats = invalid.playbackRate = std::numeric_limits<double>::quiet_NaN();
    invalid.gate = invalid.wet = std::numeric_limits<float>::quiet_NaN();
    const auto sanitized = fresh.process(std::numeric_limits<float>::quiet_NaN(), 0, invalid,
                                         std::numeric_limits<double>::infinity(), true, false);
    check(std::isfinite(sanitized[0]) && std::isfinite(sanitized[1]), "invalid controls and samples cannot poison the buffer");

    // The same repeating gesture reads each new phrase instead of replaying a
    // saved capture. Measure a 1 kHz phrase followed by a 3 kHz phrase.
    fresh.reset();
    PatternSample repeated;
    repeated.delayBeats = 0.25;
    repeated.wet = 1;
    double lateNewCos = 0, lateNewSin = 0, lateOldCos = 0, lateOldSin = 0;
    for (int frame = 0; frame < 144000; ++frame) {
        const float input = tone(frame < 72000 ? 1000 : 3000, frame);
        const auto output = fresh.process(input, input, repeated, samplesPerBeat, true, false);
        if (frame >= 96000) {
            lateNewCos += output[0] * std::cos(twoPi * 3000 * frame / sampleRate);
            lateNewSin += output[0] * std::sin(twoPi * 3000 * frame / sampleRate);
            lateOldCos += output[0] * std::cos(twoPi * 1000 * frame / sampleRate);
            lateOldSin += output[0] * std::sin(twoPi * 1000 * frame / sampleRate);
        }
    }
    const double newPhraseLevel = std::hypot(lateNewCos, lateNewSin) / 24000;
    const double stalePhraseLevel = std::hypot(lateOldCos, lateOldSin) / 24000;
    check(newPhraseLevel > 0.38 && stalePhraseLevel < 0.001,
          "subsequent pattern loops process new incoming phrases");

    // A near-half-cycle discontinuity has a bounded fade rather than a click.
    fresh.reset();
    double previous = 0, maxJumpStep = 0;
    repeated.delayBeats = 0.1;
    for (int frame = 0; frame < 30000; ++frame) {
        if (frame == 28000) repeated.delayBeats += 23.0 / samplesPerBeat;
        const auto output = fresh.process(tone(1000, frame), tone(1000, frame), repeated, samplesPerBeat, false, false);
        if (frame >= 28000 && frame < 28200)
            maxJumpStep = std::max(maxJumpStep, std::abs(output[0] - previous));
        previous = output[0];
    }
    check(maxJumpStep < 0.08, "discontinuous scratch jumps are crossfaded without large waveform steps");

    // A held readhead should settle to silence, never a sustained DC value.
    double holdEnergy = 0;
    for (int frame = 0; frame < 12000; ++frame) {
        repeated.delayBeats += 1.0 / samplesPerBeat;
        repeated.playbackRate = 0;
        const auto output = fresh.process(tone(1000, frame), tone(1000, frame), repeated, samplesPerBeat, false, false);
        if (frame > 6000) holdEnergy += output[0] * output[0];
    }
    check(std::sqrt(holdEnergy / 5999) < 0.0002, "frozen scratch fades to silence and rejects DC");

    // Faster playback must reject frequencies that would alias audibly.
    double worstAlias = 0;
    for (double speed : {-2.0, 2.0}) {
        fresh.reset();
        repeated.playbackRate = speed;
        double energy = 0;
        for (int frame = 0; frame < 72000; ++frame) {
            repeated.delayBeats = frame < 48000 ? 0.5
                : (24000.0 + (frame - 48000) * (1.0 - speed)) / samplesPerBeat;
            const auto output = fresh.process(tone(17000, frame), tone(17000, frame), repeated, samplesPerBeat, false, frame < 48000);
            if (frame >= 49000 && frame < 55000) energy += output[0] * output[0];
        }
        worstAlias = std::max(worstAlias, std::sqrt(energy / 6000));
    }
    check(worstAlias < 0.001, "2x forward and reverse reject out-of-band input before it aliases");

    // Ring wrap and retained-history boundaries exercise modulo indexing.
    LiveScratchEngine wrapped;
    wrapped.prepare(8000, 1);
    repeated.delayBeats = 0.1;
    repeated.playbackRate = 1;
    double wrapEnergy = 0;
    for (int frame = 0; frame < 140000; ++frame) {
        const float input = static_cast<float>(0.3 * std::sin(twoPi * 700 * frame / 8000.0));
        const auto output = wrapped.process(input, 999, repeated, 4000, true, false);
        check(std::isfinite(output[0]) && output[0] == output[1], "mono and ring wrap remain finite and linked");
        if (frame > 135000) wrapEnergy += output[0] * output[0];
    }
    check(wrapEnergy > 150, "rolling buffer still reads fresh audio after history capacity wraps");

    // Length now stretches the same gesture, including its lookback. A deep
    // read must not silently flatten at the former 16-second / 64-beat limits.
    // Keep this oracle at 8 kHz so a genuine two-minute history is inexpensive.
    double deepReadError = 0, stretchedSourceError = 0;
    int stretchedSourceFrames = 0;
    {
        constexpr double slowRate = 8000.0;
        LiveScratchEngine longHistory;
        longHistory.prepare(slowRate, 2, LiveScratchEngine::maximumHistorySeconds);
        PatternSample deep;
        deep.delayBeats = 96.0;
        deep.wet = 1.0f;
        int reproducedImpulses = 0;
        for (int frame = 0; frame < 131 * 8000; ++frame) {
            const float input = frame == 8 * 8000 ? 0.7f : frame == 34 * 8000 ? -0.4f : 0.0f;
            const auto output = longHistory.process(input, -input, deep, slowRate, false, false);
            if (frame >= 97 * 8000) {
                const int sourceFrame = frame - longHistory.latencySamples() - 96 * 8000;
                const float expected = sourceFrame == 8 * 8000 ? 0.7f
                    : sourceFrame == 34 * 8000 ? -0.4f : 0.0f;
                deepReadError = std::max(deepReadError, std::abs(static_cast<double>(output[0] - expected)));
                check(output[0] == -output[1], "long lookback preserves stereo polarity through ring wrap");
                if (std::abs(output[0]) > 0.3f) ++reproducedImpulses;
            }
        }
        check(reproducedImpulses == 2 && deepReadError < 1.0e-6,
              "96-second, 96-beat reads reproduce original impulses before and after the 128-second ring wraps");
        deep.delayBeats = 129.0;
        for (int frame = 0; frame < 1000; ++frame) {
            const auto output = longHistory.process(0.123f, -0.123f, deep, slowRate, false, false);
            if (frame > 500)
                check(output[0] == 0.123f && output[1] == -0.123f,
                      "lookback older than the retained history returns smoothly to live audio without clamping the gesture");
        }

        // Exercise the deepest factory gesture at 16 bars / 20 BPM. A gentle
        // ramp encodes source time directly: interior wet samples must follow
        // the requested original source, not live fallback or a clipped delay.
        longHistory.reset();
        const auto longPattern = backScratch::fitPatternToPhrase(backScratch::makePattern(40), 64.0);
        constexpr double slowBeat = slowRate * 3.0;
        const int totalFrames = static_cast<int>(longPattern.beats * slowBeat);
        for (int frame = 0; frame < totalFrames; ++frame) {
            const auto input = static_cast<float>(0.05 + 0.2 * frame / totalFrames);
            const double sourceBeat = (frame - longHistory.latencySamples()) / slowBeat;
            const auto point = sourceBeat >= 0 ? backScratch::evaluatePattern(longPattern, sourceBeat) : PatternSample {};
            const auto output = longHistory.process(input, -input, point, slowBeat, false, false);
            const auto& segment = longPattern.segments[static_cast<std::size_t>(point.segment)];
            const double sourceFrame = frame - longHistory.latencySamples() - point.delayBeats * slowBeat;
            if (point.delayBeats * 3.0 > 16.0 && sourceFrame > 100.0
                && point.wet == 1.0f && point.gate == 1.0f && std::abs(point.playbackRate) > 0.1
                && (sourceBeat - segment.start) * 3.0 > 0.03) {
                const double expected = 0.05 + 0.2 * sourceFrame / totalFrames;
                stretchedSourceError = std::max(stretchedSourceError,
                    std::abs(static_cast<double>(output[0]) - expected));
                ++stretchedSourceFrames;
            }
        }
        check(stretchedSourceFrames > 100000 && stretchedSourceError < 2.0e-6,
              "a 16-bar factory phrase at 20 BPM follows the complete stretched source trajectory");
    }
    // Exercise every recipe on three deliberately different source families:
    // sustained sub+saw bass, transient drums, and voiced/formant-like material.
    // These are signal-quality checks, not a substitute for a listening pass.
    constexpr double familyRate = 24000.0;
    constexpr double familyBeat = familyRate * 0.5;
    int familyLength = 1;
    for (int preset = 0; preset < backScratch::factoryPresetCount; ++preset)
        familyLength = std::max(familyLength,
            static_cast<int>(backScratch::makePattern(preset).beats * familyBeat * 2) + 1);
    constexpr double generatedBeat = familyRate * 60.0 / 140.0;
    familyLength = std::max(familyLength, static_cast<int>(64.0 * generatedBeat) + 2000);
    std::array<std::vector<float>, 3> material, reference;
    std::uint32_t noiseState = 357911u;
    for (int family = 0; family < 3; ++family) {
        material[static_cast<std::size_t>(family)].resize(familyLength);
        reference[static_cast<std::size_t>(family)].resize(familyLength);
    }
    for (int frame = 0; frame < familyLength; ++frame) {
        const double t = frame / familyRate;
        const double beat = std::fmod(t * 2.0, 4.0);
        noiseState = noiseState * 1664525u + 1013904223u;
        const double noise = static_cast<double>(noiseState >> 8) / 8388608.0 - 1.0;
        double bass = 0.31 * std::sin(twoPi * 55 * t);
        for (int harmonic = 1; harmonic <= 12; ++harmonic)
            bass += 0.06 / harmonic * std::sin(twoPi * 220 * harmonic * t);
        const double kickTime = std::fmod(t, 1.0);
        const double snareTime = std::fmod(t + 0.5, 1.0);
        const double hatTime = std::fmod(t, 0.25);
        const double drums = 0.42 * std::exp(-kickTime * 18.0)
            * std::sin(twoPi * (48 * kickTime + 60.0 * (1.0 - std::exp(-kickTime * 30.0)) / 30.0))
            + 0.25 * std::exp(-snareTime * 30.0) * noise
            + 0.08 * std::exp(-hatTime * 85.0) * noise;
        double vowel = 0;
        for (int harmonic = 1; harmonic <= 32; ++harmonic) {
            const double frequency = harmonic * 145.0;
            const double formants = std::exp(-std::pow((frequency - 800) / 250, 2.0))
                + 0.55 * std::exp(-std::pow((frequency - 1500) / 350, 2.0));
            vowel += (0.025 / harmonic + 0.032 * formants)
                * std::sin(twoPi * frequency * t + 0.03 * std::sin(twoPi * 5 * t));
        }
        vowel *= 0.3 + 0.7 * std::pow(std::sin(twoPi * 0.5 * std::fmod(beat, 1.0)), 2.0);
        material[0][static_cast<std::size_t>(frame)] = static_cast<float>(bass);
        material[1][static_cast<std::size_t>(frame)] = static_cast<float>(drums);
        material[2][static_cast<std::size_t>(frame)] = static_cast<float>(vowel);
    }
    LiveScratchEngine familyEngine;
    familyEngine.prepare(familyRate, 2);
    for (int family = 0; family < 3; ++family) {
        familyEngine.reset();
        for (int frame = 0; frame < familyLength; ++frame) {
            const float input = material[static_cast<std::size_t>(family)][static_cast<std::size_t>(frame)];
            reference[static_cast<std::size_t>(family)][static_cast<std::size_t>(frame)]
                = familyEngine.process(input, input, {}, familyBeat, true, false)[0];
        }
    }
    double familyPeak = 0, smallestChange = 1;
    for (int preset = 0; preset < backScratch::factoryPresetCount; ++preset) {
        const auto pattern = backScratch::makePattern(preset);
        const int totalFrames = static_cast<int>(pattern.beats * familyBeat * 2);
        for (int family = 0; family < 3; ++family) {
            familyEngine.reset();
            double changedEnergy = 0, originalEnergy = 0, outputEnergy = 0;
            for (int frame = 0; frame < totalFrames; ++frame) {
                const auto index = static_cast<std::size_t>(frame);
                const auto f = static_cast<std::size_t>(family);
                const float input = material[f][index];
                const auto output = familyEngine.process(input, -input,
                    backScratch::evaluatePattern(pattern, frame / familyBeat), familyBeat, true, false);
                check(std::isfinite(output[0]) && std::abs(output[0] + output[1]) < 1.0e-7f,
                      "every bass/drum/vocal preset remains finite and stereo linked");
                if (frame < 1000)
                    check(output[0] == reference[f][index], "factory dry opening passes the current phrase");
                const double difference = output[0] - reference[f][index];
                changedEnergy += difference * difference;
                originalEnergy += reference[f][index] * reference[f][index];
                outputEnergy += output[0] * output[0];
                familyPeak = std::max(familyPeak, static_cast<double>(std::abs(output[0])));
            }
            const double change = std::sqrt(changedEnergy / originalEnergy);
            smallestChange = std::min(smallestChange, change);
            check(change > 0.015, "every factory recipe audibly transforms each test source family");
            check(outputEnergy > 0.1 * originalEnergy && outputEnergy < 2.0 * originalEnergy,
                  "factory recipes preserve usable level without runaway gain");
        }
    }
    check(familyPeak < 0.85, "representative bass/drum/vocal transients retain headroom across the bank");
    // Generated compositions are independent of the factory bank. Render
    // complete 1-, 4-, and 16-bar phrases at 140 BPM, in both sub modes, rather
    // than checking only short early excerpts of a long phrase.
    double generatedPeak = 0, smallestGeneratedEnergyRatio = 10, largestGeneratedEnergyRatio = 0;
    double smallestProtectedBass = 1;
    int generatedRenders = 0;
    constexpr std::array<std::uint32_t, 2> generatedSeeds {20260925u, 79421u};
    for (double phraseBeats : {4.0, 16.0, 64.0}) {
        for (const auto seed : generatedSeeds) {
            const auto pattern = backScratch::generatePattern(seed, phraseBeats);
            const int totalFrames = static_cast<int>(std::ceil(phraseBeats * generatedBeat))
                + familyEngine.latencySamples();
            for (int family = 0; family < 3; ++family) {
                for (bool keepSub : {false, true}) {
                    familyEngine.reset();
                    std::array<float, 4096> beginning {};
                    double inputEnergy = 0, outputEnergy = 0, bassInPhaseSum = 0;
                    int measuredFrames = 0;
                    for (int frame = 0; frame < totalFrames; ++frame) {
                        const auto index = static_cast<std::size_t>(frame);
                        const auto f = static_cast<std::size_t>(family);
                        const float input = material[f][index];
                        const int sourceFrame = frame - familyEngine.latencySamples();
                        const auto point = sourceFrame >= 0
                            ? backScratch::evaluatePattern(pattern, sourceFrame / generatedBeat) : PatternSample {};
                        const auto output = familyEngine.process(input, -input, point, generatedBeat, keepSub, false);
                        check(std::isfinite(output[0]) && std::abs(output[0] + output[1]) < 1.0e-7f,
                              "complete generated bass/drum/vocal phrases stay finite and stereo linked");
                        if (frame < static_cast<int>(beginning.size())) beginning[index] = output[0];
                        generatedPeak = std::max(generatedPeak, static_cast<double>(std::abs(output[0])));
                        if (sourceFrame >= 1000) {
                            inputEnergy += reference[f][index] * reference[f][index];
                            outputEnergy += output[0] * output[0];
                            bassInPhaseSum += output[0] * std::sin(twoPi * 55 * sourceFrame / familyRate);
                            ++measuredFrames;
                        }
                    }
                    const double energyRatio = outputEnergy / inputEnergy;
                    smallestGeneratedEnergyRatio = std::min(smallestGeneratedEnergyRatio, energyRatio);
                    largestGeneratedEnergyRatio = std::max(largestGeneratedEnergyRatio, energyRatio);
                    check(energyRatio > 0.12 && energyRatio < 2.0,
                          "generated phrases retain finite source level without silent or runaway output");
                    if (family == 0 && keepSub) {
                        const double originalBassAmplitude = 2.0 * bassInPhaseSum / measuredFrames;
                        smallestProtectedBass = std::min(smallestProtectedBass, originalBassAmplitude / 0.31);
                        check(originalBassAmplitude > 0.295 && originalBassAmplitude < 0.325,
                              "generated scratches keep the original bass fundamental when Keep Sub is on");
                    }
                    // Reset after the entire phrase, while history is full.
                    // The next start must reproduce the initial samples exactly.
                    familyEngine.reset();
                    for (int frame = 0; frame < static_cast<int>(beginning.size()); ++frame) {
                        const auto index = static_cast<std::size_t>(frame);
                        const float input = material[static_cast<std::size_t>(family)][index];
                        const int sourceFrame = frame - familyEngine.latencySamples();
                        const auto point = sourceFrame >= 0
                            ? backScratch::evaluatePattern(pattern, sourceFrame / generatedBeat) : PatternSample {};
                        const auto output = familyEngine.process(input, -input, point, generatedBeat, keepSub, false);
                        check(output[0] == beginning[index],
                              "restarting generated playback cannot retain audio/filter state from the previous phrase");
                    }
                    ++generatedRenders;
                }
            }
        }
    }
    check(generatedPeak < 0.85, "generated source-family test transients retain measured headroom");

    // Verify rejection on real generated high-speed strokes. The input tone
    // would fold below Nyquist without speed-dependent antialias filtering.
    LiveScratchEngine generatedAlias;
    generatedAlias.prepare(sampleRate, 2);
    double generatedAliasEnergy = 0;
    int generatedAliasFrames = 0;
    for (const auto seed : generatedSeeds) {
        const auto pattern = backScratch::generatePattern(seed, 16.0);
        generatedAlias.reset();
        const double beatFrames = sampleRate * 60.0 / 140.0;
        const int totalFrames = static_cast<int>(std::ceil(pattern.beats * beatFrames));
        for (int frame = 0; frame < totalFrames; ++frame) {
            const double beat = (frame - generatedAlias.latencySamples()) / beatFrames;
            const auto point = beat >= 0 ? backScratch::evaluatePattern(pattern, beat) : PatternSample {};
            const auto output = generatedAlias.process(tone(17000, frame), tone(17000, frame),
                                                       point, beatFrames, false, false);
            const auto& segment = pattern.segments[static_cast<std::size_t>(point.segment)];
            if (frame > 48000 && point.wet == 1.0f && point.gate > 0.99f
                && std::abs(point.playbackRate) >= 1.8 && beat - segment.start > 0.04) {
                generatedAliasEnergy += output[0] * output[0];
                ++generatedAliasFrames;
            }
        }
    }
    check(generatedAliasFrames > 500, "generated antialias test includes sustained high-speed scratch samples");
    const double generatedAliasRms = std::sqrt(generatedAliasEnergy / generatedAliasFrames);
    check(generatedAliasRms < 0.001, "generated high-speed scratch strokes reject out-of-band input");
    runShortestPhraseEngineTests();
    std::cout << generatedRenders << " full generated source-family/sub-mode renders at 140 BPM: peak "
              << generatedPeak << ", energy ratio " << smallestGeneratedEnergyRatio << ".." << largestGeneratedEnergyRatio
              << ", minimum original bass " << smallestProtectedBass << "x, rejected alias RMS "
              << generatedAliasRms << '\n';
    std::cout << backScratch::factoryPresetCount << " presets x bass/drums/vocal-like sources: peak " << familyPeak
              << ", smallest normalized change " << smallestChange << '\n';
    std::cout << "128-second history: 96-second impulse error " << deepReadError
              << ", 16-bar / 20 BPM source error " << stretchedSourceError
              << " across " << stretchedSourceFrames << " deep-lookback samples\n";
    std::cout << "Live scratch engine: neutral relative error " << worstNeutralError
              << ", broadband transition error " << largestNeutralStepError << ", time-warp relative error " << worstWarpError
              << "; sub kept/full " << keepRatio << '/' << fullRatio
              << "x; 100 Hz kept/full " << keep100Ratio << '/' << full100Ratio
              << "x; jump step " << maxJumpStep << "; rejected alias RMS " << worstAlias << '\n';
}
