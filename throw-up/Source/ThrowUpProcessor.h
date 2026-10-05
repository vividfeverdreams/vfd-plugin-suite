#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "ReverbEngine.h"
#include <array>
#include <atomic>

class ThrowUpProcessor final : public juce::AudioProcessor
{
public:
    ThrowUpProcessor();
    const juce::String getName() const override { return "THROW-UP"; }
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void reset() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 50; }
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram(int i) override { loadFactoryPreset(i); }
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    void loadFactoryPreset(int index);
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inputLevel { 0.0f }, outputLevel { 0.0f };
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout();
    throwup::Settings readSettings() const;
    void process(juce::AudioBuffer<float>&, bool forceBypass);
    throwup::ReverbEngine engine;
    std::array<std::atomic<float>*, 13> values {};
    std::atomic<int> currentProgram { 0 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThrowUpProcessor)
};
