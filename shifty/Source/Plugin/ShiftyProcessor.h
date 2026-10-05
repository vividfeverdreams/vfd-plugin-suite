#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include "../Dsp/FormantEngine.h"

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
    double getTailLengthSeconds() const override {
        return 2.0 * getLatencySamples() / (getSampleRate() > 0.0 ? getSampleRate() : 48000.0);
    }
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
    void saveUserPreset(const juce::String& name);
    bool loadUserPreset(const juce::File& file);
    juce::Array<juce::File> userPresetFiles() const;
    juce::File userPresetDirectory() const;
    void loadFactoryPreset(int index);
    static juce::StringArray factoryPresetNames();
    void randomizeParameters();
    bool isParameterLocked(const juce::String&) const;
    void setParameterLocked(const juce::String&, bool);
    static juce::StringArray randomizableParameterIds();
    void undo();
    void redo();
    bool canUndo() const { return undoManager.canUndo(); }
    bool canRedo() const { return undoManager.canRedo(); }

    /** Most recent Motion offset in semitones (last sample of the last block). */
    float currentMotionOffset() const noexcept { return lastMotionOffset.load(std::memory_order_relaxed); }

    /** Synced Motion phase, 0..1. Cycles restart at each host bar when the bar
        start is known; otherwise they follow the absolute ppq position. */
    static double syncedMotionPhase(double ppq, std::optional<double> barStartPpq,
                                    double barQuarters, double cycleQuarters) noexcept;
    static double barLengthInQuarters(int numerator, int denominator) noexcept;

    /** User presets live in <root>/Application Support/SHIFTY/Presets on macOS.
        Releases up to 0.4.2 used <root>/SHIFTY/Presets; its files are copied
        once, never moved, deleted or overwritten. root is ~/Library on macOS. */
    static juce::File presetDirectoryForRoot(const juce::File& userDataRoot);
    static juce::File legacyPresetDirectoryForRoot(const juce::File& userDataRoot);
    static int migrateLegacyPresets(const juce::File& legacyDirectory, const juce::File& newDirectory);
    /** Test hook: replaces ~/Library (the user data root) for preset storage. */
    void setUserDataRootForTesting(const juce::File& root);

private:
    struct ParameterSnapshot {
        std::array<float, 16> values {};
        juce::String presetName;
    };
    struct SnapshotAction;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void processAudio(juce::AudioBuffer<float>&, bool forceBypass) noexcept;
    void updateSpectrum(const juce::AudioBuffer<float>& buffer, bool output) noexcept;
    void syncPresetNameFromState();
    void assignPresetName(const juce::String& name);
    juce::File presetDirectory() const;
    void migrateLegacyPresetsOnce() const;
    void setParameterAsGesture(const char* id, float value);
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
    FormantEngine engine;
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
    static constexpr size_t parameterCount = 16;
    static constexpr uint32_t allRandomLocks = (1u << parameterCount) - 1u;
    std::array<std::atomic<float>*, parameterCount> paramValues {};
    double motionPhase = 0.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> motionDepth;
    std::atomic<float> lastMotionOffset { 0.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGain;
    int undoSuppression = 0;
    int activeGestures = 0;
    ParameterSnapshot gestureStart;
    ParameterSnapshot batchStart;
    juce::File userDataRootOverride;
    mutable bool presetMigrationChecked = false;
    static constexpr uint32_t defaultRandomLocks = (1u << 7) | (1u << 14) | (1u << 15);
    std::atomic<uint32_t> randomLocks { defaultRandomLocks };
    juce::Random variationRandom;
    mutable juce::CriticalSection presetNameMutex;
    juce::String presetName { "Rubber Throat" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShiftyProcessor)
};

} // namespace shifty
