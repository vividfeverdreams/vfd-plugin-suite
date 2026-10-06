#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace shifty
{

/** Parameters shared by all SHIFTY engines. Engines ignore what they do not use. */
struct EngineParams
{
    float shiftSemitones = 0.0f;
    float focus = 0.5f;
    float bite = 0.0f;
    float drive = 0.0f;
    float mix = 1.0f;
    float subCutHz = 120.0f;
    bool subSafe = true;
    bool vowel = false;
    // TALK and MOUTH
    float vowelMorph = 2.0f; // 0 OO, 1 OH, 2 AH, 3 EH, 4 EE
    int voiceSet = 0;        // 0 Human, 1 Growl, 2 Wow
    float character = 0.0f;  // CHARACTER macro, 0..1
    bool bypass = false;     // crossfade to the untouched input (zero-latency engines)
};

/** Per-sample modulation handed to an engine for one processBlock call. */
struct Modulation
{
    const float* shift = nullptr; // SHIFT offset in semitones
    const float* morph = nullptr; // vowel-morph offset (TALK)
};

inline float clampFinite (float value, float low, float high, float fallback) noexcept
{
    return std::isfinite (value) ? juce::jlimit (low, high, value) : fallback;
}

/** DRIVE saturation, the same curve as 0.4.2, with first-order antiderivative
    anti-aliasing (ADAA) on the curve's nonlinear part. The linear part passes
    sample-aligned; only the generated distortion is band-limited. Zero drive is
    exactly transparent. */
class AdaaDrive
{
public:
    void reset() noexcept { previous = 0.0; }

    float process (float input, float amount) noexcept
    {
        const double x1 = input;
        const double x0 = previous;
        previous = x1;
        if (amount <= 0.00001f)
            return input;
        const double a = amount;
        const double g = 1.0 + 4.0 * a;
        const double norm = 1.0 + 0.5 * a;
        const double linear = 1.0 + a * (g / norm - 1.0);
        const double scale = a / norm;
        // N(x) = scale * (tanh(g x) - g x), the curve minus its small-signal line.
        const auto antiderivative = [g, scale] (double x) noexcept
        {
            const double y = std::abs (g * x);
            const double logCosh = y + std::log1p (std::exp (-2.0 * y)) - 0.69314718055994531;
            return scale * (logCosh / g - 0.5 * g * x * x);
        };
        double nonlinear;
        const double dx = x1 - x0;
        if (std::abs (dx) > 1.0e-6)
            nonlinear = (antiderivative (x1) - antiderivative (x0)) / dx;
        else
        {
            const double mid = 0.5 * (x0 + x1);
            nonlinear = scale * (std::tanh (g * mid) - g * mid);
        }
        return static_cast<float> (linear * x1 + nonlinear);
    }

private:
    double previous = 0.0;
};

/** 0.4.2 DRIVE, kept bit-for-bit for the original Classic sound. */
inline float originalDrive (float sample, float amount) noexcept
{
    if (amount <= 0.00001f)
        return sample;
    const float driven = std::tanh (sample * (1.0f + 4.0f * amount)) / (1.0f + 0.5f * amount);
    return sample + amount * (driven - sample);
}

/** Second-order Butterworth section (TPT state variable form). Two in series
    give a Linkwitz-Riley 4 band; lowpass + highpass of one LR4 pair sum to an
    allpass, which SHIFTY uses to keep the dry path phase-matched. */
class ButterworthSection
{
public:
    void reset() noexcept { s1 = s2 = 0.0f; }

    void setCutoff (float frequency, double sampleRate) noexcept
    {
        g = static_cast<float> (std::tan (juce::MathConstants<double>::pi
                                          * juce::jlimit (10.0, 0.45 * sampleRate, static_cast<double> (frequency))
                                          / sampleRate));
        h = 1.0f / (1.0f + 1.41421356f * g + g * g);
    }

    void process (float x, float& low, float& high) noexcept
    {
        high = (x - (1.41421356f + g) * s1 - s2) * h;
        const float band = g * high + s1;
        s1 = g * high + band;
        low = g * band + s2;
        s2 = g * band + low;
    }

private:
    float g = 0.0f, h = 1.0f, s1 = 0.0f, s2 = 0.0f;
};

class LinkwitzRiley4
{
public:
    void reset() noexcept { for (auto& s : sections) s.reset(); }
    void setCutoff (float frequency, double sampleRate) noexcept
    {
        for (auto& s : sections) s.setCutoff (frequency, sampleRate);
    }
    // Two lowpass sections for the low band and two highpass sections for the high band.
    void process (float x, float& low, float& high) noexcept
    {
        float l1, h1, l2, h2, l3, h3, l4, h4;
        sections[0].process (x, l1, h1);
        sections[1].process (l1, l2, h2);
        sections[2].process (x, l3, h3);
        sections[3].process (h3, l4, h4);
        juce::ignoreUnused (h1, h2, l3, l4);
        low = l2;
        high = h4;
    }

private:
    std::array<ButterworthSection, 4> sections;
};

/** Display points shared by the analyzer and engine curves: point i sits at
    20 Hz * (nyquist / 20 Hz)^(i / 63). */
constexpr int displayPoints = 64;

inline float displayFrequency (int point, double sampleRate) noexcept
{
    const double nyquist = 0.5 * sampleRate;
    return static_cast<float> (20.0 * std::pow (nyquist / 20.0, point / 63.0));
}

} // namespace shifty
