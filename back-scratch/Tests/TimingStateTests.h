#pragma once

#include "BackScratchProcessor.h"
#include "PatternTests.h"
#include "ProcessorTests.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace backScratch::timingStateTests {

inline void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct TemporaryPreset {
    juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("BACK-SCRATCH-timing-state", ".backscratch");
    ~TemporaryPreset() { file.deleteFile(); }
};

inline void sameRetimedPattern(const ScratchPattern& original, const ScratchPattern& retimed)
{
    require(original.count == retimed.count && original.gridOffset == retimed.gridOffset,
            "timing modifiers preserve every gesture and the chosen grid offset");
    const double ratio = retimed.beats / original.beats;
    for (int index = 0; index < original.count; ++index) {
        const auto& a = original.segments[static_cast<size_t>(index)];
        const auto& b = retimed.segments[static_cast<size_t>(index)];
        require(std::abs(a.start * ratio - b.start) < 1e-8
                    && std::abs(a.end * ratio - b.end) < 1e-8
                    && std::abs(a.delayStart * ratio - b.delayStart) < 1e-8
                    && std::abs(a.delayEnd * ratio - b.delayEnd) < 1e-8
                    && a.gateStart == b.gateStart && a.gateEnd == b.gateEnd
                    && a.wet == b.wet && a.curved == b.curved,
                "straight, dotted and triplet timing scale one existing curve without recomposition");
    }
    for (int probe = 0; probe < 257; ++probe) {
        const double fraction = (probe + 0.371) / 257.0;
        auto expected = evaluatePattern(original, original.beats * fraction);
        expected.delayBeats *= ratio;
        patternTests::checkSameSample(evaluatePattern(retimed, retimed.beats * fraction), expected,
                                     "timing modifiers preserve playback rate, gate and wet at every normalized position");
    }
}

inline void checkTimingIdentity()
{
    BackScratchProcessor processor;
    processor.setPhraseLocked(true);
    constexpr std::array<double, 8> baseBeats {0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0};
    constexpr std::array<double, 3> multipliers {1.0, 1.5, 2.0 / 3.0};
    for (int entry = 0; entry < factoryPresetCount + 16; ++entry) {
        processor.setPhraseDurationChoice(3);
        processor.setPhraseTimingChoice(0);
        if (entry < factoryPresetCount) processor.selectFactoryPreset(entry);
        else {
            processor.setHostParameter("generated", 1.0f);
            processor.setHostParameter("generationSeed", static_cast<float>(entry * 991));
            processor.setHostParameter("sourceBeats", static_cast<float>(baseBeats[static_cast<size_t>(entry % 8)]));
            processor.setHostParameter("sourceBarBeats", 4.0f);
        }
        processor.shiftPattern(9 - processor.patternShiftSteps());
        const auto original = processor.currentPattern();
        const auto seed = processor.generationSeed();
        const float originalBeats = processor.parameters().getRawParameterValue("sourceBeats")->load();
        const float originalMeter = processor.parameters().getRawParameterValue("sourceBarBeats")->load();
        for (int choice = 0; choice < BackScratchProcessor::phraseDurationCount; ++choice) {
            for (int timing = 0; timing < 3; ++timing) {
                processor.setPhraseDurationChoice(choice);
                processor.setPhraseTimingChoice(timing);
                const auto actual = processor.currentPattern();
                const double expected = baseBeats[static_cast<size_t>(choice)] * multipliers[static_cast<size_t>(timing)];
                require(std::abs(actual.beats - expected) < 1e-10
                            && std::abs(processor.phraseBeats() - expected) < 1e-10,
                        "all eight base lengths support exact straight, dotted and triplet durations");
                require(processor.phraseTimingChoice() == timing && processor.patternShiftSteps() == 9
                            && processor.generationSeed() == seed
                            && processor.parameters().getRawParameterValue("sourceBeats")->load() == originalBeats
                            && processor.parameters().getRawParameterValue("sourceBarBeats")->load() == originalMeter,
                        "length and timing changes do not replace the original composition or grid offset");
                sameRetimedPattern(original, actual);
            }
        }
    }
    processor.setPhraseTimingChoice(0);
    const auto unmodifiedLabel = processor.phraseDurationLabel();
    processor.setPhraseTimingChoice(1);
    const auto dottedLabel = processor.phraseDurationLabel();
    processor.setPhraseTimingChoice(2);
    require(unmodifiedLabel != dottedLabel && dottedLabel != processor.phraseDurationLabel()
                && processor.phraseDurationLabel() != unmodifiedLabel,
            "duration readout distinguishes all three timing modes");
    processor.shiftPattern(-1);
    require(processor.patternShiftSteps() == 8, "left shift moves one cell toward an earlier position");
    processor.shiftPattern(16);
    require(processor.patternShiftSteps() == 8, "one full grid rotation leaves the pattern unchanged");
    processor.shiftPattern(-9);
    require(processor.patternShiftSteps() == 15, "left shift wraps from the first cell to the last");
    processor.shiftPattern(1);
    require(processor.patternShiftSteps() == 0, "right shift wraps from the last cell to the first");
}

inline void checkFactoryShiftIsolation()
{
    constexpr std::array<double, 8> baseBeats {0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0};
    const auto checkOriginal = [](BackScratchProcessor& processor, int index) {
        const auto actual = processor.currentPattern();
        const auto original = fitPatternToPhrase(makePattern(index), processor.phraseBeats());
        require(processor.patternShiftSteps() == 0 && actual.gridOffset == 0,
                "choosing a factory preset clears the previous pattern's grid shift");
        require(!processor.isGenerated() && !processor.hasCustomPattern() && processor.variationSeed() == 0,
                "factory recall restores its original recipe instead of the previous edited or generated pattern");
        require(patternTests::fingerprint(actual) == patternTests::fingerprint(original)
                    && patternTests::fingerprint(processor.displayState().pattern) == patternTests::fingerprint(original),
                "factory recall restores the complete unshifted playback curve and matching display");
    };
    BackScratchProcessor processor;
    for (const bool locked : {false, true}) {
        for (int timing = 0; timing < 3; ++timing) {
            for (int index = 0; index < factoryPresetCount; ++index) {
                const int chosenDuration = index % BackScratchProcessor::phraseDurationCount;
                const bool keepSub = index % 2 == 0;
                processor.setPhraseLocked(locked);
                processor.setPhraseDurationChoice(chosenDuration);
                processor.setPhraseTimingChoice(timing);
                processor.setKeepSub(keepSub);
                processor.shiftPattern(1 + index % 15 - processor.patternShiftSteps());
                processor.selectFactoryPreset(index);
                checkOriginal(processor, index);
                int expectedDuration = chosenDuration;
                if (!locked) {
                    const double nativeBeats = makePattern(index).beats;
                    expectedDuration = static_cast<int>(std::min_element(baseBeats.begin(), baseBeats.end(),
                        [nativeBeats](double a, double b) { return std::abs(a - nativeBeats) < std::abs(b - nativeBeats); }) - baseBeats.begin());
                }
                require(processor.phraseDurationChoice() == expectedDuration && processor.phraseLocked() == locked
                            && processor.phraseTimingChoice() == timing && processor.keepSub() == keepSub,
                        "clearing a factory shift preserves length-lock behavior, dotted/triplet mode and sub preference");
                processor.shiftPattern(-1);
                processor.selectFactoryPreset(index);
                checkOriginal(processor, index);
                processor.shiftPattern(7);
                processor.setCurrentProgram(index);
                checkOriginal(processor, index);
            }
        }
    }

    TemporaryPreset savedPreset;
    for (const bool edited : {false, true}) {
        processor.setPhraseLocked(true);
        processor.setPhraseDurationChoice(3);
        processor.randomizePattern();
        processor.shiftPattern(11 - processor.patternShiftSteps());
        if (edited) {
            require(processor.beginPatternEdit()
                        && processor.previewPatternEdit(fitPatternToPhrase(makePattern(12), processor.phraseBeats()))
                        && processor.commitPatternEdit(),
                    "factory shift test starts from a committed custom contour");
        }
        const auto shifted = processor.currentPattern();
        require(processor.savePreset(savedPreset.file).wasOk(), "shifted generated and edited presets save");
        juce::MemoryBlock project;
        processor.getStateInformation(project);
        processor.selectFactoryPreset(54);
        checkOriginal(processor, 54);
        require(processor.loadPreset(savedPreset.file).wasOk() && processor.patternShiftSteps() == 11
                    && patternTests::fingerprint(processor.currentPattern()) == patternTests::fingerprint(shifted),
                "user preset recall preserves its intentionally saved grid offset after factory navigation");
        processor.setCurrentProgram(19);
        checkOriginal(processor, 19);
        processor.setStateInformation(project.getData(), static_cast<int>(project.getSize()));
        require(processor.patternShiftSteps() == 11
                    && patternTests::fingerprint(processor.currentPattern()) == patternTests::fingerprint(shifted),
                "project recall preserves intentional grid offsets after factory navigation");
        processor.selectFactoryPreset(5);
        checkOriginal(processor, 5);
    }

    for (const int index : {5, 54}) {
        BackScratchProcessor changed, fresh;
        changed.selectFactoryPreset(12);
        changed.shiftPattern(9);
        changed.selectFactoryPreset(index);
        fresh.selectFactoryPreset(index);
        juce::MemoryBlock changedState, freshState;
        changed.getStateInformation(changedState);
        fresh.getStateInformation(freshState);
        require(processorTests::render(257, &changedState) == processorTests::render(257, &freshState),
                "factory playback after shifting another preset exactly matches a fresh unshifted instance");
    }
    std::cout << "BACK-SCRATCH all 75 factory shifts isolated across direct recall, reselect and host programs; saved offsets and audio retained\n";
}

inline void checkTimingRecall()
{
    TemporaryPreset preset;
    for (const bool generated : {false, true}) {
        for (int timing = 0; timing < 3; ++timing) {
            BackScratchProcessor source;
            source.selectFactoryPreset(54);
            source.setHostParameter("generated", generated ? 1.0f : 0.0f);
            source.setHostParameter("generationSeed", 83177);
            source.setHostParameter("sourceBeats", 16);
            source.setHostParameter("sourceBarBeats", 4);
            source.setPhraseDurationChoice(0);
            source.setPhraseTimingChoice(timing);
            source.shiftPattern(timing + 5);
            source.setPhraseLocked(false);
            source.setKeepSub(false);
            const auto expected = source.currentPattern();
            require(source.savePreset(preset.file).wasOk(), "timing and shifted phrase preset saves");
            const auto xml = juce::XmlDocument::parse(preset.file);
            require(xml != nullptr && xml->getIntAttribute("version") == 9
                        && xml->getIntAttribute("phraseTiming", -1) == timing
                        && xml->getIntAttribute("patternShift", -1) == timing + 5,
                    "version seven preset explicitly stores timing mode and pattern grid shift");
            BackScratchProcessor restoredPreset;
            restoredPreset.setPhraseTimingChoice((timing + 1) % 3);
            restoredPreset.shiftPattern(12);
            require(restoredPreset.loadPreset(preset.file).wasOk()
                        && restoredPreset.phraseTimingChoice() == timing
                        && restoredPreset.patternShiftSteps() == timing + 5
                        && restoredPreset.phraseDurationChoice() == 0
                        && restoredPreset.isGenerated() == generated
                        && !restoredPreset.phraseLocked() && !restoredPreset.keepSub(),
                    "preset recall replaces existing timing and shift settings exactly");
            sameRetimedPattern(expected, restoredPreset.currentPattern());
            require(patternTests::fingerprint(expected) == patternTests::fingerprint(restoredPreset.currentPattern()),
                    "preset recall reproduces the complete shifted audible curve");
            juce::MemoryBlock state;
            source.getStateInformation(state);
            const auto stateXml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
            require(stateXml != nullptr && stateXml->getIntAttribute("engineVersion") == 9,
                    "project state declares the new timing-compatible schema");
            BackScratchProcessor restoredProject;
            restoredProject.setPhraseTimingChoice((timing + 1) % 3);
            restoredProject.shiftPattern(13);
            restoredProject.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            require(restoredProject.phraseTimingChoice() == timing
                        && restoredProject.patternShiftSteps() == timing + 5
                        && restoredProject.currentPresetName() == source.currentPresetName()
                        && patternTests::fingerprint(expected) == patternTests::fingerprint(restoredProject.currentPattern()),
                    "project recall restores timing, shift, saved name and exact audible pattern");
        }
    }
}

inline void checkLegacyTimingDefaults()
{
    TemporaryPreset preset;
    BackScratchProcessor source;
    source.selectFactoryPreset(5);
    source.setPhraseBars(4);
    source.setPhraseTimingChoice(2);
    source.shiftPattern(11);
    require(source.savePreset(preset.file).wasOk(), "legacy timing test fixture saves");
    const auto modernPreset = juce::XmlDocument::parse(preset.file);
    require(modernPreset != nullptr, "modern preset fixture parses");
    juce::MemoryBlock modernState;
    source.getStateInformation(modernState);
    const auto modernProject = juce::AudioProcessor::getXmlFromBinary(modernState.getData(), static_cast<int>(modernState.getSize()));
    require(modernProject != nullptr, "modern project fixture parses");
    BackScratchProcessor target;
    for (const int version : {4, 5, 6}) {
        for (const bool staleNewFields : {false, true}) {
            juce::XmlElement oldPreset(*modernPreset);
            oldPreset.setAttribute("version", version);
            oldPreset.setAttribute("generatorVersion", 1);
            oldPreset.setAttribute("phraseBars", 4);
            if (!staleNewFields) {
                oldPreset.removeAttribute("phraseTiming");
                oldPreset.removeAttribute("patternShift");
            }
            require(preset.file.replaceWithText(oldPreset.toString()), "legacy preset fixture writes");
            target.setPhraseTimingChoice(1);
            target.shiftPattern(13 - target.patternShiftSteps());
            require(target.loadPreset(preset.file).wasOk()
                        && target.phraseTimingChoice() == 0 && target.patternShiftSteps() == 0,
                    "version four through six presets always restore straight unshifted timing on a previously modified instance");
            juce::XmlElement oldProject(*modernProject);
            oldProject.setAttribute("engineVersion", version);
            oldProject.setAttribute("generatorVersion", 1);
            if (!staleNewFields) {
                for (auto* child = oldProject.getFirstChildElement(); child != nullptr;) {
                    auto* next = child->getNextElement();
                    const auto id = child->getStringAttribute("id");
                    if (id == "phraseTiming" || id == "patternShift") oldProject.removeChildElement(child, true);
                    child = next;
                }
            }
            juce::MemoryBlock oldState;
            juce::AudioProcessor::copyXmlToBinary(oldProject, oldState);
            target.setPhraseTimingChoice(2);
            target.shiftPattern(14 - target.patternShiftSteps());
            target.setStateInformation(oldState.getData(), static_cast<int>(oldState.getSize()));
            require(target.phraseTimingChoice() == 0 && target.patternShiftSteps() == 0,
                    "old project schemas clear timing and shift rather than inheriting stale or previously active values");
        }
    }
    juce::MemoryBlock capturedState;
    juce::MemoryOutputStream stream(capturedState, false);
    stream.writeInt(0x42534331);
    stream.writeInt(2);
    target.setPhraseTimingChoice(1);
    target.shiftPattern(7);
    target.setStateInformation(capturedState.getData(), static_cast<int>(capturedState.getSize()));
    require(target.phraseTimingChoice() == 0 && target.patternShiftSteps() == 0
                && target.parameters().getRawParameterValue("bypass")->load() == 1.0f,
            "captured-wheel project migration also clears modern timing settings while opening dry");
}

inline void checkInvalidTimingFiles()
{
    TemporaryPreset preset;
    BackScratchProcessor processor;
    processor.selectFactoryPreset(19);
    processor.setPhraseTimingChoice(2);
    processor.shiftPattern(9);
    require(processor.savePreset(preset.file).wasOk(), "invalid timing test fixture saves");
    const auto validPreset = juce::XmlDocument::parse(preset.file);
    require(validPreset != nullptr, "valid timing test fixture parses");
    juce::MemoryBlock unchanged;
    processor.getStateInformation(unchanged);
    const auto validProject = juce::AudioProcessor::getXmlFromBinary(unchanged.getData(), static_cast<int>(unchanged.getSize()));
    require(validProject != nullptr, "valid timing project parses");
    const struct InvalidValue { const char* id; const char* value; } cases[] {
        {"phraseTiming", nullptr}, {"phraseTiming", "-1"}, {"phraseTiming", "3"},
        {"phraseTiming", "1.5"}, {"phraseTiming", "garbage"}, {"phraseTiming", "2oops"},
        {"patternShift", nullptr}, {"patternShift", "-1"}, {"patternShift", "16"},
        {"patternShift", "5.25"}, {"patternShift", "garbage"}, {"patternShift", "8oops"}
    };
    for (const auto& entry : cases) {
        juce::XmlElement invalidPreset(*validPreset);
        if (entry.value == nullptr) invalidPreset.removeAttribute(entry.id);
        else invalidPreset.setAttribute(entry.id, entry.value);
        // Any premature parameter writes before rejecting a malformed new
        // field would change these unrelated settings and the named identity.
        invalidPreset.setAttribute("factory", 0);
        invalidPreset.setAttribute("keepSub", 0);
        require(preset.file.replaceWithText(invalidPreset.toString()), "invalid preset fixture writes");
        require(processor.loadPreset(preset.file).failed(),
                "version seven rejects missing, out-of-range, fractional and malformed timing fields");
        juce::MemoryBlock after;
        processor.getStateInformation(after);
        require(after == unchanged, "invalid preset rejection leaves every parameter and saved identity unchanged");
        juce::XmlElement invalidProject(*validProject);
        for (auto* child : invalidProject.getChildIterator()) {
            if (child->getStringAttribute("id") != entry.id) continue;
            if (entry.value == nullptr) child->removeAttribute("value");
            else child->setAttribute("value", entry.value);
        }
        juce::MemoryBlock invalidState;
        juce::AudioProcessor::copyXmlToBinary(invalidProject, invalidState);
        processor.setStateInformation(invalidState.getData(), static_cast<int>(invalidState.getSize()));
        processor.getStateInformation(after);
        require(after == unchanged, "invalid modern project timing fields are rejected without partial state mutation");
    }
}

struct MeterHead final : juce::AudioPlayHead {
    int numerator = 4, denominator = 4;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo position;
        position.setIsPlaying(true);
        position.setBpm(120.0);
        position.setPpqPosition(0.0);
        position.setTimeSignature(TimeSignature {numerator, denominator});
        return position;
    }
};

inline void checkRandomizedTimingAndExtremeMeters()
{
    BackScratchProcessor processor;
    processor.setPhraseLocked(false);
    processor.setPhraseTimingChoice(2);
    processor.shiftPattern(11);
    constexpr std::array<double, 8> bases {0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0};
    unsigned observedChoices = 0;
    for (int count = 0; count < 64; ++count) {
        processor.randomizePattern();
        const int choice = processor.phraseDurationChoice();
        observedChoices |= 1u << choice;
        const double base = bases[static_cast<size_t>(choice)];
        require(processor.phraseTimingChoice() == 2 && processor.patternShiftSteps() == 11
                    && !processor.phraseLocked()
                    && std::abs(processor.phraseBeats() - base * (2.0 / 3.0)) < 1e-10
                    && processor.parameters().getRawParameterValue("sourceBeats")->load() == static_cast<float>(base),
                "unlocked randomization changes only the base length while retaining timing, shift and an unmodified composition length");
    }
    require((observedChoices & (observedChoices - 1u)) != 0,
            "unlocked randomization can select multiple base phrase lengths");
    MeterHead meter;
    processor.setPlayHead(&meter);
    processor.prepareToPlay(48000.0, 64);
    processor.setPhraseLocked(true);
    juce::AudioBuffer<float> audio(2, 64);
    juce::MidiBuffer midi;
    const struct Extreme { int numerator, denominator, choice, timing; double base, result; } extremes[] {
        {1, 16, 3, 2, 0.25, 1.0 / 6.0},
        {16, 4, 7, 1, 256.0, 384.0}
    };
    for (const auto& entry : extremes) {
        meter.numerator = entry.numerator;
        meter.denominator = entry.denominator;
        audio.clear();
        processor.processBlock(audio, midi);
        processor.setPhraseDurationChoice(entry.choice);
        processor.setPhraseTimingChoice(entry.timing);
        processor.randomizePattern();
        const auto modified = processor.currentPattern();
        require(std::abs(modified.beats - entry.result) < 1e-10
                    && std::abs(processor.phraseBeats() - entry.result) < 1e-10
                    && processor.parameters().getRawParameterValue("sourceBeats")->load() == static_cast<float>(entry.base),
                "extreme dotted and triplet host-meter durations retain a bounded unmodified source composition");
        processor.setPhraseTimingChoice(0);
        const auto original = processor.currentPattern();
        require(original.beats == entry.base, "turning timing off returns to the exact original host-meter length");
        sameRetimedPattern(original, modified);
    }
}

} // namespace backScratch::timingStateTests

inline void runTimingStateTests()
{
    using namespace backScratch::timingStateTests;
    checkFactoryShiftIsolation();
    checkTimingIdentity();
    checkTimingRecall();
    checkLegacyTimingDefaults();
    checkInvalidTimingFiles();
    checkRandomizedTimingAndExtremeMeters();
    std::cout << "BACK-SCRATCH dotted/triplet shape identity, shifted preset and project recall, schema migration and timing validation passed\n";
}
