#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
#include "../Dsp/FormantEngine.h"

namespace shifty {

class ShiftyProcessor final : public juce::AudioProcessor {
public:
    ShiftyProcessor();
    ~ShiftyProcessor() override;

    const juce::String getName() const override { return "SHIFTY"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateSpectrum(const juce::AudioBuffer<float>& buffer, bool output) noexcept;
    void syncPresetNameFromState();
    juce::File presetDirectory() const;

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
    double freeMotionPhase = 0.0;
    double motionPhaseIncrement = 0.0;
    int motionSamplesUntilUpdate = 0;
    float heldMotionWave = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGain;
    static constexpr uint32_t defaultRandomLocks = (1u << 7) | (1u << 14) | (1u << 15);
    std::atomic<uint32_t> randomLocks { defaultRandomLocks };
    juce::Random variationRandom;
    mutable juce::CriticalSection presetNameMutex;
    juce::String presetName { "Rubber Throat" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShiftyProcessor)
};

} // namespace shifty
