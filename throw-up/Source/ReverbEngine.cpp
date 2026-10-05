#include "ReverbEngine.h"

#include <algorithm>
#include <cmath>

namespace throwup
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float sqrtOneEighth = 0.3535533905932738f;
// The network is energy-normalised for stable feedback, which leaves its
// filtered return too quiet beside a unity dry bass. Restore a useful musical
// level at the return, outside the feedback/drive stages: +18 dB at full THROW.
constexpr float wetReturnGain = 7.943282347f;
constexpr std::array<float, 8> delaySeconds {
    0.02171f, 0.02603f, 0.03137f, 0.03743f,
    0.04469f, 0.05329f, 0.06377f, 0.07621f
};
constexpr std::array<float, 8> motionRates {
    0.71f, 0.83f, 1.0f, 1.13f, 1.37f, 1.61f, 1.79f, 1.97f
};

float bounded (float value, float minimum, float maximum, float fallback)
{
    return std::isfinite (value) ? std::clamp (value, minimum, maximum) : fallback;
}

float noDenormal (float value)
{
    return std::abs (value) < 1.0e-20f ? 0.0f : value;
}
} // namespace

void ReverbEngine::Delay::prepare (std::size_t capacity)
{
    data.assign (std::max<std::size_t> (capacity, 4), 0.0f);
    write = 0;
}

void ReverbEngine::Delay::clear()
{
    std::fill (data.begin(), data.end(), 0.0f);
    write = 0;
}

float ReverbEngine::Delay::read (float delaySamples) const
{
    delaySamples = std::clamp (delaySamples, 1.0f, static_cast<float> (data.size() - 2));
    const auto whole = static_cast<std::size_t> (delaySamples);
    const float fraction = delaySamples - static_cast<float> (whole);
    // Wrap integer indices, not a floating position: rounding a tiny negative
    // position up to the ring length would otherwise read one past the buffer.
    const auto first = write >= whole ? write - whole : write + data.size() - whole;
    const auto second = first == 0 ? data.size() - 1 : first - 1;
    return data[first] + fraction * (data[second] - data[first]);
}

void ReverbEngine::Delay::push (float value)
{
    data[write] = noDenormal (value);
    if (++write == data.size())
        write = 0;
}

float ReverbEngine::Allpass::process (float input, float coefficient)
{
    const float delayed = delay.read (length);
    const float output = delayed - coefficient * input;
    delay.push (input + coefficient * output);
    return output;
}

void ReverbEngine::Allpass::clear() { delay.clear(); }

float ReverbEngine::OnePole::lowpass (float input, float coefficient)
{
    state = noDenormal (input + coefficient * (state - input));
    return state;
}

float ReverbEngine::OnePole::highpass (float input, float coefficient)
{
    return input - lowpass (input, coefficient);
}

Settings ReverbEngine::sanitise (const Settings& settings)
{
    Settings s = settings;
    s.mix = bounded (s.mix, 0.0f, 100.0f, 0.0f);
    s.size = bounded (s.size, 0.0f, 100.0f, 60.0f);
    s.decay = bounded (s.decay, 0.15f, 18.0f, 2.4f);
    s.predelay = bounded (s.predelay, 0.0f, 250.0f, 24.0f);
    s.lowCut = bounded (s.lowCut, 80.0f, 1000.0f, 180.0f);
    s.highCut = bounded (s.highCut, 1000.0f, 18000.0f, 6500.0f);
    s.diffusion = bounded (s.diffusion, 0.0f, 100.0f, 75.0f);
    s.motion = bounded (s.motion, 0.0f, 100.0f, 20.0f);
    s.width = bounded (s.width, 0.0f, 150.0f, 110.0f);
    s.grit = bounded (s.grit, 0.0f, 100.0f, 10.0f);
    s.duck = bounded (s.duck, 0.0f, 100.0f, 15.0f);
    s.output = bounded (s.output, -18.0f, 6.0f, 0.0f);
    return s;
}

float ReverbEngine::safeSample (float sample)
{
    return std::isfinite (sample) ? sample : 0.0f;
}

void ReverbEngine::prepare (double newSampleRate, int maxBlockSize)
{
    (void) maxBlockSize;
    sampleRate = std::isfinite (newSampleRate) && newSampleRate >= 8000.0
                   ? newSampleRate : 48000.0;
    parameterSlew = static_cast<float> (1.0 - std::exp (-1.0 / (sampleRate * 0.035)));
    fastSlew = static_cast<float> (1.0 - std::exp (-1.0 / (sampleRate * 0.008)));
    sizeSlew = static_cast<float> (1.0 - std::exp (-1.0 / (sampleRate * 0.080)));
    envelopeAttack = static_cast<float> (std::exp (-1.0 / (sampleRate * 0.003)));
    envelopeRelease = static_cast<float> (std::exp (-1.0 / (sampleRate * 0.160)));

    for (auto& delay : predelayLines)
        delay.prepare (static_cast<std::size_t> (sampleRate * 0.252) + 8);
    for (auto& delay : tailLines)
        delay.prepare (static_cast<std::size_t> (sampleRate * 0.250) + 8);

    constexpr std::array<float, 4> diffuserSeconds { 0.00311f, 0.00473f, 0.00703f, 0.01117f };
    for (std::size_t channel = 0; channel < diffusers.size(); ++channel)
        for (std::size_t stage = 0; stage < diffusers[channel].size(); ++stage)
        {
            auto& diffuser = diffusers[channel][stage];
            // Integer allpass lengths preserve their magnitude response.
            diffuser.length = std::floor (static_cast<float> (sampleRate)
                                         * diffuserSeconds[stage]
                                         * (channel == 0 ? 1.0f : 1.127f));
            diffuser.delay.prepare (static_cast<std::size_t> (diffuser.length) + 4);
        }
    prepared = true;
    reset();
}

void ReverbEngine::reset()
{
    for (auto& line : predelayLines) line.clear();
    for (auto& line : tailLines) line.clear();
    for (auto& channel : diffusers)
        for (auto& diffuser : channel) diffuser.clear();
    for (auto& filter : tailDamping) filter.state = 0.0f;
    for (auto& channel : inputHighpass)
        for (auto& filter : channel) filter.state = 0.0f;
    for (auto& channel : outputHighpass)
        for (auto& filter : channel) filter.state = 0.0f;
    for (auto& filter : outputLowpass) filter.state = 0.0f;
    for (std::size_t i = 0; i < modulationPhases.size(); ++i)
        modulationPhases[i] = static_cast<float> (i) * 0.773f;
    envelope = 0.0f;
    current = target;
    coefficientCountdown = 0;
    updateCoefficients();
    excitationGain = excitationTarget;
}

void ReverbEngine::setSettings (const Settings& settings)
{
    target = sanitise (settings);
    if (! prepared)
        current = target;
}

void ReverbEngine::hadamard (std::array<float, 8>& values)
{
    // Normalised Walsh-Hadamard matrix: orthogonal, so feedback cannot add energy.
    for (std::size_t stride = 1; stride < values.size(); stride *= 2)
        for (std::size_t start = 0; start < values.size(); start += stride * 2)
            for (std::size_t j = 0; j < stride; ++j)
            {
                const float a = values[start + j];
                const float b = values[start + j + stride];
                values[start + j] = a + b;
                values[start + j + stride] = a - b;
            }
    for (auto& value : values) value *= sqrtOneEighth;
}

void ReverbEngine::updateCoefficients()
{
    const float sr = static_cast<float> (sampleRate);
    lowCutCoefficient = std::exp (-2.0f * pi * std::min (current.lowCut, sr * 0.45f) / sr);
    highCutCoefficient = std::exp (-2.0f * pi * std::min (current.highCut, sr * 0.45f) / sr);
    const float sizeFactor = 0.30f + current.size * 0.025f;
    float feedbackEnergy = 0.0f;
    for (std::size_t i = 0; i < tailLines.size(); ++i)
    {
        tailLengths[i] = delaySeconds[i] * sr * sizeFactor;
        // RT60 is measured in seconds, independently of room size and sample rate.
        feedbackGains[i] = std::exp (-6.907755279f * tailLengths[i] / (sr * current.decay));
        feedbackEnergy += feedbackGains[i] * feedbackGains[i];
    }
    // Balance energy when decay/size changes: long, tiny rooms otherwise become
    // much louder than short rooms because they recirculate far more energy.
    excitationTarget = std::sqrt (std::max (0.001f, 1.0f - feedbackEnergy * 0.125f));
    mixWet = current.mix * 0.01f;
    outputGain = current.output == 0.0f ? 1.0f : std::pow (10.0f, current.output / 20.0f);
}

void ReverbEngine::process (float* const* channels, int numChannels, int numSamples)
{
    if (! prepared || channels == nullptr || numChannels < 1 || numSamples <= 0
        || channels[0] == nullptr || (numChannels > 1 && channels[1] == nullptr))
        return;
    const bool stereo = numChannels > 1;
    const float sr = static_cast<float> (sampleRate);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto slew = [] (float& value, float destination, float speed)
        {
            const float previous = value;
            value += speed * (destination - value);
            if (value == previous || std::abs (destination - value) < 0.0001f)
                value = destination;
        };
        const float previousOutput = current.output;
        const float mixStep = 100.0f / (sr * 0.004f);
        current.mix += std::clamp (target.mix - current.mix, -mixStep, mixStep);
        mixWet = current.mix * 0.01f;
        slew (current.output, target.output, fastSlew);
        slew (current.size, target.size, sizeSlew);
        slew (current.predelay, target.predelay, sizeSlew);
        slew (current.decay, target.decay, parameterSlew);
        slew (current.lowCut, target.lowCut, parameterSlew);
        slew (current.highCut, target.highCut, parameterSlew);
        slew (current.diffusion, target.diffusion, parameterSlew);
        slew (current.motion, target.motion, parameterSlew);
        slew (current.width, target.width, parameterSlew);
        slew (current.grit, target.grit, parameterSlew);
        slew (current.duck, target.duck, parameterSlew);
        if (current.output != previousOutput)
            outputGain = current.output == 0.0f ? 1.0f : std::pow (10.0f, current.output / 20.0f);
        if (--coefficientCountdown <= 0)
        {
            updateCoefficients();
            coefficientCountdown = 16;
        }
        slew (excitationGain, excitationTarget, parameterSlew);

        const float dryL = safeSample (channels[0][sample]);
        const float dryR = stereo ? safeSample (channels[1][sample]) : dryL;
        std::array<float, 2> input { std::clamp (dryL, -8.0f, 8.0f),
                                    std::clamp (dryR, -8.0f, 8.0f) };
        const float detector = std::max (std::abs (input[0]), std::abs (input[1]));
        const float envelopeCoefficient = detector > envelope ? envelopeAttack : envelopeRelease;
        envelope = noDenormal (detector + envelopeCoefficient * (envelope - detector));
        const float predelaySamples = current.predelay * sr * 0.001f;
        const float diffusionCoefficient = current.diffusion * 0.0072f;
        for (std::size_t channel = 0; channel < input.size(); ++channel)
        {
            for (auto& filter : inputHighpass[channel])
                input[channel] = filter.highpass (input[channel], lowCutCoefficient);
            const float original = input[channel];
            // Subsample predelay blends the current sample with the previous one.
            const float delayed = predelayLines[channel].read (std::max (1.0f, predelaySamples));
            input[channel] = predelaySamples < 1.0f
                ? original + predelaySamples * (delayed - original) : delayed;
            predelayLines[channel].push (original);
            for (auto& diffuser : diffusers[channel])
                input[channel] = diffuser.process (input[channel], diffusionCoefficient);
        }

        std::array<float, 8> tail;
        const float motionDepth = current.motion * 0.000019f * sr;
        const float motionSpeed = 0.11f + current.motion * 0.0038f;
        for (std::size_t i = 0; i < tail.size(); ++i)
        {
            const float modulation = std::sin (modulationPhases[i]) * motionDepth;
            modulationPhases[i] += 2.0f * pi * motionSpeed * motionRates[i] / sr;
            if (modulationPhases[i] >= 2.0f * pi) modulationPhases[i] -= 2.0f * pi;
            tail[i] = tailLines[i].read (tailLengths[i] + modulation);
        }

        // Two different orthogonal output vectors produce a decorrelated stereo tail.
        float wetL = (tail[0] + tail[1] - tail[2] + tail[3]
                    - tail[4] + tail[5] - tail[6] - tail[7]) * sqrtOneEighth;
        float wetR = (tail[0] - tail[1] + tail[2] + tail[3]
                    + tail[4] - tail[5] - tail[6] - tail[7]) * sqrtOneEighth;

        for (std::size_t i = 0; i < tail.size(); ++i)
            tail[i] = tailDamping[i].lowpass (tail[i], highCutCoefficient) * feedbackGains[i];
        hadamard (tail);
        for (std::size_t i = 0; i < tail.size(); ++i)
        {
            const float injection = ((i & 1) == 0 ? input[0] : input[1]) * (0.5f * excitationGain);
            // The guard is normally inactive; protects against pathological input/automation.
            tailLines[i].push (std::clamp (safeSample (tail[i] + injection), -32.0f, 32.0f));
        }

        std::array<float, 2> wet { wetL, wetR };
        const float drive = 1.0f + current.grit * 0.08f;
        const float saturationBlend = current.grit * 0.01f;
        for (std::size_t channel = 0; channel < wet.size(); ++channel)
        {
            wet[channel] = outputLowpass[channel].lowpass (wet[channel], highCutCoefficient);
            if (saturationBlend > 0.00001f)
            {
                const float saturated = std::tanh (wet[channel] * drive) / std::sqrt (drive);
                wet[channel] += saturationBlend * (saturated - wet[channel]);
            }
            // Filter after grit, too: saturation must never add sub energy to the wet return.
            for (auto& filter : outputHighpass[channel])
                wet[channel] = filter.highpass (wet[channel], lowCutCoefficient);
        }
        const float mid = (wet[0] + wet[1]) * 0.5f;
        const float side = (wet[0] - wet[1]) * (0.005f * current.width);
        const float duckGain = 1.0f / (1.0f + current.duck * 0.12f * envelope);
        wetL = (mid + side) * duckGain * wetReturnGain;
        wetR = (mid - side) * duckGain * wetReturnGain;

        if (target.bypass || (mixWet == 0.0f && outputGain == 1.0f))
        {
            channels[0][sample] = dryL;
            if (stereo) channels[1][sample] = dryR;
        }
        else
        {
            // THROW changes only the wet return. Keep the original attack/body
            // at unity while adding the independently excited reverb tail.
            channels[0][sample] = safeSample ((dryL
                                              + (stereo ? wetL : mid * duckGain * wetReturnGain) * mixWet) * outputGain);
            if (stereo)
                channels[1][sample] = safeSample ((dryR + wetR * mixWet) * outputGain);
        }
    }
}
} // namespace throwup
