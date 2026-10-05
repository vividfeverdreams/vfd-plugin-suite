#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Dsp/HarvestEngine.h"
#include <atomic>

class EnergyHarvesterProcessor final : public juce::AudioProcessor {
public:
    EnergyHarvesterProcessor();
    const juce::String getName() const override { return "ENERGY HARVESTER"; }
    void prepareToPlay(double,int) override;
    void releaseResources() override {}
    void reset() override { resetPending.store(true); }
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return std::max(8.01, double(std::max(desiredTimeMs.load(),latchedTime.load()))*.001+.02); }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> visualState{0}; std::atomic<float> visualProgress{0}, latchedTime{800}, desiredTimeMs{800}, hostBpm{120};
    std::atomic<bool> hasHostTempo{false};
    std::atomic<bool> visualGate{false}, visualBypass{false}, reducedMotion{false};
    std::atomic<int> uiScale{100}; std::atomic<uint64_t> launchCount{0}, queuePoints{0};
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void process(juce::AudioBuffer<float>&,bool);
    void publish(bool) noexcept;
    energy::HarvestEngine engine;
    std::atomic<bool> resetPending{true};
    void configure() noexcept;
    inline static constexpr std::array<const char*,11> ids{"harvest","spinUpMs","fxAmount","fxPan","fxChorus","fxPhaser","fxFlanger","fxWashout","syncMode","syncDivision","outputDb"};
    inline static constexpr std::array<float,11> defaults{0,800,45,0,0,1,0,0,0,1,0};
    std::array<float,11> values_=defaults;
    std::array<juce::RangedAudioParameter*,11> parameters_{};
    std::array<std::atomic<float>*,11> raw_{};
    double bpm_=120; int numerator_=4, denominator_=4;
    bool gate_=false, wasPlaying_=false;
    bool hadTimeline_=false; juce::int64 nextPosition_=0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnergyHarvesterProcessor)
};
