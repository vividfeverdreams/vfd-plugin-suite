#include "HarvestEngine.h"

namespace energy {
namespace { constexpr double pi=3.14159265358979323846; }
void HarvestEngine::prepare(double rate, int channels) {
    fs_=std::clamp(rate,8000.0,384000.0); channels_=std::clamp(channels,1,2);
    for (auto& r:ring_) r.assign(static_cast<size_t>(std::ceil(fs_*.060))+8,0);
    effects_.prepare(fs_,channels_);
    fxSmoothing_=1-std::exp(-1/(.010*fs_));
    declickLength_=std::max(1,int(std::llround(.005*fs_)));
    landBase_=std::max(3.0,.0015*fs_);
    maxDelay_=double(ring_[0].size())-4;
    // Integrate the read-head delay growth (1 - ratio) of the fixed spin curve once, from the end backwards.
    const auto growth=[](double x){return 1-std::pow(2.0,-2*(1-smooth(x)));};
    remaining_[landingSteps]=0;
    for(int k=landingSteps-1;k>=0;--k) {
        constexpr int sub=16; double area=0;
        for(int j=0;j<sub;++j) {
            const double a=(k+double(j)/sub)/landingSteps, b=(k+double(j+1)/sub)/landingSteps;
            area+=.5*(growth(a)+growth(b))*(b-a);
        }
        remaining_[size_t(k)]=remaining_[size_t(k+1)]+area;
    }
    reset(gate_); fx_=fxTarget_; outputGain_=outputTarget_;outputSmoothing_=1-std::exp(-1/(.010*fs_));samples_=0; launches_=0;
}
void HarvestEngine::clearVoice() noexcept {
    phase_=.25; low_={}; controls_={}; controlStart_={}; controlCount_=0;
    landing_=false; landRamp_=0; landDelay_=0;
    effects_.reset();
}
void HarvestEngine::reset(bool gate) noexcept {
    gate_=gate; state_=gate?State::atSpeed:State::off; tail_=false;
    wet_=fadeStart_=fadeTarget_=0; fadeCount_=0; clock_=0; write_=valid_=0;
    for(auto& r:ring_) std::fill(r.begin(),r.end(),0); clearVoice();
}
void HarvestEngine::declick() noexcept {
    // The logical state moves on immediately; an audible voice keeps running as a short fading tail.
    if(voiceActive() && wet_>0) {
        tail_=true;
        if(!(fadeCount_>0 && fadeTarget_<=0 && fadeCount_<=declickLength_)) fadeTo(0,declickLength_);
    } else { tail_=false; wet_=0; fadeCount_=0; }
}
void HarvestEngine::restart(bool gate) noexcept {
    declick(); gate_=gate; state_=gate?State::atSpeed:State::off;
}
void HarvestEngine::setTime(float ms) noexcept { if(std::isfinite(ms)) timeMs_=std::clamp(ms,1.f,3600000.f); }
void HarvestEngine::setFx(float p) noexcept { if(std::isfinite(p)) fxTarget_=std::clamp(double(p)*.01,0.0,1.0); }
void HarvestEngine::setOutputDb(float db) noexcept {if(std::isfinite(db))outputTarget_=std::pow(10.,std::clamp(double(db),-18.,18.)*.05);}
void HarvestEngine::fadeTo(double v,int length) noexcept {
    fadeStart_=wet_; fadeTarget_=v; fadeLength_=std::max(1,length); fadeCount_=fadeLength_;
}
void HarvestEngine::planLanding() noexcept {
    // Hand the voice to one short-delay head once at most ~1 ms of read-head delay growth is still to come
    // (never before 75 % of the climb), so the landing is a single tap instead of a two-head comb and flam.
    const int span=std::max(1,n_-handoff_);
    const double limit=.001*fs_/span;
    int k=landingSteps;
    while(k>0 && remaining_[size_t(k-1)]<=limit) --k;
    landStart_=std::min(span,int(std::floor(std::max(.75,double(k)/landingSteps)*span)));
    landFade_=std::max(1,std::min(int(std::llround(.030*fs_)),(span-landStart_)/2));
}
void HarvestEngine::launch() noexcept {
    const bool audible=wet_>0;
    if(!audible) clearVoice();
    latchedMs_=timeMs_; n_=std::max(1,int(std::llround(timeMs_*.001*fs_)));
    effects_.setDuration(timeMs_);
    handoff_=std::max(1,std::min(int(std::llround(.040*fs_)),int(std::llround(.1*n_))));
    const int returnLength=std::min(n_,std::max(int(std::llround(.25*n_)),int(std::llround(.300*fs_))));
    returnStart_=n_-returnLength;
    planLanding();
    clock_=0; state_=State::spinning; tail_=false; ++launches_;
    if(audible) { controlStart_=controls_; controlLength_=std::max(1,int(.005*fs_)); controlCount_=controlLength_; }
    else controls_=recipe();
    fadeTo(1,std::max(1,int(.005*fs_)));
}
void HarvestEngine::setGate(bool on) noexcept {
    if(on==gate_) return; gate_=on;
    if(bypassed_) { state_=on?State::atSpeed:State::off; return; }
    if(on) launch();
    else if(voiceActive() && wet_>0) {
        tail_=false; state_=State::releasing;
        if(!(fadeCount_>0 && fadeTarget_<=0)) fadeTo(0,std::max(1,int(.010*fs_)));
    }
    else { tail_=false; state_=State::off; }
}
HarvestEngine::Controls HarvestEngine::recipe() const noexcept {
    const double s=smooth(double(clock_)/std::max(1,n_-handoff_)), e=1-s;
    return {std::pow(2.0,-2*e),.18+.82*s,std::min(.42*fs_,350*std::pow(18000.0/350,s))};
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
    if(bypass==bypassed_) return;
    bypassed_=bypass;
    // Entering host bypass fades an audible spin over 5 ms; leaving it keeps the (still current) history.
    if(bypass) { declick(); state_=gate_?State::atSpeed:State::off; }
}
Frame HarvestEngine::process(Frame input,bool bypass) noexcept {
    ++samples_;
    if(ring_[0].empty()) return input;
    setBypass(bypass);
    // Non-finite host samples never enter the history; the idle path still passes the host's sample through.
    const float left=std::isfinite(input.left)?input.left:0.f;
    const float right=channels_==1?left:(std::isfinite(input.right)?input.right:0.f);
    ring_[0][static_cast<size_t>(write_)]=left;
    ring_[1][static_cast<size_t>(write_)]=right;
    valid_=std::min(valid_+1,int(ring_[0].size()));
    fx_+=(fxTarget_-fx_)*fxSmoothing_;
    outputGain_+=(outputTarget_-outputGain_)*outputSmoothing_;
    Frame output=input;
    if(voiceActive()) {
        if(clock_>=n_) {
            wet_=0; fadeCount_=0; tail_=false; landing_=false; landRamp_=0;
            if(state_==State::spinning || state_==State::releasing) state_=gate_?State::atSpeed:State::off;
        }
        else {
            auto target=recipe();
            if(controlCount_>0) {
                const double a=smooth(double(controlLength_-controlCount_)/controlLength_);
                auto lerp=[a](double start,double end){return start+(end-start)*a;};
                controls_={lerp(controlStart_.ratio,target.ratio),lerp(controlStart_.gain,target.gain),lerp(controlStart_.cutoff,target.cutoff)};
                --controlCount_;
            } else controls_=target;
            if(fadeCount_>0) {
                wet_=fadeStart_+(fadeTarget_-fadeStart_)*smooth(double(fadeLength_-fadeCount_)/fadeLength_);
                if(--fadeCount_==0) wet_=fadeTarget_;
            }
            phase_+= (1-controls_.ratio)/(.040*fs_); phase_-=std::floor(phase_);
            const double q1=phase_, q2=std::fmod(phase_+.5,1.0);
            const double w1=std::pow(std::sin(pi*q1),2), w2=1-w1;
            // Landing head: a single read pointer that starts 1.5 ms behind the input and moves at the same
            // speed as the two-head voice; it crossfades in near the end and back out on an audible relaunch.
            const bool wantLanding=clock_>=landStart_;
            if(wantLanding && !landing_) { landing_=true; landDelay_=landBase_; }
            if(landing_) {
                landDelay_=std::min(landDelay_+(1-controls_.ratio),maxDelay_);
                landRamp_=wantLanding?std::min(1.0,landRamp_+1.0/landFade_):std::max(0.0,landRamp_-1.0/declickLength_);
                if(!wantLanding && landRamp_<=0) landing_=false;
            }
            const double land=smooth(landRamp_);
            const double lp=1-std::exp(-2*pi*controls_.cutoff/fs_);
            double alpha=wet_*std::min(1.0,double(valid_)/(.044*fs_));
            if(clock_>=n_-handoff_) alpha*=1-smooth(double(clock_-(n_-handoff_))/handoff_);
            Frame coreFrame{};
            for(int ch=0;ch<channels_;++ch) {
                const auto c=size_t(ch);
                double pitched=0;
                if(land<1) pitched=(1-land)*(w1*read(ch,.004*fs_+.040*fs_*q1)+w2*read(ch,.004*fs_+.040*fs_*q2));
                if(land>0) pitched+=land*read(ch,landDelay_);
                low_[c]+=lp*(pitched-low_[c]); const double core=low_[c]*controls_.gain;
                if(ch==0)coreFrame.left=float(core);else coreFrame.right=float(core);
            }
            if(channels_==1)coreFrame.right=coreFrame.left;
            // Every effect recedes over the last 40 % of the climb; Washout's hall return reaches 0 at the deadline.
            const double position=double(clock_)/std::max(1,n_-handoff_);
            const double recede=1-smooth((position-.6)/.4);
            const double washReturn=clock_<returnStart_?1.0:1-smooth(double(clock_-returnStart_)/std::max(1,n_-returnStart_));
            const auto effected=effects_.process(coreFrame,fx_,recede,washReturn);
            output.left=float(left+alpha*(effected.left*outputGain_-left));output.right=float(right+alpha*(effected.right*outputGain_-right));
            if(channels_==1) output.right=output.left;
            ++clock_;
            if(fadeCount_==0 && wet_<=0 && (state_==State::releasing || tail_)) {
                tail_=false; if(state_==State::releasing) state_=State::off;
            }
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
