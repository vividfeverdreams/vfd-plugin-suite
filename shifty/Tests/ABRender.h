#pragma once

// --ab-batch <old SHIFTY.vst3> <jobs.txt>
//
// Renders audio files through SHIFTY for A/B listening and measurements. Each
// line of jobs.txt is  mode|input.wav|output.wav|spec  with mode:
//   new     this build, in process. spec: "Folder/Name" of a factory preset,
//           or "default" (a fresh instance), then optional ";id=value" overrides
//           (any parameter ID, or engine=talk|mouth|classic|original).
//   old     the old bundle (e.g. the signed 0.5.4), hosted as a VST3, given the
//           0.5.4 factory preset of that name (Tests/LegacyPresets054.h) through
//           its saved state, or "default" for a fresh old instance.
//   legacy  the 0.5.4 preset rendered by this build (for identity checks).
//   state   a saved SHIFTY state file (spec = path), rendered by this build.
// Inputs are read with JUCE and rendered at 48 kHz in 512-sample blocks with the
// transport playing at 140 BPM; outputs are 32-bit float WAV.

#include "Compatibility.h"
#include "LegacyPresets054.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <ctime>

namespace shifty::abrender {

using namespace shifty::compat;

inline bool readWav(const juce::File& file, juce::AudioBuffer<float>& audio)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) return false;
    audio.setSize(2, static_cast<int>(reader->lengthInSamples));
    reader->read(&audio, 0, audio.getNumSamples(), 0, true, true);
    if (reader->numChannels == 1) audio.copyFrom(1, 0, audio, 0, 0, audio.getNumSamples());
    return true;
}

inline bool writeWav(const juce::File& file, const juce::AudioBuffer<float>& audio)
{
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (stream == nullptr) return false;
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(sampleRate)
        .withNumChannels(2).withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    return writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
}

inline juce::AudioBuffer<float> renderThrough(juce::AudioProcessor& processor, const juce::AudioBuffer<float>& input)
{
    PlayingHost host;
    processor.setPlayHead(&host);
    processor.setRateAndBufferSizeDetails(sampleRate, blockSize);
    processor.prepareToPlay(sampleRate, blockSize);
    juce::AudioBuffer<float> out(input);
    juce::MidiBuffer midi;
    for (int position = 0; position < out.getNumSamples(); position += blockSize) {
        const int count = std::min(blockSize, out.getNumSamples() - position);
        juce::AudioBuffer<float> block(out.getArrayOfWritePointers(), 2, position, count);
        processor.processBlock(block, midi);
        host.samples += count;
    }
    processor.releaseResources();
    processor.setPlayHead(nullptr);
    return out;
}

inline void setValue(ShiftyProcessor& p, const juce::String& id, float value)
{
    if (auto* parameter = p.parameters().getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

inline void unlockEverything(ShiftyProcessor& p)
{
    for (const auto& id : ShiftyProcessor::randomizableParameterIds()) p.setParameterLocked(id, false);
    p.setParameterLocked(ShiftyProcessor::engineLockId, false);
}

/** Every field of a preset, the way applyFactoryPreset sets it, with nothing locked. */
inline void applyPreset(ShiftyProcessor& p, const FactoryPreset& preset)
{
    const std::pair<const char*, float> values[] = {
        {"shift", preset.shift}, {"focus", preset.focus}, {"bite", preset.bite}, {"drive", preset.drive},
        {"mix", preset.mix}, {"motionEnable", preset.motion ? 1.0f : 0.0f}, {"motionShape", (float) preset.shape},
        {"motionRate", (float) preset.rate}, {"motionDepth", preset.depth}, {"subSafe", preset.subSafe ? 1.0f : 0.0f},
        {"subCut", preset.subCut}, {"output", preset.output}, {"motionSync", preset.sync ? 1.0f : 0.0f},
        {"motionFreeHz", preset.freeHz}, {"bypass", 0.0f}, {"vowel", preset.vowelColor ? 1.0f : 0.0f},
        {"vowelMorph", preset.morph}, {"voiceSet", (float) preset.voiceSet}, {"character", preset.character},
        {"talk", preset.talk}, {"envAmount", preset.envAmount}, {"envAttack", preset.envAttack},
        {"envRelease", preset.envRelease}, {"yoiAmount", preset.yoiAmount}, {"yoiFrom", preset.yoiFrom},
        {"yoiTo", preset.yoiTo}, {"yoiTime", preset.yoiTime}, {"yoiShift", preset.yoiShift},
        {"motionTarget", (float) preset.target}, {"driveModel", (float) preset.driveModel}, {"smash", preset.smash},
        {"smashTime", preset.smashTime}, {"smashBalance", preset.smashBalance} };
    for (const auto& [id, value] : values) setValue(p, id, value);
    p.setEngine(preset.engine, preset.lowLatency);
}

inline const FactoryPreset* findPreset(const std::vector<FactoryPreset>& bank, const juce::String& spec)
{
    const auto folder = spec.upToFirstOccurrenceOf("/", false, false);
    const auto name = spec.fromFirstOccurrenceOf("/", false, false);
    for (const auto& preset : bank)
        if (preset.folder == folder && preset.name == name) return &preset;
    return nullptr;
}

/** Applies "id=value" overrides (engine=talk|mouth|classic|original too). */
inline void applyOverrides(ShiftyProcessor& p, const juce::StringArray& overrides)
{
    for (const auto& item : overrides) {
        const auto key = item.upToFirstOccurrenceOf("=", false, false).trim();
        const auto value = item.fromFirstOccurrenceOf("=", false, false).trim();
        if (key.isEmpty()) continue;
        if (key == "engine") {
            const auto mode = value == "mouth" ? EngineMode::mouth : value == "classic" ? EngineMode::classic
                            : value == "original" ? EngineMode::original : EngineMode::talk;
            p.setEngine(mode, false);
        } else {
            setValue(p, key, value.getFloatValue());
        }
    }
}

inline int runBatch(const juce::String& oldPath, const juce::File& jobs)
{
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> oldTypes;
    format.findAllTypesForFile(oldTypes, oldPath);
    int failures = 0;
    for (const auto& line : juce::StringArray::fromLines(jobs.loadFileAsString())) {
        if (line.trim().isEmpty() || line.startsWith("#")) continue;
        juce::StringArray parts;
        parts.addTokens(line, "|", "");
        if (parts.size() < 4) { std::cerr << "Bad job: " << line << "\n"; ++failures; continue; }
        const auto mode = parts[0].trim();
        juce::AudioBuffer<float> input;
        if (!readWav(juce::File(parts[1].trim()), input)) { std::cerr << "Can't read " << parts[1] << "\n"; ++failures; continue; }
        juce::StringArray spec;
        spec.addTokens(parts[3], ";", "");
        const auto head = spec[0].trim();
        spec.remove(0);
        juce::AudioBuffer<float> out;
        if (mode == "old") {
            if (oldTypes.isEmpty()) { std::cerr << "No old VST3\n"; return 1; }
            juce::String error;
            auto instance = format.createInstanceFromDescription(*oldTypes[0], sampleRate, blockSize, error);
            if (!instance) { std::cerr << error << "\n"; ++failures; continue; }
            if (head != "default") {
                const auto* preset = findPreset(legacy054::bank(), head);
                if (preset == nullptr) { std::cerr << "No 0.5.4 preset " << head << "\n"; ++failures; continue; }
                ShiftyProcessor reference;
                unlockEverything(reference);
                applyPreset(reference, *preset);
                // Lock defaults as a 0.5.4 instance has them (OUTPUT and BYPASS).
                for (const auto& id : ShiftyProcessor::randomizableParameterIds())
                    reference.setParameterLocked(id, id == "output" || id == "bypass");
                juce::MemoryBlock state;
                reference.getStateInformation(state);
                juce::XmlElement hosted("VST3PluginState");
                hosted.createNewChildElement("IComponent")->addTextElement(state.toBase64Encoding());
                juce::MemoryBlock wrapped;
                juce::AudioProcessor::copyXmlToBinary(hosted, wrapped);
                instance->setStateInformation(wrapped.getData(), static_cast<int>(wrapped.getSize()));
            }
            out = renderThrough(*instance, input);
        } else {
            ShiftyProcessor processor;
            unlockEverything(processor);
            if (mode == "state") {
                juce::MemoryBlock state;
                juce::File(head).loadFileAsData(state);
                processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            } else if (head != "default") {
                const auto* preset = findPreset(mode == "legacy" ? legacy054::bank() : factoryBank(), head);
                if (preset == nullptr) { std::cerr << "No preset " << head << "\n"; ++failures; continue; }
                applyPreset(processor, *preset);
            }
            applyOverrides(processor, spec);
            out = renderThrough(processor, input);
        }
        if (!writeWav(juce::File(parts[2].trim()), out)) { std::cerr << "Can't write " << parts[2] << "\n"; ++failures; continue; }
        std::cout << "rendered " << parts[2].trim() << "\n";
    }
    return failures == 0 ? 0 : 1;
}


// --cpu: thread CPU time per instance, stereo, 48 kHz, 64-sample buffers, 10 s of a bass-like signal, for each
// engine with the 0.5 drive (SOFT, SMASH off) and with each analog model plus SMASH at 50%.
inline int runCpu()
{
    constexpr int block = 64;
    const int length = static_cast<int>(sampleRate * 10.0);
    juce::AudioBuffer<float> source(2, length);
    for (int i = 0; i < length; ++i) {
        const double t = i / sampleRate;
        double v = 0.0;
        for (int h = 1; h * 55.0 < 16000.0; ++h) v += std::sin(2.0 * pi * 55.0 * h * t) / h;
        source.setSample(0, i, static_cast<float>(0.15 * v));
        source.setSample(1, i, static_cast<float>(0.15 * v));
    }
    struct Setup { const char* name; EngineMode engine; bool lowLatency; };
    const Setup engines[] { {"TALK", EngineMode::talk, false}, {"MOUTH", EngineMode::mouth, false},
                            {"CLASSIC", EngineMode::classic, false}, {"CLASSIC LOW LATENCY", EngineMode::classic, true},
                            {"ORIGINAL 0.4.2", EngineMode::original, false} };
    const char* models[] { "SOFT, SMASH off", "TAPE + SMASH 50%", "TUBE + SMASH 50%", "FUZZ + SMASH 50%" };
    std::cout << "engine | setup | average CPU | 99th percentile callback (budget " << 1.0e6 * block / sampleRate << " us)\n";
    for (const auto& engine : engines) {
        for (int model = 0; model < 4; ++model) {
            ShiftyProcessor processor;
            unlockEverything(processor);
            setValue(processor, "drive", model == 0 ? 0.3f : 0.5f);
            setValue(processor, "driveModel", static_cast<float>(model));
            setValue(processor, "smash", model == 0 ? 0.0f : 0.5f);
            processor.setEngine(engine.engine, engine.lowLatency);
            PlayingHost host;
            processor.setPlayHead(&host);
            processor.setRateAndBufferSizeDetails(sampleRate, block);
            processor.prepareToPlay(sampleRate, block);
            juce::AudioBuffer<float> buffer(2, block);
            juce::MidiBuffer midi;
            std::vector<double> callbacks;
            callbacks.reserve(static_cast<size_t>(length / block + 1));
            timespec start {}, end {};
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
            for (int position = 0; position + block <= length; position += block) {
                for (int c = 0; c < 2; ++c) buffer.copyFrom(c, 0, source, c, position, block);
                const auto before = juce::Time::getHighResolutionTicks();
                processor.processBlock(buffer, midi);
                callbacks.push_back(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - before));
                host.samples += block;
            }
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
            const double seconds = static_cast<double>(end.tv_sec - start.tv_sec) + 1.0e-9 * static_cast<double>(end.tv_nsec - start.tv_nsec);
            std::sort(callbacks.begin(), callbacks.end());
            const double p99 = callbacks[static_cast<size_t>(0.99 * static_cast<double>(callbacks.size() - 1))];
            std::cout << engine.name << " | " << models[model] << " | " << std::fixed << std::setprecision(2)
                      << 100.0 * seconds / 10.0 << "% | " << std::setprecision(0) << 1.0e6 * p99 << " us\n";
            processor.releaseResources();
            processor.setPlayHead(nullptr);
        }
    }
    return 0;
}

} // namespace shifty::abrender
