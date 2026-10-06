#include "HarvestEngine.h"
#include <limits>

namespace energy {
namespace { constexpr double pi=3.14159265358979323846; }
HarvestEngine::Biquad HarvestEngine::butterworth(double rate,double hz,bool highpass) noexcept {
    const double w=2*pi*hz/rate,cw=std::cos(w),alpha=std::sin(w)/(2*0.70710678118654752),a0=1+alpha;Biquad q;
    if(highpass){q.b0=(1+cw)*.5/a0;q.b1=-(1+cw)/a0;}else{q.b0=(1-cw)*.5/a0;q.b1=(1-cw)/a0;}
    q.b2=q.b0;q.a1=-2*cw/a0;q.a2=(1-alpha)/a0;return q;
}
void HarvestEngine::prepare(double rate, int channels) {
    fs_=std::clamp(rate,8000.0,384000.0); channels_=std::clamp(channels,1,2);
    // The two-head shifter keeps the original 60 ms ring (identical index arithmetic, so 0.3.2 projects render
    // bit for bit); the varispeed deck reads a separate long history.
    for (auto& r:ring_) r.assign(static_cast<size_t>(std::ceil(fs_*.060))+8,0);
    const size_t size=size_t(std::min(std::ceil(fs_*historySeconds)+8,double(1<<22)));
    for (auto& h:history_) h.assign(size,0);
    effects_.prepare(fs_,channels_); effects_.setLegacy(!latest_);
    fxSmoothing_=1-std::exp(-1/(.010*fs_));
    declickLength_=std::max(1,int(std::llround(.005*fs_)));
    landBase_=std::max(3.0,.0015*fs_); launchRampLength_=std::max(1,int(std::llround(.015*fs_)));
    deckHigh_=butterworth(fs_,20,true);
    maxDelay_=double(ring_[0].size())-4; maxDeckDelay_=double(size)-4;
    // Integrate the read-head delay growth (1 - ratio) of the default spin curve once, from the end backwards.
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
    const double keep=subKeepHz_; subKeepHz_=-1; setSubKeep(float(keep));
    reset(gate_); fx_=fxTarget_; outputGain_=outputTarget_;outputSmoothing_=1-std::exp(-1/(.010*fs_));samples_=0; launches_=0;
    mix_=mixTarget_; resonance_=resonanceTarget_; filterStart_=filterStartTarget_;
}
void HarvestEngine::clearVoice(bool keepEffects) noexcept {
    phase_=.25; low_={}; svfA_={}; svfB_={}; controls_={}; controlStart_={}; controlCount_=0; launchClock_=0;
    landing_=false; landRamp_=0; landDelay_=0; deckDelay_=2; deckSpeed_=1; holding_=false; past_=false;
    if(!keepEffects) { effects_.reset(); tailGain_=0; tailStep_=0; }
}
void HarvestEngine::reset(bool gate) noexcept {
    gate_=gate; state_=gate?State::atSpeed:State::off; tail_=false;
    wet_=fadeStart_=fadeTarget_=0; fadeCount_=0; clock_=0; write_=valid_=0; deckWrite_=deckValid_=0; keepLowZ_={}; keepHighZ_={}; deckHighZ_={};
    clearVoice();
}
void HarvestEngine::declick() noexcept {
    // The logical state moves on immediately; an audible voice keeps running as a short fading tail.
    if(voiceActive() && wet_>0) {
        tail_=true; holding_=false;
        if(!(fadeCount_>0 && fadeTarget_<=0 && fadeCount_<=declickLength_)) fadeTo(0,declickLength_);
    } else { tail_=false; holding_=false; wet_=0; fadeCount_=0; }
    if(tailGain_>0) tailStep_=-std::max(tailGain_/declickLength_,1e-9);
}
void HarvestEngine::restart(bool gate) noexcept {
    declick(); gate_=gate; state_=gate?State::atSpeed:State::off;
}
void HarvestEngine::setTime(float ms) noexcept { if(std::isfinite(ms)) timeMs_=std::clamp(ms,1.f,3600000.f); }
void HarvestEngine::setFx(float p) noexcept { if(std::isfinite(p)) fxTarget_=std::clamp(double(p)*.01,0.0,1.0); }
void HarvestEngine::setOutputDb(float db) noexcept {if(std::isfinite(db))outputTarget_=std::pow(10.,std::clamp(double(db),-18.,18.)*.05);}
void HarvestEngine::setFilter(int mode, float resonance, float startHz) noexcept {
    filterMode_=FilterMode(std::clamp(mode,0,2));
    if(std::isfinite(resonance)) resonanceTarget_=std::clamp(double(resonance),0.0,1.0);
    if(std::isfinite(startHz)) filterStartTarget_=std::clamp(double(startHz),20.0,8000.0);
}
void HarvestEngine::setSubKeep(float hz) noexcept {
    const double value=std::isfinite(hz)&&hz>0?std::clamp(double(hz),20.0,500.0):0.0;
    if(std::abs(value-subKeepHz_)<1e-9) return;
    subKeepHz_=value;
    if(value>0) { keepLow_=butterworth(fs_,value,false); keepHigh_=butterworth(fs_,value,true); }
}
void HarvestEngine::fadeTo(double v,int length) noexcept {
    fadeStart_=wet_; fadeTarget_=v; fadeLength_=std::max(1,length); fadeCount_=fadeLength_;
}
double HarvestEngine::shape(double x) const noexcept {
    if(!curved_) return smooth(x);
    return smooth(std::pow(std::clamp(x,0.0,1.0),curveExponent_));
}
double HarvestEngine::warp(double x) const noexcept {
    x=std::clamp(x,0.0,1.0);
    return curved_?std::pow(x,curveExponent_):x;
}
void HarvestEngine::planLanding() noexcept {
    // Hand the voice to one short-delay head once at most ~1 ms of read-head delay growth is still to come
    // (never before 75 % of the climb), so the landing is a single tap instead of a two-head comb and flam.
    if(!latest_ || mode_!=Mode::start || trueTape_ || curved_ || std::abs(depthOctaves_-2)>1e-9) {
        // The growth table assumes the default curve; other curves start the landing head at 85 %.
        landStart_=latest_&&mode_==Mode::start&&!trueTape_?int(.85*std::max(1,n_-handoff_)):std::numeric_limits<int>::max();
        landFade_=std::max(1,std::min(int(std::llround(.030*fs_)),(std::max(1,n_-handoff_)-std::min(landStart_,n_))/2));
        return;
    }
    const int span=std::max(1,n_-handoff_);
    const double limit=.001*fs_/span;
    int k=landingSteps;
    while(k>0 && remaining_[size_t(k-1)]<=limit) --k;
    landStart_=std::min(span,int(std::floor(std::max(.75,double(k)/landingSteps)*span)));
    landFade_=std::max(1,std::min(int(std::llround(.030*fs_)),(span-landStart_)/2));
}
void HarvestEngine::launch() noexcept {
    const bool audible=wet_>0;
    // An audible relaunch keeps its read method (shifter or deck) so the voice never jumps.
    if(!audible) {
        clearVoice(tailGain_>0);
        mode_=modeTarget_; trueTape_=trueTapeTarget_;
    }
    // A spin launched while a Tail still rings keeps the hall outside the crossfade and lands with Tail too.
    landingMode_=landingTarget_==Landing::clean&&tailGain_>0?Landing::tail:landingTarget_; tailLanding_=landingMode_==Landing::tail;
    curve_=curveTarget_; curved_=std::abs(curve_)>1e-9; curveExponent_=std::pow(2.0,2*curve_); depthOctaves_=depthTarget_/12;
    latchedMs_=timeMs_; n_=std::max(1,int(std::llround(timeMs_*.001*fs_)));
    effects_.setDuration(timeMs_);
    handoff_=std::max(1,std::min(int(std::llround(.040*fs_)),int(std::llround(.1*n_))));
    if(mode_==Mode::rewind) handoff_=std::max(1,std::min(handoff_,int(std::llround(.008*fs_))));
    split_=n_/2;
    const int returnLength=std::min(n_,std::max(int(std::llround(.25*n_)),int(std::llround(.300*fs_))));
    returnStart_=n_-returnLength;
    planLanding();
    clock_=0; state_=State::spinning; tail_=false; holding_=false; past_=false; ++launches_;
    if(audible) { controlStart_=controls_; controlLength_=std::max(1,int(.005*fs_)); controlCount_=controlLength_; }
    else { controls_=recipe(); launchClock_=0; }
    // Deck modes begin at full level and bandwidth, so they cross over from the dry input over 15 ms.
    fadeTo(1,std::max(1,int((!audible&&usesDeck()?.015:.005)*fs_)));
}
void HarvestEngine::setGate(bool on) noexcept {
    if(on==gate_) return; gate_=on;
    if(bypassed_) { state_=on?State::atSpeed:State::off; return; }
    if(on) launch();
    else if(voiceActive() && wet_>0) {
        tail_=false; holding_=false; state_=State::releasing;
        if(!(fadeCount_>0 && fadeTarget_<=0)) fadeTo(0,std::max(1,int(.010*fs_)));
    }
    else { tail_=false; holding_=false; state_=State::off; }
}
HarvestEngine::Controls HarvestEngine::startCurve(double x) const noexcept {
    const double s=shape(x), e=1-s;
    return {std::pow(2.0,-depthOctaves_*e),.18+.82*s,std::min(.42*fs_,filterStart_*std::pow(18000.0/filterStart_,s))};
}
HarvestEngine::Controls HarvestEngine::recipe() const noexcept {
    const int clock=std::min(clock_,n_);
    switch(mode_) {
        case Mode::start: return startCurve(double(clock)/std::max(1,n_-handoff_));
        case Mode::stop: case Mode::brake: {
            // Tape power-down: the motor fades along the curve. Brake: constant deceleration, brighter and louder.
            const double x=double(clock)/std::max(1,n_);
            const double v=std::max(0.0,mode_==Mode::stop?1-shape(x):1-warp(x));
            return {v,mode_==Mode::stop?std::pow(v,.6):std::sqrt(v),std::min(.42*fs_,filterStart_*std::pow(18000.0/filterStart_,mode_==Mode::stop?v:std::sqrt(v)))};
        }
        case Mode::stopStart: {
            if(clock<split_) {
                const double v=std::max(0.0,1-shape(double(clock)/std::max(1,split_)));
                return {v,std::pow(v,.6),std::min(.42*fs_,filterStart_*std::pow(18000.0/filterStart_,v))};
            }
            return startCurve(double(clock-split_)/std::max(1,n_-split_-handoff_));
        }
        case Mode::rewind: {
            // Forward speed brakes through standstill into a backspin three times normal speed.
            const double v=1-4*shape(double(clock)/std::max(1,n_-handoff_)), a=std::min(1.0,std::abs(v));
            return {v,.35+.65*a,std::min(.42*fs_/std::max(1.0,std::abs(v)),filterStart_*std::pow(18000.0/filterStart_,a))};
        }
    }
    return {};
}
double HarvestEngine::readDeck(int channel,double delay) const noexcept {
    if(deckValid_<int(std::ceil(delay))+3) return 0;
    const auto& r=history_[static_cast<size_t>(channel)]; const int size=int(r.size());
    // Split the delay so the fractional position keeps full precision deep into the long history.
    const double whole=std::floor(delay),fraction=delay-whole;
    int i=deckWrite_-int(whole); while(i<0) i+=size;
    double t=0; if(fraction>0) { i-=1; t=1-fraction; }
    const auto at=[&](int v){return double(r[static_cast<size_t>(((v%size)+size)%size)]);};
    return at(i-1)*(-t*(t-1)*(t-2)/6)+at(i)*((t+1)*(t-1)*(t-2)/2)
          +at(i+1)*(-(t+1)*t*(t-2)/2)+at(i+2)*((t+1)*t*(t-1)/6);
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
double HarvestEngine::filter(int channel,double x,double cutoff) noexcept {
    const auto c=size_t(channel);
    const double lp=1-std::exp(-2*pi*cutoff/fs_);
    low_[c]+=lp*(x-low_[c]);
    if(resonance_<=0) return low_[c];
    // Resonant TPT state-variable sweep, faded in over the first 10 % of Resonance so the control never steps.
    // A band-pass centre stops at a radio-like 3.2 kHz; above that it would pass almost nothing of a bass track.
    const double centre=filterMode_==FilterMode::bandpass?std::min(cutoff,3200.0):cutoff;
    // Band-pass uses a wider, gentler Q (0.5 to 2.5, about two octaves down to half an octave) so it stays a voice.
    const double q=filterMode_==FilterMode::bandpass?.5+resonance_*2:.7071+resonance_*4.3,r=1/(2*q);
    const double g=std::tan(pi*std::min(centre,.45*fs_)/fs_);
    const auto stage=[g](double in,std::array<double,2>& s,double damping,double& band){
        const double hp=(in-(2*damping+g)*s[0]-s[1])/(1+2*damping*g+g*g); band=g*hp+s[0]; s[0]=g*hp+band;
        const double lowOut=g*band+s[1]; s[1]=g*band+lowOut; return lowOut;};
    double band=0,ignored=0,out=0;
    // LP 24 is a classic four-pole: a plain Butterworth pair, then the resonant pair (one peak, not two stacked).
    if(filterMode_==FilterMode::lowpass24) out=stage(stage(x,svfA_[c],1/(2*.5412),ignored),svfB_[c],r,band);
    else { out=stage(x,svfA_[c],r,band); if(filterMode_==FilterMode::bandpass) out=band*2*r; }
    // Level compensation keeps a resonant sweep near the dry level (about -3.5 dB at full Resonance).
    out/=1+.5*resonance_;
    return low_[c]+std::min(1.0,resonance_*10)*(out-low_[c]);
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
    history_[0][static_cast<size_t>(deckWrite_)]=left;
    history_[1][static_cast<size_t>(deckWrite_)]=right;
    deckValid_=std::min(deckValid_+1,int(history_[0].size()));
    fx_+=(fxTarget_-fx_)*fxSmoothing_;
    outputGain_+=(outputTarget_-outputGain_)*outputSmoothing_;
    const auto glide=[this](double& value,double target){value+=(target-value)*fxSmoothing_;if(std::abs(target-value)<1e-7)value=target;};
    glide(mix_,mixTarget_); glide(resonance_,resonanceTarget_); glide(filterStart_,filterStartTarget_);
    // Sub Keep's dry low band follows the input continuously, so it is settled whenever a launch begins.
    std::array<double,2> keepLow{};
    if(subKeepHz_>0) {
        for(int ch=0;ch<channels_;++ch){const auto c=size_t(ch);keepLow[c]=keepLow_.run(keepLow_.run(ch==0?left:right,keepLowZ_[c][0]),keepLowZ_[c][1]);}
        if(channels_==1) keepLow[1]=keepLow[0];
    }
    Frame output=input;
    // Idle: keep the Sub Keep and deck high-passes warm on the dry input so a launch starts without a transient.
    if(!voiceActive()) for(int ch=0;ch<channels_;++ch) {
        const auto c=size_t(ch); const double x=ch==0?left:right;
        if(subKeepHz_>0) keepHigh_.run(keepHigh_.run(x,keepHighZ_[c][0]),keepHighZ_[c][1]);
        deckHigh_.run(deckHigh_.run(x,deckHighZ_[c][0]),deckHighZ_[c][1]);
    }
    if(voiceActive()) {
        if(clock_>=n_ && !past_) {
            // At the deadline: Hold and the stop modes sustain; Clean and Tail landings return to the dry input.
            if(!lands() || landingMode_==Landing::hold) {
                past_=true;
                if(state_==State::spinning) { state_=lands()?State::atSpeed:State::stopped; holding_=lands(); }
            } else {
                const bool landed=state_==State::spinning;
                wet_=0; fadeCount_=0; tail_=false; holding_=false; landing_=false; landRamp_=0;
                if(state_==State::spinning || state_==State::releasing) state_=gate_?State::atSpeed:State::off;
                if(landed && tailLanding_) { tailGain_=1; tailStep_=0; tailSilence_=0; tailAge_=0; }
            }
        }
        if(voiceActive()) {
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
            const bool deck=usesDeck();
            double w1=0,w2=0,q1=0,q2=0,runout=1;
            if(!deck) {
                phase_+= (1-controls_.ratio)/(.040*fs_); phase_-=std::floor(phase_);
                q1=phase_; q2=std::fmod(phase_+.5,1.0);
                w1=std::pow(std::sin(pi*q1),2); w2=1-w1;
            } else {
                deckSpeed_=controls_.ratio;
                deckDelay_=std::clamp(deckDelay_+(1-deckSpeed_),2.0,maxDeckDelay_);
                // Running out of recorded history (very long backspins) fades the deck instead of wrapping.
                runout=std::clamp((std::min(maxDeckDelay_,double(deckValid_)-4)-deckDelay_)/(.05*fs_),0.0,1.0);
            }
            // Landing head: a single read pointer that starts 1.5 ms behind the input and moves at the same
            // speed as the two-head voice; it crossfades in near the end and back out on an audible relaunch.
            const bool wantLanding=!deck && clock_>=landStart_;
            if(wantLanding && !landing_) { landing_=true; landDelay_=landBase_; }
            if(landing_) {
                landDelay_=std::min(landDelay_+(1-controls_.ratio),maxDelay_);
                landRamp_=wantLanding?std::min(1.0,landRamp_+1.0/landFade_):std::max(0.0,landRamp_-1.0/declickLength_);
                if(!wantLanding && landRamp_<=0) landing_=false;
            }
            const double land=smooth(landRamp_);
            double alpha=wet_*std::min(1.0,double(valid_)/(.044*fs_));
            if(lands() && landingMode_!=Landing::hold && clock_>=n_-handoff_) alpha*=1-smooth(double(clock_-(n_-handoff_))/handoff_);
            alpha*=mix_;
            Frame coreFrame{};
            for(int ch=0;ch<channels_;++ch) {
                double pitched=0;
                // The deck's 20 Hz high-pass removes the subsonic residue of a nearly stopped tape: standstill is silent.
                if(deck) { const auto c=size_t(ch); pitched=deckHigh_.run(deckHigh_.run(readDeck(ch,deckDelay_)*runout,deckHighZ_[c][0]),deckHighZ_[c][1]); }
                else {
                    if(land<1) pitched=(1-land)*(w1*read(ch,.004*fs_+.040*fs_*q1)+w2*read(ch,.004*fs_+.040*fs_*q2));
                    if(land>0) pitched+=land*read(ch,landDelay_);
                }
                const double core=filter(ch,pitched,controls_.cutoff)*controls_.gain;
                if(ch==0)coreFrame.left=float(core);else coreFrame.right=float(core);
            }
            if(channels_==1)coreFrame.right=coreFrame.left;
            // Latest sound: effects recede over the last 40 % of a climb and Washout's return reaches 0 at the
            // deadline. Hold keeps everything engaged; Tail lets the hall return ring on after the landing.
            double recede=1, washReturn=1;
            if(latest_ && landingMode_!=Landing::hold) {
                if(mode_==Mode::start) recede=1-smooth((double(clock_)/std::max(1,n_-handoff_)-.6)/.4);
                else if(mode_==Mode::stopStart && clock_>=split_) recede=1-smooth((double(clock_-split_)/std::max(1,n_-split_-handoff_)-.6)/.4);
                if(lands() && landingMode_==Landing::clean)
                    washReturn=clock_<returnStart_?1.0:1-smooth(double(clock_-returnStart_)/std::max(1,n_-returnStart_));
            }
            // After a launch from silence the effect mixes ramp in over 15 ms, so their filters start quietly.
            if(latest_ && launchClock_<launchRampLength_) recede*=smooth(double(launchClock_++)/launchRampLength_);
            double hall[2]{};
            const auto effected=effects_.process(coreFrame,fx_,recede,washReturn,tailLanding_?hall:nullptr);
            if(subKeepHz_<=0) {
                output.left=float(left+alpha*(effected.left*outputGain_-left));output.right=float(right+alpha*(effected.right*outputGain_-right));
            } else {
                // Sub Keep: below the split the output stays the dry input; only the band above carries the voice.
                const double wetL=keepLow[0]+keepHigh_.run(keepHigh_.run(effected.left*outputGain_,keepHighZ_[0][0]),keepHighZ_[0][1]);
                const double wetR=keepLow[1]+keepHigh_.run(keepHigh_.run(effected.right*outputGain_,keepHighZ_[1][0]),keepHighZ_[1][1]);
                output.left=float(left+alpha*(wetL-left));output.right=float(right+alpha*(wetR-right));
            }
            if(tailLanding_) { const double g=mix_*outputGain_; output.left=float(output.left+g*hall[0]); output.right=float(output.right+g*hall[1]); }
            if(channels_==1) output.right=output.left;
            if(!past_) ++clock_;
            if(fadeCount_==0 && wet_<=0 && (state_==State::releasing || tail_)) {
                tail_=false; holding_=false; if(state_==State::releasing) state_=State::off;
                if(tailLanding_) { tailGain_=0; effects_.reset(); }
            }
        }
    } else if(tailGain_>0) {
        // Tail landing: the hall rings out over the dry signal after the spin has landed.
        double hall[2]{};
        effects_.process({0.f,0.f},fx_,0,1,hall);
        tailGain_=std::max(0.0,tailGain_+tailStep_);
        const double g=mix_*outputGain_*tailGain_;
        output.left=float(left+g*hall[0]); output.right=channels_==1?output.left:float(right+g*hall[1]);
        // The ring-out ends once it stays below -110 dB for 100 ms; after 10 s it fades out over 200 ms regardless.
        if(std::abs(hall[0])+std::abs(hall[1])<3e-6) { if(++tailSilence_>int(.1*fs_)) tailGain_=0; } else tailSilence_=0;
        if(++tailAge_>int(10*fs_) && tailStep_>=0) tailStep_=-tailGain_/(.2*fs_);
        if(tailGain_<=0) { tailGain_=0; tailStep_=0; effects_.reset(); }
    }
    write_=(write_+1)%int(ring_[0].size()); deckWrite_=(deckWrite_+1)%int(history_[0].size()); return output;
}
float HarvestEngine::progress() const noexcept {
    if(state_==State::atSpeed || state_==State::stopped) return 1;
    if(state_==State::off) return 0;
    return std::clamp(float(clock_)/float(n_),0.f,1.f);
}
float HarvestEngine::speed() const noexcept {
    if(state_==State::stopped) return 0;
    if(!voiceActive()) return 1;
    return float(controls_.ratio);
}
}
