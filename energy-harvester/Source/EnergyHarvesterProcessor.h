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
    // Keeps the original 8.01 s floor; the cap stops absurd sync lengths (e.g. 4 bars of 7/4 at 10 BPM) reaching hosts.
    double getTailLengthSeconds() const override { return std::clamp(double(std::max(desiredTimeMs.load(),latchedTime.load()))*.001+.02,8.01,60.0); }
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
    // Animation feeds (audio thread writes once per block, relaxed). visualLevel holds the largest output
    // peak since the UI last consumed it with exchange(0); publishedSamples is engine.processedSamples().
    std::atomic<float> visualLevel{0}; std::atomic<uint64_t> publishedSamples{0};
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void process(juce::AudioBuffer<float>&,bool);
    void publish(bool,float peak=0) noexcept;
    energy::HarvestEngine engine;
    // resetPending: hard reset after a processing gap (prepare, host reset()). stateEpoch is odd while
    // setStateInformation is replacing parameters; the audio thread reads it seqlock-style around its raw reads.
    std::atomic<bool> resetPending{true};
    std::atomic<uint32_t> stateEpoch{0}; uint32_t seenEpoch_=0; bool recallUnsettled_=false;
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
