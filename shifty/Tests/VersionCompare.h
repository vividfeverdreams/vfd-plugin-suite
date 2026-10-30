#pragma once

// --version-compare hosts two SHIFTY.vst3 bundles (e.g. the released 0.5.3 and this build), loads every
// factory preset into both through their parameters, renders the compat probe signals through each, and
// reports how far apart the two outputs are and how strongly each one changes the dry sound.

#include "Compatibility.h"

namespace shifty::versioncompare {

using namespace shifty::compat;

inline double rms(const std::vector<float>& v, size_t from = 0)
{
    double sum = 0.0;
    for (size_t i = from; i < v.size(); ++i)
        sum += static_cast<double>(v[i]) * v[i];
    return std::sqrt(sum / std::max<size_t>(1, v.size() - from));
}

inline double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1e-12)); }

// Level of (out - dry delayed by latency) relative to the dry level: how much SHIFTY changed the sound.
inline double effectDb(const std::vector<std::vector<float>>& out, const std::vector<std::vector<float>>& dry, int latency)
{
    double change = 0.0, reference = 0.0;
    const size_t skip = static_cast<size_t>(sampleRate * 0.2) + static_cast<size_t>(std::max(0, latency));
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = skip; i < out[c].size(); ++i) {
            const float d = dry[c][i - static_cast<size_t>(std::max(0, latency))];
            change += (out[c][i] - d) * (out[c][i] - d);
            reference += d * d;
        }
    return db(std::sqrt(change / std::max(reference, 1e-20)));
}

inline int runVersionCompare(const juce::String& oldPath, const juce::String& newPath)
{
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> oldTypes, newTypes;
    format.findAllTypesForFile(oldTypes, oldPath);
    format.findAllTypesForFile(newTypes, newPath);
    if (oldTypes.isEmpty() || newTypes.isEmpty()) { std::cerr << "No VST3 type found\n"; return 1; }
    std::cout << "Old: " << oldTypes[0]->version << "  New: " << newTypes[0]->version << "\n";

    const auto signals = probes(1.6);
    ShiftyProcessor reference;
    const auto& own = reference.getParameters();
    int differing = 0, presets = 0;
    double worst = -400.0, sumEffectOld = 0.0, sumEffectNew = 0.0;
    for (int index = 0; index < static_cast<int>(factoryBank().size()); ++index) {
        reference.loadFactoryPreset(index);
        juce::String error;
        auto oldOne = format.createInstanceFromDescription(*oldTypes[0], sampleRate, blockSize, error);
        auto newOne = format.createInstanceFromDescription(*newTypes[0], sampleRate, blockSize, error);
        if (!oldOne || !newOne) { std::cerr << "Could not open: " << error << "\n"; return 1; }
        for (auto* instance : { oldOne.get(), newOne.get() }) {
            instance->prepareToPlay(sampleRate, blockSize);
            for (int p = 0; p < own.size() && p < instance->getParameters().size(); ++p)
                instance->getParameters()[p]->setValueNotifyingHost(own[p]->getValue());
            juce::AudioBuffer<float> silence(2, blockSize);
            silence.clear();
            juce::MidiBuffer midi;
            instance->processBlock(silence, midi);
            instance->releaseResources();
        }
        double presetWorst = -400.0, effectOld = 0.0, effectNew = 0.0;
        for (const auto& probe : signals) {
            PlayingHost hostA, hostB;
            const auto a = render(*oldOne, probe, hostA);
            const auto b = render(*newOne, probe, hostB);
            const int latencyA = oldOne->getLatencySamples(), latencyB = newOne->getLatencySamples();
            double diff = 0.0, level = 0.0;
            for (size_t c = 0; c < 2; ++c)
                for (size_t i = 0; i < a[c].size(); ++i) {
                    diff += (b[c][i] - a[c][i]) * (b[c][i] - a[c][i]);
                    level += a[c][i] * a[c][i];
                }
            presetWorst = std::max(presetWorst, db(std::sqrt(diff / std::max(level, 1e-20))));
            effectOld += effectDb(a, probe.audio, latencyA) / static_cast<double>(signals.size());
            effectNew += effectDb(b, probe.audio, latencyB) / static_cast<double>(signals.size());
            if (latencyA != latencyB)
                std::cout << "  latency differs on " << factoryBank()[(size_t) index].name << ": " << latencyA << " vs " << latencyB << "\n";
        }
        ++presets;
        sumEffectOld += effectOld;
        sumEffectNew += effectNew;
        worst = std::max(worst, presetWorst);
        const bool differs = presetWorst > -60.0;
        differing += differs ? 1 : 0;
        std::cout << (differs ? "DIFF " : "same ") << std::fixed << std::setprecision(1) << std::setw(7) << presetWorst
                  << " dB  effect old " << std::setw(6) << effectOld << " dB  new " << std::setw(6) << effectNew << " dB  "
                  << factoryBank()[(size_t) index].name << "\n";
    }
    std::cout << "\n" << presets << " presets; " << differing << " differ by more than -60 dB; worst " << worst
              << " dB; mean effect old " << sumEffectOld / presets << " dB, new " << sumEffectNew / presets << " dB\n";
    return 0;
}

// --render-states old.vst3 new.vst3 <folder of saved SHIFTY states> in.f32 <sample rate> <out folder>
// Loads each state (the plug-in's own state, as a DAW stores it) into both bundles, plays in.f32 (raw
// interleaved stereo float32) through each with the transport running at 140 BPM, writes
// <name>-0.5.3.f32 / <name>-0.5.4.f32 and reports the difference between the two.
inline int runRenderStates(const juce::String& oldPath, const juce::String& newPath, const juce::File& states,
                           const juce::File& input, double rate, const juce::File& outFolder)
{
    juce::MemoryBlock raw;
    if (!input.loadFileAsData(raw) || raw.getSize() < 8) { std::cerr << "Can't read " << input.getFullPathName() << "\n"; return 1; }
    const int length = static_cast<int>(raw.getSize() / (2 * sizeof(float)));
    const auto* samples = static_cast<const float*>(raw.getData());
    juce::AudioBuffer<float> dry(2, length);
    for (int i = 0; i < length; ++i)
        for (int c = 0; c < 2; ++c) dry.setSample(c, i, samples[2 * i + c]);
    outFolder.createDirectory();

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> oldTypes, newTypes;
    format.findAllTypesForFile(oldTypes, oldPath);
    format.findAllTypesForFile(newTypes, newPath);
    if (oldTypes.isEmpty() || newTypes.isEmpty()) { std::cerr << "No VST3 type found\n"; return 1; }

    const auto write = [&](const juce::AudioBuffer<float>& audio, const juce::File& file) {
        std::vector<float> interleaved(static_cast<size_t>(audio.getNumSamples()) * 2);
        for (int i = 0; i < audio.getNumSamples(); ++i)
            for (int c = 0; c < 2; ++c) interleaved[static_cast<size_t>(2 * i + c)] = audio.getSample(c, i);
        file.replaceWithData(interleaved.data(), interleaved.size() * sizeof(float));
    };
    const auto renderWith = [&](const juce::PluginDescription& type, const juce::MemoryBlock& pluginState) {
        juce::String error;
        auto instance = format.createInstanceFromDescription(type, rate, blockSize, error);
        juce::AudioBuffer<float> out(dry);
        if (!instance) { std::cerr << error << "\n"; return out; }
        juce::XmlElement hosted("VST3PluginState");
        hosted.createNewChildElement("IComponent")->addTextElement(pluginState.toBase64Encoding());
        juce::MemoryBlock wrapped;
        juce::AudioProcessor::copyXmlToBinary(hosted, wrapped);
        instance->setStateInformation(wrapped.getData(), static_cast<int>(wrapped.getSize()));
        PlayingHost host;
        instance->setPlayHead(&host);
        instance->prepareToPlay(rate, blockSize);
        juce::MidiBuffer midi;
        for (int position = 0; position < length; position += blockSize) {
            const int count = std::min(blockSize, length - position);
            juce::AudioBuffer<float> block(out.getArrayOfWritePointers(), 2, position, count);
            instance->processBlock(block, midi);
            host.samples += count;
        }
        instance->releaseResources();
        instance->setPlayHead(nullptr);
        return out;
    };
    for (const auto& file : states.findChildFiles(juce::File::findFiles, false, "*.state")) {
        juce::MemoryBlock state;
        file.loadFileAsData(state);
        const auto a = renderWith(*oldTypes[0], state);
        const auto b = renderWith(*newTypes[0], state);
        double diff = 0.0, level = 0.0, change = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < length; ++i) {
                const double d = b.getSample(c, i) - a.getSample(c, i);
                diff += d * d;
                level += static_cast<double>(a.getSample(c, i)) * a.getSample(c, i);
            }
        const auto name = file.getFileNameWithoutExtension();
        write(a, outFolder.getChildFile(name + "-0.5.3.f32"));
        write(b, outFolder.getChildFile(name + "-0.5.4.f32"));
        std::cout << std::fixed << std::setprecision(1) << std::setw(8) << db(std::sqrt(diff / std::max(level, 1e-20)))
                  << " dB difference  " << name << "\n";
    }
    return 0;
}

} // namespace shifty::versioncompare
