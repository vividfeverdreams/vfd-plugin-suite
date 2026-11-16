#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace yoiboi::dsp
{
// SHIFTY SMASH's LR4 split, program-relative RMS detectors, soft knee and
// opto release, voiced without its band saturation to retain vowel contrast.
// Mono body only: the independent sub must be added AFTER body(). No allocation.
class Finisher
{
public:
    void prepare (double rate) noexcept;
    void reset() noexcept;
    void set (float amount, float trimDb) noexcept;
    float body (float x) noexcept;
    float glue (float x, unsigned channel) noexcept;
    float dc (float x, unsigned channel) noexcept;
    void copyChannel (unsigned from, unsigned to) noexcept { console[to] = console[from]; dcIn[to] = dcIn[from]; dcOut[to] = dcOut[from]; }
    float gain() const noexcept { return smoothTrim; }
    static float guard (float x) noexcept { return std::clamp (x, -0.86f, 0.86f); }
private:
    struct Section
    {
        float g = 0, h = 1, s1 = 0, s2 = 0;
        void configure (float hz, double rate) noexcept;
        void process (float x, float& low, float& high) noexcept;
    };
    struct LR4
    {
        std::array<Section, 4> s;
        void configure (float hz, double rate) noexcept { for (auto& f : s) f.configure (hz, rate); }
        void reset() noexcept { for (auto& f : s) f.s1 = f.s2 = 0; }
        void process (float x, float& low, float& high) noexcept;
        float phase (float x) noexcept
        {
            float l, h; s[0].process (x, l, h);
            return l + h - 1.41421356f * (s[0].s1 - s[0].g * h);
        }
    } low, high, allpass;
    struct Band { float detector = 0, db = 0, memory = 0, gain = 1, step = 0; };
    std::array<Band, 3> bands {};
    std::array<float, 3> detectorK {}, attackK {}, releaseK {};
    std::array<float, 3> console {}, dcIn {}, dcOut {};
    float amount = .45f, trim = 1, smoothTrim = 1, smoothAmount = .45f;
    float fast = 0, reference = 0, fastK = 0, referenceK = 0, memoryK = 0;
    float consoleK = 0, dcPole = .999f, smoothing = .001f;
    bool referenceSet = false;
    int interval = 32, countdown = 0;
};
}
