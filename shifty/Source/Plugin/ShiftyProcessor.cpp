#include "ShiftyProcessor.h"
#include "../UI/ShiftyEditor.h"

#include <cmath>

namespace shifty {

namespace {
// Parameter IDs. Append only: hosts and saved projects address them by ID and
// (for Audio Units) by order. The first 16 are the 0.4.2 parameters.
constexpr const char* ids[] = {
    "shift", "focus", "bite", "drive", "mix", "subSafe", "subCut", "output",
    "motionEnable", "motionShape", "motionRate", "motionDepth", "motionSync",
    "motionFreeHz", "bypass", "vowel",
    // 0.5.0
    "vowelMorph", "voiceSet", "character", "talk", "envAmount", "envAttack", "envRelease",
    "yoiAmount", "yoiFrom", "yoiTo", "yoiTime", "yoiShift", "motionTarget"
};
enum Index : size_t {
    iShift, iFocus, iBite, iDrive, iMix, iSubSafe, iSubCut, iOutput,
    iMotionEnable, iMotionShape, iMotionRate, iMotionDepth, iMotionSync,
    iMotionFreeHz, iBypass, iVowel,
    iVowelMorph, iVoiceSet, iCharacter, iTalk, iEnvAmount, iEnvAttack, iEnvRelease,
    iYoiAmount, iYoiFrom, iYoiTo, iYoiTime, iYoiShift, iMotionTarget
};
constexpr double twoPi = juce::MathConstants<double>::twoPi;
constexpr int engineVersion = 4;
constexpr const char* defaultFolder = "Talk";
constexpr const char* defaultPreset = "Rubber Throat";

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

int engineSlot(EngineMode mode, bool lowLatency) noexcept
{
    return static_cast<int>(mode) * 2 + (mode == EngineMode::classic && lowLatency ? 1 : 0);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout ShiftyProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto f = [&layout] (const char* id, const char* name, float lo, float hi, float step, float value, int version = 1) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id, version}, name,
            juce::NormalisableRange<float>(lo, hi, step), value));
    };
    auto skewed = [&layout] (const char* id, const char* name, float lo, float hi, float step, float value, float centre) {
        juce::NormalisableRange<float> range(lo, hi, step);
        range.setSkewForCentre(centre);
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, 2}, name, range, value));
    };
    auto b = [&layout] (const char* id, const char* name, bool value) {
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, 1}, name, value));
    };
    auto choice = [&layout] (const char* id, const char* name, juce::StringArray options, int value, int version = 1) {
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, version}, name, options, value));
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
    choice("motionShape", "Motion Shape", {"Sine", "Triangle", "Ramp Up", "Ramp Down"}, 1);
    choice("motionRate", "Motion Rate", {"1/16", "1/8", "1/4", "1/2", "1 Bar"}, 1);
    f("motionDepth", "Motion Depth", 0.0f, 12.0f, .01f, 6.0f);
    b("motionSync", "Motion Sync", true);
    f("motionFreeHz", "Motion Free Rate", .05f, 20.0f, .01f, 2.0f);
    b("bypass", "Bypass", false);
    // Append new controls so existing host parameter indices remain stable.
    b("vowel", "Vowel Color", false);
    // 0.5.0: TALK vowels, macros, envelope and YOI, Motion target.
    f("vowelMorph", "Vowel Morph", 0.0f, 4.0f, .001f, 2.0f, 2);
    choice("voiceSet", "Voice", {"Human", "Growl", "Wow"}, 0, 2);
    f("character", "Character", 0.0f, 1.0f, .001f, 0.0f, 2);
    f("talk", "Talk", 0.0f, 1.0f, .001f, 0.0f, 2);
    f("envAmount", "Envelope Amount", -1.0f, 1.0f, .001f, 0.0f, 2);
    skewed("envAttack", "Envelope Attack", 0.5f, 100.0f, .01f, 5.0f, 10.0f);
    skewed("envRelease", "Envelope Release", 10.0f, 1000.0f, .1f, 150.0f, 150.0f);
    f("yoiAmount", "Yoi Amount", 0.0f, 1.0f, .001f, 0.0f, 2);
    f("yoiFrom", "Yoi From", 0.0f, 4.0f, .001f, 0.0f, 2);
    f("yoiTo", "Yoi To", 0.0f, 4.0f, .001f, 4.0f, 2);
    skewed("yoiTime", "Yoi Time", 20.0f, 1000.0f, .1f, 200.0f, 200.0f);
    f("yoiShift", "Yoi Shift", -12.0f, 12.0f, .01f, 0.0f, 2);
    choice("motionTarget", "Motion Target", {"Shift", "Vowel", "Both"}, 0, 2);
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
    originalEngine.configure(FormantEngine::Mode::original042, false);
    classicEngine.configure(FormantEngine::Mode::latest, false);
    classicLowLatencyEngine.configure(FormantEngine::Mode::latest, true);
    state.state.setProperty("presetName", presetName, nullptr);
    // New instances open on TALK with its Rubber Throat sound. Saved projects
    // replace all of this in setStateInformation().
    ++undoSuppression;
    if (const int index = factoryPresetIndex(defaultFolder, defaultPreset); index >= 0)
        applyFactoryPreset(factoryBank()[(size_t) index]);
    --undoSuppression;
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

int ShiftyProcessor::latencyFor(EngineMode mode, bool lowLatency, double sampleRate) noexcept
{
    switch (mode) {
        case EngineMode::talk: return 0;
        case EngineMode::mouth: return MouthEngine::latencyFor(sampleRate);
        case EngineMode::classic: return FormantEngine::latencyFor(sampleRate, lowLatency);
        case EngineMode::original: break;
    }
    return FormantEngine::latencyFor(sampleRate, false);
}

double ShiftyProcessor::getTailLengthSeconds() const
{
    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    return 2.0 * getLatencySamples() / rate + 0.05;
}

void ShiftyProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const int channels = getTotalNumInputChannels();
    originalEngine.prepare(sampleRate, samplesPerBlock, channels);
    classicEngine.prepare(sampleRate, samplesPerBlock, channels);
    classicLowLatencyEngine.prepare(sampleRate, samplesPerBlock, channels);
    talkEngine.prepare(sampleRate, samplesPerBlock, channels);
    mouthEngine.prepare(sampleRate, samplesPerBlock, channels);
    envelopeFollower.prepare(sampleRate);
    yoiSweep.prepare(sampleRate);
    setLatencySamples(latencyFor(engineMode(), classicLowLatency(), sampleRate));
    activeEngine = -1;
    switchFadeOut = 0;
    switchGain = 1.0f;
    analyzerInput.fill(0.0f);
    analyzerOutput.fill(0.0f);
    analyzerInputWrite = analyzerOutputWrite = 0;
    motionPhase = 0.0;
    motionDepth.reset(sampleRate, 0.02);
    motionDepth.setCurrentAndTargetValue(paramValues[iMotionEnable]->load() >= .5f ? paramValues[iMotionDepth]->load() : 0.0f);
    originalMotionPhase = 0.0;
    originalPhaseIncrement = 0.0;
    originalSamplesUntilUpdate = 0;
    originalHeldWave = 0.0f;
    lastMotionOffset.store(0.0f, std::memory_order_relaxed);
    outputGain.reset(sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue(paramValues[iBypass]->load() >= .5f
        ? 1.0f : juce::Decibels::decibelsToGain(paramValues[iOutput]->load()));
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

    // Engine changes arrive from the message thread. Fade the old engine out
    // over one block, then start the new one from a clean state and fade it in.
    const auto wantedMode = engineMode();
    const bool wantedLowLatency = wantedMode == EngineMode::classic && classicLowLatency();
    const int wanted = engineSlot(wantedMode, wantedLowLatency);
    bool fadingOut = false;
    if (activeEngine < 0 || wanted == activeEngine) {
        activeEngine = wanted;
        switchFadeOut = 0;
    } else if (switchFadeOut == 0) {
        fadingOut = true; // this block still runs the old engine
        switchFadeOut = 1;
    } else {
        switch (wantedMode) {
            case EngineMode::original: originalEngine.reset(); break;
            case EngineMode::classic: (wantedLowLatency ? classicLowLatencyEngine : classicEngine).reset(); break;
            case EngineMode::talk: talkEngine.reset(); break;
            case EngineMode::mouth: mouthEngine.reset(); break;
        }
        envelopeFollower.reset();
        yoiSweep.reset();
        activeEngine = wanted;
        switchFadeOut = 0;
        switchGain = 0.0f;
        switchStep = static_cast<float>(1.0 / (0.02 * (getSampleRate() > 0.0 ? getSampleRate() : 48000.0)));
    }
    const auto runMode = static_cast<EngineMode>(activeEngine / 2);
    const bool runLowLatency = (activeEngine % 2) == 1;

    const bool isBypassed = forceBypass || paramValues[iBypass]->load(std::memory_order_relaxed) >= .5f;
    if (runMode == EngineMode::original)
        processOriginal(buffer, channels, isBypassed);
    else
        processLatest(buffer, channels, isBypassed, runMode, runLowLatency);

    if (fadingOut || switchGain < 1.0f) {
        for (int i = 0; i < n; ++i) {
            float gain;
            if (fadingOut) gain = 1.0f - static_cast<float>(i + 1) / static_cast<float>(n);
            else { switchGain = juce::jmin(1.0f, switchGain + switchStep); gain = switchGain; }
            for (int c = 0; c < channels; ++c)
                buffer.setSample(c, i, buffer.getSample(c, i) * gain);
        }
    }

    const float gain = isBypassed ? 1.0f : juce::Decibels::decibelsToGain(paramValues[iOutput]->load(std::memory_order_relaxed));
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
    publishDisplay(runMode, runLowLatency);
}

void ShiftyProcessor::processOriginal(juce::AudioBuffer<float>& buffer, int channels, bool bypassed) noexcept
{
    // The 0.4.2 signal path, kept for projects and presets made with it: Motion
    // is a 64-sample sample-and-hold added to SHIFT before the engine's 20 ms
    // smoother. Only one fix applies here: with the transport stopped, synced
    // Motion keeps running at the host tempo instead of freezing.
    const auto n = buffer.getNumSamples();
    const auto val = [this](size_t i) noexcept { return paramValues[i]->load(std::memory_order_relaxed); };
    EngineParams p;
    p.shiftSemitones = val(iShift);
    p.focus = val(iFocus);
    p.bite = val(iBite);
    p.drive = val(iDrive);
    p.mix = val(iMix);
    p.subSafe = val(iSubSafe) >= .5f;
    p.subCutHz = val(iSubCut);
    p.vowel = val(iVowel) >= .5f;
    if (bypassed) p.mix = 0.0f;

    const bool motionEnabled = val(iMotionEnable) >= .5f;
    const int shape = juce::jlimit(0, 3, (int) std::round(val(iMotionShape)));
    const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    double phase = originalMotionPhase;
    double cyclesPerSample = 0.0;
    bool useHostPhase = false;
    if (motionEnabled) {
        if (val(iMotionSync) >= .5f) {
            static constexpr double quartersPerCycle[] = {.25, .5, 1.0, 2.0, 4.0};
            const auto qpc = quartersPerCycle[(size_t) juce::jlimit(0, 4, (int)std::round(val(iMotionRate)))];
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
            cyclesPerSample = bpm / (60.0 * sampleRate * qpc);
        } else {
            cyclesPerSample = val(iMotionFreeHz) / sampleRate;
        }
    }

    const float baseShift = p.shiftSemitones;
    for (int offset = 0; offset < n;) {
        if (originalSamplesUntilUpdate == 0) {
            const double tickPhase = useHostPhase ? phase + offset * cyclesPerSample : originalMotionPhase;
            originalHeldWave = waveAt(tickPhase, shape);
            originalMotionPhase = tickPhase - std::floor(tickPhase);
            originalPhaseIncrement = 64.0 * cyclesPerSample;
            originalSamplesUntilUpdate = 64;
        }
        p.shiftSemitones = juce::jlimit(-12.0f, 12.0f,
            baseShift + (motionEnabled ? val(iMotionDepth) * originalHeldWave : 0.0f));
        originalEngine.setParameters(p);
        const int count = juce::jmin(originalSamplesUntilUpdate, n - offset);
        float* pointers[2] { buffer.getWritePointer(0, offset),
            channels > 1 ? buffer.getWritePointer(1, offset) : nullptr };
        juce::AudioBuffer<float> section(pointers, channels, count);
        originalEngine.processBlock(section);
        offset += count;
        originalSamplesUntilUpdate -= count;
        if (originalSamplesUntilUpdate == 0) {
            originalMotionPhase += originalPhaseIncrement;
            originalMotionPhase -= std::floor(originalMotionPhase);
        }
    }
    lastMotionOffset.store(motionEnabled ? val(iMotionDepth) * originalHeldWave : 0.0f, std::memory_order_relaxed);
    displayLfoPhase.store(static_cast<float>(originalMotionPhase), std::memory_order_relaxed);
    displayLfoValue.store(originalHeldWave, std::memory_order_relaxed);
    displayMotionRunning.store(motionEnabled, std::memory_order_relaxed);
    displayShift.store(juce::jlimit(-12.0f, 12.0f, baseShift + (motionEnabled ? val(iMotionDepth) * originalHeldWave : 0.0f)),
                       std::memory_order_relaxed);
}

void ShiftyProcessor::processLatest(juce::AudioBuffer<float>& buffer, int channels, bool bypassed,
                                    EngineMode mode, bool lowLatency) noexcept
{
    const auto n = buffer.getNumSamples();
    const auto val = [this](size_t i) noexcept { return paramValues[i]->load(std::memory_order_relaxed); };

    // CHARACTER and TALK macros scale other controls inside the DSP; the
    // parameters themselves are never rewritten, so automation stays intact.
    const float character = val(iCharacter);
    const float talk = val(iTalk);
    EngineParams p;
    p.shiftSemitones = val(iShift);
    p.focus = val(iFocus);
    p.bite = val(iBite) + character * 0.6f * (1.0f - val(iBite));
    p.drive = val(iDrive) + character * 0.35f * (1.0f - val(iDrive));
    p.mix = val(iMix);
    p.subSafe = val(iSubSafe) >= .5f;
    p.subCutHz = val(iSubCut);
    p.vowel = val(iVowel) >= .5f;
    p.vowelMorph = val(iVowelMorph);
    p.voiceSet = juce::jlimit(0, 2, (int) std::round(val(iVoiceSet)));
    p.character = character;
    if (mode == EngineMode::classic)
        p.focus = val(iFocus) + character * 0.3f * (1.0f - val(iFocus));
    if (bypassed) {
        if (mode == EngineMode::classic) p.mix = 0.0f; // delayed dry, latency kept
        else p.bypass = true;
    }
    FormantEngine& classic = lowLatency ? classicLowLatencyEngine : classicEngine;
    if (mode == EngineMode::talk) talkEngine.setParameters(p);
    else if (mode == EngineMode::mouth) mouthEngine.setParameters(p);
    else classic.setParameters(p);

    // Motion: per-sample LFO, handed to the engine unsmoothed; only its depth
    // and on/off ramp over 20 ms. Synced Motion locks to the host position while
    // playing and free-runs at the host tempo (120 BPM without one) otherwise.
    const bool motionEnabled = val(iMotionEnable) >= .5f;
    const bool sync = val(iMotionSync) >= .5f;
    const int shape = juce::jlimit(0, 3, (int) std::round(val(iMotionShape)));
    const int rate = juce::jlimit(0, 4, (int) std::round(val(iMotionRate)));
    const int target = mode == EngineMode::talk ? juce::jlimit(0, 2, (int) std::round(val(iMotionTarget))) : 0;
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
                                        : static_cast<double>(val(iMotionFreeHz)) / sampleRate;
    motionDepth.setTargetValue(motionEnabled ? val(iMotionDepth) * (1.0f + talk) : 0.0f);

    // ENV and YOI. TALK moves the vowel; MOUTH and CLASSIC move SHIFT.
    float envAmount = val(iEnvAmount);
    envAmount += talk * 0.6f * (envAmount < 0.0f ? -1.0f : 1.0f) * (1.0f - std::abs(envAmount));
    const float yoiAmount = val(iYoiAmount) + talk * 0.8f * (1.0f - val(iYoiAmount));
    const float envAttack = val(iEnvAttack), envRelease = val(iEnvRelease), yoiTime = val(iYoiTime);
    const float yoiFrom = val(iYoiFrom), yoiTo = val(iYoiTo), yoiShift = val(iYoiShift);
    const float morphBase = val(iVowelMorph);
    const bool vowelEngine = mode == EngineMode::talk;

    std::array<float, 64> shiftOffsets {}, morphOffsets {};
    float lfoValue = 0.0f, envLevel = 0.0f, yoiPosition = 1.0f, lfoOffset = 0.0f;
    for (int offset = 0; offset < n;) {
        const int count = juce::jmin(static_cast<int>(shiftOffsets.size()), n - offset);
        for (int i = 0; i < count; ++i) {
            double phase = motionPhase;
            if (hostLocked)
                phase = syncedMotionPhase(*ppq + (offset + i) * quartersPerSample, barStart,
                                          barQuarters, cycleQuarters);
            motionPhase = phase + cyclesPerSample;
            motionPhase -= std::floor(motionPhase);
            lfoValue = waveAt(phase, shape);
            lfoOffset = motionDepth.getNextValue() * lfoValue;

            float magnitude = 0.0f;
            for (int c = 0; c < channels; ++c)
                magnitude = juce::jmax(magnitude, std::abs(buffer.getSample(c, offset + i)));
            envLevel = envelopeFollower.process(magnitude, envAttack, envRelease);
            yoiPosition = yoiSweep.process(magnitude, yoiTime);

            float shiftOffset = target != 1 ? lfoOffset : 0.0f;
            shiftOffset += yoiAmount * yoiShift * (1.0f - yoiPosition);
            float morphOffset = 0.0f;
            if (vowelEngine) {
                if (target != 0) morphOffset += lfoOffset / 3.0f;
                morphOffset += envLevel * envAmount * 4.0f;
                morphOffset += yoiAmount * ((yoiFrom + (yoiTo - yoiFrom) * yoiPosition) - morphBase);
            } else {
                shiftOffset += envLevel * envAmount * 12.0f;
            }
            shiftOffsets[(size_t) i] = shiftOffset;
            morphOffsets[(size_t) i] = morphOffset;
        }
        float* pointers[2] { buffer.getWritePointer(0, offset),
            channels > 1 ? buffer.getWritePointer(1, offset) : nullptr };
        juce::AudioBuffer<float> section(pointers, channels, count);
        Modulation modulation;
        modulation.shift = shiftOffsets.data();
        modulation.morph = morphOffsets.data();
        if (mode == EngineMode::talk) talkEngine.processBlock(section, modulation);
        else if (mode == EngineMode::mouth) mouthEngine.processBlock(section, modulation);
        else classic.processBlock(section, shiftOffsets.data());
        offset += count;
        if (offset >= n) {
            lastMotionOffset.store(lfoOffset, std::memory_order_relaxed);
            displayShift.store(juce::jlimit(-12.0f, 12.0f, p.shiftSemitones + shiftOffsets[(size_t) count - 1]), std::memory_order_relaxed);
            displayMorph.store(juce::jlimit(0.0f, 4.0f, morphBase + morphOffsets[(size_t) count - 1]), std::memory_order_relaxed);
        }
    }
    displayLfoPhase.store(static_cast<float>(motionPhase), std::memory_order_relaxed);
    displayLfoValue.store(lfoValue, std::memory_order_relaxed);
    displayMotionRunning.store(motionEnabled, std::memory_order_relaxed);
    displayEnvelopeLevel.store(envLevel, std::memory_order_relaxed);
    displayYoi.store(yoiPosition, std::memory_order_relaxed);
}

void ShiftyProcessor::publishDisplay(EngineMode mode, bool lowLatency) noexcept
{
    displayEngine.store(static_cast<int>(mode), std::memory_order_relaxed);
    if (mode == EngineMode::original || mode == EngineMode::classic) {
        const auto& engine = mode == EngineMode::original ? originalEngine
                           : (lowLatency ? classicLowLatencyEngine : classicEngine);
        displayEnvelopeValid.store(engine.hasDisplay(), std::memory_order_relaxed);
        for (size_t i = 0; i < (size_t) displayPoints; ++i) {
            displayDetected[i].store(engine.displayDetected()[i], std::memory_order_relaxed);
            displayMoved[i].store(engine.displayMoved()[i], std::memory_order_relaxed);
        }
    } else if (mode == EngineMode::talk) {
        const auto& d = talkEngine.display();
        for (size_t k = 0; k < 5; ++k) {
            displayFormantFrequency[k].store(d.frequency[k], std::memory_order_relaxed);
            displayFormantBandwidth[k].store(d.bandwidth[k], std::memory_order_relaxed);
            displayFormantGain[k].store(d.gain[k], std::memory_order_relaxed);
        }
        displayBody.store(d.body, std::memory_order_relaxed);
    } else {
        displayPitch.store(mouthEngine.trackedPitch(), std::memory_order_relaxed);
        displayConfidence.store(mouthEngine.trackingConfidence(), std::memory_order_relaxed);
    }
}

ShiftyProcessor::Display ShiftyProcessor::display() const noexcept
{
    Display d;
    d.engine = static_cast<EngineMode>(displayEngine.load(std::memory_order_relaxed));
    d.envelopeValid = displayEnvelopeValid.load(std::memory_order_relaxed);
    for (size_t i = 0; i < (size_t) displayPoints; ++i) {
        d.detected[i] = displayDetected[i].load(std::memory_order_relaxed);
        d.moved[i] = displayMoved[i].load(std::memory_order_relaxed);
    }
    for (size_t k = 0; k < 5; ++k) {
        d.talk.frequency[k] = displayFormantFrequency[k].load(std::memory_order_relaxed);
        d.talk.bandwidth[k] = displayFormantBandwidth[k].load(std::memory_order_relaxed);
        d.talk.gain[k] = displayFormantGain[k].load(std::memory_order_relaxed);
    }
    d.talk.body = displayBody.load(std::memory_order_relaxed);
    d.pitch = displayPitch.load(std::memory_order_relaxed);
    d.confidence = displayConfidence.load(std::memory_order_relaxed);
    d.shift = displayShift.load(std::memory_order_relaxed);
    d.morph = displayMorph.load(std::memory_order_relaxed);
    d.talk.morph = d.morph;
    d.lfoPhase = displayLfoPhase.load(std::memory_order_relaxed);
    d.lfoValue = displayLfoValue.load(std::memory_order_relaxed);
    d.envelope = displayEnvelopeLevel.load(std::memory_order_relaxed);
    d.yoi = displayYoi.load(std::memory_order_relaxed);
    d.motionRunning = displayMotionRunning.load(std::memory_order_relaxed);
    return d;
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

// ---------------------------------------------------------------------------
// Engines

void ShiftyProcessor::applyEngine(EngineMode mode, bool lowLatency)
{
    engineModeValue.store(static_cast<int>(mode));
    lowLatencyValue.store(lowLatency);
    setLatencySamples(latencyFor(mode, lowLatency, getSampleRate() > 0.0 ? getSampleRate() : 48000.0));
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void ShiftyProcessor::setEngine(EngineMode mode, bool lowLatency)
{
    if (mode == engineMode() && lowLatency == classicLowLatency()) return;
    beginUndoableEdit();
    applyEngine(mode, lowLatency);
    endUndoableEdit("Change SHIFTY engine");
}

void ShiftyProcessor::useLatestSound()
{
    if (!isOriginalSound()) return;
    beginUndoableEdit();
    applyEngine(EngineMode::classic, false);
    endUndoableEdit("Use latest SHIFTY sound");
}

void ShiftyProcessor::setEditorScale(double scale)
{
    const double clean = juce::jlimit(0.5, 2.0, std::isfinite(scale) ? scale : 1.0);
    if (std::abs(clean - editorScaleValue.load()) < 1.0e-4) return;
    editorScaleValue.store(clean);
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

// ---------------------------------------------------------------------------
// Preset names and undo

juce::String ShiftyProcessor::currentPresetName() const
{
    const juce::ScopedLock lock(presetNameMutex);
    return presetName;
}

juce::String ShiftyProcessor::currentPresetFolder() const
{
    const juce::ScopedLock lock(presetNameMutex);
    return presetFolder;
}

void ShiftyProcessor::setPresetName(const juce::String& name)
{
    assignPresetName(name.trim().isNotEmpty() ? name.trim() : "Untitled", {});
}

void ShiftyProcessor::assignPresetName(const juce::String& name, const juce::String& folder)
{
    state.state.setProperty("presetName", name, nullptr);
    state.state.setProperty("presetFolder", folder, nullptr);
    const juce::ScopedLock lock(presetNameMutex);
    presetName = name;
    presetFolder = folder;
}

void ShiftyProcessor::syncPresetNameFromState()
{
    const juce::ScopedLock lock(presetNameMutex);
    presetName = state.state.getProperty("presetName", "Untitled").toString();
    presetFolder = state.state.getProperty("presetFolder", "").toString();
}

ShiftyProcessor::ParameterSnapshot ShiftyProcessor::captureSnapshot() const
{
    ParameterSnapshot snapshot;
    for (size_t i = 0; i < std::size(ids); ++i)
        if (const auto* parameter = state.getParameter(ids[i]))
            snapshot.values[i] = parameter->getValue();
    snapshot.presetName = currentPresetName();
    snapshot.presetFolder = currentPresetFolder();
    snapshot.engine = static_cast<int>(engineMode());
    snapshot.lowLatency = classicLowLatency();
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
    if (target.presetName != other.presetName || target.presetFolder != other.presetFolder)
        assignPresetName(target.presetName, target.presetFolder);
    if (target.engine != other.engine || target.lowLatency != other.lowLatency)
        applyEngine(static_cast<EngineMode>(target.engine), target.lowLatency);
    --undoSuppression;
}

void ShiftyProcessor::recordUndoStep(const ParameterSnapshot& before, const ParameterSnapshot& after,
                                     const juce::String& name)
{
    if (before.values == after.values && before.presetName == after.presetName
        && before.presetFolder == after.presetFolder && before.engine == after.engine
        && before.lowLatency == after.lowLatency) return;
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

// ---------------------------------------------------------------------------
// Randomisation

juce::StringArray ShiftyProcessor::randomizableParameterIds()
{
    juce::StringArray result;
    for (const auto* id : ids) result.add(id);
    return result;
}

bool ShiftyProcessor::isParameterLocked(const juce::String& id) const
{
    if (id == engineLockId) return (randomLocks.load() & engineLockBit) != 0;
    for (size_t i = 0; i < std::size(ids); ++i)
        if (id == ids[i]) return (randomLocks.load() & (1u << i)) != 0;
    return false;
}

void ShiftyProcessor::setParameterLocked(const juce::String& id, bool locked)
{
    uint32_t bit = id == engineLockId ? engineLockBit : 0u;
    for (size_t i = 0; i < std::size(ids) && bit == 0; ++i)
        if (id == ids[i]) bit = 1u << i;
    if (bit == 0) return;
    if (locked) randomLocks.fetch_or(bit);
    else randomLocks.fetch_and(~bit);
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void ShiftyProcessor::randomizeParameters()
{
    const auto locks = randomLocks.load();
    if ((locks & allRandomLocks) == allRandomLocks) return;
    beginUndoableEdit();
    auto range = [this](float lo, float hi) {
        return lo + variationRandom.nextFloat() * (hi - lo);
    };
    auto logRange = [&range](float lo, float hi) { return std::exp(range(std::log(lo), std::log(hi))); };
    // Character and movement ranges are chosen for quick mid-bass variations.
    // Output and bypass are included only when unlocked.
    // The engine is picked like a control (TALK, MOUTH or CLASSIC, never the
    // original 0.4.2 sound). Latency follows it and the host is told at once,
    // as with a preset; Undo restores both.
    if ((locks & engineLockBit) == 0) {
        constexpr EngineMode engines[] { EngineMode::talk, EngineMode::mouth, EngineMode::classic };
        applyEngine(engines[variationRandom.nextInt(3)], classicLowLatency());
    }
    const float randomShift = range(2.0f, 12.0f) * (variationRandom.nextBool() ? 1.0f : -1.0f);
    const float values[] = {
        randomShift, range(.35f, 1.0f), range(.35f, 1.0f),
        range(0.0f, .65f), range(.70f, 1.0f), variationRandom.nextBool() ? 1.0f : 0.0f,
        range(60.0f, 200.0f), range(-12.0f, 0.0f), variationRandom.nextBool() ? 1.0f : 0.0f,
        (float)variationRandom.nextInt(4), (float)variationRandom.nextInt(5),
        range(.5f, 8.0f), variationRandom.nextBool() ? 1.0f : 0.0f,
        logRange(.1f, 10.0f),
        variationRandom.nextBool() ? 1.0f : 0.0f,
        // Vowel Color: on three times in four, since without it CLASSIC has
        // nothing to move on a plain synth.
        variationRandom.nextInt(4) != 0 ? 1.0f : 0.0f,
        // 0.5.0
        range(0.0f, 4.0f), (float)variationRandom.nextInt(3), range(0.0f, .8f), range(0.0f, .8f),
        range(-.6f, .8f), logRange(1.0f, 30.0f), logRange(40.0f, 400.0f),
        variationRandom.nextBool() ? range(.4f, 1.0f) : 0.0f, range(0.0f, 4.0f), range(0.0f, 4.0f),
        logRange(60.0f, 500.0f), range(-6.0f, 6.0f), (float)variationRandom.nextInt(3)
    };
    static_assert(std::size(values) == std::size(ids));
    for (size_t i = 0; i < std::size(ids); ++i) {
        if ((locks & (1u << i)) != 0) continue;
        setParameterAsGesture(ids[i], values[i]);
    }
    setPresetName("Random Variation");
    endUndoableEdit("Randomize SHIFTY");
}

// ---------------------------------------------------------------------------
// State

void ShiftyProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto tree = state.copyState();
    tree.setProperty("schemaVersion", 1, nullptr);
    tree.setProperty("engineVersion", engineVersion, nullptr);
    tree.setProperty("presetName", currentPresetName(), nullptr);
    tree.setProperty("presetFolder", currentPresetFolder(), nullptr);
    tree.setProperty("randomLockMask", (int)randomLocks.load(), nullptr);
    tree.setProperty("randomLockVersion", 2, nullptr);
    tree.setProperty("engineMode", static_cast<int>(engineMode()), nullptr);
    tree.setProperty("classicLowLatency", classicLowLatency(), nullptr);
    tree.setProperty("editorScale", editorScale(), nullptr);
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
            // APVTS otherwise keeps the current value of a missing parameter.
            // Old sessions consistently open with every newer control at its
            // default (Vowel Color off), whatever this instance held before.
            for (const auto* id : ids) {
                if (tree.getChildWithProperty("id", id).isValid()) continue;
                const auto* parameter = state.getParameter(id);
                juce::ValueTree child("PARAM");
                child.setProperty("id", id, nullptr);
                child.setProperty("value", juce::String(id) == "vowel" ? 0.0f
                    : parameter->convertFrom0to1(parameter->getDefaultValue()), nullptr);
                tree.addChild(child, -1, nullptr);
            }
            auto locks = (uint32_t)(int)tree.getProperty("randomLockMask", (int)defaultRandomLocks);
            if (!tree.hasProperty("randomLockVersion")) {
                // Saved before 0.5.3, when the engine had no lock. A mask still
                // at the old default (Vowel Color locked) was never touched and
                // takes today's default; "everything locked" stays everything.
                if (locks == legacyDefaultRandomLocks) locks = defaultRandomLocks;
                else if ((locks & (engineLockBit - 1u)) == engineLockBit - 1u) locks |= engineLockBit;
            }
            // A session that made its own lock choices before Vowel Color
            // existed keeps it out of RANDOM.
            if (missingVowel && tree.hasProperty("randomLockMask")) locks |= (1u << 15);
            randomLocks.store(locks & allRandomLocks);
            // Projects saved before 0.5.0 carry no engine: they made the 0.4.2
            // sound, so they keep it until the user upgrades.
            const auto mode = tree.hasProperty("engineMode")
                ? static_cast<EngineMode>(juce::jlimit(0, 3, (int) tree.getProperty("engineMode")))
                : EngineMode::original;
            const bool lowLatency = (bool) tree.getProperty("classicLowLatency", false);
            // Sessions from before 0.5.0 carry no size: they open at 100%.
            editorScaleValue.store(juce::jlimit(0.5, 2.0, (double) tree.getProperty("editorScale", 1.0)));
            state.replaceState(tree);
            syncPresetNameFromState();
            applyEngine(mode, lowLatency);
            // A restored host session starts a fresh edit history.
            undoManager.clearUndoHistory();
        }
    }
}

// ---------------------------------------------------------------------------
// User presets

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

void ShiftyProcessor::saveUserPreset(const juce::String& name, const juce::String& folder)
{
    const auto legal = juce::File::createLegalFileName(name.trim());
    if (legal.isEmpty()) return;
    migrateLegacyPresetsOnce();
    // One folder level inside the preset folder: no path separators, and no
    // leading dots (".." would climb out; ".name" would be hidden).
    auto legalFolder = juce::File::createLegalFileName(folder.trim());
    while (legalFolder.startsWithChar('.')) legalFolder = legalFolder.substring(1).trimStart();
    auto dir = legalFolder.isEmpty() ? presetDirectory() : presetDirectory().getChildFile(legalFolder);
    if (dir.createDirectory().failed()) return;
    assignPresetName(name.trim(), legalFolder.isEmpty() ? juce::String("User") : "User/" + legalFolder);
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
    // instance keeps its own randomization locks. Presets saved before 0.5.0
    // recall the 0.4.2 sound; newer controls they lack load at their defaults.
    beginUndoableEdit();
    for (const auto* id : ids) {
        const auto child = tree.getChildWithProperty("id", id);
        if (child.isValid() && child.hasProperty("value"))
            setParameterAsGesture(id, static_cast<float>(child.getProperty("value")));
        else if (juce::String(id) == "vowel")
            setParameterAsGesture(id, 0.0f);
        else if (auto* parameter = state.getParameter(id))
            setParameterAsGesture(id, parameter->convertFrom0to1(parameter->getDefaultValue()));
    }
    const auto mode = tree.hasProperty("engineMode")
        ? static_cast<EngineMode>(juce::jlimit(0, 3, (int) tree.getProperty("engineMode")))
        : EngineMode::original;
    applyEngine(mode, (bool) tree.getProperty("classicLowLatency", false));
    const auto parent = file.getParentDirectory();
    const auto folder = parent == presetDirectory() || ! parent.isAChildOf(presetDirectory())
        ? juce::String("User") : "User/" + parent.getFileName();
    assignPresetName(tree.getProperty("presetName", file.getFileNameWithoutExtension()).toString(), folder);
    endUndoableEdit("Load SHIFTY user preset");
    return true;
}

juce::Array<juce::File> ShiftyProcessor::userPresetFiles() const
{
    migrateLegacyPresetsOnce();
    auto files = presetDirectory().findChildFiles(juce::File::findFiles, true, "*.shifty");
    files.sort();
    return files;
}

juce::StringArray ShiftyProcessor::userPresetFolders() const
{
    juce::StringArray folders;
    for (const auto& directory : presetDirectory().findChildFiles(juce::File::findDirectories, false))
        if (!directory.getFileName().startsWithChar('.'))
            folders.add(directory.getFileName());
    folders.sortNatural();
    return folders;
}

// ---------------------------------------------------------------------------
// Factory presets

juce::StringArray ShiftyProcessor::factoryPresetNames()
{
    juce::StringArray names;
    for (const auto& preset : factoryBank()) names.add(preset.name);
    return names;
}

int ShiftyProcessor::factoryPresetIndex(const juce::String& folder, const juce::String& name)
{
    const auto& bank = factoryBank();
    for (size_t i = 0; i < bank.size(); ++i)
        if (bank[i].folder == folder && bank[i].name == name) return (int) i;
    return -1;
}

void ShiftyProcessor::applyFactoryPreset(const FactoryPreset& p)
{
    const auto set = [this](const char* id, float value) { setParameterAsGesture(id, value); };
    set("shift", p.shift);
    set("focus", p.focus);
    set("bite", p.bite);
    set("drive", p.drive);
    set("mix", p.mix);
    set("motionEnable", p.motion ? 1.0f : 0.0f);
    set("motionShape", (float)p.shape);
    set("motionRate", (float)p.rate);
    set("motionDepth", p.depth);
    set("subSafe", p.subSafe ? 1.0f : 0.0f);
    set("subCut", p.subCut);
    set("output", p.output);
    set("motionSync", p.sync ? 1.0f : 0.0f);
    set("motionFreeHz", p.freeHz);
    set("bypass", 0.0f);
    set("vowel", p.vowelColor ? 1.0f : 0.0f);
    set("vowelMorph", p.morph);
    set("voiceSet", (float)p.voiceSet);
    set("character", p.character);
    set("talk", p.talk);
    set("envAmount", p.envAmount);
    set("envAttack", p.envAttack);
    set("envRelease", p.envRelease);
    set("yoiAmount", p.yoiAmount);
    set("yoiFrom", p.yoiFrom);
    set("yoiTo", p.yoiTo);
    set("yoiTime", p.yoiTime);
    set("yoiShift", p.yoiShift);
    set("motionTarget", (float)p.target);
    applyEngine(p.engine, p.lowLatency);
    assignPresetName(p.name, p.folder);
}

void ShiftyProcessor::loadFactoryPreset(int index)
{
    const auto& bank = factoryBank();
    if (!juce::isPositiveAndBelow(index, (int)bank.size())) return;
    beginUndoableEdit();
    applyFactoryPreset(bank[(size_t)index]);
    endUndoableEdit("Load SHIFTY preset");
}

} // namespace shifty

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new shifty::ShiftyProcessor();
}
