#include "Finisher.h"
namespace yoiboi::dsp
{
namespace
{
float coefficient (double seconds, double rate) { return static_cast<float> (1 - std::exp (-1 / (seconds * rate))); }
float db (float x) { return 10 * std::log10 (x + 1e-15f); }
float knee (float x)
{
    constexpr float width = 9;
    if (x <= -width * .5f) return 0;
    if (x >= width * .5f) return x;
    const float t = x + width * .5f;
    return t * t / (2 * width);
}
}
void Finisher::Section::configure (float hz, double rate) noexcept
{
    g = static_cast<float> (std::tan (3.141592653589793 * std::min (static_cast<double> (hz), .44 * rate) / rate));
    h = 1 / (1 + 1.41421356f * g + g * g);
}
void Finisher::Section::process (float x, float& l, float& hi) noexcept
{
    hi = (x - (1.41421356f + g) * s1 - s2) * h;
    const float b = g * hi + s1;
    s1 = g * hi + b; l = g * b + s2; s2 = g * b + l;
}
void Finisher::LR4::process (float x, float& l, float& h) noexcept
{
    float a, b, unused;
    s[0].process (x, a, unused); s[1].process (a, l, unused);
    s[2].process (x, unused, b); s[3].process (b, unused, h);
}
void Finisher::prepare (double rate) noexcept
{
    low.configure (130, rate); high.configure (2500, rate); allpass.configure (2500, rate);
    interval = std::max (1, static_cast<int> (rate / 6000));
    constexpr double det[] { .015, .008, .005 }, attack[] { .020, .010, .008 }, release[] { .250, .220, .130 };
    for (unsigned b = 0; b < 3; ++b)
    {
        detectorK[b] = coefficient (det[b], rate);
        attackK[b] = coefficient (attack[b], rate / interval);
        releaseK[b] = coefficient (release[b], rate / interval);
    }
    fastK = coefficient (.05, rate); referenceK = coefficient (1, rate);
    memoryK = coefficient (1.5, rate / interval);
    consoleK = coefficient (1 / (2 * 3.141592653589793 * 180), rate);
    dcPole = static_cast<float> (std::exp (-2 * 3.141592653589793 * 18 / rate));
    smoothing = coefficient (.010, rate);
    reset();
}
void Finisher::reset() noexcept
{
    low.reset(); high.reset(); allpass.reset(); bands = {};
    for (auto& b : bands) b.gain = 1;
    console = {}; dcIn = {}; dcOut = {};
    fast = reference = 0; referenceSet = false; countdown = 0;
    smoothTrim = trim; smoothAmount = amount;
}
void Finisher::set (float a, float t) noexcept
{
    amount = std::clamp (std::isfinite (a) ? a : 0.f, 0.f, 1.f);
    trim = std::pow (10.f, std::clamp (std::isfinite (t) ? t : 0.f, -24.f, 36.f) / 20);
}
float Finisher::body (float x) noexcept
{
    smoothAmount += smoothing * (amount - smoothAmount);
    smoothTrim += smoothing * (trim - smoothTrim);
    float l, rest, m, h;
    low.process (x, l, rest); high.process (rest, m, h);
    const std::array<float, 3> split { allpass.phase (l), m, h };
    for (unsigned b = 0; b < 3; ++b)
        bands[b].detector += detectorK[b] * (split[b] * split[b] - bands[b].detector);
    fast += fastK * (x * x - fast);
    constexpr float gate = 3.2e-6f;
    if (fast > gate)
    {
        if (!referenceSet) { reference = fast; referenceSet = true; }
        reference += referenceK * (x * x - reference);
    }
    if (--countdown <= 0)
    {
        countdown = interval;
        const float ref = db (std::max (gate, reference));
        constexpr float downOffset[] { -1, -1, -8 }, upOffset[] { 0, -6, -14 }, maxUp[] { 0, 18, 22 };
        for (unsigned b = 0; b < 3; ++b)
        {
            auto& s = bands[b];
            const float level = db (s.detector);
            const float down = -(b == 0 ? .667f : .95f) * knee (level - ref - downOffset[b]);
            const float up = std::min (maxUp[b], .8f * knee (ref + upOffset[b] - level))
                           * std::clamp ((level - ref + 60) / 12, 0.f, 1.f);
            const float target = down + up;
            s.memory += memoryK * (std::clamp (-down / 12, 0.f, 1.f) - s.memory);
            const float k = target < s.db ? attackK[b] : releaseK[b] / (1 + .8f * s.memory);
            s.db += k * (target - s.db);
            // No band waveshaper: preserve the spectral envelope of each vowel.
            const float g = std::pow (10.f, smoothAmount * s.db / 20);
            s.step = (g - s.gain) / static_cast<float> (interval);
        }
    }
    float out = 0;
    for (unsigned b = 0; b < 3; ++b) { bands[b].gain += bands[b].step; out += split[b] * bands[b].gain; }
    return out;
}
float Finisher::glue (float x, unsigned c) noexcept
{
    // SUSTAINABLE console low-memory compression + biased, compensated soft clip.
    // Light parallel voicing: loudness comes mainly from the measured trim/OTT.
    constexpr float driveAmount = .10f, drive = 1 + 3 * driveAmount;
    console[c] += consoleK * (x - console[c]);
    const float compressed = x + .18f * driveAmount * (console[c] - x);
    constexpr float bias = .10f * driveAmount;
    const auto soft = [] (float v) noexcept
    {
        // SUSTAINABLE's bounded console curve, avoiding audio-rate libm tanh.
        if (std::abs (v) >= 3.f) return std::copysign (1.f, v);
        const float q = v * v;
        return v * (27.f + q) / (27.f + 9.f * q);
    };
    const float wet = (soft (compressed * drive + bias) - soft (bias)) / std::sqrt (drive);
    x += .20f * smoothAmount * (wet - x);
    // C-infinity ceiling: no knee derivative discontinuity generating distant
    // harmonics. Three square roots implement the eighth root cheaply.
    const float r = x / .84f, r2 = r * r, r4 = r2 * r2;
    return x / std::sqrt (std::sqrt (std::sqrt (1 + r4 * r4)));
}
float Finisher::dc (float x, unsigned c) noexcept
{
    const float y = x - dcIn[c] + dcPole * dcOut[c];
    dcIn[c] = x; dcOut[c] = std::abs (y) > 1e-15f ? y : 0;
    return y;
}
}
