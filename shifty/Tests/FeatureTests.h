#pragma once

#include "../Source/Plugin/ShiftyProcessor.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <set>
#include <vector>

namespace shifty::tests {

namespace {
constexpr std::array<const char*, 16> expectedParameterIds {{
    "shift", "focus", "bite", "drive", "mix", "subSafe", "subCut", "output",
    "motionEnable", "motionShape", "motionRate", "motionDepth", "motionSync",
    "motionFreeHz", "bypass", "vowel"
}};
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

void setPattern(ShiftyProcessor& processor, bool alternate)
{
    for (size_t i = 0; i < expectedParameterIds.size(); ++i) {
        const auto normalized = alternate ? 0.93f - static_cast<float>(i) * 0.043f
                                         : 0.07f + static_cast<float>(i) * 0.041f;
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
    for (const auto& child : current)
        if (child.hasProperty("id") && child["id"].toString() != "vowel")
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
                   "all 16 parameters are listed for randomization");
    for (const auto* id : expectedParameterIds) {
        checks.require(eligible.contains(id), juce::String("randomization includes ") + id);
        checks.require(processor.isParameterLocked(id)
                           == (juce::String(id) == "output" || juce::String(id) == "bypass" || juce::String(id) == "vowel"),
                       juce::String("default lock for ") + id);
    }

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
    for (int i = 0; i < 32; ++i) processor.randomizeParameters();
    checks.require(processor.parameters().getRawParameterValue("vowel")->load() == 0.0f,
                   "Random never introduces vowel color with the default locks");

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

    const auto legacy = legacySessionOf(source);
    auto legacyValues = originalValues;
    legacyValues.back() = 0.0f;
    setAllLocks(restored, true);
    restored.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
    reportValueDifferences(restored, legacyValues, "legacy roundtrip");
    checks.require(valuesOf(restored) == legacyValues, "v0.1 session retains its original settings and disables optional vowel color");
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
    checks.require(names.size() == 24, "factory bank has doubled from 12 to 24 presets");
    auto uniqueNames = names;
    uniqueNames.removeDuplicates(false);
    checks.require(uniqueNames.size() == names.size(), "factory preset names are unique");
    std::set<Values> factorySounds;
    for (int index = 0; index < names.size(); ++index) {
        setPattern(recipient, false);
        recipient.loadFactoryPreset(index);
        checks.require(recipient.parameters().getRawParameterValue("vowel")->load() == 0.0f,
                       "factory preset starts with source formants only: " + names[index]);
        const auto first = valuesOf(recipient);
        checks.require(recipient.currentPresetName() == names[index], "factory name matches index " + juce::String(index));
        checks.require(locksOf(recipient) == recipientLocks, "factory preset preserves parameter locks");
        setPattern(recipient, true);
        recipient.loadFactoryPreset(index);
        checks.require(valuesOf(recipient) == first, "factory preset fully specifies its sound: " + names[index]);
        checks.require(locksOf(recipient) == recipientLocks, "factory reload preserves parameter locks");
        checkLegalValues(checks, recipient, names[index]);
        factorySounds.insert(first);
    }
    checks.require(factorySounds.size() == static_cast<size_t>(names.size()), "all 24 factory sounds have distinct settings");
    checks.section("session persistence, old sessions, user presets, and all 24 deterministic factory presets");
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

std::vector<float> renderFreeMotion(int blockSize)
{
    constexpr double sampleRate = 48000.0;
    constexpr int samples = 32768;
    constexpr double tau = juce::MathConstants<double>::twoPi;
    ShiftyProcessor processor;
    processor.loadFactoryPreset(0);
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
    const auto smallBlocks = renderFreeMotion(64);
    for (const int otherBlockSize : {257, 2048}) {
        const auto otherBlocks = renderFreeMotion(otherBlockSize);
        double error = 0.0;
        float worst = 0.0f;
        for (size_t i = 0; i < smallBlocks.size(); ++i) {
            const auto difference = smallBlocks[i] - otherBlocks[i];
            error += static_cast<double>(difference) * difference;
            worst = juce::jmax(worst, std::abs(difference));
        }
        const auto rms = std::sqrt(error / static_cast<double>(smallBlocks.size()));
        checks.require(std::isfinite(rms) && rms < 1.0e-5 && worst < 3.0e-4f,
                       "20 Hz free Motion is independent of host block size 64 vs "
                           + juce::String(otherBlockSize) + " (RMS "
                           + juce::String(rms, 9) + ", max " + juce::String(worst, 9) + ")");
        std::cout << "20 Hz Motion, 64 vs " << otherBlockSize << " sample host blocks: RMS difference "
                  << rms << ", largest difference " << worst << ".\n";
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
    if (checks.failures == 0)
        std::cout << "SHIFTY feature tests passed.\n";
    else
        std::cerr << "SHIFTY feature tests failed: " << checks.failures << " checks.\n";
    return checks.failures == 0 ? 0 : 1;
}

} // namespace shifty::tests
