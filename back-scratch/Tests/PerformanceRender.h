#pragma once
#include "BackScratchProcessor.h"
#include "ProcessorTests.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <iostream>

inline void renderPerformanceDemos(const juce::File& directory, const juce::File& vocalFile = {})
{
    using namespace backScratch;
    using namespace backScratch::processorTests;
    directory.createDirectory();
    constexpr double sr = 48000, bpm = 140, pi2 = 6.28318530717958647692;
    const int sectionFrames = static_cast<int>(std::round(4 * 60.0 / bpm * sr));
    const int totalFrames = sectionFrames * 6;
    juce::AudioBuffer<float> voice;
    double voiceRate = sr;
    if (vocalFile.existsAsFile()) {
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> reader {formats.createReaderFor(vocalFile)}) {
            voiceRate = reader->sampleRate;
            voice.setSize(1, static_cast<int>(reader->lengthInSamples));
            reader->read(&voice, 0, voice.getNumSamples(), 0, true, false);
        }
    }
    const auto source = [&](int kind, int frame) {
        const double time = frame / sr, beat = time * bpm / 60.0;
        const double local = beat - std::floor(beat);
        const double bassNote = (static_cast<int>(beat) % 8 < 4 ? 49.0 : 43.6535);
        if (kind == 0) {
            const double envelope = 0.5 + 0.5 * std::exp(-local * 4);
            double harmonics = 0;
            for (int h = 2; h <= 15; ++h)
                harmonics += std::sin(pi2 * bassNote * h * time + 0.7 * std::sin(pi2 * time * 1.1)) / h;
            return static_cast<float>(0.28 * std::sin(pi2 * bassNote * time) + 0.19 * harmonics * envelope);
        }
        if (kind == 1) {
            const double kickT = local * 60.0 / bpm;
            const double kick = 0.55 * std::exp(-kickT * 18) * std::sin(pi2 * (43 * kickT + 0.95 * (1 - std::exp(-kickT * 35))));
            const auto hash = static_cast<std::uint32_t>(frame) * 747796405u + 2891336453u;
            const double noise = (static_cast<double>((hash ^ (hash >> 16)) & 65535u) / 32768.0 - 1);
            const double snare = (static_cast<int>(beat) % 2 ? 0.28 : 0) * std::exp(-kickT * 26) * noise;
            const double hatT = std::fmod(beat, 0.5) * 60.0 / bpm;
            const double hat = 0.065 * std::exp(-hatT * 100) * noise;
            return static_cast<float>(kick + snare + hat);
        }
        if (voice.getNumSamples() > 0) {
            const int index = static_cast<int>(std::fmod(time * voiceRate, voice.getNumSamples()));
            return voice.getSample(0, index) * 0.8f;
        }
        const double envelope = std::pow(std::sin(pi2 * time * 2.1) * 0.5 + 0.5, 2);
        double vowel = 0;
        for (int h = 1; h < 40; ++h) {
            const double f = h * 122.0;
            const double weight = std::exp(-std::pow((f - 650) / 250, 2)) + 0.6 * std::exp(-std::pow((f - 1550) / 400, 2));
            vowel += weight * std::sin(pi2 * f * time) / h;
        }
        return static_cast<float>(vowel * envelope * 0.8);
    };
    const int generationSeeds[3][2] {{83177, 600201}, {91431, 780111}, {44891, 930121}};
    const char* names[] {"01-Bass-Keep-Sub-On-and-Off.wav", "02-Drum-Scratch-Fills.wav", "03-Vocal-Scratch-Cuts.wav"};
    for (int kind = 0; kind < 3; ++kind) {
        Head head; head.bpm = bpm;
        BackScratchProcessor processor; processor.setPlayHead(&head); processor.prepareToPlay(sr, 256);
        processor.setHostParameter("generated", 1);
        processor.setHostParameter("generationSeed", static_cast<float>(generationSeeds[kind][0]));
        processor.setPhraseBars(1); processor.setPhraseLocked(true);
        const int latency = processor.getLatencySamples();
        juce::AudioBuffer<float> output(2, totalFrames);
        juce::MidiBuffer midi;
        double peak = 0;
        int previousSection = -1;
        for (int start = 0; start < totalFrames + latency;) {
            const int section = std::min(5, start / sectionFrames);
            const int sectionEnd = start < totalFrames ? (section + 1) * sectionFrames : totalFrames + latency;
            const int count = std::min(256, sectionEnd - start);
            if (section != previousSection) {
                processor.setHostParameter("bypass", section == 0 || section == 5 ? 1 : 0);
                processor.setHostParameter("generationSeed", static_cast<float>(generationSeeds[kind][section >= 3 ? 1 : 0]));
                processor.setKeepSub(kind != 0 || section != 4);
                previousSection = section;
            }
            juce::AudioBuffer<float> block(2, count);
            for (int i = 0; i < count; ++i) {
                const float sample = source(kind, start + i);
                block.setSample(0, i, sample); block.setSample(1, i, sample);
            }
            head.beat = start * bpm / (sr * 60);
            processor.processBlock(block, midi);
            for (int i = 0; i < count; ++i) for (int ch = 0; ch < 2; ++ch) {
                const float sample = block.getSample(ch, i);
                check(std::isfinite(sample), "demo samples finite");
                const int compensatedFrame = start + i - latency;
                if (compensatedFrame >= 0 && compensatedFrame < totalFrames) {
                    output.setSample(ch, compensatedFrame, sample);
                    peak = std::max(peak, static_cast<double>(std::abs(sample)));
                }
            }
            start += count;
        }
        check(peak < 1.0 && peak > 0.01, "demo has headroom and audible output");
        const auto file = directory.getChildFile(names[kind]);
        auto stream = file.createOutputStream();
        check(stream != nullptr, "demo output opens");
        stream->setPosition(0); stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(), sr, 2, 24, {}, 0));
        check(writer != nullptr && writer->writeFromAudioSampleBuffer(output, 0, output.getNumSamples()), "demo WAV renders");
        std::cout << names[kind] << " peak=" << peak << ", " << totalFrames / sr << " seconds\n";
    }
}
