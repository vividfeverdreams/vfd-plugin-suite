#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include "FxRack.h"

namespace energy {
// stopped: a Tape Stop or Brake has reached standstill and holds silence while HARVEST stays on.
enum class State : int { off, spinning, atSpeed, releasing, stopped };
// Mode and Landing values are the parameter choice indices (append-only).
enum class Mode : int { start, stop, brake, stopStart, rewind };
enum class Landing : int { clean, tail, hold };
enum class FilterMode : int { lowpass12, lowpass24, bandpass };
struct Frame { float left{}, right{}; };

class HarvestEngine {
public:
    void prepare(double sampleRate, int channels);
    // Hard reset: clears the voice and history validity. Use after a processing gap (prepare, host reset()).
    void reset(bool gate) noexcept;
    // Soft restart for transport start, seek, loop wrap and state recall while audio keeps running:
    // adopts the gate without an edge, fades any audible voice out over 5 ms and keeps the history.
    void restart(bool gate) noexcept;
    void setTime(float milliseconds) noexcept;
    void setFx(float percent) noexcept;
    void setOutputDb(float decibels) noexcept;
    void setEffects(unsigned mask) noexcept {effects_.setEffects(mask);}
    // false renders exactly like 0.3.2: constant effect mixes, full-band effects, two-head landing, boosting pan.
    void setLatestSound(bool latest) noexcept { latest_=latest; effects_.setLegacy(!latest); }
    // Latched at the next launch: mode, True Tape engine, curve (-1..1, 0 = smoothstep), start depth in semitones.
    void setMode(int mode) noexcept { modeTarget_=Mode(std::clamp(mode,0,4)); }
    void setTrueTape(bool on) noexcept { trueTapeTarget_=on; }
    void setCurve(float curve) noexcept { if(std::isfinite(curve)) curveTarget_=std::clamp(double(curve),-1.0,1.0); }
    void setDepth(float semitones) noexcept { if(std::isfinite(semitones)) depthTarget_=std::clamp(double(semitones),1.0,48.0); }
    void setLanding(int landing) noexcept { landingTarget_=Landing(std::clamp(landing,0,2)); }
    // Live controls. Resonance 0 is the original one-pole brightness sweep; above it a resonant SVF takes over.
    void setFilter(int mode, float resonance, float startHz) noexcept;
    void setSubKeep(float hz) noexcept;   // 0 = off; otherwise the band below hz stays dry
    void setMix(float fraction) noexcept { if(std::isfinite(fraction)) mixTarget_=std::clamp(double(fraction),0.0,1.0); }
    void setGate(bool on) noexcept;
    void setBypass(bool bypass) noexcept;
    Frame process(Frame input, bool bypass = false) noexcept;
    State state() const noexcept { return state_; }
    Mode mode() const noexcept { return mode_; }
    bool gate() const noexcept { return gate_; }
    float progress() const noexcept;
    // Playback ratio of the audible voice: 1 normal, 0 standstill, negative while rewinding.
    float speed() const noexcept;
    bool tailRinging() const noexcept { return tailGain_>0; }
    float latchedMs() const noexcept { return latchedMs_; }
    std::uint64_t launches() const noexcept { return launches_; }
    std::uint64_t processedSamples() const noexcept { return samples_; }
    double rate() const noexcept { return fs_; }
    static constexpr double historySeconds=16;
private:
    struct Controls { double ratio=1, gain=1, cutoff=18000; };
    struct Biquad { double b0=1,b1=0,b2=0,a1=0,a2=0;
        double run(double x,std::array<double,2>& z) const noexcept {const double y=b0*x+z[0];z[0]=b1*x-a1*y+z[1];z[1]=b2*x-a2*y;return y;} };
    static constexpr int landingSteps=256;
    static double smooth(double x) noexcept { x=std::clamp(x,0.0,1.0); return x*x*(3-2*x); }
    static Biquad butterworth(double rate,double hz,bool highpass) noexcept;
    double shape(double x) const noexcept;
    double warp(double x) const noexcept;
    bool lands() const noexcept { return mode_==Mode::start || mode_==Mode::stopStart || mode_==Mode::rewind; }
    bool usesDeck() const noexcept { return mode_!=Mode::start || trueTape_; }
    Controls recipe() const noexcept;
    Controls startCurve(double x) const noexcept;
    void launch() noexcept;
    void planLanding() noexcept;
    void fadeTo(double value, int samples) noexcept;
    void declick() noexcept;
    bool voiceActive() const noexcept { return state_==State::spinning || state_==State::releasing || state_==State::stopped || tail_ || holding_; }
    double read(int channel, double delay) const noexcept;      // 60 ms ring: two-head shifter and landing head
    double readDeck(int channel, double delay) const noexcept;  // long history: varispeed deck
    double filter(int channel, double x, double cutoff) noexcept;
    void clearVoice(bool keepEffects=false) noexcept;
    double fs_=48000, phase_=0.25, fx_=0.45, fxTarget_=0.45, fxSmoothing_=1-std::exp(-1/480.0);
    double outputGain_=1,outputTarget_=1,outputSmoothing_=0, mix_=1, mixTarget_=1;
    int channels_=2, write_=0, valid_=0, deckWrite_=0, deckValid_=0, n_=38400, handoff_=1920, clock_=0, split_=0;
    int fadeCount_=0, fadeLength_=1, controlCount_=0, controlLength_=1, declickLength_=240;
    int returnStart_=0, landStart_=0, landFade_=1, tailSilence_=0, tailAge_=0, launchClock_=0, launchRampLength_=720;
    double wet_=0, fadeStart_=0, fadeTarget_=0, tailGain_=0, tailStep_=0;
    double landDelay_=0, landRamp_=0, landBase_=72, maxDelay_=0, maxDeckDelay_=0, deckDelay_=2, deckSpeed_=1;
    double curve_=0, curveTarget_=0, curveExponent_=1, depthOctaves_=2, depthTarget_=24;
    double resonance_=0, resonanceTarget_=0, filterStart_=350, filterStartTarget_=350, subKeepHz_=0;
    float timeMs_=800, latchedMs_=800;
    bool gate_=false, bypassed_=false, tail_=false, landing_=false, latest_=true, holding_=false, past_=false, curved_=false;
    bool trueTape_=false, trueTapeTarget_=false, tailLanding_=false;
    State state_=State::off;
    Mode mode_=Mode::start, modeTarget_=Mode::start;
    Landing landingMode_=Landing::clean, landingTarget_=Landing::clean;
    FilterMode filterMode_=FilterMode::lowpass12;
    Controls controls_, controlStart_;
    std::array<std::vector<float>,2> ring_, history_;
    std::array<double,2> low_{};
    std::array<std::array<double,2>,2> svfA_{}, svfB_{};
    Biquad keepLow_, keepHigh_, deckHigh_;
    std::array<std::array<std::array<double,2>,2>,2> keepLowZ_{}, keepHighZ_{}, deckHighZ_{};
    // remaining_[k]: normalised read-head delay growth still to come after position k/landingSteps.
    std::array<double,landingSteps+1> remaining_{};
    std::uint64_t launches_=0, samples_=0;
    FxRack effects_;
};
}
