#pragma once

#include "AutomationBridge.h"
#include "BackScratchProcessor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace backScratch::playbackAlignmentTests {

inline void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Head final : juce::AudioPlayHead {
    double beat = 0.0, bpm = 120.0;
    int numerator = 4, denominator = 4;
    bool playing = true;
    bool looping = false;
    double loopStart = 0.0, loopEnd = 4.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setPpqPosition(beat); info.setBpm(bpm); info.setIsPlaying(playing);
        info.setTimeSignature(TimeSignature {numerator, denominator});
        info.setIsLooping(looping);
        if (looping) info.setLoopPoints(LoopPoints {loopStart, loopEnd});
        return info;
    }
};

struct Controls {
    int duration = 0, timing = 0, shift = 0, factory = 0, motion = 2;
    bool generated = false, keepSub = true, bypass = false;
    std::uint32_t seed = 1;
};

struct Context {
    Controls controls;
    double beat = 0.0, bpm = 120.0, barBeats = 4.0;
};

inline double duration(const Context& context)
{
    const auto& controls = context.controls;
    const double base = controls.duration < 3
        ? std::array<double, 3> {0.5, 1.0, 2.0}[static_cast<size_t>(controls.duration)]
        : std::array<double, 5> {1.0, 2.0, 4.0, 8.0, 16.0}[static_cast<size_t>(controls.duration - 3)] * context.barBeats;
    return base * std::array<double, 3> {1.0, 1.5, 2.0 / 3.0}[static_cast<size_t>(controls.timing)];
}

inline ScratchPattern pattern(const Context& context)
{
    const auto& controls = context.controls;
    // The source composition remains four quarter-note beats in 4/4 even
    // when tempo, playback length, timing modifier, or host meter changes.
    auto result = fitPatternToPhrase(controls.generated
        ? generatePattern(controls.seed, 4.0, 4.0, controls.motion)
        : makePattern(controls.factory, 0, controls.motion), duration(context));
    result.gridOffset = controls.shift;
    return result;
}

inline float source(int frame, double rate, int channel)
{
    const double t = frame / rate;
    return static_cast<float>(0.24 * std::sin(t * juce::MathConstants<double>::twoPi * 49.0)
        + 0.13 * std::sin(t * juce::MathConstants<double>::twoPi * (channel ? 667.0 : 441.0))
        + 0.04 * std::sin(t * juce::MathConstants<double>::twoPi * 1913.0));
}

inline void comparePattern(const ScratchPattern& expected, const ScratchPattern& actual)
{
    require(expected.count == actual.count && std::abs(expected.beats - actual.beats) < 1e-9
            && expected.gridOffset == actual.gridOffset,
            "displayed pattern has the audible duration and shift");
    for (int index = 0; index < expected.count; ++index) {
        const auto& a = expected.segments[static_cast<size_t>(index)];
        const auto& b = actual.segments[static_cast<size_t>(index)];
        require(std::abs(a.start - b.start) < 1e-9 && std::abs(a.end - b.end) < 1e-9
                && std::abs(a.delayStart - b.delayStart) < 1e-9 && std::abs(a.delayEnd - b.delayEnd) < 1e-9
                && a.gateStart == b.gateStart && a.gateEnd == b.gateEnd
                && a.wet == b.wet && a.curved == b.curved
                && std::abs(a.curveStart - b.curveStart) < 1e-9 && std::abs(a.curveEnd - b.curveEnd) < 1e-9,
                "displayed trajectory and gates belong to the currently rendered pattern");
    }
}

inline void compareDisplay(const BackScratchProcessor& processor, const Context& context)
{
    const auto view = processor.displayState();
    const auto expected = pattern(context);
    const double turns = context.beat / expected.beats;
    const double phase = turns - std::floor(turns);
    const auto phaseDistance = [phase](float value) {
        const double distance = std::abs(value - phase);
        return std::min(distance, std::abs(1.0 - distance));
    };
    require(view.phase >= 0.0f && view.phase < 1.0f && processor.phase() >= 0.0f && processor.phase() < 1.0f,
            "display playhead wraps to zero rather than rounding up to an out-of-range end position");
    require(phaseDistance(view.phase) < 1e-6 && phaseDistance(processor.phase()) < 1e-6,
            "display playhead follows the last audible sample, including the reported plugin latency");
    require(view.playing && std::abs(view.barBeats - context.barBeats) < 1e-12
            && std::abs(view.tempoBpm - context.bpm) < 1e-9,
            "display tempo and meter come from the same audible context as its playhead");
    require(view.durationChoice == context.controls.duration && view.timingChoice == context.controls.timing
            && view.generated == context.controls.generated && view.factoryIndex == context.controls.factory
            && view.seed == (context.controls.generated ? context.controls.seed : 0),
            "display identity does not anticipate queued automation");
    comparePattern(expected, view.pattern);
    const auto active = evaluatePattern(expected, context.beat);
    require(std::abs(view.activeSample.delayBeats - active.delayBeats) < 1e-9
            && std::abs(view.activeSample.playbackRate - active.playbackRate) < 1e-8
            && std::abs(view.activeSample.gate - active.gate) < 1e-6f
            && view.activeSample.wet == active.wet && view.activeSample.segment == active.segment,
            "display's active point comes from the exact audible double-precision beat and gate");
}

struct Change { int frame; const char* id; float value; };

inline void checkHistoryStatus()
{
    LiveScratchEngine engine;
    engine.prepare(8000.0, 1, 1.0);
    require(!engine.isHistoryReady() && !engine.isScratchActive(), "new history starts unavailable and inactive");
    PatternSample stroke;
    stroke.wet = 1.0f;
    const int firstReadable = engine.latencySamples() + LiveScratchEngine::interpolationGuardSamples - 1;
    for (int frame = 0; frame <= firstReadable; ++frame) {
        engine.process(0.2f, 0.2f, stroke, 4000.0, false, false);
        require(engine.isScratchActive() && engine.isHistoryReady() == (frame == firstReadable),
                "history-ready indication accounts for both reported latency and sinc lookbehind");
    }
    stroke.delayBeats = 1000.0;
    engine.process(0.2f, 0.2f, stroke, 4000.0, false, false);
    require(!engine.isHistoryReady() && engine.isScratchActive(),
            "unavailable old audio reports a requested scratch using live fallback");
    stroke.delayBeats = 0.0;
    engine.process(0.2f, 0.2f, stroke, 4000.0, false, true);
    require(engine.isHistoryReady() && !engine.isScratchActive(), "bypass does not announce an active scratch");
    stroke.wet = 0.0f;
    engine.process(0.2f, 0.2f, stroke, 4000.0, false, false);
    require(engine.isHistoryReady() && !engine.isScratchActive(), "ordinary dry segments do not announce a missing stroke");
    engine.reset();
    require(!engine.isHistoryReady() && !engine.isScratchActive(), "seek reset clears the history status");
}

inline void apply(Controls& controls, const Change& change)
{
    const juce::String id(change.id);
    const int value = juce::roundToInt(change.value);
    if (id == "motionVersion") controls.motion = value;
    else if (id == "phraseDuration") controls.duration = value;
    else if (id == "phraseTiming") controls.timing = value;
    else if (id == "patternShift") controls.shift = value;
    else if (id == "factory") controls.factory = value;
    else if (id == "generated") controls.generated = value != 0;
    else if (id == "generationSeed") controls.seed = static_cast<std::uint32_t>(value);
    else if (id == "keepSub") controls.keepSub = value != 0;
    else if (id == "bypass") controls.bypass = value != 0;
}

// The reference clock is a precomputed per-sample timeline, independent of the
// processor's ring buffer, event queues, and display publication. The same
// reference engine consumes it once; all host buffer partitions must match.
inline void checkTimeline(double rate, int blockSize)
{
    BackScratchProcessor processor;
    Head head;
    processor.setPhraseDurationChoice(0);
    processor.setPlayHead(&head); processor.prepareToPlay(rate, blockSize);
    LiveScratchEngine reference;
    reference.prepare(rate, 2, 2.0);
    const int latency = reference.latencySamples();
    require(processor.getLatencySamples() == latency, "display/audio test uses the reported latency");
    const int tempoChange = 5 * latency + 33;
    const int secondTempoChange = 7 * latency + 77;
    const int frames = 18 * latency + 3000;
    const std::array<Change, 16> changes {{{latency / 4, "keepSub", 0},
        {latency / 2, "phraseTiming", 1}, {latency - 3, "patternShift", 3},
        {latency + 19, "phraseDuration", 1}, {2 * latency + 7, "factory", 12},
        {3 * latency + 13, "phraseTiming", 2}, {3 * latency + 13, "generationSeed", 83177},
        {3 * latency + 13, "generated", 1}, {4 * latency + 11, "patternShift", 15},
        {6 * latency + 21, "phraseDuration", 3}, {8 * latency + 5, "patternShift", 0},
        {10 * latency + 5, "bypass", 1}, {11 * latency + 5, "bypass", 0},
        {latency / 3, "motionVersion", 1}, {2 * latency + 41, "motionVersion", 2},
        {9 * latency + 31, "motionVersion", 1}}};
    std::vector<Context> timeline(static_cast<size_t>(frames));
    std::vector<std::array<float, 2>> expected(static_cast<size_t>(frames));
    std::vector<bool> expectedHistory(static_cast<size_t>(frames)), expectedScratch(static_cast<size_t>(frames));
    Controls controls;
    double beat = -0.03125;
    for (int frame = 0; frame < frames; ++frame) {
        for (const auto& change : changes) if (change.frame == frame) apply(controls, change);
        const double bpm = frame < tempoChange ? 120.0 : frame < secondTempoChange ? 173.0 : 89.0;
        const double bar = frame < tempoChange ? 4.0 : 3.0;
        timeline[static_cast<size_t>(frame)] = {controls, beat, bpm, bar};
        beat += bpm / (60.0 * rate);
    }
    const auto audibleContext = [&timeline, latency, rate](int frame) {
        if (frame >= latency) return timeline[static_cast<size_t>(frame - latency)];
        auto context = timeline.front();
        context.beat += (frame - latency) * context.bpm / (60.0 * rate);
        return context;
    };
    auto rendered = pattern(audibleContext(0));
    Context previous = audibleContext(0);
    for (int frame = 0; frame < frames; ++frame) {
        const auto context = audibleContext(frame);
        if (context.controls.duration != previous.controls.duration || context.controls.timing != previous.controls.timing
            || context.controls.shift != previous.controls.shift || context.controls.factory != previous.controls.factory
            || context.controls.generated != previous.controls.generated || context.controls.seed != previous.controls.seed
            || context.controls.motion != previous.controls.motion
            || context.barBeats != previous.barBeats) rendered = pattern(context);
        previous = context;
        expected[static_cast<size_t>(frame)] = reference.process(source(frame, rate, 0), source(frame, rate, 1),
            evaluatePattern(rendered, context.beat), 60.0 * rate / context.bpm,
            context.controls.keepSub, context.controls.bypass);
        expectedHistory[static_cast<size_t>(frame)] = reference.isHistoryReady();
        expectedScratch[static_cast<size_t>(frame)] = reference.isScratchActive();
    }
    juce::MidiBuffer midi;
    double largestDifference = 0.0;
    for (int start = 0; start < frames;) {
        int count = std::min(blockSize, frames - start);
        // A host announces tempo/meter changes at a processing boundary.
        for (const int boundary : {tempoChange, secondTempoChange})
            if (start < boundary) count = std::min(count, boundary - start);
        const auto& current = timeline[static_cast<size_t>(start)];
        head.beat = current.beat; head.bpm = current.bpm;
        head.numerator = static_cast<int>(current.barBeats);
        backScratchBridge::current.clear();
        for (const auto& change : changes) {
            if (change.frame < start || change.frame >= start + count) continue;
            auto* parameter = processor.parameters().getParameter(change.id);
            require(parameter != nullptr, "timing and shift controls are exposed to the host");
            const float normalized = parameter->convertTo0to1(change.value);
            backScratchBridge::current.push(parameter, change.frame - start, normalized);
            parameter->setValueNotifyingHost(normalized);
        }
        juce::AudioBuffer<float> audio(2, count);
        for (int sample = 0; sample < count; ++sample)
            for (int channel = 0; channel < 2; ++channel)
                audio.setSample(channel, sample, source(start + sample, rate, channel));
        processor.processBlock(audio, midi);
        for (int sample = 0; sample < count; ++sample) {
            for (int channel = 0; channel < 2; ++channel) {
                const auto actual = audio.getSample(channel, sample);
                require(std::isfinite(actual), "timing changes keep both output channels finite");
                largestDifference = std::max(largestDifference,
                    std::abs(static_cast<double>(actual - expected[static_cast<size_t>(start + sample)][static_cast<size_t>(channel)])));
            }
        }
        compareDisplay(processor, audibleContext(start + count - 1));
        const auto status = processor.displayState();
        require(status.historyReady == expectedHistory[static_cast<size_t>(start + count - 1)]
                && status.scratchActive == expectedScratch[static_cast<size_t>(start + count - 1)],
                "display reports actual audible history fallback rather than promising an unavailable scratch");
        start += count;
    }
    if (largestDifference > 0.00003)
        std::cerr << "Playback/display oracle rate=" << rate << " block=" << blockSize
                  << " maximum audio difference=" << largestDifference << '\n';
    require(largestDifference < 0.00003,
            "rendered audio follows the same latency-delayed timeline shown on the display");

    // GUI-side setters may run between audio callbacks. The graph must retain
    // the sound that was actually rendered until the new sound reaches output.
    const auto before = processor.displayState();
    processor.setHostParameter("generationSeed", 92831);
    processor.setHostParameter("phraseTiming", 1);
    processor.setHostParameter("patternShift", 11);
    const auto after = processor.displayState();
    comparePattern(before.pattern, after.pattern);
    require(before.phase == after.phase && before.seed == after.seed && before.timingChoice == after.timingChoice,
            "unprocessed GUI changes cannot put the display ahead of playback");

    // A seek begins a new history, but keeps the graph and audible musical
    // clock together even at a negative/fractional destination.
    head.beat = -3.125; head.bpm = 101.0; head.numerator = 4;
    juce::AudioBuffer<float> afterSeek(2, 37); afterSeek.clear();
    processor.processBlock(afterSeek, midi);
    Context seek;
    seek.controls = timeline.back().controls;
    seek.controls.seed = 92831; seek.controls.timing = 1; seek.controls.shift = 11;
    seek.beat = head.beat + (36 - latency) * head.bpm / (60.0 * rate);
    seek.bpm = head.bpm; seek.barBeats = 4.0;
    compareDisplay(processor, seek);
    head.playing = false;
    processor.processBlock(afterSeek, midi);
    processor.setHostParameter("phraseTiming", 2);
    processor.setHostParameter("patternShift", 4);
    const auto stopped = processor.displayState();
    require(!stopped.playing && stopped.timingChoice == 2 && stopped.pattern.gridOffset == 4,
            "stopped display previews timing and shift controls without waiting for audio");
}

inline void checkHostLoop(int blockSize, bool startBeforeLoop = false)
{
    constexpr double rate = 48000.0, samplesPerBeat = 24000.0;
    const int frames = startBeforeLoop ? 5000 : 72000;
    Head head;
    head.looping = true;
    head.loopStart = startBeforeLoop ? 1.0 : 2.125;
    head.loopEnd = startBeforeLoop ? 1.003 : 2.875;
    const double initialBeat = startBeforeLoop ? 0.999 : 2.375;
    const auto hostBeat = [&head, initialBeat](int frame) {
        const double unwrapped = initialBeat + frame / samplesPerBeat;
        if (unwrapped < head.loopEnd) return unwrapped;
        return head.loopStart + std::fmod(unwrapped - head.loopStart, head.loopEnd - head.loopStart);
    };
    Controls controls;
    controls.factory = 8; controls.timing = 2; controls.shift = 5; controls.keepSub = false;
    BackScratchProcessor processor;
    processor.setPhraseDurationChoice(controls.duration);
    processor.setHostParameter("factory", static_cast<float>(controls.factory));
    processor.setHostParameter("phraseTiming", static_cast<float>(controls.timing));
    processor.setHostParameter("patternShift", static_cast<float>(controls.shift));
    processor.setKeepSub(false);
    processor.setPlayHead(&head); processor.prepareToPlay(rate, blockSize);
    LiveScratchEngine reference;
    reference.prepare(rate, 2, 2.0);
    const int latency = reference.latencySamples();
    Context context {controls, initialBeat, 120.0, 4.0};
    const auto active = pattern(context);
    juce::MidiBuffer midi;
    double largestDifference = 0.0;
    int crossedInsideBlocks = 0;
    int worstFrame = -1;
    for (int start = 0; start < frames; start += blockSize) {
        const int count = std::min(blockSize, frames - start);
        head.beat = hostBeat(start);
        if (head.beat + (count - 1) / samplesPerBeat >= head.loopEnd) ++crossedInsideBlocks;
        juce::AudioBuffer<float> audio(2, count);
        for (int sample = 0; sample < count; ++sample)
            for (int channel = 0; channel < 2; ++channel)
                audio.setSample(channel, sample, source(start + sample, rate, channel));
        processor.processBlock(audio, midi);
        for (int sample = 0; sample < count; ++sample) {
            const int frame = start + sample;
            const double audibleBeat = hostBeat(frame - latency);
            // Global and callback-local PPQ arithmetic can land on opposite
            // floating-point sides of the same exact musical knot. Preserve
            // this strict audio oracle: choosing a different gate/readhead for
            // even one sample is an actual audible timing regression.
            const auto expected = reference.process(source(frame, rate, 0), source(frame, rate, 1),
                evaluatePattern(active, audibleBeat), samplesPerBeat, false, false);
            for (int channel = 0; channel < 2; ++channel) {
                const double difference = std::abs(static_cast<double>(audio.getSample(channel, sample) - expected[static_cast<size_t>(channel)]));
                if (difference > largestDifference) { largestDifference = difference; worstFrame = frame; }
            }
        }
        context.beat = hostBeat(start + count - 1 - latency);
        compareDisplay(processor, context);
        const auto status = processor.displayState();
        require(status.historyReady == reference.isHistoryReady() && status.scratchActive == reference.isScratchActive(),
                "looped playback retains the same readable history as a continuous engine");
    }
    require(crossedInsideBlocks >= 2, "host-loop test covers buffers that cross loop boundaries");
    if (largestDifference > 0.00003)
        std::cerr << "Host loop block=" << blockSize << " maximum difference=" << largestDifference
                  << " worstFrame=" << worstFrame << '\n';
    require(largestDifference < 0.00003,
            "host loops preserve audio history and wrap the audible clock inside a callback");
    if (startBeforeLoop) return;

    // Loop mode must not hide a genuine edit-cursor jump elsewhere in the
    // region. This destination is deliberately different from the next beat.
    head.beat = 2.21;
    reference.reset();
    juce::AudioBuffer<float> seek(2, 1024);
    for (int sample = 0; sample < seek.getNumSamples(); ++sample)
        for (int channel = 0; channel < 2; ++channel)
            seek.setSample(channel, sample, source(sample, rate, channel));
    processor.processBlock(seek, midi);
    for (int sample = 0; sample < seek.getNumSamples(); ++sample) {
        const double beat = head.beat + (sample - latency) / samplesPerBeat;
        const auto expected = reference.process(source(sample, rate, 0), source(sample, rate, 1),
            evaluatePattern(active, beat), samplesPerBeat, false, false);
        for (int channel = 0; channel < 2; ++channel)
            require(std::abs(seek.getSample(channel, sample) - expected[static_cast<size_t>(channel)]) < 0.00003,
                    "an actual seek while loop mode is enabled still clears stale audio");
    }
    context.beat = head.beat + (seek.getNumSamples() - 1 - latency) / samplesPerBeat;
    compareDisplay(processor, context);
}

inline void checkLoopBoundaryRounding()
{
    constexpr double rate = 48000.0, samplesPerBeat = 24000.0;
    Head head;
    head.looping = true; head.loopStart = 1.0; head.loopEnd = 2.0;
    // A host's timeline may round a boundary to loopStart while our double
    // calculation falls a few ulps short of loopEnd. That is the same time.
    head.beat = head.loopEnd - 1024.0 / samplesPerBeat - 1e-14;
    BackScratchProcessor processor;
    processor.setHostParameter("bypass", 1.0f);
    processor.setPlayHead(&head); processor.prepareToPlay(rate, 1024);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> audio(2, 1024);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            audio.setSample(channel, sample, channel == 0 ? 0.125f : -0.25f);
    processor.processBlock(audio, midi);
    require(audio.getSample(0, 1023) == 0.125f, "loop-rounding test primes latency and dry history");
    head.beat = head.loopStart;
    juce::AudioBuffer<float> next(2, 64);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < next.getNumSamples(); ++sample)
            next.setSample(channel, sample, channel == 0 ? 0.125f : -0.25f);
    processor.processBlock(next, midi);
    for (int sample = 0; sample < next.getNumSamples(); ++sample)
        require(next.getSample(0, sample) == 0.125f && next.getSample(1, sample) == -0.25f,
                "numeric rounding at a host loop boundary must not reset audio history");
}

} // namespace backScratch::playbackAlignmentTests

inline void runPlaybackAlignmentTests()
{
    backScratch::playbackAlignmentTests::checkHistoryStatus();
    for (const double rate : {8000.0, 44100.0, 48000.0, 96000.0})
        for (const int blockSize : {64, 257, 1024})
            backScratch::playbackAlignmentTests::checkTimeline(rate, blockSize);
    for (const int blockSize : {257, 1024})
        backScratch::playbackAlignmentTests::checkHostLoop(blockSize);
    backScratch::playbackAlignmentTests::checkHostLoop(257, true);
    backScratch::playbackAlignmentTests::checkLoopBoundaryRounding();
    std::cout << "BACK-SCRATCH audible display, shifted/modulated automation, tempo and seek alignment tests passed\n";
}
