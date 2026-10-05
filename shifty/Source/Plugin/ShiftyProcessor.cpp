#include "ShiftyProcessor.h"
#include "../UI/ShiftyEditor.h"

#include <cmath>

namespace shifty {

namespace {
constexpr const char* ids[] = {
    "shift", "focus", "bite", "drive", "mix", "subSafe", "subCut", "output",
    "motionEnable", "motionShape", "motionRate", "motionDepth", "motionSync",
    "motionFreeHz", "bypass", "vowel"
};
constexpr double twoPi = juce::MathConstants<double>::twoPi;

float waveAt(double cycles, int shape) noexcept
{
    const auto phase = cycles - std::floor(cycles);
    switch (shape) {
        case 1: return (float) (1.0 - 4.0 * std::abs(phase - 0.5));
        case 2: return (float) (2.0 * phase - 1.0);
        case 3: return (float) (1.0 - 2.0 * phase);
        default: return (float) std::sin(twoPi * phase);
    }
}

struct FactoryPreset {
    const char* name;
    float shift, focus, bite, drive, mix;
    bool motion;
    int shape, rate;
    float depth;
    bool sync = true;
    float freeHz = 2.0f;
    float subCut = 120.0f;
};

constexpr FactoryPreset presets[] = {
    {"Rubber Throat", -4.0f, .55f, .35f, .20f, 1.0f, false, 1, 1, 6.0f},
    {"Basement Growl", -8.0f, .72f, .58f, .30f, 1.0f, false, 1, 2, 5.0f},
    {"Goblin Talk", 4.0f, .67f, .66f, .22f, .92f, true, 1, 1, 3.0f},
    {"Hollow Reese", -5.5f, .34f, .42f, .08f, .85f, false, 0, 2, 4.0f},
    {"Elastic Bite", 6.0f, .76f, .81f, .34f, .95f, false, 1, 1, 6.0f},
    {"Dark Chamber", -10.0f, .26f, .30f, .10f, 1.0f, false, 0, 3, 4.0f},
    {"Vowel Roll", -2.0f, .61f, .56f, .18f, 1.0f, true, 0, 1, 7.0f},
    {"Motor Mouth", 1.0f, .66f, .52f, .31f, 1.0f, true, 1, 0, 5.0f},
    {"Slow Lurch", -6.0f, .52f, .45f, .25f, 1.0f, true, 0, 3, 8.0f},
    {"Bright Chew", 7.0f, .78f, .57f, .16f, .9f, false, 1, 1, 4.0f},
    {"Subtle Shape", -2.5f, .46f, .18f, .0f, .55f, false, 0, 2, 3.0f},
    {"Riser Syllable", 3.0f, .62f, .65f, .26f, 1.0f, true, 2, 2, 8.0f},
    {"Midrange Nudge", 1.5f, .58f, .22f, .06f, .72f, false, 0, 2, 1.5f},
    {"Nasal Fold", 3.5f, .84f, .62f, .12f, .90f, false, 1, 1, 2.5f},
    {"Hollow Knock", -3.0f, .22f, .56f, .09f, .82f, false, 0, 2, 2.0f},
    {"Glass Jaw", 8.5f, .70f, .72f, .18f, .78f, false, 1, 1, 3.0f},
    {"Warm Snarl", -2.0f, .65f, .48f, .42f, .88f, false, 0, 2, 2.0f},
    {"Narrow Grit", -1.0f, .92f, .80f, .26f, .76f, false, 1, 1, 1.0f},
    {"Pocket Wobble", -2.5f, .60f, .40f, .10f, .92f, true, 0, 2, 2.8f},
    {"Short Croak", -4.5f, .74f, .68f, .24f, .95f, true, 3, 1, 3.5f},
    {"Talking Metal", 2.0f, .87f, .76f, .28f, .86f, true, 1, 0, 4.0f},
    {"Liquid Twitch", .5f, .44f, .36f, .05f, .80f, true, 0, 1, 2.2f, false, 3.7f},
    {"Lazy Chew", -1.5f, .52f, .44f, .15f, .90f, true, 1, 3, 3.2f, false, .63f},
    {"Ratcheted Vowel", 2.5f, .78f, .60f, .20f, .94f, true, 2, 0, 4.5f}
};
}

juce::AudioProcessorValueTreeState::ParameterLayout ShiftyProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto f = [&layout] (const char* id, const char* name, float lo, float hi, float step, float value) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id, 1}, name,
            juce::NormalisableRange<float>(lo, hi, step), value));
    };
    auto b = [&layout] (const char* id, const char* name, bool value) {
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, 1}, name, value));
    };
    f("shift", "Shift", -12.0f, 12.0f, .01f, -4.0f);
    f("focus", "Focus", 0.0f, 1.0f, .001f, .55f);
    f("bite", "Bite", 0.0f, 1.0f, .001f, .35f);
    f("drive", "Drive", 0.0f, 1.0f, .001f, .20f);
    f("mix", "Mix", 0.0f, 1.0f, .001f, 1.0f);
    b("subSafe", "Sub Safe", true);
    f("subCut", "Sub Safe Frequency", 60.0f, 200.0f, 1.0f, 120.0f);
    f("output", "Output", -24.0f, 6.0f, .01f, 0.0f);
    b("motionEnable", "Motion Enabled", false);
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"motionShape", 1}, "Motion Shape",
        juce::StringArray{"Sine", "Triangle", "Ramp Up", "Ramp Down"}, 1));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"motionRate", 1}, "Motion Rate",
        juce::StringArray{"1/16", "1/8", "1/4", "1/2", "1 Bar"}, 1));
    f("motionDepth", "Motion Depth", 0.0f, 12.0f, .01f, 6.0f);
    b("motionSync", "Motion Sync", true);
    f("motionFreeHz", "Motion Free Rate", .05f, 20.0f, .01f, 2.0f);
    b("bypass", "Bypass", false);
    // Append new controls so existing host parameter indices remain stable.
    b("vowel", "Vowel Color", false);
    return layout;
}

ShiftyProcessor::ShiftyProcessor()
  : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    state(*this, &undoManager, "SHIFTY_STATE", createParameterLayout()),
    analyzerFft(std::make_unique<juce::dsp::FFT>(10))
{
    static_assert(std::size(ids) == parameterCount);
    for (size_t i = 0; i < std::size(ids); ++i)
        paramValues[i] = state.getRawParameterValue(ids[i]);
    // Hosts may query latency before prepareToPlay. The engine refines this
    // value for the actual sample rate during preparation.
    setLatencySamples(2048);
    state.state.setProperty("presetName", presetName, nullptr);
}

ShiftyProcessor::~ShiftyProcessor() = default;

bool ShiftyProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto in = layout.getMainInputChannelSet();
    const auto out = layout.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono()
                         || in == juce::AudioChannelSet::stereo());
}

void ShiftyProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate, samplesPerBlock, getTotalNumInputChannels());
    setLatencySamples(engine.getLatencySamples());
    analyzerInput.fill(0.0f);
    analyzerOutput.fill(0.0f);
    analyzerInputWrite = analyzerOutputWrite = 0;
    freeMotionPhase = 0.0;
    motionPhaseIncrement = 0.0;
    motionSamplesUntilUpdate = 0;
    heldMotionWave = 0.0f;
    outputGain.reset(sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue(paramValues[14]->load() >= .5f
        ? 1.0f : juce::Decibels::decibelsToGain(paramValues[7]->load()));
    for (auto& v : inputSpectrum) v.store(0.0f, std::memory_order_relaxed);
    for (auto& v : outputSpectrum) v.store(0.0f, std::memory_order_relaxed);
}

void ShiftyProcessor::releaseResources() {}

void ShiftyProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    juce::ScopedNoDenormals noDenormals;
    const auto n = buffer.getNumSamples();
    const auto channels = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
    if (n <= 0 || channels <= 0) return;
    for (int c = channels; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);

    for (int c = 0; c < channels; ++c)
        inputPeaks[(size_t)c].store(buffer.getMagnitude(c, 0, n), std::memory_order_relaxed);
    if (channels == 1) inputPeaks[1].store(inputPeaks[0].load(), std::memory_order_relaxed);
    updateSpectrum(buffer, false);

    const auto val = [this](size_t i) noexcept { return paramValues[i]->load(std::memory_order_relaxed); };
    EngineParams p;
    p.shiftSemitones = val(0);
    p.focus = val(1);
    p.bite = val(2);
    p.drive = val(3);
    p.mix = val(4);
    p.subSafe = val(5) >= .5f;
    p.subCutHz = val(6);
    p.vowel = val(15) >= .5f;
    const bool isBypassed = val(14) >= .5f;
    if (isBypassed) p.mix = 0.0f;

    const bool motionEnabled = val(8) >= .5f;
    const int shape = juce::jlimit(0, 3, (int) std::round(val(9)));
    double phase = freeMotionPhase;
    double cyclesPerSample = 0.0;
    bool useHostPhase = false;
    if (motionEnabled) {
        const bool sync = val(12) >= .5f;
        if (sync) {
            static constexpr double quartersPerCycle[] = {.25, .5, 1.0, 2.0, 4.0};
            const auto qpc = quartersPerCycle[(size_t) juce::jlimit(0, 4, (int)std::round(val(10)))];
            double bpm = 120.0;
            bool playing = false;
            if (auto* playhead = getPlayHead()) {
                if (const auto position = playhead->getPosition()) {
                    if (const auto hostBpm = position->getBpm()) bpm = *hostBpm;
                    playing = position->getIsPlaying();
                    if (playing && position->getPpqPosition()) {
                        phase = *position->getPpqPosition() / qpc;
                        useHostPhase = true;
                    }
                }
            }
            if (playing) cyclesPerSample = bpm / (60.0 * getSampleRate() * qpc);
        } else {
            cyclesPerSample = val(13) / getSampleRate();
        }
    }

    // Keep modulation on a fixed sample cadence, including when a host splits
    // its buffers for automation or renders with a much larger buffer.
    const float baseShift = p.shiftSemitones;
    for (int offset = 0; offset < n;) {
        if (motionSamplesUntilUpdate == 0) {
            const double tickPhase = useHostPhase ? phase + offset * cyclesPerSample
                                                  : freeMotionPhase;
            heldMotionWave = waveAt(tickPhase, shape);
            freeMotionPhase = tickPhase - std::floor(tickPhase);
            motionPhaseIncrement = 64.0 * cyclesPerSample;
            motionSamplesUntilUpdate = 64;
        }
        p.shiftSemitones = juce::jlimit(-12.0f, 12.0f,
            baseShift + (motionEnabled ? val(11) * heldMotionWave : 0.0f));
        engine.setParameters(p);
        const int count = juce::jmin(motionSamplesUntilUpdate, n - offset);
        float* pointers[2] { buffer.getWritePointer(0, offset),
            channels > 1 ? buffer.getWritePointer(1, offset) : nullptr };
        juce::AudioBuffer<float> section(pointers, channels, count);
        engine.processBlock(section);
        offset += count;
        motionSamplesUntilUpdate -= count;
        if (motionSamplesUntilUpdate == 0) {
            freeMotionPhase += motionPhaseIncrement;
            freeMotionPhase -= std::floor(freeMotionPhase);
        }
    }

    const float gain = isBypassed ? 1.0f : juce::Decibels::decibelsToGain(val(7));
    outputGain.setTargetValue(gain);
    for (int i = 0; i < n; ++i) {
        const float smoothedGain = outputGain.getNextValue();
        for (int c = 0; c < channels; ++c)
            buffer.setSample(c, i, buffer.getSample(c, i) * smoothedGain);
    }
    for (int c = 0; c < channels; ++c)
        outputPeaks[(size_t)c].store(buffer.getMagnitude(c, 0, n), std::memory_order_relaxed);
    if (channels == 1) outputPeaks[1].store(outputPeaks[0].load(), std::memory_order_relaxed);
    updateSpectrum(buffer, true);
}

juce::AudioProcessorEditor* ShiftyProcessor::createEditor()
{
    return new ShiftyEditor(*this);
}

void ShiftyProcessor::copySpectrum(std::array<float, 64>& input,
                                   std::array<float, 64>& output) const noexcept
{
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = inputSpectrum[i].load(std::memory_order_relaxed);
        output[i] = outputSpectrum[i].load(std::memory_order_relaxed);
    }
}

void ShiftyProcessor::updateSpectrum(const juce::AudioBuffer<float>& buffer, bool output) noexcept
{
    auto& samples = output ? analyzerOutput : analyzerInput;
    auto& index = output ? analyzerOutputWrite : analyzerInputWrite;
    auto& destination = output ? outputSpectrum : inputSpectrum;
    constexpr int size = 1024;
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        samples[(size_t)index] = buffer.getSample(0, i);
        if (++index != size) continue;
        index = 0;
        fftScratch.fill(0.0f);
        for (int j = 0; j < size; ++j)
            fftScratch[(size_t)j] = samples[(size_t)j]
                * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float)j / size));
        analyzerFft->performFrequencyOnlyForwardTransform(fftScratch.data(), true);
        std::array<float, 64> bands {};
        const auto nyquist = (float)(getSampleRate() * .5);
        const auto denominator = std::log(nyquist / 20.0f);
        for (int k = 1; k < size / 2; ++k) {
            const auto hz = (float)k * (float)getSampleRate() / size;
            const int bucket = juce::jlimit(0, 63,
                (int)(63.0f * std::log(juce::jmax(20.0f, hz) / 20.0f) / denominator));
            bands[(size_t)bucket] = juce::jmax(bands[(size_t)bucket], fftScratch[(size_t)k]);
        }
        float peak = 0.0f;
        for (auto v : bands) peak = juce::jmax(peak, v);
        for (size_t b = 0; b < bands.size(); ++b)
            destination[b].store(peak < 1.0e-7f ? 0.0f : juce::jlimit(0.0f, 1.0f, bands[b] / peak),
                                 std::memory_order_relaxed);
    }
}

juce::String ShiftyProcessor::currentPresetName() const
{
    const juce::ScopedLock lock(presetNameMutex);
    return presetName;
}

void ShiftyProcessor::setPresetName(const juce::String& name)
{
    const auto clean = name.trim().isNotEmpty() ? name.trim() : "Untitled";
    state.state.setProperty("presetName", clean, &undoManager);
    const juce::ScopedLock lock(presetNameMutex);
    presetName = clean;
}

void ShiftyProcessor::syncPresetNameFromState()
{
    const juce::ScopedLock lock(presetNameMutex);
    presetName = state.state.getProperty("presetName", "Untitled").toString();
}

void ShiftyProcessor::undo()
{
    state.copyState();
    undoManager.undo();
    syncPresetNameFromState();
}

void ShiftyProcessor::redo()
{
    state.copyState();
    undoManager.redo();
    syncPresetNameFromState();
}

juce::StringArray ShiftyProcessor::randomizableParameterIds()
{
    juce::StringArray result;
    for (const auto* id : ids) result.add(id);
    return result;
}

bool ShiftyProcessor::isParameterLocked(const juce::String& id) const
{
    for (size_t i = 0; i < std::size(ids); ++i)
        if (id == ids[i]) return (randomLocks.load() & (1u << i)) != 0;
    return false;
}

void ShiftyProcessor::setParameterLocked(const juce::String& id, bool locked)
{
    for (size_t i = 0; i < std::size(ids); ++i) {
        if (id != ids[i]) continue;
        if (locked) randomLocks.fetch_or(1u << i);
        else randomLocks.fetch_and(~(1u << i));
        updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
        return;
    }
}

void ShiftyProcessor::randomizeParameters()
{
    const auto locks = randomLocks.load();
    if ((locks & allRandomLocks) == allRandomLocks) return;
    state.copyState();
    undoManager.beginNewTransaction("Randomize SHIFTY");
    auto range = [this](float lo, float hi) {
        return lo + variationRandom.nextFloat() * (hi - lo);
    };
    // Character and movement ranges are chosen for quick mid-bass variations.
    // Output, bypass and optional vowel color are included only when unlocked.
    const float randomShift = range(2.0f, 12.0f) * (variationRandom.nextBool() ? 1.0f : -1.0f);
    const float values[] = {
        randomShift, range(.35f, 1.0f), range(.35f, 1.0f),
        range(0.0f, .65f), range(.70f, 1.0f), variationRandom.nextBool() ? 1.0f : 0.0f,
        range(60.0f, 200.0f), range(-12.0f, 0.0f), variationRandom.nextBool() ? 1.0f : 0.0f,
        (float)variationRandom.nextInt(4), (float)variationRandom.nextInt(5),
        range(.5f, 8.0f), variationRandom.nextBool() ? 1.0f : 0.0f,
        std::exp(range(std::log(.1f), std::log(10.0f))),
        variationRandom.nextBool() ? 1.0f : 0.0f,
        variationRandom.nextBool() ? 1.0f : 0.0f
    };
    static_assert(std::size(values) == std::size(ids));
    for (size_t i = 0; i < std::size(ids); ++i) {
        if ((locks & (1u << i)) != 0) continue;
        if (auto* parameter = state.getParameter(ids[i])) {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(values[i]));
            parameter->endChangeGesture();
        }
    }
    state.copyState();
    setPresetName("Random Variation");
    undoManager.beginNewTransaction();
}

void ShiftyProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto tree = state.copyState();
    tree.setProperty("schemaVersion", 1, nullptr);
    tree.setProperty("engineVersion", 3, nullptr);
    tree.setProperty("presetName", currentPresetName(), nullptr);
    tree.setProperty("randomLockMask", (int)randomLocks.load(), nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, dest);
}

void ShiftyProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size)) {
        auto tree = juce::ValueTree::fromXml(*xml);
        if (tree.isValid() && tree.getType() == state.state.getType()
            && (int)tree.getProperty("schemaVersion", 1) <= 1) {
            const bool missingVowel = !tree.getChildWithProperty("id", "vowel").isValid();
            if (missingVowel) {
                // APVTS otherwise retains the current value of a missing parameter.
                // Old sessions must consistently open with source formant shifting.
                juce::ValueTree vowel("PARAM");
                vowel.setProperty("id", "vowel", nullptr);
                vowel.setProperty("value", 0.0f, nullptr);
                tree.addChild(vowel, -1, nullptr);
            }
            auto locks = (uint32_t)(int)tree.getProperty("randomLockMask", (int)defaultRandomLocks);
            if (missingVowel) locks |= (1u << 15);
            randomLocks.store(locks & allRandomLocks);
            state.replaceState(tree);
            syncPresetNameFromState();
        }
    }
}

juce::File ShiftyProcessor::presetDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("SHIFTY").getChildFile("Presets");
}

void ShiftyProcessor::saveUserPreset(const juce::String& name)
{
    const auto legal = juce::File::createLegalFileName(name.trim());
    if (legal.isEmpty()) return;
    auto dir = presetDirectory();
    if (dir.createDirectory().failed()) return;
    setPresetName(name);
    juce::MemoryBlock block;
    getStateInformation(block);
    dir.getChildFile(legal + ".shifty").replaceWithData(block.getData(), block.getSize());
}

bool ShiftyProcessor::loadUserPreset(const juce::File& file)
{
    if (!file.existsAsFile() || file.getFileExtension() != ".shifty") return false;
    juce::MemoryBlock data;
    if (!file.loadFileAsData(data)) return false;
    const auto xml = getXmlFromBinary(data.getData(), (int)data.getSize());
    if (!xml) return false;
    const auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid() || tree.getType() != state.state.getType()
        || (int)tree.getProperty("schemaVersion", 1) > 1) return false;
    const auto locks = randomLocks.load();
    setStateInformation(data.getData(), (int)data.getSize());
    randomLocks.store(locks);
    return true;
}

juce::Array<juce::File> ShiftyProcessor::userPresetFiles() const
{
    return presetDirectory().findChildFiles(juce::File::findFiles, false, "*.shifty");
}

juce::StringArray ShiftyProcessor::factoryPresetNames()
{
    juce::StringArray names;
    for (const auto& preset : presets) names.add(preset.name);
    return names;
}

void ShiftyProcessor::loadFactoryPreset(int index)
{
    if (!juce::isPositiveAndBelow(index, (int)std::size(presets))) return;
    const auto& p = presets[(size_t)index];
    const auto set = [this](const char* id, float value) {
        if (auto* parameter = state.getParameter(id)) {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
            parameter->endChangeGesture();
        }
    };
    state.copyState();
    undoManager.beginNewTransaction("Load SHIFTY preset");
    set("shift", p.shift);
    set("focus", p.focus);
    set("bite", p.bite);
    set("drive", p.drive);
    set("mix", p.mix);
    set("motionEnable", p.motion ? 1.0f : 0.0f);
    set("motionShape", (float)p.shape);
    set("motionRate", (float)p.rate);
    set("motionDepth", p.depth);
    set("subSafe", 1.0f);
    set("subCut", p.subCut);
    set("output", 0.0f);
    set("motionSync", p.sync ? 1.0f : 0.0f);
    set("motionFreeHz", p.freeHz);
    set("bypass", 0.0f);
    set("vowel", 0.0f);
    state.copyState();
    setPresetName(p.name);
    undoManager.beginNewTransaction();
}

} // namespace shifty

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new shifty::ShiftyProcessor();
}
