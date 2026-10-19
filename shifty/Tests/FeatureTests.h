#pragma once

#include "../Source/Plugin/ShiftyProcessor.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <set>
#include <vector>

namespace shifty::tests {

namespace {
// The 16 controls of 0.4.2 in their original order, then the 13 added in 0.5.0 (append-only).
constexpr std::array<const char*, 29> expectedParameterIds {{
    "shift", "focus", "bite", "drive", "mix", "subSafe", "subCut", "output",
    "motionEnable", "motionShape", "motionRate", "motionDepth", "motionSync",
    "motionFreeHz", "bypass", "vowel",
    "vowelMorph", "voiceSet", "character", "talk", "envAmount", "envAttack", "envRelease",
    "yoiAmount", "yoiFrom", "yoiTo", "yoiTime", "yoiShift", "motionTarget"
}};
constexpr size_t legacyParameterCount = 16; // parameters that existed in 0.4.2
using Values = std::array<float, expectedParameterIds.size()>;
using Locks = std::array<bool, expectedParameterIds.size()>;

bool exactlyEqual(float first, float second)
{
    // Parameter locks promise an unchanged value, including its exact float
    // representation; a tolerance would hide unintended small edits.
    return std::bit_cast<std::uint32_t>(first) == std::bit_cast<std::uint32_t>(second);
}

struct Checks {
    int failures = 0;

    void require(bool condition, const juce::String& detail)
    {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << detail << '\n';
        }
    }

    void section(const char* detail) const
    {
        std::cout << "Checked " << detail << ".\n";
    }
};

Values valuesOf(const ShiftyProcessor& processor)
{
    Values result {};
    for (size_t i = 0; i < expectedParameterIds.size(); ++i)
        result[i] = processor.parameters().getParameter(expectedParameterIds[i])->getValue();
    return result;
}

void reportValueDifferences(const ShiftyProcessor& processor, const Values& expected,
                            const juce::String& context)
{
    const auto actual = valuesOf(processor);
    for (size_t i = 0; i < expected.size(); ++i) {
        if (exactlyEqual(expected[i], actual[i])) continue;
        const auto* parameter = processor.parameters().getParameter(expectedParameterIds[i]);
        std::cerr << std::setprecision(10) << context << ": " << expectedParameterIds[i]
                  << ", normalized expected=" << expected[i] << ", actual=" << actual[i]
                  << ", delta=" << actual[i] - expected[i]
                  << "; physical expected=" << parameter->convertFrom0to1(expected[i])
                  << ", actual=" << parameter->convertFrom0to1(actual[i])
                  << ", interval=" << parameter->getNormalisableRange().interval << '\n';
    }
}

Locks locksOf(const ShiftyProcessor& processor)
{
    Locks result {};
    for (size_t i = 0; i < expectedParameterIds.size(); ++i)
        result[i] = processor.isParameterLocked(expectedParameterIds[i]);
    return result;
}

void setAllLocks(ShiftyProcessor& processor, bool locked)
{
    for (const auto* id : expectedParameterIds)
        processor.setParameterLocked(id, locked);
    processor.setParameterLocked(ShiftyProcessor::engineLockId, locked);
}

void setNormalized(ShiftyProcessor& processor, const char* id, float normalized)
{
    auto* parameter = processor.parameters().getParameter(id);
    // Use real control values. AudioParameterBool accepts an arbitrary raw
    // normalized float, but sessions correctly restore its discrete 0/1 value.
    const auto canonical = parameter->convertTo0to1(parameter->convertFrom0to1(normalized));
    parameter->setValueNotifyingHost(canonical);
}

void setActual(ShiftyProcessor& processor, const char* id, float actual)
{
    auto* parameter = processor.parameters().getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(actual));
}

/** New instances open on a TALK preset with macros and envelope movement. Tests of the
    Motion LFO itself switch those extra movements off. */
void neutraliseMovement(ShiftyProcessor& processor)
{
    setActual(processor, "character", 0.0f);
    setActual(processor, "talk", 0.0f);
    setActual(processor, "envAmount", 0.0f);
    setActual(processor, "yoiAmount", 0.0f);
    setActual(processor, "yoiShift", 0.0f);
}

void setPattern(ShiftyProcessor& processor, bool alternate)
{
    for (size_t i = 0; i < expectedParameterIds.size(); ++i) {
        const auto step = static_cast<float>(i);
        const auto normalized = alternate ? 0.97f - std::fmod(0.04f + step * 0.043f, 0.94f)
                                         : 0.03f + std::fmod(0.04f + step * 0.041f, 0.94f);
        setNormalized(processor, expectedParameterIds[i], normalized);
    }
    // APVTS flushes its parameter values to the state tree here. This models
    // the host storing a completed edit before the following user command.
    processor.parameters().copyState();
}

void checkLegalValues(Checks& checks, const ShiftyProcessor& processor,
                      const juce::String& context)
{
    for (const auto* id : expectedParameterIds) {
        const auto* parameter = processor.parameters().getParameter(id);
        const auto normalized = parameter->getValue();
        const auto actual = parameter->convertFrom0to1(normalized);
        checks.require(std::isfinite(normalized) && normalized >= 0.0f && normalized <= 1.0f
                           && std::isfinite(actual), context + ": legal " + id);
    }
}

juce::MemoryBlock sessionOf(ShiftyProcessor& processor)
{
    juce::MemoryBlock result;
    processor.getStateInformation(result);
    return result;
}

juce::MemoryBlock legacySessionOf(ShiftyProcessor& processor)
{
    // Recreate the v0.1 schema rather than remove a known v0.2 lock property.
    // This fixture therefore remains independent of the lock storage format.
    const auto current = processor.parameters().copyState();
    juce::ValueTree legacy("SHIFTY_STATE");
    legacy.setProperty("schemaVersion", 1, nullptr);
    legacy.setProperty("engineVersion", 1, nullptr);
    legacy.setProperty("presetName", processor.currentPresetName(), nullptr);
    // Only the fifteen controls that v0.1 had: no vowel color, no 0.5.0 controls, no engine.
    for (const auto& child : current)
        if (child.hasProperty("id"))
            for (size_t i = 0; i + 1 < legacyParameterCount; ++i)
                if (child["id"].toString() == expectedParameterIds[i])
                    legacy.addChild(child.createCopy(), -1, nullptr);

    juce::MemoryBlock result;
    if (auto xml = legacy.createXml())
        juce::AudioProcessor::copyXmlToBinary(*xml, result);
    return result;
}

void checkRandomLocksAndRanges(Checks& checks)
{
    ShiftyProcessor processor;
    const auto eligible = ShiftyProcessor::randomizableParameterIds();
    checks.require(eligible.size() == static_cast<int>(expectedParameterIds.size()),
                   "all 29 parameters are listed for randomization");
    for (const auto* id : expectedParameterIds) {
        checks.require(eligible.contains(id), juce::String("randomization includes ") + id);
        checks.require(processor.isParameterLocked(id)
                           == (juce::String(id) == "output" || juce::String(id) == "bypass"),
                       juce::String("default lock for ") + id);
    }
    checks.require(!processor.isParameterLocked(ShiftyProcessor::engineLockId), "the engine is unlocked by default");

    // Keep arbitrary exact settings locked across many complete randomizations.
    setPattern(processor, false);
    for (size_t i = 0; i < expectedParameterIds.size(); ++i)
        processor.setParameterLocked(expectedParameterIds[i], i % 3 == 1);
    const auto baseline = valuesOf(processor);
    const auto lockMask = locksOf(processor);
    Values minimum;
    Values maximum;
    minimum.fill(1.0f);
    maximum.fill(0.0f);
    for (int draw = 0; draw < 160; ++draw) {
        processor.randomizeParameters();
        const auto current = valuesOf(processor);
        checkLegalValues(checks, processor, "random draw " + juce::String(draw));
        checks.require(locksOf(processor) == lockMask, "randomization preserves the lock choices");
        for (size_t i = 0; i < current.size(); ++i) {
            if (lockMask[i])
                checks.require(exactlyEqual(current[i], baseline[i]),
                               juce::String("random preserves exact locked value: ")
                                   + expectedParameterIds[i]);
            minimum[i] = juce::jmin(minimum[i], current[i]);
            maximum[i] = juce::jmax(maximum[i], current[i]);
        }
    }
    for (size_t i = 0; i < minimum.size(); ++i)
        if (!lockMask[i])
            checks.require(maximum[i] > minimum[i],
                           juce::String("unlocked parameter varies: ") + expectedParameterIds[i]);

    // Unlock one parameter at a time, including Output and Bypass. A large
    // sample avoids assuming that each discrete control must change every draw.
    setAllLocks(processor, true);
    for (size_t target = 0; target < expectedParameterIds.size(); ++target) {
        processor.setParameterLocked(expectedParameterIds[target], false);
        const auto before = valuesOf(processor);
        bool changed = false;
        bool othersPreserved = true;
        for (int draw = 0; draw < 64; ++draw) {
            processor.randomizeParameters();
            const auto after = valuesOf(processor);
            const auto* parameter = processor.parameters().getParameter(expectedParameterIds[target]);
            const float actual = parameter->convertFrom0to1(after[target]);
            if (juce::String(expectedParameterIds[target]) == "shift")
                checks.require(std::abs(actual) >= 1.99f, "Random avoids an inaudible near-zero Shift");
            if (juce::String(expectedParameterIds[target]) == "mix")
                checks.require(actual >= .699f, "Random keeps the formant effect prominent in the mix");
            if (juce::String(expectedParameterIds[target]) == "bite")
                checks.require(actual >= .349f, "Random retains useful source-envelope contrast");
            changed = changed || !exactlyEqual(after[target], before[target]);
            for (size_t other = 0; other < after.size(); ++other)
                if (other != target)
                    othersPreserved = othersPreserved && exactlyEqual(after[other], before[other]);
        }
        checks.require(changed, juce::String("single unlocked parameter can change: ")
                                    + expectedParameterIds[target]);
        checks.require(othersPreserved, juce::String("single unlocked parameter respects all locks: ")
                                            + expectedParameterIds[target]);
        processor.setParameterLocked(expectedParameterIds[target], true);
    }
    const auto allLocked = valuesOf(processor);
    processor.randomizeParameters();
    checks.require(valuesOf(processor) == allLocked, "Random is harmless with every parameter locked");
    checks.section("default locks, exact exclusions, all eligible parameters, and valid random ranges");
}

void checkSourceFirstDefaultsAndMigration(Checks& checks)
{
    ShiftyProcessor processor;
    checks.require(processor.parameters().getRawParameterValue("vowel")->load() == 0.0f,
                   "new instance uses only the source formant envelope");
    // 0.5.3: Vowel Color is unlocked by default, so RANDOM in CLASSIC can give a
    // plain synth a shape (CLASSIC alone has no envelope to move on a saw).
    bool colored = false;
    for (int i = 0; i < 32; ++i) {
        processor.randomizeParameters();
        colored = colored || processor.parameters().getRawParameterValue("vowel")->load() > 0.5f;
    }
    checks.require(colored, "Random can introduce vowel color with the default locks");

    // Lock masks saved before 0.5.3 (no randomLockVersion).
    {
        const auto stateWithLocks = [](ShiftyProcessor& source, int mask, bool versioned) {
            juce::MemoryBlock block;
            source.getStateInformation(block);
            auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
            auto tree = juce::ValueTree::fromXml(*xml);
            tree.setProperty("randomLockMask", mask, nullptr);
            if (!versioned) tree.removeProperty("randomLockVersion", nullptr);
            juce::MemoryBlock result;
            juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), result);
            return result;
        };
        ShiftyProcessor source, target;
        const int legacyDefault = (1 << 7) | (1 << 14) | (1 << 15);
        auto state = stateWithLocks(source, legacyDefault, false);
        target.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        checks.require(!target.isParameterLocked("vowel") && target.isParameterLocked("output")
                           && target.isParameterLocked("bypass") && !target.isParameterLocked(ShiftyProcessor::engineLockId),
                       "an untouched pre-0.5.3 lock mask takes today's defaults");
        state = stateWithLocks(source, legacyDefault, true);
        target.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        checks.require(target.isParameterLocked("vowel"), "a 0.5.3 session keeps a Vowel Color lock the user chose");
        state = stateWithLocks(source, (1 << 29) - 1, false);
        target.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        checks.require(target.isParameterLocked(ShiftyProcessor::engineLockId),
                       "a pre-0.5.3 session with every control locked keeps RANDOM inert (engine locked too)");
        state = stateWithLocks(source, (1 << 0) | (1 << 7), false);
        target.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        checks.require(!target.isParameterLocked(ShiftyProcessor::engineLockId) && target.isParameterLocked("shift"),
                       "a pre-0.5.3 custom mask is kept and the engine starts unlocked");
    }

    // An older host state contains fifteen parameters and its own lock choices.
    // Test loading it into an already-colored instance, not only a fresh one.
    const auto fixture = legacySessionOf(processor);
    auto xml = juce::AudioProcessor::getXmlFromBinary(fixture.getData(), static_cast<int>(fixture.getSize()));
    if (!xml) { checks.require(false, "legacy migration fixture is valid"); return; }
    auto tree = juce::ValueTree::fromXml(*xml);
    tree.setProperty("randomLockMask", (1 << 0) | (1 << 7), nullptr);
    tree.setProperty("engineVersion", 2, nullptr);
    juce::MemoryBlock oldState;
    juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), oldState);
    setActual(processor, "vowel", 1.0f);
    processor.setParameterLocked("vowel", false);
    processor.setStateInformation(oldState.getData(), static_cast<int>(oldState.getSize()));
    checks.require(processor.parameters().getRawParameterValue("vowel")->load() == 0.0f,
                   "old state cannot inherit the preceding instance's vowel color");
    for (size_t i = 0; i < expectedParameterIds.size(); ++i)
        checks.require(processor.isParameterLocked(expectedParameterIds[i]) == (i == 0 || i == 7 || i == 15),
                       "old lock mask is preserved and the new vowel control is locked");
    checks.section("source-first defaults and migration from fifteen-parameter states");
}

void checkUndoRedo(Checks& checks)
{
    ShiftyProcessor processor;
    processor.loadFactoryPreset(3);
    setAllLocks(processor, false);
    processor.setParameterLocked("mix", true);
    processor.setParameterLocked("output", true);
    processor.setParameterLocked("bypass", true);
    processor.parameters().copyState();
    const auto before = valuesOf(processor);
    const auto locks = locksOf(processor);
    processor.randomizeParameters();
    const auto first = valuesOf(processor);
    processor.randomizeParameters();
    const auto second = valuesOf(processor);
    checks.require(first != before && second != first, "successive randomizations change the sound");
    checks.require(processor.canUndo(), "Random creates an undoable action");
    processor.undo();
    checks.require(valuesOf(processor) == first, "one Undo restores the whole previous random sound");
    processor.undo();
    checks.require(valuesOf(processor) == before, "second Undo restores the starting preset parameters");
    checks.require(processor.canRedo(), "randomized settings can be redone");
    processor.redo();
    checks.require(valuesOf(processor) == first, "one Redo restores the first entire random sound");
    processor.redo();
    checks.require(valuesOf(processor) == second, "second Redo restores the second entire random sound");
    checks.require(locksOf(processor) == locks, "random undo and redo preserve parameter locks");
    checks.section("single-action random Undo and Redo");
}

void checkPersistenceAndPresets(Checks& checks)
{
    ShiftyProcessor source;
    setPattern(source, true);
    setActual(source, "vowel", 1.0f);
    source.setPresetName("Session roundtrip");
    for (size_t i = 0; i < expectedParameterIds.size(); ++i)
        source.setParameterLocked(expectedParameterIds[i], i % 2 == 0);
    const auto originalValues = valuesOf(source);
    const auto originalLocks = locksOf(source);
    const auto session = sessionOf(source);
    ShiftyProcessor restored;
    restored.setStateInformation(session.getData(), static_cast<int>(session.getSize()));
    reportValueDifferences(restored, originalValues, "host roundtrip");
    checks.require(valuesOf(restored) == originalValues, "host session restores every parameter");
    checks.require(locksOf(restored) == originalLocks, "host session restores custom parameter locks");
    checks.require(restored.currentPresetName() == source.currentPresetName(), "host session restores preset name");
    checks.require(restored.engineMode() == EngineMode::talk, "host session restores the engine");

    const auto legacy = legacySessionOf(source);
    // v0.1 states: their fifteen controls exactly, vowel color off, 0.5.0 controls at their defaults.
    auto legacyValues = originalValues;
    legacyValues[legacyParameterCount - 1] = 0.0f;
    for (size_t i = legacyParameterCount; i < expectedParameterIds.size(); ++i)
        legacyValues[i] = source.parameters().getParameter(expectedParameterIds[i])->getDefaultValue();
    setAllLocks(restored, true);
    restored.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
    reportValueDifferences(restored, legacyValues, "legacy roundtrip");
    checks.require(valuesOf(restored) == legacyValues, "v0.1 session retains its original settings and disables optional vowel color");
    checks.require(restored.engineMode() == EngineMode::original && restored.getLatencySamples() > 0,
                   "a session without an engine (0.4.2 and older) loads on the original 0.4.2 sound");
    const ShiftyProcessor fresh;
    checks.require(locksOf(restored) == locksOf(fresh), "v0.1 session receives the default lock choices");

    ShiftyProcessor recipient;
    setAllLocks(recipient, false);
    recipient.setParameterLocked("focus", true);
    recipient.setParameterLocked("motionDepth", true);
    const auto recipientLocks = locksOf(recipient);
    const juce::TemporaryFile fixture(".shifty");
    checks.require(fixture.getFile().replaceWithData(session.getData(), session.getSize()),
                   "create isolated user-preset fixture");
    checks.require(recipient.loadUserPreset(fixture.getFile()), "load user preset fixture");
    reportValueDifferences(recipient, originalValues, "user preset roundtrip");
    checks.require(valuesOf(recipient) == originalValues, "user preset restores the complete sound");
    checks.require(locksOf(recipient) == recipientLocks, "user preset preserves this instance's lock choices");
    checks.require(fixture.getFile().replaceWithData(legacy.getData(), legacy.getSize()),
                   "create isolated legacy user-preset fixture");
    checks.require(recipient.loadUserPreset(fixture.getFile()), "load v0.1 user preset fixture");
    reportValueDifferences(recipient, legacyValues, "legacy user preset roundtrip");
    checks.require(valuesOf(recipient) == legacyValues, "legacy user preset restores source-formant settings with vowel color off");
    checks.require(locksOf(recipient) == recipientLocks, "legacy user preset preserves local lock choices");
    const auto beforeInvalidPreset = valuesOf(recipient);
    checks.require(fixture.getFile().replaceWithText("Not a SHIFTY preset"), "write invalid preset fixture");
    checks.require(!recipient.loadUserPreset(fixture.getFile()), "invalid user preset is rejected");
    checks.require(valuesOf(recipient) == beforeInvalidPreset && locksOf(recipient) == recipientLocks,
                   "invalid user preset leaves sound and locks intact");

    const auto names = ShiftyProcessor::factoryPresetNames();
    const auto& bank = ShiftyProcessor::factoryPresets();
    int classicCount = 0;
    for (const auto& preset : bank) classicCount += preset.folder == "Classic" ? 1 : 0;
    checks.require(names.size() >= 84 && classicCount == 24,
                   "factory bank: 60+ new presets plus the 24 originals in Classic (" + juce::String(names.size()) + ")");
    juce::StringArray keys;
    for (const auto& preset : bank) keys.add(preset.folder + "/" + preset.name);
    auto uniqueKeys = keys;
    uniqueKeys.removeDuplicates(false);
    checks.require(uniqueKeys.size() == keys.size(), "factory preset folder/name pairs are unique");
    std::set<std::pair<Values, int>> factorySounds;
    for (int index = 0; index < names.size(); ++index) {
        const auto& preset = bank[static_cast<size_t>(index)];
        setPattern(recipient, false);
        recipient.loadFactoryPreset(index);
        if (preset.folder == "Classic")
            checks.require(recipient.parameters().getRawParameterValue("vowel")->load() == 0.0f
                               && recipient.engineMode() == EngineMode::original,
                           "Classic original keeps source formants only and the 0.4.2 sound: " + names[index]);
        checks.require(recipient.engineMode() == preset.engine, "factory preset sets its engine: " + keys[index]);
        const auto first = valuesOf(recipient);
        checks.require(recipient.currentPresetName() == names[index] && recipient.currentPresetFolder() == preset.folder,
                       "factory name and folder match index " + juce::String(index));
        checks.require(locksOf(recipient) == recipientLocks, "factory preset preserves parameter locks");
        setPattern(recipient, true);
        recipient.loadFactoryPreset(index);
        checks.require(valuesOf(recipient) == first, "factory preset fully specifies its sound: " + names[index]);
        checks.require(locksOf(recipient) == recipientLocks, "factory reload preserves parameter locks");
        checkLegalValues(checks, recipient, names[index]);
        factorySounds.insert({ first, static_cast<int>(recipient.engineMode()) * 2 + (recipient.classicLowLatency() ? 1 : 0) });
    }
    checks.require(factorySounds.size() == static_cast<size_t>(names.size()), "all factory sounds have distinct settings");
    checks.section("session persistence, old sessions, user presets, and every deterministic factory preset");
}

class PlayingHost final : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo position;
        position.setBpm(140.0);
        position.setPpqPosition(seconds * 140.0 / 60.0);
        position.setTimeInSeconds(seconds);
        position.setIsPlaying(true);
        return position;
    }
    double seconds = 0.0;
};

void checkFactoryAudio(Checks& checks)
{
    const auto names = ShiftyProcessor::factoryPresetNames();
    constexpr std::array<int, 4> blocks {{64, 257, 512, 1024}};
    constexpr double tau = juce::MathConstants<double>::twoPi;
    for (const double sampleRate : {48000.0, 96000.0}) {
        for (int preset = 0; preset < names.size(); ++preset) {
            ShiftyProcessor processor;
            PlayingHost host;
            processor.setPlayHead(&host);
            processor.setRateAndBufferSizeDetails(sampleRate, 1024);
            processor.prepareToPlay(sampleRate, 1024);
            processor.loadFactoryPreset(preset);
            juce::MidiBuffer midi;
            int cursor = 0;
            int blockIndex = 0;
            double energy = 0.0;
            float peak = 0.0f;
            bool finite = true;
            const auto samples = static_cast<int>(sampleRate * 0.4);
            while (cursor < samples) {
                const auto count = juce::jmin(samples - cursor, blocks[static_cast<size_t>(blockIndex++) % blocks.size()]);
                juce::AudioBuffer<float> audio(2, count);
                host.seconds = static_cast<double>(cursor) / sampleRate;
                for (int channel = 0; channel < audio.getNumChannels(); ++channel) {
                    for (int i = 0; i < count; ++i) {
                        const auto t = static_cast<double>(cursor + i) / sampleRate;
                        const auto fundamental = 110.0 + static_cast<double>(channel) * 0.4;
                        double sample = 0.0;
                        for (int harmonic = 1; harmonic <= 18; ++harmonic) {
                            const auto frequency = fundamental * harmonic;
                            const auto formant = 0.25 + std::exp(-std::pow((frequency - 580.0) / 300.0, 2.0))
                                                       + 0.6 * std::exp(-std::pow((frequency - 1550.0) / 480.0, 2.0));
                            sample += 0.07 * formant * std::sin(tau * frequency * t + harmonic * 0.17)
                                        / std::sqrt(static_cast<double>(harmonic));
                        }
                        audio.setSample(channel, i, static_cast<float>(sample));
                    }
                }
                processor.processBlock(audio, midi);
                for (int channel = 0; channel < audio.getNumChannels(); ++channel) {
                    for (int i = 0; i < count; ++i) {
                        const auto sample = audio.getSample(channel, i);
                        finite = finite && std::isfinite(sample);
                        peak = juce::jmax(peak, std::abs(sample));
                        energy += static_cast<double>(sample) * sample;
                    }
                }
                cursor += count;
            }
            processor.setPlayHead(nullptr);
            processor.releaseResources();
            const auto description = names[preset] + " at " + juce::String(sampleRate / 1000.0, 0) + " kHz";
            checks.require(finite, description + " produces finite audio");
            checks.require(energy > 1.0e-5 && peak < 8.0f, description + " produces audible, bounded mid-bass");
        }
    }
    checks.section("mid-bass audio through all factory presets at 48 and 96 kHz with moving host transport");
}

std::vector<float> renderFreeMotion(int blockSize, EngineMode engine)
{
    constexpr double sampleRate = 48000.0;
    constexpr int samples = 32768;
    constexpr double tau = juce::MathConstants<double>::twoPi;
    ShiftyProcessor processor;
    processor.loadFactoryPreset(ShiftyProcessor::factoryPresetIndex("Classic", "Rubber Throat"));
    processor.setEngine(engine, false);
    setActual(processor, "motionTarget", 2.0f); // TALK: move SHIFT and the vowel
    setActual(processor, "motionEnable", 1.0f);
    setActual(processor, "motionSync", 0.0f);
    setActual(processor, "motionFreeHz", 20.0f);
    setActual(processor, "motionDepth", 8.0f);
    setActual(processor, "motionShape", 2.0f);
    setActual(processor, "shift", -2.0f);
    processor.setRateAndBufferSizeDetails(sampleRate, blockSize);
    processor.prepareToPlay(sampleRate, blockSize);
    std::vector<float> result(static_cast<size_t>(samples));
    juce::MidiBuffer midi;
    for (int cursor = 0; cursor < samples;) {
        const auto count = juce::jmin(samples - cursor, blockSize);
        juce::AudioBuffer<float> audio(2, count);
        for (int i = 0; i < count; ++i) {
            const auto t = static_cast<double>(cursor + i) / sampleRate;
            double sample = 0.0;
            for (int harmonic = 1; harmonic <= 18; ++harmonic)
                sample += 0.08 * std::sin(tau * 110.0 * harmonic * t + harmonic * 0.17)
                                  / std::sqrt(static_cast<double>(harmonic));
            for (int channel = 0; channel < 2; ++channel)
                audio.setSample(channel, i, static_cast<float>(sample));
        }
        processor.processBlock(audio, midi);
        std::copy_n(audio.getReadPointer(0), count, result.begin() + cursor);
        cursor += count;
    }
    return result;
}

void checkMotionBlockIndependence(Checks& checks)
{
  for (const auto engine : { EngineMode::original, EngineMode::classic, EngineMode::talk, EngineMode::mouth }) {
    const char* engineLabel = engine == EngineMode::original ? "original" : engine == EngineMode::classic ? "CLASSIC"
                            : engine == EngineMode::talk ? "TALK" : "MOUTH";
    const auto smallBlocks = renderFreeMotion(64, engine);
    for (const int otherBlockSize : {257, 2048}) {
        const auto otherBlocks = renderFreeMotion(otherBlockSize, engine);
        double error = 0.0;
        float worst = 0.0f;
        for (size_t i = 0; i < smallBlocks.size(); ++i) {
            const auto difference = smallBlocks[i] - otherBlocks[i];
            error += static_cast<double>(difference) * difference;
            worst = juce::jmax(worst, std::abs(difference));
        }
        const auto rms = std::sqrt(error / static_cast<double>(smallBlocks.size()));
        checks.require(std::isfinite(rms) && rms < 1.0e-5 && worst < 3.0e-4f,
                       juce::String(engineLabel) + ": 20 Hz free Motion is independent of host block size 64 vs "
                           + juce::String(otherBlockSize) + " (RMS "
                           + juce::String(rms, 9) + ", max " + juce::String(worst, 9) + ")");
        std::cout << engineLabel << ", 20 Hz Motion, 64 vs " << otherBlockSize << " sample host blocks: RMS difference "
                  << rms << ", largest difference " << worst << ".\n";
    }
  }
}

void checkOutputSmoothing(Checks& checks)
{
    constexpr double sampleRate = 48000.0;
    constexpr float input = 0.15f;
    ShiftyProcessor processor;
    setActual(processor, "mix", 0.0f);
    setActual(processor, "output", 0.0f);
    setActual(processor, "bypass", 0.0f);
    processor.setRateAndBufferSizeDetails(sampleRate, 257);
    processor.prepareToPlay(sampleRate, 257);
    juce::MidiBuffer midi;
    auto renderConstant = [&](int length) {
        std::vector<float> result;
        result.reserve(static_cast<size_t>(length));
        for (int cursor = 0; cursor < length;) {
            const auto count = juce::jmin(length - cursor, 257);
            juce::AudioBuffer<float> audio(2, count);
            for (int channel = 0; channel < 2; ++channel)
                std::fill_n(audio.getWritePointer(channel), count, input);
            processor.processBlock(audio, midi);
            result.insert(result.end(), audio.getReadPointer(0), audio.getReadPointer(0) + count);
            cursor += count;
        }
        return result;
    };
    const auto settled = renderConstant(16384);
    checks.require(std::abs(settled.back() - input) < 0.001f, "gain-ramp probe starts at unity");
    float previous = settled.back();
    for (const float targetDb : {-24.0f, 0.0f}) {
        setActual(processor, "output", targetDb);
        const auto transition = renderConstant(1920);
        const auto expected = input * juce::Decibels::decibelsToGain(targetDb);
        float largestJump = 0.0f;
        for (const auto sample : transition) {
            largestJump = juce::jmax(largestJump, std::abs(sample - previous));
            previous = sample;
        }
        checks.require(largestJump < 0.001f, "Output step to " + juce::String(targetDb, 0)
                                               + " dB is smooth across host blocks");
        checks.require(std::abs(transition.back() - expected) < 0.0001f,
                       "Output ramp reaches " + juce::String(targetDb, 0) + " dB within 40 ms");
        std::cout << "Output step to " << targetDb << " dB: largest adjacent-sample jump "
                  << largestJump << ".\n";
    }
    checks.section("abrupt Output reductions and increases are smoothed across host blocks");
}

// ---------------------------------------------------------------------------
// Regression tests for the 0.4.3 bug fixes.

class TestHost final : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo position;
        position.setBpm(bpm);
        position.setIsPlaying(playing);
        position.setPpqPosition(ppq);
        position.setPpqPositionOfLastBarStart(barStart);
        position.setTimeSignature(juce::AudioPlayHead::TimeSignature{numerator, denominator});
        return position;
    }
    double bpm = 120.0, ppq = 0.0, barStart = 0.0;
    bool playing = false;
    int numerator = 4, denominator = 4;
};

void prepareForTest(ShiftyProcessor& processor, double sampleRate, int blockSize)
{
    processor.setRateAndBufferSizeDetails(sampleRate, blockSize);
    processor.prepareToPlay(sampleRate, blockSize);
}

void checkHostBypassFor(Checks& checks, EngineMode engine, bool lowLatency, const char* label)
{
    ShiftyProcessor processor;
    if (engine == EngineMode::original)
        processor.loadFactoryPreset(ShiftyProcessor::factoryPresetIndex("Classic", "Rubber Throat"));
    else
        processor.setEngine(engine, lowLatency);
    constexpr double sampleRate = 48000.0;
    constexpr int block = 256;
    prepareForTest(processor, sampleRate, block);
    const int latency = processor.getLatencySamples();
    juce::MidiBuffer midi;
    auto run = [&](bool hostBypassed, int samples, auto&& input) {
        std::vector<float> result;
        for (int done = 0; done < samples;) {
            const int count = juce::jmin(block, samples - done);
            juce::AudioBuffer<float> audio(2, count);
            for (int i = 0; i < count; ++i)
                for (int channel = 0; channel < 2; ++channel)
                    audio.setSample(channel, i, input(done + i));
            if (hostBypassed) processor.processBlockBypassed(audio, midi);
            else processor.processBlock(audio, midi);
            result.insert(result.end(), audio.getReadPointer(0), audio.getReadPointer(0) + count);
            done += count;
        }
        return result;
    };
    const auto loud = [](int i) {
        return 0.4f * static_cast<float>(std::sin(0.0072 * i) + 0.5 * std::sin(0.051 * i));
    };
    const auto silence = [](int) { return 0.0f; };
    run(false, 24000, loud);
    run(true, 24000, silence); // host bypass engaged; SHIFTY's mix ramps to dry
    constexpr int impulseAt = 1000;
    const auto bypassed = run(true, impulseAt + latency + 4000,
                              [](int i) { return i == impulseAt ? 1.0f : 0.0f; });
    float strayDuringBypass = 0.0f;
    for (size_t i = 0; i < bypassed.size(); ++i)
        if (static_cast<int>(i) != impulseAt + latency)
            strayDuringBypass = juce::jmax(strayDuringBypass, std::abs(bypassed[i]));
    const float impulseOut = bypassed[static_cast<size_t>(impulseAt + latency)];
    checks.require(std::abs(impulseOut - 1.0f) < 1.0e-5f && strayDuringBypass < 1.0e-5f,
                   juce::String(label) + ": host-bypassed impulse leaves at exactly the reported latency ("
                       + juce::String(impulseOut, 6) + " at +" + juce::String(latency)
                       + ", stray " + juce::String(strayDuringBypass, 8) + ")");
    const int settle = juce::jmax(4096, 3 * latency);
    run(true, settle, silence);
    const auto resumed = run(false, settle, silence);
    float stale = 0.0f;
    for (const auto sample : resumed) stale = juce::jmax(stale, std::abs(sample));
    checks.require(stale < 1.0e-6f, juce::String(label) + ": no stale pre-bypass audio after leaving host bypass (peak "
                                        + juce::String(stale, 9) + ")");
    std::cout << "Host bypass, " << label << ": impulse " << impulseOut << " at the reported " << latency
              << "-sample latency, stray " << strayDuringBypass << ", after re-enable peak " << stale << ".\n";
}

void checkHostBypass(Checks& checks)
{
    ShiftyProcessor processor;
    // SHIFTY does not claim the host bypass, so the VST3 wrapper keeps adding its own "Bypass".
    // The 16 controls of 0.4.2 keep their order, IDs and version hint; the 0.5.0 controls follow them.
    checks.require(processor.getBypassParameter() == nullptr, "host bypass parameter is still the wrapper's own");
    const auto& parameters = processor.getParameters();
    checks.require(parameters.size() == static_cast<int>(expectedParameterIds.size()), "29 parameters, append-only");
    for (int i = 0; i < juce::jmin(parameters.size(), static_cast<int>(expectedParameterIds.size())); ++i) {
        const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*>(parameters[i]);
        const int expectedHint = static_cast<size_t>(i) < legacyParameterCount ? 1 : 2;
        checks.require(ranged != nullptr && ranged->getParameterID() == expectedParameterIds[static_cast<size_t>(i)]
                           && parameters[i]->getVersionHint() == expectedHint,
                       juce::String("parameter order, ID and version hint at index ") + juce::String(i));
    }
    checkHostBypassFor(checks, EngineMode::talk, false, "TALK");
    checkHostBypassFor(checks, EngineMode::mouth, false, "MOUTH");
    checkHostBypassFor(checks, EngineMode::classic, false, "CLASSIC");
    checkHostBypassFor(checks, EngineMode::classic, true, "CLASSIC low latency");
    checkHostBypassFor(checks, EngineMode::original, false, "original 0.4.2 sound");
    checks.section("host bypass keeps the latency-aligned dry path on every engine, and the parameter list is append-only");
}

std::vector<float> renderMotionOffsets(ShiftyProcessor& processor, TestHost* host, double sampleRate,
                                       int block, int blocks, std::vector<double>* blockEndPpq = nullptr)
{
    std::vector<float> offsets;
    juce::MidiBuffer midi;
    processor.setPlayHead(host);
    const double quartersPerSample = host != nullptr ? host->bpm / (60.0 * sampleRate) : 0.0;
    for (int b = 0; b < blocks; ++b) {
        if (host != nullptr && host->playing) {
            host->ppq = b * block * quartersPerSample;
            const double bar = ShiftyProcessor::barLengthInQuarters(host->numerator, host->denominator);
            host->barStart = std::floor(host->ppq / bar) * bar;
            if (blockEndPpq != nullptr) blockEndPpq->push_back(host->ppq + (block - 1) * quartersPerSample);
        }
        juce::AudioBuffer<float> audio(2, block);
        audio.clear();
        processor.processBlock(audio, midi);
        offsets.push_back(processor.currentMotionOffset());
    }
    processor.setPlayHead(nullptr);
    return offsets;
}

void checkStoppedTransportMotion(Checks& checks)
{
    constexpr double sampleRate = 48000.0;
    constexpr int block = 64;
    for (const bool withHost : {true, false}) {
        ShiftyProcessor processor;
        neutraliseMovement(processor);
        setActual(processor, "motionEnable", 1.0f);
        setActual(processor, "motionSync", 1.0f);
        setActual(processor, "motionRate", 2.0f);  // 1/4
        setActual(processor, "motionShape", 0.0f); // sine
        setActual(processor, "motionDepth", 6.0f);
        prepareForTest(processor, sampleRate, block);
        TestHost host;
        host.playing = false;
        host.bpm = 140.0;
        const double bpm = withHost ? 140.0 : 120.0; // 120 BPM without a host tempo
        const auto offsets = renderMotionOffsets(processor, withHost ? &host : nullptr, sampleRate, block, 1500);
        double worst = 0.0;
        float lowest = 0.0f, highest = 0.0f;
        for (size_t b = 0; b < offsets.size(); ++b) {
            const double sampleIndex = static_cast<double>((b + 1) * block - 1);
            const double expected = 6.0 * std::sin(juce::MathConstants<double>::twoPi
                                                   * sampleIndex * bpm / (60.0 * sampleRate));
            worst = juce::jmax(worst, std::abs(expected - offsets[b]));
            lowest = juce::jmin(lowest, offsets[b]);
            highest = juce::jmax(highest, offsets[b]);
        }
        checks.require(worst < 1.0e-3 && lowest < -5.9f && highest > 5.9f,
                       juce::String("stopped-transport synced Motion free-runs at ") + juce::String(bpm, 0)
                           + " BPM (max error " + juce::String(worst, 6) + " st)");
        std::cout << "Synced Motion with the transport stopped" << (withHost ? "" : ", no host tempo")
                  << ": follows " << bpm << " BPM within " << worst << " st, range "
                  << lowest << " .. " << highest << " st.\n";
    }
    checks.section("synced Motion free-runs at the host tempo (or 120 BPM) while the transport is stopped");
}

void checkTimeSignatureSync(Checks& checks)
{
    checks.require(ShiftyProcessor::barLengthInQuarters(3, 4) == 3.0
                       && ShiftyProcessor::barLengthInQuarters(6, 8) == 3.0
                       && ShiftyProcessor::barLengthInQuarters(7, 8) == 3.5
                       && ShiftyProcessor::barLengthInQuarters(4, 4) == 4.0
                       && ShiftyProcessor::barLengthInQuarters(0, 0) == 4.0,
                   "bar length follows the time signature");
    checks.require(std::abs(ShiftyProcessor::syncedMotionPhase(4.5, 3.0, 3.0, 3.0) - 0.5) < 1.0e-12
                       && std::abs(ShiftyProcessor::syncedMotionPhase(6.0, 6.0, 3.0, 3.0)) < 1.0e-12
                       && std::abs(ShiftyProcessor::syncedMotionPhase(6.75, 3.0, 3.0, 3.0) - 0.25) < 1.0e-12
                       && std::abs(ShiftyProcessor::syncedMotionPhase(7.25, 7.0, 3.5, 1.0) - 0.25) < 1.0e-12,
                   "synced phase restarts at the host bar start, including a bar line inside a block");

    constexpr double sampleRate = 48000.0;
    constexpr int block = 64;
    ShiftyProcessor processor;
    neutraliseMovement(processor);
    setActual(processor, "motionEnable", 1.0f);
    setActual(processor, "motionSync", 1.0f);
    setActual(processor, "motionRate", 4.0f);  // 1 Bar
    setActual(processor, "motionShape", 2.0f); // ramp up: -depth at each bar start
    setActual(processor, "motionDepth", 4.0f);
    prepareForTest(processor, sampleRate, block);
    TestHost host;
    host.playing = true;
    host.bpm = 120.0;
    host.numerator = 3;
    host.denominator = 4;
    std::vector<double> ppqAtBlockEnd;
    const auto offsets = renderMotionOffsets(processor, &host, sampleRate, block, 3000, &ppqAtBlockEnd);
    double worst = 0.0;
    for (size_t b = 0; b < offsets.size(); ++b) {
        const double inBar = std::fmod(ppqAtBlockEnd[b], 3.0); // bars of three quarters from ppq 0
        const double expected = 4.0 * (2.0 * inBar / 3.0 - 1.0);
        if (inBar < 0.01 || inBar > 2.99) continue; // skip the ramp's reset sample
        worst = juce::jmax(worst, std::abs(expected - offsets[b]));
    }
    checks.require(worst < 1.0e-3, "1 Bar Motion lasts one 3/4 bar and restarts on the bar line (max error "
                                       + juce::String(worst, 6) + " st)");
    std::cout << "1 Bar Motion in 3/4 at 120 BPM: 1.5 s cycle aligned to bar starts within " << worst << " st.\n";
    checks.section("Motion sync honours the host time signature and bar start");
}

// Motion depth and dropouts through the full processor (free 0.5 / 9.4 Hz sine).
double partialLevelDb(const std::vector<float>& x, int start, int length, double frequency, double sampleRate)
{
    std::complex<double> sum = 0.0;
    for (int i = 0; i < length; ++i)
        sum += static_cast<double>(x[static_cast<size_t>(start + i)])
             * std::polar(1.0, -juce::MathConstants<double>::twoPi * frequency * (start + i) / sampleRate);
    return 20.0 * std::log10(2.0 * std::abs(sum) / length + 1.0e-12);
}

std::vector<float> renderResonantMotion(double lfoHz, float depth, float bite, double seconds)
{
    constexpr double sampleRate = 48000.0;
    constexpr int period = 872; // 55.05 Hz with a whole-sample period
    const double f0 = sampleRate / period;
    ShiftyProcessor processor;
    processor.setEngine(EngineMode::classic, false); // the 0.4.3 Motion fix concerns CLASSIC
    neutraliseMovement(processor);
    setActual(processor, "shift", -6.0f);
    setActual(processor, "focus", 0.7f);
    setActual(processor, "bite", bite);
    setActual(processor, "drive", 0.0f);
    setActual(processor, "mix", 1.0f);
    setActual(processor, "motionEnable", depth > 0.0f ? 1.0f : 0.0f);
    setActual(processor, "motionSync", 0.0f);
    setActual(processor, "motionShape", 0.0f);
    setActual(processor, "motionFreeHz", static_cast<float>(lfoHz));
    setActual(processor, "motionDepth", depth);
    prepareForTest(processor, sampleRate, 256);
    const int latency = processor.getLatencySamples();
    const int total = static_cast<int>(seconds * sampleRate) + latency;
    std::vector<float> output;
    juce::MidiBuffer midi;
    const auto resonance = [](double f) {
        const auto bell = [f](double c, double w) { const double d = (f - c) / w; return std::exp(-0.5 * d * d); };
        return 18.0 * bell(700.0, 90.0) + 12.0 * bell(1800.0, 170.0);
    };
    std::vector<double> amplitude(300);
    for (int h = 1; h < 300; ++h) amplitude[static_cast<size_t>(h)] = std::pow(10.0, resonance(h * f0) / 20.0) / h;
    for (int cursor = 0; cursor < total; cursor += 256) {
        juce::AudioBuffer<float> audio(2, 256);
        for (int i = 0; i < 256; ++i) {
            const double phase = juce::MathConstants<double>::twoPi * f0 * (cursor + i) / sampleRate;
            double sample = 0.0;
            for (int h = 1; h * f0 < 16000.0; ++h) sample += amplitude[static_cast<size_t>(h)] * std::sin(h * phase);
            const auto value = static_cast<float>(0.05 * sample);
            audio.setSample(0, i, value);
            audio.setSample(1, i, value);
        }
        processor.processBlock(audio, midi);
        output.insert(output.end(), audio.getReadPointer(0), audio.getReadPointer(0) + 256);
    }
    output.erase(output.begin(), output.begin() + latency);
    return output;
}

void checkFastMotionDepth(Checks& checks)
{
    constexpr double sampleRate = 48000.0;
    constexpr int period = 872;
    const double partial = 13.0 * sampleRate / period; // 715 Hz, on the moving resonance's flank
    // Lock-in amplitude of the partial's dB trajectory at the LFO rate.
    auto modulation = [&](const std::vector<float>& x, double lfoHz) {
        const double span = std::floor(3.0 * lfoHz) / lfoHz;
        std::vector<double> level, time;
        for (int s = 48000; s + period < static_cast<int>((1.0 + span) * sampleRate); s += 16) {
            level.push_back(partialLevelDb(x, s, period, partial, sampleRate));
            time.push_back((s + period / 2.0) / sampleRate);
        }
        double mean = 0.0;
        for (auto v : level) mean += v;
        mean /= static_cast<double>(level.size());
        std::complex<double> sum = 0.0;
        for (size_t i = 0; i < level.size(); ++i)
            sum += (level[i] - mean) * std::polar(1.0, -juce::MathConstants<double>::twoPi * lfoHz * time[i]);
        const double window = juce::MathConstants<double>::pi * lfoHz * period / sampleRate;
        return 2.0 * std::abs(sum) / static_cast<double>(level.size()) / (std::sin(window) / window);
    };
    const auto slow = renderResonantMotion(0.5, 4.0f, 0.5f, 4.5);
    const auto fast = renderResonantMotion(9.4, 4.0f, 0.5f, 4.5);
    const double ratio = modulation(fast, 9.4) / modulation(slow, 0.5);
    checks.require(ratio > 0.8, "9.4 Hz Motion keeps its depth (" + juce::String(100.0 * ratio, 1)
                                    + "% of the 0.5 Hz modulation; 0.4.2 gave about 49%)");

    // Dropouts: windows far below the deepest level of the slow (0.5 Hz) sweep,
    // which visits the same -10..-2 st range quasi-statically. For this
    // fixture that minimum matches a 0.05 st static sweep within 0.3 dB.
    double staticMinimum = 1.0e9;
    for (int s = 48000; s + period < static_cast<int>(4.0 * sampleRate); s += 32)
        staticMinimum = juce::jmin(staticMinimum, partialLevelDb(slow, s, period, partial, sampleRate));
    int below = 0, windows = 0;
    double fastMinimum = 1.0e9;
    for (int s = 48000; s + period < static_cast<int>(4.0 * sampleRate); s += 32) {
        const double level = partialLevelDb(fast, s, period, partial, sampleRate);
        fastMinimum = juce::jmin(fastMinimum, level);
        ++windows;
        if (level < staticMinimum - 3.0) ++below;
    }
    const double fraction = static_cast<double>(below) / windows;
    checks.require(fraction < 0.02 && fastMinimum > staticMinimum - 20.0,
                   "fast Motion with BITE stays near the static range (" + juce::String(100.0 * fraction, 2)
                       + "% of windows >3 dB below, deepest " + juce::String(fastMinimum - staticMinimum, 1)
                       + " dB re the slow-sweep minimum)");
    std::cout << "Motion at 9.4 Hz: " << 100.0 * ratio << "% of 0.5 Hz modulation depth; "
              << 100.0 * fraction << "% windows >3 dB below the slow-sweep minimum, deepest "
              << fastMinimum - staticMinimum << " dB.\n";
    checks.section("fast Motion keeps its depth without deep partial dropouts");
}

juce::MemoryBlock presetData(const juce::String& marker)
{
    ShiftyProcessor source;
    setPattern(source, true);
    source.setPresetName(marker);
    return sessionOf(source);
}

void checkPresetLocationAndMigration(Checks& checks)
{
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("shifty-preset-root", "", false);
    checks.require(root.createDirectory().wasOk(), "create temporary user-data root");
    const auto legacy = root.getChildFile("SHIFTY").getChildFile("Presets");
    const auto current = root.getChildFile("Application Support").getChildFile("SHIFTY").getChildFile("Presets");
    checks.require(legacy.createDirectory().wasOk() && current.createDirectory().wasOk(), "create preset folders");
    const auto legacyA = presetData("Legacy A");
    legacy.getChildFile("A.shifty").replaceWithData(legacyA.getData(), legacyA.getSize());
    legacy.getChildFile("B.shifty").replaceWithText("legacy B");
    current.getChildFile("B.shifty").replaceWithText("current B");

    ShiftyProcessor processor;
    processor.setUserDataRootForTesting(root);
    checks.require(processor.userPresetDirectory() == current,
                   "user presets live in Application Support/SHIFTY/Presets");
    const auto files = processor.userPresetFiles();
    juce::StringArray names;
    for (const auto& file : files) names.add(file.getFileName());
    checks.require(names.contains("A.shifty") && names.contains("B.shifty") && files.size() == 2,
                   "legacy presets appear in the new folder");
    juce::MemoryBlock copied;
    current.getChildFile("A.shifty").loadFileAsData(copied);
    checks.require(copied == legacyA, "legacy preset copied byte for byte");
    checks.require(current.getChildFile("B.shifty").loadFileAsString() == "current B",
                   "an existing preset with the same name is not overwritten");
    checks.require(legacy.getChildFile("A.shifty").existsAsFile() && legacy.getChildFile("B.shifty").existsAsFile(),
                   "legacy presets are copied, not moved");
    checks.require(current.getChildFile(".legacy-presets-migrated").existsAsFile(), "migration marker written");

    current.getChildFile("A.shifty").deleteFile();
    ShiftyProcessor later;
    later.setUserDataRootForTesting(root);
    later.userPresetFiles();
    checks.require(!current.getChildFile("A.shifty").exists(), "migration runs once; deleted presets stay deleted");
    later.saveUserPreset("Saved Here");
    checks.require(current.getChildFile("Saved Here.shifty").existsAsFile()
                       && !legacy.getChildFile("Saved Here.shifty").exists(),
                   "new presets are saved in the new folder");
    checks.require(ShiftyProcessor::presetDirectoryForRoot(juce::File("/Users/x/Library")).getFullPathName()
                       == "/Users/x/Library/Application Support/SHIFTY/Presets",
                   "documented macOS preset path");
    root.deleteRecursively();
    checks.section("user presets in Application Support with a one-time, non-destructive migration");
}

void checkUserPresetUndo(Checks& checks)
{
    ShiftyProcessor processor;
    processor.loadFactoryPreset(2);
    const auto factory = valuesOf(processor);
    const auto factoryName = processor.currentPresetName();
    const juce::TemporaryFile fixture(".shifty");
    const auto data = presetData("Undoable User Preset");
    checks.require(fixture.getFile().replaceWithData(data.getData(), data.getSize()), "write user preset fixture");
    checks.require(processor.loadUserPreset(fixture.getFile()), "load user preset");
    const auto loaded = valuesOf(processor);
    checks.require(loaded != factory && processor.currentPresetName() == "Undoable User Preset",
                   "user preset applied");
    checks.require(processor.canUndo(), "loading a user preset keeps and extends the undo history");
    processor.undo();
    checks.require(valuesOf(processor) == factory && processor.currentPresetName() == factoryName,
                   "Undo restores the sound before the user preset");
    checks.require(processor.canUndo(), "earlier undo steps survive the user preset load");
    processor.redo();
    checks.require(valuesOf(processor) == loaded && processor.currentPresetName() == "Undoable User Preset",
                   "Redo re-applies the user preset");
    checks.section("loading a user preset is one undoable step");
}

void checkAutomationStaysOutOfUndo(Checks& checks)
{
    ShiftyProcessor processor;
    auto* shift = processor.parameters().getParameter("shift");
    auto* focus = processor.parameters().getParameter("focus");
    for (int i = 0; i < 20; ++i) {
        shift->setValueNotifyingHost(0.1f + 0.04f * static_cast<float>(i)); // host automation: no gestures
        processor.parameters().copyState();                                 // APVTS flush
    }
    checks.require(!processor.canUndo(), "host automation does not create undo steps");
    const auto focusBefore = focus->getValue();
    focus->beginChangeGesture();
    focus->setValueNotifyingHost(0.2f);
    focus->setValueNotifyingHost(0.25f);
    focus->endChangeGesture();
    checks.require(processor.canUndo(), "a control gesture is one undo step");
    shift->setValueNotifyingHost(0.9f); // automation continues after the gesture
    processor.parameters().copyState();
    const auto automatedShift = shift->getValue();
    processor.undo();
    checks.require(exactlyEqual(focus->getValue(), focusBefore), "Undo restores the gesture's parameter");
    checks.require(exactlyEqual(shift->getValue(), automatedShift), "Undo leaves automated parameters alone");
    checks.require(!processor.canUndo() && processor.canRedo(), "only the gesture was recorded");
    processor.redo();
    checks.require(exactlyEqual(focus->getValue(), focus->convertTo0to1(focus->convertFrom0to1(0.25f))),
                   "Redo re-applies the gesture's final value");
    checks.section("host automation stays out of Undo; control gestures, Random and presets are undoable");
}
} // namespace

inline int runFeatureTests()
{
    Checks checks;
    checkRandomLocksAndRanges(checks);
    checkSourceFirstDefaultsAndMigration(checks);
    checkUndoRedo(checks);
    checkPersistenceAndPresets(checks);
    checkFactoryAudio(checks);
    checkMotionBlockIndependence(checks);
    checkOutputSmoothing(checks);
    checkHostBypass(checks);
    checkStoppedTransportMotion(checks);
    checkTimeSignatureSync(checks);
    checkFastMotionDepth(checks);
    checkPresetLocationAndMigration(checks);
    checkUserPresetUndo(checks);
    checkAutomationStaysOutOfUndo(checks);
    if (checks.failures == 0)
        std::cout << "SHIFTY feature tests passed.\n";
    else
        std::cerr << "SHIFTY feature tests failed: " << checks.failures << " checks.\n";
    return checks.failures == 0 ? 0 : 1;
}

} // namespace shifty::tests
