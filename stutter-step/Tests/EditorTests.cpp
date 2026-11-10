#include "StutterEditor.h"
#include "MidiClip.h"
#include "Tests/HostKeysTest.h"
#include <iostream>

int runStutterHostKeys();
int runStutterWindowEdges (const juce::File& cropsFolder);

namespace {
juce::Component* findControl (juce::Component& editor, const juce::String& name)
{
    for (auto* c : editor.getChildren())
        if (c->getName() == name || c->getTitle() == name) return c;
    return nullptr;
}
}

int runStutterHostKeys()
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) {
        std::cout << "host keys: SKIPPED (no display)\n";
        return 0;
    }
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("stutter-host-keys-" + juce::String (juce::Time::currentTimeMillis()));
    const auto previousRoot = juce::SystemStats::getEnvironmentVariable ("STUTTER_STEP_PRESET_ROOT", {});
    setenv ("STUTTER_STEP_PRESET_ROOT", root.getFullPathName().toRawUTF8(), 1);
    StutterProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditorAndMakeActive());
    auto* save = dynamic_cast<juce::Button*> (findControl (*editor, "Save groove"));
    juce::TextEditor* field = nullptr;
    for (auto* c : editor->getChildren())
        if (auto* t = dynamic_cast<juce::TextEditor*> (c)) field = t;
    if (save == nullptr || field == nullptr) {
        std::cout << "FAIL: real SAVE controls missing\n";
        return 1;
    }
    const auto art = [&] (float x, float y) {
        const float s = (float) editor->getWidth() / StutterEditor::cropW;
        return juce::Point<int> (juce::roundToInt ((x - StutterEditor::cropX) * s),
                                 juce::roundToInt ((y - StutterEditor::cropY) * s));
    };
    const auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (60); };
    const auto openField = [&] {
        save->triggerClick(); // the same SAVE button the user clicks; JUCE dispatches the click asynchronously
        pump();
        return field->isVisible() ? field : nullptr;
    };
    vfd::test::HostKeysProbe probe;
    probe.currentPreset = [&] {
        const auto g = proc.getGrooveInfo();
        return g.name + (g.edited ? " *" : "");
    };
    probe.blankSpot = art (980, 590);
    probe.openTextField = openField;
    probe.controlSpots = { art (350, 818), art (222, 530), art (216, 1130), art (484, 530) };
    int failures = vfd::test::runHostKeys (*editor, probe, std::cout);
    int checks = 0;
    const auto expect = [&] (bool ok, const char* what) {
        ++checks;
        if (! ok) ++failures;
        std::cout << (ok ? "  ok   " : "  FAIL ") << what << '\n';
    };

    juce::Component holder;
    holder.setSize (editor->getWidth(), editor->getHeight());
    holder.addAndMakeVisible (*editor);
    holder.setTopLeftPosition (-6000, -6000);
    holder.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    holder.setVisible (true);
    pump();
    auto* peer = holder.getPeer();
    expect (peer != nullptr && editor->isShowing(), "STUTTER editor regression window opens");
    if (peer != nullptr) {
        const auto press = [&] (int code, juce::ModifierKeys mods = {}) {
            const bool used = peer->handleKeyPress (juce::KeyPress (code, mods, 0));
            pump();
            return used;
        };
        const auto mouse = [&] (juce::Point<int> at, bool down) {
            peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, at.toFloat(),
                down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys(),
                juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                juce::Time::currentTimeMillis());
        };
        const auto click = [&] (juce::Point<int> at) { mouse (at, true); mouse (at, false); pump(); };
        const auto set = [&] (const char* id, float v) {
            auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); pump();
        };
        // Leave the field opened by the shared test, then test all four arrows and modifiers.
        if (field->isVisible()) { field->grabKeyboardFocus(); press (juce::KeyPress::escapeKey); }
        click (probe.blankSpot);
        proc.loadFactory (0);
        expect (press (juce::KeyPress::downKey) && proc.getGrooveInfo().presetIndex == 1, "Down loads the next factory groove");
        expect (press (juce::KeyPress::upKey) && proc.getGrooveInfo().presetIndex == 0, "Up loads the previous factory groove");
        for (int modifier : { juce::ModifierKeys::shiftModifier, juce::ModifierKeys::commandModifier,
                              juce::ModifierKeys::altModifier, juce::ModifierKeys::ctrlModifier }) {
            const auto before = proc.getGrooveInfo().name;
            bool used = false;
            for (int code : { juce::KeyPress::leftKey, juce::KeyPress::rightKey, juce::KeyPress::upKey, juce::KeyPress::downKey })
                used = press (code, juce::ModifierKeys (modifier)) || used;
            expect (! used && before == proc.getGrooveInfo().name, "modified arrows reach the host without loading a groove");
        }
        expect (! press (juce::KeyPress::escapeKey), "Escape with nothing open reaches the host");
        click (art (740, 716)); // NEW GROOVE
        const auto rolled = proc.getGrooveInfo().seed;
        expect (! press (juce::KeyPress::returnKey) && proc.getGrooveInfo().seed == rolled, "Return never re-rolls NEW GROOVE after a click");

        // Focus moving outside the plug-in keeps the draft; Return, Escape and OK still work.
        openField();
        field->setText ("Keep this draft", false);
        juce::TextEditor hostField;
        holder.addAndMakeVisible (hostField);
        hostField.setBounds (0, 0, 20, 20);
        hostField.grabKeyboardFocus(); pump();
        expect (field->isVisible() && field->getText() == "Keep this draft", "a window/host focus change preserves the SAVE draft");
        field->grabKeyboardFocus();
        press (juce::KeyPress::returnKey);
        expect (! field->isVisible() && proc.getGrooveInfo().user == "Keep this draft", "Return saves the retained draft to USER");
        openField(); field->setText ("Must not save", false);
        press (juce::KeyPress::escapeKey);
        expect (! field->isVisible() && ! root.getChildFile ("Must not save.ssgroove").exists(), "Escape cancels SAVE");
        openField(); field->setText ("Saved with OK", false);
        click (save->getBounds().getCentre());
        expect (! field->isVisible() && proc.getGrooveInfo().user == "Saved with OK", "clicking OK saves after the field releases focus");
        openField(); field->setText ("Cancel on key", false);
        click (art (222, 530));
        expect (! field->isVisible() && ! root.getChildFile ("Cancel on key.ssgroove").exists(), "another key cancels naming");
        proc.loadFactory (stutter::numFactoryGrooves - 1);
        click (probe.blankSpot);
        press (juce::KeyPress::rightKey);
        expect (proc.getGrooveInfo().user == "Keep this draft", "keyboard arrows enter USER in the on-screen list order");
        press (juce::KeyPress::rightKey); press (juce::KeyPress::rightKey);
        expect (proc.getGrooveInfo().presetIndex == 0, "keyboard arrows wrap from the last USER groove to factory zero");
        press (juce::KeyPress::leftKey);
        expect (proc.getGrooveInfo().user == "Saved with OK", "keyboard arrows wrap backward into USER");

        // LOCK longer than the permanent free parameter stays untouched, even across a real drag.
        proc.uiBpm = 120.0; set ("rate", 1); set ("swing", 60); set ("slapLock", 1); // 300 ms
        const auto before = proc.snapshot().slapSeconds();
        const auto time = art (460, 1118);
        mouse (time, true); mouse (time.translated (0, -20), true); mouse (time.translated (0, -20), false); pump();
        expect (proc.snapshot().slapLock && std::abs (proc.snapshot().slapSeconds() - before) < 1.0e-9,
                "a 300 ms TIME drag preserves LOCK and the exact echo time");
        click (probe.blankSpot); // separate the gestures so JUCE cannot read the next mouse-down as a double-click
        set ("rate", 2); // 150 ms fits free TIME
        const auto freeStart = proc.snapshot().slapSeconds();
        mouse (time, true);
        expect (! proc.snapshot().slapLock && std::abs (proc.snapshot().slapSeconds() - freeStart) < 1.0e-6,
                "a representable locked TIME unlocks at the displayed value on mouse-down");
        mouse (time, false); pump();

        // DRAG: a drag hands the groove's MIDI clip to the OS drag; a click alone doesn't, and neither takes focus.
        auto* ss = dynamic_cast<StutterEditor*> (editor.get());
        auto* midi = findControl (*editor, "Drag MIDI");
        expect (ss != nullptr && midi != nullptr && ! midi->getWantsKeyboardFocus(), "the DRAG MIDI key is there and never takes the keyboard");
        if (ss != nullptr && midi != nullptr) {
            juce::Array<juce::File> dragged;
            ss->startExternalDrag = [&dragged] (const juce::File& f) { dragged.add (f); return true; };
            proc.uiBpm = 140.0;
            proc.loadFactory (stutter::factoryIndexOf ("Glass Stairs"));
            click (probe.blankSpot);
            const auto at = midi->getBounds().getCentre();
            click (at);
            expect (dragged.isEmpty(), "a click on DRAG starts no drag");
            click (probe.blankSpot);
            mouse (at, true); mouse (at.translated (24, -30), true); mouse (at.translated (24, -30), false); pump();
            const auto s = proc.snapshot();
            juce::MemoryOutputStream want;
            stutter::makeMidiClip (s.pattern, s.swing, s.length, StutterProcessor::rateBeats (s.rate), s.bpm, "Glass Stairs").writeTo (want, 0);
            juce::MemoryBlock got;
            expect (dragged.size() == 1 && dragged[0].getFileName() == "Glass Stairs.mid" && dragged[0].loadFileAsData (got)
                        && got == want.getMemoryBlock(),
                    "dragging DRAG hands over the groove's MIDI clip, named after it");
            expect (proc.getGrooveInfo().name == "Glass Stairs" && ! proc.getGrooveInfo().edited, "dragging the clip leaves the groove alone");
            expect (! midi->hasKeyboardFocus (true) && ! press (juce::KeyPress::returnKey), "Return after a drag still reaches the host");
            click (probe.blankSpot);
            openField(); field->setText ("Drag cancels", false);
            click (at);
            expect (! field->isVisible() && ! root.getChildFile ("Drag cancels.ssgroove").exists(), "pressing DRAG cancels naming like any other key");
            ss->startExternalDrag = nullptr;
        }
        holder.removeChildComponent (&hostField);

        // 0.5.0: the row under the lane. Every key works with a click, takes no focus, and the host keeps Return.
        click (probe.blankSpot);
        proc.loadFactory (0);
        proc.editLayer = 0;
        pump();
        const auto key = [&] (const char* name) { return findControl (*editor, name); };
        bool allThere = true;
        for (const auto* name : { "Edit gate", "Edit slap", "Nudge earlier", "Nudge later", "Dotted timing", "Triplet timing" }) {
            auto* c = key (name);
            allThere = allThere && c != nullptr && ! c->getWantsKeyboardFocus() && c->isShowing();
        }
        expect (allThere, "GATE, SLAP, both NUDGE keys, DOTTED and TRIPLET are on screen and never take the keyboard");
        if (allThere) {
            const auto centre = [&] (const char* name) { return key (name)->getBounds().getCentre(); };
            const auto start = proc.getPattern();
            click (centre ("Nudge later"));
            expect (proc.getPattern() == stutter::rotated (start, 1) && proc.getGrooveInfo().edited, "NUDGE later moves the groove one step and marks it edited");
            click (centre ("Nudge earlier"));
            expect (proc.getPattern() == start, "NUDGE earlier moves it back");
            expect (! press (juce::KeyPress::returnKey) && ! press (juce::KeyPress::spaceKey), "Return and space still reach the host after NUDGE");
            click (centre ("Dotted timing"));
            expect (juce::roundToInt (proc.apvts.getRawParameterValue ("timing")->load()) == 1, "DOTTED lights dotted timing");
            click (centre ("Triplet timing"));
            expect (juce::roundToInt (proc.apvts.getRawParameterValue ("timing")->load()) == 2, "TRIPLET replaces it: only one is lit");
            click (centre ("Triplet timing"));
            expect (juce::roundToInt (proc.apvts.getRawParameterValue ("timing")->load()) == 0, "clicking the lit key goes back to straight");

            // The lane follows the layer: GATE edits the gate, SLAP edits sends (click toggles, drag sets the amount).
            auto* laneControl = key ("Step lane");
            const auto stepPoint = [&] (int step, float height01) {
                const auto b = laneControl->getBounds().toFloat();
                return juce::Point<float> (b.getX() + b.getWidth() * ((float) step + 0.5f) / 16.0f, b.getY() + b.getHeight() * height01).toInt();
            };
            proc.setPattern (stutter::parsePattern ("X...X...X...X...", "................"));
            click (stepPoint (4, 0.5f));
            expect (proc.getPattern()[4].v == stutter::Step::ghost && ! proc.getPattern()[4].slap, "in GATE a click cycles the step (hit to ghost)");
            click (centre ("Edit slap"));
            expect (proc.editLayer.load() == 1, "SLAP switches the lane to the slap sends");
            const auto gateBefore = stutter::cellsToString (proc.getPattern());
            click (stepPoint (8, 0.5f));
            expect (proc.getPattern()[8].slap && proc.getPattern()[8].send == stutter::fullSend
                        && stutter::cellsToString (proc.getPattern()) == gateBefore, "in SLAP a click sends that step, full, and leaves the gate alone");
            click (stepPoint (8, 0.5f));
            expect (! proc.getPattern()[8].slap, "a second click stops the send");
            click (stepPoint (2, 0.5f));
            expect (! proc.getPattern()[2].slap && stutter::cellsToString (proc.getPattern()) == gateBefore, "a silent step can't send, and stays silent");
            const auto from = stepPoint (12, 0.2f), to = stepPoint (12, 0.62f);
            mouse (from, true); mouse (from.translated (0, 6), true); mouse (to, true); mouse (to, false); pump();
            const auto dragged = proc.getPattern()[12];
            expect (dragged.slap && dragged.send > 4 && dragged.send < 14, "dragging down a column sets a smaller send");
            const auto a = stepPoint (0, 0.15f), b = stepPoint (4, 0.15f);
            mouse (a, true); mouse (a.translated (6, 0), true); mouse (b, true); mouse (b, false); pump();
            expect (proc.getPattern()[0].slap && proc.getPattern()[4].slap && proc.getPattern()[0].send >= 18,
                    "dragging across draws sends on every sounding step it crosses");
            expect (! press (juce::KeyPress::returnKey) && ! press (juce::KeyPress::spaceKey), "Return and space still reach the host after editing sends");
            click (centre ("Edit gate"));
            expect (proc.editLayer.load() == 0, "GATE switches back");
        }
    }
    holder.removeFromDesktop();
    holder.removeChildComponent (editor.get());
    root.deleteRecursively();
    if (previousRoot.isEmpty()) unsetenv ("STUTTER_STEP_PRESET_ROOT");
    else setenv ("STUTTER_STEP_PRESET_ROOT", previousRoot.toRawUTF8(), 1);
    std::cout << "STUTTER EDITOR: " << checks << " checks, " << failures << " total failures (including shared host keys)\n";
    return failures == 0 ? 0 : 1;
}

// The window shows only the device: at every size option, the corner pixels and edge midpoints are enclosure, never
// the background around its rounded outline (0.4.3 left dark wedges in all four corners).
int runStutterWindowEdges (const juce::File& cropsFolder)
{
    StutterProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    const juce::Colour background { 0xff050d0e };
    int failures = 0, checks = 0;
    const float scales[] { 0.8f, 0.9f, 1.0f, 1.25f, 1.5f }; // the smallest drag size, then the SIZE menu's options
    for (const float scale : scales)
        for (const float pixels : { 1.0f, 2.0f }) {
            editor->setSize (juce::roundToInt (StutterEditor::baseWidth * scale),
                             juce::roundToInt (StutterEditor::baseWidth * scale * StutterEditor::cropH / StutterEditor::cropW));
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, pixels);
            const int w = image.getWidth(), h = image.getHeight();
            const juce::Point<int> spots[] { { 0, 0 }, { w - 1, 0 }, { 0, h - 1 }, { w - 1, h - 1 },
                                             { w / 2, 0 }, { w / 2, h - 1 }, { 0, h / 2 }, { w - 1, h / 2 } };
            int bad = 0;
            for (const auto& p : spots) {
                const auto c = image.getPixelAt (p.x, p.y);
                const int distance = std::abs (c.getRed() - background.getRed()) + std::abs (c.getGreen() - background.getGreen())
                                   + std::abs (c.getBlue() - background.getBlue());
                if (distance < 40 || c.getAlpha() < 255) {
                    ++bad;
                    std::cout << "  at " << p.x << "," << p.y << ": " << c.toDisplayString (true) << '\n';
                }
            }
            ++checks;
            failures += bad > 0 ? 1 : 0;
            std::cout << (bad == 0 ? "  ok   " : "  FAIL ") << juce::roundToInt (scale * 100) << "% at " << pixels
                      << "x: corners and edge midpoints show the device (" << w << " x " << h << ")\n";
            if (cropsFolder != juce::File() && std::abs (scale - 1.0f) < 0.01f && pixels > 1.5f) {
                // The four corners, 120 px each, side by side, for the review folder.
                juce::Image crops (juce::Image::RGB, 4 * 120 + 3 * 8, 120, true);
                juce::Graphics g (crops);
                g.fillAll (juce::Colours::white);
                const juce::Point<int> corners[] { { 0, 0 }, { w - 120, 0 }, { 0, h - 120 }, { w - 120, h - 120 } };
                for (int i = 0; i < 4; ++i)
                    g.drawImage (image.getClippedImage ({ corners[i].x, corners[i].y, 120, 120 }), i * 128, 0, 120, 120, 0, 0, 120, 120);
                cropsFolder.createDirectory();
                const auto file = cropsFolder.getChildFile ("corners-after-100.png");
                file.deleteFile();
                juce::FileOutputStream out (file);
                juce::PNGImageFormat().writeImageToStream (crops, out);
            }
        }
    std::cout << "WINDOW EDGES: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
