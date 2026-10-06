#pragma once
#include "EditedProcessorTests.h"
#include "TimingStateTests.h"
#include "PresetBank.h"
#include <map>
#include <string>

namespace backScratch::motionVersionTests {
inline void compare(const ScratchPattern& expected, const ScratchPattern& actual)
{
    editedProcessorTests::compare(expected, actual);
    for (int i = 0; i < 1021; ++i) {
        const double phase = (i + 0.271) * expected.beats / 1021.0;
        const auto a = evaluatePattern(expected, phase), b = evaluatePattern(actual, phase);
        patternTests::checkSameSample(a, b, "motion-version recall retains the exact delay, rate, gate and wet trajectory");
    }
}
inline juce::MemoryBlock stateOf(BackScratchProcessor& processor)
{
    juce::MemoryBlock result; processor.getStateInformation(result); return result;
}
inline void restoreXml(BackScratchProcessor& processor, const juce::XmlElement& xml)
{
    juce::MemoryBlock data; juce::AudioProcessor::copyXmlToBinary(xml, data);
    processor.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
}
inline void checkRecallAndMigration()
{
    using playbackAlignmentTests::require;
    timingStateTests::TemporaryPreset preset;
    for (const int motion : {1, 2}) for (const bool generated : {false, true}) {
        BackScratchProcessor source, target;
        source.selectFactoryPreset(13);
        source.setHostParameter("generated", generated ? 1 : 0);
        source.setHostParameter("generationSeed", 83177);
        source.setHostParameter("motionVersion", static_cast<float>(motion));
        source.setPhraseBars(2); source.setPhraseTimingChoice(2); source.shiftPattern(5);
        const auto expected = source.currentPattern();
        require(source.savePreset(preset.file).wasOk(), "both motion versions save as schema9 presets");
        require(target.loadPreset(preset.file).wasOk(), "both motion versions restore from schema9 presets");
        compare(expected, target.currentPattern());
        const auto saved = stateOf(source);
        target.setHostParameter("motionVersion", static_cast<float>(3 - motion));
        target.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        compare(expected, target.currentPattern());
        require(target.currentPresetName() == source.currentPresetName(), "motion-version preset name remains correct on recall");
        target.randomizePattern();
        require(target.parameters().getRawParameterValue("motionVersion")->load() == 2,
                "Randomize after legacy recall creates a smooth current-version pattern");
        target.setHostParameter("motionVersion", 1); target.selectFactoryPreset(4);
        require(target.parameters().getRawParameterValue("motionVersion")->load() == 2,
                "explicit factory selection creates a smooth current-version pattern");
    }
    BackScratchProcessor source, target;
    source.selectFactoryPreset(5); source.setHostParameter("motionVersion", 1);
    source.setPhraseBars(4); source.setPhraseTimingChoice(2); source.shiftPattern(11);
    require(source.savePreset(preset.file).wasOk(), "legacy motion fixture saves");
    const auto presetXml = juce::XmlDocument::parse(preset.file);
    const auto state = stateOf(source);
    const auto stateXml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    require(presetXml != nullptr && stateXml != nullptr, "legacy motion fixtures parse");
    for (int version = 4; version <= 8; ++version) {
        juce::XmlElement oldPreset(*presetXml), oldProject(*stateXml);
        oldPreset.setAttribute("version", version); oldPreset.setAttribute("phraseBars", 4);
        oldProject.setAttribute("engineVersion", version);
        for (auto* parameter : oldProject.getChildIterator()) {
            const auto id = parameter->getStringAttribute("id");
            if (id == "motionVersion") parameter->setAttribute("value", 2.0); // future/stale field must not override migration
            if (id == "phraseLength") parameter->setAttribute("value", 2.0);
        }
        require(preset.file.replaceWithText(oldPreset.toString()), "legacy motion fixture writes");
        target.setHostParameter("motionVersion", 2);
        require(target.loadPreset(preset.file).wasOk(), "schemas4–8 presets still load");
        auto expected = fitPatternToPhrase(makePattern(5, 0, 1), target.phraseBeats());
        expected.gridOffset = version < 7 ? 0 : 11;
        compare(expected, target.currentPattern());
        target.setHostParameter("motionVersion", 2); restoreXml(target, oldProject);
        expected = fitPatternToPhrase(makePattern(5, 0, 1), target.phraseBeats());
        expected.gridOffset = version < 7 ? 0 : 11;
        compare(expected, target.currentPattern());
    }
    // Hand drawings remain exact across motion-version changes; they are not
    // silently transformed into the new preset recipe or re-randomized.
    require(target.beginPatternEdit() && target.previewPatternEdit(editedProcessorTests::contour(2))
            && target.commitPatternEdit(), "motion fixture draws a custom contour");
    const auto custom = target.currentPattern();
    target.setHostParameter("motionVersion", 2); compare(custom, target.currentPattern());
    target.setHostParameter("motionVersion", 1); compare(custom, target.currentPattern());
    require(target.savePreset(preset.file).wasOk(), "custom motion fixture saves");
    const auto validPreset = juce::XmlDocument::parse(preset.file);
    const auto before = stateOf(target);
    const auto validProject = juce::AudioProcessor::getXmlFromBinary(before.getData(), static_cast<int>(before.getSize()));
    for (const auto* bad : {"", "0", "3", "2.5", "NaN", "1extra"}) {
        juce::XmlElement badPreset(*validPreset), badProject(*validProject);
        badPreset.setAttribute("generatorVersion", bad); badProject.setAttribute("generatorVersion", bad);
        require(preset.file.replaceWithText(badPreset.toString()) && target.loadPreset(preset.file).failed(),
                "invalid motion version rejects a preset before mutation");
        require(stateOf(target) == before && target.canUndoPatternEdit(), "rejected motion preset retains state and undo");
        restoreXml(target, badProject);
        require(stateOf(target) == before && target.canUndoPatternEdit(), "invalid project motion retains state and undo");
    }
    for (const auto* bad : {"", "0", "2", "NaN", "1.5"}) {
        juce::XmlElement badProject(*validProject);
        for (auto* parameter : badProject.getChildIterator())
            if (parameter->getStringAttribute("id") == "motionVersion") parameter->setAttribute("value", bad);
        restoreXml(target, badProject);
        require(stateOf(target) == before, "missing, invalid or conflicting motion parameter rejects project atomically");
    }
}
inline void exportFactoryBank(const juce::File& directory)
{
    using playbackAlignmentTests::require;
    require(directory.createDirectory().wasOk(), "factory preset directory created");
    // The whole factory bank as portable files, one folder per bank folder
    // (Classic/Bass and so on become nested folders).
    const auto& bank = bankPresets();
    std::map<std::string, int> numbers;
    for (int i = 0; i < static_cast<int>(bank.size()); ++i) {
        const auto& entry = bank[static_cast<size_t>(i)];
        BackScratchProcessor processor, restored;
        // Unlocked, so the original recipes keep their native length; the
        // file then records the lock on, as the 0.10 export did.
        processor.setPhraseLocked(false);
        processor.selectBankPreset(i);
        processor.setPhraseLocked(true);
        juce::File folder = directory;
        for (const auto& part : juce::StringArray::fromTokens(juce::String(entry.folder), "/", {}))
            folder = folder.getChildFile(juce::File::createLegalFileName(part));
        const auto file = folder.getChildFile(juce::File::createLegalFileName(juce::String(++numbers[entry.folder]).paddedLeft('0', 2)
            + " - " + juce::String(entry.name).replace(" / ", " and ") + ".backscratch"));
        const auto saved = processor.savePreset(file);
        require(saved.wasOk(), "factory bank preset exports");
        const auto loaded = restored.loadPreset(file);
        if (loaded.failed()) std::cerr << entry.folder << " / " << entry.name << ": " << loaded.getErrorMessage() << '\n';
        require(loaded.wasOk(), "factory bank preset reopens");
        compare(processor.currentPattern(), restored.currentPattern());
        require(processor.presetXml("x")->isEquivalentTo(restored.presetXml("x").get(), false),
                "a reopened factory bank file restores every saved setting and slot");
    }
    std::cout << "Exported and recalled " << bank.size() << " schema 10 factory bank presets in " << bankFolders().size() << " folders\n";
}
}
inline void runMotionVersionTests()
{
    backScratch::motionVersionTests::checkRecallAndMigration();
    std::cout << "BACK-SCRATCH smooth/legacy recipe recall, schema4–8 migration, custom geometry preservation and invalid version rejection passed\n";
}
