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
    Frame process(Frame input,double amount) noexcept;
private:
    struct Delay {std::vector<float> samples;int write=0,valid=0;double damp=0;
        void reset() noexcept {write=valid=0;damp=0;}
        double read(double offset) const noexcept;
        void push(double x) noexcept;
    };
    std::array<Delay,2> chorus_,flanger_;
    std::array<Delay,8> hall_;
    std::array<double,5> enabled_{};
    std::array<std::array<double,6>,2> apX_{},apY_{};
    std::array<double,2> phaserFeedback_{},sub_{},washHighpass_{};
    std::array<double,6> coefficients_{};std::array<double,8> feedback_{};
    double fs_=48000,smoothing_=.002,subAlpha_=.01,hallAlpha_=.1;
    double phase_=0,panPhase_=0,panRate_=2.5;
    unsigned mask_=phaser,tick_=0;int channels_=2;
};
}
