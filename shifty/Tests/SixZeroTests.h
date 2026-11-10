#pragma once

// SHIFTY 0.6.0: analog DRIVE models and the SMASH finisher.
//   - the four new parameters, appended, with defaults that are the sound before 0.6.0
//   - 0.5.4 states and presets load SOFT with SMASH off, and their lock choices
//   - new instances open on the re-voiced default; a restored state wins over it
//   - locks on the new controls hold through RANDOM, factory and user presets
//   - the Classic folder is the 0.5.4 bank, bit for bit, and renders bit for bit
//   - SMASH off is exact; on, the level stays sane and nothing passes 0 dBFS
//   - TALK keeps 0 latency with every model and SMASH
// Included by EditorSnapshot.cpp after FeatureTests.h (its helpers are used here).

#include "FeatureTests.h"
#include "LegacyPresets054.h"

namespace shifty::tests {

namespace sixzero {

constexpr const char* newIds[] { "driveModel", "smash", "smashTime", "smashBalance" };

/** A 0.5.4 session of the processor's current sound: no 0.6.0 parameters or lock bits, lock version 2. */
inline juce::MemoryBlock asOldState(ShiftyProcessor& processor, int lockMask)
{
    juce::MemoryBlock block;
    processor.getStateInformation(block);
    auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
    auto tree = juce::ValueTree::fromXml(*xml);
    for (const auto* id : newIds)
        tree.removeChild(tree.getChildWithProperty("id", id), nullptr);
    tree.removeProperty("randomLockMask2", nullptr);
    tree.setProperty("randomLockMask", lockMask, nullptr);
    tree.setProperty("randomLockVersion", 2, nullptr);
    juce::MemoryBlock result;
    juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), result);
    return result;
}

inline float raw(const ShiftyProcessor& p, const char* id) { return p.parameters().getRawParameterValue(id)->load(); }

/** Renders a bass-like stereo test signal through a processor at 48 kHz (512-sample blocks, transport playing). */
inline juce::AudioBuffer<float> renderBass(ShiftyProcessor& processor, float gain = 1.0f, double seconds = 2.0)
{
    constexpr double rate = 48000.0;
    const int length = static_cast<int>(rate * seconds);
    juce::AudioBuffer<float> audio(2, length);
    for (int i = 0; i < length; ++i) {
        const double t = i / rate;
        const double gate = std::fmod(t, 0.25) < 0.2 ? 1.0 : 0.15;
        double saw = 0.0;
        for (int h = 1; h * 55.0 < 16000.0; ++h) saw += std::sin(2.0 * juce::MathConstants<double>::pi * 55.0 * h * t) / h;
        const double ph = 2.0 * juce::MathConstants<double>::pi * 110.0 * t;
        const double v = gain * gate * (0.16 * saw + 0.22 * std::sin(ph + 2.2 * std::sin(ph)));
        audio.setSample(0, i, static_cast<float>(v));
        audio.setSample(1, i, static_cast<float>(v * 0.97));
    }
    PlayingHost host;
    processor.setPlayHead(&host);
    processor.setRateAndBufferSizeDetails(rate, 512);
    processor.prepareToPlay(rate, 512);
    juce::MidiBuffer midi;
    for (int start = 0; start < length; start += 512) {
        const int count = std::min(512, length - start);
        juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(), 2, start, count);
        processor.processBlock(block, midi);
        host.seconds += count / rate;
    }
    processor.releaseResources();
    processor.setPlayHead(nullptr);
    return audio;
}

inline void applyAll(ShiftyProcessor& p, const FactoryPreset& preset)
{
    setAllLocks(p, false);
    p.setParameterLocked(ShiftyProcessor::engineLockId, false);
    const std::pair<const char*, float> values[] = {
        {"shift", preset.shift}, {"focus", preset.focus}, {"bite", preset.bite}, {"drive", preset.drive},
        {"mix", preset.mix}, {"motionEnable", preset.motion ? 1.0f : 0.0f}, {"motionShape", (float) preset.shape},
        {"motionRate", (float) preset.rate}, {"motionDepth", preset.depth}, {"subSafe", preset.subSafe ? 1.0f : 0.0f},
        {"subCut", preset.subCut}, {"output", preset.output}, {"motionSync", preset.sync ? 1.0f : 0.0f},
        {"motionFreeHz", preset.freeHz}, {"bypass", 0.0f}, {"vowel", preset.vowelColor ? 1.0f : 0.0f},
        {"vowelMorph", preset.morph}, {"voiceSet", (float) preset.voiceSet}, {"character", preset.character},
        {"talk", preset.talk}, {"envAmount", preset.envAmount}, {"envAttack", preset.envAttack},
        {"envRelease", preset.envRelease}, {"yoiAmount", preset.yoiAmount}, {"yoiFrom", preset.yoiFrom},
        {"yoiTo", preset.yoiTo}, {"yoiTime", preset.yoiTime}, {"yoiShift", preset.yoiShift},
        {"motionTarget", (float) preset.target}, {"driveModel", (float) preset.driveModel}, {"smash", preset.smash},
        {"smashTime", preset.smashTime}, {"smashBalance", preset.smashBalance} };
    for (const auto& [id, value] : values) setActual(p, id, value);
    p.setEngine(preset.engine, preset.lowLatency);
}

inline bool identical(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return false;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (!juce::exactlyEqual(a.getSample(c, i), b.getSample(c, i))) return false;
    return true;
}

inline double rmsDb(const juce::AudioBuffer<float>& a, int from)
{
    double sum = 0.0;
    for (int c = 0; c < 2; ++c)
        for (int i = from; i < a.getNumSamples(); ++i) sum += static_cast<double>(a.getSample(c, i)) * a.getSample(c, i);
    return 10.0 * std::log10(sum / (2.0 * (a.getNumSamples() - from)) + 1.0e-20);
}

inline void checkNewParameters(Checks& checks)
{
    ShiftyProcessor processor;
    const auto& parameters = processor.getParameters();
    bool appended = parameters.size() == 33;
    const float defaults[] { 0.0f, 0.0f, 0.5f, 0.0f };
    for (int i = 0; i < 4 && appended; ++i) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameters[29 + i]);
        appended = ranged != nullptr && ranged->getParameterID() == newIds[i] && ranged->getVersionHint() == 3;
        if (appended)
            checks.require(std::abs(ranged->convertFrom0to1(ranged->getDefaultValue()) - defaults[i]) < 1.0e-4f,
                           juce::String("the default of ") + newIds[i] + " is the sound before 0.6.0");
    }
    checks.require(appended, "DRIVE MODEL, SMASH, SMASH TIME and SMASH UP/DOWN are appended (version hint 3)");
    auto* model = dynamic_cast<juce::AudioParameterChoice*>(processor.parameters().getParameter("driveModel"));
    checks.require(model != nullptr && model->choices == juce::StringArray{"Soft", "Tape", "Tube", "Fuzz"},
                   "the drive models are Soft (the 0.5 drive), Tape, Tube and Fuzz");
    for (const auto* id : newIds)
        checks.require(!processor.isParameterLocked(id), juce::String("a new instance leaves ") + id + " unlocked");
    checks.section("0.6.0 parameters appended, defaults = the 0.5.4 sound, unlocked by default");
}

inline void checkOldStatesAndLocks(Checks& checks)
{
    // A 0.5.4 session loaded into a new instance (which opens on the re-voiced default) takes SOFT and SMASH off.
    ShiftyProcessor old;
    applyAll(old, legacy054::bank()[3]);
    const auto oldState = asOldState(old, (1 << 7) | (1 << 14) | (1 << 3));
    ShiftyProcessor fresh;
    checks.require(raw(fresh, "smash") > 0.0f && juce::roundToInt(raw(fresh, "driveModel")) != 0,
                   "a new instance opens with an analog drive and SMASH on");
    fresh.setStateInformation(oldState.getData(), static_cast<int>(oldState.getSize()));
    checks.require(juce::roundToInt(raw(fresh, "driveModel")) == 0 && raw(fresh, "smash") == 0.0f
                       && std::abs(raw(fresh, "smashTime") - 0.5f) < 1.0e-4f && std::abs(raw(fresh, "smashBalance")) < 1.0e-4f,
                   "a 0.5.4 session loads the SOFT drive and SMASH off, whatever the instance held before");
    bool locksKept = fresh.isParameterLocked("drive") && fresh.isParameterLocked("output") && fresh.isParameterLocked("bypass")
                  && !fresh.isParameterLocked("shift");
    for (const auto* id : newIds) locksKept = locksKept && !fresh.isParameterLocked(id);
    checks.require(locksKept, "a 0.5.4 session keeps its lock choices and its new controls start unlocked");

    // Everything locked in 0.5.4 (29 controls and the engine): RANDOM stays inert, so the new controls lock too.
    ShiftyProcessor allLocked;
    const auto inert = asOldState(old, (1 << 30) - 1);
    allLocked.setStateInformation(inert.getData(), static_cast<int>(inert.getSize()));
    bool everyLock = allLocked.isParameterLocked(ShiftyProcessor::engineLockId);
    for (const auto* id : newIds) everyLock = everyLock && allLocked.isParameterLocked(id);
    const auto before = valuesOf(allLocked);
    allLocked.randomizeParameters();
    checks.require(everyLock && valuesOf(allLocked) == before, "a 0.5.4 session with everything locked keeps RANDOM inert");

    // A 0.5.4 user preset loads SOFT and SMASH off too.
    const juce::TemporaryFile fixture(".shifty");
    fixture.getFile().replaceWithData(oldState.getData(), oldState.getSize());
    ShiftyProcessor recipient;
    setAllLocks(recipient, false);
    recipient.loadUserPreset(fixture.getFile());
    checks.require(juce::roundToInt(raw(recipient, "driveModel")) == 0 && raw(recipient, "smash") == 0.0f,
                   "a 0.5.4 user preset loads the SOFT drive and SMASH off");

    // New locks round-trip through a session (randomLockMask2) and hold through RANDOM, presets and user presets.
    ShiftyProcessor locked;
    setAllLocks(locked, false);
    setActual(locked, "driveModel", 3.0f);
    setActual(locked, "smash", 0.37f);
    setActual(locked, "smashTime", 0.81f);
    setActual(locked, "smashBalance", -0.42f);
    for (const auto* id : newIds) locked.setParameterLocked(id, true);
    const std::array<float, 4> held { raw(locked, "driveModel"), raw(locked, "smash"), raw(locked, "smashTime"), raw(locked, "smashBalance") };
    const auto stillHeld = [&locked, &held] {
        for (size_t i = 0; i < 4; ++i)
            if (!juce::exactlyEqual(raw(locked, newIds[i]), held[i])) return false;
        return true;
    };
    juce::MemoryBlock session;
    locked.getStateInformation(session);
    ShiftyProcessor restored;
    restored.setStateInformation(session.getData(), static_cast<int>(session.getSize()));
    bool restoredLocks = true;
    for (const auto* id : newIds) restoredLocks = restoredLocks && restored.isParameterLocked(id);
    checks.require(restoredLocks && !restored.isParameterLocked("shift"), "locks on the new controls are saved with the session");
    bool randomHolds = true;
    for (int i = 0; i < 40; ++i) { locked.randomizeParameters(); randomHolds = randomHolds && stillHeld(); }
    checks.require(randomHolds, "locked DRIVE MODEL and SMASH controls hold through 40 RANDOM presses");
    int presetsHeld = 0;
    const auto count = static_cast<int>(ShiftyProcessor::factoryPresets().size());
    for (int index = 0; index < count; ++index) { locked.loadFactoryPreset(index); presetsHeld += stillHeld() ? 1 : 0; }
    checks.require(presetsHeld == count, "locked DRIVE MODEL and SMASH controls hold through every factory preset ("
                                             + juce::String(presetsHeld) + "/" + juce::String(count) + ")");
    ShiftyProcessor other;
    setAllLocks(other, false);
    setActual(other, "driveModel", 1.0f);
    setActual(other, "smash", 0.9f);
    juce::MemoryBlock otherState;
    other.getStateInformation(otherState);
    fixture.getFile().replaceWithData(otherState.getData(), otherState.getSize());
    locked.loadUserPreset(fixture.getFile());
    checks.require(stillHeld(), "locked DRIVE MODEL and SMASH controls hold through a user preset");

    // Unlocked, RANDOM reaches every new control.
    ShiftyProcessor open;
    setAllLocks(open, false);
    std::array<std::set<float>, 4> seen;
    for (int i = 0; i < 64; ++i) {
        open.randomizeParameters();
        for (size_t k = 0; k < 4; ++k) seen[k].insert(raw(open, newIds[k]));
    }
    bool reached = seen[0].size() >= 3;
    for (size_t k = 1; k < 4; ++k) reached = reached && seen[k].size() > 10;
    checks.require(reached, "RANDOM picks drive models and SMASH settings when they are unlocked");
    checks.section("0.5.4 sessions and presets load the 0.5.4 sound; new locks are saved and hold through RANDOM and presets");
}

inline void checkDefaultAndClassic(Checks& checks)
{
    ShiftyProcessor fresh;
    const auto& bank = ShiftyProcessor::factoryPresets();
    const int index = ShiftyProcessor::factoryPresetIndex(fresh.currentPresetFolder(), fresh.currentPresetName());
    checks.require(index >= 0 && bank[(size_t) index].driveModel != 0 && bank[(size_t) index].smash > 0.3f
                       && fresh.engineMode() == EngineMode::talk && fresh.getLatencySamples() == 0,
                   "a new instance opens on " + fresh.currentPresetFolder() + "/" + fresh.currentPresetName()
                       + ": TALK, an analog drive and SMASH, no latency");

    // The Classic folder is the 0.5.4 bank exactly, and every other preset uses the new sound.
    int classic = 0, classicSame = 0, revoiced = 0, others = 0;
    for (const auto& preset : bank) {
        if (preset.folder == "Classic") {
            ++classic;
            for (const auto& old : legacy054::bank())
                if (old.folder == preset.folder && old.name == preset.name) {
                    ShiftyProcessor a, b;
                    applyAll(a, old);
                    applyAll(b, preset);
                    classicSame += valuesOf(a) == valuesOf(b) && a.engineMode() == b.engineMode()
                                   && preset.driveModel == 0 && preset.smash == 0.0f ? 1 : 0;
                }
        } else {
            ++others;
            revoiced += preset.driveModel != 0 || preset.smash > 0.0f ? 1 : 0;
        }
    }
    checks.require(classic == 24 && classicSame == 24, "the 24 Classic presets are the 0.5.4 originals, SOFT drive and SMASH off ("
                                                          + juce::String(classicSame) + "/24)");
    checks.require(revoiced == others, "every other factory preset is re-voiced with an analog drive or SMASH ("
                                           + juce::String(revoiced) + "/" + juce::String(others) + ")");

    // Loaded as presets, the Classic sounds render exactly as their 0.5.4 values do in this build (the hosted
    // --version-compare against the 0.5.4 bundle proves the same against the old binary).
    int renderedSame = 0;
    for (const auto& preset : bank) {
        if (preset.folder != "Classic") continue;
        ShiftyProcessor a, b;
        for (const auto& old : legacy054::bank())
            if (old.folder == preset.folder && old.name == preset.name) applyAll(a, old);
        b.loadFactoryPreset(ShiftyProcessor::factoryPresetIndex(preset.folder, preset.name));
        renderedSame += identical(renderBass(a), renderBass(b)) ? 1 : 0;
    }
    checks.require(renderedSame == 24, "Classic presets render identically however they are loaded (" + juce::String(renderedSame) + "/24)");
    checks.section("the new-instance default, the untouched Classic folder and the re-voiced bank");
}

inline void checkSmashInProcessor(Checks& checks)
{
    // SMASH at 0 leaves the processor's output exactly as without it; switching SMASH on keeps the level sane.
    const auto& bank = ShiftyProcessor::factoryPresets();
    const auto& preset = bank[(size_t) ShiftyProcessor::factoryPresetIndex("Growl", "Basement Growl")];
    ShiftyProcessor plain, smashed;
    applyAll(plain, preset);
    applyAll(smashed, preset);
    setActual(plain, "smash", 0.0f);
    const auto off = renderBass(plain);
    setActual(smashed, "smash", 0.0f);
    setActual(smashed, "smashTime", 0.1f);
    setActual(smashed, "smashBalance", 0.7f);
    checks.require(identical(off, renderBass(smashed)), "SMASH at 0 ignores TIME and UP/DOWN: the output is exact");
    setActual(smashed, "smash", 1.0f);
    const auto on = renderBass(smashed);
    const double change = rmsDb(on, 48000) - rmsDb(off, 48000);
    checks.require(change > -2.0 && change < 2.0, "SMASH at 100% keeps the level within 2 dB (" + juce::String(change, 2) + " dB)");

    // A hot input through SMASH never passes 0 dBFS (before OUTPUT).
    ShiftyProcessor hot;
    applyAll(hot, preset);
    setActual(hot, "smash", 1.0f);
    setActual(hot, "output", 0.0f);
    const auto loud = renderBass(hot, 4.0f);
    checks.require(loud.getMagnitude(0, loud.getNumSamples()) <= 0.981f, "a hot input (+12 dB) through SMASH stays under 0 dBFS ("
                                                                         + juce::String(loud.getMagnitude(0, loud.getNumSamples()), 3) + ")");
    // TALK keeps 0 latency with every model and SMASH; BYPASS still passes the input exactly.
    bool zero = true;
    for (int model = 0; model < 4; ++model) {
        ShiftyProcessor talk;
        setActual(talk, "driveModel", static_cast<float>(model));
        setActual(talk, "smash", 0.8f);
        talk.prepareToPlay(48000.0, 512);
        zero = zero && talk.getLatencySamples() == 0;
    }
    checks.require(zero, "TALK reports 0 latency with every drive model and SMASH on");
    ShiftyProcessor bypassed;
    setActual(bypassed, "smash", 1.0f);
    setActual(bypassed, "bypass", 1.0f);
    const auto passed = renderBass(bypassed);
    ShiftyProcessor reference;
    setActual(reference, "bypass", 1.0f);
    setActual(reference, "smash", 0.0f);
    checks.require(identical(passed, renderBass(reference)), "BYPASS passes the input exactly with SMASH on");
    checks.section("SMASH in the processor: exact when off, level within 2 dB, ceiling, zero latency on TALK, bypass");
}

inline int runSixZeroTests()
{
    Checks checks;
    checkNewParameters(checks);
    checkOldStatesAndLocks(checks);
    checkDefaultAndClassic(checks);
    checkSmashInProcessor(checks);
    if (checks.failures == 0)
        std::cout << "SHIFTY 0.6.0 feature tests passed.\n";
    else
        std::cerr << "SHIFTY 0.6.0 feature tests failed: " << checks.failures << " checks.\n";
    return checks.failures == 0 ? 0 : 1;
}

} // namespace sixzero

using sixzero::runSixZeroTests;

} // namespace shifty::tests
