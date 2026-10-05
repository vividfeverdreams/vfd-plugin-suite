#include "ReverbEngine.h"
#include "Presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// Offline calibration evidence, intentionally separate from pass/fail DSP tests.
// Build: clang++ -std=c++17 -O2 -I Source Tests/WetLevelAudit.cpp
//        Source/ReverbEngine.cpp Source/Presets.cpp -o WetLevelAudit
// Usage: WetLevelAudit output.csv [factory|no-duck|open-filters|open-filters-no-duck]
//        [wav-directory] [--verify]
namespace
{
constexpr double sampleRate = 48000;
constexpr double pi = 3.14159265358979323846;
constexpr double duration = 3.2;
constexpr double sustainEnd = 1.6;
constexpr double dryRmsTarget = 0.18;

struct Audio
{
    explicit Audio (std::size_t count) : left (count), right (count), active (count) {}
    std::vector<float> left, right;
    std::vector<bool> active;
    double lastNoteEnd = sustainEnd;
};

double db (double value) { return 20 * std::log10 (std::max (value, 1.0e-12)); }

Audio signal (int kind)
{
    Audio audio (static_cast<std::size_t> (sampleRate * duration));
    if (kind == 2) audio.lastNoteEnd = 1.20 + 0.16;
    for (std::size_t i = 0; i < audio.left.size(); ++i)
    {
        const double time = i / sampleRate;
        double envelope = 0;
        if (kind == 2)
        {
            const double local = time - std::floor (time / 0.4) * 0.4;
            if (time < audio.lastNoteEnd && local < 0.16)
            {
                envelope = std::min (1.0, local / 0.003) * std::exp (-local / 0.055)
                         * std::min (1.0, (0.16 - local) / 0.006);
                audio.active[i] = true;
            }
        }
        else if (time < sustainEnd)
        {
            envelope = std::min (1.0, time / 0.005) * std::min (1.0, (sustainEnd - time) / 0.008);
            audio.active[i] = true;
        }
        if (envelope == 0) continue;
        for (int channel = 0; channel < 2; ++channel)
        {
            double value = 0;
            if (kind == 0)
            {
                // Harmonic mid-bass with a 330 Hz body and 660--2640 Hz harmonics.
                for (int harmonic = 1; harmonic <= 8; ++harmonic)
                    value += std::sin (2 * pi * 330 * harmonic * time + channel * harmonic * 0.07)
                           / std::pow (harmonic, 1.25);
            }
            else if (kind == 1)
            {
                // Detuned saw harmonics, with reduced 165 Hz fundamental;
                // most energy sits in the intended 330 Hz+ mid-bass range.
                for (int harmonic = 1; harmonic <= 16; ++harmonic)
                {
                    const double weight = harmonic == 1 ? 0.12 : 1.0 / harmonic;
                    value += weight * (std::sin (2 * pi * 165 * harmonic * time + channel * 0.2)
                                     + std::sin (2 * pi * 166.4 * harmonic * time + 0.37 - channel * 0.14));
                }
                value = std::tanh (value * 1.8);
            }
            else
            {
                // Short, aggressive formant-like stabs with a 370 Hz body.
                value = std::tanh (2.0 * (std::sin (2 * pi * 370 * time)
                                      + 0.45 * std::sin (2 * pi * 1110 * time + channel * 0.2)
                                      + 0.28 * std::sin (2 * pi * 1850 * time)));
            }
            (channel == 0 ? audio.left[i] : audio.right[i]) = static_cast<float> (value * envelope);
        }
    }
    double energy = 0;
    std::size_t samples = 0;
    for (std::size_t i = 0; i < audio.left.size(); ++i)
        if (audio.active[i])
        {
            energy += audio.left[i] * audio.left[i] + audio.right[i] * audio.right[i];
            samples += 2;
        }
    const double scale = dryRmsTarget / std::sqrt (energy / samples);
    for (std::size_t i = 0; i < audio.left.size(); ++i)
    {
        audio.left[i] *= static_cast<float> (scale);
        audio.right[i] *= static_cast<float> (scale);
    }
    return audio;
}

Audio render (const Audio& original, throwup::Settings settings)
{
    settings.mix = 100;
    settings.output = 0;
    settings.bypass = false;
    throwup::ReverbEngine engine;
    engine.setSettings (settings);
    engine.prepare (sampleRate, 256);
    auto result = original;
    for (std::size_t begin = 0; begin < result.left.size(); begin += 256)
    {
        float* channels[] { result.left.data() + begin, result.right.data() + begin };
        const auto count = std::min<std::size_t> (256, result.left.size() - begin);
        engine.process (channels, 2, static_cast<int> (count));
    }
    return result;
}

double rms (const Audio& audio, double begin, double end, const std::vector<bool>* active = nullptr)
{
    double energy = 0;
    std::size_t count = 0;
    const auto endSample = std::min (audio.left.size(), static_cast<std::size_t> (end * sampleRate));
    for (auto i = static_cast<std::size_t> (begin * sampleRate); i < endSample; ++i)
        if (active == nullptr || (*active)[i])
        {
            energy += audio.left[i] * audio.left[i] + audio.right[i] * audio.right[i];
            count += 2;
        }
    return count == 0 ? 0 : std::sqrt (energy / count);
}

std::pair<double, double> strongestWindow (const Audio& audio)
{
    constexpr std::size_t window = static_cast<std::size_t> (sampleRate * 0.1);
    double energy = 0, maximum = 0;
    std::size_t bestEnd = window;
    for (std::size_t i = 0; i < audio.left.size(); ++i)
    {
        energy += audio.left[i] * audio.left[i] + audio.right[i] * audio.right[i];
        if (i >= window)
            energy -= audio.left[i - window] * audio.left[i - window]
                    + audio.right[i - window] * audio.right[i - window];
        if (i + 1 >= window && energy > maximum)
        {
            maximum = energy;
            bestEnd = i + 1;
        }
    }
    return { std::sqrt (maximum / (2 * window)), (bestEnd - window) / sampleRate };
}

double peak (const Audio& audio)
{
    double maximum = 0;
    for (std::size_t i = 0; i < audio.left.size(); ++i)
    {
        if (! std::isfinite (audio.left[i]) || ! std::isfinite (audio.right[i]))
            throw std::runtime_error ("Nonfinite audio in wet-level audit");
        maximum = std::max (maximum, static_cast<double> (std::max (std::abs (audio.left[i]), std::abs (audio.right[i]))));
    }
    return maximum;
}

void writeU32 (std::ofstream& file, std::uint32_t value)
{
    for (int byte = 0; byte < 4; ++byte) file.put (static_cast<char> ((value >> (byte * 8)) & 255));
}

void writeU16 (std::ofstream& file, std::uint16_t value)
{
    file.put (static_cast<char> (value & 255));
    file.put (static_cast<char> (value >> 8));
}

void writeWav (const std::filesystem::path& path, const Audio& audio)
{
    std::ofstream file (path, std::ios::binary);
    if (! file) throw std::runtime_error ("Cannot write WAV");
    const auto dataBytes = static_cast<std::uint32_t> (audio.left.size() * 8);
    file.write ("RIFF", 4); writeU32 (file, 36 + dataBytes); file.write ("WAVEfmt ", 8);
    writeU32 (file, 16); writeU16 (file, 3); writeU16 (file, 2);
    writeU32 (file, static_cast<std::uint32_t> (sampleRate));
    writeU32 (file, static_cast<std::uint32_t> (sampleRate * 8));
    writeU16 (file, 8); writeU16 (file, 32); file.write ("data", 4); writeU32 (file, dataBytes);
    for (std::size_t i = 0; i < audio.left.size(); ++i)
    {
        file.write (reinterpret_cast<const char*> (&audio.left[i]), sizeof (float));
        file.write (reinterpret_cast<const char*> (&audio.right[i]), sizeof (float));
    }
}
} // namespace

int main (int argc, char** argv)
{
    try
    {
        if (argc < 2 || argc > 5)
            throw std::runtime_error ("Usage: WetLevelAudit output.csv [factory|no-duck|open-filters|open-filters-no-duck] [wav-directory] [--verify]");
        const std::string mode = argc >= 3 ? argv[2] : "factory";
        if (mode != "factory" && mode != "no-duck" && mode != "open-filters" && mode != "open-filters-no-duck")
            throw std::runtime_error ("Unknown audit mode");
        const bool verify = (argc >= 4 && std::string (argv[3]) == "--verify")
                         || (argc == 5 && std::string (argv[4]) == "--verify");
        if ((argc == 5 && (! verify || std::string (argv[3]) == "--verify"))
            || (verify && mode != "factory"))
            throw std::runtime_error ("--verify requires factory mode; optional WAV directory precedes --verify");
        const std::filesystem::path wavDirectory = argc >= 4 && std::string (argv[3]) != "--verify" ? argv[3] : "";
        if (! wavDirectory.empty()) std::filesystem::create_directories (wavDirectory);
        std::ofstream csv (argv[1]);
        if (! csv) throw std::runtime_error ("Cannot write CSV");
        csv << "preset,group,source,mode,low_cut_hz,high_cut_hz,duck,size,decay_seconds,dry_active_dbfs,"
               "wet_active_vs_dry_db,wet_best_100ms_vs_dry_db,wet_best_window_start_seconds,"
               "wet_first_100ms_vs_dry_db,wet_first_250ms_post_note_vs_dry_db,"
               "wet_next_750ms_post_note_vs_dry_db,wet_peak_dbfs,mixed_peak_dbfs,dry_peak_dbfs\n";
        csv << std::fixed << std::setprecision (4);
        constexpr std::array<const char*, 3> names { "harmonic", "reese", "stabs" };
        // Product-level audibility floors at full THROW, expressed relative to
        // dry active RMS rather than any internal gain constant. Longer rooms
        // intentionally take more time to develop; strongly band-limited
        // creative effects need a wider allowance. Short rooms must be clearly
        // audible while the note plays, not pass by integrating a long tail.
        constexpr std::array<double, 5> activeFloor { -6, -13, -18, -22, -23 };
        constexpr std::array<double, 5> bestWindowFloor { -6, -6, -9, -10, -15 };
        constexpr std::array<double, 5> firstWindowFloor { -12, -17, -24, -32, -26 };
        constexpr std::array<double, 5> postNoteFloor { -13, -9, -11, -14, -18 };
        std::size_t failures = 0;
        double maximumWetPeak = 0, maximumMixedPeak = 0;
        const auto& presets = throwup::factoryPresets();
        for (int kind = 0; kind < 3; ++kind)
        {
            const auto original = signal (kind);
            if (! wavDirectory.empty()) writeWav (wavDirectory / (std::string (names[kind]) + "-dry.wav"), original);
            for (std::size_t index = 0; index < presets.size(); ++index)
            {
                const auto& preset = presets[index];
                auto settings = preset.settings;
                if (mode.find ("no-duck") != std::string::npos) settings.duck = 0;
                if (mode.find ("open-filters") != std::string::npos)
                {
                    settings.lowCut = 80;
                    settings.highCut = 18000;
                }
                const auto rendered = render (original, settings);
                auto wet = rendered;
                for (std::size_t i = 0; i < wet.left.size(); ++i)
                {
                    wet.left[i] -= original.left[i];
                    wet.right[i] -= original.right[i];
                }
                const double dry = rms (original, 0, original.lastNoteEnd, &original.active);
                const double activeWet = rms (wet, 0.25, original.lastNoteEnd, &original.active);
                const auto best = strongestWindow (wet);
                const double firstArrival = settings.predelay * 0.001 + 0.02171 * (0.30 + settings.size * 0.025);
                const double activeDb = db (activeWet / dry);
                const double bestDb = db (best.first / dry);
                const double firstDb = db (rms (wet, firstArrival, firstArrival + 0.1) / dry);
                const double postDb = db (rms (wet, original.lastNoteEnd, original.lastNoteEnd + 0.25) / dry);
                const double wetPeak = peak (wet), mixedPeak = peak (rendered);
                maximumWetPeak = std::max (maximumWetPeak, wetPeak);
                maximumMixedPeak = std::max (maximumMixedPeak, mixedPeak);
                if (verify)
                {
                    const auto group = index / 10;
                    if (activeDb < activeFloor[group] || bestDb < bestWindowFloor[group]
                        || firstDb < firstWindowFloor[group] || postDb < postNoteFloor[group])
                    {
                        if (++failures <= 12)
                            std::cerr << "INAUDIBLE " << preset.name << " / " << names[kind]
                                      << ": active " << activeDb << ", best 100 ms " << bestDb
                                      << ", first 100 ms " << firstDb << ", post-note 250 ms " << postDb << " dB vs dry\n";
                    }
                    // Finite is checked by peak(); peaks above unity are valid
                    // floating-point headroom, but a runaway return is not.
                    if (wetPeak > 4 || mixedPeak > 5)
                        throw std::runtime_error ("Unbounded factory render in wet-level audit");
                }
                csv << '"' << preset.name << "\"," << preset.group << ',' << names[kind] << ',' << mode << ','
                    << settings.lowCut << ',' << settings.highCut << ',' << settings.duck << ','
                    << settings.size << ',' << settings.decay << ',' << db (dry) << ','
                    << activeDb << ',' << bestDb << ',' << best.second << ','
                    << firstDb << ',' << postDb << ','
                    << db (rms (wet, original.lastNoteEnd + 0.25, original.lastNoteEnd + 1.0) / dry) << ','
                    << db (wetPeak) << ',' << db (mixedPeak) << ',' << db (peak (original)) << '\n';
                if (! wavDirectory.empty() && index % 10 == 0)
                {
                    const auto stem = std::string (preset.group) + "-" + names[kind];
                    writeWav (wavDirectory / (stem + "-wet.wav"), wet);
                    writeWav (wavDirectory / (stem + "-mixed.wav"), rendered);
                }
            }
        }
        std::cout << "Audited 50 presets x 3 mid-bass sources at 100% THROW; mode=" << mode
                  << ", dry active RMS=" << db (dryRmsTarget) << " dBFS; CSV=" << argv[1] << '\n';
        std::cout << "Maximum wet peak " << db (maximumWetPeak) << " dBFS; maximum parallel-output peak "
                  << db (maximumMixedPeak) << " dBFS (floating-point headroom).\n";
        if (verify)
        {
            std::cout << (failures == 0 ? "PASS" : "FAIL") << ": " << (150 - failures)
                      << "/150 preset/source pairs meet active, strongest 100 ms, first-arrival, and post-note audibility floors.\n";
            if (failures != 0) return 1;
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
