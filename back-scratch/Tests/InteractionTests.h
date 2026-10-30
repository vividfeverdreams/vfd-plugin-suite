#pragma once

#include "BackScratchEditor.h"
#include "ScratchPattern.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace backScratch::interactionTests {

inline void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: editor interaction: " << message << '\n';
        std::exit(1);
    }
}

inline juce::MouseEvent event(BackScratchEditor& editor, juce::Point<float> point,
                              juce::Point<float> downPoint, bool dragged = false)
{
    const auto scale = [&editor](juce::Point<float> p) {
        return p.transformedBy(editor.designToEditor());
    };
    const auto time = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), scale(point),
             juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
             1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &editor, &editor, time,
             scale(downPoint), time, 1, dragged };
}

inline void click(BackScratchEditor& editor, BackScratchEditor::Control control)
{
    const auto point = BackScratchEditor::controlBounds(control).getCentre();
    editor.mouseDown(event(editor, point, point));
    editor.mouseUp(event(editor, point, point));
}

inline juce::MouseEvent knobEvent(juce::Slider& knob, juce::Point<float> position,
                                 juce::Point<float> down, bool dragging, int modifiers)
{
    static juce::int64 clock = juce::Time::currentTimeMillis();
    const juce::Time time(++clock);
    return {juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys(modifiers),
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &knob, &knob, time, down, time, 1, dragging};
}

inline void dragKnob(juce::Slider& knob, float pixelsUp, bool fine = false)
{
    const auto down = knob.getLocalBounds().toFloat().getCentre();
    const auto position = down.translated(0, -pixelsUp);
    const int modifiers = juce::ModifierKeys::leftButtonModifier | (fine ? juce::ModifierKeys::shiftModifier : 0);
    knob.mouseDown(knobEvent(knob, down, down, false, modifiers));
    knob.mouseDrag(knobEvent(knob, position, down, true, modifiers));
    knob.mouseUp(knobEvent(knob, position, down, true, 0));
}

inline void wheelKnob(juce::Slider& knob, bool up)
{
    const auto position = knob.getLocalBounds().toFloat().getCentre();
    juce::MouseWheelDetails wheel {};
    wheel.deltaY = up ? 0.1f : -0.1f;
    knob.mouseWheelMove(knobEvent(knob, position, position, false, 0), wheel);
}

struct GestureCounter final : juce::AudioProcessorParameter::Listener {
    void parameterValueChanged(int, float) override { ++changes; }
    void parameterGestureChanged(int, bool starting) override { starting ? ++begins : ++ends; }
    int begins = 0, ends = 0, changes = 0;
};

inline void checkControlLayout(const BackScratchEditor& editor)
{
    using Control = BackScratchEditor::Control;
    constexpr std::array controls {Control::randomize, Control::keepSub, Control::phraseDuration,
                                   Control::phraseLock, Control::dotted, Control::triplet,
                                   Control::shiftLeft, Control::shiftRight, Control::randomPreset, Control::previous,
                                   Control::next, Control::presets, Control::save, Control::load};
    const auto viewport = BackScratchEditor::designViewport();
    // Reserve the physical enclosure's top-right indicator, including its glow.
    // This checks hit geometry; the release screenshots separately check artwork.
    const juce::Rectangle<float> hardwareIndicator {904.0f, 94.0f, 50.0f, 50.0f};
    for (size_t i = 0; i < controls.size(); ++i) {
        const auto bounds = BackScratchEditor::controlBounds(controls[i]);
        check(!bounds.isEmpty() && viewport.contains(bounds),
              "every interactive control fits inside the cropped enclosure");
        check(!bounds.intersects(hardwareIndicator),
              "the enclosure indicator remains free of interactive controls");
        const auto scaled = bounds.transformedBy(editor.designToEditor());
        check(editor.getLocalBounds().toFloat().expanded(0.01f).contains(scaled),
              "control hit regions stay inside the editor after scaling");
        for (size_t j = i + 1; j < controls.size(); ++j)
            check(!bounds.intersects(BackScratchEditor::controlBounds(controls[j])),
                  "interactive controls do not share overlapping hit regions");
    }
    const auto left = BackScratchEditor::controlBounds(Control::shiftLeft);
    const auto right = BackScratchEditor::controlBounds(Control::shiftRight);
    check(std::abs((left.getCentreX() + right.getCentreX()) * 0.5f - 562.0f) < 0.01f
              && left.getY() >= 519.0f && right.getBottom() <= 550.0f,
          "pattern shift arrows are centered in the graph footer beside the phrase label");
}

inline juce::Component* findNamed(juce::Component& parent, const juce::String& name)
{
    for (auto* child : parent.getChildren()) {
        if (child->getName() == name) return child;
        if (auto* found = findNamed(*child, name)) return found;
    }
    return nullptr;
}

inline void checkInfoInteraction(BackScratchEditor& editor, BackScratchProcessor& processor)
{
    auto* button = dynamic_cast<juce::Button*>(findNamed(editor, "INFO"));
    auto* overlay = findNamed(editor, "VFD Info Page");
    check(button != nullptr && overlay != nullptr, "INFO key and credits page exist");
    check(!overlay->isVisible(), "credits page starts closed");
    check(editor.getLocalBounds().contains(button->getBounds()), "INFO key stays within the shell");
    for (const auto control : {BackScratchEditor::Control::randomize, BackScratchEditor::Control::keepSub,
                               BackScratchEditor::Control::phraseDuration, BackScratchEditor::Control::save,
                               BackScratchEditor::Control::load})
        check(!button->getBounds().toFloat().intersects(BackScratchEditor::controlBounds(control)
                  .transformedBy(editor.designToEditor())), "INFO key clears existing controls");
    std::vector<float> parameters;
    for (auto* parameter : processor.getParameters()) parameters.push_back(parameter->getValue());
    button->onClick();
    check(overlay->isVisible() && overlay->getBounds() == editor.getLocalBounds(),
          "INFO opens a credits page filling the editor");
    // Since 1.6.2 the shared page passes everything but Escape and the plain
    // arrows to the host; it holds the arrows so presets can't change under it.
    check(!overlay->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)) && overlay->isVisible(),
          "credits page passes space to the host and stays open");
    check(overlay->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)) && overlay->isVisible(),
          "credits page holds the arrow keys so the preset can't change under it");
    const auto countLinks = [](const auto& self, juce::Component& parent) -> int {
        int count = dynamic_cast<juce::HyperlinkButton*>(&parent) != nullptr ? 1 : 0;
        for (auto* child : parent.getChildren()) count += self(self, *child);
        return count;
    };
    check(countLinks(countLinks, *overlay) == 4, "credits page contains exactly four external links");
    for (const auto& [label, address] : std::array<std::pair<const char*, const char*>, 4> {{
             { "@cxy", "https://x.com/cxy" }, { "@clt", "https://x.com/clt" },
             { "vividfeverdreams.xyz", "https://www.vividfeverdreams.xyz" },
             { "Dream Sequence on Fractured Atlas", "https://fundraising.fracturedatlas.org/dream-sequence" } }}) {
        auto* link = dynamic_cast<juce::HyperlinkButton*>(findNamed(*overlay, label));
        check(link != nullptr && link->getURL().toString(false) == address,
              "credits link uses its approved destination");
    }
    check(overlay->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) && !overlay->isVisible(),
          "Escape closes the credits page");
    button->onClick();
    auto* close = dynamic_cast<juce::Button*>(findNamed(*overlay, "Close info"));
    check(close != nullptr, "credits page exposes its close control");
    close->onClick();
    check(!overlay->isVisible(), "close button dismisses the credits page");
    for (int index = 0; index < processor.getParameters().size(); ++index)
        check(processor.getParameters()[index]->getValue() == parameters[static_cast<size_t>(index)],
              "opening and closing credits preserves every host parameter");
}

} // namespace backScratch::interactionTests

inline void runInteractionTests(backScratch::BackScratchProcessor& processor)
{
    using namespace backScratch;
    using namespace backScratch::interactionTests;
    using Control = BackScratchEditor::Control;

    auto editor = std::make_unique<BackScratchEditor>(processor);
    editor->setVisible(true); // Off-screen visibility is required for child hit testing.
    checkControlLayout(*editor);
    checkInfoInteraction(*editor, processor);
    processor.selectFactoryPreset(0);
    processor.setKeepSub(true);
    processor.setPhraseDurationChoice(3);
    processor.setPhraseTimingChoice(0);
    processor.setHostParameter("patternShift", 0);
    processor.setPhraseLocked(true);
    click(*editor, Control::randomize);
    auto seed = processor.generationSeed();
    auto factory = processor.selectedFactoryPreset();
    check(processor.isGenerated(), "randomize composes a generated phrase instead of selecting a factory preset");
    check(processor.keepSub(), "randomization keeps the chosen sub mode");
    click(*editor, Control::randomize);
    check(processor.generationSeed() != seed && processor.isGenerated(),
          "repeated randomization creates a new generated phrase");

    click(*editor, Control::keepSub);
    check(!processor.keepSub(), "the single KEEP SUB toggle turns preservation off");
    click(*editor, Control::randomize);
    check(!processor.keepSub(), "randomization does not silently restore the sub");
    click(*editor, Control::keepSub);
    check(processor.keepSub(), "the same KEEP SUB toggle restores low-end preservation");

    auto* knob = dynamic_cast<juce::Slider*>(editor->getComponentAt(
        BackScratchEditor::controlBounds(Control::phraseDuration).getCentre()
            .transformedBy(editor->designToEditor()).roundToInt()));
    check(knob != nullptr && knob->getComponentID() == "phrase-duration",
          "the duration knob occupies the visible control location after cropping");
    GestureCounter gestures;
    auto* duration = processor.parameters().getParameter("phraseDuration");
    duration->addListener(&gestures);
    processor.setPhraseDurationChoice(0);
    check(knob->getValue() == 0.0, "the duration knob follows host parameter changes");
    for (int i = 0; i < BackScratchProcessor::phraseDurationCount; ++i) {
        if (i != 0) wheelKnob(*knob, true);
        check(processor.phraseDurationChoice() == i, "the knob reaches every duration from a half beat through 16 bars");
        click(*editor, Control::randomize);
        check(processor.phraseDurationChoice() == i && processor.phraseLocked(),
              "a locked phrase keeps its length when randomizing");
        check(std::abs(processor.currentPattern().beats - processor.phraseBeats()) < 1.0e-9,
              "the displayed phrase duration matches the actual generated pattern");
    }
    wheelKnob(*knob, true);
    check(processor.phraseDurationChoice() == BackScratchProcessor::phraseDurationCount - 1,
          "the longest duration is a stop rather than wrapping");
    processor.setPhraseDurationChoice(0);
    dragKnob(*knob, 36.0f);
    const int normalDrag = processor.phraseDurationChoice();
    check(normalDrag > 0, "dragging the knob upwards selects a longer duration");
    processor.setPhraseDurationChoice(0);
    dragKnob(*knob, 36.0f, true);
    check(processor.phraseDurationChoice() < normalDrag, "Shift-drag offers finer duration control");
    processor.setPhraseDurationChoice(4);
    dragKnob(*knob, -36.0f);
    check(processor.phraseDurationChoice() < 4, "dragging down selects a shorter duration");
    processor.setPhraseDurationChoice(0);
    wheelKnob(*knob, false);
    check(processor.phraseDurationChoice() == 0 && std::abs(processor.phraseBeats() - 0.5) < 1.0e-9,
          "the shortest duration stops at a half beat");
    const auto knobCentre = knob->getLocalBounds().toFloat().getCentre();
    knob->mouseDoubleClick(knobEvent(*knob, knobCentre, knobCentre, false, 0));
    check(processor.phraseDurationChoice() == 3, "double-click restores a one-bar phrase");
    check(gestures.begins > 0 && gestures.begins == gestures.ends && gestures.changes > 0,
          "knob and wheel editing send balanced host automation gestures");
    duration->removeListener(&gestures);

    GestureCounter timingGestures, shiftGestures;
    auto* timing = processor.parameters().getParameter("phraseTiming");
    auto* patternShift = processor.parameters().getParameter("patternShift");
    timing->addListener(&timingGestures);
    patternShift->addListener(&shiftGestures);
    const auto originalPattern = processor.currentPattern();
    const auto originalSeed = processor.generationSeed();
    click(*editor, Control::dotted);
    check(processor.phraseTimingChoice() == 1 && std::abs(processor.phraseBeats() - 6.0) < 1.0e-9,
          "the dotted button stretches a one-bar phrase to six beats");
    check(processor.generationSeed() == originalSeed && processor.currentPattern().count == originalPattern.count,
          "the timing modifier retimes the current pattern without recomposing it");
    click(*editor, Control::triplet);
    check(processor.phraseTimingChoice() == 2 && std::abs(processor.phraseBeats() - 8.0 / 3.0) < 1.0e-9,
          "triplet replaces dotted timing with two thirds of the knob length");
    click(*editor, Control::triplet);
    check(processor.phraseTimingChoice() == 0 && std::abs(processor.phraseBeats() - 4.0) < 1.0e-9,
          "pressing the lit timing button restores straight timing");
    click(*editor, Control::dotted);
    click(*editor, Control::randomize);
    check(processor.phraseTimingChoice() == 1 && processor.phraseDurationChoice() == 3,
          "the phrase lock preserves both the knob value and timing modifier");
    click(*editor, Control::dotted);
    check(processor.phraseTimingChoice() == 0, "dotted can also be turned off with its own button");
    click(*editor, Control::shiftLeft);
    check(processor.patternShiftSteps() == patternGridDivisions - 1,
          "nudging left from zero wraps to the final grid cell");
    click(*editor, Control::shiftRight);
    check(processor.patternShiftSteps() == 0, "the right arrow reverses a left nudge");
    const auto beforeShift = processor.currentPattern();
    const auto seedBeforeShift = processor.generationSeed();
    click(*editor, Control::shiftRight);
    check(processor.patternShiftSteps() == 1 && processor.currentPattern().gridOffset == 1,
          "the right arrow shifts the actual audible pattern by one visible cell");
    const auto shifted = processor.currentPattern();
    for (int sample = 0; sample < 63; ++sample) {
        const double beat = (sample + 0.37) / 63.0 * beforeShift.beats;
        const auto before = evaluatePattern(beforeShift, beat);
        const auto after = evaluatePattern(shifted, beat + patternGridStepBeats(beforeShift));
        check(std::abs(before.delayBeats - after.delayBeats) < 1.0e-8
                  && std::abs(before.gate - after.gate) < 1.0e-5
                  && std::abs(before.playbackRate - after.playbackRate) < 1.0e-8,
              "a nudge moves the complete gesture, including its curve and gate, without changing it");
    }
    for (int step = 1; step < patternGridDivisions; ++step) click(*editor, Control::shiftRight);
    check(processor.patternShiftSteps() == 0 && processor.generationSeed() == seedBeforeShift,
          "sixteen right nudges return to the same pattern and seed");
    const auto nudgePoint = BackScratchEditor::controlBounds(Control::shiftRight).getCentre();
    const auto offControl = BackScratchEditor::designViewport().getTopLeft().translated(2.0f, 2.0f);
    editor->mouseDown(event(*editor, nudgePoint, nudgePoint));
    editor->mouseDrag(event(*editor, offControl, nudgePoint, true));
    editor->mouseUp(event(*editor, offControl, nudgePoint, true));
    check(processor.patternShiftSteps() == 0, "dragging off a nudge cancels the edit");
    const auto dottedPoint = BackScratchEditor::controlBounds(Control::dotted).getCentre();
    editor->mouseDown(event(*editor, dottedPoint, dottedPoint));
    editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    editor->mouseUp(event(*editor, dottedPoint, dottedPoint));
    check(processor.phraseTimingChoice() == 0, "Escape cancels a pending timing modifier edit");
    check(timingGestures.begins >= 5 && timingGestures.begins == timingGestures.ends
              && shiftGestures.begins >= 18 && shiftGestures.begins == shiftGestures.ends,
          "timing and pattern nudge buttons send balanced host automation gestures");
    timing->removeListener(&timingGestures);
    patternShift->removeListener(&shiftGestures);
    click(*editor, Control::phraseLock);
    check(!processor.phraseLocked(), "the length lock can be released");
    click(*editor, Control::randomize);
    check(processor.phraseDurationChoice() >= 0
              && processor.phraseDurationChoice() < BackScratchProcessor::phraseDurationCount,
          "unlocked randomization chooses a supported phrase length");
    click(*editor, Control::phraseLock);
    check(processor.phraseLocked(), "the length lock can be restored");
    const auto lockedLength = processor.phraseDurationChoice();
    click(*editor, Control::randomize);
    check(processor.phraseDurationChoice() == lockedLength, "restoring the lock preserves the current phrase");

    processor.selectFactoryPreset(0);
    click(*editor, Control::shiftLeft);
    check(processor.patternShiftSteps() == patternGridDivisions - 1,
          "bottom left arrow shifts the current factory pattern");
    // Since 0.11 the arrows walk the whole factory browser in menu order. With
    // nothing browsed yet they start from the Classic entry of the current
    // original recipe.
    const auto& bank = bankPresets();
    const int bankSize = static_cast<int>(bank.size());
    const int classicStart = classicBankIndex(0);
    check(classicStart > 0, "the original Sub Tug has an exact Classic entry");
    click(*editor, Control::previous);
    check(processor.selectedBankPreset() == classicStart - 1 && processor.patternShiftSteps() == 0,
          "previous steps to the preceding browser entry at its original grid position");
    click(*editor, Control::shiftRight);
    check(processor.patternShiftSteps() == 1,
          "bottom right arrow shifts the current pattern");
    click(*editor, Control::next);
    check(processor.selectedBankPreset() == classicStart && processor.selectedFactoryPreset() == 0
              && processor.patternShiftSteps() == 0 && !processor.hasCustomPattern(),
          "next returns to the exact Classic recipe at its original grid position");
    for (int step = 1; step <= bankSize; ++step) {
        const int expected = (classicStart + step) % bankSize;
        click(*editor, Control::shiftRight);
        click(*editor, Control::next);
        const auto& preset = bank[static_cast<size_t>(expected)];
        check(processor.selectedBankPreset() == expected && processor.patternShiftSteps() == 0,
              "every browser preset is reachable in order and restores its original grid position");
        if (preset.classic >= 0)
            check(processor.selectedFactoryPreset() == preset.classic && processor.variationSeed() == 0 && !processor.hasCustomPattern(),
                  "classic entries select their original recipe");
        else
            check(processor.hasCustomPattern() && processor.currentPresetName() == juce::String(preset.name),
                  "new entries load their own drawn pattern and name");
        check(processor.currentPattern().count > 0, "every selected preset has a live time pattern");
    }
    check(processor.selectedBankPreset() == classicStart, "the arrows wrap around the whole browser");

    seed = processor.generationSeed();
    const auto button = BackScratchEditor::controlBounds(Control::randomize).getCentre();
    const auto outside = BackScratchEditor::designViewport().getTopLeft().translated(2.0f, 2.0f);
    editor->mouseDown(event(*editor, button, button));
    check(processor.generationSeed() == seed, "randomize activates on release rather than mouse-down");
    editor->mouseDrag(event(*editor, outside, button, true));
    editor->mouseUp(event(*editor, outside, button, true));
    check(processor.generationSeed() == seed, "dragging off the button cancels randomization");
    editor->mouseDown(event(*editor, outside, outside));
    editor->mouseUp(event(*editor, button, outside, true));
    check(processor.generationSeed() == seed, "release without pressing the button does nothing");

    factory = processor.selectedFactoryPreset();
    check(!editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)),
          "space passes through so the DAW can start and stop transport");
    check(processor.generationSeed() == seed && processor.selectedFactoryPreset() == factory,
          "space leaves the pattern unchanged");
    // The plain arrows are handled by the editor's HostKeys listener (checked
    // through the window's peer in the host-keys run), never by keyPressed.
    check(!editor->keyPressed(juce::KeyPress(juce::KeyPress::leftKey))
              && !editor->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)),
          "keyPressed leaves the arrows to the preset listener");
    check(!editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys::commandModifier, 0)),
          "modified shortcuts are left to the host");
    for (const auto key : {juce::KeyPress(juce::KeyPress::returnKey), juce::KeyPress(juce::KeyPress::tabKey),
                           juce::KeyPress('a'), juce::KeyPress(juce::KeyPress::escapeKey),
                           juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0),
                           juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)})
        check(!editor->keyPressed(key), "Return, Tab, letters, Cmd-Z, Shift-Cmd-Z and an idle Escape are left to the host");
    check(processor.generationSeed() == seed && processor.selectedFactoryPreset() == factory,
          "keys left to the host leave the pattern unchanged");
    check(processor.generationSeed() == seed, "modified space leaves the pattern alone");
    editor->mouseDown(event(*editor, button, button));
    editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    editor->mouseUp(event(*editor, button, button));
    check(processor.generationSeed() == seed, "Escape cancels a pending button press");

    check(editor->getWidth() <= 700 && editor->getWidth() >= 420
              && std::abs(editor->getHeight() - editor->getWidth() * 942.0 / 700.0) <= 1.0,
          "the default editor shows the enclosure edge to edge at its fixed aspect ratio");
    check(editor->isResizable(), "hosts can resize the editor");
    editor->setSize(560, 754);
    checkControlLayout(*editor);
    checkInfoInteraction(*editor, processor);
    check(editor->getLocalBounds().contains(knob->getBounds()), "the duration knob remains inside the cropped editor");
    processor.setPhraseDurationChoice(2);
    wheelKnob(*knob, true);
    check(processor.phraseDurationChoice() == 3, "the knob still edits its host parameter after resize");
    click(*editor, Control::keepSub);
    check(!processor.keepSub(), "hit regions remain correct at a different editor size");
    click(*editor, Control::triplet);
    check(processor.phraseTimingChoice() == 2, "timing buttons remain aligned after resizing");
    click(*editor, Control::shiftLeft);
    check(processor.patternShiftSteps() == patternGridDivisions - 1,
          "pattern arrows remain aligned after resizing");
    click(*editor, Control::shiftRight);
    click(*editor, Control::triplet);
    GestureCounter closingGesture;
    duration->addListener(&closingGesture);
    const auto closingPoint = knob->getLocalBounds().toFloat().getCentre();
    knob->mouseDown(knobEvent(*knob, closingPoint, closingPoint, false, juce::ModifierKeys::leftButtonModifier));
    check(closingGesture.begins == 1 && closingGesture.ends == 0, "a held knob begins an automation gesture");
    editor.reset();
    check(closingGesture.begins == closingGesture.ends, "closing the editor while dragging ends the host gesture");
    duration->removeListener(&closingGesture);
    check(!processor.keepSub() && processor.generationSeed() == seed,
          "closing the editor preserves the active variation and sub mode");
    processor.selectFactoryPreset(0);
    processor.setKeepSub(true);
    std::cout << "BACK-SCRATCH control layout, indicator clearance, duration knob, dotted/triplet timing, circular pattern nudges, crop, generation, phrase lock, sub toggle, preset navigation and editor lifecycle checks passed\n";
}
