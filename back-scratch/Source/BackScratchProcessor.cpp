#include "BackScratchProcessor.h"
#include "BackScratchEditor.h"
#include "AutomationBridge.h"
#include "PatternEditing.h"
#include "EditedPatternSerialization.h"
#include <cmath>
#include <cstdlib>
#include <thread>

namespace backScratch {
namespace {
// Host parameter IDs are append-only: the first fourteen are the 0.10 controls.
constexpr const char* controlIds[] = {"factory", "variation", "keepSub", "bypass", "generated", "generationSeed", "phraseDuration",
                                      "phraseLock", "sourceBeats", "sourceBarBeats", "phraseTiming", "patternShift", "edited", "motionVersion",
                                      "engage", "engageQuantize", "retrigger", "phraseEvery", "patternSlot", "slotChain",
                                      "wetMix", "outputTrim", "subSplit", "depth", "lookback"};
constexpr std::array<int, 5> phraseBarOptions {1, 2, 4, 8, 16};
constexpr int maximumSeed = 1048575;
constexpr int stateVersion = 10;
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
// 0.11 performance controls.
double quantizeBeats(float choice, double barBeats) noexcept {
    const int index = std::clamp(juce::roundToInt(choice), 0, 4);
    return index == 4 ? std::clamp(barBeats, 0.25, 16.0) : std::array<double, 4>{0.25, 0.5, 1.0, 2.0}[static_cast<size_t>(index)];
}
int everyCount(float choice) noexcept { return std::array<int, 4>{1, 2, 4, 8}[static_cast<size_t>(std::clamp(juce::roundToInt(choice), 0, 3))]; }
int chainLength(float choice) noexcept { return std::array<int, 4>{1, 2, 4, 8}[static_cast<size_t>(std::clamp(juce::roundToInt(choice), 0, 3))]; }
double lookbackBeats(float choice, double barBeats) noexcept {
    const int index = std::clamp(juce::roundToInt(choice), 0, 4);
    return index == 0 ? 0.0 : index == 1 ? 1.0 : std::array<double, 3>{1.0, 2.0, 4.0}[static_cast<size_t>(index - 2)] * barBeats;
}
long long floorDivision(double value) noexcept { return static_cast<long long>(std::floor(value)); }
int positiveModulo(long long value, int divisor) noexcept {
    const auto remainder = static_cast<int>(value % divisor);
    return remainder < 0 ? remainder + divisor : remainder;
}
constexpr int oldStateMagic = 0x42534331;
float displayFraction(const ScratchPattern& pattern, double beat) noexcept {
    return std::clamp(static_cast<float>(patternDisplayPhase(pattern, beat)), 0.0f, std::nextafter(1.0f, 0.0f));
}
// Controls whose host automation is delivered sample-accurately. The tag lets
// the stateless bridge router find this instance's queue without allocation.
template <typename Base>
class Queued final : public Base, public backScratchBridge::QueueTag {
public:
    using Base::Base;
};
bool readInteger(const juce::String& text, int low, int high, int& result) {
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || !trimmed.containsOnly("0123456789")) return false;
    const auto value = trimmed.getLargeIntValue();
    if (value < low || value > high) return false;
    result = static_cast<int>(value);
    return true;
}
bool readNumber(const juce::String& text, double low, double high, double& result) {
    const auto trimmed = text.trim();
    if (trimmed.isEmpty()) return false;
    const char* first = trimmed.toRawUTF8();
    char* end = nullptr;
    const double value = std::strtod(first, &end);
    if (end == first || *end != '\0' || !std::isfinite(value) || value < low || value > high) return false;
    result = value;
    return true;
}
ScratchPattern dryGeometry() noexcept
{
    ScratchPattern dry;
    dry.beats = 1.0;
    dry.count = 1;
    dry.segments[0] = {0.0, 1.0, 0.0, 0.0, 1.0f, 1.0f, 0.0f, false};
    return dry;
}
double maximumDelayBeats(const ScratchPattern& pattern) noexcept
{
    double maximum = 0.0;
    for (int i = 0; i < pattern.count; ++i) {
        const auto& segment = pattern.segments[static_cast<size_t>(i)];
        if (segment.wet <= 0.0f) continue;
        maximum = std::max({maximum, segment.delayStart, segment.delayEnd});
    }
    return maximum;
}
juce::File& userPresetRootOverride() { static juce::File root; return root; }
// The Size menu's choices; anything else means "not chosen".
float validEditorScale(float scale) noexcept
{
    for (const float choice : {0.9f, 1.0f, 1.25f, 1.5f})
        if (std::abs(scale - choice) < 1.0e-4f) return choice;
    return 0.0f;
}

// Neutral values of the 0.11 controls: exactly the 0.10 sound.
struct NeutralControl { const char* id; float value; };
constexpr std::array<NeutralControl, 11> neutralControls {{
    {"engage", 1.0f}, {"engageQuantize", 0.0f}, {"retrigger", 0.0f}, {"phraseEvery", 0.0f}, {"patternSlot", 0.0f},
    {"slotChain", 0.0f}, {"wetMix", 100.0f}, {"outputTrim", 0.0f}, {"subSplit", 2.0f}, {"depth", 100.0f}, {"lookback", 0.0f}}};
}

juce::AudioProcessorValueTreeState::ParameterLayout BackScratchProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto addFloat = [&layout](const char* id, const char* name, float low, float high, float step, float initial) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id, 1}, name, juce::NormalisableRange<float>(low, high, step), initial));
    };
    auto addQueuedFloat = [&layout](const char* id, const char* name, float low, float high, float step, float initial,
                                    const char* label = "") {
        layout.add(std::make_unique<Queued<juce::AudioParameterFloat>>(
            juce::ParameterID{id, 1}, name, juce::NormalisableRange<float>(low, high, step), initial,
            juce::AudioParameterFloatAttributes{}.withLabel(label)));
    };
    auto addQueuedChoice = [&layout](const char* id, const char* name, juce::StringArray choices, int initial) {
        layout.add(std::make_unique<Queued<juce::AudioParameterChoice>>(juce::ParameterID{id, 1}, name, choices, initial));
    };
    // Preserve the old parameter identities/order for host project compatibility.
    // The retired wheel controls do not drive the live-pattern engine.
    addFloat("position", "Legacy Scratch Position", 0, 1, 0.000001f, 0);
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"touch", 1}, "Legacy Scratch Touch", false));
    addFloat("smooth", "Legacy Smooth", 0, 20, 0.1f, 3);
    addFloat("mix", "Legacy Mix", 0, 1, 0.001f, 1);
    addFloat("output", "Legacy Output", -24, 6, 0.1f, 0);
    layout.add(std::make_unique<Queued<juce::AudioParameterBool>>(juce::ParameterID{"bypass", 1}, "Bypass", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"sens", 1}, "Legacy Sensitivity",
        juce::StringArray{"1/16 Bar", "1/8 Bar", "1/4 Bar", "1/2 Bar", "1 Bar"}, 2));
    addFloat("time", "Legacy Scratch Time", -64, 64, 0.000001f, 0);
    juce::StringArray names;
    for (const auto& preset : factoryPresets()) names.add(preset.name);
    layout.add(std::make_unique<Queued<juce::AudioParameterChoice>>(juce::ParameterID{"factory", 1}, "Scratch Pattern", names, 0));
    layout.add(std::make_unique<Queued<juce::AudioParameterInt>>(juce::ParameterID{"variation", 1}, "Variation", 0, 65535, 0));
    layout.add(std::make_unique<Queued<juce::AudioParameterBool>>(juce::ParameterID{"keepSub", 1}, "Keep Sub", true));
    layout.add(std::make_unique<Queued<juce::AudioParameterBool>>(juce::ParameterID{"generated", 1}, "Generated Pattern", false));
    layout.add(std::make_unique<Queued<juce::AudioParameterInt>>(juce::ParameterID{"generationSeed", 1}, "Pattern Seed", 0, maximumSeed, 1));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"phraseLength", 1}, "Legacy Phrase Length",
        juce::StringArray{"1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars"}, 0));
    layout.add(std::make_unique<Queued<juce::AudioParameterBool>>(juce::ParameterID{"phraseLock", 1}, "Phrase Length Lock", true));
    layout.add(std::make_unique<Queued<juce::AudioParameterChoice>>(juce::ParameterID{"phraseDuration", 1}, "Phrase Length",
        juce::StringArray{"1/2 Beat", "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars"}, 3));
    addQueuedFloat("sourceBeats", "Pattern Original Length", 0.25f, 256.0f, 0.0001f, 4.0f);
    addQueuedFloat("sourceBarBeats", "Pattern Original Meter", 0.0f, 16.0f, 0.0001f, 4.0f);
    layout.add(std::make_unique<Queued<juce::AudioParameterChoice>>(juce::ParameterID{"phraseTiming", 1}, "Phrase Timing",
        juce::StringArray{"Straight", "Dotted", "Triplet"}, 0));
    layout.add(std::make_unique<Queued<juce::AudioParameterInt>>(juce::ParameterID{"patternShift", 1}, "Pattern Grid Shift", 0, patternGridDivisions - 1, 0));
    layout.add(std::make_unique<Queued<juce::AudioParameterBool>>(juce::ParameterID{"edited", 1}, "Edited Pattern", false));
    layout.add(std::make_unique<Queued<juce::AudioParameterInt>>(juce::ParameterID{"motionVersion", 1}, "Pattern Motion Version", 1, 2, 2));
    // 0.11 controls, appended. Their defaults reproduce the 0.10 sound.
    layout.add(std::make_unique<Queued<juce::AudioParameterBool>>(juce::ParameterID{"engage", 1}, "Engage", true));
    addQueuedChoice("engageQuantize", "Engage Quantize", {"1/16", "1/8", "1/4", "1/2", "1 Bar"}, 0);
    layout.add(std::make_unique<Queued<juce::AudioParameterBool>>(juce::ParameterID{"retrigger", 1}, "Retrigger", false));
    addQueuedChoice("phraseEvery", "Play Every", {"Every Phrase", "Every 2nd Phrase", "Every 4th Phrase", "Every 8th Phrase"}, 0);
    addQueuedChoice("patternSlot", "Pattern Slot", {"A", "B", "C", "D", "E", "F", "G", "H"}, 0);
    addQueuedChoice("slotChain", "Slot Chain", {"Off", "A-B", "A-D", "A-H"}, 0);
    addQueuedFloat("wetMix", "Mix", 0.0f, 100.0f, 0.1f, 100.0f, "%");
    addQueuedFloat("outputTrim", "Output", -12.0f, 12.0f, 0.1f, 0.0f, "dB");
    addQueuedChoice("subSplit", "Keep Sub Split", {"120 Hz", "160 Hz", "200 Hz", "300 Hz"}, LiveScratchEngine::defaultSplit);
    addQueuedFloat("depth", "Depth", 0.0f, 200.0f, 0.1f, 100.0f, "%");
    addQueuedChoice("lookback", "Look Back", {"Off", "1 Beat", "1 Bar", "2 Bars", "4 Bars"}, 0);
    return layout;
}

BackScratchProcessor::BackScratchProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "BACK_SCRATCH_STATE", makeLayout()), pattern(makePattern(0))
{
    editedPatterns.publish(fitPatternToPhrase(pattern, 1.0));
    slotBanks.publish(SlotBank {});
    editUndo.reserve(64); editRedo.reserve(64);
    for (size_t i = 0; i < controlCount; ++i) {
        controlParameters[i] = state.getParameter(controlIds[i]);
        rawControls[i] = state.getRawParameterValue(controlIds[i]);
        controls[i] = lastRaw[i] = rawControls[i]->load();
        if (auto* tag = dynamic_cast<backScratchBridge::QueueTag*>(controlParameters[i])) tag->queue = &automationEvents;
    }
    startTimerHz(2);
}

BackScratchProcessor::ParameterGroup::ParameterGroup(BackScratchProcessor& processor)
    : owner(processor), lock(processor.editLock)
{
    // An odd sequence tells the audio thread that a group is incomplete.
    if (owner.parameterGroupDepth++ == 0) owner.parameterGroupSequence.fetch_add(1);
}

BackScratchProcessor::ParameterGroup::~ParameterGroup()
{
    if (--owner.parameterGroupDepth == 0) owner.parameterGroupSequence.fetch_add(1);
}

double BackScratchProcessor::neededHistorySeconds() const
{
    std::array<float, controlCount> values {};
    for (size_t i = 0; i < controlCount; ++i) values[i] = rawControls[i]->load();
    const double barBeats = currentBarBeats();
    const double beats = durationBeats(values[phraseLength], barBeats, values[phraseTiming]);
    double deepest = 0.0;
    {
        const int slot = editedPatterns.acquireLatest();
        deepest = maximumDelayBeats(patternFor(values, barBeats, slot >= 0 ? &editedPatterns.get(slot) : nullptr));
        editedPatterns.release(slot);
    }
    if (const int bank = slotBanks.acquireLatest(); bank >= 0) {
        const auto& slots = slotBanks.get(bank);
        for (int k = 0; k < SlotBank::count; ++k)
            if (slots.filled[static_cast<size_t>(k)]) deepest = std::max(deepest, maximumDelayBeats(slots.patterns[static_cast<size_t>(k)]) * beats);
        slotBanks.release(bank);
    }
    deepest = deepest * std::max(1.0, values[depth] / 100.0) + lookbackBeats(values[lookback], barBeats);
    // Allow for a slower tempo than now and for a little headroom.
    return deepest * 60.0 / std::clamp(currentTempoBpm(), 20.0, 400.0) * 1.25 + 1.0;
}

void BackScratchProcessor::setHistoryMode(int seconds)
{
    const int mode = seconds == 16 || seconds == 32 || seconds == 64 || seconds == 128 ? seconds : 0;
    if (historyModeSetting.exchange(mode) != mode)
        updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    maintainHistory();
}

void BackScratchProcessor::maintainHistory()
{
    const juce::ScopedLock lock(historyLock);
    engine.collectRetiredHistory();
    if (engine.sampleRate() <= 0.0 || getLatencySamples() <= 0 || engine.isHistoryRequestPending()) return;
    const int mode = historyModeSetting.load();
    const double current = engine.historySeconds();
    double target = mode;
    if (mode == 0) {
        // Automatic: grow to the next of 16/32/64/128 s when the settings need
        // it. Never shrink while running; prepareToPlay starts small again.
        const double need = neededHistorySeconds();
        target = 16.0;
        while (target < need && target < LiveScratchEngine::maximumHistorySeconds) target *= 2.0;
        if (target <= current) return;
    }
    if (std::abs(target - current) > 0.5) engine.requestHistory(target);
}

void BackScratchProcessor::timerCallback()
{
    maintainHistory();
}

double BackScratchProcessor::getTailLengthSeconds() const
{
    std::array<float, controlCount> values {};
    for (size_t i = 0; i < controlCount; ++i) values[i] = rawControls[i]->load();
    const auto active = currentPattern();
    const double maximumDelay = maximumDelayBeats(active) * std::max(1.0, values[depth] / 100.0)
                              + lookbackBeats(values[lookback], currentBarBeats());
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
    {
        const juce::ScopedLock lock(historyLock);
        // Fixed choices allocate exactly; Automatic starts at the size the
        // current pattern needs and grows later if a setting asks for more.
        const int mode = historyModeSetting.load();
        double seconds = mode;
        if (mode == 0) {
            const double need = neededHistorySeconds();
            seconds = 16.0;
            while (seconds < need && seconds < LiveScratchEngine::maximumHistorySeconds) seconds *= 2.0;
        }
        engine.prepare(rate, getTotalNumInputChannels(), seconds);
    }
    for (size_t i = 0; i < controlCount; ++i) controls[i] = lastRaw[i] = rawControls[i]->load();
    queueActive.fill(false);
    pattern = currentPattern();
    slotPatternIndex = -1;
    try {
        controlHistory.resize(static_cast<size_t>(engine.latencySamples() + 1));
    } catch (...) {
        // Without the delayed control frames, controls apply undelayed; never
        // let an allocation failure escape into the host.
        controlHistory.clear();
    }
    controlWrite = 0; controlFrames = 0;
    renderedControls = controls;
    renderedBarBeats = currentBarBeats();
    wasPlaying = hadSongPosition = false;
    freeBeat = expectedBeat = 0.0;
    previousStep = 0.0; previousCount = 0;
    engageRequested = engagedNow = controls[engage] > 0.5f;
    engagePending = false;
    engageBoundary = retriggerOrigin = lastContextBeat = 0.0;
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
int BackScratchProcessor::selectedSlot() const noexcept { return std::clamp(juce::roundToInt(rawControls[patternSlot]->load()), 0, slotCount - 1); }
void BackScratchProcessor::selectSlot(int slot) { writeParameterGesture("patternSlot", static_cast<float>(std::clamp(slot, 0, slotCount - 1))); }
int BackScratchProcessor::slotChainChoice() const noexcept { return std::clamp(juce::roundToInt(rawControls[slotChain]->load()), 0, 3); }
bool BackScratchProcessor::isEngaged() const noexcept { return rawControls[engage]->load() > 0.5f; }
void BackScratchProcessor::setEngaged(bool on) { writeParameterGesture("engage", on ? 1.0f : 0.0f); }
float BackScratchProcessor::parameterValue(const char* id) const noexcept
{
    if (auto* value = state.getRawParameterValue(id)) return value->load();
    return 0.0f;
}
bool BackScratchProcessor::slotFilled(int slot) const noexcept
{
    if (slot <= 0) return true;
    if (slot >= slotCount) return false;
    const int bank = slotBanks.acquireLatest();
    const bool filled = bank >= 0 && slotBanks.get(bank).filled[static_cast<size_t>(slot - 1)];
    slotBanks.release(bank);
    return filled;
}

void BackScratchProcessor::setEditorScale(float scale)
{
    const float valid = validEditorScale(scale);
    if (!juce::exactlyEqual(editorScaleSetting.exchange(valid), valid))
        updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
}

ScratchPattern BackScratchProcessor::patternFor(const Values& values, double barBeats, const ScratchPattern* custom) noexcept
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

ScratchPattern BackScratchProcessor::slotPatternFor(const Values& values, double barBeats, const SlotBank& bank, int slot) noexcept
{
    // Slots share the phrase length, timing and grid shift of the main pattern.
    const double beats = durationBeats(values[phraseLength], barBeats, values[phraseTiming]);
    const auto index = static_cast<size_t>(std::clamp(slot, 1, SlotBank::count) - 1);
    auto result = fitPatternToPhrase(bank.filled[index] ? bank.patterns[index] : dryGeometry(), beats);
    result.gridOffset = wrapPatternGridOffset(juce::roundToInt(values[patternShift]));
    return result;
}

ScratchPattern BackScratchProcessor::currentPattern() const noexcept
{
    Values values {};
    for (size_t i = 0; i < controlCount; ++i) values[i] = rawControls[i]->load();
    const int slot = selectedSlot();
    if (slot > 0) {
        const int bank = slotBanks.acquireLatest();
        auto result = bank >= 0 ? slotPatternFor(values, currentBarBeats(), slotBanks.get(bank), slot)
                                : slotPatternFor(values, currentBarBeats(), SlotBank {}, slot);
        slotBanks.release(bank);
        return result;
    }
    const int edit = editedPatterns.acquireLatest();
    const auto result = patternFor(values, currentBarBeats(), edit >= 0 ? &editedPatterns.get(edit) : nullptr);
    editedPatterns.release(edit);
    return result;
}

BackScratchProcessor::DisplayState BackScratchProcessor::displayState() const noexcept
{
    DisplayState result;
    Values values {};
    double beat = 0.0;
    int slot = EditedPatternStore::None;
    bool coherent = false;
    if (displayReady.load() && playingDisplay.load()) {
        // Bounded: if the audio thread is mid-publication or has no snapshot,
        // fall back to the current controls instead of spinning the UI thread.
        for (int attempt = 0; attempt < 64 && !coherent; ++attempt) {
            const unsigned before = displaySequence.load();
            if ((before & 1u) != 0) { std::this_thread::yield(); continue; }
            slot = editedPatterns.acquirePublished(audibleEditSlot);
            if (slot < 0) break;
            for (size_t i = 0; i < controlCount; ++i) values[i] = audibleControls[i].load();
            beat = audibleBeat.load();
            result.barBeats = audibleBarBeats.load();
            result.tempoBpm = audibleTempo.load();
            result.playing = audiblePlaying.load();
            result.historyReady = audibleHistoryReady.load();
            result.scratchActive = audibleScratchActive.load();
            result.slot = audibleSlot.load();
            result.cycleActive = audibleCycleActive.load();
            result.engaged = audibleEngaged.load();
            coherent = before == displaySequence.load();
            if (!coherent) { editedPatterns.release(slot); slot = EditedPatternStore::None; }
        }
    }
    if (!coherent) {
        beat = 0.0;
        result.playing = false;
        result.historyReady = true;
        result.scratchActive = false;
        for (size_t i = 0; i < controlCount; ++i) values[i] = rawControls[i]->load();
        result.barBeats = currentBarBeats();
        result.tempoBpm = currentTempoBpm();
        result.slot = selectedSlot();
        result.cycleActive = true;
        result.engaged = values[engage] > 0.5f;
        slot = editedPatterns.acquireLatest();
    }
    if (result.slot > 0) {
        const int bank = slotBanks.acquireLatest();
        result.pattern = bank >= 0 ? slotPatternFor(values, result.barBeats, slotBanks.get(bank), result.slot)
                                   : slotPatternFor(values, result.barBeats, SlotBank {}, result.slot);
        slotBanks.release(bank);
    } else {
        result.pattern = patternFor(values, result.barBeats, slot >= 0 ? &editedPatterns.get(slot) : nullptr);
    }
    editedPatterns.release(slot);
    result.every = everyCount(values[phraseEvery]);
    result.chainLength = chainLength(values[slotChain]);
    result.activeSample = evaluatePattern(result.pattern, beat);
    result.phase = displayFraction(result.pattern, beat);
    result.durationChoice = durationChoice(values[phraseLength]);
    result.timingChoice = timingChoice(values[phraseTiming]);
    result.factoryIndex = std::clamp(juce::roundToInt(values[factory]), 0, factoryPresetCount - 1);
    result.generated = values[generated] > 0.5f;
    result.edited = values[edited] > 0.5f || result.slot > 0;
    result.seed = static_cast<std::uint32_t>(std::clamp(juce::roundToInt(values[result.generated ? seed : variation]), 0, maximumSeed));
    return result;
}
const juce::String BackScratchProcessor::getProgramName(int i) { return factoryPresets()[static_cast<size_t>(std::clamp(i, 0, factoryPresetCount - 1))].name; }

juce::String BackScratchProcessor::currentPresetName() const
{
    if (const int slot = selectedSlot(); slot > 0)
        return juce::String("Slot ") + juce::String::charToString(static_cast<juce::juce_wchar>('A' + slot))
            + (slotFilled(slot) ? juce::String{} : juce::String(" (empty)"));
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
        const float normalized = parameter->convertTo0to1(actual);
        // Re-sending an unchanged value would still "touch" the parameter, and
        // hosts such as Live then switch off its automation.
        if (std::abs(parameter->getValue() - normalized) < 1.0e-7f) return;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(normalized);
        parameter->endChangeGesture();
    }
}

void BackScratchProcessor::setKeepSub(bool keep) { writeParameterGesture("keepSub", keep ? 1.0f : 0.0f); }
void BackScratchProcessor::selectFactoryPreset(int index)
{
    const ParameterGroup group(*this);
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
    const ParameterGroup group(*this);
    const auto old = generationSeed();
    auto next = static_cast<std::uint32_t>(1 + random.nextInt(maximumSeed));
    if (next == old) next = next % maximumSeed + 1;
    if (const int slot = selectedSlot(); slot > 0) {
        // Compose straight into the selected slot as editable geometry, so the
        // main pattern and its settings stay untouched. One undo step.
        const auto composed = generatePattern(next, durationBeats(rawControls[phraseLength]->load(), currentBarBeats()),
                                              currentBarBeats(), 2);
        auto geometry = fitPatternToPhrase(composed, 1.0);
        geometry.gridOffset = 0;
        geometry.editingRange = std::min(1.0, patternEditingDelayRange(geometry));
        if (validateEditedPattern(geometry)) {
            editUndo.push_back(captureEditSnapshot(slot));
            if (editUndo.size() > 64) editUndo.erase(editUndo.begin());
            editRedo.clear();
            publishSlot(slot, &geometry);
        }
        return;
    }
    clearEditHistory();
    writeParameterGesture("edited", 0.0f);
    writeParameterGesture("motionVersion", 2.0f);
    clearSavedName();
    if (!phraseLocked()) setPhraseDurationChoice(random.nextInt(phraseDurationCount));
    // Compose at the base knob duration; timing modifiers retime that same
    // identity and never force the bounded composer beyond its source range.
    writeParameterGesture("sourceBeats", static_cast<float>(durationBeats(rawControls[phraseLength]->load(), currentBarBeats())));
    writeParameterGesture("sourceBarBeats", static_cast<float>(currentBarBeats()));
    writeParameterGesture("generationSeed", static_cast<float>(next));
    writeParameterGesture("generated", 1.0f);
    writeParameterGesture("bypass", 0.0f);
}

void BackScratchProcessor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    // Render the delayed dry path, so the host's latency compensation still
    // lines the bypassed track up, and keep transport/history tracking alive
    // so leaving bypass does not look like a seek.
    hostBypassBlock = true;
    processBlock(audio, midi);
    hostBypassBlock = false;
}

void BackScratchProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int count = audio.getNumSamples(), channels = audio.getNumChannels();
    if (count <= 0 || channels <= 0) { automationEvents.clear(); return; }
    // A larger or smaller history prepared by the message thread is adopted
    // here, between callbacks, without allocating.
    engine.adoptRequestedHistory();
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
    // A host can integrate a tempo ramp across the previous callback while
    // reporting only the tempo at its start. Allow for that much drift.
    const double tempoAllowance = previousCount > 0 ? previousCount * std::abs(step - previousStep) : 0.0;
    const double positionTolerance = std::max(2.0 * step, 1.0e-7) + tempoAllowance;
    double positionError = std::abs(songBeat - expectedBeat);
    if (looping && songBeat >= loopStart - positionTolerance
        && expectedBeat >= loopStart - positionTolerance && expectedBeat <= loopEnd + positionTolerance)
        positionError = std::min(positionError, std::abs(positionError - (loopEnd - loopStart)));
    const bool seek = hasSongPosition && hadSongPosition && positionError > positionTolerance;
    const bool restored = resetPending.exchange(false);
    const bool discontinuity = restored || seek || playing != wasPlaying;
    if (discontinuity) {
        // Start a new musical history, but let the dry latency pipeline and
        // crossover keep running: no gap, and nothing stale is scratched.
        engine.invalidateHistory();
        clearControlHistory();
    }
    if (restored) {
        for (size_t i = 0; i < controlCount; ++i) controls[i] = lastRaw[i] = restoredControls[i].load();
        queueActive.fill(false);
    }

    // Read the controls and snapshots like a sequence lock. A grouped GUI
    // change (preset, randomize, load) is applied completely or not at all.
    const unsigned groupBefore = parameterGroupSequence.load();
    Values raw {};
    for (size_t i = 0; i < controlCount; ++i) raw[i] = rawControls[i]->load();
    const int latestSlot = editedPatterns.acquireLatest();
    const int latestBank = slotBanks.acquireLatest();
    const bool stable = (groupBefore & 1u) == 0 && parameterGroupSequence.load() == groupBefore;
    if ((stable || restored) && latestSlot >= 0) {
        editedPatterns.release(inputEditSlot);
        inputEditSlot = latestSlot;
    } else {
        editedPatterns.release(latestSlot);
    }
    if ((stable || restored) && latestBank >= 0 && latestBank != inputBankSlot) {
        slotBanks.release(inputBankSlot);
        inputBankSlot = latestBank;
        slotPatternIndex = -1;
    } else {
        slotBanks.release(latestBank);
    }

    int eventCount = 0;
    std::array<bool, controlCount> hasQueue {};
    // During an incomplete group, host points for this callback are dropped;
    // the parameter values they set are picked up in the next callback.
    for (int i = 0; stable && i < automationEvents.size; ++i) {
        const auto& event = automationEvents.events[static_cast<size_t>(i)];
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
    for (size_t i = 0; stable && i < controlCount; ++i) {
        if (hasQueue[i]) queueActive[i] = true;
        else if (!queueActive[i] || raw[i] != lastRaw[i]) controls[i] = raw[i];
        lastRaw[i] = raw[i];
    }
    int nextEvent = 0;
    float peakL = 0.0f, peakR = 0.0f;
    auto* left = audio.getWritePointer(0);
    auto* right = audio.getWritePointer(channels > 1 ? 1 : 0);
    ControlFrame renderedContext;
    double renderedPatternBeat = 0.0;
    int renderedSlot = 0;
    bool renderedCycleActive = true;
    const int blockEditSlot = inputEditSlot;
    if (discontinuity && engagePending) { engagedNow = engageRequested; engagePending = false; }
    for (int sample = 0; sample < count; ++sample) {
        while (nextEvent < eventCount && timedControls[static_cast<size_t>(nextEvent)].offset <= sample) {
            const auto& event = timedControls[static_cast<size_t>(nextEvent++)];
            controls[static_cast<size_t>(event.control)] = event.value;
        }
        const double beat = transportBeat(songBeat + (playing ? sample * step : 0.0));
        ControlFrame context {controls, beat, samplesPerBeat, barBeats, playing, blockEditSlot, hostBypassBlock};
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
        const auto& values = context.values;
        bool changed = context.barBeats != renderedBarBeats || context.editSlot != renderedEditSlot;
        for (const int c : {factory, variation, generated, seed, phraseLength, sourceBeats, sourceBarBeats, phraseTiming, patternShift, edited, motionVersion})
            changed = changed || values[static_cast<size_t>(c)] != renderedControls[static_cast<size_t>(c)];
        if (changed) {
            pattern = patternFor(values, context.barBeats, context.editSlot >= 0 ? &editedPatterns.get(context.editSlot) : nullptr);
            slotPatternIndex = -1;
        }
        if (context.editSlot != renderedEditSlot) {
            editedPatterns.retain(context.editSlot);
            editedPatterns.release(renderedEditSlot);
            renderedEditSlot = context.editSlot;
        }
        renderedControls = values;
        renderedBarBeats = context.barBeats;

        // Quantized engage on the audible clock: a change waits for the next
        // 1/16 ... bar line; the engine then blends in or out over 3 ms.
        const bool wantEngaged = values[engage] > 0.5f;
        if (wantEngaged != engageRequested) {
            engageRequested = wantEngaged;
            if (!context.playing) {
                engagedNow = wantEngaged;
                engagePending = false;
                if (wantEngaged) retriggerOrigin = context.beat;
            } else {
                const double grid = quantizeBeats(values[engageQuantize], context.barBeats);
                engageBoundary = std::ceil(context.beat / grid - 1.0e-9) * grid;
                engagePending = true;
            }
        }
        if (engagePending && (context.beat >= engageBoundary - 1.0e-9 || context.beat < lastContextBeat - 1.0e-6)) {
            engagedNow = engageRequested;
            engagePending = false;
            if (engagedNow) retriggerOrigin = context.beat >= engageBoundary - 1.0e-9 ? engageBoundary : context.beat;
        }
        lastContextBeat = context.beat;
        // Retrigger restarts the phrase where it was engaged.
        const double patternBeat = values[retrigger] > 0.5f ? context.beat - retriggerOrigin : context.beat;
        const auto cycle = floorDivision(patternBeat / pattern.beats + 1.0e-9);
        const int every = everyCount(values[phraseEvery]);
        const bool cycleActive = every == 1 || positiveModulo(cycle, every) == every - 1;
        const int chain = chainLength(values[slotChain]);
        const int slot = chain > 1 ? positiveModulo(cycle, chain) : std::clamp(juce::roundToInt(values[patternSlot]), 0, slotCount - 1);
        const ScratchPattern* active = &pattern;
        if (slot > 0) {
            if (slotPatternIndex != slot) {
                slotPattern = inputBankSlot >= 0 ? slotPatternFor(values, context.barBeats, slotBanks.get(inputBankSlot), slot)
                                                 : fitPatternToPhrase(dryGeometry(), pattern.beats);
                slotPatternIndex = slot;
            }
            active = &slotPattern;
        }
        auto point = evaluatePattern(*active, patternBeat);
        if (!engagedNow || !cycleActive) point.wet = 0.0f;
        LiveScratchEngine::Shaping shaping;
        const double depthFactor = values[depth] / 100.0;
        if (!juce::exactlyEqual(depthFactor, 1.0)) {
            point.delayBeats *= depthFactor;
            point.playbackRate = 1.0 - depthFactor * (1.0 - point.playbackRate);
            if (depthFactor > 1.0) shaping.speedLimit = LiveScratchEngine::maximumSpeed;
        }
        if (const double offset = lookbackBeats(values[lookback], context.barBeats); offset > 0.0 && point.wet > 0.0f)
            point.delayBeats += offset;
        shaping.mix = values[wetMix] / 100.0;
        shaping.gain = juce::exactlyEqual(values[outputTrim], 0.0f) ? 1.0 : std::pow(10.0, values[outputTrim] / 20.0);
        shaping.split = std::clamp(juce::roundToInt(values[subSplit]), 0, static_cast<int>(LiveScratchEngine::splitFrequencies.size()) - 1);
        renderedContext = context;
        renderedPatternBeat = patternBeat;
        renderedSlot = slot;
        renderedCycleActive = cycleActive && engagedNow;
        const bool dryPath = values[bypass] > 0.5f || !context.playing || context.hostBypass;
        const auto out = engine.process(left[sample], right[sample], point, context.samplesPerBeat,
                                        values[sub] > 0.5f, dryPath, shaping);
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
    audibleBeat.store(renderedPatternBeat);
    audibleBarBeats.store(renderedContext.barBeats);
    audibleTempo.store(60.0 * rate / renderedContext.samplesPerBeat);
    audiblePlaying.store(renderedContext.playing);
    audibleHistoryReady.store(engine.isHistoryReady());
    audibleScratchActive.store(engine.isScratchActive());
    audibleSlot.store(renderedSlot);
    audibleCycleActive.store(renderedCycleActive);
    audibleEngaged.store(engagedNow);
    displaySequence.fetch_add(1);
    displayReady.store(renderedContext.editSlot >= 0);
    displayedPhase.store(displayFraction(renderedSlot > 0 ? slotPattern : pattern, renderedPatternBeat));
    barBeatsDisplay.store(barBeats);
    tempoDisplay.store(bpm);
    playingDisplay.store(playing);
    peaks[0].store(peakL); peaks[1].store(peakR);
    automationEvents.clear();
    freeBeat = expectedBeat = endBeat;
    previousStep = step;
    previousCount = count;
    hadSongPosition = hasSongPosition;
    wasPlaying = playing;
}

// ----- State -----

void BackScratchProcessor::applyNeutralPerformance(juce::ValueTree& tree) const
{
    // Older projects and presets predate these controls; their neutral values
    // reproduce the 0.10 sound. replaceState() would otherwise keep whatever
    // this instance currently has.
    for (const auto& control : neutralControls) {
        auto child = tree.getChildWithProperty("id", control.id);
        if (!child.isValid()) { child = juce::ValueTree("PARAM"); child.setProperty("id", control.id, nullptr); tree.addChild(child, -1, nullptr); }
        child.setProperty("value", control.value, nullptr);
    }
}

void BackScratchProcessor::getStateInformation(juce::MemoryBlock& result)
{
    const juce::ScopedLock editGuard(editLock);
    auto snapshot = state.copyState();
    snapshot.setProperty("engineVersion", stateVersion, nullptr);
    // Use the copied parameter value, so concurrent host automation cannot
    // produce conflicting recipe metadata and parameter data in one save.
    snapshot.setProperty("generatorVersion", juce::roundToInt(static_cast<float>(
        snapshot.getChildWithProperty("id", "motionVersion").getProperty("value", 2.0f))), nullptr);
    snapshot.setProperty("historyMode", historyModeSetting.load(), nullptr);
    snapshot.setProperty("editorScale", static_cast<double>(editorScaleSetting.load()), nullptr);
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
        if (const int bank = slotBanks.acquireLatest(); bank >= 0) {
            const auto& slots = slotBanks.get(bank);
            for (int k = 0; k < SlotBank::count; ++k) {
                if (!slots.filled[static_cast<size_t>(k)]) continue;
                auto* node = xml->createNewChildElement("SLOT");
                node->setAttribute("index", k + 1);
                node->addChildElement(editedPatternToXml(slots.patterns[static_cast<size_t>(k)]).release());
            }
            slotBanks.release(bank);
        }
        copyXmlToBinary(*xml, result);
    }
}

namespace {
// Reads SLOT children (schema 10 projects and presets). False if any is invalid.
bool readSlots(const juce::XmlElement& xml, SlotBank& bank)
{
    bank = SlotBank {};
    for (auto* node : xml.getChildWithTagNameIterator("SLOT")) {
        int index = 0;
        if (!readInteger(node->getStringAttribute("index"), 1, SlotBank::count, index)) return false;
        const auto* geometry = node->getChildByName("EDITED_PATTERN");
        ScratchPattern parsed;
        if (geometry == nullptr || !editedPatternFromXml(*geometry, parsed)) return false;
        bank.patterns[static_cast<size_t>(index - 1)] = parsed;
        bank.filled[static_cast<size_t>(index - 1)] = true;
    }
    return true;
}
}

void BackScratchProcessor::setStateInformation(const void* data, int size)
{
    const ParameterGroup group(*this);
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
        for (const auto& control : neutralControls) setHostParameter(control.id, control.value);
        slotBanks.publish(SlotBank {});
        historyModeSetting.store(LiveScratchEngine::maximumHistorySeconds);
        clearEditHistory();
        resetAfterRestore();
        maintainHistory();
        return;
    }
    if (size > 4 * 1024 * 1024) return;
    const auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || !xml->hasTagName("BACK_SCRATCH_STATE")) return;
    auto loaded = juce::ValueTree::fromXml(*xml);
    const int version = static_cast<int>(loaded.getProperty("engineVersion", 0));
    if (version < 4 || version > stateVersion) return;
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
    SlotBank bank;
    int history = LiveScratchEngine::maximumHistorySeconds;
    float scale = 0.0f;
    if (version >= 10) {
        if (!readSlots(*xml, bank)) return;
        double mode = 0.0, savedScale = 0.0;
        if (loaded.hasProperty("historyMode")) {
            if (!readNumber(loaded.getProperty("historyMode").toString(), 0.0, 128.0, mode)) return;
            history = juce::roundToInt(mode);
        } else {
            history = 0;
        }
        if (loaded.hasProperty("editorScale") && readNumber(loaded.getProperty("editorScale").toString(), 0.0, 2.0, savedScale))
            scale = static_cast<float>(savedScale);
    } else {
        // Projects from 0.10 and earlier: the 0.11 controls stay neutral, no
        // slots, and the full 128-second history they were made with.
        applyNeutralPerformance(loaded);
    }
    if (editedPatterns.publish(restoredPattern) < 0) return;
    if (slotBanks.publish(bank) < 0) return;
    for (int i = loaded.getNumChildren(); --i >= 0;)
        if (loaded.getChild(i).hasType("EDITED_PATTERN") || loaded.getChild(i).hasType("SLOT")) loaded.removeChild(i, nullptr);
    clearEditHistory();
    state.replaceState(loaded);
    historyModeSetting.store(history == 16 || history == 32 || history == 64 || history == 128 ? history : 0);
    editorScaleSetting.store(validEditorScale(scale));
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
    maintainHistory();
}

// ----- Presets -----

juce::File BackScratchProcessor::userPresetRoot()
{
    if (userPresetRootOverride() != juce::File()) return userPresetRootOverride();
    // ~/Library/Application Support/BACK-SCRATCH/Presets on macOS, %APPDATA%\BACK-SCRATCH\Presets on Windows.
    auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    root = root.getChildFile("Application Support");
   #endif
    return root.getChildFile("BACK-SCRATCH").getChildFile("Presets");
}

void BackScratchProcessor::setUserPresetRootForTesting(const juce::File& root) { userPresetRootOverride() = root; }

std::unique_ptr<juce::XmlElement> BackScratchProcessor::presetXml(const juce::String& name) const
{
    const juce::ScopedLock editGuard(editLock);
    auto xml = std::make_unique<juce::XmlElement>("BACK_SCRATCH_PRESET");
    xml->setAttribute("version", stateVersion);
    xml->setAttribute("generatorVersion", juce::roundToInt(rawControls[motionVersion]->load()));
    xml->setAttribute("edited", hasCustomPattern() ? 1 : 0);
    const int slot = editedPatterns.acquireLatest();
    if (slot < 0) return nullptr;
    xml->addChildElement(editedPatternToXml(editedPatterns.get(slot)).release());
    editedPatterns.release(slot);
    xml->setAttribute("name", name.substring(0, 80));
    xml->setAttribute("factory", selectedFactoryPreset()); xml->setAttribute("variation", static_cast<int>(variationSeed()));
    xml->setAttribute("generated", isGenerated() ? 1 : 0); xml->setAttribute("generationSeed", static_cast<int>(generationSeed()));
    xml->setAttribute("phraseDuration", phraseDurationChoice()); xml->setAttribute("phraseLocked", phraseLocked() ? 1 : 0);
    xml->setAttribute("phraseTiming", phraseTimingChoice()); xml->setAttribute("patternShift", patternShiftSteps());
    xml->setAttribute("sourceBeats", static_cast<double>(rawControls[sourceBeats]->load()));
    xml->setAttribute("sourceBarBeats", static_cast<double>(rawControls[sourceBarBeats]->load()));
    xml->setAttribute("keepSub", keepSub() ? 1 : 0);
    // 0.11 sound settings. Engage, Output and history are performance and
    // machine settings, so presets leave them as they are.
    xml->setAttribute("engageQuantize", juce::roundToInt(rawControls[engageQuantize]->load()));
    xml->setAttribute("retrigger", rawControls[retrigger]->load() > 0.5f ? 1 : 0);
    xml->setAttribute("phraseEvery", juce::roundToInt(rawControls[phraseEvery]->load()));
    xml->setAttribute("patternSlot", selectedSlot());
    xml->setAttribute("slotChain", slotChainChoice());
    xml->setAttribute("wetMix", static_cast<double>(rawControls[wetMix]->load()));
    xml->setAttribute("subSplit", juce::roundToInt(rawControls[subSplit]->load()));
    xml->setAttribute("depth", static_cast<double>(rawControls[depth]->load()));
    xml->setAttribute("lookback", juce::roundToInt(rawControls[lookback]->load()));
    if (const int bank = slotBanks.acquireLatest(); bank >= 0) {
        const auto& slots = slotBanks.get(bank);
        for (int k = 0; k < SlotBank::count; ++k) {
            if (!slots.filled[static_cast<size_t>(k)]) continue;
            auto* node = xml->createNewChildElement("SLOT");
            node->setAttribute("index", k + 1);
            node->addChildElement(editedPatternToXml(slots.patterns[static_cast<size_t>(k)]).release());
        }
        slotBanks.release(bank);
    }
    return xml;
}

juce::Result BackScratchProcessor::savePreset(const juce::File& file)
{
    const juce::ScopedLock editGuard(editLock);
    const auto xml = presetXml(file.getFileNameWithoutExtension());
    if (xml == nullptr) return juce::Result::fail("The pattern is updating. Try saving again.");
    if (!file.getParentDirectory().createDirectory().wasOk() || !file.replaceWithText(xml->toString()))
        return juce::Result::fail("Couldn't save this preset. Choose a writable folder.");
    {
        const juce::ScopedLock lock(nameLock);
        savedName = xml->getStringAttribute("name"); namedFactory = selectedFactoryPreset();
        namedGenerated = isGenerated(); namedDuration = phraseDurationChoice();
        namedEdited = hasCustomPattern();
        namedMotionVersion = juce::roundToInt(rawControls[motionVersion]->load());
        namedTiming = phraseTimingChoice(); namedShift = patternShiftSteps();
        namedSourceBeats = rawControls[sourceBeats]->load(); namedSourceBarBeats = rawControls[sourceBarBeats]->load();
        namedSeed = isGenerated() ? generationSeed() : variationSeed();
    }
    return juce::Result::ok();
}

juce::Result BackScratchProcessor::loadPreset(const juce::File& file)
{
    if (!file.existsAsFile() || file.getSize() > 4 * 1024 * 1024) return juce::Result::fail("Choose a BACK-SCRATCH preset file.");
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("BACK_SCRATCH_PRESET"))
        return juce::Result::fail("This isn't a BACK-SCRATCH preset.");
    return loadPresetXml(*xml, file.getFileNameWithoutExtension());
}

ScratchPattern BackScratchProcessor::presetGeometry(const juce::XmlElement& xml) const
{
    // The preset's main pattern as normalized geometry, for loading into a slot.
    Values values {};
    for (size_t i = 0; i < controlCount; ++i) values[i] = rawControls[i]->load();
    values[factory] = static_cast<float>(std::clamp(xml.getIntAttribute("factory", 0), 0, factoryPresetCount - 1));
    values[variation] = static_cast<float>(std::clamp(xml.getIntAttribute("variation", 0), 0, 65535));
    values[generated] = xml.getIntAttribute("generated", 0) != 0 ? 1.0f : 0.0f;
    values[seed] = static_cast<float>(std::clamp(xml.getIntAttribute("generationSeed", 1), 0, maximumSeed));
    values[sourceBeats] = static_cast<float>(std::clamp(xml.getDoubleAttribute("sourceBeats", 4.0), 0.25, 256.0));
    values[sourceBarBeats] = static_cast<float>(std::clamp(xml.getDoubleAttribute("sourceBarBeats", 4.0), 0.0, 16.0));
    values[motionVersion] = static_cast<float>(std::clamp(xml.getIntAttribute("generatorVersion", 1), 1, 2));
    values[phraseLength] = static_cast<float>(std::clamp(xml.getIntAttribute("phraseDuration", 3), 0, phraseDurationCount - 1));
    values[phraseTiming] = 0.0f;
    values[patternShift] = 0.0f;
    ScratchPattern custom = dryGeometry();
    bool hasCustom = false;
    if (const auto* node = xml.getChildByName("EDITED_PATTERN")) hasCustom = editedPatternFromXml(*node, custom);
    values[edited] = hasCustom && xml.getIntAttribute("edited", 0) != 0 ? 1.0f : 0.0f;
    auto geometry = fitPatternToPhrase(patternFor(values, currentBarBeats(), &custom), 1.0);
    geometry.gridOffset = 0;
    if (geometry.editingRange <= 0.0) geometry.editingRange = std::min(1.0, patternEditingDelayRange(geometry));
    return validateEditedPattern(geometry) ? geometry : dryGeometry();
}

juce::Result BackScratchProcessor::loadPresetXml(const juce::XmlElement& xmlIn, const juce::String& fallbackName)
{
    const ParameterGroup group(*this);
    const auto* xml = &xmlIn;
    const int version = xml->getIntAttribute("version");
    if (version < 4 || version > stateVersion) return juce::Result::fail("This preset version isn't supported.");
    if (const int slot = selectedSlot(); slot > 0) {
        // With a slot selected, a preset only provides that slot's pattern.
        auto geometry = presetGeometry(*xml);
        editUndo.push_back(captureEditSnapshot(slot));
        if (editUndo.size() > 64) editUndo.erase(editUndo.begin());
        editRedo.clear();
        publishSlot(slot, &geometry);
        return juce::Result::ok();
    }
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
    // 0.11 settings: neutral for older presets.
    std::array<float, neutralControls.size()> performance {};
    for (size_t i = 0; i < neutralControls.size(); ++i) performance[i] = neutralControls[i].value;
    SlotBank bank;
    if (version >= 10) {
        int quantize = 0, retriggerOn = 0, every = 0, slot = 0, chain = 0, split = 0, lookbackChoice = 0;
        double mixValue = 100.0, depthValue = 100.0;
        if (!readInteger(xml->getStringAttribute("engageQuantize"), 0, 4, quantize)
            || !readInteger(xml->getStringAttribute("retrigger"), 0, 1, retriggerOn)
            || !readInteger(xml->getStringAttribute("phraseEvery"), 0, 3, every)
            || !readInteger(xml->getStringAttribute("patternSlot"), 0, slotCount - 1, slot)
            || !readInteger(xml->getStringAttribute("slotChain"), 0, 3, chain)
            || !readNumber(xml->getStringAttribute("wetMix"), 0.0, 100.0, mixValue)
            || !readInteger(xml->getStringAttribute("subSplit"), 0, 3, split)
            || !readNumber(xml->getStringAttribute("depth"), 0.0, 200.0, depthValue)
            || !readInteger(xml->getStringAttribute("lookback"), 0, 4, lookbackChoice)
            || !readSlots(*xml, bank))
            return juce::Result::fail("This preset contains invalid 0.11 settings.");
        performance = {rawControls[engage]->load(), static_cast<float>(quantize), static_cast<float>(retriggerOn),
                       static_cast<float>(every), static_cast<float>(slot), static_cast<float>(chain),
                       static_cast<float>(mixValue), rawControls[outputTrim]->load(), static_cast<float>(split),
                       static_cast<float>(depthValue), static_cast<float>(lookbackChoice)};
    } else {
        // Engage and Output are performance settings; keep them.
        performance[0] = rawControls[engage]->load();
        performance[7] = rawControls[outputTrim]->load();
    }
    if (editedPatterns.publish(restoredPattern) < 0) return juce::Result::fail("The pattern is updating. Try loading again.");
    if (slotBanks.publish(bank) < 0) return juce::Result::fail("The pattern is updating. Try loading again.");
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
    for (size_t i = 0; i < neutralControls.size(); ++i) writeParameterGesture(neutralControls[i].id, performance[i]);
    {
        const juce::ScopedLock lock(nameLock);
        savedName = xml->getStringAttribute("name", fallbackName).substring(0, 80);
        namedFactory = index; namedGenerated = generatedPattern != 0; namedDuration = duration;
        namedEdited = enabled != 0;
        namedMotionVersion = restoredMotion;
        namedTiming = timing; namedShift = shift;
        namedSourceBeats = static_cast<float>(originalBeats); namedSourceBarBeats = static_cast<float>(originalBar);
        namedSeed = static_cast<std::uint32_t>(generatedPattern ? generation : variationValue);
    }
    bankSelection.store(-1);
    return juce::Result::ok();
}

void BackScratchProcessor::selectBankPreset(int index)
{
    const auto& presets = bankPresets();
    if (index < 0 || index >= static_cast<int>(presets.size())) return;
    const auto& preset = presets[static_cast<size_t>(index)];
    const ParameterGroup group(*this);
    if (const int slot = selectedSlot(); slot > 0) {
        // Into the selected slot: just the pattern, as one undoable step.
        ScratchPattern geometry = preset.build != nullptr ? preset.build()
            : fitPatternToPhrase(makePattern(preset.classic, 0, 2), 1.0);
        geometry.gridOffset = 0;
        if (geometry.editingRange <= 0.0) geometry.editingRange = std::min(1.0, patternEditingDelayRange(geometry));
        if (!validateEditedPattern(geometry)) return;
        editUndo.push_back(captureEditSnapshot(slot));
        if (editUndo.size() > 64) editUndo.erase(editUndo.begin());
        editRedo.clear();
        publishSlot(slot, &geometry);
        bankSelection.store(index);
        return;
    }
    const auto& settings = preset.settings;
    if (preset.classic >= 0) {
        // Original recipes keep the 0.10 behaviour of factory selection (length
        // lock, timing, Keep Sub). Exact Classic entries also return the 0.11
        // sound settings to neutral; tuned entries apply their own.
        selectFactoryPreset(preset.classic);
        writeParameterGesture("engageQuantize", static_cast<float>(settings.engageQuantize));
        writeParameterGesture("retrigger", settings.retrigger ? 1.0f : 0.0f);
        writeParameterGesture("phraseEvery", static_cast<float>(settings.every));
        writeParameterGesture("slotChain", static_cast<float>(settings.chain));
        writeParameterGesture("wetMix", settings.mix);
        writeParameterGesture("subSplit", static_cast<float>(settings.split));
        writeParameterGesture("depth", settings.depth);
        writeParameterGesture("lookback", static_cast<float>(settings.lookback));
        {
            const juce::ScopedLock lock(nameLock);
            savedName = preset.name;
            namedFactory = preset.classic; namedGenerated = false; namedEdited = false;
            namedMotionVersion = 2; namedDuration = phraseDurationChoice(); namedTiming = phraseTimingChoice();
            namedShift = 0; namedSeed = 0;
        }
        bankSelection.store(index);
        return;
    }
    // New presets: complete designs, including their phrase length and timing.
    auto xml = std::make_unique<juce::XmlElement>("BACK_SCRATCH_PRESET");
    auto geometry = preset.build != nullptr ? preset.build() : dryGeometry();
    xml->setAttribute("version", stateVersion);
    xml->setAttribute("generatorVersion", 2);
    xml->setAttribute("edited", 1);
    xml->addChildElement(editedPatternToXml(geometry).release());
    xml->setAttribute("name", juce::String(preset.name));
    xml->setAttribute("factory", 0); xml->setAttribute("variation", 0);
    xml->setAttribute("generated", 0); xml->setAttribute("generationSeed", 1);
    xml->setAttribute("phraseDuration", settings.phraseDuration);
    xml->setAttribute("phraseLocked", phraseLocked() ? 1 : 0);
    xml->setAttribute("phraseTiming", settings.timing); xml->setAttribute("patternShift", 0);
    xml->setAttribute("sourceBeats", 4.0); xml->setAttribute("sourceBarBeats", 4.0);
    xml->setAttribute("keepSub", keepSub() ? 1 : 0);
    xml->setAttribute("engageQuantize", settings.engageQuantize);
    xml->setAttribute("retrigger", settings.retrigger ? 1 : 0);
    xml->setAttribute("phraseEvery", settings.every);
    xml->setAttribute("patternSlot", 0);
    xml->setAttribute("slotChain", settings.chain);
    xml->setAttribute("wetMix", static_cast<double>(settings.mix));
    xml->setAttribute("subSplit", settings.split);
    xml->setAttribute("depth", static_cast<double>(settings.depth));
    xml->setAttribute("lookback", settings.lookback);
    for (int k = 0; k < SlotBank::count; ++k) {
        const auto build = preset.slots[static_cast<size_t>(k)];
        if (build == nullptr) continue;
        auto* node = xml->createNewChildElement("SLOT");
        node->setAttribute("index", k + 1);
        node->addChildElement(editedPatternToXml(build()).release());
    }
    if (loadPresetXml(*xml, preset.name).wasOk()) bankSelection.store(index);
}

// ----- Slots -----

bool BackScratchProcessor::publishSlot(int slot, const ScratchPattern* geometry)
{
    if (slot <= 0 || slot >= slotCount) return false;
    const int latest = slotBanks.acquireLatest();
    SlotBank bank = latest >= 0 ? slotBanks.get(latest) : SlotBank {};
    slotBanks.release(latest);
    const auto index = static_cast<size_t>(slot - 1);
    bank.filled[index] = geometry != nullptr;
    bank.patterns[index] = geometry != nullptr ? *geometry : ScratchPattern {};
    if (slotBanks.publish(bank) < 0) return false;
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    return true;
}

void BackScratchProcessor::copyPatternToSlot(int source, int target)
{
    const juce::ScopedLock lock(editLock);
    if (editStart || source == target || target < 0 || target >= slotCount || source < 0 || source >= slotCount) return;
    auto snapshot = captureEditSnapshot(source);
    snapshot.slot = target;
    snapshot.enabled = true;
    const auto before = captureEditSnapshot(target);
    if (!applyEditSnapshot(snapshot)) return;
    editUndo.push_back(before);
    if (editUndo.size() > 64) editUndo.erase(editUndo.begin());
    editRedo.clear();
    clearSavedName();
}

void BackScratchProcessor::clearSlot(int slot)
{
    const juce::ScopedLock lock(editLock);
    if (editStart || slot <= 0 || slot >= slotCount || !slotFilled(slot)) return;
    const auto before = captureEditSnapshot(slot);
    if (!publishSlot(slot, nullptr)) return;
    editUndo.push_back(before);
    if (editUndo.size() > 64) editUndo.erase(editUndo.begin());
    editRedo.clear();
}

juce::AudioProcessorEditor* BackScratchProcessor::createEditor() { return new BackScratchEditor(*this); }
} // namespace backScratch

void backScratchBridge::Router::push(const void* parameter, int offset, float normalizedValue) noexcept
{
    // The pinned VST3 wrapper (and the tests) pass juce::AudioProcessorParameter
    // pointers. Only BACK-SCRATCH controls carry a tag with their owner's queue.
    if (parameter == nullptr) return;
    const auto* base = static_cast<const juce::AudioProcessorParameter*>(parameter);
    if (const auto* tag = dynamic_cast<const backScratchBridge::QueueTag*>(base); tag != nullptr && tag->queue != nullptr)
        tag->queue->push(parameter, offset, normalizedValue);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new backScratch::BackScratchProcessor(); }
