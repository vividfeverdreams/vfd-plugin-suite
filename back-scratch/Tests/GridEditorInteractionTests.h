#pragma once
#include <functional>
#include <memory>

#include "InteractionTests.h"
#include "PatternEditing.h"
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace backScratch::gridEditorInteractionTests {

inline bool samePlayback(const ScratchPattern& a, const ScratchPattern& b)
{
    if (std::abs(a.beats - b.beats) > 1.0e-9 || a.gridOffset != b.gridOffset) return false;
    for (int i = 0; i < 509; ++i) {
        const auto av = evaluatePattern(a, (i + 0.371) / 509.0 * a.beats);
        const auto bv = evaluatePattern(b, (i + 0.371) / 509.0 * b.beats);
        if (std::abs(av.delayBeats - bv.delayBeats) > 1.0e-8
            || std::abs(av.playbackRate - bv.playbackRate) > 1.0e-8
            || std::abs(av.wet - bv.wet) > 1.0e-6f || std::abs(av.gate - bv.gate) > 1.0e-6f) return false;
    }
    return true;
}

inline juce::Point<float> gridPoint(float cell, float depth)
{
    const auto graph = BackScratchEditor::patternBounds();
    return {graph.getX() + cell / patternGridDivisions * graph.getWidth(),
            graph.getBottom() - depth * graph.getHeight()};
}

inline void stroke(BackScratchEditor& editor, juce::Point<float> start, juce::Point<float> end)
{
    using namespace interactionTests;
    editor.mouseDown(event(editor, start, start));
    editor.mouseDrag(event(editor, end, start, true));
    editor.mouseUp(event(editor, end, start, true));
}

inline void pumpMenuCallbacks()
{
   #if JUCE_MAC
    for (int i = 0; i < 20; ++i)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.001, true);
   #endif
}

// Run the message loop until done() holds, for at most timeoutMs. A loaded
// machine can need far more than a fixed number of passes.
inline bool pumpMessagesUntil(const std::function<bool()>& done, double timeoutMs = 2000.0)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    while (!done() && juce::Time::getMillisecondCounterHiRes() < deadline) {
       #if JUCE_MAC
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, true);
       #else
        juce::Thread::sleep(1);
       #endif
    }
    return done();
}

inline void chooseMenuItem(BackScratchEditor& editor, BackScratchEditor::Control control,
                          const juce::String& title)
{
    using namespace interactionTests;
    click(editor, control);
    auto* popup = juce::Component::getCurrentlyModalComponent();
    check(popup != nullptr && editor.isParentOf(popup), "the dropdown opens as an actual menu inside the editor");
    auto* item = dynamic_cast<juce::PopupMenu::CustomComponent*>(findNamed(*popup, title));
    check(item != nullptr, "the dropdown contains the named selectable tool or shape");
    // Exercise JUCE's real menu-selection and async completion path, including
    // destruction of the popup, rather than calling the editor's action directly.
    item->triggerMenuItem();
    // The menu's completion callback is already queued; a two-hop sentinel
    // posted now runs after it, whatever the machine load.
    auto finished = std::make_shared<bool>(false);
    juce::MessageManager::callAsync([finished] { juce::MessageManager::callAsync([finished] { *finished = true; }); });
    pumpMessagesUntil([finished] { return *finished && juce::Component::getCurrentlyModalComponent() == nullptr; });
    check(juce::Component::getCurrentlyModalComponent() == nullptr, "choosing an item dismisses the dropdown");
}

inline void checkToolbarLayout(const BackScratchEditor& editor)
{
    using Control = BackScratchEditor::Control;
    using interactionTests::check;
    constexpr std::array tools {Control::pencil, Control::drawOptions, Control::shapes, Control::eraser, Control::undo, Control::redo};
    constexpr std::array others {Control::presets, Control::randomPreset, Control::previous, Control::next,
        Control::shiftLeft, Control::shiftRight, Control::save, Control::load,
        Control::phraseDuration, Control::phraseLock, Control::dotted, Control::triplet,
        Control::randomize, Control::keepSub};
    for (size_t i = 0; i < tools.size(); ++i) {
        const auto bounds = BackScratchEditor::controlBounds(tools[i]);
        check(BackScratchEditor::designViewport().contains(bounds), "editing tools stay within the shell");
        check(bounds.getY() >= 190 && bounds.getBottom() < 236,
              "editing tools fit in the display header without moving the graph or preset browser");
        check(!bounds.intersects(BackScratchEditor::patternBounds()), "drawing cannot trigger a toolbar button");
        check(editor.getLocalBounds().toFloat().expanded(0.01f).contains(bounds.transformedBy(editor.designToEditor())),
              "editing hit regions scale with the device");
        for (size_t j = i + 1; j < tools.size(); ++j)
            check(!bounds.intersects(BackScratchEditor::controlBounds(tools[j])), "editing tools have separate hit regions");
        for (auto other : others)
            check(!bounds.intersects(BackScratchEditor::controlBounds(other)), "editing tools clear all existing controls");
    }
}

} // namespace backScratch::gridEditorInteractionTests

inline void runGridEditorInteractionTests()
{
    using namespace backScratch;
    using namespace interactionTests;
    using namespace gridEditorInteractionTests;
    using Control = BackScratchEditor::Control;
    BackScratchProcessor processor;
    processor.prepareToPlay(48000, 512);
    processor.selectFactoryPreset(0);
    processor.setPhraseDurationChoice(3);
    auto editor = std::make_unique<BackScratchEditor>(processor);
    editor->setVisible(true);
    checkToolbarLayout(*editor);
    const auto original = processor.currentPattern();

    const auto start = gridPoint(2.2f, 0.6f), end = gridPoint(8.0f, 0.15f);
    stroke(*editor, start, end);
    check(!processor.hasCustomPattern() && samePlayback(original, processor.currentPattern()),
          "the graph remains read-only until a drawing tool is armed");
    click(*editor, Control::pencil);
    check(!processor.hasCustomPattern(), "selecting the pencil does not alter audio");
    editor->mouseDown(event(*editor, start, start));
    check(processor.hasCustomPattern() && !samePlayback(original, processor.currentPattern()),
          "pencil-down previews an audible scratch immediately");
    editor->mouseDrag(event(*editor, gridPoint(2.3f, 0.4f), start, true));
    const auto adjusted = processor.currentPattern();
    check(evaluatePattern(adjusted, 2.25 * adjusted.beats / patternGridDivisions).delayBeats > 0.0,
          "freehand stays at the drawn height instead of restarting the first cell at the baseline");
    check(std::abs(evaluatePattern(adjusted, 2.05 * adjusted.beats / patternGridDivisions).delayBeats
        - evaluatePattern(original, 2.05 * original.beats / patternGridDivisions).delayBeats) < 1.0e-8,
          "fractional freehand leaves audio before the pointer start untouched");
    editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    editor->mouseUp(event(*editor, start, start));
    check(!processor.hasCustomPattern() && samePlayback(original, processor.currentPattern()),
          "Escape cancels live preview and mouse-up cannot commit it later");
    check(!processor.canUndoPatternEdit(), "canceling a gesture adds no undo step");

    click(*editor, Control::pencil);
    stroke(*editor, start, end);
    const auto drawn = processor.currentPattern();
    check(processor.hasCustomPattern() && validateEditedPattern(drawn),
          "a sparse pointer drag creates a valid complete custom scratch shape");
    check(processor.canUndoPatternEdit(), "one finished stroke exposes undo");
    click(*editor, Control::undo);
    check(samePlayback(original, processor.currentPattern()) && !processor.hasCustomPattern(),
          "one undo restores the entire pre-drag factory phrase");
    check(!processor.canUndoPatternEdit() && processor.canRedoPatternEdit(),
          "many drag previews produce exactly one undo entry");
    click(*editor, Control::redo);
    check(samePlayback(drawn, processor.currentPattern()), "redo restores the complete drawn shape");

    click(*editor, Control::eraser);
    stroke(*editor, gridPoint(3.2f, 0.2f), gridPoint(6.8f, 0.85f));
    const auto erased = processor.currentPattern();
    for (int cell = 3; cell <= 6; ++cell) {
        const auto value = evaluatePattern(erased, (cell + 0.5) * erased.beats / patternGridDivisions);
        check(value.wet == 0.0f && value.gate == 1.0f,
              "eraser restores dry input across every crossed cell rather than muting it");
    }
    for (int cell : {0, 1, 8, 13, 15}) {
        const auto before = evaluatePattern(drawn, (cell + 0.43) * drawn.beats / patternGridDivisions);
        const auto after = evaluatePattern(erased, (cell + 0.43) * erased.beats / patternGridDivisions);
        check(std::abs(before.delayBeats - after.delayBeats) < 1.0e-8
                  && std::abs(before.playbackRate - after.playbackRate) < 1.0e-8,
              "eraser leaves untouched cells and curved playback intact");
    }
    click(*editor, Control::undo);
    check(samePlayback(drawn, processor.currentPattern()), "one undo restores the entire erased span");
    click(*editor, Control::redo);
    check(samePlayback(erased, processor.currentPattern()), "redo restores an eraser gesture");

    const auto outside = BackScratchEditor::patternBounds().getTopLeft().translated(-12, -12);
    stroke(*editor, outside, end);
    check(samePlayback(erased, processor.currentPattern()), "a drag started outside the grid never edits on entry");
    const auto beforeCancel = processor.currentPattern();
    editor->mouseDown(event(*editor, gridPoint(0.3f, 0.5f), gridPoint(0.3f, 0.5f)));
    editor->mouseDrag(event(*editor, gridPoint(15.8f, 0.5f), gridPoint(0.3f, 0.5f), true));
    editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    editor->mouseUp(event(*editor, gridPoint(15.8f, 0.5f), gridPoint(0.3f, 0.5f), true));
    check(samePlayback(beforeCancel, processor.currentPattern()), "Escape restores every cell touched during an eraser drag");

    editor->setSize(560, 754);
    checkToolbarLayout(*editor);
    click(*editor, Control::pencil);
    stroke(*editor, gridPoint(14.6f, 0.35f), gridPoint(9.0f, 0.75f));
    check(validateEditedPattern(processor.currentPattern()) && !samePlayback(beforeCancel, processor.currentPattern()),
          "drawing backwards still edits correctly after the device is resized");
    const auto beforeClose = processor.currentPattern();
    editor->mouseDown(event(*editor, start, start));
    editor->mouseDrag(event(*editor, end, start, true));
    editor.reset();
    check(samePlayback(beforeClose, processor.currentPattern()), "closing the editor cancels an unfinished drawing gesture");

    editor = std::make_unique<BackScratchEditor>(processor);
    editor->setVisible(true);
    click(*editor, Control::eraser);
    stroke(*editor, gridPoint(0.1f, 0.5f), gridPoint(15.9f, 0.5f));
    const auto dry = processor.currentPattern();
    check(dry.editingRange > 0 && std::abs(dry.editingRange - beforeClose.editingRange) < 1.0e-8,
          "erasing the whole pattern retains a stable vertical editing scale");
    for (int cell = 0; cell < patternGridDivisions; ++cell)
        check(evaluatePattern(dry, (cell + 0.5) * dry.beats / patternGridDivisions).wet == 0.0f,
              "erasing every grid cell leaves full dry playback");
    click(*editor, Control::eraser);
    stroke(*editor, start, end);
    check(samePlayback(dry, processor.currentPattern()), "clicking the active tool again returns to safe browsing");
    click(*editor, Control::pencil);
    const auto baseline = gridPoint(4.0f, 0.0f);
    editor->mouseDown(event(*editor, baseline, baseline));
    check(evaluatePattern(processor.currentPattern(), 4.01 * dry.beats / patternGridDivisions).wet == 1.0f,
          "a click exactly on the baseline starts a drawing gesture immediately");
    editor->mouseDrag(event(*editor, gridPoint(8.0f, 0.55f), baseline, true));
    editor->mouseUp(event(*editor, gridPoint(8.0f, 0.55f), baseline, true));
    check(!samePlayback(dry, processor.currentPattern()), "a stroke can rise naturally from the graph baseline");
    click(*editor, Control::undo);
    check(samePlayback(dry, processor.currentPattern()), "a baseline stroke remains one undoable gesture");

    const auto rightEdge = gridPoint(16.0f, 0.6f);
    editor->mouseDown(event(*editor, rightEdge, rightEdge));
    editor->mouseUp(event(*editor, rightEdge, rightEdge));
    const auto lastCell = evaluatePattern(processor.currentPattern(), 15.99 * dry.beats / patternGridDivisions);
    check(lastCell.wet == 1.0f && lastCell.delayBeats > 0.0,
          "a click exactly on the rightmost boundary draws a local mark at the final edge");
    check(evaluatePattern(processor.currentPattern(), 0.5 * dry.beats / patternGridDivisions).wet == 0.0f,
          "drawing on the rightmost boundary does not wrap into the first cell");
    click(*editor, Control::undo);
    check(samePlayback(dry, processor.currentPattern()), "undo restores a right-boundary click");

    editor->setSize(560, 754);
    stroke(*editor, gridPoint(16.0f, 0.0f), gridPoint(12.0f, 0.3f));
    check(!samePlayback(dry, processor.currentPattern()) && validateEditedPattern(processor.currentPattern()),
          "the bottom-right corner starts a valid backwards stroke after scaling");
    click(*editor, Control::undo);
    check(samePlayback(dry, processor.currentPattern()), "corner drawing stays undoable after scaling");
    const auto outsideBaseline = gridPoint(4.0f, 0.0f).translated(0.0f, 0.5f);
    stroke(*editor, outsideBaseline, gridPoint(8.0f, 0.55f));
    check(samePlayback(dry, processor.currentPattern()),
          "including the baseline does not let gestures start in the labels beneath it");
    check(!editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey))
              && !editor->keyPressed(juce::KeyPress(juce::KeyPress::leftKey)),
          "grid editing leaves DAW transport and navigation keys alone");
    // Cmd-Z reaches the host even with a tool armed (the screen's undo and
    // redo keys stay); Escape is kept only while it disarms the tool.
    const auto beforeShortcut = processor.currentPattern();
    const bool couldUndo = processor.canUndoPatternEdit();
    check(!editor->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
              && !editor->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
              && samePlayback(beforeShortcut, processor.currentPattern()) && processor.canUndoPatternEdit() == couldUndo,
          "Cmd-Z and Shift-Cmd-Z go to the host while a drawing tool is armed");
    check(editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)), "Escape disarms the armed drawing tool");
    check(!editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)), "with nothing to cancel, Escape goes to the host");
    click(*editor, Control::pencil);
    // Tool menus select through the actual popup component. A geometric tool
    // always previews from its initial snapshot, so reversing a drag has no trail.
    chooseMenuItem(*editor, Control::drawOptions, "Line");
    check(editor->currentDrawMode() == BackScratchEditor::DrawMode::line, "the split arrow selects and arms Line");
    check(samePlayback(dry, processor.currentPattern()), "choosing a drawing tool alone does not change playback");
    const auto lineStart = gridPoint(3.25f, 0.25f);
    const auto longEnd = gridPoint(12.0f, 0.55f);
    const auto shortEnd = gridPoint(7.6f, 0.38f);
    editor->mouseDown(event(*editor, lineStart, lineStart));
    editor->mouseDrag(event(*editor, longEnd, lineStart, true));
    editor->mouseDrag(event(*editor, shortEnd, lineStart, true));
    editor->mouseUp(event(*editor, shortEnd, lineStart, true));
    const auto linePattern = processor.currentPattern();
    check(validateEditedPattern(linePattern), "Line produces a complete safe custom phrase");
    for (double cell : {2.8, 8.0, 10.0, 13.5})
        check(evaluatePattern(linePattern, cell * linePattern.beats / patternGridDivisions).wet == 0.0f,
              "shortening a Line drag removes the old preview instead of leaving a trail");
    const auto mid1 = evaluatePattern(linePattern, 4.0 * linePattern.beats / patternGridDivisions);
    const auto mid2 = evaluatePattern(linePattern, 6.0 * linePattern.beats / patternGridDivisions);
    check(mid1.wet > 0.9f && std::abs(mid1.playbackRate - mid2.playbackRate) < 1.0e-7,
          "Line creates the straight ramp advertised by its menu preview");
    click(*editor, Control::undo);
    check(samePlayback(dry, processor.currentPattern()), "a Line drag is exactly one undoable action");

    chooseMenuItem(*editor, Control::drawOptions, "Curve");
    check(editor->currentDrawMode() == BackScratchEditor::DrawMode::curve, "the tool menu selects Curve");
    stroke(*editor, lineStart, longEnd);
    const auto curvePattern = processor.currentPattern();
    const auto edgeRate = evaluatePattern(curvePattern, 3.3 * curvePattern.beats / patternGridDivisions).playbackRate;
    const auto middleRate = evaluatePattern(curvePattern, 7.6 * curvePattern.beats / patternGridDivisions).playbackRate;
    check(validateEditedPattern(curvePattern) && std::abs(edgeRate - middleRate) > 0.05,
          "Curve creates a changing speed sweep instead of another straight pitched ramp");
    click(*editor, Control::undo);
    check(samePlayback(dry, processor.currentPattern()), "Curve also restores completely with one undo");

    processor.setPhraseDurationChoice(4);
    processor.setPhraseTimingChoice(2);
    processor.shiftPattern(3);
    const auto beforeShape = processor.currentPattern();
    chooseMenuItem(*editor, Control::shapes, "Baby Scratch");
    const auto starter = processor.currentPattern();
    check(processor.hasCustomPattern() && validateEditedPattern(starter)
              && std::abs(starter.beats - beforeShape.beats) < 1.0e-8 && starter.gridOffset == beforeShape.gridOffset,
          "a starter shape keeps the current phrase length, triplet timing and shift");
    check(!samePlayback(starter, beforeShape) && editor->currentDrawMode() == BackScratchEditor::DrawMode::free,
          "choosing a scratch starter loads the shape and arms Free Draw for further editing");
    stroke(*editor, gridPoint(6.1f, 0.10f), gridPoint(11.1f, 0.45f));
    check(!samePlayback(starter, processor.currentPattern()), "a selected starter can immediately be drawn over");
    click(*editor, Control::undo);
    check(samePlayback(starter, processor.currentPattern()), "one undo restores the starter after freehand refinement");
    click(*editor, Control::undo);
    check(samePlayback(beforeShape, processor.currentPattern()), "a second undo removes the entire starter selection");
    click(*editor, Control::redo);
    check(samePlayback(starter, processor.currentPattern()), "redo restores a selected starter exactly");
    chooseMenuItem(*editor, Control::drawOptions, "Free Draw");
    check(editor->currentDrawMode() == BackScratchEditor::DrawMode::free, "Free Draw is explicitly selectable from the menu");
    click(*editor, Control::shapes);
    auto* popup = juce::Component::getCurrentlyModalComponent();
    check(popup != nullptr, "shape menu opens before cancellation");
    popup->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    pumpMenuCallbacks();
    check(samePlayback(starter, processor.currentPattern()), "dismissing the shape menu leaves the pattern unchanged");
    click(*editor, Control::drawOptions);
    const auto closingPopup = juce::Component::SafePointer<juce::Component>(juce::Component::getCurrentlyModalComponent());
    check(closingPopup != nullptr, "tool menu opens before editor close");
    editor.reset();
    pumpMenuCallbacks();
    check(closingPopup == nullptr && juce::Component::getCurrentlyModalComponent() == nullptr,
          "closing the editor dismisses its popup without a dangling callback or look-and-feel");
    std::cout << "BACK-SCRATCH freehand, line/curve, actual tool/shape menus, live preview, erase, undo/redo, cancellation and scaling checks passed\n";
}
