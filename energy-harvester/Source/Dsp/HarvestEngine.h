#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include "FxRack.h"

namespace energy {
enum class State : int { off, spinning, atSpeed, releasing };
struct Frame { float left{}, right{}; };

class HarvestEngine {
public:
    void prepare(double sampleRate, int channels);
    // Hard reset: clears history and voice. Use after a processing gap (prepare, host reset()).
    void reset(bool gate) noexcept;
    // Soft restart for transport start, seek, loop wrap and state recall while audio keeps running:
    // adopts the gate without an edge, fades any audible voice out over 5 ms and keeps the history.
    void restart(bool gate) noexcept;
    void setTime(float milliseconds) noexcept;
    void setFx(float percent) noexcept;
    void setOutputDb(float decibels) noexcept;
    void setEffects(unsigned mask) noexcept {effects_.setEffects(mask);}
    void setGate(bool on) noexcept;
    void setBypass(bool bypass) noexcept;
    Frame process(Frame input, bool bypass = false) noexcept;
    State state() const noexcept { return state_; }
    bool gate() const noexcept { return gate_; }
    float progress() const noexcept;
    float latchedMs() const noexcept { return latchedMs_; }
    std::uint64_t launches() const noexcept { return launches_; }
    std::uint64_t processedSamples() const noexcept { return samples_; }
    double rate() const noexcept { return fs_; }
private:
    struct Controls { double ratio=1, gain=1, cutoff=18000; };
    static constexpr int landingSteps=256;
    static double smooth(double x) noexcept { x=std::clamp(x,0.0,1.0); return x*x*(3-2*x); }
    Controls recipe() const noexcept;
    void launch() noexcept;
    void planLanding() noexcept;
    void fadeTo(double value, int samples) noexcept;
    void declick() noexcept;
    bool voiceActive() const noexcept { return state_==State::spinning || state_==State::releasing || tail_; }
    double read(int channel, double delay) const noexcept;
    void clearVoice() noexcept;
    double fs_=48000, phase_=0.25, fx_=0.45, fxTarget_=0.45, fxSmoothing_=1-std::exp(-1/480.0);
    double outputGain_=1,outputTarget_=1,outputSmoothing_=0;
    int channels_=2, write_=0, valid_=0, n_=38400, handoff_=1920, clock_=0;
    int fadeCount_=0, fadeLength_=1, controlCount_=0, controlLength_=1, declickLength_=240;
    int returnStart_=0, landStart_=0, landFade_=1;
    double wet_=0, fadeStart_=0, fadeTarget_=0;
    double landDelay_=0, landRamp_=0, landBase_=72, maxDelay_=0;
    float timeMs_=800, latchedMs_=800;
    bool gate_=false, bypassed_=false, tail_=false, landing_=false;
    State state_=State::off;
    Controls controls_, controlStart_;
    std::array<std::vector<float>,2> ring_;
    std::array<double,2> low_{};
    // remaining_[k]: normalised read-head delay growth still to come after position k/landingSteps.
    std::array<double,landingSteps+1> remaining_{};
    std::uint64_t launches_=0, samples_=0;
    FxRack effects_;
};
}
