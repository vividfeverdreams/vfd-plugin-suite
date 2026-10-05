// Build without JUCE:
// clang++ -std=c++20 -O2 -Wall -Wextra -I Source/Dsp Source/Dsp/YoiVoice.cpp \
//   Source/Dsp/tests/YoiVoiceOfflineTest.cpp -o build/yoi-voice-test
// Run with: build/yoi-voice-test

#include "YoiVoice.h"

#include <algorithm>
#include <chrono>
#include <complex>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

using yoiboi::dsp::VoiceControls;
using yoiboi::dsp::YoiVoice;

namespace
{
void require (bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit (1);
    }
}

float checkedSample (YoiVoice& voice)
{
    const auto sample = voice.renderSample();
    require (std::isfinite (sample), "non-finite output");
    require (std::abs (sample) <= 0.98f, "output exceeded the final peak guard");
    return sample;
}

std::vector<float> renderAtBlockSize (int blockSize)
{
    YoiVoice voice;
    voice.prepare (48000.0);
    VoiceControls controls;
    controls.loop = true;
    std::vector<float> result (60000);
    for (int i = 0; i < static_cast<int> (result.size()); ++i)
    {
        if (i % blockSize == 0) voice.setControls (controls);
        if (i == 0) voice.noteOn (36, 0.85f, 0.0f);
        if (i == 8000) voice.noteOn (48, 0.85f, 50.0f, false);
        if (i == 20000) voice.setPitchBend (2.0f);
        if (i == 24000) voice.noteOff();
        result[static_cast<size_t> (i)] = checkedSample (voice);
    }
    return result;
}

double amplitudeAt (const std::vector<float>& samples, double frequency, double sampleRate)
{
    std::complex<double> sum {};
    constexpr double tau = 6.2831853071795864769;
    for (std::size_t i = 0; i < samples.size(); ++i)
        sum += static_cast<double> (samples[i]) * std::polar (1.0, -tau * frequency * i / sampleRate);
    return 2.0 * std::abs (sum) / static_cast<double> (samples.size());
}

std::vector<float> renderSteady (VoiceControls c, int note = 45)
{
    YoiVoice voice;
    voice.prepare (48000.0);
    voice.setControls (c);
    voice.reset(); // Start at the requested values, bypassing automation slew.
    voice.noteOn (note, 1.0f, 0.0f);
    for (int i = 0; i < 48000; ++i) checkedSample (voice);
    std::vector<float> result (48000);
    for (auto& sample : result) sample = checkedSample (voice);
    return result;
}

void testSubRoutingAndDecimation()
{
    yoiboi::dsp::HalfbandDecimator impulse4x, impulse2x;
    impulse4x.prepare();
    impulse2x.prepare();
    std::array<float, 64> impulseResponse {};
    for (std::size_t i = 0; i < impulseResponse.size(); ++i)
    {
        const auto a = impulse4x.process (0.0f, 0.0f);
        const auto b = impulse4x.process (0.0f, i == 0 ? 1.0f : 0.0f);
        impulseResponse[i] = impulse2x.process (a, b);
    }
    require (std::distance (impulseResponse.begin(), std::max_element (
                 impulseResponse.begin(), impulseResponse.end())) == YoiVoice::latencySamples(),
             "declared host latency does not match the oversampling filters");
    VoiceControls c;
    c.output = 0.8f;
    c.sub = 1.0f;
    c.mouth = 1.0f;
    c.startPose = c.bodyPose = c.endPose = 0;
    c.fmIndex = 3.0f;
    c.fmRatio = 2.0f;
    std::vector<float> cleanReference;
    double directThird = 0.0, drivenThird = 0.0;
    for (const auto dirt : { 0.0f, 1.0f })
    {
        c.dirt = dirt;
        c.subMode = 0;
        const auto noSub = renderSteady (c);
        c.subMode = 1;
        const auto withSub = renderSteady (c);
        std::vector<float> isolatedSub (noSub.size());
        for (std::size_t i = 0; i < noSub.size(); ++i)
            isolatedSub[i] = withSub[i] - noSub[i];
        const auto fundamental = amplitudeAt (isolatedSub, 55.0, 48000.0);
        require (fundamental > 0.22 && fundamental < 0.225, "direct sine lost level or entered the body high-pass");
        directThird = amplitudeAt (isolatedSub, 165.0, 48000.0) / fundamental;
        require (directThird < 0.00002, "direct sine contains measurable waveshaper harmonics");
        for (int harmonic = 2; harmonic <= 12; ++harmonic)
            require (amplitudeAt (isolatedSub, 55.0 * harmonic, 48000.0) / fundamental < 0.00002,
                     "body/sub intermodulation entered the direct sine");
        if (cleanReference.empty()) cleanReference = isolatedSub;
        else
            for (std::size_t i = 0; i < isolatedSub.size(); ++i)
                require (std::abs (isolatedSub[i] - cleanReference[i]) < 0.00002f,
                         "Dirt changed the Direct sub waveform");

        c.subMode = 0;
        c.sub = 0.0f;
        require (renderSteady (c) == noSub, "Off mode still depends on sub level");
        c.sub = 1.0f;
        c.subMode = 2;
        auto drivenSub = renderSteady (c);
        for (std::size_t i = 0; i < noSub.size(); ++i) drivenSub[i] -= noSub[i];
        drivenThird = amplitudeAt (drivenSub, 165.0, 48000.0)
                      / amplitudeAt (drivenSub, 55.0, 48000.0);
        if (dirt == 1.0f) require (drivenThird > 0.20, "Driven mode did not add controlled harmonics");
    }

    c.subMode = 0;
    c.dirt = 0.0f;
    c.fmIndex = 0.0f;
    c.lowCut = false;
    const auto fullBody = renderSteady (c, 33);
    c.lowCut = true;
    const auto filteredBody = renderSteady (c, 33);
    const auto bodyLowRatio = amplitudeAt (filteredBody, 55.0, 48000.0)
                             / amplitudeAt (fullBody, 55.0, 48000.0);
    require (bodyLowRatio < 0.065, "low cut did not remove body low-end with sub Off");

    double worstStopDb = -300.0;
    double minPass = 1.0, maxPass = 1.0;
    double worstAlignedError = 0.0;
    for (const double frequency : { 1000.0, 5000.0, 10000.0, 15000.0, 30000.0, 40000.0, 55000.0, 70000.0 })
    {
        yoiboi::dsp::HalfbandDecimator stage4x, stage2x;
        stage4x.prepare();
        stage2x.prepare();
        std::vector<float> result (48000);
        int sampleIndex = 0;
        for (int i = -1024; i < 48000; ++i)
        {
            std::array<float, 4> input {};
            for (auto& sample : input)
                sample = static_cast<float> (std::sin (6.2831853071795864769 * frequency
                                                      * sampleIndex++ / 192000.0));
            const auto a = stage4x.process (input[0], input[1]);
            const auto b = stage4x.process (input[2], input[3]);
            const auto sample = stage2x.process (a, b);
            if (i >= 0)
            {
                result[static_cast<std::size_t> (i)] = sample;
                if (frequency <= 15000.0)
                {
                    // Direct mode samples the fourth internal endpoint. Delay
                    // that identical sine by 24 native samples: this verifies
                    // actual phase alignment, including decimation phase.
                    const auto expected = std::sin (6.2831853071795864769 * frequency
                        * (sampleIndex - 1 - 4 * YoiVoice::latencySamples()) / 192000.0);
                    worstAlignedError = std::max (worstAlignedError, std::abs (sample - expected));
                    require (std::abs (sample - expected) < 0.0001,
                             "oversampled path is not aligned with the 24-sample direct delay");
                }
            }
        }
        auto aliasFrequency = std::fmod (frequency, 48000.0);
        if (aliasFrequency > 24000.0) aliasFrequency = 48000.0 - aliasFrequency;
        const auto amplitude = amplitudeAt (result, aliasFrequency, 48000.0);
        if (frequency <= 15000.0)
        {
            minPass = std::min (minPass, amplitude);
            maxPass = std::max (maxPass, amplitude);
            require (std::abs (amplitude - 1.0) < 0.001, "oversampling filter dulled the audible passband");
        }
        else
        {
            const auto rejectionDb = 20.0 * std::log10 (std::max (amplitude, 1.0e-15));
            worstStopDb = std::max (worstStopDb, rejectionDb);
            require (rejectionDb < -70.0, "oversampling filter did not suppress ultrasonic aliases");
        }
    }
    std::cout << "Sub routing: direct third harmonic=" << 20.0 * std::log10 (directThird)
              << " dBc; driven third=" << 20.0 * std::log10 (drivenThird)
              << " dBc; body low-cut at 55 Hz=" << 20.0 * std::log10 (bodyLowRatio)
              << " dB. FIR passband gain=" << minPass << ".." << maxPass
              << "; worst measured 30-70 kHz rejection=" << worstStopDb
              << " dB; 24-sample aligned sine max error=" << worstAlignedError << ".\n";
}
} // namespace

int main()
{
    testSubRoutingAndDecimation();
    int checkedRenders = 0;
    float peak = 0.0f;
    for (const double sampleRate : { 8000.0, 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        for (const int note : { 0, 24, 36, 48, 60, 96, 127 })
        {
            for (int variant = 0; variant < 4; ++variant)
            {
                YoiVoice voice;
                voice.prepare (sampleRate);
                VoiceControls controls;
                controls.startPose = variant % 5;
                controls.bodyPose = (variant + 2) % 5;
                controls.endPose = (variant + 4) % 5;
                controls.loop = (variant % 2) != 0;
                controls.fmRatio = variant == 3 ? 8.0f : 1.5f + static_cast<float> (variant);
                controls.fmIndex = variant == 3 ? 14.0f : 3.0f + 2.0f * static_cast<float> (variant);
                controls.dirt = variant == 3 ? 1.0f : 0.2f * static_cast<float> (variant);
                controls.resonance = variant == 3 ? 1.0f : 0.5f;
                controls.output = 2.0f;
                controls.sub = 1.0f;
                voice.setControls (controls);
                voice.noteOn (note, 1.0f, 0.0f);
                double sumSquares = 0.0;
                const auto count = static_cast<int> (sampleRate * 0.2);
                for (int i = 0; i < count; ++i)
                {
                    const auto sample = checkedSample (voice);
                    sumSquares += sample * sample;
                    peak = std::max (peak, std::abs (sample));
                }
                require (sumSquares / count > 0.00001, "held note unexpectedly silent");
                voice.noteOff();
                for (int i = 0; i < static_cast<int> (sampleRate); ++i) checkedSample (voice);
                require (! voice.isAudible(), "note remains active after a one-second release");
                require (voice.renderSample() == 0.0f, "released note does not reach silence");
                ++checkedRenders;
            }
        }
    }

    // Parameter changes and hostile numeric inputs must not poison a voice.
    std::mt19937 generator (0x594f4942u);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);
    int randomPatches = 0;
    for (const double sampleRate : { 44100.0, 48000.0, 192000.0 })
    {
        YoiVoice voice;
        voice.prepare (sampleRate);
        for (int patch = 0; patch < 1000; ++patch)
        {
            VoiceControls c;
            c.mouth = unit (generator);
            c.size = unit (generator);
            c.bite = unit (generator);
            c.dirt = unit (generator);
            c.sub = unit (generator);
            c.subMode = static_cast<int> (generator() % 3);
            c.lowCut = (generator() & 1u) != 0u;
            c.output = 2.0f * unit (generator);
            c.resonance = unit (generator);
            c.lengthSeconds = 0.04f + unit (generator) * 17.96f;
            c.bodyTime = 0.2f + unit (generator) * 0.6f;
            c.curve = unit (generator);
            c.fmRatio = 0.5f + unit (generator) * 7.5f;
            c.fmIndex = unit (generator) * 14.0f;
            c.startPose = static_cast<int> (generator() % 5);
            c.bodyPose = static_cast<int> (generator() % 5);
            c.endPose = static_cast<int> (generator() % 5);
            c.loop = (generator() & 1u) != 0u;
            voice.setControls (c);
            voice.noteOn (static_cast<int> (generator() % 128), unit (generator), unit (generator) * 500.0f, (patch % 3) == 0);
            voice.setPitchBend (unit (generator) * 48.0f - 24.0f);
            for (int i = 0; i < 1024; ++i) peak = std::max (peak, std::abs (checkedSample (voice)));
            if (patch % 7 == 0) voice.noteOff();
            ++randomPatches;
        }
        const auto bad = std::numeric_limits<float>::quiet_NaN();
        VoiceControls badControls;
        badControls.mouth = badControls.size = badControls.bite = badControls.dirt = bad;
        badControls.sub = badControls.output = badControls.lengthSeconds = badControls.bodyTime = bad;
        badControls.curve = badControls.fmRatio = badControls.fmIndex = badControls.resonance = bad;
        badControls.startPose = -999;
        badControls.bodyPose = 999;
        badControls.endPose = -999;
        voice.setControls (badControls);
        voice.noteOn (-999, bad, bad);
        voice.setPitchBend (bad);
        for (int i = 0; i < 8192; ++i) checkedSample (voice);
    }

    const auto reference = renderAtBlockSize (512);
    for (const int blockSize : { 1, 7, 33, 128, 511, 2048 })
        require (reference == renderAtBlockSize (blockSize), "audio depends on host block size");

    // The host exposes 2-second free gestures and up to an 18-second synced
    // dotted bar at 20 BPM. Long cycles retain sample-rate-independent timing.
    for (const double sampleRate : { 44100.0, 192000.0 })
    {
        for (const float length : { 1.0f / 60.0f, 2.0f, 4.0f, 12.0f, 18.0f })
        {
            YoiVoice voice;
            voice.prepare (sampleRate);
            VoiceControls c;
            c.lengthSeconds = length;
            c.loop = true;
            voice.setControls (c);
            voice.noteOn (36, 1.0f, 0.0f);
            const auto cycleSamples = static_cast<int> (sampleRate * length);
            for (int i = 0; i < cycleSamples / 2; ++i) checkedSample (voice);
            const auto timingTolerance = std::max (0.000001f, 1.1f / cycleSamples);
            require (std::abs (voice.gesturePhase() - 0.5f) < timingTolerance,
                     "long gesture length was clamped or its timing drifted");
            for (int i = cycleSamples / 2; i <= cycleSamples; ++i) checkedSample (voice);
            require (voice.gesturePhase() < std::max (0.0001f, timingTolerance),
                     "loop did not return to its start on time");
        }
    }

    YoiVoice unityOutput, boostedOutput;
    unityOutput.prepare (48000.0);
    boostedOutput.prepare (48000.0);
    VoiceControls unityControls;
    unityControls.output = 1.0f;
    unityControls.loop = true;
    auto boostedControls = unityControls;
    boostedControls.output = std::pow (10.0f, 6.0f / 20.0f);
    unityOutput.setControls (unityControls);
    boostedOutput.setControls (boostedControls);
    unityOutput.noteOn (36, 1.0f, 0.0f);
    boostedOutput.noteOn (36, 1.0f, 0.0f);
    double unityEnergy = 0.0, boostedEnergy = 0.0;
    for (int i = 0; i < 48000; ++i)
    {
        const auto unitySample = checkedSample (unityOutput);
        const auto boostedSample = checkedSample (boostedOutput);
        unityEnergy += unitySample * unitySample;
        boostedEnergy += boostedSample * boostedSample;
    }
    require (boostedEnergy > unityEnergy * 1.5, "+6 dB output setting was silently clamped to unity");

    // A sudden resonance change must slew the filter damping rather than
    // abruptly rescale its current state. Compare the first affected sample
    // against an identical voice left untouched, over many oscillator phases.
    for (const int note : { 24, 36, 60, 96, 127 })
    {
        YoiVoice referenceVoice;
        referenceVoice.prepare (48000.0);
        VoiceControls c;
        c.fmRatio = 0.5f;
        c.fmIndex = 14.0f;
        c.resonance = 0.0f;
        c.dirt = 1.0f;
        referenceVoice.setControls (c);
        referenceVoice.noteOn (note, 1.0f, 0.0f);
        for (int i = 0; i < 12000; ++i)
        {
            auto changedVoice = referenceVoice;
            auto changedControls = c;
            changedControls.resonance = 1.0f;
            changedVoice.setControls (changedControls);
            const auto expected = checkedSample (referenceVoice);
            const auto changed = checkedSample (changedVoice);
            require (std::abs (expected - changed) < 0.005f,
                     "resonance automation produced an abrupt gain step");
        }
    }

    // Bend is relative to MIDI note pitch and must be cleared by reset/prepare.
    for (const float bend : { -24.0f, -12.0f, 0.0f, 12.0f, 24.0f })
    {
        YoiVoice bent, direct;
        bent.prepare (48000.0);
        direct.prepare (48000.0);
        bent.setPitchBend (bend);
        bent.noteOn (48, 1.0f, 0.0f);
        direct.noteOn (48 + static_cast<int> (bend), 1.0f, 0.0f);
        for (int i = 0; i < 2048; ++i)
            require (bent.renderSample() == direct.renderSample(), "pitch bend does not match equivalent MIDI pitch");
        bent.reset();
        direct.reset();
        bent.noteOn (36, 1.0f, 0.0f);
        direct.noteOn (36, 1.0f, 0.0f);
        for (int i = 0; i < 2048; ++i)
            require (bent.renderSample() == direct.renderSample(), "reset retained pitch bend");
    }

    YoiVoice legato;
    legato.prepare (48000.0);
    legato.noteOn (36, 1.0f, 0.0f);
    for (int i = 0; i < 2000; ++i) checkedSample (legato);
    const auto phase = legato.gesturePhase();
    legato.noteOn (48, 0.8f, 100.0f, false);
    require (legato.gesturePhase() == phase, "legato restarted the vowel gesture");
    for (int i = 0; i < 2000; ++i) checkedSample (legato);
    legato.noteOn (24, 0.8f, 0.0f, true);
    require (legato.gesturePhase() == 0.0f, "retrigger did not restart the vowel gesture");
    legato.reset();
    require (! legato.isAudible() && legato.renderSample() == 0.0f, "panic/reset did not silence the voice");

    for (const double badRate : { 0.0, -44100.0, std::numeric_limits<double>::quiet_NaN(),
                                  std::numeric_limits<double>::infinity() })
    {
        YoiVoice voice;
        voice.prepare (badRate);
        voice.noteOn (36, 1.0f, 0.0f);
        float energy = 0.0f;
        for (int i = 0; i < 1024; ++i) energy += std::abs (checkedSample (voice));
        require (energy > 0.01f, "invalid sample rate did not use a working fallback");
    }

    YoiVoice benchmark;
    benchmark.prepare (48000.0);
    VoiceControls benchmarkControls;
    benchmarkControls.loop = true;
    benchmark.setControls (benchmarkControls);
    benchmark.noteOn (36, 1.0f, 0.0f);
    const auto start = std::chrono::steady_clock::now();
    double benchmarkSum = 0.0;
    for (int i = 0; i < 480000; ++i) benchmarkSum += benchmark.renderSample();
    const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    require (std::isfinite (benchmarkSum), "benchmark produced non-finite audio");
    std::cout << checkedRenders << " rate/note/patch renders, " << randomPatches
              << " dynamic random patches, MIDI/bend/reset, invalid inputs, 7 block sizes,"
              << " 16.667ms/2/4/12/18-second gesture timing and +6 dB output passed"
              << "; peak=" << peak << '\n'
              << "10 seconds of 48 kHz mono audio rendered in " << seconds << " seconds ("
              << (seconds * 10.0) << "% of one core in this offline run)\n";
    return 0;
}
