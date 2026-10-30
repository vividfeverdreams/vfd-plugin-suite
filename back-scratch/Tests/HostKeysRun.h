#pragma once

// Suite 1.6.2 "hands off the keyboard" (shared/VfdHostKeys.h) and the status line, on the real editor made by
// the real processor. The key checks open the editor in an off-screen window and send keys and clicks through
// its peer, the path a host's events take (shared/Tests/HostKeysTest.h), then check what BACK-SCRATCH adds:
// arrows into a selected slot, Shift-arrows and an idle Escape for the host, Escape disarming a tool.
//
//   BackScratchHostSmoke --host-keys   (also part of the normal run; prints "skip" without a display)

#include "BackScratchProcessor.h"
#include "BackScratchEditor.h"
#include "BinaryData.h"
#include "PresetBank.h"
#include "ScratchPattern.h"
#include "Tests/HostKeysTest.h"
#include <array>
#include <iostream>
#include <memory>
#include <vector>

namespace backScratch::hostKeysRun {

using Control = BackScratchEditor::Control;

inline juce::Point<int> editorPoint(const BackScratchEditor& editor, juce::Point<float> design)
{
    return design.transformedBy(editor.designToEditor()).roundToInt();
}

// What BACK-SCRATCH adds to the shared run, through the same kind of window.
inline int checkSlotArrowsAndEscape(BackScratchEditor& editor, BackScratchProcessor& processor, std::ostream& out)
{
    int failures = 0;
    const auto expect = [&](bool ok, const juce::String& what) {
        out << (ok ? "  ok   " : "  FAIL ") << what << "\n";
        if (!ok) ++failures;
    };
    const auto pump = [](int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); };
    juce::Component holder;
    holder.setSize(editor.getWidth(), editor.getHeight());
    holder.addAndMakeVisible(editor);
    holder.setTopLeftPosition(-6000, -6000);
    holder.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    holder.setVisible(true);
    pump(300);
    auto* peer = holder.getPeer();
    expect(peer != nullptr && editor.isShowing(), "editor window opens again");
    if (peer != nullptr && editor.isShowing()) {
        const auto press = [peer](int key, juce::ModifierKeys mods = {}) { return peer->handleKeyPress(juce::KeyPress(key, mods, 0)); };
        const auto click = [&](juce::Point<float> design) {
            const auto at = holder.getLocalPoint(&editor, editorPoint(editor, design)).toFloat();
            const auto t = juce::Time::currentTimeMillis();
            peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, at,
                                   juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), juce::MouseInputSource::defaultPressure,
                                   juce::MouseInputSource::defaultOrientation, t);
            peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, at, juce::ModifierKeys(),
                                   juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, t + 30);
            pump(30);
        };
        const auto& bank = bankPresets();
        click({130.0f, 800.0f});
        // Slot B selected: the right arrow loads the next browser entry into it, as the next key does.
        processor.clearSlot(1);
        processor.selectSlot(1);
        const int before = processor.selectedBankPreset();
        expect(press(juce::KeyPress::rightKey), "the right arrow is kept with slot B selected");
        const int loaded = processor.selectedBankPreset();
        expect(processor.slotFilled(1) && processor.canUndoPatternEdit(), "the right arrow loads a preset into slot B");
        expect(loaded >= 0 && loaded != before
                   && editor.statusText() == "LOADED: " + juce::String(bank[static_cast<std::size_t>(loaded)].name),
               "the screen says which preset landed in the slot ('" + editor.statusText() + "')");
        processor.undoPatternEdit();
        expect(!processor.slotFilled(1), "one Undo empties slot B again");
        processor.selectSlot(0);
        // Modified arrows and an Escape with nothing to cancel go to the host.
        const int index = processor.selectedBankPreset();
        expect(!press(juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier)
                   && !press(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier)
                   && !press(juce::KeyPress::downKey, juce::ModifierKeys::ctrlModifier)
                   && processor.selectedBankPreset() == index,
               "Shift-, Alt- and Ctrl-arrows reach the host and leave the preset");
        expect(!press(juce::KeyPress::escapeKey), "Escape reaches the host when nothing is being cancelled");
        // An armed drawing tool: Escape disarms it (and is kept), Cmd-Z still reaches the host.
        click(BackScratchEditor::controlBounds(Control::pencil).getCentre());
        expect(juce::Component::getCurrentlyFocusedComponent() == &editor, "clicking the pencil focuses only the editor");
        expect(!press('z', juce::ModifierKeys::commandModifier), "Cmd-Z reaches the host while the pencil is armed");
        expect(press(juce::KeyPress::escapeKey), "Escape is kept while it disarms the pencil");
        expect(!press(juce::KeyPress::escapeKey), "the next Escape reaches the host");
        expect(!press(juce::KeyPress::spaceKey) && !press(juce::KeyPress::returnKey), "space and Return still reach the host");
    }
    holder.removeFromDesktop();
    holder.removeChildComponent(&editor);
    return failures;
}

inline int run()
{
    BackScratchProcessor processor;
    processor.prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> created(processor.createEditorAndMakeActive());
    auto* editor = dynamic_cast<BackScratchEditor*>(created.get());
    if (editor == nullptr) {
        std::cout << "HOST KEYS: FAIL (no BACK-SCRATCH editor)\n";
        return 1;
    }
    const auto& bank = bankPresets();
    vfd::test::HostKeysProbe probe;
    // The browser entry the arrows step from: the preset name once one is browsed. A generated pattern
    // (RANDOMIZE, clicked below) keeps the arrows' place in the browser, so this, not "New Scratch", is
    // what the left arrow returns to. The run checks below that the screen shows that entry's name.
    probe.currentPreset = [&processor, &bank] {
        const int index = processor.selectedBankPreset();
        return index >= 0 && index < static_cast<int>(bank.size()) ? juce::String(bank[static_cast<std::size_t>(index)].name)
                                                                    : processor.currentPresetName();
    };
    probe.blankSpot = editorPoint(*editor, {130.0f, 800.0f}); // red shell and PCB art left of the controls
    probe.controlSpots = {editorPoint(*editor, BackScratchEditor::controlBounds(Control::phraseDuration).getCentre()),
                          editorPoint(*editor, BackScratchEditor::controlBounds(Control::randomize).getCentre()),
                          editorPoint(*editor, BackScratchEditor::controlBounds(Control::dotted).getCentre())};
    int failures = vfd::test::runHostKeys(*editor, probe, std::cout);
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr) {
        const int index = processor.selectedBankPreset();
        const bool shown = index >= 0 && processor.currentPresetName() == juce::String(bank[static_cast<std::size_t>(index)].name);
        std::cout << (shown ? "  ok   " : "  FAIL ") << "the screen shows the preset the arrows loaded ('"
                  << processor.currentPresetName() << "')\n";
        failures += shown ? 0 : 1;
        failures += checkSlotArrowsAndEscape(*editor, processor, std::cout);
        std::cout << "BACK-SCRATCH HOST KEYS: " << (failures == 0 ? "ALL PASSED" : juce::String(failures) + " FAILED") << "\n";
    }
    created.reset();
    return failures;
}

inline juce::String longestPresetName()
{
    juce::String longest;
    for (const auto& preset : bankPresets())
        if (juce::String(preset.name).length() > longest.length()) longest = preset.name;
    return longest;
}

// BS-F3: the status line never squeezes text below 90% width. Every fixed message, and LOADED: with the
// longest factory name, fits one line or wraps onto two without being cut, on both screen pages.
inline juce::StringArray longStatusMessages()
{
    juce::StringArray messages {
        "The pattern is updating. Try saving again.", "Couldn't save this preset. Choose a writable folder.",
        "Choose a BACK-SCRATCH preset file.", "This isn't a BACK-SCRATCH preset.", "This preset version isn't supported.",
        "This preset contains an invalid motion version.", "This preset contains invalid timing or shift settings.",
        "This preset contains invalid settings.", "This preset contains an invalid edited pattern.",
        "This preset contains invalid 0.11 settings.", "The pattern is updating. Try loading again.",
        "FREE DRAW / FOLLOW YOUR HAND", "LINE / DRAG TWO POINTS", "CURVE / DRAG A SWEEP", "ACCELERATING ROLL / DRAG ACROSS",
        "REVERSE ROLL 1/16 / DRAG ACROSS", "CHOPPED ROLL 1/16 / DRAG ACROSS", "ROLL 1/16T / DRAG ACROSS",
        "ERASE / DRAG TO RESTORE AUDIO", "SLOT H / EMPTY / LIVE INPUT / CHAIN PLAYING H",
        "GENERATED PATTERN / LIVE INPUT / CHAIN PLAYING H", "EDITED PATTERN / LIVE INPUT / CHAIN PLAYING H"};
    messages.add("LOADED: " + longestPresetName());
    messages.add("SAVED: " + longestPresetName());
    for (int shape = 0; shape < scratchShapeCount; ++shape) messages.add(juce::String(scratchShapeName(shape)).toUpperCase());
    for (const auto& folder : bankFolders())
        messages.add(juce::String(folder).replace("/", " / ").toUpperCase() + " / LIVE INPUT / CHAIN PLAYING H");
    for (const auto& preset : factoryPresets())
        messages.addIfNotAlreadyThere(juce::String(preset.category) + " / 75 OF 75 / LIVE INPUT / CHAIN PLAYING H");
    return messages;
}

inline int checkStatusLines(std::ostream& out)
{
    const auto face = juce::Typeface::createSystemTypefaceFor(BinaryData::ChakraPetchSemiBold_ttf,
                                                               BinaryData::ChakraPetchSemiBold_ttfSize);
    const juce::Font font(juce::FontOptions(face).withHeight(BackScratchEditor::statusFontHeight));
    int failures = 0, wrapped = 0, checked = 0;
    for (const auto& text : longStatusMessages()) {
        for (const bool perform : {false, true}) {
            const auto lines = BackScratchEditor::statusLayout(font, text, perform);
            // Every line fits its box at 90% width or more, so nothing is squeezed further or cut.
            const float width = BackScratchEditor::statusBounds(perform, lines.size() > 1).getWidth();
            bool fits = lines.size() >= 1 && lines.size() <= 2 && lines.joinIntoString(" ") == text;
            for (const auto& line : lines)
                fits = fits && juce::GlyphArrangement::getStringWidth(font, line) * BackScratchEditor::statusMinimumScale <= width;
            ++checked;
            wrapped += lines.size() > 1 ? 1 : 0;
            if (!fits) {
                ++failures;
                out << "  FAIL status line '" << text << "' on " << (perform ? "PERFORM" : "PATTERN") << " is squeezed or cut\n";
            }
        }
    }
    out << "  " << (failures == 0 ? "ok   " : "FAIL ") << checked << " status lines at 90% width or more ("
        << wrapped << " wrapped onto two lines, none cut)\n";
    return failures;
}

// Screens with the longest messages, for the release notes and visual QC.
inline void renderStatusScreens(const juce::File& directory)
{
    const std::array<std::pair<const char*, juce::String>, 5> shots {{
        {"status-error", "This preset contains invalid timing or shift settings."},
        {"status-chain", "GENERATED PATTERN / LIVE INPUT / CHAIN PLAYING H"},
        {"status-loaded", "LOADED: " + longestPresetName()},
        {"status-roll", "ACCELERATING ROLL / DRAG ACROSS"},
        {"status-short", "LOADED: Bar 4 Rewind"}}};
    for (const auto& [name, text] : shots) {
        BackScratchProcessor processor;
        processor.prepareToPlay(48000.0, 512);
        BackScratchEditor editor(processor);
        editor.setVisible(true);
        editor.showStatus(text, juce::String(name) == "status-error");
        const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 2.0f);
        const auto file = directory.getChildFile(juce::String("BACK-SCRATCH-") + name + ".png");
        file.deleteFile();
        juce::FileOutputStream output(file);
        if (!output.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, output))
            throw std::runtime_error("status screenshot saved");
    }
}

} // namespace backScratch::hostKeysRun
