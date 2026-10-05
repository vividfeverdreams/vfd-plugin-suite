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

struct ShiftyProcessor::SnapshotAction final : public juce::UndoableAction {
    SnapshotAction(ShiftyProcessor& ownerToUse, ParameterSnapshot beforeToUse, ParameterSnapshot afterToUse)
        : owner(ownerToUse), before(std::move(beforeToUse)), after(std::move(afterToUse)) {}

    bool perform() override
    {
        // The change has already been made when the action is recorded.
        if (std::exchange(firstPerform, false)) return true;
        owner.applySnapshot(after, before);
        return true;
    }

    bool undo() override
    {
        owner.applySnapshot(before, after);
        return true;
    }

    int getSizeInUnits() override { return 1; }

    ShiftyProcessor& owner;
    ParameterSnapshot before, after;
    bool firstPerform = true;
};

ShiftyProcessor::ShiftyProcessor()
  : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    state(*this, nullptr, "SHIFTY_STATE", createParameterLayout()),
    analyzerFft(std::make_unique<juce::dsp::FFT>(10))
{
    static_assert(std::size(ids) == parameterCount);
    for (size_t i = 0; i < std::size(ids); ++i)
        paramValues[i] = state.getRawParameterValue(ids[i]);
    // Hosts may query latency before prepareToPlay. The engine refines this
    // value for the actual sample rate during preparation.
    setLatencySamples(2048);
    state.state.setProperty("presetName", presetName, nullptr);
    // Gestures come only from SHIFTY's own controls; host automation does not
    // begin or end gestures, so it never becomes an undo step.
    for (auto* parameter : getParameters())
        parameter->addListener(this);
}

ShiftyProcessor::~ShiftyProcessor()
{
    for (auto* parameter : getParameters())
        parameter->removeListener(this);
}

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
    motionPhase = 0.0;
    motionDepth.reset(sampleRate, 0.02);
    motionDepth.setCurrentAndTargetValue(paramValues[8]->load() >= .5f ? paramValues[11]->load() : 0.0f);
    lastMotionOffset.store(0.0f, std::memory_order_relaxed);
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
    processAudio(buffer, false);
}

void ShiftyProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    processAudio(buffer, true);
}

double ShiftyProcessor::barLengthInQuarters(int numerator, int denominator) noexcept
{
    if (numerator <= 0 || denominator <= 0) return 4.0;
    return 4.0 * static_cast<double>(numerator) / static_cast<double>(denominator);
}

double ShiftyProcessor::syncedMotionPhase(double ppq, std::optional<double> barStartPpq,
                                          double barQuarters, double cycleQuarters) noexcept
{
    double position = ppq;
    if (barStartPpq.has_value() && barQuarters > 0.0) {
        // Position inside the current bar. Wrapping also covers a bar line
        // that falls inside the host block (constant time signature).
        position = ppq - *barStartPpq;
        position -= std::floor(position / barQuarters) * barQuarters;
    }
    const double cycles = position / (cycleQuarters > 0.0 ? cycleQuarters : 1.0);
    return cycles - std::floor(cycles);
}

void ShiftyProcessor::processAudio(juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept
{
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
    const bool isBypassed = forceBypass || val(14) >= .5f;
    if (isBypassed) p.mix = 0.0f;
    engine.setParameters(p);

    // Motion is evaluated for every sample and handed to the engine as an
    // unsmoothed offset; only its depth (and on/off) is ramped over 20 ms.
    // Synced motion locks to the host position while playing and free-runs
    // at the host tempo (120 BPM without one) when the transport is stopped.
    const bool motionEnabled = val(8) >= .5f;
    const bool sync = val(12) >= .5f;
    const int shape = juce::jlimit(0, 3, (int) std::round(val(9)));
    const int rate = juce::jlimit(0, 4, (int) std::round(val(10)));
    const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    double bpm = 120.0;
    bool playing = false;
    std::optional<double> ppq, barStart;
    int numerator = 4, denominator = 4;
    if (auto* playhead = getPlayHead()) {
        if (const auto position = playhead->getPosition()) {
            if (const auto hostBpm = position->getBpm())
                if (std::isfinite(*hostBpm) && *hostBpm > 0.0) bpm = *hostBpm;
            playing = position->getIsPlaying();
            if (const auto hostPpq = position->getPpqPosition())
                if (std::isfinite(*hostPpq)) ppq = *hostPpq;
            if (const auto hostBar = position->getPpqPositionOfLastBarStart())
                if (std::isfinite(*hostBar)) barStart = *hostBar;
            if (const auto signature = position->getTimeSignature()) {
                numerator = signature->numerator;
                denominator = signature->denominator;
            }
        }
    }
    static constexpr double quartersPerNote[] = {.25, .5, 1.0, 2.0};
    const double barQuarters = barLengthInQuarters(numerator, denominator);
    const double cycleQuarters = rate == 4 ? barQuarters : quartersPerNote[(size_t) rate];
    const bool hostLocked = sync && playing && ppq.has_value();
    const double quartersPerSample = bpm / (60.0 * sampleRate);
    const double cyclesPerSample = sync ? quartersPerSample / cycleQuarters
                                        : static_cast<double>(val(13)) / sampleRate;
    motionDepth.setTargetValue(motionEnabled ? val(11) : 0.0f);

    std::array<float, 64> offsets {};
    for (int offset = 0; offset < n;) {
        const int count = juce::jmin(static_cast<int>(offsets.size()), n - offset);
        for (int i = 0; i < count; ++i) {
            double phase = motionPhase;
            if (hostLocked)
                phase = syncedMotionPhase(*ppq + (offset + i) * quartersPerSample, barStart,
                                          barQuarters, cycleQuarters);
            motionPhase = phase + cyclesPerSample;
            motionPhase -= std::floor(motionPhase);
            offsets[(size_t) i] = motionDepth.getNextValue() * waveAt(phase, shape);
        }
        float* pointers[2] { buffer.getWritePointer(0, offset),
            channels > 1 ? buffer.getWritePointer(1, offset) : nullptr };
        juce::AudioBuffer<float> section(pointers, channels, count);
        engine.processBlock(section, offsets.data());
        offset += count;
        if (offset >= n) lastMotionOffset.store(offsets[(size_t) count - 1], std::memory_order_relaxed);
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
    assignPresetName(name.trim().isNotEmpty() ? name.trim() : "Untitled");
}

void ShiftyProcessor::assignPresetName(const juce::String& name)
{
    state.state.setProperty("presetName", name, nullptr);
    const juce::ScopedLock lock(presetNameMutex);
    presetName = name;
}

void ShiftyProcessor::syncPresetNameFromState()
{
    const juce::ScopedLock lock(presetNameMutex);
    presetName = state.state.getProperty("presetName", "Untitled").toString();
}

ShiftyProcessor::ParameterSnapshot ShiftyProcessor::captureSnapshot() const
{
    ParameterSnapshot snapshot;
    for (size_t i = 0; i < std::size(ids); ++i)
        if (const auto* parameter = state.getParameter(ids[i]))
            snapshot.values[i] = parameter->getValue();
    snapshot.presetName = currentPresetName();
    return snapshot;
}

void ShiftyProcessor::applySnapshot(const ParameterSnapshot& target, const ParameterSnapshot& other)
{
    // Only the parameters this step changed are restored. Values written since
    // by host automation (or other edits) are left alone.
    ++undoSuppression;
    for (size_t i = 0; i < std::size(ids); ++i) {
        if (juce::exactlyEqual(target.values[i], other.values[i])) continue;
        auto* parameter = state.getParameter(ids[i]);
        if (parameter == nullptr || juce::exactlyEqual(parameter->getValue(), target.values[i])) continue;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(target.values[i]);
        parameter->endChangeGesture();
    }
    if (target.presetName != other.presetName) assignPresetName(target.presetName);
    --undoSuppression;
}

void ShiftyProcessor::recordUndoStep(const ParameterSnapshot& before, const ParameterSnapshot& after,
                                     const juce::String& name)
{
    if (before.values == after.values && before.presetName == after.presetName) return;
    undoManager.beginNewTransaction(name);
    undoManager.perform(new SnapshotAction(*this, before, after));
    undoManager.beginNewTransaction();
}

void ShiftyProcessor::beginUndoableEdit()
{
    if (undoSuppression++ == 0) batchStart = captureSnapshot();
}

void ShiftyProcessor::endUndoableEdit(const juce::String& name)
{
    if (undoSuppression > 0 && --undoSuppression == 0)
        recordUndoStep(batchStart, captureSnapshot(), name);
}

void ShiftyProcessor::parameterGestureChanged(int, bool gestureIsStarting)
{
    // Called by SHIFTY's own controls (message thread). Random, preset loads
    // and Undo/Redo themselves suppress per-gesture recording.
    if (undoSuppression > 0) return;
    if (gestureIsStarting) {
        if (activeGestures++ == 0) gestureStart = captureSnapshot();
    } else if (activeGestures > 0 && --activeGestures == 0) {
        recordUndoStep(gestureStart, captureSnapshot(), "Change SHIFTY parameter");
    }
}

void ShiftyProcessor::setParameterAsGesture(const char* id, float value)
{
    if (auto* parameter = state.getParameter(id)) {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        parameter->endChangeGesture();
    }
}

void ShiftyProcessor::undo()
{
    undoManager.undo();
}

void ShiftyProcessor::redo()
{
    undoManager.redo();
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
    beginUndoableEdit();
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
        setParameterAsGesture(ids[i], values[i]);
    }
    setPresetName("Random Variation");
    endUndoableEdit("Randomize SHIFTY");
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
            // A restored host session starts a fresh edit history.
            undoManager.clearUndoHistory();
        }
    }
}

juce::File ShiftyProcessor::presetDirectoryForRoot(const juce::File& userDataRoot)
{
   #if JUCE_MAC
    return userDataRoot.getChildFile("Application Support").getChildFile("SHIFTY").getChildFile("Presets");
   #else
    return userDataRoot.getChildFile("SHIFTY").getChildFile("Presets");
   #endif
}

juce::File ShiftyProcessor::legacyPresetDirectoryForRoot(const juce::File& userDataRoot)
{
    return userDataRoot.getChildFile("SHIFTY").getChildFile("Presets");
}

int ShiftyProcessor::migrateLegacyPresets(const juce::File& legacyDirectory, const juce::File& newDirectory)
{
    // One-time and non-destructive: legacy files are copied, never moved or
    // deleted, and an existing preset of the same name is never overwritten.
    // A marker prevents presets the user later deletes from returning.
    if (legacyDirectory == newDirectory || !legacyDirectory.isDirectory()) return 0;
    const auto marker = newDirectory.getChildFile(".legacy-presets-migrated");
    if (marker.existsAsFile() || newDirectory.createDirectory().failed()) return 0;
    int copied = 0;
    bool complete = true;
    for (const auto& source : legacyDirectory.findChildFiles(juce::File::findFiles, false, "*.shifty")) {
        const auto destination = newDirectory.getChildFile(source.getFileName());
        if (destination.exists()) continue;
        if (source.copyFileTo(destination)) ++copied;
        else complete = false;
    }
    if (complete)
        marker.replaceWithText("Presets from " + legacyDirectory.getFullPathName()
                               + " were copied here once. The originals were left in place.\n");
    return copied;
}

void ShiftyProcessor::setUserDataRootForTesting(const juce::File& root)
{
    userDataRootOverride = root;
    presetMigrationChecked = false;
}

juce::File ShiftyProcessor::presetDirectory() const
{
    const auto root = userDataRootOverride != juce::File()
        ? userDataRootOverride
        : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    return presetDirectoryForRoot(root);
}

void ShiftyProcessor::migrateLegacyPresetsOnce() const
{
    if (std::exchange(presetMigrationChecked, true)) return;
    const auto root = userDataRootOverride != juce::File()
        ? userDataRootOverride
        : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    migrateLegacyPresets(legacyPresetDirectoryForRoot(root), presetDirectoryForRoot(root));
}

juce::File ShiftyProcessor::userPresetDirectory() const
{
    return presetDirectory();
}

void ShiftyProcessor::saveUserPreset(const juce::String& name)
{
    const auto legal = juce::File::createLegalFileName(name.trim());
    if (legal.isEmpty()) return;
    migrateLegacyPresetsOnce();
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
    // Apply the sound as one undoable step, like a factory preset. This
    // instance keeps its own randomization locks.
    beginUndoableEdit();
    for (const auto* id : ids) {
        const auto child = tree.getChildWithProperty("id", id);
        if (child.isValid() && child.hasProperty("value"))
            setParameterAsGesture(id, static_cast<float>(child.getProperty("value")));
        else if (juce::String(id) == "vowel")
            setParameterAsGesture(id, 0.0f); // Presets from before Vowel Color load it off.
    }
    assignPresetName(tree.getProperty("presetName", file.getFileNameWithoutExtension()).toString());
    endUndoableEdit("Load SHIFTY user preset");
    return true;
}

juce::Array<juce::File> ShiftyProcessor::userPresetFiles() const
{
    migrateLegacyPresetsOnce();
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
    const auto set = [this](const char* id, float value) { setParameterAsGesture(id, value); };
    beginUndoableEdit();
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
    setPresetName(p.name);
    endUndoableEdit("Load SHIFTY preset");
}

} // namespace shifty

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new shifty::ShiftyProcessor();
}
