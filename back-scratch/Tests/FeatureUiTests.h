#pragma once
#include "BackScratchEditor.h"
#include "InteractionTests.h"
#include "GridEditorInteractionTests.h"
#include "ProcessorTests.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>
#include <vector>

// 0.11 editor and processor features: the PERFORM page, Engage, Play Every,
// slots and chains, Mix/Depth/Output bars, rolls, the Size menu, user preset
// folders, factory bank presets and saved-state round trips.
namespace backScratch::featureUiTests {

using Control = BackScratchEditor::Control;
using interactionTests::check;
using interactionTests::click;
using interactionTests::event;

constexpr std::array<Control, 21> performControls {
    Control::engage, Control::quantize, Control::retrigger, Control::every, Control::lookback,
    Control::mix, Control::depth, Control::output, Control::slotA, Control::slotB, Control::slotC,
    Control::slotD, Control::slotE, Control::slotF, Control::slotG, Control::slotH,
    Control::chain, Control::split, Control::history, Control::pagePattern, Control::pagePerform};

inline void checkLayout()
{
    const auto screen = BackScratchEditor::patternBounds();
    for (std::size_t a = 0; a < performControls.size(); ++a) {
        const auto box = BackScratchEditor::controlBounds(performControls[a]);
        check(!box.isEmpty(), "every 0.11 control has a place");
        if (performControls[a] != Control::pagePattern && performControls[a] != Control::pagePerform)
            check(screen.contains(box), "PERFORM controls sit inside the screen's graph area");
        for (std::size_t b = a + 1; b < performControls.size(); ++b)
            check(!box.intersects(BackScratchEditor::controlBounds(performControls[b])), "PERFORM controls never overlap");
        for (const auto other : {Control::shiftLeft, Control::shiftRight, Control::presets, Control::previous, Control::next,
                                 Control::randomize, Control::dotted, Control::triplet, Control::phraseLock})
            check(!box.intersects(BackScratchEditor::controlBounds(other)), "0.11 controls keep clear of the existing controls");
    }
}

inline void processBlocks(BackScratchProcessor& processor, processorTests::Head& head, int blocks)
{
    juce::MidiBuffer midi;
    for (int block = 0; block < blocks; ++block) {
        juce::AudioBuffer<float> audio(2, 512);
        for (int i = 0; i < 512; ++i) for (int ch = 0; ch < 2; ++ch) audio.setSample(ch, i, processorTests::testSound(block * 512 + i, ch));
        processor.processBlock(audio, midi);
        head.beat += 512.0 * head.bpm / (60.0 * 48000.0);
    }
}

inline void checkPerformPage()
{
    BackScratchProcessor processor;
    processor.prepareToPlay(48000.0, 512);
    auto editor = std::make_unique<BackScratchEditor>(processor);
    editor->setVisible(true);
    check(!editor->showsPerformPage(), "the editor opens on the pattern graph");
    click(*editor, Control::pagePerform);
    check(editor->showsPerformPage(), "the PERFORM soft key shows the performance controls");
    // The drawing tools are not live on the PERFORM page.
    click(*editor, Control::pencil);
    click(*editor, Control::engage);
    check(!processor.isEngaged(), "ENGAGE turns the pattern off");
    click(*editor, Control::engage);
    check(processor.isEngaged(), "ENGAGE turns it back on");
    click(*editor, Control::retrigger);
    check(processor.parameterValue("retrigger") > 0.5f, "RETRIG toggles");
    click(*editor, Control::retrigger);
    // Bars: drag sideways, double-click resets.
    const auto mix = BackScratchEditor::controlBounds(Control::mix);
    const auto start = mix.getCentre();
    editor->mouseDown(event(*editor, start, start));
    editor->mouseDrag(event(*editor, start.translated(-94.0f, 0.0f), start, true));
    editor->mouseUp(event(*editor, start.translated(-94.0f, 0.0f), start, true));
    check(processor.parameterValue("wetMix") < 60.0f && processor.parameterValue("wetMix") > 40.0f,
          "dragging MIX left lowers it in proportion");
    editor->mouseDoubleClick(event(*editor, start, start));
    check(juce::approximatelyEqual(processor.parameterValue("wetMix"), 100.0f), "double-clicking MIX restores 100%");
    // Slots.
    click(*editor, Control::slotC);
    check(processor.selectedSlot() == 2 && !processor.slotFilled(2), "clicking a slot selects it");
    processor.copyPatternToSlot(0, 2);
    check(processor.slotFilled(2), "copying slot A fills slot C");
    check(processor.canUndoPatternEdit(), "copying to a slot is one undo step");
    processor.undoPatternEdit();
    check(!processor.slotFilled(2), "undo empties the slot again");
    processor.redoPatternEdit();
    check(processor.slotFilled(2), "redo fills it again");
    // Choice keys open a real menu inside the editor.
    click(*editor, Control::every);
    auto* popup = juce::Component::getCurrentlyModalComponent();
    check(popup != nullptr && editor->isParentOf(popup), "PLAY opens its menu inside the editor");
    editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    gridEditorInteractionTests::pumpMessagesUntil([] { return juce::Component::getCurrentlyModalComponent() == nullptr; });
    editor->showSlotMenu(3);
    check(juce::Component::getCurrentlyModalComponent() != nullptr, "right-click on a slot offers copy and clear");
    editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    gridEditorInteractionTests::pumpMessagesUntil([] { return juce::Component::getCurrentlyModalComponent() == nullptr; });
    click(*editor, Control::slotA);
    click(*editor, Control::pagePattern);
    check(!editor->showsPerformPage(), "the PATTERN soft key returns to the graph");
}

inline void checkRollTool()
{
    BackScratchProcessor processor;
    processor.prepareToPlay(48000.0, 512);
    processor.selectFactoryPreset(0);
    auto editor = std::make_unique<BackScratchEditor>(processor);
    editor->setVisible(true);
    gridEditorInteractionTests::chooseMenuItem(*editor, Control::drawOptions, "Roll 1/16");
    check(editor->currentDrawMode() == BackScratchEditor::DrawMode::roll, "the pencil menu arms the roll tool");
    gridEditorInteractionTests::stroke(*editor, gridEditorInteractionTests::gridPoint(8.0f, 0.5f),
                                       gridEditorInteractionTests::gridPoint(12.0f, 0.5f));
    check(processor.hasCustomPattern() && processor.canUndoPatternEdit(), "a roll stroke is one undoable edit");
    const auto pattern = processor.currentPattern();
    const double sixteenth = 0.25; // one-bar phrase in quarter-note beats
    const auto first = evaluatePattern(pattern, 2.0 + sixteenth * 1.5);
    const auto later = evaluatePattern(pattern, 2.0 + sixteenth * 3.5);
    check(first.wet > 0.0f && std::abs((2.0 + sixteenth * 1.5 - first.delayBeats) - (2.0 + sixteenth * 0.5)) < 1.0e-6
              && std::abs((2.0 + sixteenth * 3.5 - later.delayBeats) - (2.0 + sixteenth * 0.5)) < 1.0e-6,
          "every pass of a dragged 1/16 roll replays the sixteenth where it started");
    processor.undoPatternEdit();
    check(!processor.hasCustomPattern(), "undo removes the roll");
}

inline void checkSizeMenu()
{
    BackScratchProcessor processor;
    processor.prepareToPlay(48000.0, 512);
    auto editor = std::make_unique<BackScratchEditor>(processor);
    editor->setVisible(true);
    editor->applyScale(1.25f);
    check(editor->getWidth() == 875 && editor->getHeight() == juce::roundToInt(942 * 1.25f),
          "Size 125% enlarges the editor at its aspect ratio");
    check(juce::approximatelyEqual(processor.editorScale(), 1.25f), "the chosen size is remembered");
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    BackScratchProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(juce::approximatelyEqual(restored.editorScale(), 1.25f), "the size is saved with the project as editorScale");
    // A 1920 x 1080 display's user area: 90% and 100% fit with 60 px to spare, 125% and 150% do not.
    const juce::Rectangle<float> desktop(0, 0, 1920, 1050);
    check(BackScratchEditor::sizeFits(0.9f, desktop) && BackScratchEditor::sizeFits(1.0f, desktop)
              && !BackScratchEditor::sizeFits(1.25f, desktop) && !BackScratchEditor::sizeFits(1.5f, desktop),
          "sizes that do not fit the display are disabled");
    editor->showSizeMenu();
    check(juce::Component::getCurrentlyModalComponent() != nullptr, "right-clicking the shell opens the Size menu");
    editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    gridEditorInteractionTests::pumpMessagesUntil([] { return juce::Component::getCurrentlyModalComponent() == nullptr; });
}

// Audible behaviour of Engage, Play Every and chains, through processBlock.
inline void checkPerformanceAudio()
{
    processorTests::Head head;
    BackScratchProcessor processor;
    processor.setPlayHead(&head);
    processor.prepareToPlay(48000.0, 512);
    processor.setChoice("phraseEvery", 2); // last of every 4
    processBlocks(processor, head, 30);
    auto view = processor.displayState();
    check(view.playing && view.every == 4 && !view.cycleActive, "bar 1 of 4 waits when playing the last of every 4");
    // Advance into bar 4 (beats 12-16 at 140 BPM).
    head.beat = 12.5;
    processor.prepareToPlay(48000.0, 512);
    processBlocks(processor, head, 30);
    view = processor.displayState();
    check(view.cycleActive, "bar 4 of 4 plays");
    // Engage snaps to the bar line.
    processor.setChoice("phraseEvery", 0);
    processor.setChoice("engageQuantize", 4);
    head.beat = 0.0;
    processor.prepareToPlay(48000.0, 512);
    processBlocks(processor, head, 10);
    processor.setEngaged(false);
    processBlocks(processor, head, 10);
    check(!processor.isEngaged() && processor.isAudiblyEngaged(), "a disengage waits for the next bar line");
    processBlocks(processor, head, 170);
    check(!processor.isAudiblyEngaged(), "the pattern stops at the bar line");
    // A chain steps through filled slots one phrase at a time.
    processor.setEngaged(true);
    processor.copyPatternToSlot(0, 1);
    processor.setChoice("slotChain", 1);
    head.beat = 4.25;
    processor.prepareToPlay(48000.0, 512);
    processBlocks(processor, head, 30);
    check(processor.displayState().slot == 1, "bar 2 of an A-B chain plays slot B");

    // Retrigger: engaging restarts the phrase at the snap point. Two
    // processors run in lockstep; only one has Retrigger on. Both engage at
    // the quarter note on beat 9, so the retriggered one-bar phrase must sit
    // one beat (a quarter of the phrase) behind the song-position one.
    BackScratchProcessor retriggered, following;
    processorTests::Head headA, headB;
    for (auto* p : {&retriggered, &following}) {
        p->setPlayHead(p == &retriggered ? &headA : &headB);
        p->setChoice("engageQuantize", 2);
        p->setEngaged(false);
        p->prepareToPlay(48000.0, 512);
    }
    retriggered.setValue("retrigger", 1.0f);
    headA.beat = headB.beat = 8.6;
    processBlocks(retriggered, headA, 4);
    processBlocks(following, headB, 4);
    check(!retriggered.isAudiblyEngaged() && !following.isAudiblyEngaged(), "both start disengaged");
    retriggered.setEngaged(true);
    following.setEngaged(true);
    processBlocks(retriggered, headA, 40);
    processBlocks(following, headB, 40);
    check(retriggered.isAudiblyEngaged() && following.isAudiblyEngaged(), "both engage at the next quarter note");
    const double offset = std::fmod(static_cast<double>(following.displayState().phase - retriggered.displayState().phase) + 1.0, 1.0);
    check(std::abs(offset - 0.25) < 1.0e-3, "Retrigger restarts the one-bar phrase at the beat-9 snap point");
    retriggered.setPlayHead(nullptr);
    following.setPlayHead(nullptr);
}

// Factory bank presets and user preset folders.
inline void checkPresets()
{
    BackScratchProcessor processor;
    processor.prepareToPlay(48000.0, 512);
    const auto& bank = bankPresets();
    int fills = -1, chain = -1, classicExact = classicBankIndex(5);
    for (std::size_t i = 0; i < bank.size(); ++i) {
        if (bank[i].name == "Bar 4 Rewind") fills = static_cast<int>(i);
        if (bank[i].name == "4 Bar Story") chain = static_cast<int>(i);
    }
    check(fills >= 0 && chain >= 0 && classicExact >= 0, "the bank contains its folders");
    processor.selectBankPreset(fills);
    check(processor.hasCustomPattern() && juce::roundToInt(processor.parameterValue("phraseEvery")) == 2
              && processor.currentPresetName() == "Bar 4 Rewind",
          "a Fills preset loads its pattern and plays on the last of every 4 bars");
    processor.selectBankPreset(chain);
    check(processor.slotFilled(1) && processor.slotFilled(2) && processor.slotFilled(3)
              && juce::roundToInt(processor.parameterValue("slotChain")) == 2,
          "a chain preset fills its slots and sets its chain");
    processor.selectBankPreset(classicExact);
    check(processor.selectedFactoryPreset() == 5 && !processor.hasCustomPattern()
              && juce::roundToInt(processor.parameterValue("slotChain")) == 0
              && juce::approximatelyEqual(processor.parameterValue("depth"), 100.0f)
              && juce::roundToInt(processor.parameterValue("subSplit")) == 2,
          "a Classic entry is the original recipe with every 0.11 setting neutral");
    // Into a slot: only that slot's pattern changes.
    processor.selectSlot(4);
    processor.selectBankPreset(fills);
    check(processor.slotFilled(4) && processor.selectedFactoryPreset() == 5 && !processor.hasCustomPattern(),
          "with slot E selected, a preset loads into slot E only");
    processor.selectSlot(0);

    // User folders under a temporary root; nothing touches the real folder.
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("BACK-SCRATCH-UserPresets", "");
    BackScratchProcessor::setUserPresetRootForTesting(root);
    check(BackScratchProcessor::userPresetRoot() == root, "tests redirect the user preset root");
    const auto file = root.getChildFile("Riddim Ideas").getChildFile("Wobble Gate.backscratch");
    processor.setValue("depth", 150.0f);
    check(processor.savePreset(file).wasOk() && file.existsAsFile(), "saving creates the chosen folder and preset");
    BackScratchProcessor other;
    check(other.loadPreset(file).wasOk() && juce::approximatelyEqual(other.parameterValue("depth"), 150.0f)
              && other.slotFilled(1) == processor.slotFilled(1),
          "a 0.11 preset restores Depth and its slots");
    root.deleteRecursively();
    BackScratchProcessor::setUserPresetRootForTesting({});
}

// Saved projects: 0.11 settings round-trip; 0.10 projects open neutral.
inline void checkStateRoundTrip()
{
    BackScratchProcessor source;
    source.prepareToPlay(48000.0, 512);
    source.setChoice("phraseEvery", 3);
    source.setChoice("lookback", 2);
    source.setValue("wetMix", 65.0f);
    source.setValue("outputTrim", -3.0f);
    source.setChoice("subSplit", 1);
    source.setValue("depth", 130.0f);
    source.copyPatternToSlot(0, 5);
    source.setHistoryMode(64);
    juce::MemoryBlock state;
    source.getStateInformation(state);
    BackScratchProcessor restored;
    restored.prepareToPlay(48000.0, 512);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(juce::roundToInt(restored.parameterValue("phraseEvery")) == 3 && juce::roundToInt(restored.parameterValue("lookback")) == 2
              && juce::approximatelyEqual(restored.parameterValue("wetMix"), 65.0f)
              && juce::approximatelyEqual(restored.parameterValue("outputTrim"), -3.0f)
              && juce::roundToInt(restored.parameterValue("subSplit")) == 1
              && juce::approximatelyEqual(restored.parameterValue("depth"), 130.0f)
              && restored.slotFilled(5) && restored.historyMode() == 64,
          "every 0.11 setting, slot and the history choice return with the project");
    // A schema 9 project (0.10.2) loaded into an instance with changed 0.11
    // controls returns them all to neutral and uses the full 128-second history.
    auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    xml->setAttribute("engineVersion", 9);
    xml->removeAttribute("historyMode");
    xml->removeAttribute("editorScale");
    // A 0.10.2 project has none of the 0.11 parameters or slots.
    const juce::StringArray added {"engage", "engageQuantize", "retrigger", "phraseEvery", "patternSlot", "slotChain",
                                   "wetMix", "outputTrim", "subSplit", "depth", "lookback"};
    std::vector<juce::XmlElement*> removed;
    for (auto* child : xml->getChildIterator())
        if (child->hasTagName("SLOT") || added.contains(child->getStringAttribute("id"))) removed.push_back(child);
    for (auto* node : removed) xml->removeChildElement(node, true);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*xml, old);
    restored.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
    check(restored.isEngaged() && juce::roundToInt(restored.parameterValue("phraseEvery")) == 0
              && juce::approximatelyEqual(restored.parameterValue("depth"), 100.0f)
              && juce::approximatelyEqual(restored.parameterValue("wetMix"), 100.0f)
              && !restored.slotFilled(5) && restored.historyMode() == 128,
          "a 0.10 project opens with the 0.11 controls neutral, no slots and 128 s of history");
    restored.maintainHistory();
}

// Screenshots of every new state, for visual inspection and the release notes.
inline void renderFeatureScreens(const juce::File& directory)
{
    directory.createDirectory();
    const auto save = [&directory](BackScratchEditor& editor, const char* name) {
        const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 2.0f);
        const auto file = directory.getChildFile(juce::String("BACK-SCRATCH-") + name + ".png");
        file.deleteFile();
        juce::FileOutputStream output(file);
        check(output.openedOk() && juce::PNGImageFormat().writeImageToStream(image, output), "feature screenshot saved");
    };
    const auto render = [&](const char* name, const auto& configure, const auto& interact) {
        processorTests::Head head;
        BackScratchProcessor processor;
        processor.setPlayHead(&head);
        processor.prepareToPlay(48000.0, 512);
        configure(processor, head);
        BackScratchEditor editor(processor);
        editor.setVisible(true);
        interact(editor, processor);
        gridEditorInteractionTests::pumpMenuCallbacks();
        save(editor, name);
        editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        gridEditorInteractionTests::pumpMessagesUntil([] { return juce::Component::getCurrentlyModalComponent() == nullptr; });
    };
    const auto none = [](auto&, auto&) {};
    const auto busy = [](BackScratchProcessor& processor, processorTests::Head& head) {
        processor.copyPatternToSlot(0, 1);
        processor.copyPatternToSlot(0, 2);
        processor.copyPatternToSlot(0, 4);
        processor.setChoice("slotChain", 2);
        processor.selectSlot(2);
        processor.setEngaged(false);
        processor.setValue("retrigger", 1.0f);
        processor.setChoice("phraseEvery", 2);
        processor.setChoice("lookback", 2);
        processor.setValue("wetMix", 70.0f);
        processor.setValue("depth", 140.0f);
        processor.setValue("outputTrim", -3.0f);
        processor.setChoice("subSplit", 1);
        processor.setHistoryMode(64);
        processBlocks(processor, head, 4);
    };
    render("perform", none, [](BackScratchEditor& editor, auto&) { click(editor, Control::pagePerform); });
    render("perform-busy", busy, [](BackScratchEditor& editor, auto&) { click(editor, Control::pagePerform); });
    render("standby", [](BackScratchProcessor& processor, auto&) { processor.setEngaged(false); }, none);
    render("waiting", [](BackScratchProcessor& processor, processorTests::Head& head) {
        processor.setChoice("phraseEvery", 2);
        processBlocks(processor, head, 40);
    }, none);
    render("roll", [](BackScratchProcessor& processor, auto&) { processor.selectFactoryPreset(0); },
           [](BackScratchEditor& editor, auto&) {
               gridEditorInteractionTests::chooseMenuItem(editor, Control::drawOptions, "Accelerating Roll");
               gridEditorInteractionTests::stroke(editor, gridEditorInteractionTests::gridPoint(8.0f, 0.5f),
                                                  gridEditorInteractionTests::gridPoint(16.0f, 0.5f));
               const auto hover = gridEditorInteractionTests::gridPoint(4.0f, 0.5f);
               editor.mouseMove(event(editor, hover, hover));
           });
    render("tools-menu-rolls", none, [](BackScratchEditor& editor, auto&) { click(editor, Control::drawOptions); });
    render("shapes-menu-rolls", none, [](BackScratchEditor& editor, auto&) { click(editor, Control::shapes); });
    render("presets-menu", none, [](BackScratchEditor& editor, auto&) { click(editor, Control::presets); });
    render("size-menu", none, [](BackScratchEditor& editor, auto&) { editor.showSizeMenu(); });
    render("slot-menu", busy, [](BackScratchEditor& editor, auto&) { click(editor, Control::pagePerform); editor.showSlotMenu(5); });
    render("every-menu", none, [](BackScratchEditor& editor, auto&) { click(editor, Control::pagePerform); click(editor, Control::every); });
    for (const auto* name : {"Bar 4 Rewind", "Accelerating Roll", "4 Bar Story", "Stop Half Bar"}) {
        const auto& bank = bankPresets();
        int index = -1;
        for (std::size_t i = 0; i < bank.size(); ++i) if (bank[i].name == name) index = static_cast<int>(i);
        render((juce::String("preset-") + juce::String(name).replace(" ", "-")).toRawUTF8(),
               [index](BackScratchProcessor& processor, auto&) { processor.selectBankPreset(index); }, none);
    }
    render("slot-b", busy, [](BackScratchEditor&, BackScratchProcessor& processor) { processor.selectSlot(1); });
    render("size-125", none, [](BackScratchEditor& editor, auto&) { editor.applyScale(1.25f); });
}

} // namespace backScratch::featureUiTests

inline void runFeatureUiTests()
{
    using namespace backScratch::featureUiTests;
    checkLayout();
    checkPerformPage();
    checkRollTool();
    checkSizeMenu();
    checkPerformanceAudio();
    checkPresets();
    checkStateRoundTrip();
    std::cout << "BACK-SCRATCH 0.11 PERFORM page, Engage snap, Retrigger, Play Every, slots and chains, Mix/Depth/Output, roll tool, "
                 "Size menu, factory bank, user preset folders and project round trips passed\n";
}
