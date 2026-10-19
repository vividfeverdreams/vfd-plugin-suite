#pragma once

// 0.5.0 feature tests: engines and latency, the original-sound upgrade, ENV and
// YOI, user preset folders, the editor size menu, and editor mouse handling. Shares Checks and helpers
// with FeatureTests.h.

#include "FeatureTests.h"
#include <set>
#include "../Source/UI/ShiftyEditor.h"

namespace shifty::tests {

namespace {

class ChangeRecorder final : public juce::AudioProcessorListener {
public:
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override
    {
        if (details.nonParameterStateChanged) ++nonParameterChanges;
        if (details.latencyChanged) ++latencyChanges;
    }
    int nonParameterChanges = 0;
    int latencyChanges = 0;
};

void checkEnginesAndLatency(Checks& checks)
{
    ShiftyProcessor processor;
    checks.require(processor.engineMode() == EngineMode::talk && processor.getLatencySamples() == 0,
                   "a new instance opens on TALK with no latency");
    for (auto* parameter : processor.getParameters())
        checks.require(!parameter->getName(64).containsIgnoreCase("engine"),
                       "the engine is not a host parameter: " + parameter->getName(64));
    prepareForTest(processor, 48000.0, 512);
    ChangeRecorder recorder;
    processor.addListener(&recorder);
    const struct { EngineMode mode; bool low; int latency; } steps[] = {
        { EngineMode::mouth, false, 1024 }, { EngineMode::classic, false, 2048 },
        { EngineMode::classic, true, 1024 }, { EngineMode::talk, false, 0 } };
    for (const auto& step : steps) {
        processor.setEngine(step.mode, step.low);
        checks.require(processor.engineMode() == step.mode && processor.getLatencySamples() == step.latency,
                       "setEngine reports its latency at once (" + juce::String(step.latency) + ")");
    }
    checks.require(recorder.latencyChanges >= 4, "each engine change tells the host the latency changed");
    // Undo walks back through the engine changes, latency included.
    const int expectedBack[] = { 1024, 2048, 1024, 0 };
    for (const int latency : expectedBack) {
        processor.undo();
        checks.require(processor.getLatencySamples() == latency, "Undo restores the engine and its latency ("
                                                                    + juce::String(latency) + ")");
    }
    checks.require(processor.engineMode() == EngineMode::talk, "Undo reaches the first engine");
    processor.redo();
    checks.require(processor.engineMode() == EngineMode::mouth && processor.getLatencySamples() == 1024,
                   "Redo re-applies an engine change");
    processor.removeListener(&recorder);

    // RANDOM picks the engine unless it is locked; latency follows, and Undo
    // brings back the engine and latency together.
    setAllLocks(processor, false);
    bool seen[4] {};
    bool latencyFollows = true, undoRestores = true;
    processor.addListener(&recorder);
    const int latencyChangesBefore = recorder.latencyChanges;
    for (int i = 0; i < 60; ++i) {
        const auto engineBefore = processor.engineMode();
        const int latencyBefore = processor.getLatencySamples();
        processor.randomizeParameters();
        const auto engine = processor.engineMode();
        seen[static_cast<int>(engine)] = true;
        latencyFollows = latencyFollows && processor.getLatencySamples()
            == ShiftyProcessor::latencyFor(engine, processor.classicLowLatency(), 48000.0);
        if (i % 10 == 0) {
            processor.undo();
            undoRestores = undoRestores && processor.engineMode() == engineBefore
                           && processor.getLatencySamples() == latencyBefore;
            processor.redo();
        }
    }
    checks.require(seen[static_cast<int>(EngineMode::talk)] && seen[static_cast<int>(EngineMode::mouth)]
                       && seen[static_cast<int>(EngineMode::classic)],
                   "RANDOM visits TALK, MOUTH and CLASSIC when the engine is unlocked");
    checks.require(!seen[static_cast<int>(EngineMode::original)], "RANDOM never picks the original 0.4.2 sound");
    checks.require(latencyFollows, "after RANDOM the reported latency matches the engine");
    checks.require(recorder.latencyChanges > latencyChangesBefore, "RANDOM tells the host when the latency changes");
    checks.require(undoRestores, "Undo after RANDOM restores the engine and its latency");
    processor.removeListener(&recorder);

    // A locked engine stays put, so latency never moves under the host.
    processor.setEngine(EngineMode::classic, true);
    processor.setParameterLocked(ShiftyProcessor::engineLockId, true);
    for (int i = 0; i < 50; ++i) processor.randomizeParameters();
    checks.require(processor.engineMode() == EngineMode::classic && processor.classicLowLatency()
                       && processor.getLatencySamples() == 1024,
                   "RANDOM keeps a locked engine and its latency");

    // The engine travels with the session.
    const auto session = sessionOf(processor);
    ShiftyProcessor restored;
    restored.setStateInformation(session.getData(), static_cast<int>(session.getSize()));
    prepareForTest(restored, 96000.0, 512);
    checks.require(restored.engineMode() == EngineMode::classic && restored.classicLowLatency()
                       && restored.getLatencySamples() == 2048,
                   "a session restores its engine and latency (CLASSIC low latency, 2048 at 96 kHz)");
    checks.require(restored.isParameterLocked(ShiftyProcessor::engineLockId), "a session remembers the engine lock");
    checks.section("engines: TALK by default, latency per engine, undoable, randomized unless locked, never automated");
}

void checkOriginalSoundUpgrade(Checks& checks)
{
    ShiftyProcessor source;
    setPattern(source, false);
    const auto legacy = legacySessionOf(source);
    ShiftyProcessor processor;
    prepareForTest(processor, 48000.0, 512);
    processor.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
    checks.require(processor.isOriginalSound() && processor.getLatencySamples() == 2048,
                   "an old session opens on the original sound at its original latency");
    const auto before = valuesOf(processor);
    processor.useLatestSound();
    checks.require(processor.engineMode() == EngineMode::classic && !processor.classicLowLatency()
                       && processor.getLatencySamples() == 2048 && valuesOf(processor) == before,
                   "Use latest sound switches to CLASSIC with the same controls and latency");
    checks.require(processor.canUndo(), "Use latest sound is one undoable step");
    processor.undo();
    checks.require(processor.isOriginalSound(), "Undo returns to the original sound");
    processor.setEngine(EngineMode::talk, false);
    const bool couldUndo = processor.canUndo();
    processor.useLatestSound();
    checks.require(processor.engineMode() == EngineMode::talk && processor.canUndo() == couldUndo,
                   "Use latest sound does nothing to a sound that is already current");

    // The 24 originals live in Classic and recall the original sound; their reworks use the new engines.
    int originals = 0;
    for (const auto& preset : ShiftyProcessor::factoryPresets())
        if (preset.folder == "Classic") {
            ++originals;
            checks.require(preset.engine == EngineMode::original, "Classic preset uses the original sound: " + preset.name);
        }
    checks.require(originals == 24, "24 originals in the Classic folder");
    checks.section("original 0.4.2 sound for old sessions, with an undoable upgrade");
}

struct MovementProbe {
    float envelope = 0.0f, morph = 0.0f, shift = 0.0f, yoi = 1.0f;
};

MovementProbe playLevel(ShiftyProcessor& processor, float amplitude, int samples, int& clock)
{
    juce::MidiBuffer midi;
    for (int done = 0; done < samples; done += 256) {
        juce::AudioBuffer<float> audio(2, 256);
        for (int i = 0; i < 256; ++i)
            for (int channel = 0; channel < 2; ++channel)
                audio.setSample(channel, i, amplitude * static_cast<float>(
                    std::sin(juce::MathConstants<double>::twoPi * 110.0 * (clock + i) / 48000.0)));
        clock += 256;
        processor.processBlock(audio, midi);
    }
    const auto display = processor.display();
    return { display.envelope, display.morph, display.shift, display.yoi };
}

void checkEnvelopeAndYoi(Checks& checks)
{
    // TALK: louder notes open the vowel; a negative amount closes it.
    for (const float amount : { 1.0f, -1.0f }) {
        ShiftyProcessor processor;
        neutraliseMovement(processor);
        setActual(processor, "vowelMorph", 2.0f);
        setActual(processor, "envAmount", amount * 0.5f);
        setActual(processor, "envAttack", 2.0f);
        setActual(processor, "envRelease", 60.0f);
        prepareForTest(processor, 48000.0, 256);
        int clock = 0;
        const auto quiet = playLevel(processor, 0.005f, 24000, clock); // -46 dBFS
        const auto loud = playLevel(processor, 0.5f, 24000, clock);    // -6 dBFS
        checks.require(quiet.envelope < 0.1f && loud.envelope > 0.9f, "ENV follows the level (TALK)");
        checks.require(amount > 0.0f ? loud.morph > quiet.morph + 1.5f : loud.morph < quiet.morph - 1.5f,
                       "ENV moves the TALK vowel " + juce::String(amount > 0.0f ? "up" : "down")
                           + " (" + juce::String(quiet.morph, 2) + " -> " + juce::String(loud.morph, 2) + ")");
    }
    // CLASSIC and MOUTH: ENV moves SHIFT.
    for (const auto mode : { EngineMode::classic, EngineMode::mouth }) {
        ShiftyProcessor processor;
        processor.setEngine(mode, false);
        neutraliseMovement(processor);
        setActual(processor, "shift", 0.0f);
        setActual(processor, "envAmount", 0.5f);
        prepareForTest(processor, 48000.0, 256);
        int clock = 0;
        const auto loud = playLevel(processor, 0.5f, 24000, clock);
        checks.require(loud.shift > 5.0f, "ENV raises SHIFT on " + juce::String(mode == EngineMode::classic ? "CLASSIC" : "MOUTH")
                                              + " (" + juce::String(loud.shift, 2) + " st)");
    }
    // The original sound ignores ENV and YOI entirely.
    {
        ShiftyProcessor processor;
        processor.loadFactoryPreset(ShiftyProcessor::factoryPresetIndex("Classic", "Rubber Throat"));
        setActual(processor, "envAmount", 1.0f);
        setActual(processor, "yoiAmount", 1.0f);
        setActual(processor, "yoiShift", 12.0f);
        prepareForTest(processor, 48000.0, 256);
        int clock = 0;
        const auto loud = playLevel(processor, 0.5f, 24000, clock);
        checks.require(std::abs(loud.shift - processor.parameters().getRawParameterValue("shift")->load()) < 1.0e-4f,
                       "the original sound keeps SHIFT where its control is");
    }
    // YOI: each new note restarts the FROM -> TO sweep.
    {
        ShiftyProcessor processor;
        neutraliseMovement(processor);
        setActual(processor, "yoiAmount", 1.0f);
        setActual(processor, "yoiFrom", 0.0f);
        setActual(processor, "yoiTo", 4.0f);
        setActual(processor, "yoiTime", 200.0f);
        prepareForTest(processor, 48000.0, 256);
        int clock = 0;
        playLevel(processor, 0.0f, 9600, clock);
        const auto start = playLevel(processor, 0.4f, 1280, clock);  // ~27 ms into the note
        const auto settled = playLevel(processor, 0.4f, 12800, clock);
        playLevel(processor, 0.0f, 4800, clock);
        const auto again = playLevel(processor, 0.4f, 1280, clock);
        checks.require(start.yoi < 0.35f && start.morph < 1.4f, "YOI starts each note near FROM ("
                                                                   + juce::String(start.morph, 2) + ")");
        checks.require(settled.yoi > 0.999f && settled.morph > 3.99f, "YOI arrives at TO after its time");
        checks.require(again.yoi < 0.35f, "the next note restarts the sweep");
    }
    checks.section("ENV moves the TALK vowel or SHIFT; YOI sweeps FROM -> TO on each note; the original sound ignores both");
}

void checkUserPresetFolders(Checks& checks)
{
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("shifty-folders", "", false);
    const auto presets = ShiftyProcessor::presetDirectoryForRoot(root);
    checks.require(presets.getChildFile("Basses").createDirectory().wasOk(), "create temporary preset folders");
    // Files the user already has: never modified.
    presets.getChildFile("Keep.shifty").replaceWithText("user top-level bytes");
    presets.getChildFile("Basses").getChildFile("Old.shifty").replaceWithText("user folder bytes");
    const auto keepTime = presets.getChildFile("Keep.shifty").getLastModificationTime();

    ShiftyProcessor processor;
    processor.setUserDataRootForTesting(root);
    processor.setEngine(EngineMode::mouth, false);
    processor.saveUserPreset("Night Talk", "Basses");
    processor.saveUserPreset("Fresh", "Live Set");
    processor.saveUserPreset("Top Level");
    checks.require(presets.getChildFile("Basses").getChildFile("Night Talk.shifty").existsAsFile()
                       && presets.getChildFile("Live Set").getChildFile("Fresh.shifty").existsAsFile()
                       && presets.getChildFile("Top Level.shifty").existsAsFile(),
                   "presets save into the chosen folder (new folders are created)");
    checks.require(processor.currentPresetFolder() == "User", "a top-level preset shows the User folder");
    checks.require(presets.getChildFile("Keep.shifty").loadFileAsString() == "user top-level bytes"
                       && presets.getChildFile("Keep.shifty").getLastModificationTime() == keepTime
                       && presets.getChildFile("Basses").getChildFile("Old.shifty").loadFileAsString() == "user folder bytes",
                   "existing user files are left untouched");
    const auto folders = processor.userPresetFolders();
    checks.require(folders.contains("Basses") && folders.contains("Live Set") && folders.size() == 2,
                   "user folders are listed");
    checks.require(processor.userPresetFiles().size() == 5, "user presets are found in every folder");
    processor.saveUserPreset("Escape", "../../outside");
    processor.saveUserPreset("Escape", "..");
    processor.saveUserPreset("Escape", ".hidden");
    int outside = 0;
    for (const auto& file : root.findChildFiles(juce::File::findFiles, true, "Escape.shifty"))
        outside += file.isAChildOf(presets) ? 0 : 1;
    checks.require(outside == 0 && !presets.getParentDirectory().getChildFile("Escape.shifty").exists()
                       && !presets.getChildFile(".hidden").exists(),
                   "a folder name cannot leave the preset folder or hide itself");

    ShiftyProcessor other;
    other.setUserDataRootForTesting(root);
    checks.require(other.loadUserPreset(presets.getChildFile("Basses").getChildFile("Night Talk.shifty")),
                   "load a preset from a folder");
    checks.require(other.currentPresetFolder() == "User/Basses" && other.currentPresetName() == "Night Talk"
                       && other.engineMode() == EngineMode::mouth,
                   "a folder preset restores its name, folder and engine");
    root.deleteRecursively();
    checks.section("user preset folders: save into a folder, list, load, and never modify existing files");
}

void checkEditorSize(Checks& checks)
{
    // Size choices that fit a display: width <= area width, height + 60 <= area height.
    const auto fits = [](int width, int height) { return ShiftyEditor::sizesFitting({ 0, 0, width, height }); };
    checks.require(ShiftyEditor::sizeFor(1.0) == juce::Point<int>(650, 849), "100% is 650 x 849");
    checks.require(fits(2560, 1440) == std::vector<double>{ 0.9, 1.0, 1.25, 1.5 }, "every size fits 2560 x 1440");
    checks.require(fits(1920, 1080) == std::vector<double>{ 0.9, 1.0 }, "90% and 100% fit 1920 x 1080");
    checks.require(fits(1440, 900) == std::vector<double>{ 0.9 }, "only 90% fits 1440 x 900");

    // The size is saved with the project, without a parameter, and the host is told.
    ShiftyProcessor processor;
    ChangeRecorder recorder;
    processor.addListener(&recorder);
    processor.setEditorScale(1.25);
    processor.removeListener(&recorder);
    checks.require(recorder.nonParameterChanges == 1, "a new size notifies the host as non-parameter state");
    const auto session = sessionOf(processor);
    ShiftyProcessor restored;
    restored.setStateInformation(session.getData(), static_cast<int>(session.getSize()));
    checks.require(std::abs(restored.editorScale() - 1.25) < 1.0e-9, "the editor size is restored with the session");
    ShiftyProcessor old;
    old.setEditorScale(1.5);
    const auto legacy = legacySessionOf(processor);
    old.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
    checks.require(std::abs(old.editorScale() - 1.0) < 1.0e-9, "an old session opens at 100%");

    // The editor reports its size back, and offers the four sizes.
    ShiftyProcessor host;
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("shifty-editor", "", false);
    host.setUserDataRootForTesting(root);
    {
        ShiftyEditor editor(host);
        const auto large = ShiftyEditor::sizeFor(1.5);
        editor.setSize(large.x, large.y);
        checks.require(std::abs(host.editorScale() - 1.5) < 0.002, "resizing the editor saves its size");
        const auto menu = editor.createSizeMenu();
        int items = 0;
        for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
            if (it.getItem().itemID > 0) ++items;
        checks.require(items == 4, "the size menu offers 90%, 100%, 125% and 150%");
    }
    root.deleteRecursively();
    checks.section("editor size: four choices, fitting rule, saved as non-parameter state");
}
class GestureCounter final : public juce::AudioProcessorParameter::Listener {
public:
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool starting) override { ++(starting ? begins : ends); }
    int begins = 0, ends = 0;
};

juce::MouseEvent sliderEvent(juce::Slider& slider, juce::Point<float> position, juce::Point<float> down,
                             int modifiers = 0, bool dragged = true)
{
    static juce::int64 clock = juce::Time::currentTimeMillis();
    const juce::Time time(++clock);
    return {juce::Desktop::getInstance().getMainMouseSource(), position,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | modifiers),
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &slider, &slider, time, down, time, 1, dragged};
}

void checkEditorMouse(Checks& checks)
{
    ShiftyProcessor host;
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("shifty-mouse", "", false);
    host.setUserDataRootForTesting(root);
    {
        ShiftyEditor editor(host);
        const auto size = ShiftyEditor::sizeFor(1.0);
        editor.setSize(size.x, size.y);
        editor.showPageForTesting(ShiftyEditor::Page::motion, ShiftyEditor::MotionTab::yoi);

        // Screen dropdowns hang from their own control, inside the window (not under the editor).
        const auto window = editor.getScreenBounds();
        for (const auto* anchor : {"shape", "rate", "target", "yoiFrom", "yoiTo", "folder", "saveFolder"}) {
            const auto area = editor.menuOptionsForTesting(anchor).getTargetScreenArea();
            checks.require(!area.isEmpty() && window.contains(area) && area != window
                               && area.getBottom() < window.getCentreY(),
                           juce::String("the ") + anchor + " menu drops down from its control on the screen");
        }

        const auto near = [](double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; };
        const auto raw = [&host](const char* id) { return static_cast<double>(host.parameters().getRawParameterValue(id)->load()); };

        // A value strip: a click never jumps, drags are relative, Shift is 5x finer.
        auto* amount = editor.sliderForTesting("yoiAmount");
        GestureCounter gestures;
        host.parameters().getParameter("yoiAmount")->addListener(&gestures);
        amount->setValue(0.5, juce::sendNotificationSync);
        const juce::Point<float> start {4.0f, amount->getHeight() * 0.5f}; // on the AMOUNT caption
        amount->mouseDown(sliderEvent(*amount, start, start, 0, false));
        checks.require(near(amount->getValue(), 0.5, 1.0e-6), "clicking AMOUNT does not jump its value");
        amount->mouseDrag(sliderEvent(*amount, start.translated(0.0f, -25.0f), start));
        checks.require(near(amount->getValue(), 0.6, 0.002), "dragging AMOUNT up 25 px raises it 10%");
        amount->mouseDrag(sliderEvent(*amount, start.translated(25.0f, -25.0f), start));
        checks.require(near(amount->getValue(), 0.7, 0.002), "dragging AMOUNT right raises it too");
        amount->mouseDrag(sliderEvent(*amount, start.translated(25.0f, 0.0f), start, juce::ModifierKeys::shiftModifier));
        checks.require(near(amount->getValue(), 0.68, 0.002), "Shift drags 5x finer, with no jump when it is pressed");
        amount->mouseDrag(sliderEvent(*amount, start.translated(25.0f, 25.0f), start));
        checks.require(near(amount->getValue(), 0.58, 0.002), "releasing Shift mid-drag continues from where the value is");
        amount->mouseDrag(sliderEvent(*amount, start.translated(25.0f, -2000.0f), start));
        checks.require(near(amount->getValue(), 1.0, 1.0e-6), "AMOUNT stops at 100%");
        amount->mouseDrag(sliderEvent(*amount, start.translated(25.0f, -2010.0f), start));
        amount->mouseDrag(sliderEvent(*amount, start.translated(25.0f, -1990.0f), start));
        checks.require(near(amount->getValue(), 0.92, 0.002), "pushing past the end does not store up travel");
        amount->mouseUp(sliderEvent(*amount, start.translated(25.0f, -1990.0f), start));
        checks.require(gestures.begins == 1 && gestures.ends == 1, "one AMOUNT drag is one host gesture");
        checks.require(near(raw("yoiAmount"), amount->getValue(), 1.0e-4), "the strip writes the parameter");
        host.parameters().getParameter("yoiAmount")->removeListener(&gestures);
        amount->mouseDown(sliderEvent(*amount, start, start, juce::ModifierKeys::altModifier, false));
        amount->mouseDrag(sliderEvent(*amount, start.translated(0.0f, -50.0f), start, juce::ModifierKeys::altModifier));
        amount->mouseUp(sliderEvent(*amount, start.translated(0.0f, -50.0f), start, juce::ModifierKeys::altModifier));
        checks.require(near(amount->getValue(), 0.0, 1.0e-6), "Alt-click resets AMOUNT and does not drag");

        // Bipolar strips get more travel: 40 px moves the envelope amount 20%.
        auto* envelope = editor.sliderForTesting("envAmount");
        envelope->setValue(0.0, juce::sendNotificationSync);
        envelope->mouseDown(sliderEvent(*envelope, start, start, 0, false));
        envelope->mouseDrag(sliderEvent(*envelope, start.translated(0.0f, -40.0f), start));
        envelope->mouseUp(sliderEvent(*envelope, start.translated(0.0f, -40.0f), start));
        checks.require(near(envelope->getValue(), 0.2, 0.002), "ENV AMOUNT: 40 px up is +20%");

        // Knobs keep their 250 px travel and gain Shift for fine steps.
        auto* shift = editor.sliderForTesting("shift");
        shift->setValue(0.0, juce::sendNotificationSync);
        const auto centre = shift->getLocalBounds().toFloat().getCentre();
        shift->mouseDown(sliderEvent(*shift, centre, centre, 0, false));
        shift->mouseDrag(sliderEvent(*shift, centre.translated(0.0f, -25.0f), centre));
        shift->mouseDrag(sliderEvent(*shift, centre.translated(0.0f, -50.0f), centre, juce::ModifierKeys::shiftModifier));
        shift->mouseUp(sliderEvent(*shift, centre.translated(0.0f, -50.0f), centre));
        checks.require(near(shift->getValue(), 2.88, 0.02), "SHIFT knob: 25 px is 2.4 st, then 25 px with Shift adds 0.48 st");

        // The vowel bar follows the mouse along its drawn OO..EE marks.
        editor.showPageForTesting(ShiftyEditor::Page::voice);
        auto* vowel = editor.sliderForTesting("vowelMorph");
        const auto at = [vowel](double designX) {
            return juce::Point<float>(static_cast<float>(vowel->getWidth() * (designX - 300.0) / 300.0), vowel->getHeight() * 0.5f);
        };
        const struct { double x, value; } marks[] { {324.0, 0.0}, {450.0, 2.0}, {576.0, 4.0}, {387.0, 1.0} };
        for (const auto& mark : marks) {
            vowel->mouseDown(sliderEvent(*vowel, at(mark.x), at(mark.x), 0, false));
            vowel->mouseUp(sliderEvent(*vowel, at(mark.x), at(mark.x), 0, false));
            checks.require(near(vowel->getValue(), mark.value, 0.03),
                           "clicking a vowel mark selects it: " + juce::String(mark.value));
        }
    }
    root.deleteRecursively();
    checks.section("editor mouse: menus drop from their control, relative strips, Shift fine, vowel bar");
}
} // namespace

void checkRandomPreset(Checks& checks)
{
    ShiftyProcessor host;
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("shifty-dice", "", false);
    host.setUserDataRootForTesting(root);
    {
        ShiftyEditor editor(host);
        const auto size = ShiftyEditor::sizeFor(1.0);
        editor.setSize(size.x, size.y);
        juce::Button* dice = nullptr;
        for (auto* child : editor.getChildren())
            if (child->getName() == "Random preset") dice = dynamic_cast<juce::Button*>(child);
        checks.require(dice != nullptr, "the presets page has a Random preset button");
        if (dice == nullptr) return;
        editor.showPageForTesting(ShiftyEditor::Page::sound);
        checks.require(!dice->isVisible(), "the dice is hidden away from the presets page");
        editor.showPageForTesting(ShiftyEditor::Page::presets);
        checks.require(dice->isVisible() && dice->getTooltip().isNotEmpty(), "the dice shows on the presets page, with a tooltip");

        // Factory, all folders: a different sound every time, never an original 0.4.2 sound.
        editor.selectPresetFilterForTesting(0, {});
        std::set<juce::String> folders;
        bool repeats = false, originals = false, undoable = true;
        for (int i = 0; i < 80; ++i) {
            const auto before = host.currentPresetFolder() + "/" + host.currentPresetName();
            dice->onClick();
            const auto after = host.currentPresetFolder() + "/" + host.currentPresetName();
            repeats = repeats || after == before;
            originals = originals || host.isOriginalSound();
            folders.insert(host.currentPresetFolder());
            if (i % 20 == 0) {
                host.undo();
                undoable = undoable && host.currentPresetFolder() + "/" + host.currentPresetName() == before;
                host.redo();
            }
        }
        checks.require(!repeats, "the dice never reloads the preset already loaded");
        checks.require(!originals, "from all folders the dice skips the original 0.4.2 sounds");
        checks.require(folders.size() >= 6, "the dice reaches across the factory folders");
        checks.require(undoable, "a dice roll is one Undo step");

        // A folder filter limits the dice to that folder.
        editor.selectPresetFilterForTesting(0, "Growl");
        bool inFolder = true;
        for (int i = 0; i < 20; ++i) {
            dice->onClick();
            inFolder = inFolder && host.currentPresetFolder() == "Growl";
        }
        checks.require(inFolder, "with a folder chosen the dice stays in it");
        // The Classic folder holds only originals, so they are the candidates there.
        editor.selectPresetFilterForTesting(0, "Classic");
        dice->onClick();
        checks.require(host.isOriginalSound() && host.currentPresetFolder() == "Classic",
                       "in the Classic folder the dice picks an original sound");
        // Nothing to pick: harmless.
        editor.selectPresetFilterForTesting(1, {});
        const auto before = host.currentPresetName();
        dice->onClick();
        checks.require(host.currentPresetName() == before, "an empty list leaves the sound alone");
    }
    root.deleteRecursively();
    checks.section("random preset: filtered list, no repeats, skips originals, one Undo step");
}

inline int runRedesignTests()
{
    Checks checks;
    checkEnginesAndLatency(checks);
    checkOriginalSoundUpgrade(checks);
    checkEnvelopeAndYoi(checks);
    checkUserPresetFolders(checks);
    checkEditorSize(checks);
    checkEditorMouse(checks);
    checkRandomPreset(checks);
    if (checks.failures == 0)
        std::cout << "SHIFTY 0.5.0 feature tests passed.\n";
    else
        std::cerr << "SHIFTY 0.5.0 feature tests failed: " << checks.failures << " checks.\n";
    return checks.failures == 0 ? 0 : 1;
}

} // namespace shifty::tests
