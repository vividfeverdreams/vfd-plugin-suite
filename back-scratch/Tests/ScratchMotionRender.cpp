// Standalone, deterministic comparison renderer. No JUCE or audio device:
// clang++ -std=c++17 -O2 -I Source Tests/ScratchMotionRender.cpp \
//   Source/ScratchPattern.cpp Source/LiveScratchEngine.cpp -o /tmp/scratch-motion-render
// /tmp/scratch-motion-render /tmp/BACK-SCRATCH-motion-comparison
#include "ScratchPattern.h"
#include "LiveScratchEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr double sr = 48000.0, bpm = 140.0, pi2 = 6.28318530717958647692;
constexpr double beatFrames = sr * 60.0 / bpm;
float syntheticSource(int kind, int frame)
{
    const double time = frame / sr, beat = frame / beatFrames;
    const double local = beat - std::floor(beat);
    if (kind == 0) {
        const double note = 49.0;
        double upper = 0.0;
        for (int harmonic = 2; harmonic <= 15; ++harmonic)
            upper += std::sin(pi2 * note * harmonic * time + 0.7 * std::sin(pi2 * time * 1.1)) / harmonic;
        return static_cast<float>(0.28 * std::sin(pi2 * note * time)
            + 0.19 * upper * (0.5 + 0.5 * std::exp(-local * 4.0)));
    }
    if (kind == 1) {
        const double transient = local * 60.0 / bpm;
        const double kick = 0.48 * std::exp(-transient * 18)
            * std::sin(pi2 * (43 * transient + 0.95 * (1 - std::exp(-transient * 35))));
        const auto hash = static_cast<std::uint32_t>(frame) * 747796405u + 2891336453u;
        const double noise = (static_cast<double>((hash ^ (hash >> 16)) & 65535u) / 32768.0 - 1);
        const double snare = (static_cast<int>(beat) % 2 ? 0.28 : 0)
            * std::exp(-transient * 26) * noise;
        const double hatTime = std::fmod(beat, 0.5) * 60.0 / bpm;
        return static_cast<float>(kick + snare + 0.065 * std::exp(-hatTime * 100) * noise);
    }
    // Synthetic vowel-like harmonics; this is not a recorded or cloned voice.
    const double envelope = std::pow(std::sin(pi2 * time * 2.1) * 0.5 + 0.5, 2);
    double vowel = 0;
    for (int harmonic = 1; harmonic < 40; ++harmonic) {
        const double frequency = harmonic * 122.0;
        const double weight = std::exp(-std::pow((frequency - 650) / 250, 2))
            + 0.6 * std::exp(-std::pow((frequency - 1550) / 400, 2));
        vowel += weight * std::sin(pi2 * frequency * time) / harmonic;
    }
    return static_cast<float>(vowel * envelope * 0.8);
}
void writeWave(const std::filesystem::path& path, const std::vector<float>& data)
{
    std::ofstream output(path, std::ios::binary);
    const auto number = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) output.put(static_cast<char>((value >> (8 * i)) & 255u));
    };
    const auto text = [&](const char* value, int count) { output.write(value, count); };
    const auto size = static_cast<std::uint32_t>(data.size() * 3);
    text("RIFF", 4); number(size + 36, 4); text("WAVEfmt ", 8); number(16, 4);
    number(1, 2); number(1, 2); number(static_cast<std::uint32_t>(sr), 4);
    number(static_cast<std::uint32_t>(sr) * 3, 4); number(3, 2); number(24, 2);
    text("data", 4); number(size, 4);
    for (const float sample : data) {
        if (!std::isfinite(sample) || std::abs(sample) >= 1.0f)
            throw std::runtime_error("Comparison clipped or became non-finite");
        const auto value = static_cast<std::int32_t>(std::lround(sample * 8388607.0));
        number(static_cast<std::uint32_t>(value), 3);
    }
    if (!output.good()) throw std::runtime_error("Could not write comparison WAV");
}
}

int main(int argc, char** argv)
{
    if (argc != 2) { std::cerr << "Supply the comparison output directory\n"; return 1; }
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    const int frames = static_cast<int>(std::lround(16.0 * beatFrames));
    const char* labels[] {"Bass", "Drums", "Synthetic-vowel"};
    double maximumPeak = 0.0;
    for (int kind = 0; kind < 3; ++kind) {
        std::vector<float> dry(static_cast<std::size_t>(frames));
        for (int frame = 0; frame < frames; ++frame) dry[static_cast<std::size_t>(frame)] = syntheticSource(kind, frame);
        writeWave(directory / (std::string(labels[kind]) + "-01-source.wav"), dry);
        for (int version = 1; version <= 2; ++version) {
            const auto pattern = backScratch::fitPatternToPhrase(backScratch::makePattern(0, 0, version), 4.0);
            for (const bool protect : {false, true}) {
                if (protect && kind != 0) continue;
                backScratch::LiveScratchEngine engine;
                engine.prepare(sr, 2, 2.0);
                std::vector<float> output(static_cast<std::size_t>(frames));
                for (int frame = 0; frame < frames + engine.latencySamples(); ++frame) {
                    const int sourceFrame = frame - engine.latencySamples();
                    const auto point = sourceFrame < 0 ? backScratch::PatternSample {}
                        : backScratch::evaluatePattern(pattern, sourceFrame / beatFrames);
                    const float input = syntheticSource(kind, frame);
                    const auto value = engine.process(input, input, point, beatFrames, protect, false);
                    if (sourceFrame >= 0) {
                        output[static_cast<std::size_t>(sourceFrame)] = value[0];
                        maximumPeak = std::max(maximumPeak, std::abs(static_cast<double>(value[0])));
                    }
                }
                const std::string name = std::string(labels[kind]) + (version == 1 ? "-02-before-v090" : "-03-after-v0100")
                    + (protect ? "-keep-sub" : "-full-range") + ".wav";
                writeWave(directory / name, output);
            }
        }
    }
    std::ofstream readme(directory / "READ ME.txt");
    readme << "BACK-SCRATCH scratch-motion comparison\n\n"
        "Synthetic bass, drum loop, and vowel-like signal. No recorded voice or third-party samples.\n"
        "Every comparison uses the same Sub Tug recipe, one-bar duration, 140 BPM, four bars.\n"
        "01 = clean source; 02 = original v0.9.0 straight-rate motion; 03 = smooth v0.10 motion.\n"
        "Full-range files scratch the entire source. Bass also includes matched Keep Sub comparisons.\n"
        "24-bit mono, 48 kHz. Original levels preserved; no loudness matching or post-processing.\n"
        "All processed files have their declared engine latency removed so timing matches the source.\n"
        "These are controlled render comparisons, not an Ableton playback or subjective listening certification.\n";
    std::cout << "11 controlled comparison files; maximum output peak " << maximumPeak << '\n';
}
