#pragma once

// 0.5.4 editor input: host keys (SH-F1), typed values and tooltips (SH-F2), the preset list's wheel and
// the favourite star (SH-F3).
//
//   runEditorInputTests()  no window needed; part of --feature-tests
//   runHostKeysTests()     the real editor in an off-screen window, keys and clicks sent through its peer:
//                          ShiftyEditorSnapshot --host-keys (also part of --feature-tests). Without a
//                          display it prints "skip" and passes.

#include "FeatureTests.h"
#include "../Source/UI/ShiftyEditor.h"
#include "Tests/HostKeysTest.h"

#include <functional>
#include <iostream>
#include <optional>
#include <vector>

namespace shifty::tests {

namespace input {

/** Design (shell art) coordinates to the editor: the crop starts at (57, 73) and is 1008 x 1317. */
inline juce::Point<int> editorPoint(const juce::Component& editor, float x, float y)
{
    return { juce::roundToInt((x - 57.0f) * static_cast<float>(editor.getWidth()) / 1008.0f),
             juce::roundToInt((y - 73.0f) * static_cast<float>(editor.getHeight()) / 1317.0f) };
}

inline juce::Component* findNamed(juce::Component& root, const juce::String& name)
{
    if (root.getName() == name) return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findNamed(*child, name)) return found;
    return nullptr;
}

inline void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

/** A left click through the editor's window, as the host delivers it. */
inline void click(juce::Component& editor, juce::Point<int> local)
{
    auto* peer = editor.getPeer();
    if (peer == nullptr) return;
    const auto at = peer->getComponent().getLocalPoint(&editor, local).toFloat();
    const auto t = juce::Time::currentTimeMillis();
    peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, at,
                           juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), juce::MouseInputSource::defaultPressure,
                           juce::MouseInputSource::defaultOrientation, t);
    peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, at, juce::ModifierKeys(),
                           juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, t + 30);
    pump(30);
}

inline juce::String loadedKey(const ShiftyProcessor& processor)
{
    return processor.currentPresetFolder() + "/" + processor.currentPresetName();
}

inline juce::String bankKey(size_t index)
{
    const auto& preset = ShiftyProcessor::factoryPresets()[index];
    return juce::String(preset.folder) + "/" + preset.name;
}

inline juce::MouseEvent wheelEvent(ShiftyEditor& editor, juce::Point<float> at)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(),
             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
             juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
             juce::MouseInputSource::defaultTiltY, &editor, &editor, now, at, now, 0, false };
}

inline juce::File scratchRoot(const char* name)
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile(juce::String(name) + "-" + juce::String(juce::Time::currentTimeMillis()));
}

} // namespace input

namespace {

// SH-F1 without a window: the arrows' order, wrap-around, filters, highlight and Undo, through stepPreset
// (the function the arrow keys call).
void checkArrowBrowsing(Checks& checks)
{
    using namespace input;
    const auto root = scratchRoot("shifty-arrows");
    {
        ShiftyProcessor host;
        host.setUserDataRootForTesting(root);
        host.saveUserPreset("Arrow One");
        host.saveUserPreset("Arrow Two", "Keys");
        ShiftyEditor editor(host);
        const auto size = ShiftyEditor::sizeFor(1.0);
        editor.setSize(size.x, size.y);
        const auto& bank = ShiftyProcessor::factoryPresets();
        const int total = static_cast<int>(bank.size());
        const auto visible = [&editor] {
            const int selected = editor.selectedPresetForTesting(), first = editor.firstVisiblePresetForTesting();
            return selected >= first && selected < first + 4;
        };

        // FACTORY, all folders: the list is the whole bank in its order, the 24 originals included.
        editor.selectPresetFilterForTesting(0, {});
        bool listIsBank = editor.presetCountForTesting() == total;
        for (int i = 0; listIsBank && i < total; ++i)
            listIsBank = editor.presetItemForTesting(i) == bankKey(static_cast<size_t>(i));
        checks.require(listIsBank, "FACTORY with all folders lists the whole bank in order, originals included");
        host.loadFactoryPreset(0);
        editor.refreshForTesting();
        bool forward = true, highlight = true, originals = false;
        for (int i = 1; i <= total; ++i) {
            editor.stepPresetForTesting(1);
            forward = forward && loadedKey(host) == bankKey(static_cast<size_t>(i % total));
            highlight = highlight && editor.selectedPresetForTesting() == i % total && visible();
            originals = originals || host.isOriginalSound();
        }
        checks.require(forward, "the next-preset arrow steps through the list in order and wraps to the top");
        checks.require(highlight, "the list's highlight follows the loaded preset and stays in view");
        checks.require(originals, "the arrows reach the original 0.4.2 sounds when the list shows them");
        bool backward = true;
        for (int i = 1; i <= total; ++i) {
            editor.stepPresetForTesting(-1);
            backward = backward && loadedKey(host) == bankKey(static_cast<size_t>((total - i) % total));
        }
        checks.require(backward, "the previous-preset arrow steps back in order and wraps to the bottom");

        // Each arrow is one Undo step, as a double-click is.
        const auto before = loadedKey(host);
        editor.stepPresetForTesting(1);
        const auto after = loadedKey(host);
        host.undo();
        const bool undone = loadedKey(host) == before;
        host.redo();
        checks.require(undone && loadedKey(host) == after, "an arrow load is one Undo step");

        // A folder: only its presets, in order, wrapping. Growl holds no original sound; Classic holds only them.
        for (const char* folder : { "Growl", "Classic" }) {
            std::vector<int> expected;
            for (int i = 0; i < total; ++i)
                if (juce::String(bank[static_cast<size_t>(i)].folder) == folder) expected.push_back(i);
            editor.selectPresetFilterForTesting(0, folder);
            host.loadFactoryPreset(expected.front());
            editor.refreshForTesting();
            bool inOrder = editor.presetCountForTesting() == static_cast<int>(expected.size());
            bool onlyOriginals = true, noOriginals = true;
            for (size_t i = 1; i <= expected.size(); ++i) {
                editor.stepPresetForTesting(1);
                inOrder = inOrder && loadedKey(host) == bankKey(static_cast<size_t>(expected[i % expected.size()]));
                onlyOriginals = onlyOriginals && host.isOriginalSound();
                noOriginals = noOriginals && !host.isOriginalSound();
            }
            checks.require(inOrder, juce::String("with the ") + folder + " folder chosen the arrows stay in it, in order, wrapping");
            checks.require(juce::String(folder) == "Classic" ? onlyOriginals : noOriginals,
                           juce::String("the ") + folder + " folder's arrows include the originals only if the list does");
        }

        // A search: only the matching names.
        if (auto* search = dynamic_cast<juce::TextEditor*>(findNamed(editor, "Search presets"))) {
            // TextEditor reports typing asynchronously; refreshing the list reads the search at once.
            search->setText("throat", false);
            editor.selectPresetFilterForTesting(0, {});
            bool matches = editor.presetCountForTesting() > 1;
            for (int i = 0; i < editor.presetCountForTesting() + 1; ++i) {
                editor.stepPresetForTesting(1);
                matches = matches && host.currentPresetName().containsIgnoreCase("throat");
            }
            checks.require(matches, "with a search the arrows step only through the names it finds");
            search->setText({}, false);
            editor.selectPresetFilterForTesting(0, {});
        } else {
            checks.require(false, "the PRESETS page has its search field");
        }

        // USER: the saved files, in the list's order. The loaded factory sound is not in it, so the
        // arrows start at the top (next) or the bottom (previous).
        const auto files = host.userPresetFiles();
        editor.selectPresetFilterForTesting(1, {});
        editor.stepPresetForTesting(1);
        const bool firstUser = host.currentPresetName() == files[0].getFileNameWithoutExtension();
        editor.stepPresetForTesting(1);
        const bool secondUser = host.currentPresetName() == files[1].getFileNameWithoutExtension();
        editor.stepPresetForTesting(1);
        checks.require(files.size() == 2 && firstUser && secondUser
                           && host.currentPresetName() == files[0].getFileNameWithoutExtension(),
                       "the USER tab's arrows load the saved presets in list order and wrap");
        editor.selectPresetFilterForTesting(0, "Growl");
        editor.stepPresetForTesting(-1);
        checks.require(host.currentPresetFolder() == "Growl" && editor.selectedPresetForTesting() == editor.presetCountForTesting() - 1,
                       "from a sound the list doesn't show, the previous arrow starts at the bottom");

        // A RANDOM variation is not in the list: the arrows go on from the highlighted row.
        editor.selectPresetFilterForTesting(0, {});
        host.loadFactoryPreset(10);
        editor.refreshForTesting();
        host.randomizeParameters();
        editor.refreshForTesting();
        const bool variation = host.currentPresetName() == "Random Variation";
        editor.stepPresetForTesting(1);
        const bool nextAfter = loadedKey(host) == bankKey(11);
        host.randomizeParameters();
        editor.refreshForTesting();
        editor.stepPresetForTesting(-1);
        checks.require(variation && nextAfter && loadedKey(host) == bankKey(10),
                       "after RANDOM the arrows step from the preset the list highlights");

        // The save panel holds the arrows: the sound being saved can't change under it.
        editor.showPageForTesting(ShiftyEditor::Page::presets);
        editor.beginPresetSaveForTesting();
        const auto saving = loadedKey(host);
        editor.stepPresetForTesting(1);
        checks.require(loadedKey(host) == saving, "the arrows don't change the sound while a preset is being saved");
        checks.require(editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)), "Escape closes the save panel");
        checks.require(!editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)), "Escape with nothing to cancel is the host's");
        editor.stepPresetForTesting(1);
        checks.require(loadedKey(host) != saving, "the arrows browse again once the panel is closed");

        // The editor's own key handling keeps nothing else (the arrows are hostKeys').
        const bool undoBefore = host.canUndo();
        const auto shown = loadedKey(host);
        bool released = true;
        for (const auto& key : { juce::KeyPress(juce::KeyPress::returnKey), juce::KeyPress(juce::KeyPress::spaceKey, {}, ' '),
                                 juce::KeyPress(juce::KeyPress::tabKey), juce::KeyPress('a', {}, 'a'),
                                 juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0),
                                 juce::KeyPress('z', juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier), 0) })
            released = !editor.keyPressed(key) && released;
        checks.require(released && loadedKey(host) == shown && host.canUndo() == undoBefore,
                       "Return, space, Tab, letters, Cmd-Z and Shift-Cmd-Z are left to the host and change nothing");
    }
    root.deleteRecursively();
    checks.section("arrow keys: the PRESETS list as shown, in order, wrapping, one Undo step each; other keys are the host's");
}

// SH-F2: typed values read their units and vowel names; tooltips name controls in plain English.
void checkTypedValues(Checks& checks)
{
    using namespace input;
    struct Case { const char* id; const char* text; std::optional<double> value; };
    const Case cases[] {
        { "yoiTime", "1 s", 1000.0 }, { "yoiTime", "250ms", 250.0 }, { "yoiTime", "0.5 s", 500.0 },
        { "yoiTime", "300", 300.0 }, { "envRelease", "2 sec", 2000.0 }, { "envAttack", "1,5 ms", 1.5 },
        { "envAttack", "12 MS", 12.0 },
        { "vowelMorph", "AH", 2.0 }, { "vowelMorph", "ee", 4.0 }, { "vowelMorph", "OO", 0.0 },
        { "vowelMorph", "OH-AH", 1.5 }, { "vowelMorph", "3", 3.0 },
        { "mix", "50%", 0.5 }, { "mix", "50", 0.5 }, { "envAmount", "+30%", 0.3 }, { "envAmount", "-20", -0.2 },
        { "subCut", "120 Hz", 120.0 }, { "subCut", "0.1 kHz", 100.0 }, { "motionFreeHz", "0.5hz", 0.5 },
        { "shift", "-4 st", -4.0 }, { "yoiShift", "+2.5", 2.5 }, { "output", "-3 dB", -3.0 },
        { "yoiTime", "fast", std::nullopt }, { "yoiTime", "1 x", std::nullopt }, { "mix", "", std::nullopt },
        { "vowelMorph", "AX", std::nullopt }, { "output", "3 st", std::nullopt }, { "shift", "-", std::nullopt },
    };
    for (const auto& c : cases) {
        const auto parsed = ShiftyEditor::parseTypedValue(c.id, c.text);
        const bool ok = parsed.has_value() == c.value.has_value()
                        && (!parsed.has_value() || std::abs(*parsed - *c.value) < 1.0e-9);
        checks.require(ok, juce::String("typing '") + c.text + "' into " + c.id + " reads as "
                               + (c.value.has_value() ? juce::String(*c.value) : juce::String("nothing"))
                               + " (got " + (parsed.has_value() ? juce::String(*parsed) : juce::String("nothing")) + ")");
    }

    // Through the real value boxes: one Undo step each, units and vowel names understood.
    ShiftyProcessor host;
    const auto root = scratchRoot("shifty-typing");
    host.setUserDataRootForTesting(root);
    {
        ShiftyEditor editor(host);
        const auto size = ShiftyEditor::sizeFor(1.0);
        editor.setSize(size.x, size.y);
        const auto type = [&editor](const juce::String& labelName, const juce::String& text) {
            auto* label = dynamic_cast<juce::Label*>(findNamed(editor, labelName));
            if (label == nullptr) return false;
            label->showEditor();
            auto* field = label->getCurrentTextEditor();
            if (field == nullptr) return false;
            field->setText(text, false);
            label->hideEditor(false);
            return true;
        };
        const auto raw = [&host](const char* id) { return static_cast<double>(host.parameters().getRawParameterValue(id)->load()); };
        editor.showPageForTesting(ShiftyEditor::Page::motion, ShiftyEditor::MotionTab::yoi);
        const bool typed = type("YOI sweep time value", "1 s");
        checks.require(typed && std::abs(raw("yoiTime") - 1000.0) < 0.01, "typing 1 s into the YOI time sets 1000 ms, not 1 ms");
        checks.require(host.canUndo(), "a typed value is an Undo step");
        type("YOI sweep time value", "250ms");
        checks.require(std::abs(raw("yoiTime") - 250.0) < 0.01, "typing 250ms sets 250 ms");
        type("YOI sweep time value", "fast");
        checks.require(std::abs(raw("yoiTime") - 250.0) < 0.01, "text that is not a time changes nothing");
        editor.showPageForTesting(ShiftyEditor::Page::voice);
        host.setEngine(EngineMode::talk, false);
        type("Vowel value", "AH");
        checks.require(std::abs(raw("vowelMorph") - 2.0) < 0.002, "typing AH into the vowel picks AH, not OO");
        type("Vowel value", "eh");
        checks.require(std::abs(raw("vowelMorph") - 3.0) < 0.002, "typing eh picks EH");

        // Tooltips: plain-English names, never parameter IDs such as motionFreeHz.
        bool plain = true;
        for (const char* id : { "shift", "focus", "bite", "drive", "mix", "subCut", "output", "motionDepth", "motionFreeHz",
                                "envAmount", "envAttack", "envRelease", "yoiAmount", "yoiTime", "yoiShift", "vowelMorph",
                                "character", "talk" }) {
            const auto name = ShiftyEditor::displayNameFor(id);
            auto* label = dynamic_cast<juce::Label*>(findNamed(editor, name + " value"));
            auto* slider = editor.sliderForTesting(id);
            const bool ok = label != nullptr && slider != nullptr && name != id
                            && label->getTooltip().startsWith(name + ": double click to type a value")
                            && !label->getTooltip().contains(id) && slider->getTooltip().startsWith(name);
            if (!ok) std::cerr << "  tooltip for " << id << ": '" << (label != nullptr ? label->getTooltip() : juce::String("none")) << "'\n";
            plain = plain && ok;
        }
        checks.require(plain, "value boxes and controls name themselves in plain English");
        auto* rate = dynamic_cast<juce::Label*>(findNamed(editor, "LFO rate value"));
        checks.require(rate != nullptr && rate->getTooltip() == "LFO rate: double click to type a value in Hz",
                       "the LFO rate box says what it takes");
        auto* time = dynamic_cast<juce::Label*>(findNamed(editor, "YOI sweep time value"));
        checks.require(time != nullptr && time->getTooltip().contains("0.5 s"), "time boxes say they take seconds too");
    }
    root.deleteRecursively();
    checks.section("typed values: units (s, ms, %, Hz, kHz, dB, st) and vowel names; plain-English tooltips");
}

// SH-F3: the list scrolls by the wheel's own distance; the star needs a saved preset.
void checkPresetWheelAndStar(Checks& checks)
{
    using namespace input;
    ShiftyProcessor host;
    const auto root = scratchRoot("shifty-wheel");
    host.setUserDataRootForTesting(root);
    {
        ShiftyEditor editor(host);
        const auto size = ShiftyEditor::sizeFor(1.0);
        editor.setSize(size.x, size.y);
        editor.showPageForTesting(ShiftyEditor::Page::presets);
        editor.selectPresetFilterForTesting(0, {});
        const auto over = editorPoint(editor, 500.0f, 370.0f).toFloat();
        const auto wheel = [&](float dx, float dy, bool smooth, int times = 1) {
            for (int i = 0; i < times; ++i)
                editor.mouseWheelMove(wheelEvent(editor, over), juce::MouseWheelDetails { dx, dy, false, smooth, false });
            return editor.firstVisiblePresetForTesting();
        };
        wheel(0.0f, 1.0f, false, 20); // to the top
        checks.require(editor.firstVisiblePresetForTesting() == 0, "the wheel scrolls the list to the top");
        // Rows at 100% are 35 * 849 / 1317 = 22.6 px; a macOS trackpad pixel is 0.5 / 256 of wheel delta.
        const float pixel = 0.5f / 256.0f;
        checks.require(wheel(0.0f, -pixel, true, 30) == 1, "30 px of trackpad scroll moves the list one row, not 30");
        checks.require(wheel(0.0f, -3.0f * pixel, true, 20) == 3, "momentum keeps following the fingers (90 px, about 4 rows)");
        checks.require(wheel(0.6f, 0.0f, true, 10) == 3 && wheel(0.3f, 0.0f, false, 3) == 3, "sideways swipes leave the list alone");
        checks.require(wheel(0.0f, -10.0f / 256.0f, false) == 4, "a macOS wheel notch moves one row");
        checks.require(wheel(0.0f, -60.0f / 256.0f, false) == 7, "a Windows wheel notch moves three rows");
        checks.require(wheel(0.0f, 10.0f / 256.0f, false) == 6, "a notch up moves one row up");
        checks.require(wheel(0.0f, 30.0f * pixel, true) == 5, "turning back on the trackpad starts from the new direction");
        const int last = juce::jmax(0, editor.presetCountForTesting() - 4);
        checks.require(wheel(0.0f, -2000.0f * pixel, true, 10) == last, "a long fling stops at the end of the list");
        checks.require(wheel(0.0f, 30.0f * pixel, true) == last - 1, "travel stored against the end doesn't delay the way back");

        // The star on SOUND: only a saved or factory preset can be a favourite.
        editor.showPageForTesting(ShiftyEditor::Page::sound);
        auto* star = dynamic_cast<juce::Button*>(findNamed(editor, "Favorite"));
        checks.require(star != nullptr && star->isVisible() && star->isEnabled() && star->getTooltip().contains("FAV"),
                       "the star is live for a factory preset");
        if (star != nullptr) {
            const int favorites = editor.favoriteCountForTesting();
            host.randomizeParameters();
            editor.refreshForTesting();
            checks.require(host.currentPresetName() == "Random Variation" && !star->isEnabled()
                               && star->getTooltip().contains("saved"),
                           "after RANDOM the star is off, and its tooltip says a preset must be saved first");
            star->onClick(); // what a click would run
            checks.require(editor.favoriteCountForTesting() == favorites, "the star ignores an unsaved Random Variation");
            host.saveUserPreset("Random Variation");
            editor.refreshForTesting();
            checks.require(host.currentPresetFolder() == "User" && star->isEnabled(), "saved under that name, it can be a favourite");
            host.loadFactoryPreset(5);
            editor.refreshForTesting();
            checks.require(star->isEnabled(), "a factory preset can be a favourite");
            editor.showPageForTesting(ShiftyEditor::Page::presets);
            checks.require(!star->isVisible(), "the star shows on SOUND only");
        }
    }
    root.deleteRecursively();
    checks.section("preset list: wheel and trackpad scroll by their distance; the star needs a saved preset");
}

} // namespace

inline int runEditorInputTests()
{
    Checks checks;
    checkArrowBrowsing(checks);
    checkTypedValues(checks);
    checkPresetWheelAndStar(checks);
    if (checks.failures == 0)
        std::cout << "SHIFTY 0.5.4 editor input tests passed.\n";
    else
        std::cerr << "SHIFTY 0.5.4 editor input tests failed: " << checks.failures << " checks.\n";
    return checks.failures == 0 ? 0 : 1;
}

// SH-F1 with a window: the shared host-keys check on SHIFTY's real editor, then SHIFTY's own key routing.
inline int runHostKeysTests()
{
    using namespace input;
    int failures = 0;
    const auto expect = [&failures](bool ok, const juce::String& what) {
        std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
        if (!ok) ++failures;
    };
    const auto root = scratchRoot("shifty-hostkeys");
    {
        ShiftyProcessor processor;
        processor.setUserDataRootForTesting(root);
        processor.loadFactoryPreset(ShiftyProcessor::factoryPresetIndex("Talk", "Rubber Throat"));
        std::unique_ptr<juce::AudioProcessorEditor> owner(processor.createEditorAndMakeActive());
        auto* editorPointer = dynamic_cast<ShiftyEditor*>(owner.get());
        if (editorPointer == nullptr) { std::cout << "  FAIL the processor creates a SHIFTY editor\n"; return 1; }
        auto& editor = *editorPointer;
        const auto size = ShiftyEditor::sizeFor(1.0);
        editor.setSize(size.x, size.y);

        // RANDOM is one of the controls clicked. With every lock closed its click changes nothing, so the
        // shared check's arrows come back to the preset they started on; RANDOM itself is tried below.
        setAllLocks(processor, true);
        vfd::test::HostKeysProbe probe;
        probe.currentPreset = [&processor] { return processor.currentPresetFolder() + " / " + processor.currentPresetName(); };
        probe.blankSpot = editorPoint(editor, 110.0f, 980.0f); // the shell's left edge, beside the SHIFT knob
        const auto engine = processor.engineMode();
        const float segment = engine == EngineMode::talk ? 0.5f : engine == EngineMode::mouth ? 1.5f : 2.5f;
        probe.controlSpots = { editorPoint(editor, 351.0f, 755.0f),                          // the SHIFT knob
                               editorPoint(editor, 433.0f, 1168.0f),                         // RANDOM
                               editorPoint(editor, 540.0f + 400.0f * segment / 3.0f, 222.0f) }; // the engine selector, on the engine in use
        probe.openTextField = [&editor]() -> juce::TextEditor* {
            // As a user would: the PRESETS tab, then a click in the search field.
            click(editor, editorPoint(editor, 878.0f, 137.0f));
            auto* search = dynamic_cast<juce::TextEditor*>(findNamed(editor, "Search presets"));
            if (search == nullptr || !search->isShowing()) return nullptr;
            click(editor, editor.getLocalPoint(search, search->getLocalBounds().getCentre()));
            return search;
        };
        failures += vfd::test::runHostKeys(editor, probe, std::cout);

        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr) {
            std::cout << "SHIFTY host keys\n";
            setAllLocks(processor, false);
            juce::Component holder;
            holder.setSize(editor.getWidth(), editor.getHeight());
            holder.addAndMakeVisible(editor);
            holder.setTopLeftPosition(-6000, -6000);
            holder.addToDesktop(juce::ComponentPeer::windowIsTemporary);
            holder.setVisible(true);
            pump(600);
            auto* peer = holder.getPeer();
            expect(peer != nullptr && editor.isShowing(), "editor window opens again");
            if (peer != nullptr && editor.isShowing()) {
                const auto press = [peer](int key, juce::ModifierKeys mods = {}) {
                    const auto text = key == juce::KeyPress::spaceKey ? juce::juce_wchar(' ')
                                      : (key >= 'a' && key <= 'z' && !mods.isCommandDown()) ? juce::juce_wchar(key) : juce::juce_wchar(0);
                    const bool used = peer->handleKeyPress(juce::KeyPress(key, mods, text));
                    pump(20);
                    return used;
                };
                const auto focused = [] { return juce::Component::getCurrentlyFocusedComponent(); };
                const auto blank = editorPoint(editor, 110.0f, 980.0f);
                const auto listIndex = [&editor, &processor] {
                    for (int i = 0; i < editor.presetCountForTesting(); ++i)
                        if (editor.presetItemForTesting(i) == loadedKey(processor)) return i;
                    return -1;
                };
                // The shared check typed a space into the search; start from an empty one.
                auto* search = dynamic_cast<juce::TextEditor*>(findNamed(editor, "Search presets"));
                if (search != nullptr) search->setText({}, true);
                editor.selectPresetFilterForTesting(0, {});
                click(editor, blank);

                // The arrows browse on every page: right and down are next, left and up previous.
                bool everyPage = true;
                for (const auto page : { ShiftyEditor::Page::sound, ShiftyEditor::Page::voice, ShiftyEditor::Page::motion,
                                         ShiftyEditor::Page::presets }) {
                    editor.showPageForTesting(page);
                    const int start = listIndex();
                    const int count = editor.presetCountForTesting();
                    const bool right = press(juce::KeyPress::rightKey) && listIndex() == (start + 1) % count;
                    const bool down = press(juce::KeyPress::downKey) && listIndex() == (start + 2) % count;
                    const bool up = press(juce::KeyPress::upKey) && listIndex() == (start + 1) % count;
                    const bool left = press(juce::KeyPress::leftKey) && listIndex() == start;
                    everyPage = everyPage && start >= 0 && right && down && up && left;
                }
                expect(everyPage, "on SOUND, VOICE, MOTION and PRESETS, right/down load the next preset and left/up the previous");

                // Modified arrows are the host's.
                const auto shown = loadedKey(processor);
                bool modifiedPass = true;
                for (const int mod : { juce::ModifierKeys::shiftModifier, juce::ModifierKeys::commandModifier,
                                       juce::ModifierKeys::altModifier, juce::ModifierKeys::ctrlModifier })
                    for (const int key : { juce::KeyPress::leftKey, juce::KeyPress::rightKey, juce::KeyPress::upKey, juce::KeyPress::downKey })
                        modifiedPass = !press(key, juce::ModifierKeys(mod)) && modifiedPass;
                expect(modifiedPass && loadedKey(processor) == shown, "Shift, Cmd, Alt and Ctrl arrows reach the host and change nothing");
                expect(!press(juce::KeyPress::escapeKey), "Escape reaches the host when nothing is being cancelled");

                // Cmd-Z is Live's: SHIFTY doesn't undo. Its own UNDO and REDO keys still do.
                press(juce::KeyPress::rightKey);
                const auto loaded = loadedKey(processor);
                expect(!press('z', juce::ModifierKeys::commandModifier) && loadedKey(processor) == loaded,
                       "Cmd-Z reaches the host and doesn't undo inside SHIFTY");
                expect(!press('z', juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier))
                           && loadedKey(processor) == loaded,
                       "Shift-Cmd-Z reaches the host and doesn't redo inside SHIFTY");
                click(editor, editorPoint(editor, 751.0f, 1158.0f)); // UNDO
                pump(100); // the editor's timer enables REDO
                const bool undone = loadedKey(processor) == shown;
                click(editor, editorPoint(editor, 823.0f, 1158.0f)); // REDO
                pump(100);
                expect(undone && loadedKey(processor) == loaded && focused() == &editor,
                       "the on-screen UNDO and REDO keys still undo and redo, and leave only the editor focused");

                // PRESETS: clicking a row highlights it, Return doesn't load it, double-clicking does.
                editor.showPageForTesting(ShiftyEditor::Page::presets);
                pump(juce::MouseEvent::getDoubleClickTimeout() + 50);
                const int shownRow = listIndex() - editor.firstVisiblePresetForTesting();
                const int visibleRow = shownRow == 2 ? 1 : 2; // a row that is not the loaded preset
                const auto rowSpot = editorPoint(editor, 500.0f, 302.0f + 35.0f * static_cast<float>(visibleRow) + 17.0f);
                click(editor, rowSpot);
                const int row = editor.firstVisiblePresetForTesting() + visibleRow;
                const auto rowKey = editor.presetItemForTesting(row);
                const bool highlighted = editor.selectedPresetForTesting() == row && rowKey != loadedKey(processor);
                expect(highlighted && !press(juce::KeyPress::returnKey) && loadedKey(processor) == loaded,
                       "Return reaches the host and no longer loads the highlighted row");
                pump(juce::MouseEvent::getDoubleClickTimeout() + 50);
                click(editor, rowSpot);
                click(editor, rowSpot);
                expect(loadedKey(processor) == rowKey, "double-clicking a row still loads it");
                pump(juce::MouseEvent::getDoubleClickTimeout() + 50);

                // The search field: typing keeps the keys; Return gives them back, and the arrows browse what it found.
                if (search != nullptr) {
                    click(editor, editor.getLocalPoint(search, search->getLocalBounds().getCentre()));
                    const bool typing = focused() == search && press('t') && press('h');
                    const auto beforeReturn = loadedKey(processor);
                    press(juce::KeyPress::returnKey);
                    expect(typing && search->getText() == "th" && focused() == &editor && loadedKey(processor) == beforeReturn,
                           "Return in the search field ends the search without loading a row (focus on the editor)");
                    const bool browsed = press(juce::KeyPress::rightKey) && processor.currentPresetName().containsIgnoreCase("th");
                    expect(browsed, "after the search the arrows browse the names it found");
                    search->setText({}, true);
                } else {
                    expect(false, "the PRESETS page has its search field");
                }

                // The save panel: its name field keeps the keys; with the panel open the arrows load nothing, and
                // Escape closes it even after a click elsewhere let go of the field.
                editor.beginPresetSaveForTesting();
                auto* name = findNamed(editor, "Preset name");
                const bool nameTyping = name != nullptr && focused() == name && press('a');
                click(editor, blank);
                const auto saving = loadedKey(processor);
                press(juce::KeyPress::rightKey);
                const bool held = loadedKey(processor) == saving && focused() == &editor;
                const bool closes = press(juce::KeyPress::escapeKey) && name != nullptr && !name->isVisible();
                expect(nameTyping && held && closes && !press(juce::KeyPress::escapeKey),
                       "the save panel keeps its typing, holds the arrows, and Escape closes it (then Escape is the host's)");

                // RANDOM: a click leaves only the editor focused, and the arrows go on from the list's highlight.
                editor.showPageForTesting(ShiftyEditor::Page::sound);
                editor.selectPresetFilterForTesting(0, {});
                processor.loadFactoryPreset(20);
                editor.refreshForTesting();
                click(editor, editorPoint(editor, 433.0f, 1168.0f));
                const bool variation = processor.currentPresetName() == "Random Variation" && focused() == &editor;
                const auto rolled = valuesOf(processor);
                const bool returnFree = !press(juce::KeyPress::returnKey) && valuesOf(processor) == rolled;
                press(juce::KeyPress::rightKey);
                expect(variation && returnFree && loadedKey(processor) == bankKey(21),
                       "after RANDOM, Return doesn't re-fire it and the next arrow loads the row after the highlight");
                expect(!press(juce::KeyPress::spaceKey) && !press('a') && !press(juce::KeyPress::tabKey),
                       "space, letters and Tab still reach the host");
            }
            holder.removeFromDesktop();
            holder.removeChildComponent(&editor);
        }
        owner.reset();
    }
    root.deleteRecursively();
    std::cout << "SHIFTY HOST KEYS: " << (failures == 0 ? "ALL PASSED" : juce::String(failures) + " FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}

} // namespace shifty::tests
