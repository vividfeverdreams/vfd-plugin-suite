#include "FxRack.h"
#include "HarvestEngine.h"
namespace energy {
// crossover: high band seen by chorus/phaser/flanger/hall. keep: in-phase low band Washout and panning leave alone.
namespace {constexpr double tau=6.2831853071795864769, crossover=120, keep=150;}
double FxRack::Delay::read(double offset) const noexcept {
    if(samples.empty()||valid<int(std::ceil(offset))+1)return 0;
    double at=write-offset;const int size=int(samples.size());while(at<0)at+=size;
    const int a=int(at)%size,b=(a+1)%size;const double f=at-std::floor(at);
    return samples[size_t(a)]+f*(samples[size_t(b)]-samples[size_t(a)]);
}
void FxRack::Delay::push(double x) noexcept {
    samples[size_t(write)]=std::isfinite(x)?float(x):0.f;write=(write+1)%int(samples.size());valid=std::min(valid+1,int(samples.size()));
}
FxRack::Biquad FxRack::butterworth(double rate,double hz,bool highpass) noexcept {
    const double w=tau*hz/rate,cw=std::cos(w),alpha=std::sin(w)/(2*0.70710678118654752),a0=1+alpha;Biquad q;
    if(highpass){q.b0=(1+cw)*.5/a0;q.b1=-(1+cw)/a0;}else{q.b0=(1-cw)*.5/a0;q.b1=(1-cw)/a0;}
    q.b2=q.b0;q.a1=-2*cw/a0;q.a2=(1-alpha)/a0;return q;
}
void FxRack::prepare(double rate,int channels){
    fs_=rate;channels_=channels;smoothing_=1-std::exp(-1/(.010*fs_));
    // Two Butterworth high-pass passes form a 24 dB/oct Linkwitz-Riley high band for the modulation effects and
    // the hall send; one Butterworth low-pass at 150 Hz gives the in-phase low band Washout and panning keep.
    highpass_=butterworth(fs_,crossover,true);lowpass_=butterworth(fs_,keep,false);
    for(auto& d:chorus_)d.samples.assign(size_t(std::ceil(.060*fs_))+4,0);
    for(auto& d:flanger_)d.samples.assign(size_t(std::ceil(.024*fs_))+4,0);
    const int primes[]={1429,1619,1867,2053,2251,2399,2749,2953};
    for(int i=0;i<8;++i)hall_[size_t(i)].samples.assign(size_t(std::max(8,int(std::round(primes[i]*fs_/44100)))),0);
    reset();
}
void FxRack::reset() noexcept {
    for(auto& d:chorus_)d.reset();for(auto& d:flanger_)d.reset();for(auto& d:hall_)d.reset();
    phase_=panPhase_=0;tick_=0;apX_={};apY_={};phaserFeedback_={};highA_={};highB_={};lowZ_={};coefficients_={};feedback_={};
    for(int i=0;i<5;++i)enabled_[size_t(i)]=(mask_&(1u<<i))?1.:0.;
}
Frame FxRack::process(Frame input,double amount,double recede,double washReturn) noexcept {
    const double intensity=std::clamp(amount,0.,1.),fade=std::clamp(recede,0.,1.),back=std::clamp(washReturn,0.,1.);
    for(int i=0;i<5;++i)enabled_[size_t(i)]+=smoothing_*(((mask_&(1u<<i))?1.:0.)-enabled_[size_t(i)]);
    if((tick_++&15)==0){
        const double sweep=220*std::pow(2.,4.5*(.5+.5*std::sin(phase_*(.35+1.1*intensity))));
        const double ratios[]={.55,.78,1.,1.35,1.8,2.4};
        for(int i=0;i<6;++i){double k=std::tan(tau*.5*std::clamp(sweep*ratios[i],30.,fs_*.38)/fs_);coefficients_[size_t(i)]=(k-1)/(k+1);}
        const double decay=1.4+4.1*intensity;hallAlpha_=1-std::exp(-tau*(700+6500*(1-intensity))/fs_);
        for(auto i=0u;i<8;++i)feedback_[i]=std::pow(10.,-3.0*double(hall_[i].samples.size())/(fs_*decay));
    }
    const std::array<double,2> x{input.left,input.right};
    std::array<double,2> high{},low{},y{};
    const double chorusMix=.55*intensity*enabled_[1]*fade,phaserMix=.65*intensity*enabled_[2]*fade,flangerMix=.65*intensity*enabled_[3]*fade;
    const double flangeDelay=(.4+3.7*(1+std::sin(phase_*.27)))*fs_*.001;
    for(int ch=0;ch<channels_;++ch){const auto c=size_t(ch);
        high[c]=highpass_.run(highpass_.run(x[c],highA_[c]),highB_[c]);low[c]=lowpass_.run(x[c],lowZ_[c]);
        double band=high[c];auto& delay=chorus_[c];delay.push(band);double voices=0;
        for(int v=0;v<3;++v){const double offset=channels_==2?ch*.9:0;double d=(16+v*5+(2+4*intensity)*std::sin(phase_*(.19+.083*v)+v*2.1+offset))*fs_*.001;voices+=delay.read(d)/3;}
        const double ready=std::min(1.,delay.valid/(fs_*.036));band+=chorusMix*ready*(voices-band);
        const double dry=band;double shifted=dry+phaserFeedback_[c]*.28*intensity;
        for(int stage=0;stage<6;++stage){auto j=size_t(stage);double out=coefficients_[j]*shifted+apX_[c][j]-coefficients_[j]*apY_[c][j];apX_[c][j]=shifted;apY_[c][j]=out;shifted=out;}
        phaserFeedback_[c]=std::clamp(shifted,-3.,3.);band+=phaserMix*(shifted-dry);
        auto& flange=flanger_[c];double delayed=flange.read(flangeDelay);
        flange.push(band-delayed*.52*intensity*enabled_[3]);band+=flangerMix*std::min(1.,flange.valid/(fs_*.009))*(delayed-band);
        y[c]=x[c]+(band-high[c]);   // only the high band changed; exact input when every mix is zero
        high[c]=band;               // the modulated high band feeds the hall
    }
    if(channels_==1){y[1]=y[0];high[1]=high[0];low[1]=low[0];}
    std::array<double,8> delayed{};double sum=0;
    for(auto i=0u;i<8;++i){auto& d=hall_[i];const double value=d.valid>=int(d.samples.size())?d.samples[size_t(d.write)]:0.;d.damp+=hallAlpha_*(value-d.damp);delayed[i]=d.damp;sum+=delayed[i];}
    const double sendL=high[0]*fade,sendR=high[1]*fade;
    const double encodeL[]={1,1,-1,-1,1,1,-1,-1},encodeR[]={1,-1,1,-1,1,-1,1,-1};double wetL=0,wetR=0;
    for(auto i=0u;i<8;++i){hall_[i].push((delayed[i]-.25*sum)*feedback_[i]+.25*(sendL*encodeL[i]+sendR*encodeR[i]));wetL+=delayed[i]*encodeL[i]*.25;wetR+=delayed[i]*encodeR[i]*.25;}
    // Washout replaces the part above the low band with the hall; the hall return fades independently.
    const double wash=.90*intensity*intensity*enabled_[4],duck=wash*fade,ret=wash*back;const double wet[]{wetL,wetR};
    for(size_t c=0;c<2;++c)y[c]+=ret*wet[c]-duck*(y[c]-low[c]);
    if(channels_==2){
        // Balance only attenuates, and only above the low band, so neither side exceeds unity and the sub stays centred.
        const double pan=.96*intensity*enabled_[0]*fade*std::sin(panPhase_);
        y[0]+=(std::sqrt(1-std::max(pan,0.))-1)*(y[0]-low[0]);y[1]+=(std::sqrt(1+std::min(pan,0.))-1)*(y[1]-low[1]);
    }else y[1]=y[0];
    phase_+=tau/fs_;panPhase_=std::fmod(panPhase_+tau*panRate_/fs_,tau);
    return {std::isfinite(y[0])?float(y[0]):input.left,std::isfinite(y[1])?float(y[1]):input.right};
}
}
