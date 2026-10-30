#pragma once

#include "BackScratchProcessor.h"
#include "PatternEditing.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace backScratch::editedSerializationTests {

inline void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct TemporaryPreset {
    juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("BACK-SCRATCH-edited-serialization", ".backscratch");
    ~TemporaryPreset() { file.deleteFile(); }
};

inline bool hasClippedCurve(const ScratchPattern& pattern)
{
    for (int i = 0; i < pattern.count; ++i) {
        const auto& s = pattern.segments[static_cast<size_t>(i)];
        if (s.curved && (s.curveStart > 1.0e-10 || s.curveEnd < 1.0 - 1.0e-10)) return true;
    }
    return false;
}

inline void compare(const ScratchPattern& expected, const ScratchPattern& actual)
{
    require(validateEditedPattern(actual) && expected.count == actual.count
                && expected.gridOffset == actual.gridOffset
                && std::abs(expected.beats - actual.beats) < 1.0e-10
                && std::abs(expected.editingRange - actual.editingRange) < 1.0e-10,
            "custom recall preserves segment count, phrase, shift and fixed graph height");
    for (int i = 0; i < expected.count; ++i) {
        const auto& a = expected.segments[static_cast<size_t>(i)];
        const auto& b = actual.segments[static_cast<size_t>(i)];
        require(std::abs(a.start - b.start) < 1.0e-10 && std::abs(a.end - b.end) < 1.0e-10
                    && std::abs(a.delayStart - b.delayStart) < 1.0e-10
                    && std::abs(a.delayEnd - b.delayEnd) < 1.0e-10
                    && std::abs(a.curveStart - b.curveStart) < 1.0e-12
                    && std::abs(a.curveEnd - b.curveEnd) < 1.0e-12
                    && a.gateStart == b.gateStart && a.gateEnd == b.gateEnd
                    && a.wet == b.wet && a.curved == b.curved,
                "custom XML preserves clipped polynomial domains and every source/gate endpoint");
    }
    for (int probe = 0; probe < 4096; ++probe) {
        const double beat = (probe + 0.371) * expected.beats / 4096.0;
        const auto a = evaluatePattern(expected, beat);
        const auto b = evaluatePattern(actual, beat);
        require(std::abs(a.delayBeats - b.delayBeats) < 1.0e-9
                    && std::abs(a.playbackRate - b.playbackRate) < 1.0e-9
                    && std::abs(a.gate - b.gate) < 2.0e-6f && a.wet == b.wet,
                "recalled clipped curves reproduce the same rendered trajectory and readhead speed");
    }
}

inline void installClippedFixture(BackScratchProcessor& processor, bool generated)
{
    processor.selectFactoryPreset(68); // A long rounded word gesture crosses grid boundaries.
    processor.setPhraseDurationChoice(5);
    processor.setPhraseTimingChoice(1);
    processor.shiftPattern(7);
    processor.setKeepSub(false);
    processor.setPhraseLocked(false);
    if (generated) {
        processor.setHostParameter("generated", 1.0f);
        processor.setHostParameter("sourceBeats", 16.0f);
        processor.setHostParameter("sourceBarBeats", 4.0f);
    }
    for (int seed = 1; seed <= (generated ? 64 : 1); ++seed) {
        if (generated) processor.setHostParameter("generationSeed", static_cast<float>(seed * 817));
        const auto original = processor.currentPattern();
        for (int eraseCell = 0; eraseCell < patternGridDivisions; ++eraseCell) {
            auto edited = original;
            if (!erasePatternGridCells(edited, eraseCell, eraseCell) || !hasClippedCurve(edited)) continue;
            const int drawCell = (eraseCell + 8) % patternGridDivisions;
            if (!drawPatternGridLine(edited, drawCell, 0.0, drawCell + 1,
                                      patternEditingDelayRange(edited) * 0.6)
                || !hasClippedCurve(edited)) continue;
            require(processor.beginPatternEdit() && processor.previewPatternEdit(edited)
                        && processor.commitPatternEdit() && processor.hasCustomPattern(),
                    "real pencil/eraser helpers commit clipped factory and generated fixtures");
            compare(edited, processor.currentPattern());
            return;
        }
    }
    throw std::runtime_error("custom serialization fixture must include an actually clipped smooth curve");
}

inline void checkClippedRecall(bool generated)
{
    TemporaryPreset preset;
    BackScratchProcessor source;
    installClippedFixture(source, generated);
    const auto original = source.currentPattern();
    require(hasClippedCurve(original) && original.editingRange > 0.0,
            "curve recall tests exercise non-default polynomial domains and saved graph range");
    require(source.savePreset(preset.file).wasOk(), "clipped custom geometry saves as a preset");
    BackScratchProcessor fromPreset;
    fromPreset.setPhraseLocked(false); // a locked phrase keeps its own length and timing on preset loads
    require(fromPreset.loadPreset(preset.file).wasOk() && fromPreset.hasCustomPattern()
                && fromPreset.isGenerated() == generated && !fromPreset.keepSub() && !fromPreset.phraseLocked(),
            "clipped preset restores custom identity and accompanying controls");
    compare(original, fromPreset.currentPattern());
    juce::MemoryBlock saved;
    source.getStateInformation(saved);
    BackScratchProcessor fromProject;
    fromProject.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    require(fromProject.hasCustomPattern() && fromProject.currentPresetName() == source.currentPresetName(),
            "clipped project restores custom mode and the saved name");
    compare(original, fromProject.currentPattern());
    for (const int duration : {0, 3, 7}) {
        for (const int timing : {0, 1, 2}) {
            fromPreset.setPhraseDurationChoice(duration);
            fromPreset.setPhraseTimingChoice(timing);
            fromPreset.shiftPattern(3);
            fromProject.setPhraseDurationChoice(duration);
            fromProject.setPhraseTimingChoice(timing);
            fromProject.shiftPattern(3);
            auto retimed = fitPatternToPhrase(original, fromPreset.phraseBeats());
            retimed.gridOffset = fromPreset.patternShiftSteps();
            compare(retimed, fromPreset.currentPattern());
            compare(retimed, fromProject.currentPattern());
        }
    }
}

inline void checkRejectedGeometryDoesNotMutate()
{
    TemporaryPreset preset;
    BackScratchProcessor processor;
    installClippedFixture(processor, true);
    require(processor.savePreset(preset.file).wasOk(), "invalid geometry tests begin from a saved custom phrase");
    const auto validPreset = juce::XmlDocument::parse(preset.file);
    juce::MemoryBlock unchanged;
    processor.getStateInformation(unchanged);
    const auto validProject = juce::AudioProcessor::getXmlFromBinary(unchanged.getData(), static_cast<int>(unchanged.getSize()));
    require(validPreset != nullptr && validProject != nullptr, "custom fixtures contain valid XML");

    // Each corrupt file also asks for a different preset name and control value.
    // Failure must be atomic across geometry, host parameters, name and undo.
    enum class Damage { missingChild, missingSegmentField, nan, gap, unsafeRate,
                        invalidDomain, tooMany, noSegments, unknownChild, unsupportedFormat,
                        missingRange, clippedRange, invalidGate, malformedNumber };
    const auto corrupt = [](juce::XmlElement& container, Damage damage) {
        auto* custom = container.getChildByName("EDITED_PATTERN");
        require(custom != nullptr && custom->getNumChildElements() > 1, "invalid tests have multiple stored curve pieces");
        auto* first = custom->getFirstChildElement();
        switch (damage) {
            case Damage::missingChild: container.removeChildElement(custom, true); break;
            case Damage::missingSegmentField: first->removeAttribute("curveEnd"); break;
            case Damage::nan: first->setAttribute("delayStart", "NaN"); break;
            case Damage::gap: {
                auto* second = first->getNextElement();
                second->setAttribute("start", second->getDoubleAttribute("start") + 0.001);
                break;
            }
            case Damage::unsafeRate:
                custom->deleteAllChildElements();
                for (int i = 0; i < 2; ++i) {
                    auto* node = custom->createNewChildElement("SEGMENT");
                    node->setAttribute("start", i == 0 ? 0.0 : 0.01);
                    node->setAttribute("end", i == 0 ? 0.01 : 1.0);
                    node->setAttribute("delayStart", 0.0);
                    node->setAttribute("delayEnd", i == 0 ? 0.1 : 0.0);
                    node->setAttribute("gateStart", 1.0); node->setAttribute("gateEnd", 1.0);
                    node->setAttribute("wet", 1.0); node->setAttribute("curved", 0);
                    node->setAttribute("curveStart", 0.0); node->setAttribute("curveEnd", 1.0);
                }
                break;
            case Damage::invalidDomain:
                first->setAttribute("curved", 1); first->setAttribute("curveStart", 0.9);
                first->setAttribute("curveEnd", 0.1); break;
            case Damage::tooMany:
                while (custom->getNumChildElements() <= ScratchPattern::maximumSegments)
                    custom->addChildElement(new juce::XmlElement(*first));
                break;
            case Damage::noSegments: custom->deleteAllChildElements(); break;
            case Damage::unknownChild: custom->createNewChildElement("UNKNOWN"); break;
            case Damage::unsupportedFormat: custom->setAttribute("format", 2); break;
            case Damage::missingRange: custom->removeAttribute("range"); break;
            case Damage::clippedRange: custom->setAttribute("range", 0.000001); break;
            case Damage::invalidGate: first->setAttribute("gateEnd", -0.0000001); break;
            case Damage::malformedNumber: first->setAttribute("start", "0oops"); break;
        }
    };
    for (const auto damage : {Damage::missingChild, Damage::missingSegmentField, Damage::nan,
                             Damage::gap, Damage::unsafeRate, Damage::invalidDomain, Damage::tooMany,
                             Damage::noSegments, Damage::unknownChild, Damage::unsupportedFormat,
                             Damage::missingRange, Damage::clippedRange, Damage::invalidGate,
                             Damage::malformedNumber}) {
        juce::XmlElement brokenPreset(*validPreset);
        corrupt(brokenPreset, damage);
        brokenPreset.setAttribute("name", "Must Not Replace Current Name");
        brokenPreset.setAttribute("keepSub", 1);
        require(preset.file.replaceWithText(brokenPreset.toString()) && processor.loadPreset(preset.file).failed(),
                "missing, malformed, discontinuous, oversized and unsafe custom presets are rejected");
        juce::MemoryBlock after;
        processor.getStateInformation(after);
        require(after == unchanged && processor.canUndoPatternEdit(),
                "a rejected preset preserves every state byte and the existing edit history");

        juce::XmlElement brokenProject(*validProject);
        corrupt(brokenProject, damage);
        brokenProject.setAttribute("savedName", "Must Not Replace Current Name");
        for (auto* child = brokenProject.getFirstChildElement(); child != nullptr; child = child->getNextElement())
            if (child->getStringAttribute("id") == "keepSub") child->setAttribute("value", 1);
        juce::MemoryBlock invalid;
        juce::AudioProcessor::copyXmlToBinary(brokenProject, invalid);
        processor.setStateInformation(invalid.getData(), static_cast<int>(invalid.getSize()));
        processor.getStateInformation(after);
        require(after == unchanged && processor.canUndoPatternEdit(),
                "a rejected project preserves every state byte and the existing edit history");
    }
}

} // namespace backScratch::editedSerializationTests

inline void runEditedSerializationTests()
{
    backScratch::editedSerializationTests::checkClippedRecall(false);
    backScratch::editedSerializationTests::checkClippedRecall(true);
    backScratch::editedSerializationTests::checkRejectedGeometryDoesNotMutate();
    std::cout << "BACK-SCRATCH clipped factory/generated curve recall, timed retiming and atomic invalid-geometry rejection passed\n";
}
