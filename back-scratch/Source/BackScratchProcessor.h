#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <array>
#include "ScratchPattern.h"
#include "LiveScratchEngine.h"
#include "EditedPatternStore.h"
#include "AutomationBridge.h"
#include "PresetBank.h"
#include <optional>

namespace backScratch {

class BackScratchProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    BackScratchProcessor();
    ~BackScratchProcessor() override;
    const juce::String getName() const override { return "BACK-SCRATCH"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // Hosts that drive VST3 bypass call this instead of processBlock. It runs
    // the normal path with a latency-aligned dry output, so the host's delay
    // compensation stays correct and transport/history tracking continues.
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return factoryPresetCount; }
    int getCurrentProgram() override { return selectedFactoryPreset(); }
    void setCurrentProgram(int i) override { selectFactoryPreset(i); }
    const juce::String getProgramName(int i) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& parameters() noexcept { return state; }
    const juce::AudioProcessorValueTreeState& parameters() const noexcept { return state; }
    void randomizePattern();
    void selectFactoryPreset(int);
    int selectedFactoryPreset() const noexcept;
    std::uint32_t variationSeed() const noexcept;
    juce::String currentPresetName() const;
    bool keepSub() const noexcept;
    void setKeepSub(bool);
    // The pattern of the selected slot (A = main pattern), at phrase length.
    ScratchPattern currentPattern() const noexcept;
    bool hasCustomPattern() const noexcept;
    bool beginPatternEdit();
    bool previewPatternEdit(const ScratchPattern&);
    bool commitPatternEdit();
    void cancelPatternEdit();
    bool canUndoPatternEdit() const;
    bool canRedoPatternEdit() const;
    void undoPatternEdit();
    void redoPatternEdit();
    bool isGenerated() const noexcept;
    std::uint32_t generationSeed() const noexcept;
    static constexpr int phraseDurationCount = 8;
    int phraseDurationChoice() const noexcept;
    void setPhraseDurationChoice(int);
    juce::String phraseDurationLabel() const;
    static juce::String durationLabel(int choice, int timing);
    int phraseTimingChoice() const noexcept;
    void setPhraseTimingChoice(int);
    int patternShiftSteps() const noexcept;
    void shiftPattern(int delta);
    double phraseBeats() const noexcept;
    int phraseBars() const noexcept;
    void setPhraseBars(int);
    bool phraseLocked() const noexcept;
    void setPhraseLocked(bool);
    double currentBarBeats() const noexcept { return barBeatsDisplay.load(); }
    float phase() const noexcept { return displayedPhase.load(); }

    // ----- Performance controls (0.11) -----
    static constexpr int slotCount = 8;
    int selectedSlot() const noexcept;
    void selectSlot(int);
    bool slotFilled(int slot) const noexcept;       // slot A is always "filled"
    void copyPatternToSlot(int source, int target); // whole-pattern copy, undoable
    void clearSlot(int slot);
    int slotChainChoice() const noexcept;
    bool isEngaged() const noexcept;                // the parameter
    bool isAudiblyEngaged() const noexcept { return audibleEngaged.load(); }
    void setEngaged(bool);
    void setChoice(const char* id, int value) { writeParameterGesture(id, static_cast<float>(value)); }
    void setValue(const char* id, float value) { writeParameterGesture(id, value); }
    float parameterValue(const char* id) const noexcept;
    // History length: 0 = automatic (sized to the pattern, tempo and settings),
    // otherwise a fixed 16, 32, 64 or 128 seconds. Not a host parameter.
    int historyMode() const noexcept { return historyModeSetting.load(); }
    void setHistoryMode(int seconds);
    double historySeconds() const noexcept { return engine.historySeconds(); }
    double neededHistorySeconds() const;
    // Message thread: grow (or, for a fixed choice, resize) the history when
    // the current settings need it. Also runs from the processor's timer.
    void maintainHistory();
    // Draw/roll snap for the editor, kept by the processor across editor
    // sessions (not saved).
    bool snapToGrid() const noexcept { return gridSnap.load(); }
    void setSnapToGrid(bool on) noexcept { gridSnap.store(on); }
    // Editor size chosen from the Size menu (0 = never chosen). Saved with the
    // project as the non-parameter property "editorScale".
    float editorScale() const noexcept { return editorScaleSetting.load(); }
    void setEditorScale(float);

    // ----- Presets -----
    // The factory bank: Classic (the 75 original recipes, unchanged) plus the
    // 0.11 folders. Choosing one loads into the selected slot (see README).
    void selectBankPreset(int index);
    int selectedBankPreset() const noexcept { return bankSelection.load(); }
    juce::Result savePreset(const juce::File&);
    juce::Result loadPreset(const juce::File&);
    juce::Result loadPresetXml(const juce::XmlElement&, const juce::String& fallbackName);
    std::unique_ptr<juce::XmlElement> presetXml(const juce::String& name) const;
    // User presets live in folders below this root. Tests redirect it.
    static juce::File userPresetRoot();
    static void setUserPresetRootForTesting(const juce::File&);

    struct DisplayState {
        ScratchPattern pattern;
        PatternSample activeSample;
        float phase = 0.0f;
        double barBeats = 4.0, tempoBpm = 120.0;
        bool playing = false;
        int durationChoice = 3, timingChoice = 0, factoryIndex = 0;
        bool generated = false;
        bool edited = false;
        bool historyReady = true, scratchActive = false;
        std::uint32_t seed = 0;
        // 0.11: the slot heard now, whether this phrase plays (Play Every),
        // and whether the pattern is engaged.
        int slot = 0, every = 1, chainLength = 1;
        bool cycleActive = true, engaged = true;
    };
    DisplayState displayState() const noexcept;
    bool isTransportPlaying() const noexcept { return playingDisplay.load(); }
    double currentTempoBpm() const noexcept { return tempoDisplay.load(); }
    float outputPeak(int channel) const noexcept { return peaks[static_cast<size_t>(std::clamp(channel, 0, 1))].load(); }
    void setHostParameter(const char* id, float actual);
    // Sample-accurate host automation for this instance (see AutomationBridge.h).
    backScratchBridge::Queue& automationQueue() noexcept { return automationEvents; }

    // Groups several parameter writes so the audio thread switches to all of
    // them at once. While a group is open (or was torn during a callback), the
    // audio thread keeps its previous controls and edit snapshot. The group
    // also holds the edit lock, so a concurrent state save sees all or none.
    class ParameterGroup final {
    public:
        explicit ParameterGroup(BackScratchProcessor&);
        ~ParameterGroup();
        ParameterGroup(const ParameterGroup&) = delete;
        ParameterGroup& operator=(const ParameterGroup&) = delete;
    private:
        BackScratchProcessor& owner;
        const juce::ScopedLock lock;
    };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void timerCallback() override;
    void writeParameterGesture(const char* id, float actual);
    void clearSavedName();
    void resetAfterRestore();
    juce::AudioProcessorValueTreeState state;
    LiveScratchEngine engine;
    ScratchPattern pattern, slotPattern;
    int slotPatternIndex = -1, slotPatternBank = -1;
    enum Control { factory, variation, sub, bypass, generated, seed, phraseLength, phraseLock, sourceBeats, sourceBarBeats,
                   phraseTiming, patternShift, edited, motionVersion,
                   engage, engageQuantize, retrigger, phraseEvery, patternSlot, slotChain,
                   wetMix, outputTrim, subSplit, depth, lookback, controlCount };
    using Values = std::array<float, controlCount>;
    static ScratchPattern patternFor(const Values&, double barBeats, const ScratchPattern* custom = nullptr) noexcept;
    static ScratchPattern slotPatternFor(const Values&, double barBeats, const SlotBank&, int slot) noexcept;
    struct ControlFrame {
        Values values {};
        double beat = 0.0, samplesPerBeat = 24000.0, barBeats = 4.0;
        bool playing = true;
        int editSlot = EditedPatternStore::None;
        bool hostBypass = false;
    };
    std::vector<ControlFrame> controlHistory;
    size_t controlWrite = 0;
    int controlFrames = 0;
    Values renderedControls {};
    double renderedBarBeats = 4.0;
    EditedPatternStore editedPatterns;
    SlotBankStore slotBanks;
    int renderedEditSlot = EditedPatternStore::None;
    // The snapshot written into new control frames. It persists between
    // callbacks, so an unavailable or grouped publication keeps the last one.
    int inputEditSlot = EditedPatternStore::None;
    int inputBankSlot = SlotBankStore::None;
    std::atomic<int> audibleEditSlot { EditedPatternStore::None };
    void clearControlHistory() noexcept;
    void clearEditHistory();
    // slot: 0 = main pattern (A); 1..7 = bank slots B..H. enabled is the
    // Edited flag for A and the filled flag for B..H.
    struct EditSnapshot { ScratchPattern pattern; bool enabled = false; int slot = 0; };
    EditSnapshot captureEditSnapshot(int slot) const;
    bool applyEditSnapshot(const EditSnapshot&);
    bool publishSlot(int slot, const ScratchPattern* geometry);
    ScratchPattern presetGeometry(const juce::XmlElement&) const;
    void applyNeutralPerformance(juce::ValueTree&) const;
    mutable juce::CriticalSection editLock;
    std::optional<EditSnapshot> editStart;
    int editStartSlot = EditedPatternStore::None;
    std::vector<EditSnapshot> editUndo, editRedo;
    bool editChanged = false;
    struct TimedControl { int offset = 0; int control = 0; float value = 0.0f; int order = 0; };
    std::array<juce::RangedAudioParameter*, controlCount> controlParameters {};
    std::array<std::atomic<float>*, controlCount> rawControls {};
    Values controls {}, lastRaw {};
    std::array<std::atomic<float>, controlCount> restoredControls {};
    std::array<bool, controlCount> queueActive {};
    std::array<TimedControl, 4096> timedControls {};
    double rate = 48000.0, freeBeat = 0.0, expectedBeat = 0.0;
    // The previous callback's beat step and length: a host may integrate a
    // tempo ramp across that callback while reporting only its start tempo.
    double previousStep = 0.0;
    int previousCount = 0;
    bool wasPlaying = false, hadSongPosition = false;
    bool hostBypassBlock = false;
    // Quantized engage, evaluated on the delayed (audible) clock.
    bool engageRequested = true, engagedNow = true, engagePending = false;
    double engageBoundary = 0.0, retriggerOrigin = 0.0, lastContextBeat = 0.0;
    backScratchBridge::Queue automationEvents;
    std::atomic<unsigned> parameterGroupSequence { 0 };
    int parameterGroupDepth = 0;
    std::atomic<bool> resetPending { false };
    std::atomic<float> displayedPhase { 0.0f };
    std::atomic<bool> playingDisplay { false };
    std::atomic<double> tempoDisplay { 120.0 };
    std::atomic<double> barBeatsDisplay { 4.0 };
    // Only the audio thread publishes. Atomic fields and a sequence number let
    // the editor read a coherent audible frame without ever blocking audio.
    std::atomic<unsigned> displaySequence { 0 };
    std::array<std::atomic<float>, controlCount> audibleControls {};
    std::atomic<double> audibleBeat { 0.0 }, audibleBarBeats { 4.0 }, audibleTempo { 120.0 };
    std::atomic<bool> audiblePlaying { false }, displayReady { false };
    std::atomic<bool> audibleHistoryReady { true }, audibleScratchActive { false };
    std::atomic<bool> audibleEngaged { true }, audibleCycleActive { true };
    std::atomic<int> audibleSlot { 0 };
    std::array<std::atomic<float>, 2> peaks { 0.0f, 0.0f };
    std::atomic<int> historyModeSetting { 0 };
    std::atomic<float> editorScaleSetting { 0.0f };
    std::atomic<bool> gridSnap { false };
    std::atomic<int> bankSelection { -1 };
    juce::CriticalSection historyLock;
    mutable juce::CriticalSection nameLock;
    juce::String savedName;
    int namedFactory = -1;
    std::uint32_t namedSeed = 0;
    bool namedGenerated = false;
    bool namedEdited = false;
    int namedMotionVersion = 2;
    int namedDuration = 3;
    int namedTiming = 0, namedShift = 0;
    float namedSourceBeats = 4.0f, namedSourceBarBeats = 4.0f;
    juce::Random random;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackScratchProcessor)
};

} // namespace backScratch
