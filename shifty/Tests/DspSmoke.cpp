#include "../Source/Dsp/FormantEngine.h"
#include "../Source/Dsp/Modulators.h"
#include "../Source/Dsp/MouthEngine.h"
#include "../Source/Dsp/TalkEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
using Audio = std::vector<std::vector<float>>;
constexpr double pi = juce::MathConstants<double>::pi;

struct Processed
{
    Audio audio;
    int latency = 0;
};

Audio makeAudio (int channels, int samples)
{
    return Audio (static_cast<size_t> (channels), std::vector<float> (static_cast<size_t> (samples), 0.0f));
}

int sampleCount (const Audio& audio)
{
    return audio.empty() ? 0 : static_cast<int> (audio.front().size());
}

float envelope (double seconds, double duration)
{
    const double attack = std::min (1.0, seconds / 0.025);
    const double release = std::min (1.0, (duration - seconds) / 0.2);
    return static_cast<float> (std::max (0.0, std::min (attack, release)));
}

Audio makeBass (double sampleRate, double duration, int channels, int tailSamples, bool reese)
{
    const int activeSamples = static_cast<int> (std::round (duration * sampleRate));
    auto audio = makeAudio (channels, activeSamples + tailSamples);
    const double fundamental = reese ? 67.0 : 92.0;

    for (int sample = 0; sample < activeSamples; ++sample)
    {
        const double time = sample / sampleRate;
        const double motion = reese ? 0.16 * std::sin (2.0 * pi * 1.7 * time) : 0.0;
        const float amp = envelope (time, duration);
        for (int channel = 0; channel < channels; ++channel)
        {
            double value = 0.0;
            const double detune = reese && channel == 1 ? 0.36 : 0.0;
            const double basePhase = 2.0 * pi * (fundamental + detune) * time + motion;
            for (int harmonic = 1; harmonic <= 18; ++harmonic)
            {
                const double frequency = harmonic * fundamental;
                const double firstFormant = std::exp (-std::pow ((frequency - 365.0) / 170.0, 2.0));
                const double secondFormant = std::exp (-std::pow ((frequency - 880.0) / 250.0, 2.0));
                const double shape = (0.15 + 0.95 * firstFormant + 0.7 * secondFormant)
                                     / std::sqrt (static_cast<double> (harmonic));
                value += shape * std::sin (harmonic * basePhase + 0.13 * harmonic);
            }
            audio[static_cast<size_t> (channel)][static_cast<size_t> (sample)] =
                amp * static_cast<float> (0.12 * value);
        }
    }
    return audio;
}

Audio makeProbe (double sampleRate, int channels, int tailSamples)
{
    const int activeSamples = static_cast<int> (sampleRate * 0.72);
    auto audio = makeAudio (channels, activeSamples + tailSamples);
    uint32_t random = 0x5e71a3d9u;

    for (int sample = 0; sample < activeSamples; ++sample)
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        const float noise = static_cast<float> (static_cast<int32_t> (random))
                            / static_cast<float> (std::numeric_limits<int32_t>::max());
        const double time = sample / sampleRate;
        const float transient = (sample % 4931 < 35) ? 0.17f * (1.0f - (sample % 4931) / 35.0f) : 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const double phase = channel == 0 ? 0.0 : 0.4;
            audio[static_cast<size_t> (channel)][static_cast<size_t> (sample)] =
                0.22f * static_cast<float> (std::sin (2.0 * pi * 57.0 * time + phase))
              + 0.08f * static_cast<float> (std::sin (2.0 * pi * 431.0 * time + phase))
              + 0.025f * noise + transient;
        }
    }
    return audio;
}

Processed runEngine (const Audio& input, double sampleRate, const shifty::EngineParams& initial,
                     bool automate = false)
{
    const int channels = static_cast<int> (input.size());
    const int samples = sampleCount (input);
    Processed result { makeAudio (channels, samples), 0 };

    shifty::FormantEngine engine;
    engine.setParameters (initial);
    engine.prepare (sampleRate, 1024, channels);
    result.latency = engine.getLatencySamples();

    constexpr int blockSizes[] = { 37, 1024, 1, 257, 73, 511, 8, 191 };
    int position = 0;
    int blockIndex = 0;
    while (position < samples)
    {
        const int block = std::min (samples - position,
                                    blockSizes[static_cast<size_t> (blockIndex) % std::size (blockSizes)]);
        juce::AudioBuffer<float> buffer (channels, block);
        for (int channel = 0; channel < channels; ++channel)
            buffer.copyFrom (channel, 0, input[static_cast<size_t> (channel)].data() + position, block);

        if (automate)
        {
            auto params = initial;
            const int stage = (position / static_cast<int> (sampleRate * 0.09)) % 8;
            params.shiftSemitones = stage % 2 == 0 ? -12.0f : 12.0f;
            params.focus = stage % 3 == 0 ? 0.0f : 1.0f;
            params.bite = stage % 2 == 0 ? 1.0f : 0.0f;
            params.drive = stage % 3 == 1 ? 1.0f : 0.0f;
            params.mix = stage % 4 == 0 ? 0.0f : 1.0f;
            params.subSafe = stage % 2 == 0;
            params.subCutHz = stage % 2 == 0 ? 55.0f : 240.0f;
            engine.setParameters (params);
        }

        engine.processBlock (buffer);
        for (int channel = 0; channel < channels; ++channel)
            std::copy_n (buffer.getReadPointer (channel), block,
                         result.audio[static_cast<size_t> (channel)].begin() + position);
        position += block;
        ++blockIndex;
    }
    return result;
}

double rms (const std::vector<float>& audio, int start, int end)
{
    double sum = 0.0;
    for (int i = start; i < end; ++i)
        sum += static_cast<double> (audio[static_cast<size_t> (i)]) * audio[static_cast<size_t> (i)];
    return std::sqrt (sum / std::max (1, end - start));
}

double differenceRms (const std::vector<float>& first, const std::vector<float>& second,
                      int offset, int start, int end)
{
    double sum = 0.0;
    for (int i = start; i < end; ++i)
    {
        const double difference = static_cast<double> (first[static_cast<size_t> (i)])
                                - second[static_cast<size_t> (i - offset)];
        sum += difference * difference;
    }
    return std::sqrt (sum / std::max (1, end - start));
}

double toneAmplitude (const std::vector<float>& audio, double sampleRate, double frequency,
                      int start, int end)
{
    double cosine = 0.0;
    double sine = 0.0;
    for (int i = start; i < end; ++i)
    {
        const double phase = 2.0 * pi * frequency * i / sampleRate;
        cosine += audio[static_cast<size_t> (i)] * std::cos (phase);
        sine += audio[static_cast<size_t> (i)] * std::sin (phase);
    }
    return 2.0 * std::hypot (cosine, sine) / std::max (1, end - start);
}

double estimateBassPitch (const std::vector<float>& audio, double sampleRate, int start, int end)
{
    double bestAmplitude = 0.0;
    double bestFrequency = 0.0;
    for (double frequency = 85.0; frequency <= 99.0; frequency += 0.1)
    {
        const double amplitude = toneAmplitude (audio, sampleRate, frequency, start, end);
        if (amplitude > bestAmplitude)
        {
            bestAmplitude = amplitude;
            bestFrequency = frequency;
        }
    }
    return bestFrequency;
}

bool checkNeutral (double sampleRate, int channels)
{
    shifty::FormantEngine latencyProbe;
    latencyProbe.prepare (sampleRate, 1024, channels);
    const int latency = latencyProbe.getLatencySamples();
    auto input = makeProbe (sampleRate, channels, latency + latency / 2);
    auto output = runEngine (input, sampleRate, {});
    double largestResidual = 0.0;
    double residualEnergy = 0.0;
    int count = 0;

    for (int channel = 0; channel < channels; ++channel)
    {
        for (int i = 0; i < sampleCount (input); ++i)
        {
            const double expected = i >= latency ? input[static_cast<size_t> (channel)][static_cast<size_t> (i - latency)] : 0.0;
            const double difference = output.audio[static_cast<size_t> (channel)][static_cast<size_t> (i)] - expected;
            largestResidual = std::max (largestResidual, std::abs (difference));
            residualEnergy += difference * difference;
            ++count;
        }
    }

    const double residualRms = std::sqrt (residualEnergy / count);
    std::cout << "neutral " << sampleRate << " Hz " << channels << " ch: latency " << latency
              << ", residual rms " << residualRms << ", max " << largestResidual << '\n';
    return output.latency == latency && residualRms < 2.0e-5 && largestResidual < 1.0e-4;
}

bool checkSilence (double sampleRate)
{
    shifty::EngineParams params;
    params.shiftSemitones = 12.0f;
    params.focus = params.bite = params.drive = params.mix = 1.0f;
    params.subSafe = false;
    auto output = runEngine (makeAudio (2, static_cast<int> (sampleRate * 0.3)), sampleRate, params);
    float peak = 0.0f;
    for (const auto& channel : output.audio)
        for (float sample : channel)
            peak = std::max (peak, std::abs (sample));
    std::cout << "silence " << sampleRate << " Hz: peak " << peak << '\n';
    return peak == 0.0f;
}

bool checkShiftAndPitch (double sampleRate)
{
    shifty::FormantEngine latencyProbe;
    latencyProbe.prepare (sampleRate, 1024, 1);
    const int latency = latencyProbe.getLatencySamples();
    auto input = makeBass (sampleRate, 1.8, 1, latency + latency / 2, false);

    shifty::EngineParams params;
    params.shiftSemitones = 8.0f;
    params.focus = 0.72f;
    params.bite = 0.72f;
    params.drive = 0.0f;
    params.subSafe = true;
    auto output = runEngine (input, sampleRate, params);

    const int sourceStart = static_cast<int> (0.35 * sampleRate);
    const int sourceEnd = static_cast<int> (1.45 * sampleRate);
    const double inputPitch = estimateBassPitch (input[0], sampleRate, sourceStart, sourceEnd);
    const double outputPitch = estimateBassPitch (output.audio[0], sampleRate,
                                                  sourceStart + latency, sourceEnd + latency);
    const double changedRms = differenceRms (output.audio[0], input[0], latency,
                                              sourceStart + latency, sourceEnd + latency);
    const double referenceRms = rms (input[0], sourceStart, sourceEnd);
    const double changeFraction = changedRms / std::max (referenceRms, 1.0e-9);

    const double fourthBefore = toneAmplitude (input[0], sampleRate, 92.0 * 4.0, sourceStart, sourceEnd);
    const double tenthBefore = toneAmplitude (input[0], sampleRate, 92.0 * 10.0, sourceStart, sourceEnd);
    const double fourthAfter = toneAmplitude (output.audio[0], sampleRate, 92.0 * 4.0,
                                              sourceStart + latency, sourceEnd + latency);
    const double tenthAfter = toneAmplitude (output.audio[0], sampleRate, 92.0 * 10.0,
                                             sourceStart + latency, sourceEnd + latency);
    const double formantContrastDb = 20.0 * std::log10 (std::max (1.0e-9, tenthAfter / fourthAfter)
                                                        / std::max (1.0e-9, tenthBefore / fourthBefore));

    std::cout << "shift " << sampleRate << " Hz: pitch " << inputPitch << " -> " << outputPitch
              << " Hz, normalized change " << changeFraction
              << ", 10th/4th harmonic change " << formantContrastDb << " dB\n";
    return std::abs (inputPitch - 92.0) < 0.4
        && std::abs (outputPitch - inputPitch) < 0.4
        && changeFraction > 0.025;
}

bool checkSubSafe (double sampleRate, double frequency, float shift, float drive)
{
    shifty::FormantEngine latencyProbe;
    latencyProbe.prepare (sampleRate, 1024, 1);
    const int latency = latencyProbe.getLatencySamples();
    const int active = static_cast<int> (sampleRate * 1.25);
    auto input = makeAudio (1, active + latency + latency / 2);
    for (int i = 0; i < active; ++i)
        input[0][static_cast<size_t> (i)] = static_cast<float> (0.42 * std::sin (2.0 * pi * frequency * i / sampleRate));

    shifty::EngineParams params;
    params.shiftSemitones = shift;
    params.focus = params.bite = params.mix = 1.0f;
    params.drive = drive;
    params.subSafe = true;
    params.subCutHz = 120.0f;
    auto output = runEngine (input, sampleRate, params);
    const int sourceStart = static_cast<int> (0.30 * sampleRate);
    const int sourceEnd = static_cast<int> (1.00 * sampleRate);
    const double before = toneAmplitude (input[0], sampleRate, frequency, sourceStart, sourceEnd);
    const double after = toneAmplitude (output.audio[0], sampleRate, frequency,
                                        sourceStart + latency, sourceEnd + latency);
    const double changeDb = 20.0 * std::log10 (std::max (after, 1.0e-9) / std::max (before, 1.0e-9));
    std::cout << "sub safe " << sampleRate << " Hz: " << frequency << " Hz, shift " << shift
              << ", drive " << drive << ", change " << changeDb << " dB\n";
    return std::abs (changeDb) < 0.2;
}

bool checkAutomation (double sampleRate)
{
    shifty::FormantEngine latencyProbe;
    latencyProbe.prepare (sampleRate, 1024, 2);
    const int latency = latencyProbe.getLatencySamples();
    auto input = makeBass (sampleRate, 1.4, 2, latency + latency / 2, true);
    auto output = runEngine (input, sampleRate, {}, true);
    double peak = 0.0;
    double energy = 0.0;
    bool finite = true;
    for (const auto& channel : output.audio)
    {
        for (float sample : channel)
        {
            finite = finite && std::isfinite (sample);
            peak = std::max (peak, static_cast<double> (std::abs (sample)));
            energy += static_cast<double> (sample) * sample;
        }
    }
    const double outputRms = std::sqrt (energy / (2 * sampleCount (output.audio)));
    std::cout << "automation " << sampleRate << " Hz stereo: peak " << peak
              << ", rms " << outputRms << ", finite " << std::boolalpha << finite << '\n';
    return finite && peak < 8.0 && outputRms > 0.01;
}

bool checkStereoIsolation (double sampleRate)
{
    shifty::FormantEngine latencyProbe;
    latencyProbe.prepare (sampleRate, 1024, 2);
    auto input = makeBass (sampleRate, 0.8, 2, latencyProbe.getLatencySamples() * 2, false);
    std::fill (input[1].begin(), input[1].end(), 0.0f);
    shifty::EngineParams params;
    params.shiftSemitones = 7.0f;
    params.drive = 0.5f;
    auto output = runEngine (input, sampleRate, params);
    double rightPeak = 0.0;
    for (float sample : output.audio[1])
        rightPeak = std::max (rightPeak, static_cast<double> (std::abs (sample)));
    std::cout << "stereo isolation " << sampleRate << " Hz: right peak " << rightPeak << '\n';
    return rightPeak < 1.0e-7;
}

bool checkLinkedStereoAndMixedSub (double sampleRate)
{
    const int active = static_cast<int> (sampleRate * 1.0);
    auto input = makeAudio (2, active + 16384);
    for (int i = 0; i < active; ++i)
    {
        const double t = i / sampleRate;
        double sample = 0.3 * std::sin (2.0 * pi * 55.0 * t);
        for (int h = 1; h <= 32; ++h)
            sample += .07 * std::sin (2.0 * pi * 110.0 * h * t + h * .17) / std::sqrt (h);
        input[0][static_cast<size_t> (i)] = static_cast<float> (sample);
        input[1][static_cast<size_t> (i)] = static_cast<float> (sample * .37);
    }
    shifty::EngineParams params;
    params.shiftSemitones = -9.0f;
    params.focus = .9f;
    params.bite = 1.0f;
    const auto output = runEngine (input, sampleRate, params);
    const int start = static_cast<int> (sampleRate * .35);
    const int end = static_cast<int> (sampleRate * .85);
    const auto before = toneAmplitude (input[0], sampleRate, 55.0, start, end);
    const auto after = toneAmplitude (output.audio[0], sampleRate, 55.0,
                                      start + output.latency, end + output.latency);
    const auto subDb = 20.0 * std::log10 (after / before);
    double stereoResidual = 0.0;
    for (int i = start + output.latency; i < end + output.latency; ++i)
        stereoResidual = std::max (stereoResidual, std::abs (static_cast<double> (
            output.audio[1][static_cast<size_t> (i)] - .37f * output.audio[0][static_cast<size_t> (i)])));
    std::cout << "linked stereo " << sampleRate << " Hz: ratio residual " << stereoResidual
              << ", mixed-bass 55 Hz change " << subDb << " dB\n";
    return stereoResidual < .0001 && std::abs (subDb) < .2;
}

bool checkDryAtExtremeSettings (double sampleRate)
{
    auto input = makeProbe (sampleRate, 2, 16384);
    shifty::EngineParams params;
    params.shiftSemitones = 12.0f;
    params.focus = params.bite = params.drive = 1.0f;
    params.subSafe = false;
    params.mix = 0.0f;
    const auto output = runEngine (input, sampleRate, params);
    double residual = 0.0;
    for (int c = 0; c < 2; ++c)
        residual = std::max (residual, differenceRms (output.audio[static_cast<size_t> (c)],
            input[static_cast<size_t> (c)], output.latency, output.latency, sampleCount (input)));
    std::cout << "dry at extreme settings " << sampleRate << " Hz: residual " << residual << '\n';
    return residual < 1.0e-7;
}

// Motion offsets of zero must leave the static engine bit-identical.
bool checkZeroMotionIsStatic (double sampleRate)
{
    auto input = makeBass (sampleRate, 1.2, 2, 8192, true);
    shifty::EngineParams params;
    params.shiftSemitones = -5.0f;
    params.focus = 0.8f;
    params.bite = 0.9f;
    params.drive = 0.3f;
    params.vowel = true;
    auto render = [&] (bool zeroOffsets)
    {
        shifty::FormantEngine engine;
        engine.setParameters (params);
        engine.prepare (sampleRate, 512, 2);
        auto audio = input;
        std::vector<float> zeros (512, 0.0f);
        for (int position = 0; position < sampleCount (audio); position += 512)
        {
            const int count = std::min (512, sampleCount (audio) - position);
            float* channels[2] = { audio[0].data() + position, audio[1].data() + position };
            juce::AudioBuffer<float> buffer (channels, 2, count);
            engine.processBlock (buffer, zeroOffsets ? zeros.data() : nullptr);
        }
        return audio;
    };
    const auto withoutMotion = render (false);
    const auto zeroMotion = render (true);
    const bool identical = withoutMotion == zeroMotion;
    std::cout << "zero Motion offsets " << sampleRate << " Hz: bit-identical to no Motion " << std::boolalpha
              << identical << '\n';
    return identical;
}

// Linked level compensation follows the current frame instead of a slow
// follower: a SHIFT jump must not bump the level, and a large downward shift
// of a bright source must not lose level against the old +6 dB ceiling.
bool checkCompensationTransients (double sampleRate)
{
    auto resonant = makeAudio (1, static_cast<int> (sampleRate * 3.0));
    auto fm = makeAudio (1, static_cast<int> (sampleRate * 3.0));
    for (int i = 0; i < sampleCount (resonant); ++i)
    {
        const double t = i / sampleRate;
        double value = 0.0;
        for (int h = 1; h * 110.0 < 16000.0; ++h)
        {
            const double f = h * 110.0;
            const double db = 18.0 * std::exp (-0.5 * std::pow ((f - 700.0) / 90.0, 2.0))
                            + 12.0 * std::exp (-0.5 * std::pow ((f - 1800.0) / 170.0, 2.0));
            value += std::pow (10.0, db / 20.0) / h * std::sin (2.0 * pi * f * t);
        }
        resonant[0][static_cast<size_t> (i)] = static_cast<float> (0.02 * value);
        const double phase = 2.0 * pi * 110.0 * t;
        fm[0][static_cast<size_t> (i)] = static_cast<float> (0.25 * std::tanh (1.3 * std::sin (phase + 2.4 * std::sin (phase))
                                                                      + 0.35 * std::sin (3.0 * phase + 1.6 * std::sin (2.0 * phase))));
    }
    auto render = [&] (const Audio& input, float from, float to)
    {
        shifty::EngineParams params;
        params.shiftSemitones = from;
        params.focus = 0.7f;
        params.bite = 0.5f;
        shifty::FormantEngine engine;
        engine.setParameters (params);
        engine.prepare (sampleRate, 64, 1);
        const int latency = engine.getLatencySamples();
        auto audio = input;
        audio[0].resize (audio[0].size() + static_cast<size_t> (latency), 0.0f);
        for (int position = 0; position < sampleCount (audio); position += 64)
        {
            params.shiftSemitones = position < static_cast<int> (sampleRate) ? from : to;
            engine.setParameters (params);
            const int count = std::min (64, sampleCount (audio) - position);
            float* channels[1] = { audio[0].data() + position };
            juce::AudioBuffer<float> buffer (channels, 1, count);
            engine.processBlock (buffer);
        }
        audio[0].erase (audio[0].begin(), audio[0].begin() + latency);
        return audio[0];
    };
    auto levelDb = [] (const std::vector<float>& x, int start, int length)
    {
        return 20.0 * std::log10 (rms (x, start, start + length) + 1.0e-12);
    };
    bool passed = true;
    for (const auto& jump : { std::pair<float, float> { 0.0f, 8.0f }, { 0.0f, 12.0f }, { 2.0f, -12.0f }, { 0.0f, -8.0f } })
    {
        const auto output = render (resonant, jump.first, jump.second);
        const double steady = levelDb (output, static_cast<int> (2.0 * sampleRate), static_cast<int> (0.5 * sampleRate));
        double highest = -1.0e9, lowest = 1.0e9;
        const int window = static_cast<int> (0.01 * sampleRate);
        for (int s = static_cast<int> (sampleRate); s < static_cast<int> (1.4 * sampleRate); s += 64)
        {
            const double value = levelDb (output, s, window) - steady;
            highest = std::max (highest, value);
            lowest = std::min (lowest, value);
        }
        const bool ok = highest < 2.0 && lowest > -1.5;
        std::cout << "compensation " << sampleRate << " Hz: shift " << jump.first << " -> " << jump.second
                  << ", 10 ms level excursion " << highest << " / " << lowest << " dB\n";
        passed = passed && ok;
    }
    const auto fmDown = render (fm, -12.0f, -12.0f);
    const double fmChange = levelDb (fmDown, static_cast<int> (1.0 * sampleRate), static_cast<int> (1.5 * sampleRate))
                          - levelDb (fm[0], static_cast<int> (1.0 * sampleRate), static_cast<int> (1.5 * sampleRate));
    std::cout << "compensation " << sampleRate << " Hz: FM growl at -12 st keeps level within " << fmChange << " dB\n";
    // 0.4.2 lost about 2.8-3.1 dB here because compensation stopped at +6 dB.
    return passed && std::abs (fmChange) < 1.5;
}

bool writeWav (const juce::File& directory, const juce::String& name,
               const Audio& audio, double sampleRate)
{
    const auto file = directory.getChildFile (name);
    if (file.existsAsFile() && !file.deleteFile())
        return false;

    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (!stream)
        return false;

    juce::WavAudioFormat format;
    auto options = juce::AudioFormatWriterOptions{}
        .withSampleRate (sampleRate)
        .withNumChannels (static_cast<int> (audio.size()))
        .withBitsPerSample (32)
        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor (stream, options);
    if (!writer)
        return false;

    juce::AudioBuffer<float> buffer (static_cast<int> (audio.size()), sampleCount (audio));
    for (int channel = 0; channel < static_cast<int> (audio.size()); ++channel)
        buffer.copyFrom (channel, 0, audio[static_cast<size_t> (channel)].data(), sampleCount (audio));
    return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

bool renderDemos (const juce::String& path)
{
    const auto directory = juce::File (path);
    if (!directory.createDirectory())
        return false;

    constexpr double sampleRate = 48000.0;
    shifty::FormantEngine latencyProbe;
    latencyProbe.prepare (sampleRate, 1024, 2);
    const int latency = latencyProbe.getLatencySamples();
    const auto input = makeBass (sampleRate, 2.3, 2, latency + latency / 2, true);

    Audio alignedDry = makeAudio (2, sampleCount (input));
    for (int channel = 0; channel < 2; ++channel)
        std::copy_n (input[static_cast<size_t> (channel)].begin(), sampleCount (input) - latency,
                     alignedDry[static_cast<size_t> (channel)].begin() + latency);

    shifty::EngineParams low;
    low.shiftSemitones = -7.0f;
    low.focus = 0.68f;
    low.bite = 0.55f;
    low.drive = 0.22f;
    const auto lowOutput = runEngine (input, sampleRate, low);

    shifty::EngineParams high = low;
    high.shiftSemitones = 7.0f;
    high.focus = 0.78f;
    high.bite = 0.70f;
    const auto highOutput = runEngine (input, sampleRate, high);

    const bool okay = writeWav (directory, "dry-synthetic-reese.wav", alignedDry, sampleRate)
                   && writeWav (directory, "shifty-low-synthetic-reese.wav", lowOutput.audio, sampleRate)
                   && writeWav (directory, "shifty-high-synthetic-reese.wav", highOutput.audio, sampleRate);
    if (okay)
        std::cout << "Rendered three aligned synthetic bass examples to "
                  << directory.getFullPathName() << '\n';
    return okay;
}

// ---------------------------------------------------------------------------
// 0.5.0 engines: TALK, MOUTH, CLASSIC low latency, ADAA drive, modulators.

std::vector<float> bandLimitedSaw (double sampleRate, double f0, int samples, float amplitude)
{
    std::vector<float> result (static_cast<size_t> (samples), 0.0f);
    const int harmonics = static_cast<int> (std::min (0.45 * sampleRate, 12000.0) / f0);
    for (int i = 0; i < samples; ++i)
    {
        const double phase = 2.0 * pi * f0 * i / sampleRate;
        double value = 0.0;
        for (int h = 1; h <= harmonics; ++h)
            value += std::sin (h * phase) / h;
        result[static_cast<size_t> (i)] = static_cast<float> (amplitude * 2.0 / pi * value);
    }
    return result;
}

/** Spectral centroid in Hz over 100 Hz .. 10 kHz (Hann window, 16384 points). */
double spectralCentroid (const std::vector<float>& audio, double sampleRate, int start)
{
    constexpr int order = 14;
    constexpr int size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data (static_cast<size_t> (2 * size), 0.0f);
    for (int i = 0; i < size && start + i < static_cast<int> (audio.size()); ++i)
        data[static_cast<size_t> (i)] = audio[static_cast<size_t> (start + i)]
            * static_cast<float> (0.5 - 0.5 * std::cos (2.0 * pi * i / size));
    fft.performFrequencyOnlyForwardTransform (data.data());
    double weighted = 0.0, total = 0.0;
    for (int bin = 1; bin < size / 2; ++bin)
    {
        const double frequency = bin * sampleRate / size;
        if (frequency < 100.0 || frequency > 10000.0) continue;
        const double power = static_cast<double> (data[static_cast<size_t> (bin)]) * data[static_cast<size_t> (bin)];
        weighted += power * frequency;
        total += power;
    }
    return total > 0.0 ? weighted / total : 0.0;
}

double semitonesBetween (double from, double to) { return 12.0 * std::log2 (std::max (1.0e-9, to) / std::max (1.0e-9, from)); }

/** Third-octave band levels (dB) from 100 Hz to 5 kHz (Hann window, 16384 points). */
std::vector<double> thirdOctaveLevels (const std::vector<float>& audio, double sampleRate, int start)
{
    constexpr int order = 14;
    constexpr int size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data (static_cast<size_t> (2 * size), 0.0f);
    for (int i = 0; i < size && start + i < static_cast<int> (audio.size()); ++i)
        data[static_cast<size_t> (i)] = audio[static_cast<size_t> (start + i)]
            * static_cast<float> (0.5 - 0.5 * std::cos (2.0 * pi * i / size));
    fft.performFrequencyOnlyForwardTransform (data.data());
    std::vector<double> levels;
    for (double centre = 100.0; centre <= 5000.0; centre *= std::pow (2.0, 1.0 / 3.0))
    {
        const double low = centre * std::pow (2.0, -1.0 / 6.0), high = centre * std::pow (2.0, 1.0 / 6.0);
        double power = 1.0e-20;
        for (int bin = static_cast<int> (low * size / sampleRate) + 1; bin <= static_cast<int> (high * size / sampleRate); ++bin)
            power += static_cast<double> (data[static_cast<size_t> (bin)]) * data[static_cast<size_t> (bin)];
        levels.push_back (10.0 * std::log10 (power));
    }
    return levels;
}

double bandDistanceDb (const std::vector<double>& first, const std::vector<double>& second)
{
    // RMS difference after removing the overall level difference.
    double mean = 0.0;
    for (size_t i = 0; i < first.size(); ++i) mean += second[i] - first[i];
    mean /= static_cast<double> (first.size());
    double sum = 0.0;
    for (size_t i = 0; i < first.size(); ++i) sum += std::pow (second[i] - first[i] - mean, 2.0);
    return std::sqrt (sum / static_cast<double> (first.size()));
}

template <typename Engine>
std::vector<float> runMono (Engine& engine, const std::vector<float>& input, int block,
                            const std::vector<float>* shiftMod = nullptr, const std::vector<float>* morphMod = nullptr)
{
    std::vector<float> output (input.size());
    for (size_t position = 0; position < input.size(); position += static_cast<size_t> (block))
    {
        const int count = static_cast<int> (std::min (input.size() - position, static_cast<size_t> (block)));
        juce::AudioBuffer<float> buffer (1, count);
        buffer.copyFrom (0, 0, input.data() + position, count);
        shifty::Modulation modulation;
        modulation.shift = shiftMod != nullptr ? shiftMod->data() + position : nullptr;
        modulation.morph = morphMod != nullptr ? morphMod->data() + position : nullptr;
        engine.processBlock (buffer, modulation);
        std::copy_n (buffer.getReadPointer (0), count, output.begin() + static_cast<std::ptrdiff_t> (position));
    }
    return output;
}

std::vector<float> runClassic (shifty::FormantEngine& engine, const std::vector<float>& input, int block)
{
    std::vector<float> output (input.size());
    for (size_t position = 0; position < input.size(); position += static_cast<size_t> (block))
    {
        const int count = static_cast<int> (std::min (input.size() - position, static_cast<size_t> (block)));
        juce::AudioBuffer<float> buffer (1, count);
        buffer.copyFrom (0, 0, input.data() + position, count);
        engine.processBlock (buffer);
        std::copy_n (buffer.getReadPointer (0), count, output.begin() + static_cast<std::ptrdiff_t> (position));
    }
    return output;
}

std::vector<float> talkRender (double sampleRate, const std::vector<float>& input, shifty::EngineParams params, int block = 256,
                               const std::vector<float>* morphMod = nullptr)
{
    shifty::TalkEngine engine;
    engine.prepare (sampleRate, 1024, 1);
    engine.setParameters (params);
    engine.reset();
    return runMono (engine, input, block, nullptr, morphMod);
}

bool checkTalk (double sampleRate)
{
    bool ok = true;
    const int samples = static_cast<int> (sampleRate * 1.2);
    const auto saw = bandLimitedSaw (sampleRate, 55.0, samples, 0.4f);
    const int start = static_cast<int> (sampleRate * 0.6);
    shifty::EngineParams base;
    base.focus = 0.55f;
    base.bite = 0.35f;
    base.vowelMorph = 2.0f;
    const double sawCentroid = spectralCentroid (saw, sampleRate, start);
    std::array<double, 3> centroid {};
    const float shifts[] = { -6.0f, 0.0f, 6.0f };
    for (size_t i = 0; i < 3; ++i)
    {
        auto params = base;
        params.shiftSemitones = shifts[i];
        centroid[i] = spectralCentroid (talkRender (sampleRate, saw, params), sampleRate, start);
    }
    // Vowels: every pair of the five differs clearly in third-octave shape; EE is the brightest.
    std::array<std::vector<double>, 5> vowelBands;
    for (int v = 0; v < 5; ++v)
    {
        auto params = base;
        params.vowelMorph = static_cast<float> (v);
        vowelBands[static_cast<size_t> (v)] = thirdOctaveLevels (talkRender (sampleRate, saw, params), sampleRate, start);
    }
    double closestPair = 1.0e9;
    for (size_t a = 0; a < 5; ++a)
        for (size_t b = a + 1; b < 5; ++b)
            closestPair = std::min (closestPair, bandDistanceDb (vowelBands[a], vowelBands[b]));
    const double vowelSpan = bandDistanceDb (vowelBands[0], vowelBands[4]);
    const double down = semitonesBetween (centroid[1], centroid[0]);
    const double up = semitonesBetween (centroid[1], centroid[2]);
    ok = ok && down < -3.0 && up > 3.0 && vowelSpan > 4.0 && closestPair > 2.0;

    // Extremes stay finite and bounded; silence stays silent.
    float peak = 0.0f;
    bool finite = true;
    for (const float shift : { -12.0f, 12.0f })
        for (const float amount : { 0.0f, 1.0f })
        {
            auto params = base;
            params.shiftSemitones = shift;
            params.focus = amount;
            params.bite = 1.0f;
            params.drive = 1.0f;
            params.character = 1.0f;
            params.voiceSet = amount > 0.5f ? 1 : 2;
            for (const float sample : talkRender (sampleRate, saw, params))
            {
                finite = finite && std::isfinite (sample);
                peak = std::max (peak, std::abs (sample));
            }
        }
    auto loud = base;
    loud.bite = loud.drive = loud.character = 1.0f;
    float silencePeak = 0.0f;
    for (const float sample : talkRender (sampleRate, std::vector<float> (static_cast<size_t> (samples), 0.0f), loud))
        silencePeak = std::max (silencePeak, std::abs (sample));
    ok = ok && finite && peak < 2.0f && silencePeak < 1.0e-6f;

    // SUB SAFE: a 40 Hz sine passes the split untouched in level.
    std::vector<float> sub (static_cast<size_t> (samples));
    for (int i = 0; i < samples; ++i)
        sub[static_cast<size_t> (i)] = 0.4f * static_cast<float> (std::sin (2.0 * pi * 40.0 * i / sampleRate));
    auto safe = base;
    safe.shiftSemitones = -12.0f;
    safe.bite = 1.0f;
    const auto subOut = talkRender (sampleRate, sub, safe);
    const double subGainDb = 20.0 * std::log10 (toneAmplitude (subOut, sampleRate, 40.0, start, samples)
                                                / toneAmplitude (sub, sampleRate, 40.0, start, samples));
    ok = ok && std::abs (subGainDb) < 0.5;

    // Per-sample vowel modulation is independent of the host block size.
    std::vector<float> morph (static_cast<size_t> (samples));
    for (int i = 0; i < samples; ++i)
        morph[static_cast<size_t> (i)] = 2.0f * static_cast<float> (std::sin (2.0 * pi * 7.0 * i / sampleRate));
    const auto small = talkRender (sampleRate, saw, base, 37, &morph);
    const auto large = talkRender (sampleRate, saw, base, 1024, &morph);
    double worstBlock = 0.0;
    for (size_t i = 0; i < small.size(); ++i)
        worstBlock = std::max (worstBlock, static_cast<double> (std::abs (small[i] - large[i])));
    ok = ok && worstBlock < 1.0e-5;

    std::cout << "TALK " << sampleRate << " Hz: latency 0, saw centroid " << sawCentroid << " Hz; SHIFT -6/0/+6 centroid "
              << centroid[0] << " / " << centroid[1] << " / " << centroid[2] << " Hz (" << down << ", +" << up
              << " st); vowel shape distance OO-EE " << vowelSpan << " dB, closest pair " << closestPair << " dB; extremes peak " << peak << ", silence " << silencePeak
              << "; 40 Hz sub " << subGainDb << " dB; block 37 vs 1024 with vowel modulation max " << worstBlock
              << (ok ? "" : "  FAILED") << '\n';
    return ok;
}

bool checkMouth (double sampleRate)
{
    bool ok = true;
    const int tail = shifty::MouthEngine::latencyFor (sampleRate);
    auto bass = makeBass (sampleRate, 1.4, 1, tail, false)[0];
    const int start = static_cast<int> (sampleRate * 0.5) + tail;
    const int end = static_cast<int> (sampleRate * 1.1) + tail;
    std::array<double, 3> centroid {}, pitch {};
    float tracked = 0.0f, confidence = 0.0f;
    int latency = 0;
    const float shifts[] = { -6.0f, 0.0f, 6.0f };
    for (size_t i = 0; i < 3; ++i)
    {
        shifty::MouthEngine engine;
        engine.prepare (sampleRate, 1024, 1);
        shifty::EngineParams params;
        params.shiftSemitones = shifts[i];
        params.focus = 0.6f;
        params.bite = 0.2f;
        engine.setParameters (params);
        engine.reset();
        latency = engine.getLatencySamples();
        // Stop mid-note to read the tracker.
        std::vector<float> head (bass.begin(), bass.begin() + static_cast<std::ptrdiff_t> (sampleRate));
        auto out = runMono (engine, head, 256);
        tracked = engine.trackedPitch();
        confidence = engine.trackingConfidence();
        std::vector<float> rest (bass.begin() + static_cast<std::ptrdiff_t> (sampleRate), bass.end());
        const auto restOut = runMono (engine, rest, 256);
        out.insert (out.end(), restOut.begin(), restOut.end());
        centroid[i] = spectralCentroid (out, sampleRate, start);
        pitch[i] = estimateBassPitch (out, sampleRate, start, end);
    }
    const double inputPitch = estimateBassPitch (bass, sampleRate, start - tail, end - tail);
    const double down = semitonesBetween (centroid[1], centroid[0]);
    const double up = semitonesBetween (centroid[1], centroid[2]);
    bool pitchKept = true;
    for (const double p : pitch) pitchKept = pitchKept && std::abs (p - inputPitch) <= 0.25;
    ok = ok && latency == shifty::MouthEngine::latencyFor (sampleRate) && std::abs (tracked - 92.0f) < 0.92f
         && confidence > 0.8f && pitchKept && down < -2.5 && up > 2.5;

    // Unvoiced input: finite, bounded, low confidence, and the fallback keeps working.
    shifty::MouthEngine engine;
    engine.prepare (sampleRate, 1024, 1);
    shifty::EngineParams params;
    params.shiftSemitones = 7.0f;
    params.bite = 1.0f;
    params.drive = 1.0f;
    params.character = 1.0f;
    engine.setParameters (params);
    engine.reset();
    juce::Random random (42);
    std::vector<float> noise (static_cast<size_t> (sampleRate));
    for (auto& sample : noise) sample = 0.2f * (random.nextFloat() * 2.0f - 1.0f);
    float peak = 0.0f;
    bool finite = true;
    for (const float sample : runMono (engine, noise, 256))
    {
        finite = finite && std::isfinite (sample);
        peak = std::max (peak, std::abs (sample));
    }
    ok = ok && finite && peak < 1.5f && engine.trackingConfidence() < 0.5f;

    std::cout << "MOUTH " << sampleRate << " Hz: latency " << latency << ", tracked " << tracked << " Hz (confidence "
              << confidence << "); pitch in " << inputPitch << ", out " << pitch[0] << " / " << pitch[1] << " / "
              << pitch[2] << " Hz; centroid " << down << " / +" << up << " st at SHIFT -6/+6; noise peak " << peak
              << ", confidence " << engine.trackingConfidence() << (ok ? "" : "  FAILED") << '\n';
    return ok;
}

/** Frame-rate warble of a steady 41.2 Hz resonant saw through CLASSIC at SHIFT +6: the median, over
    harmonics 3..40, of each harmonic's level standard deviation (dB) across one-period windows. */
double classicWarble (shifty::FormantEngine::Mode mode, bool lowLatency, int& latency)
{
    constexpr double sampleRate = 48000.0;
    constexpr int period = 1165; // 41.2 Hz with a whole-sample period
    const double f0 = sampleRate / period;
    const int samples = static_cast<int> (sampleRate * 3.0);
    std::vector<float> input (static_cast<size_t> (samples), 0.0f);
    const auto bell = [] (double f, double c, double w) { const double d = (f - c) / w; return std::exp (-0.5 * d * d); };
    const int harmonics = static_cast<int> (18000.0 / f0);
    std::vector<double> amplitude (static_cast<size_t> (harmonics + 1), 0.0);
    for (int h = 1; h <= harmonics; ++h)
        amplitude[static_cast<size_t> (h)] = std::pow (10.0, (18.0 * bell (h * f0, 700.0, 90.0) + 12.0 * bell (h * f0, 1800.0, 170.0)) / 20.0) / h;
    double power = 0.0;
    for (int i = 0; i < samples; ++i)
    {
        double value = 0.0;
        for (int h = 1; h <= harmonics; ++h)
            value += amplitude[static_cast<size_t> (h)] * std::sin (h * 2.0 * pi * f0 * i / sampleRate);
        value *= std::min (1.0, i / (0.01 * sampleRate));
        input[static_cast<size_t> (i)] = static_cast<float> (value);
        power += value * value;
    }
    const double gain = 0.15 / std::sqrt (power / samples);
    for (auto& sample : input) sample = static_cast<float> (sample * gain);

    shifty::FormantEngine engine;
    engine.configure (mode, lowLatency);
    shifty::EngineParams params;
    params.shiftSemitones = 6.0f;
    params.focus = 0.55f;
    params.bite = 0.35f;
    engine.setParameters (params);
    engine.prepare (sampleRate, 64, 1);
    latency = engine.getLatencySamples();
    const auto out = runClassic (engine, input, 64);
    std::vector<double> deviations;
    for (int h = 3; h <= 40; ++h)
    {
        std::vector<double> levels;
        for (int s = static_cast<int> (0.8 * sampleRate); s + period < static_cast<int> (2.6 * sampleRate); s += 64)
            levels.push_back (20.0 * std::log10 (toneAmplitude (out, sampleRate, h * f0, s, s + period) + 1.0e-12));
        double mean = 0.0;
        for (const double level : levels) mean += level;
        mean /= static_cast<double> (levels.size());
        double variance = 0.0;
        for (const double level : levels) variance += (level - mean) * (level - mean);
        deviations.push_back (std::sqrt (variance / static_cast<double> (levels.size())));
    }
    std::sort (deviations.begin(), deviations.end());
    return deviations[deviations.size() / 2];
}

bool checkClassicLowLatency()
{
    int latencyOriginal = 0, latencyLatest = 0, latencyLow = 0;
    const double original = classicWarble (shifty::FormantEngine::Mode::original042, false, latencyOriginal);
    const double latest = classicWarble (shifty::FormantEngine::Mode::latest, false, latencyLatest);
    const double low = classicWarble (shifty::FormantEngine::Mode::latest, true, latencyLow);

    // Low latency still moves the formants.
    constexpr double sampleRate = 48000.0;
    auto bass = makeBass (sampleRate, 1.2, 1, 4096, false)[0];
    std::array<double, 2> centroid {};
    for (size_t i = 0; i < 2; ++i)
    {
        shifty::FormantEngine engine;
        engine.configure (shifty::FormantEngine::Mode::latest, true);
        shifty::EngineParams params;
        params.shiftSemitones = i == 0 ? 0.0f : 6.0f;
        params.focus = 0.6f;
        params.bite = 0.4f;
        engine.setParameters (params);
        engine.prepare (sampleRate, 1024, 1);
        centroid[i] = spectralCentroid (runClassic (engine, bass, 256), sampleRate, static_cast<int> (sampleRate * 0.4));
    }
    const double moved = semitonesBetween (centroid[0], centroid[1]);
    const bool ok = latencyLow == latencyLatest / 2 && latencyLow == shifty::FormantEngine::latencyFor (sampleRate, true)
                 && latencyOriginal == latencyLatest && latest < 0.5 && low < 0.5 && original > 2.0 * latest && moved > 2.0;
    std::cout << "CLASSIC 41.2 Hz resonant saw at +6 st, warble (median harmonic level std): original 0.4.2 " << original << " dB (latency "
              << latencyOriginal << "), latest " << latest << " dB (" << latencyLatest << "), low latency " << low
              << " dB (" << latencyLow << "); low latency SHIFT +6 moves the centroid +" << moved << " st"
              << (ok ? "" : "  FAILED") << '\n';
    return ok;
}

/** Aliased power (dB re the fundamental) of DRIVE at full amount on a 4.1 kHz sine at 48 kHz. */
double aliasLevel (bool antiAliased, double frequency)
{
    constexpr double sampleRate = 48000.0;
    constexpr int order = 15;
    constexpr int size = 1 << order;
    shifty::AdaaDrive drive;
    std::vector<float> data (static_cast<size_t> (2 * size), 0.0f);
    for (int i = -64; i < size; ++i)
    {
        const float x = 0.9f * static_cast<float> (std::sin (2.0 * pi * frequency * i / sampleRate));
        const float y = antiAliased ? drive.process (x, 1.0f) : shifty::originalDrive (x, 1.0f);
        if (i < 0) continue;
        // Blackman-Harris window
        const double t = 2.0 * pi * i / size;
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2.0 * t) - 0.01168 * std::cos (3.0 * t);
        data[static_cast<size_t> (i)] = static_cast<float> (y * w);
    }
    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (data.data());
    double harmonicPower = 0.0, aliasPower = 0.0, fundamental = 0.0;
    for (int bin = 2; bin < size / 2; ++bin)
    {
        const double f = bin * sampleRate / size;
        const double power = static_cast<double> (data[static_cast<size_t> (bin)]) * data[static_cast<size_t> (bin)];
        const double harmonic = std::round (f / frequency);
        const bool isHarmonic = harmonic >= 1.0 && std::abs (f - harmonic * frequency) < 6.0 * sampleRate / size;
        if (isHarmonic) harmonicPower += power;
        else aliasPower += power;
        if (std::abs (f - frequency) < 6.0 * sampleRate / size) fundamental += power;
    }
    juce::ignoreUnused (harmonicPower);
    return 10.0 * std::log10 (aliasPower / fundamental);
}

bool checkAdaaDrive()
{
    bool ok = true;
    const double naive41 = aliasLevel (false, 4100.0), adaa41 = aliasLevel (true, 4100.0);
    const double naive73 = aliasLevel (false, 7300.0), adaa73 = aliasLevel (true, 7300.0);
    ok = ok && adaa41 < naive41 - 6.0 && adaa73 < naive73 - 6.0;

    // Zero drive is bit-transparent; at low frequency ADAA matches the 0.4.2 curve closely.
    shifty::AdaaDrive zero, low;
    bool transparent = true;
    double difference = 0.0, reference = 0.0;
    for (int i = 0; i < 48000; ++i)
    {
        const float x = 0.8f * static_cast<float> (std::sin (2.0 * pi * 55.0 * i / 48000.0));
        transparent = transparent && juce::exactlyEqual (zero.process (x, 0.0f), x);
        const float y = low.process (x, 0.6f);
        const float expected = shifty::originalDrive (x, 0.6f);
        difference += static_cast<double> (y - expected) * (y - expected);
        reference += static_cast<double> (expected) * expected;
    }
    const double relative = std::sqrt (difference / reference);
    ok = ok && transparent && relative < 0.01;
    std::cout << "DRIVE (ADAA): aliasing at full drive, 4.1 kHz sine " << naive41 << " -> " << adaa41
              << " dB, 7.3 kHz sine " << naive73 << " -> " << adaa73 << " dB re fundamental; 55 Hz difference to the 0.4.2 curve "
              << 100.0 * relative << "%; zero drive transparent " << (transparent ? "yes" : "no")
              << (ok ? "" : "  FAILED") << '\n';
    return ok;
}

bool checkModulators()
{
    constexpr double sampleRate = 48000.0;
    shifty::EnvelopeFollower follower;
    follower.prepare (sampleRate);
    float loud = 0.0f, quiet = 0.0f;
    for (int i = 0; i < 24000; ++i)
        loud = follower.process (0.5f * static_cast<float> (std::sin (2.0 * pi * 110.0 * i / sampleRate)), 5.0f, 150.0f);
    for (int i = 0; i < 48000; ++i)
        quiet = follower.process (0.004f * static_cast<float> (std::sin (2.0 * pi * 110.0 * i / sampleRate)), 5.0f, 150.0f);

    shifty::YoiSweep yoi;
    yoi.prepare (sampleRate);
    int triggers = 0;
    float atHalf = -1.0f, atEnd = -1.0f;
    int sinceOnset = 0;
    for (int i = 0; i < static_cast<int> (sampleRate * 1.6); ++i)
    {
        const double t = i / sampleRate;
        const bool on = std::fmod (t, 0.4) < 0.3;
        // Notes alternate between -8 and -14 dBFS after 100 ms gaps; a 6 Hz tremolo inside a note must not retrigger.
        const float noteLevel = static_cast<int> (t / 0.4) % 2 == 0 ? 0.4f : 0.2f;
        const float level = on ? noteLevel * static_cast<float> (0.8 + 0.2 * std::sin (2.0 * pi * 6.0 * t)) : 0.0f;
        yoi.process (level * static_cast<float> (std::sin (2.0 * pi * 80.0 * t)), 200.0f);
        if (yoi.triggered()) { ++triggers; sinceOnset = 0; }
        else ++sinceOnset;
        if (triggers == 2 && sinceOnset == 4800) atHalf = yoi.rawPosition();
        if (triggers == 2 && sinceOnset == 9700) atEnd = yoi.rawPosition();
    }
    const bool ok = loud > 0.97f && quiet < 0.05f && triggers == 4 && std::abs (atHalf - 0.5f) < 0.02f
                 && juce::exactlyEqual (atEnd, 1.0f);
    std::cout << "ENV follower: -6 dBFS -> " << loud << ", -48 dBFS -> " << quiet << "; YOI: " << triggers
              << " onsets in 4 notes, sweep at 100 ms " << atHalf << ", at 202 ms " << atEnd
              << (ok ? "" : "  FAILED") << '\n';
    return ok;
}

} // namespace

int main (int argc, char* argv[])
{
    juce::String renderPath;
    if (argc > 1)
    {
        if (argc != 3 || std::string (argv[1]) != "--render")
        {
            std::cerr << "Usage: ShiftyDspSmoke [--render <directory>]\n";
            return 2;
        }
        renderPath = juce::String::fromUTF8 (argv[2]);
    }

    std::cout << std::setprecision (6);
    bool passed = true;
    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        passed = checkNeutral (sampleRate, 1) && passed;
        passed = checkNeutral (sampleRate, 2) && passed;
        passed = checkSilence (sampleRate) && passed;
        passed = checkShiftAndPitch (sampleRate) && passed;
        for (double frequency : { 40.0, 55.0, 70.0 })
            passed = checkSubSafe (sampleRate, frequency, -12.0f, 0.0f) && passed;
        passed = checkSubSafe (sampleRate, 55.0, 12.0f, 0.0f) && passed;
        passed = checkSubSafe (sampleRate, 55.0, -12.0f, 1.0f) && passed;
        passed = checkSubSafe (sampleRate, 55.0, 0.0f, 1.0f) && passed;
        passed = checkAutomation (sampleRate) && passed;
        passed = checkStereoIsolation (sampleRate) && passed;
        passed = checkLinkedStereoAndMixedSub (sampleRate) && passed;
        passed = checkDryAtExtremeSettings (sampleRate) && passed;
        passed = checkZeroMotionIsStatic (sampleRate) && passed;
        passed = checkCompensationTransients (sampleRate) && passed;
        passed = checkTalk (sampleRate) && passed;
        passed = checkMouth (sampleRate) && passed;
    }
    passed = checkClassicLowLatency() && passed;
    passed = checkAdaaDrive() && passed;
    passed = checkModulators() && passed;

    if (!renderPath.isEmpty())
        passed = renderDemos (renderPath) && passed;

    std::cout << (passed ? "DSP smoke checks passed\n" : "DSP smoke checks failed\n");
    return passed ? 0 : 1;
}
