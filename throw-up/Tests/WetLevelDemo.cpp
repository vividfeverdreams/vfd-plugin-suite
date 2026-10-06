// Standalone, fixed-level listening fixture. Compile this same source against
// each engine version; save the old render with --baseline, then use --compare
// with the corrected engine. There is no normalization, gain matching or limiter.
#include "ReverbEngine.h"
#include "Presets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int rate = 48000, channels = 2, blockSize = 128;
constexpr int phraseFrames = rate * 6, gapFrames = rate / 4;
constexpr double pi = 3.14159265358979323846;
constexpr std::array<int, 2> presetIndices {1, 10};
using Samples = std::vector<float>;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void write16(std::ostream& stream, uint16_t value)
{
    for (int byte = 0; byte < 2; ++byte) stream.put(static_cast<char>((value >> (8 * byte)) & 255));
}
void write32(std::ostream& stream, uint32_t value)
{
    for (int byte = 0; byte < 4; ++byte) stream.put(static_cast<char>((value >> (8 * byte)) & 255));
}
uint32_t read32(std::istream& stream)
{
    uint32_t result = 0;
    for (int byte = 0; byte < 4; ++byte) {
        const auto value = stream.get();
        require(value >= 0, "reference file ended early");
        result |= static_cast<uint32_t>(value) << (8 * byte);
    }
    return result;
}

void saveWav(const std::filesystem::path& path, const Samples& samples)
{
    std::ofstream stream(path, std::ios::binary);
    require(stream.good(), "cannot create comparison WAV");
    const auto dataBytes = static_cast<uint32_t>(samples.size() * 3);
    stream.write("RIFF", 4); write32(stream, dataBytes + 36); stream.write("WAVEfmt ", 8);
    write32(stream, 16); write16(stream, 1); write16(stream, channels);
    write32(stream, rate); write32(stream, rate * channels * 3);
    write16(stream, channels * 3); write16(stream, 24);
    stream.write("data", 4); write32(stream, dataBytes);
    float peak = 0;
    for (float sample : samples) {
        require(std::isfinite(sample) && std::abs(sample) < 1.0f, "comparison would clip; no automatic gain changes allowed");
        peak = std::max(peak, std::abs(sample));
        const auto pcm = static_cast<uint32_t>(static_cast<int32_t>(std::lround(sample * 8388607.0f)));
        for (int byte = 0; byte < 3; ++byte) stream.put(static_cast<char>((pcm >> (8 * byte)) & 255));
    }
    require(stream.good(), "comparison WAV write failed");
    std::cout << path.filename().string() << ": " << samples.size() / static_cast<double>(rate * channels)
              << " seconds, fixed-level peak " << 20 * std::log10(std::max(peak, 1.0e-12f)) << " dBFS\n";
}

Samples source()
{
    Samples result(static_cast<size_t>(phraseFrames * channels), 0.0f);
    constexpr std::array<double, 4> starts {.10, .85, 1.60, 2.35};
    constexpr std::array<double, 4> lengths {.32, .40, .24, .44};
    constexpr std::array<double, 4> frequencies {164.81, 164.81, 146.83, 164.81};
    for (int sample = 0; sample < phraseFrames; ++sample) {
        const auto time = sample / static_cast<double>(rate);
        for (size_t note = 0; note < starts.size(); ++note) {
            const auto age = time - starts[note];
            if (age < 0 || age >= lengths[note]) continue;
            const auto envelope = std::min(1.0, age / .008)
                                * std::min(1.0, (lengths[note] - age) / .015)
                                * std::exp(-age * 2.5);
            for (int channel = 0; channel < channels; ++channel) {
                const auto phase = 2 * pi * frequencies[note] * age;
                const auto body = std::tanh(2.1 * std::sin(phase + 1.3 * std::sin(phase * 2.003 + channel * .03)));
                const auto edge = .20 * std::sin(phase * 3.01 + .7 * std::sin(phase * .5));
                result[static_cast<size_t>(sample * channels + channel)] = static_cast<float>(.28 * envelope * (body + edge));
            }
        }
    }
    return result;
}

Samples render(const Samples& dry, int preset)
{
    // This comparison renderer documents the 0.1.3 -> 0.1.4 return change, so it
    // renders the original rooms with the 0.1.5 sound (the Legacy 0.1.5 bank).
    auto settings = throwup::settingsFor(throwup::factoryPresets()[static_cast<size_t>(throwup::legacyPresetStart() + preset)].room);
    settings.mix = 100.0f;
    settings.output = 0.0f;
    settings.bypass = false;
    throwup::ReverbEngine engine;
    engine.setSettings(settings);
    engine.prepare(rate, blockSize);
    Samples output(dry.size());
    std::array<std::array<float, blockSize>, channels> block {};
    float* data[] {block[0].data(), block[1].data()};
    for (int start = 0; start < phraseFrames; start += blockSize) {
        const auto count = std::min(blockSize, phraseFrames - start);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < count; ++i)
                block[static_cast<size_t>(ch)][static_cast<size_t>(i)] = dry[static_cast<size_t>((start + i) * channels + ch)];
        engine.process(data, channels, count);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < count; ++i)
                output[static_cast<size_t>((start + i) * channels + ch)] = block[static_cast<size_t>(ch)][static_cast<size_t>(i)];
    }
    return output;
}

void saveBaseline(const std::filesystem::path& path, const std::array<Samples, 2>& renders)
{
    std::ofstream stream(path, std::ios::binary);
    require(stream.good(), "cannot create baseline reference");
    stream.write("TUWET001", 8);
    write32(stream, rate); write32(stream, channels); write32(stream, phraseFrames); write32(stream, 2);
    for (const auto& samples : renders)
        for (const auto sample : samples) {
            static_assert(sizeof(float) == sizeof(uint32_t));
            uint32_t bits = 0;
            std::memcpy(&bits, &sample, sizeof(bits));
            write32(stream, bits);
        }
    require(stream.good(), "baseline write failed");
}

std::array<Samples, 2> loadBaseline(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    require(stream.good(), "cannot read baseline reference");
    std::array<char, 8> magic {}; stream.read(magic.data(), magic.size());
    require(std::string(magic.data(), magic.size()) == "TUWET001", "wrong baseline reference format");
    require(read32(stream) == rate && read32(stream) == channels && read32(stream) == phraseFrames
            && read32(stream) == 2, "baseline format differs from listening fixture");
    std::array<Samples, 2> result;
    for (auto& samples : result) {
        samples.resize(static_cast<size_t>(phraseFrames * channels));
        for (auto& sample : samples) {
            const auto bits = read32(stream);
            std::memcpy(&sample, &bits, sizeof(sample));
            require(std::isfinite(sample), "baseline contains invalid audio");
        }
    }
    require(stream.peek() == std::char_traits<char>::eof(), "baseline reference has extra data");
    return result;
}

void append(Samples& combined, const Samples& segment)
{
    if (!combined.empty()) combined.insert(combined.end(), static_cast<size_t>(gapFrames * channels), 0.0f);
    combined.insert(combined.end(), segment.begin(), segment.end());
}
} // namespace

int main(int argc, char** argv)
{
    try {
        require(argc >= 3, "usage: WetLevelDemo --baseline baseline.bin OR --compare baseline.bin output-directory");
        const auto dry = source();
        const std::array<Samples, 2> current {render(dry, presetIndices[0]), render(dry, presetIndices[1])};
        if (std::string(argv[1]) == "--baseline") {
            require(argc == 3, "baseline mode needs one output file");
            saveBaseline(argv[2], current);
            std::cout << "Saved unchanged baseline renders at THROW 100%, Output 0 dB\n";
        } else {
            require(std::string(argv[1]) == "--compare" && argc == 4, "comparison mode needs reference file and output directory");
            const auto baseline = loadBaseline(argv[2]);
            const std::filesystem::path folder(argv[3]); std::filesystem::create_directories(folder);
            Samples complete;
            for (size_t preset = 0; preset < current.size(); ++preset) {
                Samples comparison;
                for (const auto* segment : {&dry, &baseline[preset], &current[preset]}) {
                    append(comparison, *segment);
                    append(complete, *segment);
                }
                saveWav(folder / (preset == 0 ? "THROW-UP-small-dry-old-corrected.wav" : "THROW-UP-medium-dry-old-corrected.wav"), comparison);
            }
            saveWav(folder / "THROW-UP-wet-level-comparison.wav", complete);
            std::ofstream notes(folder / "THROW-UP-wet-level-comparison.txt");
            notes << "THROW-UP reverb level comparison\n\n"
                  << "Same synthesized bass phrase, same source gain, THROW 100%, Output 0 dB. "
                  << "No normalization, gain matching or limiting. 48 kHz stereo, 24-bit PCM.\n\n"
                  << "The corrected reverb return is 18 dB louder; the dry signal is unchanged.\n\n"
                  << "Each individual file: 0:00 dry; 0:06.25 old v0.1.3; 0:12.50 corrected v0.1.4.\n"
                  << "Small: Orange Crate. Medium: Back Room. Each phrase includes its decay.\n\n"
                  << "Combined file: 0:00 Small dry; 0:06.25 Small old; 0:12.50 Small corrected; "
                  << "0:18.75 Medium dry; 0:25.00 Medium old; 0:31.25 Medium corrected.\n";
            require(notes.good(), "comparison guide write failed");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
}
