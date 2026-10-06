#pragma once
#include <cmath>
#include <cstdint>
#include <utility>

// Deterministic 140 BPM half-time test loop for renders and checks: G1 sub sine, a 1/8-note wobble saw an
// octave up, kick on 1, snare on 3, and offbeat hats. Peaks around -4 dBFS.
namespace energy::test {
inline float loopNoise(int64_t n) {
    uint32_t h=uint32_t(n)*747796405u+2891336453u; h=((h>>((h>>28u)+4u))^h)*277803737u; h=(h>>22u)^h;
    return float(double(h)/2147483648.-1.);
}
inline std::pair<float,float> bassLoop(int64_t n, double fs) {
    constexpr double tau=6.283185307179586, bpm=140, sub=49.0;
    const double t=double(n)/fs, beat=t*bpm/60, inBar=std::fmod(beat,4.0), inBeat=std::fmod(beat,1.0);
    const double wob=.5+.5*std::sin(tau*(bpm/60*2)*t);
    double saw=0; for(int k=1;k<=12;++k) saw+=std::sin(tau*sub*2*k*t)/k*std::pow(wob,.15*k);
    double kick=0; if(inBar<1) { const double a=inBar*60/bpm; kick=std::sin(tau*(45*a+60*(1-std::exp(-a*30))/30))*std::exp(-a*9); }
    double snare=0; if(inBar>=2&&inBar<3) { const double a=(inBar-2)*60/bpm; snare=(.6*loopNoise(n)+.4*std::sin(tau*190*a))*std::exp(-a*18); }
    const double hatPhase=std::fmod(inBeat+.5,1.0)*60/bpm; const double hat=loopNoise(n*7+3)*std::exp(-hatPhase*90)*.25;
    const double mono=.32*std::sin(tau*sub*t)+.16*saw*(.35+.65*wob)+.45*kick+.30*snare;
    return {float(mono+hat*.8),float(mono+hat*1.1)};
}
}
