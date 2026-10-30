#pragma once

#include "BackScratchProcessor.h"
#include "EditedPatternStore.h"
#include "PlaybackAlignmentTests.h"
#include "TimingStateTests.h"
#include <atomic>
#include <thread>
#include <vector>

namespace backScratch::editedProcessorTests {

inline void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

// Four independently specified, normalized musical contours. Their readhead
// slopes remain bounded at every point, including immediately after an edit.
inline ScratchPattern contour(int identity)
{
    ScratchPattern pattern;
    pattern.beats = 1.0;
    pattern.count = 4;
    const double depth = std::array<double, 4> {0.25, 0.375, 0.5, 0.1875}[static_cast<size_t>(identity % 4)];
    pattern.segments[0] = {0.0, 0.125, 0.0, 0.0, 1, 1, 0, false};
    pattern.segments[1] = {0.125, 0.375, 0.0, depth, 1, 1, 1, false};
    pattern.segments[2] = {0.375, 0.875, depth, 0.0, 1, 1, 1, false};
    pattern.segments[3] = {0.875, 1.0, 0.0, 0.0, 1, 1, 0, false};
    return pattern;
}

inline ScratchPattern audibleContour(int identity, double beats, int shift)
{
    auto result = fitPatternToPhrase(contour(identity), beats);
    result.gridOffset = shift;
    return result;
}

inline void compare(const ScratchPattern& expected, const ScratchPattern& actual)
{
    playbackAlignmentTests::comparePattern(expected, actual);
    require(std::abs(expected.editingRange - actual.editingRange) < 1.0e-9,
            "editing and recall retain the same vertical graph scale");
    for (int index = 0; index < expected.count; ++index) {
        const auto& a = expected.segments[static_cast<size_t>(index)];
        const auto& b = actual.segments[static_cast<size_t>(index)];
        require(std::abs(a.curveStart - b.curveStart) < 1.0e-12 && std::abs(a.curveEnd - b.curveEnd) < 1.0e-12,
                "editing and recall retain clipped smooth-curve domains");
    }
}

inline void checkStoreLifetime()
{
    EditedPatternStore store;
    const auto tagged = [](int tag) {
        ScratchPattern pattern;
        pattern.beats = tag;
        pattern.count = 32;
        for (int index = 0; index < pattern.count; ++index) {
            auto& segment = pattern.segments[static_cast<size_t>(index)];
            segment.start = tag + index;
            segment.end = tag + index + 1;
            segment.delayStart = tag * 3 + index;
        }
        return pattern;
    };
    const auto intact = [](const ScratchPattern& pattern) {
        if (pattern.count != 32) return false;
        for (int index = 0; index < pattern.count; ++index) {
            const auto& segment = pattern.segments[static_cast<size_t>(index)];
            if (segment.start != pattern.beats + index || segment.end != pattern.beats + index + 1
                || segment.delayStart != pattern.beats * 3 + index) return false;
        }
        return true;
    };

    std::array<int, EditedPatternStore::capacity> history;
    for (int index = 0; index < EditedPatternStore::capacity; ++index) {
        require(store.publish(tagged(index + 1)) != EditedPatternStore::None, "free edit slots accept complete snapshots");
        history[static_cast<size_t>(index)] = store.acquireLatest();
        require(history[static_cast<size_t>(index)] != EditedPatternStore::None, "latency history pins each published edit");
    }
    require(store.publish(tagged(33)) == EditedPatternStore::None,
            "a full edit pool coalesces previews instead of overwriting delayed audio geometry");
    store.publishExisting(history[5]);
    const int cancelled = store.acquireLatest();
    require(cancelled == history[5] && store.get(cancelled).beats == 6,
            "cancel can republish the gesture's protected original even when every slot is occupied");
    store.release(cancelled);
    for (int index = 0; index < EditedPatternStore::capacity; ++index) {
        const auto& pinned = store.get(history[static_cast<size_t>(index)]);
        require(pinned.beats == index + 1 && intact(pinned), "every historical edit remains immutable while pinned");
    }
    store.release(history[0]); history[0] = EditedPatternStore::None;
    require(store.publish(tagged(33)) != EditedPatternStore::None,
            "expired history returns slots for the newest preview without allocation");
    std::atomic<int> audible {history[7]};
    store.retain(history[7]);
    const int displayed = store.acquirePublished(audible);
    require(displayed == history[7] && store.get(displayed).beats == 8,
            "the display can independently pin an old audible contour");
    store.release(audible.exchange(EditedPatternStore::None));
    for (const int index : history) store.release(index);
    require(store.get(displayed).beats == 8 && intact(store.get(displayed)),
            "display readers retain a complete snapshot after audio and history release it");
    store.release(displayed);

    std::atomic<bool> finished {false}, failed {false};
    std::atomic<unsigned> reads {0};
    const auto reader = [&] {
        do {
            const int snapshot = store.acquireLatest();
            if (snapshot == EditedPatternStore::None) continue;
            if (!intact(store.get(snapshot))) failed.store(true);
            reads.fetch_add(1);
            store.release(snapshot);
        } while (!finished.load());
    };
    std::thread audio(reader), display(reader);
    for (int tag = 34; tag < 2034; ++tag) {
        while (store.publish(tagged(tag)) == EditedPatternStore::None) std::this_thread::yield();
    }
    finished.store(true);
    audio.join(); display.join();
    require(!failed.load() && reads.load() > 0,
            "concurrent editor, audio and display publication cannot tear or recycle a live contour");
}

inline void checkEditTransactions()
{
    BackScratchProcessor processor;
    processor.selectFactoryPreset(12);
    processor.setPhraseBars(4);
    processor.setPhraseTimingChoice(1);
    processor.shiftPattern(7);
    processor.setKeepSub(false);
    const auto initial = processor.currentPattern();
    require(!processor.hasCustomPattern(), "factory patterns initially retain their original source identity");
    require(processor.beginPatternEdit(), "a first pencil stroke opens one edit transaction");
    for (int identity = 0; identity < 4; ++identity)
        require(processor.previewPatternEdit(contour(identity)), "each drag update can preview a valid contour");
    require(processor.commitPatternEdit(), "mouse release commits a changed contour");
    require(processor.hasCustomPattern() && processor.canUndoPatternEdit() && !processor.canRedoPatternEdit(),
            "a committed stroke becomes one reversible custom edit");
    const auto edited = audibleContour(3, 24.0, 7);
    compare(edited, processor.currentPattern());
    require(processor.phraseDurationChoice() == 5 && processor.phraseTimingChoice() == 1
            && processor.patternShiftSteps() == 7 && !processor.keepSub(),
            "drawing preserves phrase length, timing modifier, shift and sub treatment");
    processor.undoPatternEdit();
    require(!processor.hasCustomPattern() && processor.canRedoPatternEdit(),
            "one undo removes the entire multi-update stroke and restores factory mode");
    compare(initial, processor.currentPattern());
    processor.redoPatternEdit();
    require(processor.hasCustomPattern(), "redo restores custom mode");
    compare(edited, processor.currentPattern());

    require(processor.beginPatternEdit() && processor.previewPatternEdit(contour(0)),
            "a second stroke can preview over an existing custom contour");
    processor.cancelPatternEdit();
    compare(edited, processor.currentPattern());
    processor.undoPatternEdit();
    compare(initial, processor.currentPattern());
    processor.redoPatternEdit();
    compare(edited, processor.currentPattern());
    for (int duration = 0; duration < BackScratchProcessor::phraseDurationCount; ++duration) {
        for (int timing = 0; timing < 3; ++timing) {
            processor.setPhraseDurationChoice(duration);
            processor.setPhraseTimingChoice(timing);
            const double beats = std::array<double, 8> {0.5, 1, 2, 4, 8, 16, 32, 64}[static_cast<size_t>(duration)]
                * std::array<double, 3> {1, 1.5, 2.0 / 3.0}[static_cast<size_t>(timing)];
            compare(audibleContour(3, beats, 7), processor.currentPattern());
        }
    }
    processor.randomizePattern();
    require(!processor.hasCustomPattern() && processor.isGenerated(),
            "Randomize exits custom mode and creates a new generated phrase");
    const auto generated = processor.currentPattern();
    require(processor.beginPatternEdit() && processor.previewPatternEdit(contour(1)),
            "generated phrases can be edited directly");
    processor.cancelPatternEdit();
    require(!processor.hasCustomPattern() && processor.isGenerated(),
            "canceling the first edit restores the original generated source");
    compare(generated, processor.currentPattern());
    require(processor.beginPatternEdit() && processor.previewPatternEdit(contour(2)) && processor.commitPatternEdit(),
            "a later edit commits normally after cancellation");
    processor.selectFactoryPreset(54);
    require(!processor.hasCustomPattern() && !processor.isGenerated(),
            "factory selection exits custom mode without retaining a stale drawn contour");
    auto factory = fitPatternToPhrase(makePattern(54), processor.phraseBeats());
    factory.gridOffset = 0;
    compare(factory, processor.currentPattern());
}

inline void checkEditRecall()
{
    timingStateTests::TemporaryPreset preset;
    BackScratchProcessor source;
    source.selectFactoryPreset(19);
    source.setPhraseDurationChoice(0);
    source.setPhraseTimingChoice(2);
    source.shiftPattern(11);
    source.setKeepSub(false);
    source.setPhraseLocked(false);
    require(source.beginPatternEdit() && source.previewPatternEdit(contour(2)) && source.commitPatternEdit(),
            "custom recall fixture creates a complete drawn phrase");
    const auto expected = audibleContour(2, 1.0 / 3.0, 11);
    compare(expected, source.currentPattern());
    require(source.savePreset(preset.file).wasOk(), "custom geometry saves as a portable preset");
    BackScratchProcessor restoredPreset;
    restoredPreset.setPhraseLocked(false); // a locked phrase keeps its own length and timing on preset loads
    require(restoredPreset.loadPreset(preset.file).wasOk() && restoredPreset.hasCustomPattern()
            && !restoredPreset.keepSub() && !restoredPreset.phraseLocked(),
            "preset recall restores custom mode and all accompanying controls");
    compare(expected, restoredPreset.currentPattern());
    juce::MemoryBlock saved;
    source.getStateInformation(saved);
    BackScratchProcessor restoredProject;
    restoredProject.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    require(restoredProject.hasCustomPattern() && restoredProject.currentPresetName() == source.currentPresetName(),
            "project recall restores the saved custom identity");
    compare(expected, restoredProject.currentPattern());
    restoredProject.setPhraseDurationChoice(7);
    restoredProject.setPhraseTimingChoice(1);
    compare(audibleContour(2, 96.0, 11), restoredProject.currentPattern());

    // Old states may carry unknown future fields. Schema seven must still open
    // in its original factory/generated mode instead of inheriting active edits.
    auto legacyPreset = juce::XmlDocument::parse(preset.file);
    require(legacyPreset != nullptr, "custom preset serializes valid XML");
    legacyPreset->setAttribute("version", 7);
    legacyPreset->setAttribute("generatorVersion", 1);
    restoredPreset.setPhraseLocked(false); // a locked phrase keeps its own length and timing on preset loads
    require(preset.file.replaceWithText(legacyPreset->toString()) && restoredPreset.loadPreset(preset.file).wasOk()
            && !restoredPreset.hasCustomPattern(), "schema seven preset restoration clears stale custom mode");
    auto legacyProject = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    require(legacyProject != nullptr, "custom project serializes valid XML");
    legacyProject->setAttribute("engineVersion", 7);
    legacyProject->setAttribute("generatorVersion", 1);
    juce::MemoryBlock oldState;
    juce::AudioProcessor::copyXmlToBinary(*legacyProject, oldState);
    restoredProject.setStateInformation(oldState.getData(), static_cast<int>(oldState.getSize()));
    require(!restoredProject.hasCustomPattern(), "schema seven project restoration clears stale custom mode");
}

inline void checkAudibleEditTimeline(int blockSize)
{
    constexpr double rate = 48000.0, samplesPerBeat = 24000.0;
    constexpr int frames = 12000;
    struct Event { int frame, contour, timing, shift; bool edited; };
    const std::array<Event, 8> events {{{0, 0, 0, 0, true}, {103, 1, 0, 0, true},
        {211, 2, 0, 0, true}, {317, 3, 0, 0, true}, {2003, 3, 1, 0, true},
        {4001, 3, 1, 7, true}, {6501, 0, 1, 7, true}, {9007, 0, 1, 0, false}}};
    const auto expectedPattern = [](const Event& event) {
        const double beats = event.timing == 1 ? 0.75 : 0.5;
        auto result = event.edited ? audibleContour(event.contour, beats, event.shift)
                                  : fitPatternToPhrase(makePattern(13), beats);
        result.gridOffset = event.shift;
        return result;
    };
    std::array<ScratchPattern, events.size()> expectedPatterns;
    for (size_t index = 0; index < events.size(); ++index) expectedPatterns[index] = expectedPattern(events[index]);
    const auto contextIndex = [&events](int frame) {
        size_t result = 0;
        for (size_t index = 1; index < events.size() && events[index].frame <= frame; ++index) result = index;
        return result;
    };
    BackScratchProcessor processor;
    processor.setPhraseDurationChoice(0);
    processor.setKeepSub(false);
    require(processor.beginPatternEdit() && processor.previewPatternEdit(contour(0)),
            "the live edit timeline begins with an uncommitted stroke");
    playbackAlignmentTests::Head head;
    processor.setPlayHead(&head);
    processor.prepareToPlay(rate, blockSize);
    LiveScratchEngine reference;
    reference.prepare(rate, 2, 2.0);
    const int latency = reference.latencySamples();
    juce::MidiBuffer midi;
    double maximumDifference = 0.0;
    for (int start = 0; start < frames;) {
        for (size_t index = 1; index < events.size(); ++index) {
            if (events[index].frame != start) continue;
            const auto& event = events[index];
            if (index <= 3) require(processor.previewPatternEdit(contour(event.contour)), "live drag publishes each geometry revision");
            else if (index == 4) {
                require(processor.commitPatternEdit(), "the live stroke commits without replacing its final preview");
                processor.setPhraseTimingChoice(event.timing);
            } else if (index == 5) processor.shiftPattern(event.shift);
            else if (index == 6)
                require(processor.beginPatternEdit() && processor.previewPatternEdit(contour(event.contour))
                        && processor.commitPatternEdit(), "another live stroke replaces the same shifted custom phrase");
            else processor.selectFactoryPreset(13);
            compare(expectedPatterns[index], processor.currentPattern());
            if (start > latency) {
                const auto prior = processor.displayState();
                compare(expectedPatterns[contextIndex(start - 1 - latency)], prior.pattern);
            }
        }
        int count = std::min(blockSize, frames - start);
        for (const auto& event : events) if (event.frame > start) count = std::min(count, event.frame - start);
        juce::AudioBuffer<float> audio(2, count);
        for (int sample = 0; sample < count; ++sample)
            for (int channel = 0; channel < 2; ++channel)
                audio.setSample(channel, sample, playbackAlignmentTests::source(start + sample, rate, channel));
        head.beat = start / samplesPerBeat;
        processor.processBlock(audio, midi);
        for (int sample = 0; sample < count; ++sample) {
            const int frame = start + sample;
            const int audibleFrame = frame - latency;
            const auto& active = expectedPatterns[contextIndex(audibleFrame)];
            const auto expected = reference.process(playbackAlignmentTests::source(frame, rate, 0),
                playbackAlignmentTests::source(frame, rate, 1), evaluatePattern(active, audibleFrame / samplesPerBeat),
                samplesPerBeat, false, false);
            for (int channel = 0; channel < 2; ++channel) {
                const float value = audio.getSample(channel, sample);
                require(std::isfinite(value), "live contour edits keep both audio channels finite");
                maximumDifference = std::max(maximumDifference,
                    std::abs(static_cast<double>(value - expected[static_cast<size_t>(channel)])));
            }
        }
        const int finalFrame = start + count - 1 - latency;
        const auto index = contextIndex(finalFrame);
        const auto view = processor.displayState();
        compare(expectedPatterns[index], view.pattern);
        require(view.edited == events[index].edited, "the audible display changes custom mode at the same sample as audio");
        const auto active = evaluatePattern(expectedPatterns[index], finalFrame / samplesPerBeat);
        patternTests::checkSameSample(active, view.activeSample,
            "the displayed cursor evaluates the exact delayed contour, not the latest mouse preview");
        start += count;
    }
    if (maximumDifference > 0.00003)
        std::cerr << "Edited timeline block=" << blockSize << " maximum difference=" << maximumDifference << '\n';
    require(maximumDifference < 0.00003,
            "live geometry edits, timing and shifts follow the independently delayed audio timeline");
}

inline void checkInvalidAndLargeEditFiles()
{
    timingStateTests::TemporaryPreset preset;
    BackScratchProcessor processor;
    require(processor.beginPatternEdit() && processor.previewPatternEdit(contour(2)) && processor.commitPatternEdit()
            && processor.savePreset(preset.file).wasOk(), "invalid edit tests begin from a valid saved contour");
    const auto validPreset = juce::XmlDocument::parse(preset.file);
    juce::MemoryBlock unchanged;
    processor.getStateInformation(unchanged);
    const auto validState = juce::AudioProcessor::getXmlFromBinary(unchanged.getData(), static_cast<int>(unchanged.getSize()));
    require(validPreset != nullptr && validState != nullptr, "custom-file validation fixtures serialize correctly");
    struct Invalid { bool patternLevel; const char* attribute; const char* value; };
    const Invalid cases[] {{true, "beats", "nan"}, {true, "beats", "2"}, {true, "range", "inf"},
        {false, "start", "-1"}, {false, "end", "0"}, {false, "delayStart", "-0.1"},
        {false, "delayEnd", "100"}, {false, "wet", "1.5"}, {false, "gateStart", "-1"},
        {false, "curved", "2"}, {false, "curveStart", "2"}, {false, "curveEnd", nullptr},
        {false, "start", "0junk"}};
    const auto invalidate = [](juce::XmlElement& xml, const Invalid& invalid) {
        auto* custom = xml.getChildByName("EDITED_PATTERN");
        require(custom != nullptr, "new presets and projects contain the complete custom contour");
        auto* target = invalid.patternLevel ? custom : custom->getFirstChildElement();
        require(target != nullptr, "custom contour contains explicit segment coordinates");
        if (invalid.value == nullptr) target->removeAttribute(invalid.attribute);
        else target->setAttribute(invalid.attribute, invalid.value);
    };
    for (const auto& entry : cases) {
        juce::XmlElement invalidPreset(*validPreset);
        invalidate(invalidPreset, entry);
        invalidPreset.setAttribute("factory", 54);
        require(preset.file.replaceWithText(invalidPreset.toString()) && processor.loadPreset(preset.file).failed(),
                "non-finite, malformed, missing and unsafe custom coordinates are rejected");
        juce::MemoryBlock after;
        processor.getStateInformation(after);
        require(after == unchanged, "invalid contour presets cannot partially mutate the working phrase or saved name");
        juce::XmlElement invalidState(*validState);
        invalidate(invalidState, entry);
        juce::MemoryBlock invalid;
        juce::AudioProcessor::copyXmlToBinary(invalidState, invalid);
        processor.setStateInformation(invalid.getData(), static_cast<int>(invalid.getSize()));
        processor.getStateInformation(after);
        require(after == unchanged, "invalid custom project states leave the complete current state unchanged");
    }

    ScratchPattern dense;
    dense.beats = 1.0;
    dense.count = ScratchPattern::maximumSegments;
    dense.editingRange = 0.25;
    for (int index = 0; index < dense.count; ++index)
        dense.segments[static_cast<size_t>(index)] = {static_cast<double>(index) / dense.count,
            static_cast<double>(index + 1) / dense.count, 0, 0, 1, 1, 0, false};
    require(processor.beginPatternEdit() && processor.previewPatternEdit(dense) && processor.commitPatternEdit()
            && processor.savePreset(preset.file).wasOk(), "the full fixed segment capacity remains saveable");
    require(preset.file.getSize() > 65536, "dense custom preset exercises sizes beyond the previous 64KiB file limit");
    BackScratchProcessor restored;
    restored.setPhraseLocked(false); // a locked phrase keeps its own length and timing on preset loads
    require(restored.loadPreset(preset.file).wasOk() && restored.hasCustomPattern(),
            "all 512 custom segments survive portable preset recall");
    compare(processor.currentPattern(), restored.currentPattern());
}

} // namespace backScratch::editedProcessorTests

inline void runEditedProcessorTests()
{
    using namespace backScratch::editedProcessorTests;
    checkStoreLifetime();
    checkEditTransactions();
    checkEditRecall();
    checkInvalidAndLargeEditFiles();
    for (const int blockSize : {64, 257, 1024}) checkAudibleEditTimeline(blockSize);
    std::cout << "BACK-SCRATCH immutable edit lifetime, single-stroke undo, custom recall and audible drawing alignment passed\n";
}
