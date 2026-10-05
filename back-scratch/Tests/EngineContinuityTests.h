#pragma once
#include "LiveScratchEngine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

// 0.11 engine regressions: click-free bypass, gapless history invalidation,
// moving history-loss fallback, tempo glides, and graceful memory failure.
namespace backScratch::engineContinuityTests {

inline void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }

inline float tone(int frame, double hz = 300.0, double rate = 48000.0)
{
    return static_cast<float>(0.5 * std::sin(6.28318530717958647692 * hz * frame / rate));
}

inline PatternSample held(double delayBeats, float wet = 1.0f)
{
    PatternSample sample;
    sample.delayBeats = delayBeats;
    sample.playbackRate = 1.0;
    sample.gate = 1.0f;
    sample.wet = wet;
    return sample;
}

// Fix 4: engaging Bypass used to switch from scratched to dry audio in one
// sample. It now uses the same 3 ms blend as releasing it.
inline double checkBypassFade()
{
    LiveScratchEngine engine;
    constexpr double rate = 48000.0, beat = 24000.0;
    engine.prepare(rate, 2, 4.0);
    const auto sample = held(0.2537);
    int frame = 0;
    float previous = 0.0f;
    double normalStep = 0.0;
    for (; frame < 48000; ++frame) {
        const auto out = engine.process(tone(frame), tone(frame), sample, beat, false, false);
        if (frame > 40000) normalStep = std::max(normalStep, static_cast<double>(std::abs(out[0] - previous)));
        previous = out[0];
    }
    double engageStep = 0.0;
    const int latency = engine.latencySamples();
    for (int k = 0; k < 2000; ++k, ++frame) {
        const auto out = engine.process(tone(frame), tone(frame), sample, beat, false, true);
        engageStep = std::max(engageStep, static_cast<double>(std::abs(out[0] - previous)));
        previous = out[0];
        if (k > 200) require(out[0] == tone(frame - latency), "after the 3 ms blend, bypass is exactly the delayed input");
    }
    require(engageStep <= normalStep * 1.5, "engaging bypass no longer clicks");
    return engageStep / normalStep;
}

// Fix 3: a seek/start/stop keeps the dry latency pipeline. The pre-seek audio
// plays out without a gap, and no new gesture can read it afterwards.
inline void checkInvalidation()
{
    constexpr double rate = 48000.0, beat = 24000.0;
    LiveScratchEngine dry;
    dry.prepare(rate, 2, 4.0);
    const int latency = dry.latencySamples();
    for (int frame = 0; frame < 30000; ++frame) {
        if (frame == 17000) dry.invalidateHistory();
        const auto out = dry.process(tone(frame), tone(frame), held(0.0, 0.0f), beat, true, false);
        const float expected = frame >= latency ? tone(frame - latency) : 0.0f;
        require(out[0] == expected && out[1] == expected, "invalidating history never interrupts the dry pipeline");
    }

    LiveScratchEngine wet;
    wet.prepare(rate, 2, 4.0);
    const auto sample = held(0.25); // 6000 samples back
    int frame = 0;
    for (; frame < 30000; ++frame) wet.process(tone(frame), tone(frame), sample, beat, false, false);
    require(wet.isHistoryReady(), "the stroke reads valid history before the seek");
    wet.invalidateHistory();
    const int fade = static_cast<int>(rate * 0.003) + 1;
    for (int k = 0; k < 20000; ++k, ++frame) {
        // Silence arrives after the seek: anything audible later than the
        // pipeline/fade would have to be stale pre-seek audio.
        const auto out = wet.process(0.0f, 0.0f, sample, beat, false, false);
        if (k >= fade && k < latency)
            require(out[0] == tone(frame - latency), "the pre-seek dry audio plays out after a seek");
        if (k >= latency)
            require(out[0] == 0.0f && out[1] == 0.0f, "no gesture reads audio from before the seek");
        if (k > latency && k < 6000) require(!wet.isHistoryReady(), "the new history is reported as filling");
    }
    require(wet.isHistoryReady(), "the stroke reads again once new history has arrived");
}

// Fix 11: when a stroke outruns the retained history, the fade back to live
// input follows a moving readhead instead of one held sample.
inline double checkMovingFallback()
{
    constexpr double rate = 48000.0, beat = 4800.0;
    LiveScratchEngine engine;
    engine.prepare(rate, 2, 1.0);
    const int latency = engine.latencySamples();
    std::vector<float> input(160000);
    for (std::size_t i = 0; i < input.size(); ++i) input[i] = tone(static_cast<int>(i), 220.0);
    int lost = -1;
    double largestChange = 0.0, previousScratch = 0.0;
    const double fadeStep = 1.0 / std::max(1.0, rate * 0.003);
    for (int frame = 0; frame < static_cast<int>(input.size()); ++frame) {
        // A half-speed drag whose lookback grows by half a sample per sample.
        PatternSample sample = held(0.5 * frame / beat);
        sample.playbackRate = 0.5;
        const bool wasReady = engine.isHistoryReady();
        const auto out = engine.process(input[static_cast<std::size_t>(frame)], input[static_cast<std::size_t>(frame)], sample, beat, false, false);
        if (lost < 0 && wasReady && !engine.isHistoryReady()) lost = frame;
        if (lost >= 0 && frame - lost < 100) {
            const double weight = 1.0 - (frame - lost + 1) * fadeStep;
            const double dryValue = input[static_cast<std::size_t>(frame - latency)];
            const double scratch = dryValue + (out[0] - dryValue) / weight;
            if (frame > lost) largestChange = std::max(largestChange, std::abs(scratch - previousScratch));
            previousScratch = scratch;
        }
    }
    require(lost > 0, "the drag outruns one second of history");
    require(largestChange > 1.0e-3, "the history-loss fade keeps reading moving audio, not a frozen sample");
    return largestChange;
}

// Fix 12: during a gentle host tempo ramp, a beat-locked readhead glides
// instead of crossfading on every callback. Real tempo steps still snap once.
inline void checkTempoGlide(std::uint64_t& rampJumps, std::uint64_t& deepRampJumps)
{
    constexpr double rate = 48000.0;
    const auto run = [rate](double delayBeats, double stepFraction, int blocks) {
        LiveScratchEngine engine;
        engine.prepare(rate, 2, 8.0);
        double samplesPerBeat = 24000.0;
        int frame = 0;
        for (; frame < 120000; ++frame) engine.process(tone(frame), tone(frame), held(delayBeats), samplesPerBeat, false, false);
        const auto before = engine.readheadJumps();
        for (int block = 0; block < blocks; ++block) {
            samplesPerBeat *= 1.0 + stepFraction;
            for (int i = 0; i < 512; ++i, ++frame)
                engine.process(tone(frame), tone(frame), held(delayBeats), samplesPerBeat, false, false);
        }
        return engine.readheadJumps() - before;
    };
    rampJumps = run(1.0, 0.003, 100);
    require(rampJumps == 0, "a one-beat delay glides through a 0.3%-per-callback tempo ramp");
    deepRampJumps = run(4.0, 0.003, 100);
    require(deepRampJumps <= 40, "even a four-beat delay crossfades far less than once per callback");
    require(run(1.0, 0.10, 1) == 1, "a 10% tempo step still snaps with exactly one crossfade");
    require(run(1.0, 0.0, 100) == 0, "a steady tempo never crossfades");
}

// Fix 10: a failed history allocation falls back instead of throwing.
inline void checkMemoryFallback()
{
    constexpr double rate = 48000.0;
    LiveScratchEngine full;
    full.prepare(rate, 2, 128.0);
    require(full.historySeconds() == 128.0, "the full 128-second history is retained when memory allows");
    LiveScratchEngine limited;
    limited.setHistoryMemoryLimit(static_cast<std::size_t>(20.0 * rate * 4 * sizeof(float)));
    limited.prepare(rate, 2, 128.0);
    require(limited.historySeconds() == 16.0 && limited.latencySamples() == full.latencySamples(),
            "without memory for 128 s the engine keeps 16 s at the same latency");
    LiveScratchEngine pipeline;
    pipeline.setHistoryMemoryLimit(64 * 1024);
    pipeline.prepare(rate, 2, 128.0);
    require(pipeline.historySeconds() == 0.0, "the last resort keeps only the latency pipeline");
    const int latency = pipeline.latencySamples();
    for (int frame = 0; frame < 4000; ++frame) {
        const auto out = pipeline.process(tone(frame), tone(frame), held(0.5), 24000.0, false, false);
        require(out[0] == (frame >= latency ? tone(frame - latency) : 0.0f),
                "a pipeline-only engine still delays the dry audio by the reported latency");
    }
}

} // namespace backScratch::engineContinuityTests

inline void runEngineContinuityTests()
{
    using namespace backScratch::engineContinuityTests;
    const double bypassRatio = checkBypassFade();
    checkInvalidation();
    const double fallbackChange = checkMovingFallback();
    std::uint64_t rampJumps = 0, deepRampJumps = 0;
    checkTempoGlide(rampJumps, deepRampJumps);
    checkMemoryFallback();
    std::cout << "Engine continuity: bypass-on step " << bypassRatio << "x normal, gapless invalidation, moving fallback step "
              << fallbackChange << ", tempo-ramp crossfades " << rampJumps << " (1 beat) / " << deepRampJumps
              << " (4 beats) over 100 callbacks, memory fallback 128 -> 16 -> 0 s\n";
}
