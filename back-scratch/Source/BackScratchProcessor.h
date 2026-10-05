#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <array>
#include "ScratchPattern.h"
#include "LiveScratchEngine.h"
#include "EditedPatternStore.h"
#include "AutomationBridge.h"
#include <optional>

namespace backScratch {

class BackScratchProcessor final : public juce::AudioProcessor {
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
    };
    DisplayState displayState() const noexcept;
    bool isTransportPlaying() const noexcept { return playingDisplay.load(); }
    double currentTempoBpm() const noexcept { return tempoDisplay.load(); }
    float outputPeak(int channel) const noexcept { return peaks[static_cast<size_t>(std::clamp(channel, 0, 1))].load(); }
    void setHostParameter(const char* id, float actual);
    juce::Result savePreset(const juce::File&);
    juce::Result loadPreset(const juce::File&);
    // Seconds of past audio actually retained after prepareToPlay.
    double historySeconds() const noexcept { return engine.historySeconds(); }
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
    void writeParameterGesture(const char* id, float actual);
    void clearSavedName();
    void resetAfterRestore();
    juce::AudioProcessorValueTreeState state;
    LiveScratchEngine engine;
    ScratchPattern pattern;
    enum Control { factory, variation, sub, bypass, generated, seed, phraseLength, phraseLock, sourceBeats, sourceBarBeats, phraseTiming, patternShift, edited, motionVersion, controlCount };
    static ScratchPattern patternFor(const std::array<float, controlCount>&, double barBeats, const ScratchPattern* custom = nullptr) noexcept;
    struct ControlFrame {
        std::array<float, controlCount> values {};
        double beat = 0.0, samplesPerBeat = 24000.0, barBeats = 4.0;
        bool playing = true;
        int editSlot = EditedPatternStore::None;
        bool hostBypass = false;
    };
    std::vector<ControlFrame> controlHistory;
    size_t controlWrite = 0;
    int controlFrames = 0;
    std::array<float, controlCount> renderedControls {};
    double renderedBarBeats = 4.0;
    EditedPatternStore editedPatterns;
    int renderedEditSlot = EditedPatternStore::None;
    // The snapshot written into new control frames. It persists between
    // callbacks, so an unavailable or grouped publication keeps the last one.
    int inputEditSlot = EditedPatternStore::None;
    std::atomic<int> audibleEditSlot { EditedPatternStore::None };
    void clearControlHistory() noexcept;
    void clearEditHistory();
    struct EditSnapshot { ScratchPattern pattern; bool enabled = false; };
    EditSnapshot captureEditSnapshot() const;
    bool applyEditSnapshot(const EditSnapshot&);
    mutable juce::CriticalSection editLock;
    std::optional<EditSnapshot> editStart;
    int editStartSlot = EditedPatternStore::None;
    std::vector<EditSnapshot> editUndo, editRedo;
    bool editChanged = false;
    struct TimedControl { int offset = 0; int control = 0; float value = 0.0f; int order = 0; };
    std::array<juce::RangedAudioParameter*, controlCount> controlParameters {};
    std::array<std::atomic<float>*, controlCount> rawControls {};
    std::array<float, controlCount> controls {}, lastRaw {};
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
    std::array<std::atomic<float>, 2> peaks { 0.0f, 0.0f };
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
