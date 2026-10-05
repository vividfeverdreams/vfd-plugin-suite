#include "FxRack.h"
#include "HarvestEngine.h"
namespace energy {
namespace {constexpr double tau=6.2831853071795864769;}
double FxRack::Delay::read(double offset) const noexcept {
    if(samples.empty()||valid<int(std::ceil(offset))+1)return 0;
    double at=write-offset;const int size=int(samples.size());while(at<0)at+=size;
    const int a=int(at)%size,b=(a+1)%size;const double f=at-std::floor(at);
    return samples[size_t(a)]+f*(samples[size_t(b)]-samples[size_t(a)]);
}
void FxRack::Delay::push(double x) noexcept {
    samples[size_t(write)]=std::isfinite(x)?float(x):0.f;write=(write+1)%int(samples.size());valid=std::min(valid+1,int(samples.size()));
}
void FxRack::prepare(double rate,int channels){
    fs_=rate;channels_=channels;smoothing_=1-std::exp(-1/(.010*fs_));subAlpha_=1-std::exp(-tau*120/fs_);
    for(auto& d:chorus_)d.samples.assign(size_t(std::ceil(.060*fs_))+4,0);
    for(auto& d:flanger_)d.samples.assign(size_t(std::ceil(.024*fs_))+4,0);
    const int primes[]={1429,1619,1867,2053,2251,2399,2749,2953};
    for(int i=0;i<8;++i)hall_[size_t(i)].samples.assign(size_t(std::max(8,int(std::round(primes[i]*fs_/44100)))),0);
    reset();
}
void FxRack::reset() noexcept {
    for(auto& d:chorus_)d.reset();for(auto& d:flanger_)d.reset();for(auto& d:hall_)d.reset();
    phase_=panPhase_=0;tick_=0;apX_={};apY_={};phaserFeedback_={};sub_={};washHighpass_={};coefficients_={};feedback_={};
    for(int i=0;i<5;++i)enabled_[size_t(i)]=(mask_&(1u<<i))?1.:0.;
}
Frame FxRack::process(Frame input,double amount) noexcept {
    const double intensity=std::clamp(amount,0.,1.);
    for(int i=0;i<5;++i)enabled_[size_t(i)]+=smoothing_*(((mask_&(1u<<i))?1.:0.)-enabled_[size_t(i)]);
    if((tick_++&15)==0){
        const double sweep=220*std::pow(2.,4.5*(.5+.5*std::sin(phase_*(.35+1.1*intensity))));
        const double ratios[]={.55,.78,1.,1.35,1.8,2.4};
        for(int i=0;i<6;++i){double k=std::tan(tau*.5*std::clamp(sweep*ratios[i],30.,fs_*.38)/fs_);coefficients_[size_t(i)]=(k-1)/(k+1);}
        const double decay=1.4+4.1*intensity;hallAlpha_=1-std::exp(-tau*(700+6500*(1-intensity))/fs_);
        for(auto i=0u;i<8;++i)feedback_[i]=std::pow(10.,-3.0*double(hall_[i].samples.size())/(fs_*decay));
    }
    std::array<double,2> signal{input.left,input.right};
    const double chorusMix=.55*intensity*enabled_[1],phaserMix=.65*intensity*enabled_[2],flangerMix=.65*intensity*enabled_[3];
    const double flangeDelay=(.4+3.7*(1+std::sin(phase_*.27)))*fs_*.001;
    for(int ch=0;ch<channels_;++ch){const auto c=size_t(ch);auto& delay=chorus_[c];delay.push(signal[c]);double voices=0;
        for(int v=0;v<3;++v){const double offset=channels_==2?ch*.9:0;double d=(16+v*5+(2+4*intensity)*std::sin(phase_*(.19+.083*v)+v*2.1+offset))*fs_*.001;voices+=delay.read(d)/3;}
        const double ready=std::min(1.,delay.valid/(fs_*.036));signal[c]+=chorusMix*ready*(voices-signal[c]);
        sub_[c]+=subAlpha_*(signal[c]-sub_[c]);double high=signal[c]-sub_[c],shifted=high+phaserFeedback_[c]*.28*intensity;
        for(int stage=0;stage<6;++stage){auto j=size_t(stage);double y=coefficients_[j]*shifted+apX_[c][j]-coefficients_[j]*apY_[c][j];apX_[c][j]=shifted;apY_[c][j]=y;shifted=y;}
        phaserFeedback_[c]=std::clamp(shifted,-3.,3.);signal[c]+=phaserMix*(shifted-high);
        auto& flange=flanger_[c];double delayed=flange.read(flangeDelay);
        flange.push(signal[c]-delayed*.52*intensity*enabled_[3]);signal[c]+=flangerMix*std::min(1.,flange.valid/(fs_*.009))*(delayed-signal[c]);
        washHighpass_[c]+=subAlpha_*(signal[c]-washHighpass_[c]);
    }
    if(channels_==1){signal[1]=signal[0];washHighpass_[1]=washHighpass_[0];}
    std::array<double,8> delayed{};double sum=0;
    for(auto i=0u;i<8;++i){auto& d=hall_[i];const double value=d.valid>=int(d.samples.size())?d.samples[size_t(d.write)]:0.;d.damp+=hallAlpha_*(value-d.damp);delayed[i]=d.damp;sum+=delayed[i];}
    const double left=signal[0]-washHighpass_[0],right=signal[1]-washHighpass_[1];
    const double encodeL[]={1,1,-1,-1,1,1,-1,-1},encodeR[]={1,-1,1,-1,1,-1,1,-1};double wetL=0,wetR=0;
    for(auto i=0u;i<8;++i){hall_[i].push((delayed[i]-.25*sum)*feedback_[i]+.25*(left*encodeL[i]+right*encodeR[i]));wetL+=delayed[i]*encodeL[i]*.25;wetR+=delayed[i]*encodeR[i]*.25;}
    const double washMix=.90*intensity*intensity*enabled_[4];signal[0]+=washMix*(wetL-signal[0]);signal[1]+=washMix*(wetR-signal[1]);
    if(channels_==2){const double pan=.96*intensity*enabled_[0]*std::sin(panPhase_);signal[0]*=std::sqrt(1-pan);signal[1]*=std::sqrt(1+pan);}else signal[1]=signal[0];
    phase_+=tau/fs_;panPhase_=std::fmod(panPhase_+tau*panRate_/fs_,tau);
    return {std::isfinite(signal[0])?float(signal[0]):input.left,std::isfinite(signal[1])?float(signal[1]):input.right};
}
}
