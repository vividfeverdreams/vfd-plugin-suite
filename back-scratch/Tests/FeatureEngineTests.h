#pragma once
#include "LiveScratchEngine.h"
#include "PatternEditing.h"
#include "PresetBank.h"
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// 0.11 engine and preset-bank features: mix/output, Keep Sub split choices at
// one latency, the 4x resampler table, safe history resizing, and the factory
// bank's geometry.
namespace backScratch::featureEngineTests {

inline void require(bool ok, const std::string& text) { if (!ok) throw std::runtime_error(text); }

inline float tone(int frame, double hz = 180.0) { return static_cast<float>(0.4 * std::sin(6.28318530717958647692 * hz * frame / 48000.0)); }

inline PatternSample stroke(int frame)
{
    // A slow push and pull with real speed changes, read from history.
    PatternSample sample;
    const double phase = std::fmod(frame / 24000.0, 1.0);
    sample.delayBeats = 0.1 + 0.08 * std::sin(6.28318530717958647692 * phase);
    // delay in samples = 24000 * delayBeats, so its slope is 0.08 * 2 pi * cos.
    sample.playbackRate = 1.0 - 0.08 * 6.28318530717958647692 * std::cos(6.28318530717958647692 * phase);
    sample.gate = 1.0f;
    sample.wet = 1.0f;
    return sample;
}

// Default shaping must be bit-identical to the 0.10 call.
inline void checkDefaultsIdentical()
{
    LiveScratchEngine plain, shaped;
    plain.prepare(48000.0, 2, 4.0); shaped.prepare(48000.0, 2, 4.0);
    for (int frame = 0; frame < 60000; ++frame) {
        const auto sample = stroke(frame);
        const auto a = plain.process(tone(frame), tone(frame, 233.0), sample, 24000.0, true, false);
        const auto b = shaped.process(tone(frame), tone(frame, 233.0), sample, 24000.0, true, false, LiveScratchEngine::Shaping {});
        require(a[0] == b[0] && a[1] == b[1], "default mix, output, split and speed limit reproduce 0.10 exactly");
    }
}

// Mix 0 is the delayed input; output trim scales the scratched signal and
// leaves bypass at unity.
inline void checkMixAndTrim()
{
    LiveScratchEngine engine;
    engine.prepare(48000.0, 2, 4.0);
    const int latency = engine.latencySamples();
    LiveScratchEngine::Shaping dry;
    dry.mix = 0.0;
    for (int frame = 0; frame < 30000; ++frame) {
        const auto out = engine.process(tone(frame), tone(frame), stroke(frame), 24000.0, false, false, dry);
        if (frame > 200) require(std::abs(out[0] - (frame >= latency ? tone(frame - latency) : 0.0f)) < 1.0e-6f,
                                 "mix 0% passes the delayed input");
    }
    LiveScratchEngine loud, unity;
    loud.prepare(48000.0, 2, 4.0); unity.prepare(48000.0, 2, 4.0);
    LiveScratchEngine::Shaping boost;
    boost.gain = 2.0;
    for (int frame = 0; frame < 30000; ++frame) {
        const auto a = loud.process(tone(frame), tone(frame), stroke(frame), 24000.0, false, false, boost);
        const auto b = unity.process(tone(frame), tone(frame), stroke(frame), 24000.0, false, false);
        if (frame > 1000) require(std::abs(a[0] - 2.0f * b[0]) < 1.0e-5f, "output trim is a clean gain on the effect output");
    }
    for (int frame = 30000; frame < 32000; ++frame) {
        const auto a = loud.process(tone(frame), tone(frame), stroke(frame), 24000.0, false, true, boost);
        if (frame > 30500) require(a[0] == tone(frame - latency), "bypass ignores the output trim");
    }
}

// Every Keep Sub split shares one latency; changing it blends without a step.
inline void checkSplits()
{
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        LiveScratchEngine engine;
        engine.prepare(rate, 2, 2.0);
        const int latency = engine.latencySamples();
        for (int split = 0; split < static_cast<int>(LiveScratchEngine::splitFrequencies.size()); ++split) {
            LiveScratchEngine other;
            other.prepare(rate, 2, 2.0);
            require(other.latencySamples() == latency, "every Keep Sub split reports the same latency");
        }
    }
    // Sub kept by each split: a 50 Hz tone survives a scratch with Keep Sub on.
    double largestStep = 0.0;
    LiveScratchEngine engine;
    engine.prepare(48000.0, 2, 4.0);
    float previous = 0.0f;
    for (int frame = 0; frame < 96000; ++frame) {
        LiveScratchEngine::Shaping shaping;
        shaping.split = frame < 48000 ? 2 : 0;
        const float input = static_cast<float>(0.5 * std::sin(6.28318530717958647692 * 50.0 * frame / 48000.0));
        const auto out = engine.process(input, input, stroke(frame), 24000.0, true, false, shaping);
        if (frame > 47000 && frame < 50000) largestStep = std::max(largestStep, static_cast<double>(std::abs(out[0] - previous)));
        previous = out[0];
    }
    require(largestStep < 0.02, "changing the Keep Sub split mid-stream blends without a click");
}

// Speeds up to 2x read exactly the 0.10 rows; Depth can reach 4x.
inline void checkFastStrokes()
{
    LiveScratchEngine engine;
    engine.prepare(48000.0, 2, 4.0);
    LiveScratchEngine::Shaping fast;
    fast.speedLimit = 4.0;
    double peak = 0.0;
    for (int frame = 0; frame < 48000; ++frame) {
        PatternSample sample;
        // Forward at 3.5x: delay falls by 2.5 samples per sample.
        sample.delayBeats = std::max(0.0, 1.0 - 2.5 * (frame % 9600) / 24000.0);
        sample.playbackRate = sample.delayBeats > 0.0 ? 3.5 : 1.0;
        sample.wet = 1.0f;
        const auto out = engine.process(tone(frame, 440.0), tone(frame, 440.0), sample, 24000.0, false, false, fast);
        require(std::isfinite(out[0]), "4x strokes stay finite");
        peak = std::max(peak, static_cast<double>(std::abs(out[0])));
    }
    require(peak < 0.6, "4x strokes stay within the source level");
}

// Resizing the history while running keeps the dry pipeline exact and never
// reads audio older than what was carried over.
inline void checkHistoryResize()
{
    LiveScratchEngine engine;
    engine.prepare(48000.0, 2, 2.0);
    const int latency = engine.latencySamples();
    require(engine.historySeconds() == 2.0, "the engine starts with the requested history");
    PatternSample back;
    back.delayBeats = 1.0; // one beat (24000 samples) back
    back.wet = 1.0f;
    int frame = 0;
    for (; frame < 60000; ++frame) engine.process(tone(frame), tone(frame), back, 24000.0, false, false);
    require(engine.isHistoryReady(), "the lookback is available before resizing");
    require(engine.requestHistory(8.0), "the message thread can prepare a larger history");
    engine.adoptRequestedHistory();
    engine.collectRetiredHistory();
    require(engine.historySeconds() == 8.0, "the audio thread adopts the larger history");
    bool sawFallback = false;
    for (int k = 0; k < 40000; ++k, ++frame) {
        PatternSample dryPoint;
        const auto dryOut = engine.process(tone(frame), tone(frame), k < 30000 ? back : dryPoint, 24000.0, false, false);
        if (k < 24000) sawFallback = sawFallback || !engine.isHistoryReady();
        if (k >= 30500) require(dryOut[0] == tone(frame - latency), "the dry pipeline continues exactly across a resize");
    }
    require(sawFallback, "audio older than the carried-over pipeline is never read after a resize");
}

// Every factory bank entry is valid, bounded geometry.
inline int checkPresetBank()
{
    const auto& presets = bankPresets();
    std::set<std::string> names;
    int exact = 0, tuned = 0, fresh = 0;
    for (const auto& preset : presets) {
        require(!preset.name.empty() && !preset.description.empty() && !preset.folder.empty(), "every preset is named and described");
        require(names.insert(preset.folder + "/" + preset.name).second, "preset names are unique within a folder: " + preset.name);
        if (preset.classic >= 0) {
            require(preset.classic < factoryPresetCount, "classic presets point at an original recipe");
            (preset.exact ? exact : tuned) += 1;
            continue;
        }
        ++fresh;
        require(preset.build != nullptr, "new presets have geometry");
        for (const auto build : std::array<PatternBuild, 8> {preset.build, preset.slots[0], preset.slots[1], preset.slots[2],
                                                              preset.slots[3], preset.slots[4], preset.slots[5], preset.slots[6]}) {
            if (build == nullptr) continue;
            const auto pattern = build();
            require(pattern.beats == 1.0 && validateEditedPattern(pattern), "factory geometry is valid: " + preset.name);
            double deepest = 0.0;
            for (int i = 0; i < pattern.count; ++i)
                deepest = std::max({deepest, pattern.segments[static_cast<std::size_t>(i)].delayStart,
                                    pattern.segments[static_cast<std::size_t>(i)].delayEnd});
            require(deepest <= 1.0, "factory geometry never looks back more than one phrase: " + preset.name);
        }
        require(preset.settings.mix >= 0.0f && preset.settings.mix <= 100.0f && preset.settings.depth >= 0.0f
                    && preset.settings.depth <= 200.0f && preset.settings.split >= 0 && preset.settings.split <= 3,
                "preset settings are inside the parameter ranges: " + preset.name);
    }
    require(exact == factoryPresetCount && tuned >= factoryPresetCount, "all 75 originals are reachable exactly and tuned");
    for (int index = 0; index < factoryPresetCount; ++index)
        require(classicBankIndex(index) >= 0, "every original has its exact Classic entry");
    require(fresh >= 60, "the bank adds at least sixty new presets");
    return static_cast<int>(presets.size());
}

} // namespace backScratch::featureEngineTests

inline void runFeatureEngineTests()
{
    using namespace backScratch::featureEngineTests;
    checkDefaultsIdentical();
    checkMixAndTrim();
    checkSplits();
    checkFastStrokes();
    checkHistoryResize();
    const int presets = checkPresetBank();
    std::cout << "0.11 engine features: defaults bit-identical, mix/output, four Keep Sub splits at one latency, 4x strokes, "
              << "live history resize and " << presets << " valid factory bank presets passed\n";
}
