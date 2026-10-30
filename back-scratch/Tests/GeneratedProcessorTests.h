#pragma once

#include "BackScratchProcessor.h"
#include "AutomationBridge.h"
#include "PatternTests.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace backScratch::generatedProcessorTests {
inline void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

struct MeterHead final : juce::AudioPlayHead {
    int numerator = 4, denominator = 4;
    double beat = 0.0;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo position;
        position.setIsPlaying(true); position.setBpm(120.0); position.setPpqPosition(beat);
        position.setTimeSignature(TimeSignature {numerator, denominator});
        return position;
    }
};

inline float source(int frame, int channel = 0) {
    const double t = frame / 48000.0;
    return static_cast<float>(0.3 * std::sin(t * juce::MathConstants<double>::twoPi * 49.0)
        + 0.15 * std::sin(t * juce::MathConstants<double>::twoPi * (channel ? 667.0 : 441.0)));
}

inline void same(const std::vector<float>& a, const std::vector<float>& b, const char* message) {
    require(a.size() == b.size(), message);
    double worst = 0.0;
    for (size_t index = 0; index < a.size(); ++index) {
        require(std::isfinite(a[index]) && std::isfinite(b[index]), "generated automation output remains finite");
        worst = std::max(worst, static_cast<double>(std::abs(a[index] - b[index])));
    }
    if (worst > 0.00003) { std::cerr << message << ": " << worst << '\n'; throw std::runtime_error(message); }
}

inline std::vector<float> timedGeneratedRender(int blockSize) {
    MeterHead head;
    BackScratchProcessor processor;
    processor.setPlayHead(&head); processor.prepareToPlay(48000.0, blockSize);
    struct Change { int frame; const char* id; float value; };
    const std::array<Change, 12> changes {{{127, "generationSeed", 32491}, {127, "generated", 1},
        {3001, "keepSub", 0}, {17777, "generationSeed", 84277}, {17777, "phraseDuration", 4},
        {27003, "phraseDuration", 0}, {27003, "generationSeed", 93281}, {32001, "keepSub", 1},
        {39005, "generated", 0}, {39005, "factory", 19}, {39005, "phraseDuration", 3},
        {45001, "bypass", 1}}};
    constexpr int frames = 48000;
    std::vector<float> output(static_cast<size_t>(frames));
    juce::MidiBuffer midi;
    for (int start = 0; start < frames; start += blockSize) {
        const int count = std::min(blockSize, frames - start);
        backScratchBridge::current.clear();
        for (const auto& change : changes) {
            if (change.frame < start || change.frame >= start + count) continue;
            auto* parameter = processor.parameters().getParameter(change.id);
            const float normalized = parameter->convertTo0to1(change.value);
            // At block size one the scalar setter is the independent reference.
            // Larger buffers deliver all points in the host queue while raw
            // APVTS values already contain the last point for that block.
            if (blockSize != 1) backScratchBridge::current.push(parameter, change.frame - start, normalized);
            parameter->setValueNotifyingHost(normalized);
        }
        juce::AudioBuffer<float> audio(2, count);
        for (int sample = 0; sample < count; ++sample)
            for (int channel = 0; channel < 2; ++channel) audio.setSample(channel, sample, source(start + sample, channel));
        head.beat = start / 24000.0;
        processor.processBlock(audio, midi);
        for (int sample = 0; sample < count; ++sample) output[static_cast<size_t>(start + sample)] = audio.getSample(0, sample);
    }
    return output;
}

// Compare startup against an independently clocked engine. A change inside the
// lookahead must not prime audio controls before that change's audible time.
inline void checkStartupControlLatency() {
    MeterHead head;
    BackScratchProcessor processor;
    processor.setPlayHead(&head); processor.prepareToPlay(48000.0, 257);
    LiveScratchEngine reference;
    reference.prepare(48000.0, 2);
    const int latency = reference.latencySamples();
    require(processor.getLatencySamples() == latency && latency > 0,
            "processor reports the complete audio latency");
    const int changeFrame = latency / 4;
    const auto pattern = fitPatternToPhrase(makePattern(0), 4.0);
    constexpr int frames = 6000;
    std::vector<float> actual(frames), expected(frames);
    juce::MidiBuffer midi;
    for (int start = 0; start < frames; start += 257) {
        const int count = std::min(257, frames - start);
        backScratchBridge::current.clear();
        if (changeFrame >= start && changeFrame < start + count) {
            auto* parameter = processor.parameters().getParameter("keepSub");
            backScratchBridge::current.push(parameter, changeFrame - start, 0.0f);
            parameter->setValueNotifyingHost(0.0f);
        }
        juce::AudioBuffer<float> audio(2, count);
        for (int sample = 0; sample < count; ++sample)
            for (int channel = 0; channel < 2; ++channel) audio.setSample(channel, sample, source(start + sample, channel));
        head.beat = 0.97 + start / 24000.0;
        processor.processBlock(audio, midi);
        for (int sample = 0; sample < count; ++sample) actual[static_cast<size_t>(start + sample)] = audio.getSample(0, sample);
    }
    for (int sample = 0; sample < frames; ++sample) {
        const int contextFrame = sample - latency;
        const double beat = 0.97 + contextFrame / 24000.0;
        expected[static_cast<size_t>(sample)] = reference.process(source(sample), source(sample, 1),
            evaluatePattern(pattern, beat), 24000.0, contextFrame < changeFrame, false)[0];
    }
    same(actual, expected, "latency startup must not apply future Keep Sub automation");
}

// Duration changes must be a dilation of ONE existing graph. Checking normalized
// geometry and rates catches both motif repetition and seeded recomposition.
inline void checkDurationIdentity() {
    BackScratchProcessor p;
    const auto compare = [](const ScratchPattern& a, const ScratchPattern& b) {
        require(a.count == b.count, "duration never adds or drops scratch gestures");
        const double ratio = b.beats / a.beats;
        for (int i = 0; i < a.count; ++i) {
            const auto& x = a.segments[static_cast<size_t>(i)];
            const auto& y = b.segments[static_cast<size_t>(i)];
            require(std::abs(y.start - x.start * ratio) < 1e-8
                 && std::abs(y.end - x.end * ratio) < 1e-8
                 && std::abs(y.delayStart - x.delayStart * ratio) < 1e-8
                 && std::abs(y.delayEnd - x.delayEnd * ratio) < 1e-8
                 && x.gateStart == y.gateStart && x.gateEnd == y.gateEnd
                 && x.wet == y.wet && x.curved == y.curved,
                 "length knob scales the same contour, cuts and wet values");
        }
    };
    for (int preset = 0; preset < factoryPresetCount + 20; ++preset) {
        p.setPhraseBars(4);
        if (preset < factoryPresetCount) p.selectFactoryPreset(preset);
        else p.randomizePattern();
        const auto original = p.currentPattern();
        const auto originalSeed = p.generationSeed();
        for (int choice = 0; choice < BackScratchProcessor::phraseDurationCount; ++choice) {
            p.setPhraseDurationChoice(choice);
            const auto scaled = p.currentPattern();
            compare(original, scaled);
            require(p.generationSeed() == originalSeed, "length changes never replace the seed");
            const double expected = std::array<double, 8>{0.5, 1, 2, 4, 8, 16, 32, 64}[static_cast<size_t>(choice)];
            require(scaled.beats == expected && p.phraseBeats() == expected, "all eight knob lengths are exact");
        }
    }
    p.setPhraseBars(8); p.randomizePattern();
    const auto original = p.currentPattern();
    p.setPhraseDurationChoice(0);
    const auto halfBeat = p.currentPattern();
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("BACK-SCRATCH-short-recall", ".backscratch");
    require(p.savePreset(file).wasOk(), "half-beat preset saves");
    p.randomizePattern(); p.setPhraseBars(16);
    p.setPhraseLocked(false); // a locked length would keep its 16 bars
    require(p.loadPreset(file).wasOk() && p.phraseDurationChoice() == 0, "half-beat preset duration restores");
    compare(halfBeat, p.currentPattern());
    p.setPhraseBars(8); compare(original, p.currentPattern());
    juce::MemoryBlock saved; p.getStateInformation(saved);
    BackScratchProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    restored.setPhraseDurationChoice(0); compare(halfBeat, restored.currentPattern());
    file.replaceWithText("<BACK_SCRATCH_PRESET version=\"5\" generatorVersion=\"1\" name=\"Older generated phrase\" factory=\"0\" variation=\"0\" generated=\"1\" generationSeed=\"83177\" phraseBars=\"4\" phraseLocked=\"1\" keepSub=\"1\"/>");
    p.setPhraseLocked(false); // a locked phrase keeps its own length and timing on preset loads
    require(p.loadPreset(file).wasOk() && p.phraseDurationChoice() == 5, "version 5 preset migrates original duration");
    const auto previous = generatePattern(83177, 16.0, 4.0, 1);
    compare(previous, p.currentPattern());
    p.setPhraseDurationChoice(0); compare(previous, p.currentPattern());
    // Legacy state/preset restoration can precede the first host position.
    // Use a non-4/4 meter to catch freezing the default meter into its identity.
    file.replaceWithText(file.loadFileAsString().replace("83177", "6482"));
    BackScratchProcessor migrated;
    migrated.setPhraseLocked(false); // a locked phrase keeps its own length and timing on preset loads
    require(migrated.loadPreset(file).wasOk(), "legacy preset loads before host meter exists");
    MeterHead meter; meter.numerator = 6; meter.denominator = 4;
    migrated.setPlayHead(&meter); migrated.prepareToPlay(48000, 64);
    juce::MidiBuffer midi; juce::AudioBuffer<float> silence(2, 64); silence.clear();
    migrated.processBlock(silence, midi);
    const auto inSix = generatePattern(6482, 24.0, 6.0, 1);
    compare(inSix, migrated.currentPattern());
    migrated.setPhraseDurationChoice(0); compare(inSix, migrated.currentPattern());
    juce::MemoryBlock versionSix; migrated.getStateInformation(versionSix);
    auto oldXml = juce::AudioProcessor::getXmlFromBinary(versionSix.getData(), static_cast<int>(versionSix.getSize()));
    require(oldXml != nullptr, "state serializes to valid XML");
    oldXml->setAttribute("engineVersion", 5);
    for (auto* child : oldXml->getChildIterator())
        if (child->getStringAttribute("id") == "phraseLength") child->setAttribute("value", 2.0);
    juce::MemoryBlock oldState; juce::AudioProcessor::copyXmlToBinary(*oldXml, oldState);
    BackScratchProcessor oldProject;
    oldProject.setStateInformation(oldState.getData(), static_cast<int>(oldState.getSize()));
    oldProject.setPlayHead(&meter); oldProject.prepareToPlay(48000, 64);
    oldProject.processBlock(silence, midi);
    compare(inSix, oldProject.currentPattern());
    oldProject.setPhraseDurationChoice(0); compare(inSix, oldProject.currentPattern());
    file.deleteFile();
}
} // namespace backScratch::generatedProcessorTests

inline void runGeneratedProcessorTests() {
    using namespace backScratch;
    using namespace backScratch::generatedProcessorTests;
    BackScratchProcessor processor;
    processor.prepareToPlay(48000.0, 257);
    processor.selectFactoryPreset(11);
    processor.setPhraseBars(16); processor.setPhraseLocked(true); processor.setKeepSub(false);
    std::uint64_t previous = patternTests::structuralFingerprint(processor.currentPattern());
    for (int index = 0; index < 32; ++index) {
        const auto previousSeed = processor.generationSeed();
        processor.randomizePattern();
        const auto hash = patternTests::structuralFingerprint(processor.currentPattern());
        require(processor.isGenerated() && processor.generationSeed() != previousSeed && hash != previous,
                "each Randomize click composes a new exact phrase");
        require(processor.selectedFactoryPreset() == 11, "randomization does not select another factory preset");
        require(processor.phraseBars() == 16 && processor.phraseLocked() && !processor.keepSub(),
                "randomization preserves the phrase lock and Keep Sub choice");
        previous = hash;
    }
    processor.setPhraseBars(8); processor.setPhraseLocked(false); processor.setKeepSub(true);
    const auto seed = processor.generationSeed();
    const auto hash = patternTests::structuralFingerprint(processor.currentPattern());
    const auto preset = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("BACK-SCRATCH-generated-integration", ".backscratch");
    require(processor.savePreset(preset).wasOk(), "generated preset saves");
    processor.randomizePattern(); processor.setPhraseBars(1); processor.setPhraseLocked(true); processor.setKeepSub(false);
    require(processor.loadPreset(preset).wasOk(), "generated preset loads");
    // A locked phrase keeps its length and its lock through every preset load (user, 2026-10-28).
    require(processor.isGenerated() && processor.generationSeed() == seed && processor.phraseBars() == 1
                && processor.phraseLocked() && processor.keepSub(),
            "a locked one-bar phrase keeps its length and lock through a preset saved at 8 bars, unlocked");
    processor.setPhraseLocked(false);
    require(processor.loadPreset(preset).wasOk(), "generated preset loads unlocked");
    require(processor.isGenerated() && processor.generationSeed() == seed && processor.phraseBars() == 8
                && !processor.phraseLocked() && processor.keepSub()
                && patternTests::structuralFingerprint(processor.currentPattern()) == hash,
            "unlocked, the saved generated preset restores exact phrase, length and sub toggle");
    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    BackScratchProcessor restored;
    restored.prepareToPlay(48000.0, 64);
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    require(restored.isGenerated() && restored.generationSeed() == seed && restored.phraseBars() == 8
                && !restored.phraseLocked() && restored.keepSub()
                && patternTests::structuralFingerprint(restored.currentPattern()) == hash
                && restored.currentPresetName() == processor.currentPresetName(),
            "project recall restores the full generated preset identity");
    BackScratchProcessor lockedBank;
    lockedBank.setPhraseBars(2); lockedBank.setPhraseTimingChoice(1); lockedBank.setPhraseLocked(true);
    int lengthHeld = 0;
    const int bankCount = static_cast<int>(bankPresets().size());
    for (int index = 0; index < bankCount; ++index) {
        lockedBank.selectBankPreset(index);
        lengthHeld += lockedBank.phraseBars() == 2 && lockedBank.phraseTimingChoice() == 1 && lockedBank.phraseLocked() ? 1 : 0;
    }
    require(lengthHeld == bankCount, "a locked 2-bar triplet phrase holds through every bank preset");
    preset.deleteFile();

    MeterHead meter;
    processor.setPlayHead(&meter);
    const struct { int numerator, denominator, bars; double expected; } cases[] {
        {3, 4, 1, 3.0}, {6, 8, 2, 6.0}, {7, 8, 4, 14.0}, {5, 4, 8, 40.0}, {6, 4, 16, 96.0}};
    juce::MidiBuffer midi;
    for (const auto& entry : cases) {
        meter.numerator = entry.numerator; meter.denominator = entry.denominator;
        processor.setPhraseBars(entry.bars);
        juce::AudioBuffer<float> audio(2, 257); audio.clear();
        processor.processBlock(audio, midi);
        require(processor.currentPattern().beats == entry.expected,
                "phrase bars follow actual host meter rather than an assumed four beats");
        meter.beat += 257.0 / 24000.0;
    }
    const auto sampleBySample = timedGeneratedRender(1);
    same(sampleBySample, timedGeneratedRender(64), "new pattern and phrase automation respects 64-sample offsets");
    same(sampleBySample, timedGeneratedRender(257), "new pattern and phrase automation respects 257-sample offsets");
    same(sampleBySample, timedGeneratedRender(1024), "new pattern and phrase automation respects 1024-sample offsets");
    checkStartupControlLatency();
    checkDurationIdentity();
    std::cout << "BACK-SCRATCH single-pass duration scaling, short preset recall, meter and delayed automation integration passed\n";
}
