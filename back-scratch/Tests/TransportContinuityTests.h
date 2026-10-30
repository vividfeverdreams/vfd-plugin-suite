#pragma once
#include "BackScratchProcessor.h"
#include "AutomationBridge.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

// 0.11 processor regressions: host bypass, tempo-ramp seek detection,
// gapless discontinuities, click-free Bypass, unchanged-value writes, bounded
// display reads, per-instance automation queues and grouped parameter writes.
namespace backScratch::transportContinuityTests {

inline void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }

struct RampHead final : juce::AudioPlayHead {
    double ppq = 0.0, bpm = 140.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info; info.setIsPlaying(playing); info.setBpm(bpm); info.setPpqPosition(ppq);
        info.setTimeSignature(TimeSignature{4, 4});
        return info;
    }
};

// Never close to zero, so any silent output sample is a dropout.
inline float lifted(long frame) { return static_cast<float>(0.5 + 0.25 * std::sin(6.283185307179586 * 220.0 * frame / 48000.0)); }
inline float tone(long frame) { return static_cast<float>(0.5 * std::sin(6.283185307179586 * 300.0 * frame / 48000.0)); }

// A whole-phrase stroke that holds a constant lookback: one quarter of the
// phrase, i.e. one beat at the default one-bar length.
inline void useHeldDelay(BackScratchProcessor& processor)
{
    ScratchPattern held;
    held.beats = 1.0;
    held.count = 1;
    held.editingRange = 0.25;
    held.segments[0] = {0.0, 1.0, 0.25, 0.25, 1.0f, 1.0f, 1.0f, false};
    require(processor.beginPatternEdit() && processor.previewPatternEdit(held) && processor.commitPatternEdit(),
            "test pattern installs as an edited phrase");
}

struct Run {
    BackScratchProcessor processor;
    RampHead head;
    juce::MidiBuffer midi;
    long frame = 0;
    explicit Run(int block) { processor.setPlayHead(&head); processor.prepareToPlay(48000.0, block); }
    template <typename Source>
    juce::AudioBuffer<float> block(int count, Source&& source, bool hostBypass = false) {
        juce::AudioBuffer<float> audio(2, count);
        for (int i = 0; i < count; ++i) for (int ch = 0; ch < 2; ++ch) audio.setSample(ch, i, source(frame + i));
        if (hostBypass) processor.processBlockBypassed(audio, midi); else processor.processBlock(audio, midi);
        frame += count;
        return audio;
    }
};

// Fix 1.
inline void checkHostBypass()
{
    {
        Run run(1024);
        const int latency = run.processor.getLatencySamples();
        auto impulse = run.block(1024, [](long frame) { return frame == 100 ? 1.0f : 0.0f; }, true);
        int at = -1;
        for (int i = 0; i < 1024; ++i) if (impulse.getSample(0, i) > 0.5f) at = i;
        require(latency == 730 && at == 100 + latency, "host bypass is latency-compensated: an impulse at 100 leaves at 830");
    }
    // Dry/bypassed signal passes through without a hole when the host engages
    // and releases its bypass.
    Run run(512);
    run.processor.setHostParameter("bypass", 1.0f);
    const int latency = run.processor.getLatencySamples();
    for (int block = 0; block < 60; ++block) {
        const long start = run.frame;
        auto audio = run.block(512, lifted, block >= 20 && block < 40);
        run.head.ppq += 512.0 * run.head.bpm / (60.0 * 48000.0);
        for (int i = 0; i < 512; ++i) {
            const long source = start + i - latency;
            require(audio.getSample(0, i) == (source >= 0 ? lifted(source) : 0.0f),
                    "engaging and releasing host bypass keeps the delayed signal continuous");
        }
    }
    // Releasing host bypass is not a seek: the scratch's history is intact.
    Run scratch(512);
    useHeldDelay(scratch.processor);
    scratch.processor.setKeepSub(false);
    for (int block = 0; block < 200; ++block) {
        const bool bypassed = block >= 100 && block < 120;
        scratch.block(512, tone, bypassed);
        scratch.head.ppq += 512.0 * scratch.head.bpm / (60.0 * 48000.0);
        if (block == 121) {
            const auto view = scratch.processor.displayState();
            require(view.historyReady && view.scratchActive, "leaving host bypass keeps the stroke's history readable");
        }
    }
    // Since 0.11.3 the host's bypass is the plug-in's own Bypass, at its 0.10
    // place in an unchanged parameter list, and it keeps the same guarantees.
    Run own(512);
    auto* bypassParameter = own.processor.parameters().getParameter("bypass");
    require(own.processor.getBypassParameter() == bypassParameter && bypassParameter->getParameterIndex() == 5
                && own.processor.getParameters().size() == 33,
            "the host's bypass is the Bypass parameter, with IDs, order and count unchanged");
    useHeldDelay(own.processor);
    own.processor.setKeepSub(false);
    for (int block = 0; block < 200; ++block) {
        if (block == 100 || block == 120) bypassParameter->setValueNotifyingHost(block == 100 ? 1.0f : 0.0f);
        own.block(512, tone);
        own.head.ppq += 512.0 * own.head.bpm / (60.0 * 48000.0);
        if (block == 121) {
            const auto view = own.processor.displayState();
            require(view.historyReady && view.scratchActive, "leaving Bypass keeps the stroke's history readable");
        }
    }
}

// Fixes 2 and 3: tempo ramps are not seeks; genuine seeks and unreported
// wraps never leave a hole in the delayed signal.
inline int silentDuringRamp(int blockSize, double bpmPerSecond)
{
    Run run(blockSize);
    run.processor.setHostParameter("bypass", 1.0f);
    int silent = 0;
    double bpm = 140.0;
    for (int block = 0; block < 400; ++block) {
        run.head.bpm = bpm;
        auto audio = run.block(blockSize, lifted);
        if (block > 20) for (int i = 0; i < blockSize; ++i) if (std::abs(audio.getSample(0, i)) < 1.0e-7f) ++silent;
        // The host integrates its tempo ramp within the callback.
        const double seconds = blockSize / 48000.0, start = bpm;
        if (block >= 100 && block < 300) bpm = std::max(40.0, bpm - bpmPerSecond * seconds);
        run.head.ppq += seconds * (start + bpm) * 0.5 / 60.0;
    }
    return silent;
}

inline void checkSeeksAndWraps(std::vector<std::pair<int, int>>& rampResults)
{
    for (const int blockSize : {512, 1024})
        for (const double ramp : {30.0, 60.0, 120.0}) {
            const int silent = silentDuringRamp(blockSize, ramp);
            rampResults.emplace_back(blockSize, silent);
            require(silent == 0, "tempo ramps reported inside a callback are not mistaken for seeks");
        }

    // Unreported one-bar wraps and a mid-play seek: the delayed signal stays
    // exactly continuous.
    Run run(512);
    run.processor.setHostParameter("bypass", 1.0f);
    const int latency = run.processor.getLatencySamples();
    const double step = 140.0 / 60.0 / 48000.0;
    for (int block = 0; block < 300; ++block) {
        const long start = run.frame;
        auto audio = run.block(512, lifted);
        for (int i = 0; i < 512; ++i) {
            const long source = start + i - latency;
            require(audio.getSample(0, i) == (source >= 0 ? lifted(source) : 0.0f),
                    "unreported loop wraps and seeks never open a 730-sample hole");
        }
        run.head.ppq += 512 * step;
        if (run.head.ppq >= 4.0) run.head.ppq -= 4.0;
        if (block == 150) run.head.ppq += 2.75;
    }

    // A genuine seek during a ramp is still detected: the held one-beat stroke
    // never replays audio from before the jump.
    Run seek(512);
    useHeldDelay(seek.processor);
    seek.processor.setKeepSub(false);
    double bpm = 140.0;
    for (int block = 0; block < 260; ++block) {
        const bool after = block >= 200;
        seek.head.bpm = bpm;
        auto audio = seek.block(512, [after](long frame) { return after ? 0.0f : tone(frame); });
        if (block >= 204) for (int i = 0; i < 512; ++i)
            require(audio.getSample(0, i) == 0.0f, "a seek during a tempo ramp still starts a new history");
        const double seconds = 512 / 48000.0, startBpm = bpm;
        if (block >= 150) bpm -= 30.0 * seconds;
        seek.head.ppq += seconds * (startBpm + bpm) * 0.5 / 60.0;
        if (block == 199) seek.head.ppq += 1.0;
    }
}

// Fix 4 at processor level, through sample-accurate automation.
inline double checkBypassAutomationStep()
{
    Run run(512);
    useHeldDelay(run.processor);
    run.processor.setKeepSub(false);
    auto* bypassParameter = run.processor.parameters().getParameter("bypass");
    double normal = 0.0, engage = 0.0;
    float previous = 0.0f;
    for (int block = 0; block < 160; ++block) {
        if (block == 150) {
            backScratchBridge::current.push(bypassParameter, 100, 1.0f);
            bypassParameter->setValueNotifyingHost(1.0f);
        }
        auto audio = run.block(512, tone);
        run.head.ppq += 512.0 * run.head.bpm / (60.0 * 48000.0);
        for (int i = 0; i < 512; ++i) {
            const double change = std::abs(audio.getSample(0, i) - previous);
            previous = audio.getSample(0, i);
            if (block >= 120 && block < 150) normal = std::max(normal, change);
            if (block >= 150 && block < 154) engage = std::max(engage, change);
        }
    }
    require(normal > 0.01 && engage <= normal * 1.5, "automating Bypass on fades instead of clicking");
    return engage / normal;
}

// Fix 7.
struct Gestures final : juce::AudioProcessorParameter::Listener {
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool starting) override { starting ? ++begins : ++ends; }
    int begins = 0, ends = 0;
};

inline void checkUnchangedWrites()
{
    BackScratchProcessor processor;
    processor.prepareToPlay(48000.0, 512);
    auto* bypassParameter = processor.parameters().getParameter("bypass");
    auto* shiftParameter = processor.parameters().getParameter("patternShift");
    Gestures bypassGestures, shiftGestures;
    bypassParameter->addListener(&bypassGestures);
    shiftParameter->addListener(&shiftGestures);
    processor.selectFactoryPreset(3);
    processor.randomizePattern();
    require(bypassGestures.begins == 0 && shiftGestures.begins == 0,
            "preset choice and Randomize leave untouched Bypass and Grid Shift automation alone");
    processor.setHostParameter("bypass", 1.0f);
    processor.shiftPattern(2);
    processor.selectFactoryPreset(4);
    require(bypassGestures.begins == 1 && bypassGestures.ends == 1 && shiftGestures.begins == 2 && shiftGestures.ends == 2,
            "a value that really changes still sends one balanced host gesture");
    bypassParameter->removeListener(&bypassGestures);
    shiftParameter->removeListener(&shiftGestures);
}

// Fix 8: display reads stay bounded while audio publishes concurrently.
inline double checkDisplayReads()
{
    Run run(64);
    std::atomic<bool> stop {false};
    std::atomic<int> reads {0};
    double slowestMs = 0.0;
    std::thread reader([&] {
        while (!stop.load()) {
            const auto begin = juce::Time::getMillisecondCounterHiRes();
            const auto view = run.processor.displayState();
            juce::ignoreUnused(view);
            slowestMs = std::max(slowestMs, juce::Time::getMillisecondCounterHiRes() - begin);
            ++reads;
        }
    });
    for (int block = 0; block < 3000; ++block) {
        run.block(64, tone);
        run.head.ppq += 64.0 * run.head.bpm / (60.0 * 48000.0);
    }
    while (reads.load() < 50) std::this_thread::yield();
    stop.store(true);
    reader.join();
    require(slowestMs < 250.0, "display reads never spin while audio publishes");
    return slowestMs;
}

// Fix 9: each instance receives only its own automation points.
inline void checkQueuesPerInstance()
{
    BackScratchProcessor a, b;
    a.prepareToPlay(48000.0, 512); b.prepareToPlay(48000.0, 512);
    auto* shiftA = a.parameters().getParameter("patternShift");
    auto* legacy = a.parameters().getParameter("position");
    backScratchBridge::current.clear();
    backScratchBridge::current.push(shiftA, 7, 0.5f);
    backScratchBridge::current.push(legacy, 3, 0.5f);
    require(a.automationQueue().size == 1 && b.automationQueue().size == 0,
            "automation points reach only the owning instance's queue, and only for its controls");
    juce::AudioBuffer<float> audio(2, 64); audio.clear(); juce::MidiBuffer midi;
    a.processBlock(audio, midi);
    require(a.automationQueue().size == 0, "each callback consumes its own points");
}

// Fix 13.
inline void checkParameterGroups()
{
    Run grouped(512), reference(512);
    const auto process = [&](bool compare) {
        auto x = grouped.block(512, tone);
        auto y = reference.block(512, tone);
        grouped.head.ppq += 512.0 * 140.0 / (60.0 * 48000.0);
        reference.head.ppq = grouped.head.ppq;
        double worst = 0.0;
        for (int i = 0; i < 512; ++i) worst = std::max(worst, static_cast<double>(std::abs(x.getSample(0, i) - y.getSample(0, i))));
        if (compare) require(worst == 0.0, "grouped writes switch completely or not at all");
    };
    for (int block = 0; block < 40; ++block) process(true);
    const auto write = [](BackScratchProcessor& processor) {
        processor.setHostParameter("factory", 12.0f);
        processor.setHostParameter("variation", 321.0f);
        processor.setHostParameter("patternShift", 5.0f);
    };
    {
        const BackScratchProcessor::ParameterGroup group(grouped.processor);
        write(grouped.processor);
        for (int block = 0; block < 4; ++block) process(true); // still the old sound
    }
    write(reference.processor);
    for (int block = 0; block < 40; ++block) process(true);    // switched together
}

// Fix 1 through the built VST3: the host's bypass keeps the reported latency.
// Since 0.11.3 it is the plug-in's own Bypass (flagged kIsBypass), not a
// second "Bypass" added by the JUCE wrapper, so the two are one switch.
inline void checkPackagedHostBypass(juce::AudioPluginInstance& plugin)
{
    auto* hostBypass = plugin.getBypassParameter();
    require(hostBypass != nullptr, "the packaged VST3 exposes a host bypass parameter");
    int named = 0;
    for (auto* parameter : plugin.getParameters()) named += parameter->getName(128) == "Bypass" ? 1 : 0;
    require(named == 1 && hostBypass->getName(128) == "Bypass" && hostBypass->getParameterIndex() == 5,
            "the host's bypass is the plug-in's own Bypass, at its 0.10 position, with no second wrapper Bypass");
    RampHead head;
    plugin.setPlayHead(&head);
    plugin.prepareToPlay(48000.0, 1024);
    hostBypass->setValueNotifyingHost(1.0f);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> audio(2, 1024);
    int at = -1;
    for (int block = 0; block < 3 && at < 0; ++block) {
        audio.clear();
        if (block == 2) audio.setSample(0, 100, 1.0f), audio.setSample(1, 100, 1.0f);
        plugin.processBlock(audio, midi);
        if (block == 2) {
            // The impulse may straddle a callback; look in the next one too.
            for (int i = 0; i < 1024; ++i) if (audio.getSample(0, i) > 0.5f) at = i;
            if (at < 0) { audio.clear(); plugin.processBlock(audio, midi); for (int i = 0; i < 1024; ++i) if (audio.getSample(0, i) > 0.5f) at = 1024 + i; }
        }
    }
    require(at == 100 + plugin.getLatencySamples(), "packaged VST3 host bypass keeps the impulse at the reported latency");
    hostBypass->setValueNotifyingHost(0.0f);
    plugin.releaseResources();
    plugin.setPlayHead(nullptr);
    std::cout << "Packaged VST3 host bypass: impulse delayed by " << at - 100 << " samples (reported latency "
              << plugin.getLatencySamples() << ")\n";
}

} // namespace backScratch::transportContinuityTests

inline void runTransportContinuityTests()
{
    using namespace backScratch::transportContinuityTests;
    checkHostBypass();
    std::vector<std::pair<int, int>> ramps;
    checkSeeksAndWraps(ramps);
    const double bypassStep = checkBypassAutomationStep();
    checkUnchangedWrites();
    const double slowestRead = checkDisplayReads();
    checkQueuesPerInstance();
    checkParameterGroups();
    std::cout << "BACK-SCRATCH transport continuity: host bypass 830-sample impulse, ramp silent samples";
    for (const auto& [block, silent] : ramps) std::cout << ' ' << block << ':' << silent;
    std::cout << ", gapless wraps/seeks, bypass-on step " << bypassStep << "x normal, no-op writes silent, slowest display read "
              << slowestRead << " ms, per-instance queues, atomic parameter groups passed\n";
}
