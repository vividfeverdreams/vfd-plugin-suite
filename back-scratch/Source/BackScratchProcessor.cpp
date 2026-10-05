#include "BackScratchProcessor.h"
#include "BackScratchEditor.h"
#include "AutomationBridge.h"
#include "PatternEditing.h"
#include "EditedPatternSerialization.h"
#include <cmath>
#include <cstdlib>

namespace backScratch {
namespace {
constexpr const char* controlIds[] = {"factory", "variation", "keepSub", "bypass", "generated", "generationSeed", "phraseDuration", "phraseLock", "sourceBeats", "sourceBarBeats", "phraseTiming", "patternShift", "edited", "motionVersion"};
constexpr std::array<int, 5> phraseBarOptions {1, 2, 4, 8, 16};
constexpr int maximumSeed = 1048575;
int choiceBars(float choice) noexcept { return phraseBarOptions[static_cast<size_t>(std::clamp(juce::roundToInt(choice), 0, 4))]; }
int barsChoice(int bars) noexcept {
    for (size_t i = 0; i < phraseBarOptions.size(); ++i) if (bars <= phraseBarOptions[i]) return static_cast<int>(i);
    return 4;
}
constexpr std::array<const char*, 8> durationLabels {"1/2 Beat", "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars"};
int durationChoice(float value) noexcept { return std::clamp(juce::roundToInt(value), 0, 7); }
int timingChoice(float value) noexcept { return std::clamp(juce::roundToInt(value), 0, 2); }
double durationBeats(float value, double barBeats, float timing = 0.0f) noexcept {
    const int choice = durationChoice(value);
    const double base = choice < 3 ? std::array<double, 3>{0.5, 1.0, 2.0}[static_cast<size_t>(choice)]
                                 : phraseBarOptions[static_cast<size_t>(choice - 3)] * barBeats;
    return base * std::array<double, 3>{1.0, 1.5, 2.0 / 3.0}[static_cast<size_t>(timingChoice(timing))];
}
constexpr int oldStateMagic = 0x42534331;
float displayFraction(const ScratchPattern& pattern, double beat) noexcept {
    return std::clamp(static_cast<float>(patternDisplayPhase(pattern, beat)), 0.0f, std::nextafter(1.0f, 0.0f));
}
bool readInteger(const juce::String& text, int low, int high, int& result) {
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || !trimmed.containsOnly("0123456789")) return false;
    const auto value = trimmed.getLargeIntValue();
    if (value < low || value > high) return false;
    result = static_cast<int>(value);
    return true;
}
}

juce::AudioProcessorValueTreeState::ParameterLayout BackScratchProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto addFloat = [&layout](const char* id, const char* name, float low, float high, float step, float initial) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id, 1}, name, juce::NormalisableRange<float>(low, high, step), initial));
    };
    // Preserve the old parameter identities/order for host project compatibility.
    // The retired wheel controls do not drive the live-pattern engine.
    addFloat("position", "Legacy Scratch Position", 0, 1, 0.000001f, 0);
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"touch", 1}, "Legacy Scratch Touch", false));
    addFloat("smooth", "Legacy Smooth", 0, 20, 0.1f, 3);
    addFloat("mix", "Legacy Mix", 0, 1, 0.001f, 1);
    addFloat("output", "Legacy Output", -24, 6, 0.1f, 0);
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass", 1}, "Bypass", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"sens", 1}, "Legacy Sensitivity",
        juce::StringArray{"1/16 Bar", "1/8 Bar", "1/4 Bar", "1/2 Bar", "1 Bar"}, 2));
    addFloat("time", "Legacy Scratch Time", -64, 64, 0.000001f, 0);
    juce::StringArray names;
    for (const auto& preset : factoryPresets()) names.add(preset.name);
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"factory", 1}, "Scratch Pattern", names, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"variation", 1}, "Variation", 0, 65535, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"keepSub", 1}, "Keep Sub", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"generated", 1}, "Generated Pattern", false));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"generationSeed", 1}, "Pattern Seed", 0, maximumSeed, 1));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"phraseLength", 1}, "Legacy Phrase Length",
        juce::StringArray{"1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars"}, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"phraseLock", 1}, "Phrase Length Lock", true));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"phraseDuration", 1}, "Phrase Length",
        juce::StringArray{"1/2 Beat", "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars"}, 3));
    addFloat("sourceBeats", "Pattern Original Length", 0.25f, 256.0f, 0.0001f, 4.0f);
    addFloat("sourceBarBeats", "Pattern Original Meter", 0.0f, 16.0f, 0.0001f, 4.0f);
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"phraseTiming", 1}, "Phrase Timing",
        juce::StringArray{"Straight", "Dotted", "Triplet"}, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"patternShift", 1}, "Pattern Grid Shift", 0, patternGridDivisions - 1, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"edited", 1}, "Edited Pattern", false));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"motionVersion", 1}, "Pattern Motion Version", 1, 2, 2));
    return layout;
}

BackScratchProcessor::BackScratchProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "BACK_SCRATCH_STATE", makeLayout()), pattern(makePattern(0))
{
    editedPatterns.publish(fitPatternToPhrase(pattern, 1.0));
    editUndo.reserve(64); editRedo.reserve(64);
    for (size_t i = 0; i < controlCount; ++i) {
        controlParameters[i] = state.getParameter(controlIds[i]);
        rawControls[i] = state.getRawParameterValue(controlIds[i]);
        controls[i] = lastRaw[i] = rawControls[i]->load();
    }
}

double BackScratchProcessor::getTailLengthSeconds() const
{
    const auto active = currentPattern();
    double maximumDelay = 0.0;
    for (int i = 0; i < active.count; ++i) {
        const auto& segment = active.segments[static_cast<size_t>(i)];
        maximumDelay = std::max({maximumDelay, segment.delayStart, segment.delayEnd});
    }
    // Retaining long history does not mean every short fill has a two-minute tail.
    return std::min(static_cast<double>(LiveScratchEngine::maximumHistorySeconds),
                    maximumDelay * 60.0 / std::clamp(currentTempoBpm(), 20.0, 400.0))
        + static_cast<double>(getLatencySamples()) / rate + 0.003;
}

bool BackScratchProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto input = layout.getMainInputChannelSet();
    return input == layout.getMainOutputChannelSet()
        && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

void BackScratchProcessor::prepareToPlay(double sampleRate, int)
{
    clearControlHistory();
    rate = std::isfinite(sampleRate) && sampleRate > 1.0 ? sampleRate : 48000.0;
    engine.prepare(rate, getTotalNumInputChannels(), LiveScratchEngine::maximumHistorySeconds);
    for (size_t i = 0; i < controlCount; ++i) controls[i] = lastRaw[i] = rawControls[i]->load();
    queueActive.fill(false);
    pattern = currentPattern();
    controlHistory.resize(static_cast<size_t>(engine.latencySamples() + 1));
    controlWrite = 0; controlFrames = 0;
    renderedControls = controls;
    renderedBarBeats = currentBarBeats();
    wasPlaying = hadSongPosition = false;
    freeBeat = expectedBeat = 0.0;
    resetPending.store(false);
    displayReady.store(false);
    playingDisplay.store(false);
    displayedPhase.store(0.0f);
    setLatencySamples(engine.latencySamples());
}

void BackScratchProcessor::releaseResources()
{
    engine.reset();
    playingDisplay.store(false);
    displayReady.store(false);
    wasPlaying = hadSongPosition = false;
    clearControlHistory();
}

int BackScratchProcessor::selectedFactoryPreset() const noexcept
{
    return std::clamp(juce::roundToInt(rawControls[factory]->load()), 0, factoryPresetCount - 1);
}

std::uint32_t BackScratchProcessor::variationSeed() const noexcept
{
    return static_cast<std::uint32_t>(std::clamp(juce::roundToInt(rawControls[variation]->load()), 0, 65535));
}

bool BackScratchProcessor::keepSub() const noexcept { return rawControls[sub]->load() > 0.5f; }
bool BackScratchProcessor::isGenerated() const noexcept { return rawControls[generated]->load() > 0.5f; }
std::uint32_t BackScratchProcessor::generationSeed() const noexcept { return static_cast<std::uint32_t>(std::clamp(juce::roundToInt(rawControls[seed]->load()), 0, maximumSeed)); }
int BackScratchProcessor::phraseDurationChoice() const noexcept { return durationChoice(rawControls[phraseLength]->load()); }
void BackScratchProcessor::setPhraseDurationChoice(int choice) { writeParameterGesture("phraseDuration", static_cast<float>(std::clamp(choice, 0, phraseDurationCount - 1))); }
juce::String BackScratchProcessor::durationLabel(int choice, int timing) {
    return juce::String(durationLabels[static_cast<size_t>(durationChoice(static_cast<float>(choice)))])
        + (timing == 1 ? " Dotted" : timing == 2 ? " Triplet" : "");
}
juce::String BackScratchProcessor::phraseDurationLabel() const { return durationLabel(phraseDurationChoice(), phraseTimingChoice()); }
int BackScratchProcessor::phraseTimingChoice() const noexcept { return timingChoice(rawControls[phraseTiming]->load()); }
void BackScratchProcessor::setPhraseTimingChoice(int choice) { writeParameterGesture("phraseTiming", static_cast<float>(std::clamp(choice, 0, 2))); }
int BackScratchProcessor::patternShiftSteps() const noexcept { return wrapPatternGridOffset(juce::roundToInt(rawControls[patternShift]->load())); }
void BackScratchProcessor::shiftPattern(int delta) { writeParameterGesture("patternShift", static_cast<float>(wrapPatternGridOffset(patternShiftSteps() + delta % patternGridDivisions))); }
double BackScratchProcessor::phraseBeats() const noexcept { return durationBeats(rawControls[phraseLength]->load(), currentBarBeats(), rawControls[phraseTiming]->load()); }
int BackScratchProcessor::phraseBars() const noexcept { return phraseDurationChoice() < 3 ? 0 : choiceBars(static_cast<float>(phraseDurationChoice() - 3)); }
bool BackScratchProcessor::phraseLocked() const noexcept { return rawControls[phraseLock]->load() > 0.5f; }
void BackScratchProcessor::setPhraseBars(int bars) { setPhraseDurationChoice(barsChoice(bars) + 3); }
void BackScratchProcessor::setPhraseLocked(bool locked) { writeParameterGesture("phraseLock", locked ? 1.0f : 0.0f); }
ScratchPattern BackScratchProcessor::patternFor(const std::array<float, controlCount>& values, double barBeats, const ScratchPattern* custom) noexcept
{
    const double beats = durationBeats(values[phraseLength], barBeats, values[phraseTiming]);
    const int motion = std::clamp(juce::roundToInt(values[motionVersion]), 1, 2);
    ScratchPattern result;
    if (values[edited] > 0.5f && custom != nullptr) result = fitPatternToPhrase(*custom, beats);
    else if (values[generated] > 0.5f) {
        // Composition identity is separate from playback duration. Turning the
        // length knob scales the existing graph, never asks for more gestures.
        // Older projects never stored their meter. Zero retains their original
        // bar count until the host supplies the real meter, even when restoration
        // precedes the first audio callback. No parameters are written from DSP.
        const bool legacyMeter = values[sourceBarBeats] < 0.25f;
        const double originalBar = legacyMeter ? barBeats : values[sourceBarBeats];
        const double originalBeats = values[sourceBeats] * (legacyMeter ? barBeats / 4.0 : 1.0);
        const auto original = generatePattern(static_cast<std::uint32_t>(std::clamp(juce::roundToInt(values[seed]), 0, maximumSeed)),
            std::clamp(originalBeats, 0.25, 256.0), std::clamp(originalBar, 0.25, 16.0), motion);
        result = fitPatternToPhrase(original, beats);
    }
    else result = fitPatternToPhrase(makePattern(juce::roundToInt(values[factory]), static_cast<std::uint32_t>(juce::roundToInt(values[variation])), motion), beats);
    result.gridOffset = wrapPatternGridOffset(juce::roundToInt(values[patternShift]));
    return result;
}
ScratchPattern BackScratchProcessor::currentPattern() const noexcept
{
    std::array<float, controlCount> values {};
    for (size_t i = 0; i < controlCount; ++i) values[i] = rawControls[i]->load();
    const int slot = editedPatterns.acquireLatest();
    const auto result = patternFor(values, currentBarBeats(), slot >= 0 ? &editedPatterns.get(slot) : nullptr);
    editedPatterns.release(slot);
    return result;
}
BackScratchProcessor::DisplayState BackScratchProcessor::displayState() const noexcept
{
    DisplayState result;
    std::array<float, controlCount> values {};
    double beat = 0.0;
    int slot = EditedPatternStore::None;
    if (displayReady.load() && playingDisplay.load()) {
        for (;;) {
            const unsigned before = displaySequence.load();
            if ((before & 1u) != 0) continue;
            slot = editedPatterns.acquirePublished(audibleEditSlot);
            for (size_t i = 0; i < controlCount; ++i) values[i] = audibleControls[i].load();
            beat = audibleBeat.load();
            result.barBeats = audibleBarBeats.load();
            result.tempoBpm = audibleTempo.load();
            result.playing = audiblePlaying.load();
            result.historyReady = audibleHistoryReady.load();
            result.scratchActive = audibleScratchActive.load();
            if (before == displaySequence.load() && slot >= 0) break;
            editedPatterns.release(slot);
        }
    } else {
        for (size_t i = 0; i < controlCount; ++i) values[i] = rawControls[i]->load();
        result.barBeats = currentBarBeats();
        result.tempoBpm = currentTempoBpm();
        slot = editedPatterns.acquireLatest();
    }
    result.pattern = patternFor(values, result.barBeats, slot >= 0 ? &editedPatterns.get(slot) : nullptr);
    editedPatterns.release(slot);
    result.activeSample = evaluatePattern(result.pattern, beat);
    result.phase = displayFraction(result.pattern, beat);
    result.durationChoice = durationChoice(values[phraseLength]);
    result.timingChoice = timingChoice(values[phraseTiming]);
    result.factoryIndex = std::clamp(juce::roundToInt(values[factory]), 0, factoryPresetCount - 1);
    result.generated = values[generated] > 0.5f;
    result.edited = values[edited] > 0.5f;
    result.seed = static_cast<std::uint32_t>(std::clamp(juce::roundToInt(values[result.generated ? seed : variation]), 0, maximumSeed));
    return result;
}
const juce::String BackScratchProcessor::getProgramName(int i) { return factoryPresets()[static_cast<size_t>(std::clamp(i, 0, factoryPresetCount - 1))].name; }

juce::String BackScratchProcessor::currentPresetName() const
{
    const int index = selectedFactoryPreset();
    const bool custom = isGenerated();
    const auto identity = custom ? generationSeed() : variationSeed();
    const juce::ScopedLock lock(nameLock);
    if (savedName.isNotEmpty() && hasCustomPattern() == namedEdited && custom == namedGenerated && (custom || index == namedFactory)
        && identity == namedSeed && phraseDurationChoice() == namedDuration
        && phraseTimingChoice() == namedTiming && patternShiftSteps() == namedShift
        && (hasCustomPattern() || juce::roundToInt(rawControls[motionVersion]->load()) == namedMotionVersion)
        && (!custom || (rawControls[sourceBeats]->load() == namedSourceBeats && rawControls[sourceBarBeats]->load() == namedSourceBarBeats))) return savedName;
    if (hasCustomPattern()) return "Edited Scratch";
    if (custom) return "New Scratch / " + juce::String(identity);
    return juce::String(factoryPresets()[static_cast<size_t>(index)].name)
        + (identity == 0 ? juce::String{} : " / " + juce::String(identity));
}

void BackScratchProcessor::clearSavedName()
{
    const juce::ScopedLock lock(nameLock);
    savedName.clear();
    namedFactory = -1;
}

void BackScratchProcessor::resetAfterRestore()
{
    for (size_t i = 0; i < controlCount; ++i) restoredControls[i].store(rawControls[i]->load());
    resetPending.store(true);
}

void BackScratchProcessor::setHostParameter(const char* id, float actual)
{
    if (auto* parameter = state.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(actual));
}

void BackScratchProcessor::writeParameterGesture(const char* id, float actual)
{
    if (auto* parameter = state.getParameter(id)) {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(actual));
        parameter->endChangeGesture();
    }
}

void BackScratchProcessor::setKeepSub(bool keep) { writeParameterGesture("keepSub", keep ? 1.0f : 0.0f); }
void BackScratchProcessor::selectFactoryPreset(int index)
{
    const juce::ScopedLock editGuard(editLock);
    clearEditHistory();
    writeParameterGesture("edited", 0.0f);
    writeParameterGesture("motionVersion", 2.0f);
    clearSavedName();
    writeParameterGesture("factory", static_cast<float>(std::clamp(index, 0, factoryPresetCount - 1)));
    writeParameterGesture("variation", 0.0f);
    writeParameterGesture("generated", 0.0f);
    // Arrow nudges belong to the current pattern, not the next factory preset.
    // Saved user presets and projects restore their own offsets separately.
    writeParameterGesture("patternShift", 0.0f);
    if (!phraseLocked()) {
        const double nativeBeats = makePattern(index).beats;
        int closest = 0;
        for (int choice = 1; choice < phraseDurationCount; ++choice)
            if (std::abs(durationBeats(static_cast<float>(choice), currentBarBeats()) - nativeBeats)
                < std::abs(durationBeats(static_cast<float>(closest), currentBarBeats()) - nativeBeats)) closest = choice;
        setPhraseDurationChoice(closest);
    }
    writeParameterGesture("bypass", 0.0f);
}

void BackScratchProcessor::randomizePattern()
{
    const juce::ScopedLock editGuard(editLock);
    clearEditHistory();
    writeParameterGesture("edited", 0.0f);
    writeParameterGesture("motionVersion", 2.0f);
    clearSavedName();
    const auto old = generationSeed();
    auto next = static_cast<std::uint32_t>(1 + random.nextInt(maximumSeed));
    if (next == old) next = next % maximumSeed + 1;
    if (!phraseLocked()) setPhraseDurationChoice(random.nextInt(phraseDurationCount));
    // Compose at the base knob duration; timing modifiers retime that same
    // identity and never force the bounded composer beyond its source range.
    writeParameterGesture("sourceBeats", static_cast<float>(durationBeats(rawControls[phraseLength]->load(), currentBarBeats())));
    writeParameterGesture("sourceBarBeats", static_cast<float>(currentBarBeats()));
    writeParameterGesture("generationSeed", static_cast<float>(next));
    writeParameterGesture("generated", 1.0f);
    writeParameterGesture("bypass", 0.0f);
}

void BackScratchProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int count = audio.getNumSamples(), channels = audio.getNumChannels();
    if (count <= 0 || channels <= 0) { backScratchBridge::current.clear(); return; }
    double bpm = 120.0, songBeat = freeBeat, barBeats = 4.0;
    bool playing = true, hasSongPosition = false;
    bool looping = false;
    double loopStart = 0.0, loopEnd = 0.0;
    if (auto* head = getPlayHead()) {
        if (const auto info = head->getPosition()) {
            playing = info->getIsPlaying();
            if (const auto signature = info->getTimeSignature(); signature && signature->numerator > 0 && signature->denominator > 0)
                barBeats = std::clamp(signature->numerator * 4.0 / signature->denominator, 0.25, 16.0);
            if (const auto value = info->getBpm(); value && std::isfinite(*value)) bpm = std::clamp(*value, 20.0, 400.0);
            if (const auto value = info->getPpqPosition(); value && std::isfinite(*value)) { songBeat = *value; hasSongPosition = true; }
            if (const auto points = info->getLoopPoints(); info->getIsLooping() && points
                && std::isfinite(points->ppqStart) && std::isfinite(points->ppqEnd) && points->ppqEnd > points->ppqStart) {
                loopStart = points->ppqStart; loopEnd = points->ppqEnd;
                looping = hasSongPosition && songBeat < loopEnd;
            }
        }
    }
    const auto transportBeat = [looping, loopStart, loopEnd](double beat) {
        if (!looping || beat < loopEnd) return beat;
        const double length = loopEnd - loopStart;
        return loopStart + std::fmod(beat - loopStart, length);
    };
    const double samplesPerBeat = 60.0 * rate / bpm;
    const double step = 1.0 / samplesPerBeat;
    const double positionTolerance = std::max(2.0 * step, 1.0e-7);
    double positionError = std::abs(songBeat - expectedBeat);
    if (looping && songBeat >= loopStart - positionTolerance
        && expectedBeat >= loopStart - positionTolerance && expectedBeat <= loopEnd + positionTolerance)
        positionError = std::min(positionError, std::abs(positionError - (loopEnd - loopStart)));
    const bool seek = hasSongPosition && hadSongPosition && positionError > positionTolerance;
    const bool restored = resetPending.exchange(false);
    if (restored || seek || playing != wasPlaying) {
        engine.reset();
        clearControlHistory();
    }
    if (restored) {
        for (size_t i = 0; i < controlCount; ++i) controls[i] = lastRaw[i] = restoredControls[i].load();
        queueActive.fill(false);
    }

    int eventCount = 0;
    std::array<bool, controlCount> hasQueue {};
    for (int i = 0; i < backScratchBridge::current.size; ++i) {
        const auto& event = backScratchBridge::current.events[static_cast<size_t>(i)];
        for (int control = 0; control < controlCount; ++control) {
            if (event.parameter != controlParameters[static_cast<size_t>(control)]) continue;
            if (eventCount < static_cast<int>(timedControls.size())) {
                timedControls[static_cast<size_t>(eventCount++)] = {std::clamp(event.offset, 0, count - 1), control,
                    controlParameters[static_cast<size_t>(control)]->convertFrom0to1(event.normalizedValue), i};
                hasQueue[static_cast<size_t>(control)] = true;
            }
            break;
        }
    }
    std::sort(timedControls.begin(), timedControls.begin() + eventCount,
              [](const TimedControl& a, const TimedControl& b) { return a.offset == b.offset ? a.order < b.order : a.offset < b.offset; });
    for (size_t i = 0; i < controlCount; ++i) {
        const float raw = rawControls[i]->load();
        if (hasQueue[i]) queueActive[i] = true;
        else if (!queueActive[i] || raw != lastRaw[i]) controls[i] = raw;
        lastRaw[i] = raw;
    }
    int nextEvent = 0;
    float peakL = 0.0f, peakR = 0.0f;
    auto* left = audio.getWritePointer(0);
    auto* right = audio.getWritePointer(channels > 1 ? 1 : 0);
    ControlFrame renderedContext;
    int blockEditSlot = editedPatterns.acquireLatest();
    if (blockEditSlot < 0) {
        blockEditSlot = renderedEditSlot;
        editedPatterns.retain(blockEditSlot);
    }
    for (int sample = 0; sample < count; ++sample) {
        while (nextEvent < eventCount && timedControls[static_cast<size_t>(nextEvent)].offset <= sample) {
            const auto& event = timedControls[static_cast<size_t>(nextEvent++)];
            controls[static_cast<size_t>(event.control)] = event.value;
        }
        const double beat = transportBeat(songBeat + (playing ? sample * step : 0.0));
        ControlFrame context {controls, beat, samplesPerBeat, barBeats, playing, blockEditSlot};
        // Delay the musical clock and automation by the same amount as audio.
        // Live's latency compensation then aligns the complete effect, including
        // pattern/sub changes, with its original song position.
        if (!controlHistory.empty()) {
            editedPatterns.retain(context.editSlot);
            editedPatterns.release(controlHistory[controlWrite].editSlot);
            controlHistory[controlWrite] = context;
            controlWrite = (controlWrite + 1) % controlHistory.size();
            if (controlFrames >= engine.latencySamples()) context = controlHistory[controlWrite];
            else {
                context = controlHistory[0];
                if (context.playing) context.beat += (controlFrames - engine.latencySamples()) / context.samplesPerBeat;
                ++controlFrames;
            }
        }
        bool changed = context.barBeats != renderedBarBeats || context.editSlot != renderedEditSlot;
        for (const int c : {factory, variation, generated, seed, phraseLength, sourceBeats, sourceBarBeats, phraseTiming, patternShift, edited, motionVersion})
            changed = changed || context.values[static_cast<size_t>(c)] != renderedControls[static_cast<size_t>(c)];
        if (changed) pattern = patternFor(context.values, context.barBeats,
            context.editSlot >= 0 ? &editedPatterns.get(context.editSlot) : nullptr);
        if (context.editSlot != renderedEditSlot) {
            editedPatterns.retain(context.editSlot);
            editedPatterns.release(renderedEditSlot);
            renderedEditSlot = context.editSlot;
        }
        renderedControls = context.values;
        renderedBarBeats = context.barBeats;
        renderedContext = context;
        const auto out = engine.process(left[sample], right[sample], evaluatePattern(pattern, context.beat), context.samplesPerBeat,
                                        context.values[sub] > 0.5f, context.values[bypass] > 0.5f || !context.playing);
        left[sample] = out[0];
        if (channels > 1) right[sample] = out[1];
        peakL = std::max(peakL, std::abs(out[0]));
        peakR = std::max(peakR, std::abs(out[1]));
    }
    const double endBeat = transportBeat(songBeat + (playing ? count * step : 0.0));
    // Publish the exact context used for the final output sample, including
    // delayed automation. Never show a new curve against an old audio phase.
    displaySequence.fetch_add(1);
    editedPatterns.retain(renderedContext.editSlot);
    editedPatterns.release(audibleEditSlot.exchange(renderedContext.editSlot));
    for (size_t i = 0; i < controlCount; ++i) audibleControls[i].store(renderedContext.values[i]);
    audibleBeat.store(renderedContext.beat);
    audibleBarBeats.store(renderedContext.barBeats);
    audibleTempo.store(60.0 * rate / renderedContext.samplesPerBeat);
    audiblePlaying.store(renderedContext.playing);
    audibleHistoryReady.store(engine.isHistoryReady());
    audibleScratchActive.store(engine.isScratchActive());
    displaySequence.fetch_add(1);
    displayReady.store(renderedContext.editSlot >= 0);
    editedPatterns.release(blockEditSlot);
    displayedPhase.store(displayFraction(pattern, renderedContext.beat));
    barBeatsDisplay.store(barBeats);
    tempoDisplay.store(bpm);
    playingDisplay.store(playing);
    peaks[0].store(peakL); peaks[1].store(peakR);
    backScratchBridge::current.clear();
    freeBeat = expectedBeat = endBeat;
    hadSongPosition = hasSongPosition;
    wasPlaying = playing;
}

void BackScratchProcessor::getStateInformation(juce::MemoryBlock& result)
{
    const juce::ScopedLock editGuard(editLock);
    auto snapshot = state.copyState();
    snapshot.setProperty("engineVersion", 9, nullptr);
    // Use the copied parameter value, so concurrent host automation cannot
    // produce conflicting recipe metadata and parameter data in one save.
    snapshot.setProperty("generatorVersion", juce::roundToInt(static_cast<float>(
        snapshot.getChildWithProperty("id", "motionVersion").getProperty("value", 2.0f))), nullptr);
    {
        const juce::ScopedLock lock(nameLock);
        snapshot.setProperty("savedName", savedName, nullptr);
        snapshot.setProperty("namedFactory", namedFactory, nullptr);
        snapshot.setProperty("namedSeed", static_cast<int>(namedSeed), nullptr);
        snapshot.setProperty("namedGenerated", namedGenerated, nullptr);
        snapshot.setProperty("namedEdited", namedEdited, nullptr);
        snapshot.setProperty("namedMotionVersion", namedMotionVersion, nullptr);
        snapshot.setProperty("namedDuration", namedDuration, nullptr);
        snapshot.setProperty("namedTiming", namedTiming, nullptr);
        snapshot.setProperty("namedShift", namedShift, nullptr);
        snapshot.setProperty("namedSourceBeats", namedSourceBeats, nullptr);
        snapshot.setProperty("namedSourceBarBeats", namedSourceBarBeats, nullptr);
    }
    if (const auto xml = snapshot.createXml()) {
        if (auto* old = xml->getChildByName("EDITED_PATTERN")) xml->removeChildElement(old, true);
        const int slot = editedPatterns.acquireLatest();
        if (slot >= 0) xml->addChildElement(editedPatternToXml(editedPatterns.get(slot)).release());
        editedPatterns.release(slot);
        copyXmlToBinary(*xml, result);
    }
}

void BackScratchProcessor::setStateInformation(const void* data, int size)
{
    const juce::ScopedLock editGuard(editLock);
    if (data == nullptr || size < 8) return;
    juce::MemoryInputStream input(data, static_cast<size_t>(size), false);
    if (input.readInt() == oldStateMagic) {
        clearSavedName();
        setHostParameter("factory", 0); setHostParameter("variation", 0);
        setHostParameter("generated", 0); setHostParameter("generationSeed", 1);
        setHostParameter("phraseDuration", 3); setHostParameter("phraseLock", 1);
        setHostParameter("sourceBeats", 4); setHostParameter("sourceBarBeats", 4);
        setHostParameter("phraseTiming", 0); setHostParameter("patternShift", 0);
        setHostParameter("keepSub", 1); setHostParameter("bypass", 1);
        setHostParameter("edited", 0); setHostParameter("motionVersion", 1);
        clearEditHistory();
        resetAfterRestore();
        return;
    }
    if (size > 1024 * 1024) return;
    const auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || !xml->hasTagName("BACK_SCRATCH_STATE")) return;
    auto loaded = juce::ValueTree::fromXml(*xml);
    const int version = static_cast<int>(loaded.getProperty("engineVersion", 0));
    if (version != 4 && version != 5 && version != 6 && version != 7 && version != 8 && version != 9) return;
    int restoredMotion = 1;
    if (version >= 9) {
        if (!readInteger(loaded.getProperty("generatorVersion").toString(), 1, 2, restoredMotion)) return;
        const auto text = loaded.getChildWithProperty("id", "motionVersion").getProperty("value").toString();
        const char* first = text.toRawUTF8();
        char* end = nullptr;
        const double value = std::strtod(first, &end);
        if (end == first || *end != '\0' || value != restoredMotion) return;
    } else {
        if (version >= 5 && static_cast<int>(loaded.getProperty("generatorVersion", 1)) != 1) return;
        auto child = loaded.getChildWithProperty("id", "motionVersion");
        if (!child.isValid()) { child = juce::ValueTree("PARAM"); child.setProperty("id", "motionVersion", nullptr); loaded.addChild(child, -1, nullptr); }
        child.setProperty("value", 1.0f, nullptr);
        loaded.setProperty("namedMotionVersion", 1, nullptr);
    }
    if (version == 4) {
        const auto set = [&loaded](const char* id, float value) {
            auto child = loaded.getChildWithProperty("id", id);
            if (!child.isValid()) { child = juce::ValueTree("PARAM"); child.setProperty("id", id, nullptr); loaded.addChild(child, -1, nullptr); }
            child.setProperty("value", value, nullptr);
        };
        const int oldFactory = std::clamp(static_cast<int>(loaded.getChildWithProperty("id", "factory").getProperty("value", 0)), 0, factoryPresetCount - 1);
        const int bars = std::max(1, static_cast<int>(std::ceil(makePattern(oldFactory).beats / 4.0)));
        set("generated", 0); set("generationSeed", 1); set("phraseLength", static_cast<float>(barsChoice(bars))); set("phraseLock", 1);
        loaded.setProperty("namedGenerated", false, nullptr); loaded.setProperty("namedBars", bars, nullptr);
    }
    if (version < 6) {
        const auto set = [&loaded](const char* id, float value) {
            auto child = loaded.getChildWithProperty("id", id);
            if (!child.isValid()) { child = juce::ValueTree("PARAM"); child.setProperty("id", id, nullptr); loaded.addChild(child, -1, nullptr); }
            child.setProperty("value", value, nullptr);
        };
        const float previousLength = loaded.getChildWithProperty("id", "phraseLength").getProperty("value", 0.0f);
        const int lengthChoice = std::clamp(juce::roundToInt(previousLength), 0, 4) + 3;
        const float originalBar = 0.0f;
        const float originalBeats = static_cast<float>(durationBeats(static_cast<float>(lengthChoice), 4.0));
        set("phraseDuration", static_cast<float>(lengthChoice));
        set("sourceBeats", originalBeats); set("sourceBarBeats", originalBar);
        loaded.setProperty("namedDuration", lengthChoice, nullptr);
        loaded.setProperty("namedSourceBeats", originalBeats, nullptr);
        loaded.setProperty("namedSourceBarBeats", originalBar, nullptr);
    }
    if (version < 7) {
        for (const auto* id : {"phraseTiming", "patternShift"}) {
            auto child = loaded.getChildWithProperty("id", id);
            if (!child.isValid()) { child = juce::ValueTree("PARAM"); child.setProperty("id", id, nullptr); loaded.addChild(child, -1, nullptr); }
            child.setProperty("value", 0.0f, nullptr);
        }
        loaded.setProperty("namedTiming", 0, nullptr);
        loaded.setProperty("namedShift", 0, nullptr);
    } else {
        // Validate new fields before replacing any state. APVTS serializes
        // numeric values as floating-point text, so accept integral numbers.
        for (const auto* id : {"phraseTiming", "patternShift"}) {
            const auto child = loaded.getChildWithProperty("id", id);
            if (!child.isValid() || !child.hasProperty("value")) return;
            const auto text = child.getProperty("value").toString();
            if (text.isEmpty()) return;
            const char* start = text.toRawUTF8();
            char* end = nullptr;
            const double value = std::strtod(start, &end);
            if (end == start || *end != '\0') return;
            const int maximum = juce::String(id) == "phraseTiming" ? 2 : patternGridDivisions - 1;
            if (!std::isfinite(value) || value < 0.0 || value > maximum || value != std::floor(value)) return;
        }
    }
    ScratchPattern restoredPattern = fitPatternToPhrase(makePattern(0, 0, restoredMotion), 1.0);
    if (version >= 8) {
        int enabled = 0;
        const auto text = loaded.getChildWithProperty("id", "edited").getProperty("value").toString();
        const char* first = text.toRawUTF8();
        char* end = nullptr;
        const double value = std::strtod(first, &end);
        if (end == first || *end != '\0' || (value != 0.0 && value != 1.0)) return;
        enabled = static_cast<int>(value);
        juce::ignoreUnused(enabled);
        const auto* custom = xml->getChildByName("EDITED_PATTERN");
        if (custom == nullptr || !editedPatternFromXml(*custom, restoredPattern)) return;
    } else {
        auto child = loaded.getChildWithProperty("id", "edited");
        if (!child.isValid()) { child = juce::ValueTree("PARAM"); child.setProperty("id", "edited", nullptr); loaded.addChild(child, -1, nullptr); }
        child.setProperty("value", 0.0f, nullptr);
        loaded.setProperty("namedEdited", false, nullptr);
    }
    if (editedPatterns.publish(restoredPattern) < 0) return;
    for (int i = loaded.getNumChildren(); --i >= 0;)
        if (loaded.getChild(i).hasType("EDITED_PATTERN")) loaded.removeChild(i, nullptr);
    clearEditHistory();
    state.replaceState(loaded);
    {
        const juce::ScopedLock lock(nameLock);
        savedName = loaded.getProperty("savedName").toString().substring(0, 80);
        namedFactory = static_cast<int>(loaded.getProperty("namedFactory", -1));
        namedSeed = static_cast<std::uint32_t>(static_cast<int>(loaded.getProperty("namedSeed", 0)));
        namedGenerated = static_cast<bool>(loaded.getProperty("namedGenerated", false));
        namedEdited = static_cast<bool>(loaded.getProperty("namedEdited", false));
        namedMotionVersion = static_cast<int>(loaded.getProperty("namedMotionVersion", restoredMotion));
        namedDuration = static_cast<int>(loaded.getProperty("namedDuration", 3));
        namedTiming = static_cast<int>(loaded.getProperty("namedTiming", 0));
        namedShift = static_cast<int>(loaded.getProperty("namedShift", 0));
        namedSourceBeats = static_cast<float>(loaded.getProperty("namedSourceBeats", 4.0f));
        namedSourceBarBeats = static_cast<float>(loaded.getProperty("namedSourceBarBeats", 4.0f));
    }
    resetAfterRestore();
}

juce::Result BackScratchProcessor::savePreset(const juce::File& file)
{
    const juce::ScopedLock editGuard(editLock);
    const int savedFactory = selectedFactoryPreset();
    const auto savedVariation = variationSeed(), savedGeneration = generationSeed();
    const bool savedSub = keepSub(), generatedPattern = isGenerated(), locked = phraseLocked();
    const int duration = phraseDurationChoice();
    const int timing = phraseTimingChoice(), shift = patternShiftSteps();
    const float originalBeats = rawControls[sourceBeats]->load(), originalBar = rawControls[sourceBarBeats]->load();
    juce::XmlElement xml("BACK_SCRATCH_PRESET");
    xml.setAttribute("version", 9); xml.setAttribute("generatorVersion", juce::roundToInt(rawControls[motionVersion]->load()));
    xml.setAttribute("edited", hasCustomPattern());
    const int slot = editedPatterns.acquireLatest();
    if (slot < 0) return juce::Result::fail("The pattern is updating. Try saving again.");
    xml.addChildElement(editedPatternToXml(editedPatterns.get(slot)).release());
    editedPatterns.release(slot);
    xml.setAttribute("name", file.getFileNameWithoutExtension().substring(0, 80));
    xml.setAttribute("factory", savedFactory); xml.setAttribute("variation", static_cast<int>(savedVariation));
    xml.setAttribute("generated", generatedPattern); xml.setAttribute("generationSeed", static_cast<int>(savedGeneration));
    xml.setAttribute("phraseDuration", duration); xml.setAttribute("phraseLocked", locked);
    xml.setAttribute("phraseTiming", timing); xml.setAttribute("patternShift", shift);
    xml.setAttribute("sourceBeats", static_cast<double>(originalBeats)); xml.setAttribute("sourceBarBeats", static_cast<double>(originalBar));
    xml.setAttribute("keepSub", savedSub);
    if (!file.replaceWithText(xml.toString())) return juce::Result::fail("Couldn't save this preset. Choose a writable folder.");
    {
        const juce::ScopedLock lock(nameLock);
        savedName = xml.getStringAttribute("name"); namedFactory = savedFactory;
        namedGenerated = generatedPattern; namedDuration = duration;
        namedEdited = hasCustomPattern();
        namedMotionVersion = juce::roundToInt(rawControls[motionVersion]->load());
        namedTiming = timing; namedShift = shift;
        namedSourceBeats = originalBeats; namedSourceBarBeats = originalBar;
        namedSeed = generatedPattern ? savedGeneration : savedVariation;
    }
    return juce::Result::ok();
}

juce::Result BackScratchProcessor::loadPreset(const juce::File& file)
{
    const juce::ScopedLock editGuard(editLock);
    if (!file.existsAsFile() || file.getSize() > 1024 * 1024) return juce::Result::fail("Choose a BACK-SCRATCH preset file.");
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("BACK_SCRATCH_PRESET"))
        return juce::Result::fail("This isn't a BACK-SCRATCH preset.");
    const int version = xml->getIntAttribute("version");
    if (version != 4 && version != 5 && version != 6 && version != 7 && version != 8 && version != 9) return juce::Result::fail("This preset version isn't supported.");
    int restoredMotion = 1;
    if (version >= 9 && !readInteger(xml->getStringAttribute("generatorVersion"), 1, 2, restoredMotion))
        return juce::Result::fail("This preset contains an invalid motion version.");
    const int index = xml->getIntAttribute("factory", -1), variationValue = xml->getIntAttribute("variation", -1);
    const int keep = xml->getIntAttribute("keepSub", -1);
    const int generatedPattern = version >= 5 ? xml->getIntAttribute("generated", -1) : 0;
    const int generation = version >= 5 ? xml->getIntAttribute("generationSeed", -1) : 1;
    const int bars = version >= 5 ? xml->getIntAttribute("phraseBars", -1)
        : std::max(1, static_cast<int>(std::ceil(makePattern(index).beats / 4.0)));
    const int locked = version >= 5 ? xml->getIntAttribute("phraseLocked", -1) : 1;
    const int duration = version >= 6 ? xml->getIntAttribute("phraseDuration", -1) : barsChoice(bars) + 3;
    const double originalBar = version >= 6 ? xml->getDoubleAttribute("sourceBarBeats", -1.0) : 0.0;
    const double originalBeats = version >= 6 ? xml->getDoubleAttribute("sourceBeats", -1.0) : bars * 4.0;
    const bool validBars = version >= 6 || std::find(phraseBarOptions.begin(), phraseBarOptions.end(), bars) != phraseBarOptions.end();
    int timing = 0, shift = 0;
    if (version >= 7 && (!readInteger(xml->getStringAttribute("phraseTiming"), 0, 2, timing)
        || !readInteger(xml->getStringAttribute("patternShift"), 0, patternGridDivisions - 1, shift)))
        return juce::Result::fail("This preset contains invalid timing or shift settings.");
    if (index < 0 || index >= factoryPresetCount || variationValue < 0 || variationValue > 65535
        || keep < 0 || keep > 1 || generatedPattern < 0 || generatedPattern > 1 || generation < 0 || generation > maximumSeed
        || !validBars || duration < 0 || duration >= phraseDurationCount || locked < 0 || locked > 1
        || !std::isfinite(originalBeats) || originalBeats < 0.25 || originalBeats > 256.0
        || !std::isfinite(originalBar) || originalBar < 0.0 || originalBar > 16.0
        || (originalBar > 0.0 && originalBar < 0.25)
        || (version >= 5 && version < 9 && xml->getIntAttribute("generatorVersion", 1) != 1))
        return juce::Result::fail("This preset contains invalid settings.");
    ScratchPattern restoredPattern = fitPatternToPhrase(makePattern(0, 0, restoredMotion), 1.0);
    int enabled = 0;
    if (version >= 8) {
        const auto* custom = xml->getChildByName("EDITED_PATTERN");
        if (!readInteger(xml->getStringAttribute("edited"), 0, 1, enabled)
            || custom == nullptr || !editedPatternFromXml(*custom, restoredPattern))
            return juce::Result::fail("This preset contains an invalid edited pattern.");
    }
    if (editedPatterns.publish(restoredPattern) < 0) return juce::Result::fail("The pattern is updating. Try loading again.");
    clearEditHistory();
    writeParameterGesture("edited", static_cast<float>(enabled));
    writeParameterGesture("motionVersion", static_cast<float>(restoredMotion));
    writeParameterGesture("factory", static_cast<float>(index)); writeParameterGesture("variation", static_cast<float>(variationValue));
    writeParameterGesture("generated", static_cast<float>(generatedPattern)); writeParameterGesture("generationSeed", static_cast<float>(generation));
    setPhraseDurationChoice(duration);
    setPhraseTimingChoice(timing);
    writeParameterGesture("patternShift", static_cast<float>(shift));
    writeParameterGesture("sourceBeats", static_cast<float>(originalBeats)); writeParameterGesture("sourceBarBeats", static_cast<float>(originalBar));
    writeParameterGesture("phraseLock", static_cast<float>(locked));
    writeParameterGesture("keepSub", static_cast<float>(keep)); writeParameterGesture("bypass", 0);
    {
        const juce::ScopedLock lock(nameLock);
        savedName = xml->getStringAttribute("name", file.getFileNameWithoutExtension()).substring(0, 80);
        namedFactory = index; namedGenerated = generatedPattern != 0; namedDuration = duration;
        namedEdited = enabled != 0;
        namedMotionVersion = restoredMotion;
        namedTiming = timing; namedShift = shift;
        namedSourceBeats = static_cast<float>(originalBeats); namedSourceBarBeats = static_cast<float>(originalBar);
        namedSeed = static_cast<std::uint32_t>(generatedPattern ? generation : variationValue);
    }
    return juce::Result::ok();
}

juce::AudioProcessorEditor* BackScratchProcessor::createEditor() { return new BackScratchEditor(*this); }
} // namespace backScratch
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new backScratch::BackScratchProcessor(); }
