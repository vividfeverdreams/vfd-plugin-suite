#include "HarvestEngine.h"

namespace energy {
namespace { constexpr double pi=3.14159265358979323846; }
void HarvestEngine::prepare(double rate, int channels) {
    fs_=std::clamp(rate,8000.0,384000.0); channels_=std::clamp(channels,1,2);
    for (auto& r:ring_) r.assign(static_cast<size_t>(std::ceil(fs_*.060))+8,0);
    effects_.prepare(fs_,channels_);
    reset(gate_); fx_=fxTarget_; outputGain_=outputTarget_;outputSmoothing_=1-std::exp(-1/(.010*fs_));samples_=0; launches_=0;
}
void HarvestEngine::clearVoice() noexcept {
    phase_=.25; rotorPhase_=0; low_={}; sub_={}; apInput_={}; apOutput_={};
    controls_={}; controlStart_={}; controlCount_=0;
    effects_.reset();
}
void HarvestEngine::reset(bool gate) noexcept {
    gate_=gate; state_=gate?State::atSpeed:State::off;
    wet_=fadeStart_=fadeTarget_=0; fadeCount_=0; clock_=0; write_=valid_=0;
    for(auto& r:ring_) std::fill(r.begin(),r.end(),0); clearVoice();
}
void HarvestEngine::setTime(float ms) noexcept { if(std::isfinite(ms)) timeMs_=std::clamp(ms,1.f,3600000.f); }
void HarvestEngine::setFx(float p) noexcept { if(std::isfinite(p)) fxTarget_=std::clamp(double(p)*.01,0.0,1.0); }
void HarvestEngine::setOutputDb(float db) noexcept {if(std::isfinite(db))outputTarget_=std::pow(10.,std::clamp(double(db),-18.,18.)*.05);}
void HarvestEngine::fadeTo(double v,int length) noexcept {
    fadeStart_=wet_; fadeTarget_=v; fadeLength_=std::max(1,length); fadeCount_=fadeLength_;
}
void HarvestEngine::launch() noexcept {
    const bool audible=wet_>0;
    if(!audible) clearVoice();
    latchedMs_=timeMs_; n_=std::max(1,int(std::llround(timeMs_*.001*fs_)));
    effects_.setDuration(timeMs_);
    handoff_=std::max(1,std::min(int(std::llround(.040*fs_)),int(std::llround(.1*n_))));
    clock_=0; state_=State::spinning; ++launches_;
    if(audible) { controlStart_=controls_; controlLength_=std::max(1,int(.005*fs_)); controlCount_=controlLength_; }
    else controls_=recipe();
    fadeTo(1,std::max(1,int(.005*fs_)));
}
void HarvestEngine::setGate(bool on) noexcept {
    if(on==gate_) return; gate_=on;
    if(bypassed_) { state_=on?State::atSpeed:State::off; return; }
    if(on) launch();
    else if(wet_>0) { state_=State::releasing; fadeTo(0,std::max(1,int(.010*fs_))); }
    else state_=State::off;
}
HarvestEngine::Controls HarvestEngine::recipe() const noexcept {
    const double s=smooth(double(clock_)/std::max(1,n_-handoff_)), e=1-s;
    return {std::pow(2.0,-2*e),.18+.82*s,std::min(.42*fs_,350*std::pow(18000.0/350,s)),
        .5*fx_*e,180*std::pow(20.0,s),2+18*s,.30*fx_*e};
}
double HarvestEngine::read(int channel,double delay) const noexcept {
    if(valid_<int(std::ceil(delay))+3) return 0;
    const auto& r=ring_[static_cast<size_t>(channel)]; const int size=int(r.size());
    double p=double(write_)-delay; while(p<0) p+=size;
    const int i=int(p); const double t=p-i;
    const auto at=[&](int v){return double(r[static_cast<size_t>((v+size)%size)]);};
    // Four-point Lagrange, x positions -1,0,1,2 relative to floor(p).
    return at(i-1)*(-t*(t-1)*(t-2)/6)+at(i)*((t+1)*(t-1)*(t-2)/2)
          +at(i+1)*(-(t+1)*t*(t-2)/2)+at(i+2)*((t+1)*t*(t-1)/6);
}
void HarvestEngine::setBypass(bool bypass) noexcept {
    if(bypass!=bypassed_) {reset(gate_);bypassed_=bypass;}
}
Frame HarvestEngine::process(Frame input,bool bypass) noexcept {
    ++samples_;
    if(ring_[0].empty()) return input;
    setBypass(bypass);
    ring_[0][static_cast<size_t>(write_)]=input.left;
    ring_[1][static_cast<size_t>(write_)]=channels_==1?input.left:input.right;
    valid_=std::min(valid_+1,int(ring_[0].size()));
    fx_ += (fxTarget_-fx_)* (1-std::exp(-1/(.010*fs_)));
    outputGain_+=(outputTarget_-outputGain_)*outputSmoothing_;
    Frame output=input;
    if(!bypass && (state_==State::spinning || state_==State::releasing)) {
        if(clock_>=n_) { wet_=0; state_=gate_?State::atSpeed:State::off; }
        else {
            auto target=recipe();
            if(controlCount_>0) {
                const double a=smooth(double(controlLength_-controlCount_)/controlLength_);
                auto lerp=[a](double start,double end){return start+(end-start)*a;};
                controls_={lerp(controlStart_.ratio,target.ratio),lerp(controlStart_.gain,target.gain),
                    lerp(controlStart_.cutoff,target.cutoff),lerp(controlStart_.mix,target.mix),
                    lerp(controlStart_.base,target.base),lerp(controlStart_.rotor,target.rotor),lerp(controlStart_.trem,target.trem)};
                --controlCount_;
            } else controls_=target;
            if(fadeCount_>0) {
                wet_=fadeStart_+(fadeTarget_-fadeStart_)*smooth(double(fadeLength_-fadeCount_)/fadeLength_);
                if(--fadeCount_==0) wet_=fadeTarget_;
            }
            phase_+= (1-controls_.ratio)/(.040*fs_); phase_-=std::floor(phase_);
            const double q1=phase_, q2=std::fmod(phase_+.5,1.0);
            const double w1=std::pow(std::sin(pi*q1),2), w2=1-w1;
            rotorPhase_=std::fmod(rotorPhase_+2*pi*controls_.rotor/fs_,2*pi);
            const double lp=1-std::exp(-2*pi*controls_.cutoff/fs_);
            double alpha=wet_*std::min(1.0,double(valid_)/(.044*fs_));
            if(clock_>=n_-handoff_) alpha*=1-smooth(double(clock_-(n_-handoff_))/handoff_);
            Frame coreFrame{};
            for(int ch=0;ch<channels_;++ch) {
                const auto c=size_t(ch);
                const double pitched=w1*read(ch,.004*fs_+.040*fs_*q1)+w2*read(ch,.004*fs_+.040*fs_*q2);
                low_[c]+=lp*(pitched-low_[c]); const double core=low_[c]*controls_.gain;
                if(ch==0)coreFrame.left=float(core);else coreFrame.right=float(core);
            }
            if(channels_==1)coreFrame.right=coreFrame.left;
            const auto effected=effects_.process(coreFrame,fx_);
            output.left=float(input.left+alpha*(effected.left*outputGain_-input.left));output.right=float(input.right+alpha*(effected.right*outputGain_-input.right));
            if(channels_==1) output.right=output.left;
            ++clock_;
            if(state_==State::releasing && fadeCount_==0 && wet_==0) state_=State::off;
        }
    }
    write_=(write_+1)%int(ring_[0].size()); return output;
}
float HarvestEngine::progress() const noexcept {
    if(state_==State::atSpeed) return 1;
    if(state_==State::off) return 0;
    return std::clamp(float(clock_)/float(n_),0.f,1.f);
}
}
