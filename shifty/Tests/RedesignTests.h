#pragma once

// 0.5.0 feature tests: engines and latency, the original-sound upgrade, ENV and
// YOI, user preset folders, and the editor size menu. Shares Checks and helpers
// with FeatureTests.h.

#include "FeatureTests.h"
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

    // RANDOM never touches the engine, so latency never moves under the host.
    processor.setEngine(EngineMode::classic, true);
    setAllLocks(processor, false);
    for (int i = 0; i < 50; ++i) processor.randomizeParameters();
    checks.require(processor.engineMode() == EngineMode::classic && processor.classicLowLatency()
                       && processor.getLatencySamples() == 1024,
                   "RANDOM never changes the engine or the latency");

    // The engine travels with the session.
    const auto session = sessionOf(processor);
    ShiftyProcessor restored;
    restored.setStateInformation(session.getData(), static_cast<int>(session.getSize()));
    prepareForTest(restored, 96000.0, 512);
    checks.require(restored.engineMode() == EngineMode::classic && restored.classicLowLatency()
                       && restored.getLatencySamples() == 2048,
                   "a session restores its engine and latency (CLASSIC low latency, 2048 at 96 kHz)");
    checks.section("engines: TALK by default, latency per engine, undoable, never randomized or automated");
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
} // namespace

inline int runRedesignTests()
{
    Checks checks;
    checkEnginesAndLatency(checks);
    checkOriginalSoundUpgrade(checks);
    checkEnvelopeAndYoi(checks);
    checkUserPresetFolders(checks);
    checkEditorSize(checks);
    if (checks.failures == 0)
        std::cout << "SHIFTY 0.5.0 feature tests passed.\n";
    else
        std::cerr << "SHIFTY 0.5.0 feature tests failed: " << checks.failures << " checks.\n";
    return checks.failures == 0 ? 0 : 1;
}

} // namespace shifty::tests
