#pragma once
#include <cmath>
#include <algorithm>
namespace energy {
inline constexpr const char* divisions[]={"1/8","1/4","1/2","1 BAR","2 BARS","4 BARS"};
inline double syncMilliseconds(int division,double bpm,int numerator,int denominator) noexcept {
    if(!std::isfinite(bpm)||bpm<10||bpm>999)bpm=120;
    if(numerator<1||numerator>32)numerator=4;
    if(denominator<1||denominator>64||(denominator&(denominator-1)))denominator=4;
    division=std::clamp(division,0,5);const double bar=numerator*4./denominator;
    const double beats[]={.5,1,2,bar,2*bar,4*bar};return beats[division]*60000/bpm;
}
}
