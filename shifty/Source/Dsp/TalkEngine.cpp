#include "TalkEngine.h"

#include <cmath>
#include <complex>

namespace shifty
{

namespace
{
// HUMAN: bass-voice formants (after the classic Csound/Peterson-Barney bass
// tables) with the upper-formant levels tamed so the brighter vowels still
// read on a bass. GROWL: a lower, darker throat with a wide second formant.
// WOW: a wide, bright second-formant sweep for "wow" and "yoi" moves.
const std::array<VowelFormants, 5> humanVowels {{
    { { 350.0f, 600.0f, 2400.0f, 2675.0f, 2950.0f }, { 0.0f, -10.0f, -17.0f, -16.0f, -20.0f }, { 40.0f, 80.0f, 100.0f, 120.0f, 120.0f } },
    { { 400.0f, 750.0f, 2400.0f, 2600.0f, 2900.0f }, { 0.0f, -6.0f, -12.0f, -12.0f, -22.0f }, { 40.0f, 80.0f, 100.0f, 120.0f, 120.0f } },
    { { 600.0f, 1040.0f, 2250.0f, 2450.0f, 2750.0f }, { 0.0f, -4.0f, -6.0f, -7.0f, -12.0f }, { 60.0f, 70.0f, 110.0f, 120.0f, 130.0f } },
    { { 400.0f, 1620.0f, 2400.0f, 2800.0f, 3100.0f }, { 0.0f, -6.0f, -7.0f, -9.0f, -13.0f }, { 40.0f, 80.0f, 100.0f, 120.0f, 120.0f } },
    { { 270.0f, 1850.0f, 2600.0f, 3050.0f, 3340.0f }, { 0.0f, -1.0f, -4.0f, -9.0f, -14.0f }, { 60.0f, 90.0f, 100.0f, 120.0f, 120.0f } },
}};
const std::array<VowelFormants, 5> growlVowels {{
    { { 300.0f, 520.0f, 1900.0f, 2400.0f, 2800.0f }, { 0.0f, -4.0f, -16.0f, -20.0f, -26.0f }, { 60.0f, 90.0f, 140.0f, 160.0f, 180.0f } },
    { { 380.0f, 680.0f, 2000.0f, 2450.0f, 2850.0f }, { 0.0f, -3.0f, -13.0f, -17.0f, -24.0f }, { 60.0f, 90.0f, 140.0f, 160.0f, 180.0f } },
    { { 560.0f, 900.0f, 2000.0f, 2400.0f, 2800.0f }, { 0.0f, -2.0f, -9.0f, -13.0f, -19.0f }, { 80.0f, 100.0f, 150.0f, 170.0f, 190.0f } },
    { { 420.0f, 1300.0f, 2100.0f, 2550.0f, 2950.0f }, { 0.0f, -3.0f, -9.0f, -13.0f, -19.0f }, { 70.0f, 110.0f, 150.0f, 170.0f, 190.0f } },
    { { 300.0f, 1500.0f, 2300.0f, 2800.0f, 3200.0f }, { 0.0f, -4.0f, -9.0f, -13.0f, -19.0f }, { 70.0f, 120.0f, 150.0f, 170.0f, 190.0f } },
}};
const std::array<VowelFormants, 5> wowVowels {{
    { { 300.0f, 550.0f, 2200.0f, 3000.0f, 3600.0f }, { 0.0f, -3.0f, -18.0f, -22.0f, -28.0f }, { 70.0f, 100.0f, 140.0f, 180.0f, 220.0f } },
    { { 450.0f, 800.0f, 2300.0f, 3000.0f, 3600.0f }, { 0.0f, -2.0f, -13.0f, -18.0f, -24.0f }, { 70.0f, 100.0f, 140.0f, 180.0f, 220.0f } },
    { { 750.0f, 1200.0f, 2500.0f, 3100.0f, 3700.0f }, { 0.0f, -1.0f, -7.0f, -12.0f, -18.0f }, { 80.0f, 110.0f, 150.0f, 180.0f, 220.0f } },
    { { 550.0f, 1800.0f, 2600.0f, 3200.0f, 3800.0f }, { 0.0f, -1.0f, -5.0f, -9.0f, -15.0f }, { 70.0f, 110.0f, 150.0f, 180.0f, 220.0f } },
    { { 300.0f, 2300.0f, 3000.0f, 3500.0f, 4000.0f }, { 0.0f, 0.0f, -4.0f, -8.0f, -13.0f }, { 70.0f, 120.0f, 150.0f, 180.0f, 220.0f } },
}};

float bandwidthScale (float focus) noexcept
{
    // Voice-table bandwidths (40-130 Hz) would pick out single partials of a
    // 55 Hz bass and whistle. FOCUS 0 = broad (7x), 0.55 = 2.7x, 1 = 1.26x.
    return 7.0f * std::pow (0.18f, juce::jlimit (0.0f, 1.0f, focus));
}

float bankPower (const std::array<float, 5>& frequency, const std::array<float, 5>& bandwidth,
                 const std::array<float, 5>& gain) noexcept
{
    // Output power of the band-pass bank for a sawtooth-like (1/f^2) input:
    // each normalised band-pass passes about bandwidth * S(f0) of power.
    float power = 0.0f;
    for (size_t k = 0; k < 5; ++k)
        power += gain[k] * gain[k] * bandwidth[k] / (frequency[k] * frequency[k]);
    return power;
}
} // namespace

const std::array<VowelFormants, 5>& vowelTable (int voiceSet) noexcept
{
    switch (voiceSet)
    {
        case 1: return growlVowels;
        case 2: return wowVowels;
        default: return humanVowels;
    }
}

const char* vowelName (int vowel) noexcept
{
    static const char* names[] = { "OO", "OH", "AH", "EH", "EE" };
    return names[juce::jlimit (0, 4, vowel)];
}

const char* voiceSetName (int voiceSet) noexcept
{
    static const char* names[] = { "HUMAN", "GROWL", "WOW" };
    return names[juce::jlimit (0, 2, voiceSet)];
}

VowelFormants interpolateVowel (int voiceSet, float morph) noexcept
{
    const float position = juce::jlimit (0.0f, 4.0f, morph);
    const int lower = std::min (3, static_cast<int> (position));
    const float t = position - static_cast<float> (lower);
    const auto& table = vowelTable (voiceSet);
    const auto& a = table[static_cast<size_t> (lower)];
    const auto& b = table[static_cast<size_t> (lower + 1)];
    VowelFormants result {};
    for (size_t k = 0; k < 5; ++k)
    {
        result.frequency[k] = std::exp ((1.0f - t) * std::log (a.frequency[k]) + t * std::log (b.frequency[k]));
        result.bandwidth[k] = std::exp ((1.0f - t) * std::log (a.bandwidth[k]) + t * std::log (b.bandwidth[k]));
        result.decibels[k] = (1.0f - t) * a.decibels[k] + t * b.decibels[k];
    }
    return result;
}

void TalkEngine::prepare (double sampleRate, int maxBlockSize, int channels)
{
    juce::ignoreUnused (maxBlockSize);
    sampleRate_ = (std::isfinite (sampleRate) && sampleRate > 1000.0) ? sampleRate : 48000.0;
    smoothing_ = static_cast<float> (1.0 - std::exp (-1.0 / (0.02 * sampleRate_)));
    powerCoefficient_ = static_cast<float> (1.0 - std::exp (-1.0 / (0.25 * sampleRate_)));
    autoGainCoefficient_ = static_cast<float> (1.0 - std::exp (-1.0 / (0.10 * sampleRate_)));
    channels_.resize (static_cast<size_t> (std::max (1, channels)));

    // Reference: HUMAN "AH" at the default FOCUS. Every other vowel, shift and
    // bandwidth is normalised to the same sawtooth output power.
    Smoothed saved = current_;
    current_ = Smoothed {};
    current_.focus = 0.5f;
    std::array<float, 5> f {}, bw {}, g {};
    computeFormants (0.0f, 2.0f, f, bw, g);
    referencePower_ = bankPower (f, bw, g);
    current_ = saved;
    reset();
}

void TalkEngine::reset()
{
    for (auto& channel : channels_)
    {
        for (auto& formant : channel.formants)
            formant = {};
        channel.split.reset();
        channel.drivenSplit.reset();
        channel.drive.reset();
        channel.inputPower = channel.outputPower = 0.0f;
    }
    current_ = target_;
    autoGain_ = 1.0f;
    lastSubCut_ = -1.0f;
}

void TalkEngine::setParameters (const EngineParams& params) noexcept
{
    target_.shift = clampFinite (params.shiftSemitones, -12.0f, 12.0f, 0.0f);
    target_.morph = clampFinite (params.vowelMorph, 0.0f, 4.0f, 2.0f);
    target_.focus = clampFinite (params.focus, 0.0f, 1.0f, 0.5f);
    target_.bite = clampFinite (params.bite, 0.0f, 1.0f, 0.0f);
    target_.drive = clampFinite (params.drive, 0.0f, 1.0f, 0.0f);
    target_.mix = clampFinite (params.mix, 0.0f, 1.0f, 1.0f);
    target_.subCut = clampFinite (params.subCutHz, 55.0f, 240.0f, 120.0f);
    target_.subSafe = params.subSafe ? 1.0f : 0.0f;
    target_.character = clampFinite (params.character, 0.0f, 1.0f, 0.0f);
    target_.bypass = params.bypass ? 1.0f : 0.0f;
    const int set = juce::jlimit (0, 2, params.voiceSet);
    for (int i = 0; i < 3; ++i)
        target_.setWeight[static_cast<size_t> (i)] = i == set ? 1.0f : 0.0f;
}

void TalkEngine::computeFormants (float shift, float morph, std::array<float, 5>& frequency,
                                  std::array<float, 5>& bandwidth, std::array<float, 5>& gain) const noexcept
{
    const float position = juce::jlimit (0.0f, 4.0f, morph);
    const int lower = std::min (3, static_cast<int> (position));
    const float t = position - static_cast<float> (lower);
    std::array<float, 5> logF {}, logBw {}, db {};
    float totalWeight = 0.0f;
    for (int set = 0; set < 3; ++set)
    {
        const float weight = current_.setWeight[static_cast<size_t> (set)];
        if (weight < 0.0005f)
            continue;
        const auto& table = vowelTable (set);
        const auto& a = table[static_cast<size_t> (lower)];
        const auto& b = table[static_cast<size_t> (lower + 1)];
        for (size_t k = 0; k < 5; ++k)
        {
            logF[k] += weight * ((1.0f - t) * std::log (a.frequency[k]) + t * std::log (b.frequency[k]));
            logBw[k] += weight * ((1.0f - t) * std::log (a.bandwidth[k]) + t * std::log (b.bandwidth[k]));
            db[k] += weight * ((1.0f - t) * a.decibels[k] + t * b.decibels[k]);
        }
        totalWeight += weight;
    }
    const float inverse = totalWeight > 0.0f ? 1.0f / totalWeight : 1.0f;
    const float ratio = std::exp2 (shift / 12.0f);
    const float narrowing = bandwidthScale (current_.focus)
                          / ((1.0f + 0.8f * current_.bite) * (1.0f + 0.6f * current_.character));
    const float highest = static_cast<float> (0.42 * sampleRate_);
    for (size_t k = 0; k < 5; ++k)
    {
        const float f = juce::jlimit (60.0f, highest, std::exp (logF[k] * inverse) * ratio);
        const float q = juce::jlimit (0.7f, 30.0f, f / (std::exp (logBw[k] * inverse) * ratio * narrowing));
        frequency[k] = f;
        bandwidth[k] = f / q;
        gain[k] = juce::Decibels::decibelsToGain (db[k] * inverse);
    }
}

float TalkEngine::responseMagnitude (const Display& display, float frequency) noexcept
{
    std::complex<float> sum = display.body;
    for (size_t k = 0; k < 5; ++k)
    {
        const float f0 = display.frequency[k];
        if (f0 <= 0.0f)
            continue;
        const float x = frequency / f0;
        const float d = display.bandwidth[k] / f0; // 1/Q
        // Normalised band-pass: unity at f0.
        const std::complex<float> numerator (0.0f, d * x);
        const std::complex<float> denominator (1.0f - x * x, d * x);
        sum += display.gain[k] * numerator / denominator;
    }
    return std::abs (sum);
}

void TalkEngine::processBlock (juce::AudioBuffer<float>& buffer, const Modulation& modulation)
{
    const int samples = buffer.getNumSamples();
    const int channels = std::min (buffer.getNumChannels(), static_cast<int> (channels_.size()));
    if (samples == 0 || channels == 0)
        return;
    juce::ScopedNoDenormals noDenormals;
    const float piOverRate = static_cast<float> (juce::MathConstants<double>::pi / sampleRate_);

    std::array<float, 5> frequency {}, bandwidth {}, gain {}, g {}, k {}, a1 {}, a2 {}, a3 {};
    for (int n = 0; n < samples; ++n)
    {
        // One-pole smoothing that lands exactly on its target. In float a one-pole
        // stalls a few hundred ulps short (the step rounds to nothing), which left a
        // finished bypass 3e-5 short of the dry signal; once a step no longer
        // moves the value, it takes the target.
        auto smooth = [this] (float& value, float destination) noexcept
        {
            const float next = value + smoothing_ * (destination - value);
            value = juce::exactlyEqual (next, value) ? destination : next;
        };
        smooth (current_.shift, target_.shift);
        smooth (current_.morph, target_.morph);
        smooth (current_.focus, target_.focus);
        smooth (current_.bite, target_.bite);
        smooth (current_.drive, target_.drive);
        smooth (current_.mix, target_.mix);
        smooth (current_.subCut, target_.subCut);
        smooth (current_.subSafe, target_.subSafe);
        smooth (current_.character, target_.character);
        smooth (current_.bypass, target_.bypass);
        for (size_t i = 0; i < 3; ++i)
            smooth (current_.setWeight[i], target_.setWeight[i]);

        if (std::abs (current_.subCut - lastSubCut_) > 0.01f)
        {
            for (auto& channel : channels_)
            {
                channel.split.setCutoff (current_.subCut, sampleRate_);
                channel.drivenSplit.setCutoff (current_.subCut, sampleRate_);
            }
            lastSubCut_ = current_.subCut;
        }

        const float shiftOffset = modulation.shift != nullptr && std::isfinite (modulation.shift[n]) ? modulation.shift[n] : 0.0f;
        const float morphOffset = modulation.morph != nullptr && std::isfinite (modulation.morph[n]) ? modulation.morph[n] : 0.0f;
        const float shift = juce::jlimit (-12.0f, 12.0f, current_.shift + shiftOffset);
        const float morph = juce::jlimit (0.0f, 4.0f, current_.morph + morphOffset);
        computeFormants (shift, morph, frequency, bandwidth, gain);
        const float norm = juce::jlimit (0.25f, 6.0f,
            std::sqrt (referencePower_ / std::max (1.0e-12f, bankPower (frequency, bandwidth, gain))));
        for (size_t f = 0; f < 5; ++f)
        {
            g[f] = std::tan (piOverRate * frequency[f]);
            k[f] = bandwidth[f] / frequency[f];
            a1[f] = 1.0f / (1.0f + g[f] * (g[f] + k[f]));
            a2[f] = g[f] * a1[f];
            a3[f] = g[f] * a2[f];
            gain[f] *= norm;
        }
        const float body = 0.1f * (1.0f - 0.7f * current_.character) * (1.0f - 0.5f * current_.bite);
        const float bankDrive = 1.0f + 3.0f * current_.bite + 2.0f * current_.character;
        const float bankMakeup = (1.0f + 0.25f * (bankDrive - 1.0f)) / bankDrive;

        float inputPower = 0.0f, outputPower = 0.0f;
        for (int c = 0; c < channels; ++c)
        {
            auto& state = channels_[static_cast<size_t> (c)];
            float* data = buffer.getWritePointer (c);
            const float original = std::isfinite (data[n]) ? data[n] : 0.0f;
            const float driven = state.drive.process (original, current_.drive);
            float low, originalHigh, drivenLow, drivenHigh;
            state.split.process (original, low, originalHigh);
            state.drivenSplit.process (driven, drivenLow, drivenHigh);
            juce::ignoreUnused (drivenLow);
            const float subWeight = current_.subSafe;
            const float bankInput = drivenHigh * subWeight + driven * (1.0f - subWeight);
            const float shaped = std::tanh (bankDrive * bankInput) * bankMakeup;

            float bank = 0.0f;
            for (size_t f = 0; f < 5; ++f)
            {
                auto& formant = state.formants[f];
                const float v3 = shaped - formant.ic2;
                const float v1 = a1[f] * formant.ic1 + a2[f] * v3;
                const float v2 = formant.ic2 + a2[f] * formant.ic1 + a3[f] * v3;
                formant.ic1 = 2.0f * v1 - formant.ic1;
                formant.ic2 = 2.0f * v2 - formant.ic2;
                bank += gain[f] * k[f] * v1;
            }
            const float wetHigh = (bank + body * bankInput) * autoGain_;
            const float wet = low * subWeight + wetHigh;
            const float dry = (low + originalHigh) * subWeight + original * (1.0f - subWeight);
            float out = dry + current_.mix * (wet - dry);
            out += current_.bypass * (original - out);
            if (! std::isfinite (out))
            {
                for (auto& formant : state.formants)
                    formant = {};
                out = original;
            }
            data[n] = out;

            state.inputPower += powerCoefficient_ * (bankInput * bankInput - state.inputPower);
            state.outputPower += powerCoefficient_ * (wetHigh * wetHigh - state.outputPower);
            inputPower += state.inputPower;
            outputPower += state.outputPower;
        }

        // Slow, linked level match of the bank to its own input.
        if (inputPower > 1.0e-10f && outputPower > 1.0e-12f)
        {
            const float targetGain = juce::jlimit (0.35f, 4.0f,
                autoGain_ * std::sqrt (inputPower / outputPower));
            autoGain_ += autoGainCoefficient_ * (targetGain - autoGain_);
        }

        if (n == samples - 1)
        {
            display_.frequency = frequency;
            display_.bandwidth = bandwidth;
            display_.gain = gain;
            display_.body = body;
            display_.morph = morph;
        }
    }
}

} // namespace shifty
