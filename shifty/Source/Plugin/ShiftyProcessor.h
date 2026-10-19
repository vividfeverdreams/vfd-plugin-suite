#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include "../Dsp/FormantEngine.h"
#include "../Dsp/Modulators.h"
#include "../Dsp/MouthEngine.h"
#include "../Dsp/TalkEngine.h"
#include "FactoryPresets.h"

namespace shifty {

class ShiftyProcessor final : public juce::AudioProcessor,
                              private juce::AudioProcessorParameter::Listener {
public:
    ShiftyProcessor();
    ~ShiftyProcessor() override;

    const juce::String getName() const override { return "SHIFTY"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // Host (VST3 kIsBypass) bypass runs the normal engine with SHIFTY's own
    // bypass forced on, so the dry signal keeps the reported latency and no
    // stale audio is left in the engine when the host re-enables it. The host
    // bypass parameter itself stays the wrapper's, keeping the 0.4.2 list.
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Current"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& parameters() noexcept { return state; }
    const juce::AudioProcessorValueTreeState& parameters() const noexcept { return state; }
    float inputPeak(int channel) const noexcept { return inputPeaks[(size_t) juce::jlimit(0, 1, channel)].load(); }
    float outputPeak(int channel) const noexcept { return outputPeaks[(size_t) juce::jlimit(0, 1, channel)].load(); }
    void copySpectrum(std::array<float, 64>& input, std::array<float, 64>& output) const noexcept;
    juce::String currentPresetName() const;
    void setPresetName(const juce::String& name);

    // Engines. Change them on the message thread: latency follows the engine.
    EngineMode engineMode() const noexcept { return static_cast<EngineMode>(engineModeValue.load()); }
    bool classicLowLatency() const noexcept { return lowLatencyValue.load(); }
    void setEngine(EngineMode, bool lowLatency);
    bool isOriginalSound() const noexcept { return engineMode() == EngineMode::original; }
    /** Upgrades a project or preset made with 0.4.2 to the fixed CLASSIC engine (undoable). */
    void useLatestSound();
    static int latencyFor(EngineMode, bool lowLatency, double sampleRate) noexcept;

    // Presets
    void saveUserPreset(const juce::String& name, const juce::String& folder = {});
    bool loadUserPreset(const juce::File& file);
    juce::Array<juce::File> userPresetFiles() const;
    juce::StringArray userPresetFolders() const;
    juce::File userPresetDirectory() const;
    void loadFactoryPreset(int index);
    static juce::StringArray factoryPresetNames();
    static const std::vector<FactoryPreset>& factoryPresets() { return factoryBank(); }
    static int factoryPresetIndex(const juce::String& folder, const juce::String& name);
    juce::String currentPresetFolder() const;

    void randomizeParameters();
    bool isParameterLocked(const juce::String&) const;
    void setParameterLocked(const juce::String&, bool);
    static juce::StringArray randomizableParameterIds();
    /** Lock ID for the engine (TALK / MOUTH / CLASSIC). Not a parameter, but
        RANDOM picks it like one unless it is locked. Unlocked by default. */
    static constexpr const char* engineLockId = "engine";
    void undo();
    void redo();
    bool canUndo() const { return undoManager.canUndo(); }
    bool canRedo() const { return undoManager.canRedo(); }

    // Editor size, saved with the project (not a parameter).
    double editorScale() const noexcept { return editorScaleValue.load(); }
    void setEditorScale(double scale);

    /** Live values for the screens. Written on the audio thread. */
    struct Display {
        EngineMode engine = EngineMode::talk;
        bool envelopeValid = false;
        std::array<float, displayPoints> detected {}, moved {};
        TalkEngine::Display talk;
        float pitch = 0.0f, confidence = 0.0f;
        float shift = 0.0f;     // SHIFT including Motion/ENV/YOI
        float morph = 2.0f;     // TALK vowel including modulation
        float lfoPhase = 0.0f;  // 0..1
        float lfoValue = 0.0f;  // -1..1, before depth
        float envelope = 0.0f;  // 0..1
        float yoi = 1.0f;       // sweep position 0..1
        bool motionRunning = false;
    };
    Display display() const noexcept;

    /** Most recent Motion offset in semitones (last sample of the last block). */
    float currentMotionOffset() const noexcept { return lastMotionOffset.load(std::memory_order_relaxed); }

    /** Synced Motion phase, 0..1. Cycles restart at each host bar when the bar
        start is known; otherwise they follow the absolute ppq position. */
    static double syncedMotionPhase(double ppq, std::optional<double> barStartPpq,
                                    double barQuarters, double cycleQuarters) noexcept;
    static double barLengthInQuarters(int numerator, int denominator) noexcept;

    /** User presets live in <root>/Application Support/SHIFTY/Presets[/<Folder>] on
        macOS. Releases up to 0.4.2 used <root>/SHIFTY/Presets; its files are copied
        once, never moved, deleted or overwritten. root is ~/Library on macOS. Elsewhere
        they live in <root>/SHIFTY/Presets: %APPDATA% on Windows, ~/.config on Linux. */
    static juce::File presetDirectoryForRoot(const juce::File& userDataRoot);
    static juce::File legacyPresetDirectoryForRoot(const juce::File& userDataRoot);
    static int migrateLegacyPresets(const juce::File& legacyDirectory, const juce::File& newDirectory);
    /** Test hook: replaces ~/Library (the user data root) for preset storage. */
    void setUserDataRootForTesting(const juce::File& root);

    static constexpr size_t parameterCount = 29;

private:
    struct ParameterSnapshot {
        std::array<float, parameterCount> values {};
        juce::String presetName, presetFolder;
        int engine = static_cast<int>(EngineMode::talk);
        bool lowLatency = false;
    };
    struct SnapshotAction;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void processAudio(juce::AudioBuffer<float>&, bool forceBypass) noexcept;
    void processOriginal(juce::AudioBuffer<float>&, int channels, bool bypassed) noexcept;
    void processLatest(juce::AudioBuffer<float>&, int channels, bool bypassed, EngineMode, bool lowLatency) noexcept;
    void publishDisplay(EngineMode, bool lowLatency) noexcept;
    void updateSpectrum(const juce::AudioBuffer<float>& buffer, bool output) noexcept;
    void syncPresetNameFromState();
    void assignPresetName(const juce::String& name, const juce::String& folder = {});
    juce::File presetDirectory() const;
    void migrateLegacyPresetsOnce() const;
    void setParameterAsGesture(const char* id, float value);
    void applyFactoryPreset(const FactoryPreset&);
    void applyEngine(EngineMode, bool lowLatency);
    ParameterSnapshot captureSnapshot() const;
    void applySnapshot(const ParameterSnapshot& target, const ParameterSnapshot& other);
    void beginUndoableEdit();
    void endUndoableEdit(const juce::String& name);
    void recordUndoStep(const ParameterSnapshot& before, const ParameterSnapshot& after,
                        const juce::String& name);
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;

    // Undo is built from UI gestures, Random and preset loads only. The APVTS
    // is not given this UndoManager, so host automation never enters it.
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState state;
    FormantEngine originalEngine, classicEngine, classicLowLatencyEngine;
    TalkEngine talkEngine;
    MouthEngine mouthEngine;
    EnvelopeFollower envelopeFollower;
    YoiSweep yoiSweep;
    std::atomic<int> engineModeValue { static_cast<int>(EngineMode::talk) };
    std::atomic<bool> lowLatencyValue { false };
    int activeEngine = -1;       // audio thread: engine whose state is live (mode * 2 + low latency)
    int switchFadeOut = 0;       // audio thread: samples left fading the old engine out
    float switchGain = 1.0f;     // audio thread: fade-in after a switch
    float switchStep = 0.0f;

    std::array<std::atomic<float>, 2> inputPeaks {{0.0f, 0.0f}};
    std::array<std::atomic<float>, 2> outputPeaks {{0.0f, 0.0f}};
    std::array<std::atomic<float>, 64> inputSpectrum {};
    std::array<std::atomic<float>, 64> outputSpectrum {};
    std::array<float, 1024> analyzerInput {};
    std::array<float, 1024> analyzerOutput {};
    int analyzerInputWrite = 0;
    int analyzerOutputWrite = 0;
    std::unique_ptr<juce::dsp::FFT> analyzerFft;
    std::array<float, 2048> fftScratch {};
    // One lock bit per parameter, then the engine's.
    static constexpr uint32_t engineLockBit = 1u << parameterCount;
    static constexpr uint32_t allRandomLocks = (engineLockBit << 1) - 1u;
    std::array<std::atomic<float>*, parameterCount> paramValues {};

    // Motion: latest engines
    double motionPhase = 0.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> motionDepth;
    std::atomic<float> lastMotionOffset { 0.0f };
    // Motion: the 0.4.2 path (64-sample sample-and-hold through the SHIFT smoother)
    double originalMotionPhase = 0.0;
    double originalPhaseIncrement = 0.0;
    int originalSamplesUntilUpdate = 0;
    float originalHeldWave = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGain;

    // Live display values
    std::atomic<int> displayEngine { 2 };
    std::atomic<bool> displayEnvelopeValid { false };
    std::array<std::atomic<float>, displayPoints> displayDetected {}, displayMoved {};
    std::array<std::atomic<float>, 5> displayFormantFrequency {}, displayFormantBandwidth {}, displayFormantGain {};
    std::atomic<float> displayBody { 0.2f }, displayPitch { 0.0f }, displayConfidence { 0.0f };
    std::atomic<float> displayShift { 0.0f }, displayMorph { 2.0f }, displayLfoPhase { 0.0f }, displayLfoValue { 0.0f };
    std::atomic<float> displayEnvelopeLevel { 0.0f }, displayYoi { 1.0f };
    std::atomic<bool> displayMotionRunning { false };

    int undoSuppression = 0;
    int activeGestures = 0;
    ParameterSnapshot gestureStart;
    ParameterSnapshot batchStart;
    juce::File userDataRootOverride;
    mutable bool presetMigrationChecked = false;
    // Output and Bypass. Before 0.5.3 Vowel Color was locked too, and the
    // engine was never randomized.
    static constexpr uint32_t defaultRandomLocks = (1u << 7) | (1u << 14);
    static constexpr uint32_t legacyDefaultRandomLocks = defaultRandomLocks | (1u << 15);
    std::atomic<uint32_t> randomLocks { defaultRandomLocks };
    std::atomic<double> editorScaleValue { 1.0 };
    juce::Random variationRandom;
    mutable juce::CriticalSection presetNameMutex;
    juce::String presetName { "Rubber Throat" };
    juce::String presetFolder { "Talk" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShiftyProcessor)
};

} // namespace shifty
