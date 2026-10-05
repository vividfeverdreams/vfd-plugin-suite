#include "Dsp/HarvestEngine.h"
#include "Timing.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <new>
static std::atomic<bool> track{false}; static std::atomic<int> allocations{0};
void* operator new(std::size_t n){if(track)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);} void operator delete(void* p,std::size_t) noexcept{std::free(p);}
namespace {
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
energy::Frame signal(int n,double fs){float a=float(.25*std::sin(n*6.283185307179586*220/fs)+.12*std::sin(n*6.283185307179586*661/fs));return {a,a};}
void matrix(){int cases=0;double peak=0;for(double fs:{44100.,48000.,88200.,96000.,192000.})for(int channels:{1,2})for(float ms:{80.f,800.f})for(float fx:{0.f,45.f,100.f}){
    energy::HarvestEngine e;e.setTime(ms);e.setFx(fx);e.prepare(fs,channels);
    const int pre=int(.060*fs),length=int(std::llround(ms*.001*fs));
    for(int i=0;i<pre;++i){auto in=signal(i,fs),out=e.process(in);require(out.left==in.left&&out.right==in.right,"idle must be bit-exact dry");}
    e.setGate(true);require(e.launches()==1,"one rising edge must launch once");
    track=true;for(int i=0;i<length+64;++i){auto in=signal(pre+i,fs),out=e.process(in);require(std::isfinite(out.left)&&std::isfinite(out.right),"finite output");
        require(out.left==out.right,"dual mono preserved");peak=std::max(peak,double(std::abs(out.left)));if(i>=length)require(out.left==in.left,"exact dry at scheduled deadline");}track=false;
    require(e.state()==energy::State::atSpeed,"held on reaches at speed");e.setGate(true);require(e.launches()==1,"repeated ON inert");
    e.setGate(false);e.setGate(true);require(e.launches()==2,"OFF ON rearms");++cases;
    }
    std::cout<<"PASS "<<cases<<" rate/channel/time/FX cases; peak "<<peak<<"; callback allocations "<<allocations<<'\n';require(allocations==0,"audio processing allocates no memory");
}
void lifecycle(){energy::HarvestEngine e;e.prepare(48000,2);e.reset(true);for(int i=0;i<128;++i){auto s=signal(i,48000);require(e.process(s).left==s.left,"restored ON remains dry");}require(e.launches()==0,"restored ON never launches");
    e.setGate(false);e.setTime(80);e.setGate(true);for(int i=0;i<500;++i)e.process(signal(i,48000));e.setTime(8000);
    require(e.latchedMs()==80,"duration is latched");for(int i=500;i<3900;++i)e.process(signal(i,48000));require(e.state()==energy::State::atSpeed,"mid-launch duration does not extend deadline");
    e.setGate(false);e.setGate(true);require(e.latchedMs()==8000,"next launch uses new duration");for(int i=0;i<600;++i){if(i%3==0)e.setGate(false);if(i%3==1)e.setGate(true);auto s=e.process(signal(i,48000));require(std::isfinite(s.left),"rapid retriggers finite");}
    e.setGate(false);for(int i=0;i<500;++i)e.process(signal(i,48000));require(e.state()==energy::State::off,"cancel lands dry");
    e.reset(true);for(int i=0;i<1000;++i){auto in=signal(i,48000);require(e.process(in,true).left==in.left,"bypass bit-exact");}
    e.setGate(false);e.process({},true);e.setGate(true);require(e.process({},false).left==0,"no stale history at bypass exit");
    std::cout<<"PASS recall, duration latch, cancellation, dense retriggers and bypass\n";
}
void maximum(){energy::HarvestEngine e;e.setTime(8000);e.setFx(100);e.prepare(48000,2);for(int i=0;i<3000;++i)e.process(signal(i,48000));e.setGate(true);
    auto start=std::chrono::steady_clock::now();for(int i=0;i<384100;++i)e.process(signal(i,48000));
    require(e.state()==energy::State::atSpeed,"maximum time terminates");auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"PASS 8-second maximum; rendered in "<<seconds<<" seconds\n";
}
void fixtures(){double maximumPeak=0;
    for(int kind=0;kind<12;++kind){energy::HarvestEngine basic,flux;basic.setFx(0);flux.setFx(100);basic.prepare(48000,2);flux.prepare(48000,2);double difference=0;
        for(int n=0;n<45000;++n){double t=n/48000.,beat=std::fmod(t,.25),s=0,r=0;auto sine=[t](double hz){return std::sin(6.283185307179586*hz*t);};
            uint32_t hash=uint32_t(n)*747796405u+2891336453u;hash=((hash>>((hash>>28u)+4u))^hash)*277803737u;hash=(hash>>22u)^hash;const double noise=double(hash)/2147483648.-1.;
            switch(kind){case 0:s=sine(60);break;case 1:for(int k=1;k<8;++k)s+=sine(110*k)/k;s*=.5;break;
                case 2:for(int k=1;k<12;k+=2)s+=sine(220*k)/k;s*=.6;break;case 3:s=(sine(220)+sine(277.18)+sine(329.63))*.33;break;
                case 4:s=(sine(130)+sine(130.8))*.5*(.7+.3*sine(.8));break;case 5:s=std::sin(6.283185307179586*(55*beat+1.5*(1-std::exp(-beat*35))))*std::exp(-beat*20);break;
                case 6:s=noise*std::exp(-beat*24);break;case 7:s=noise*std::exp(-beat*55);break;case 8:s=(sine(440)+.3*sine(880))*std::exp(-beat*14);break;
                case 9:for(int k=1;k<32;++k){double f=110*k;double formant=std::exp(-std::pow((f-700)/180,2))+std::exp(-std::pow((f-1300)/210,2));s+=sine(f)*formant/k;}break;
                case 10:s=sine(220);r=sine(323);break;case 11:break;}
            float l=float(.3*s),right=kind==10?float(.3*r):l;if(n==3000){basic.setGate(true);flux.setGate(true);}auto a=basic.process({l,right}),b=flux.process({l,right});
            require(std::isfinite(a.left)&&std::isfinite(b.left)&&std::isfinite(b.right),"fixture output finite");maximumPeak=std::max(maximumPeak,double(std::max(std::abs(b.left),std::abs(b.right))));difference+=std::abs(a.left-b.left);
            if(n>=41400)require(a.left==l&&b.left==l&&b.right==right,"fixture final output exact dry");if(kind==11)require(a.left==0&&b.left==0,"silent ON never manufactures audio");
        }
        if(kind!=11)require(difference>.01,"secondary magnetic effects distinct from FX zero");
    }
    std::cout<<"PASS 12 synthetic bass/chord/pad/drum/pluck/vocal-like/stereo/silence fixtures; maximum peak "<<maximumPeak<<'\n';
    energy::HarvestEngine e;e.setFx(0);e.prepare(48000,1);for(int n=-3000;n<0;++n){float x=float(.3*std::sin(n*6.283185307179586*880/48000));e.process({x,x});}e.setGate(true);std::vector<float> out(38401);
    for(int n=0;n<38401;++n){float x=float(.3*std::sin(n*6.283185307179586*880/48000));out[size_t(n)]=e.process({x,x}).left;}
    auto peak=[&](int begin,int length){double best=0;int hz=0;for(int f=100;f<=1000;f+=2){double real=0,imag=0;for(int i=0;i<length;++i){double w=.5-.5*std::cos(i*6.283185307179586/(length-1)),angle=i*6.283185307179586*f/48000;real+=out[size_t(begin+i)]*w*std::cos(angle);imag+=out[size_t(begin+i)]*w*std::sin(angle);}double power=real*real+imag*imag;if(power>best){best=power;hz=f;}}return hz;};
    int early=peak(2000,4000),late=peak(31000,4000);require(early>=180&&early<=290&&late>=700&&late<=950&&late>early*2.5,"tape start moves spectrum from quarter speed toward unity");
    std::cout<<"PASS 880-Hz input: early spectral peak "<<early<<" Hz, late "<<late<<" Hz; exact unity after deadline\n";
}
void rack(){double maximum=0;int cases=0;
    for(double fs:{44100.,48000.,96000.,192000.})for(int channels:{1,2})for(unsigned mask=0;mask<32;++mask){
        energy::FxRack rack;rack.setEffects(mask);rack.prepare(fs,channels);double difference=0;
        track=true;for(int n=0;n<int(fs*.16);++n){auto in=signal(n,fs);auto out=rack.process(in,.85);require(std::isfinite(out.left)&&std::isfinite(out.right),"rack output finite");maximum=std::max(maximum,double(std::max(std::abs(out.left),std::abs(out.right))));difference+=std::abs(in.left-out.left)+std::abs(in.right-out.right);
            if(mask==0 || (channels==1&&mask==1))require(out.left==in.left&&out.right==in.right,"no FX and mono panning retain exact input");
        }track=false;
        if(mask!=0 && !(channels==1&&mask==1))require(difference>.1,"each effect and combined rack changes sound");++cases;
    }
    require(maximum<2,"effect combinations retain bounded output");require(allocations==0,"all effects process without callback allocation");
    energy::FxRack zero;zero.setEffects(31);zero.prepare(48000,2);for(int n=0;n<48000;++n){auto in=signal(n,48000),out=zero.process(in,0);require(out.left==in.left&&out.right==in.right,"FX zero is exact regardless of effect selection");}
    energy::HarvestEngine silence;silence.setEffects(31);silence.setFx(100);silence.prepare(48000,2);silence.setGate(true);for(int n=0;n<50000;++n){auto out=silence.process({});require(out.left==0&&out.right==0,"all-five rack generates no sound on silence");}
    energy::HarvestEngine all;all.setEffects(31);all.setFx(100);all.setTime(20000);all.prepare(48000,2);all.setGate(true);for(int n=0;n<960128;++n){auto in=signal(n,48000),out=all.process(in);require(std::isfinite(out.left)&&std::abs(out.left)<2,"long sync launch stays stable");if(n>=960000)require(out.left==in.left&&out.right==in.right,"sync duration above eight seconds lands exact dry");}
    for(double bpm:{60.,90.,120.,150.,300.})for(int numerator:{3,4,7})for(int denominator:{4,8})for(int division=0;division<6;++division){double beats[]{.5,1,2,numerator*4./denominator,2*numerator*4./denominator,4*numerator*4./denominator};require(std::abs(energy::syncMilliseconds(division,bpm,numerator,denominator)-beats[division]*60000/bpm)<1e-8,"tempo and bar durations exact");}
    std::cout<<"PASS "<<cases<<" effect mask/rate/channel cases; peak "<<maximum<<"; FX zero, silence, 20-second landing, and 180 tempo/signature/division cases\n";
}

void outputLevel(){int cases=0;double maximumError=0;
    for(double rate:{44100.,48000.,96000.,192000.})for(int channels:{1,2})for(float db:{-18.f,-6.f,0.f,6.f,18.f}){
        energy::HarvestEngine unity,trimmed;unity.setFx(75);trimmed.setFx(75);unity.setEffects(31);trimmed.setEffects(31);trimmed.setOutputDb(db);unity.prepare(rate,channels);trimmed.prepare(rate,channels);
        const double gain=std::pow(10.,db*.05);const int duration=int(rate*.8);
        for(int n=-int(rate*.060);n<0;++n){auto in=signal(n,rate),out=trimmed.process(in);require(out.left==in.left&&out.right==in.right,"output trim leaves OFF exact dry");unity.process(in);}
        unity.setGate(true);trimmed.setGate(true);track=true;
        for(int n=0;n<duration+64;++n){auto in=signal(n,rate),a=unity.process(in),out=trimmed.process(in);require(std::isfinite(out.left)&&std::isfinite(out.right),"output range finite");
            if(db==0)require(a.left==out.left&&a.right==out.right,"0 dB preserves previous sound exactly");
            if(n>int(rate*.03)&&n<int(rate*.75)){maximumError=std::max(maximumError,std::abs(out.left-a.left*gain));require(std::abs(out.left-a.left*gain)<2e-6,"gain in active voice matches requested decibels");}
            if(n>=duration)require(out.left==in.left&&out.right==in.right,"boost returns to exact clean level at deadline");
        }track=false;require(allocations==0,"output level allocates no memory");auto in=signal(51,rate);require(trimmed.process(in,true).left==in.left,"bypass ignores output trim");++cases;
    }
    energy::HarvestEngine voice;voice.setOutputDb(18);voice.prepare(48000,2);voice.setGate(true);for(int n=0;n<48000;++n){auto out=voice.process({});require(out.left==0&&out.right==0,"output boost cannot manufacture sound from silence");}
    std::cout<<"PASS "<<cases<<" output-level rate/channel/dB cases; maximum gain error "<<maximumError<<"; 0 dB exact compatibility, OFF/landing/bypass unity, zero allocations\n";
}

}
int main(){try{matrix();lifecycle();maximum();fixtures();rack();outputLevel();return 0;}catch(const std::exception& e){track=false;std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
