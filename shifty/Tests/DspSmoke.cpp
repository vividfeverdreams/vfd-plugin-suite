#include "../Source/Dsp/FormantEngine.h"

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
    }

    if (!renderPath.isEmpty())
        passed = renderDemos (renderPath) && passed;

    std::cout << (passed ? "DSP smoke checks passed\n" : "DSP smoke checks failed\n");
    return passed ? 0 : 1;
}
