#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace energy {
struct Frame;
enum Effect : unsigned { pan=1, chorus=2, phaser=4, flanger=8, washout=16 };
class FxRack {
public:
    void prepare(double rate,int channels);
    void reset() noexcept;
    void setEffects(unsigned mask) noexcept { mask_=mask&31u; }
    void setDuration(float ms) noexcept {panRate_=std::clamp(2000./std::max(1.f,ms),.125,6.);}
    // Legacy reproduces ENERGY HARVESTER 0.3.2 exactly (full-band effects, constant mixes, boosting balance).
    void setLegacy(bool legacy) noexcept { legacy_=legacy; }
    // amount: FX Amount 0..1 (depth and character). recede: 1 -> 0 as the spin lands; scales every effect's mix
    // and the Washout send. washReturn: Washout hall return level, driven to 0 at the scheduled endpoint.
    // Chorus, phaser, flanger and the hall see only the band above ~120 Hz; Washout and panning keep an
    // in-phase low band (150 Hz), so the sub stays put. With every mix at zero the input is returned bit-exact.
    // hall: when not null, the Washout hall return is written there instead of being mixed in (Tail landing).
    Frame process(Frame input,double amount,double recede=1,double washReturn=1,double* hall=nullptr) noexcept;
private:
    struct Delay {std::vector<float> samples;int write=0,valid=0;double damp=0;
        void reset() noexcept {write=valid=0;damp=0;}
        double read(double offset) const noexcept;
        void push(double x) noexcept;
    };
    struct Biquad {
        double b0=1,b1=0,b2=0,a1=0,a2=0;
        double run(double x,std::array<double,2>& z) const noexcept {const double y=b0*x+z[0];z[0]=b1*x-a1*y+z[1];z[1]=b2*x-a2*y;return y;}
    };
    static Biquad butterworth(double rate,double hz,bool highpass) noexcept;
    void updateCoefficients(double intensity) noexcept;
    Frame processLegacy(Frame input,double intensity,double* hall) noexcept;
    std::array<Delay,2> chorus_,flanger_;
    std::array<Delay,8> hall_;
    std::array<double,5> enabled_{};
    std::array<std::array<double,6>,2> apX_{},apY_{};
    std::array<double,2> phaserFeedback_{},sub_{},washHighpass_{};
    Biquad highpass_,lowpass_;
    std::array<std::array<double,2>,2> highA_{},highB_{},lowZ_{};
    std::array<double,6> coefficients_{};std::array<double,8> feedback_{};
    double fs_=48000,smoothing_=.002,subAlpha_=.01,hallAlpha_=.1;
    double phase_=0,panPhase_=0,panRate_=2.5;
    unsigned mask_=phaser,tick_=0;int channels_=2;bool legacy_=false;
};
}
