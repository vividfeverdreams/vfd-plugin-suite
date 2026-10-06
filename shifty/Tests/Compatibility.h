#pragma once

// Compatibility with SHIFTY 0.4.2 and the factory preset audit.
//
// --compat hosts an installed-style 0.4.2 SHIFTY.vst3 (read only), saves its
// state for each test sound, renders probe signals through it, then loads the
// same state into this build and renders again. Projects from 0.4.2 must open
// on the original sound, at the original latency, within ~1e-5.

#include "../Source/Plugin/ShiftyProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <complex>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

namespace shifty::compat {

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;
constexpr double pi = juce::MathConstants<double>::pi;

class PlayingHost final : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo position;
        position.setBpm(140.0);
        position.setIsPlaying(true);
        position.setPpqPosition(samples / sampleRate * 140.0 / 60.0);
        position.setTimeInSeconds(samples / sampleRate);
        position.setTimeInSamples(static_cast<juce::int64>(samples));
        position.setTimeSignature(juce::AudioPlayHead::TimeSignature{4, 4});
        position.setPpqPositionOfLastBarStart(std::floor(samples / sampleRate * 140.0 / 60.0 / 4.0) * 4.0);
        return position;
    }
    double samples = 0.0;
};

struct Probe {
    const char* name;
    std::vector<std::vector<float>> audio;
};

inline std::vector<Probe> probes(double seconds)
{
    const int length = static_cast<int>(seconds * sampleRate);
    std::vector<Probe> result;
    auto stereo = [length](auto&& generator) {
        std::vector<std::vector<float>> audio(2, std::vector<float>(static_cast<size_t>(length)));
        for (int i = 0; i < length; ++i)
            for (int c = 0; c < 2; ++c)
                audio[static_cast<size_t>(c)][static_cast<size_t>(i)] = generator(i / sampleRate, c);
        return audio;
    };
    const auto fade = [seconds](double t) { return std::min({1.0, t / 0.01, (seconds - t) / 0.05}); };
    result.push_back({"saw 55 Hz", stereo([&](double t, int) {
        double v = 0.0;
        for (int h = 1; h * 55.0 < 18000.0; ++h) v += std::sin(2.0 * pi * 55.0 * h * t) / h;
        return static_cast<float>(0.18 * v * fade(t));
    })});
    result.push_back({"FM growl 110 Hz", stereo([&](double t, int) {
        const double ph = 2.0 * pi * 110.0 * t;
        return static_cast<float>(0.4 * std::tanh(1.3 * std::sin(ph + 2.4 * std::sin(ph))
                                                  + 0.35 * std::sin(3.0 * ph + 1.6 * std::sin(2.0 * ph))) * fade(t));
    })});
    result.push_back({"Reese 55 Hz", stereo([&](double t, int c) {
        double v = 0.0;
        const double a = 55.0 - 0.4 + 0.07 * c, b = 55.0 + 0.37 + 0.05 * c;
        for (int h = 1; h * 55.0 < 16000.0; ++h)
            v += (std::sin(2.0 * pi * a * h * t) + std::sin(2.0 * pi * b * h * t + 0.6 * h)) / h;
        return static_cast<float>(0.1 * v * fade(t));
    })});
    return result;
}

inline std::vector<std::vector<float>> render(juce::AudioProcessor& processor, const Probe& probe, PlayingHost& host)
{
    processor.setPlayHead(&host);
    processor.setRateAndBufferSizeDetails(sampleRate, blockSize);
    processor.prepareToPlay(sampleRate, blockSize);
    const int length = static_cast<int>(probe.audio[0].size());
    std::vector<std::vector<float>> out(2, std::vector<float>(static_cast<size_t>(length)));
    juce::MidiBuffer midi;
    host.samples = 0.0;
    for (int position = 0; position < length; position += blockSize) {
        const int count = std::min(blockSize, length - position);
        juce::AudioBuffer<float> buffer(2, count);
        for (int c = 0; c < 2; ++c)
            buffer.copyFrom(c, 0, probe.audio[static_cast<size_t>(c)].data() + position, count);
        processor.processBlock(buffer, midi);
        for (int c = 0; c < 2; ++c)
            std::copy_n(buffer.getReadPointer(c), count, out[static_cast<size_t>(c)].begin() + position);
        host.samples += count;
    }
    processor.releaseResources();
    processor.setPlayHead(nullptr);
    return out;
}

/** The plugin's own state inside a JUCE-hosted VST3's state: the IComponent
    stream, which is what a DAW stores in its project and hands back on load. */
inline juce::MemoryBlock componentState(const juce::MemoryBlock& hostedState)
{
    juce::MemoryBlock result;
    if (auto xml = juce::AudioProcessor::getXmlFromBinary(hostedState.getData(), static_cast<int>(hostedState.getSize())))
        if (auto* component = xml->getChildByName("IComponent"))
            result.fromBase64Encoding(component->getAllSubText());
    return result;
}

inline int runCompatibility(const juce::String& pluginPath, const juce::String& newPluginPath)
{
    if (!juce::File::isAbsolutePath(pluginPath) || !juce::File(pluginPath).exists()
        || !juce::File::isAbsolutePath(newPluginPath) || !juce::File(newPluginPath).exists()) {
        std::cerr << "Both VST3 paths must be existing absolute paths.\n";
        return 2;
    }
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions, newDescriptions;
    format.findAllTypesForFile(descriptions, pluginPath);
    format.findAllTypesForFile(newDescriptions, newPluginPath);
    if (descriptions.isEmpty() || newDescriptions.isEmpty()) { std::cerr << "No VST3 type found\n"; return 1; }
    std::cout << "Reference: " << descriptions[0]->name << " " << descriptions[0]->version << " from " << pluginPath << "\n"
              << "Current:   " << newDescriptions[0]->name << " " << newDescriptions[0]->version << " from " << newPluginPath << "\n";

    // Settings: the 24 factory sounds of 0.4.2, plus edge cases.
    struct Setting { juce::String name; std::vector<std::pair<const char*, float>> values; };
    std::vector<Setting> settings;
    for (const auto& preset : factoryBank()) {
        if (preset.engine != EngineMode::original) continue;
        settings.push_back({"0.4.2 " + preset.name, {
            {"shift", preset.shift}, {"focus", preset.focus}, {"bite", preset.bite}, {"drive", preset.drive},
            {"mix", preset.mix}, {"subSafe", 1.0f}, {"subCut", preset.subCut}, {"output", 0.0f},
            {"motionEnable", preset.motion ? 1.0f : 0.0f}, {"motionShape", (float) preset.shape},
            {"motionRate", (float) preset.rate}, {"motionDepth", preset.depth},
            {"motionSync", preset.sync ? 1.0f : 0.0f}, {"motionFreeHz", preset.freeHz},
            {"bypass", 0.0f}, {"vowel", 0.0f}}});
    }
    settings.push_back({"vowel colour, sub safe off, output -3 dB", {
        {"shift", 5.0f}, {"focus", .8f}, {"bite", .9f}, {"drive", .5f}, {"mix", .7f}, {"subSafe", 0.0f},
        {"subCut", 90.0f}, {"output", -3.0f}, {"motionEnable", 1.0f}, {"motionShape", 3.0f}, {"motionRate", 0.0f},
        {"motionDepth", 9.0f}, {"motionSync", 1.0f}, {"motionFreeHz", 2.0f}, {"bypass", 0.0f}, {"vowel", 1.0f}}});
    settings.push_back({"internal bypass", {
        {"shift", -7.0f}, {"focus", .6f}, {"bite", .5f}, {"drive", .3f}, {"mix", 1.0f}, {"subSafe", 1.0f},
        {"subCut", 120.0f}, {"output", 0.0f}, {"motionEnable", 0.0f}, {"motionShape", 0.0f}, {"motionRate", 1.0f},
        {"motionDepth", 3.0f}, {"motionSync", 1.0f}, {"motionFreeHz", 2.0f}, {"bypass", 1.0f}, {"vowel", 0.0f}}});

    const auto signals = probes(1.6);
    double worstAll = 0.0;
    int failures = 0;
    for (const auto& setting : settings) {
        juce::String error;
        auto reference = format.createInstanceFromDescription(*descriptions[0], sampleRate, blockSize, error);
        if (!reference) { std::cerr << "Could not open 0.4.2: " << error << "\n"; return 1; }
        // Set the 0.4.2 instance's parameters (its first 16 are SHIFTY's own, in
        // the same order and ranges as today), let one block deliver them, and
        // save its state.
        reference->prepareToPlay(sampleRate, blockSize);
        static ShiftyProcessor ranges;
        const auto ownIds = ShiftyProcessor::randomizableParameterIds();
        for (const auto& [id, value] : setting.values) {
            const int index = ownIds.indexOf(id);
            auto* own = ranges.parameters().getParameter(id);
            if (index < 0 || index >= 16 || own == nullptr || index >= reference->getParameters().size()) {
                std::cerr << "Unknown 0.4.2 parameter " << id << "\n";
                return 1;
            }
            reference->getParameters()[index]->setValueNotifyingHost(own->convertTo0to1(value));
        }
        {
            juce::AudioBuffer<float> silence(2, blockSize);
            silence.clear();
            juce::MidiBuffer midi;
            reference->processBlock(silence, midi);
        }
        reference->releaseResources();
        juce::MemoryBlock saved;
        reference->getStateInformation(saved);
        const auto pluginState = componentState(saved);

        // Two paths: the 0.5.0 VST3 bundle hosted exactly like 0.4.2 (the DAW
        // path), and the processor in-process given the plugin's own state.
        double worst = 0.0, worstRelative = 0.0;
        int referenceLatency = -1, hostedLatency = -1, latency = -1;
        bool original = true;
        for (const auto& probe : signals) {
            // Fresh instances everywhere, as when a DAW opens a project.
            PlayingHost hostA, hostB, hostC;
            auto opened = format.createInstanceFromDescription(*descriptions[0], sampleRate, blockSize, error);
            if (!opened) { std::cerr << "Could not open 0.4.2: " << error << "\n"; return 1; }
            opened->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            const auto expected = render(*opened, probe, hostA);
            referenceLatency = opened->getLatencySamples();

            auto hosted = format.createInstanceFromDescription(*newDescriptions[0], sampleRate, blockSize, error);
            if (!hosted) { std::cerr << "Could not open 0.5.0: " << error << "\n"; return 1; }
            hosted->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            const auto hostedOut = render(*hosted, probe, hostB);
            hostedLatency = hosted->getLatencySamples();

            ShiftyProcessor current;
            current.setStateInformation(pluginState.getData(), static_cast<int>(pluginState.getSize()));
            const auto actual = render(current, probe, hostC);
            latency = current.getLatencySamples();
            original = original && current.engineMode() == EngineMode::original;
            for (const auto* output : { &hostedOut, &actual }) {
                double error2 = 0.0, energy = 0.0;
                for (size_t c = 0; c < 2; ++c)
                    for (size_t i = 0; i < expected[c].size(); ++i) {
                        const double d = static_cast<double>(expected[c][i]) - (*output)[c][i];
                        worst = std::max(worst, std::abs(d));
                        error2 += d * d;
                        energy += static_cast<double>(expected[c][i]) * expected[c][i];
                    }
                worstRelative = std::max(worstRelative, std::sqrt(error2 / std::max(energy, 1.0e-30)));
            }
        }
        const bool ok = worst < 1.0e-5 && latency == referenceLatency && hostedLatency == referenceLatency && original;
        if (!ok) ++failures;
        worstAll = std::max(worstAll, worst);
        std::cout << (ok ? "PASS " : "FAIL ") << std::left << std::setw(44) << setting.name.toStdString()
                  << " max |difference| " << std::setprecision(3) << worst
                  << ", relative RMS " << worstRelative
                  << ", latency " << hostedLatency << " hosted / " << latency << " in-process (0.4.2: "
                  << referenceLatency << ")" << (original ? ", ORIGINAL SOUND" : ", NOT ORIGINAL") << "\n";
    }
    std::cout << (failures == 0 ? "0.4.2 compatibility passed: " : "0.4.2 compatibility FAILED: ")
              << settings.size() << " states x " << signals.size() << " signals, worst |difference| "
              << worstAll << "\n";
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Factory preset audit: every factory sound on a raw 55 Hz saw, an FM growl and
// a Reese. Reports level change, spectral change (per-harmonic dB spread and
// centroid shift) and peak; fails on clipping or on a near no-op on the saw.

struct Measure { double levelDb, spreadDb, centroidSt, peak; };

inline double harmonic(const std::vector<float>& x, int start, int length, double frequency)
{
    std::complex<double> sum = 0.0;
    for (int i = 0; i < length; ++i)
        sum += static_cast<double>(x[static_cast<size_t>(start + i)])
             * std::polar(1.0, -2.0 * pi * frequency * (start + i) / sampleRate);
    return 2.0 * std::abs(sum) / length;
}

inline Measure measure(const std::vector<float>& in, const std::vector<float>& out, double f0)
{
    // Long windows of whole periods average Motion and envelope movement.
    const int period = static_cast<int>(std::lround(sampleRate / f0));
    const int start = static_cast<int>(0.5 * sampleRate);
    const int length = (static_cast<int>(2.5 * sampleRate) / period) * period;
    std::vector<double> change;
    double centroidIn = 0, energyIn = 0, centroidOut = 0, energyOut = 0, peakIn = 0;
    std::vector<double> a, b;
    for (int h = 1; h * f0 < 8000.0; ++h) {
        a.push_back(harmonic(in, start, length, h * f0));
        b.push_back(harmonic(out, start, length, h * f0));
        peakIn = std::max(peakIn, a.back());
    }
    for (size_t k = 0; k < a.size(); ++k) {
        const double f = (k + 1) * f0;
        if (f < 150.0) continue;
        centroidIn += f * a[k] * a[k]; energyIn += a[k] * a[k];
        centroidOut += f * b[k] * b[k]; energyOut += b[k] * b[k];
        if (a[k] > peakIn * 1.0e-3) change.push_back(20.0 * std::log10((b[k] + 1.0e-12) / a[k]));
    }
    const double mean = std::accumulate(change.begin(), change.end(), 0.0) / std::max<size_t>(1, change.size());
    double variance = 0.0;
    for (auto v : change) variance += (v - mean) * (v - mean);
    double rmsIn = 0, rmsOut = 0, peak = 0;
    for (int i = start; i < start + length; ++i) {
        rmsIn += static_cast<double>(in[static_cast<size_t>(i)]) * in[static_cast<size_t>(i)];
        rmsOut += static_cast<double>(out[static_cast<size_t>(i)]) * out[static_cast<size_t>(i)];
    }
    for (auto v : out) peak = std::max(peak, static_cast<double>(std::abs(v)));
    return { 10.0 * std::log10(rmsOut / rmsIn), std::sqrt(variance / std::max<size_t>(1, change.size())),
             12.0 * std::log2((centroidOut / energyOut) / (centroidIn / energyIn)), peak };
}

inline int runPresetAudit()
{
    const auto signals = probes(3.2);
    const double fundamentals[] = { 55.0, 110.0, 55.0 };
    const auto& bank = factoryBank();
    int failures = 0;
    std::cout << "Factory preset audit (input RMS about -15 dBFS, peaks <= 0.6): level dB / spread dB / centroid st / peak\n";
    std::cout << std::fixed << std::setprecision(2);
    for (size_t index = 0; index < bank.size(); ++index) {
        const auto& preset = bank[index];
        std::cout << std::left << std::setw(13) << preset.folder.toStdString() << std::setw(18) << preset.name.toStdString();
        bool sawNoOp = false, clipped = false;
        for (size_t s = 0; s < signals.size(); ++s) {
            ShiftyProcessor processor;
            processor.loadFactoryPreset(static_cast<int>(index));
            PlayingHost host;
            const auto out = render(processor, signals[s], host);
            const int latency = processor.getLatencySamples();
            std::vector<float> aligned(out[0].begin() + latency, out[0].end());
            aligned.resize(out[0].size(), 0.0f);
            const auto m = measure(signals[s].audio[0], aligned, fundamentals[s]);
            std::cout << " | " << std::right << std::setw(6) << m.levelDb << std::setw(6) << m.spreadDb
                      << std::setw(7) << m.centroidSt << std::setw(5) << m.peak;
            clipped = clipped || m.peak >= 0.999 || !std::isfinite(m.peak);
            if (s == 0) sawNoOp = m.spreadDb < 1.0 && std::abs(m.centroidSt) < 1.0;
        }
        const bool exempt = preset.engine == EngineMode::original; // 0.4.2 originals are kept as they were
        const bool ok = !clipped && (exempt || !sawNoOp);
        if (!ok) ++failures;
        std::cout << (ok ? "" : (clipped ? "  CLIPS" : "  NO-OP ON SAW")) << (exempt && sawNoOp ? "  (original, kept)" : "") << "\n";
    }
    std::cout << std::defaultfloat;
    std::cout << (failures == 0 ? "Preset audit passed: " : "Preset audit FAILED: ") << bank.size()
              << " factory presets, " << failures << " failing.\n";
    return failures == 0 ? 0 : 1;
}

} // namespace shifty::compat
