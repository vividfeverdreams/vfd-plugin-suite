#pragma once
#ifndef BACK_SCRATCH_COMPAT_HOST
#include "EditedPatternSerialization.h"
#include "ProcessorTests.h"
#endif
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <utility>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Projects saved by the installed 0.10.2 build must reopen in 0.11 with every
// setting intact and render the same audio. Step 1 (--compat-save) loads the
// actual 0.10.2 VST3 from the release payload (read-only), lets it save each
// project and records what it renders. Step 2 (--compat-check) restores those
// projects into the new VST3 and compares. The two builds share symbol names,
// so they run in separate processes.
namespace backScratch::compatibilityTests {

inline void require(bool ok, const std::string& text) { if (!ok) throw std::runtime_error(text); }

inline std::unique_ptr<juce::AudioPluginInstance> load(const juce::String& path)
{
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions, path);
    require(!descriptions.isEmpty(), "VST3 scans: " + path.toStdString());
    juce::String error;
    auto plugin = format.createInstanceFromDescription(*descriptions[0], 48000.0, 512, error);
    require(plugin != nullptr, "VST3 instantiates: " + error.toStdString());
    return plugin;
}

inline juce::AudioProcessorParameter& parameter(juce::AudioPluginInstance& plugin, const juce::String& name)
{
    for (auto* candidate : plugin.getParameters())
        if (candidate->getName(128) == name) return *candidate;
    throw std::runtime_error("missing parameter " + name.toStdString());
}

// Every value a scenario sets, so step 1 can prove the old build applied it.
inline std::vector<std::pair<juce::String, float>>& configured()
{
    static std::vector<std::pair<juce::String, float>> values;
    return values;
}

inline void set(juce::AudioPluginInstance& plugin, const juce::String& name, float normalized)
{
    auto& target = parameter(plugin, name);
    target.beginChangeGesture(); target.setValueNotifyingHost(normalized); target.endChangeGesture();
    configured().emplace_back(name, normalized);
}

inline std::vector<float> render(juce::AudioPluginInstance& plugin, const juce::MemoryBlock& state, int blocks)
{
    processorTests::Head head;
    plugin.setPlayHead(&head);
    plugin.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    plugin.prepareToPlay(48000.0, 512);
    std::vector<float> result;
    juce::MidiBuffer midi;
    for (int block = 0; block < blocks; ++block) {
        juce::AudioBuffer<float> audio(2, 512);
        for (int i = 0; i < 512; ++i) for (int ch = 0; ch < 2; ++ch) audio.setSample(ch, i, processorTests::testSound(block * 512 + i, ch));
        head.beat = block * 512.0 * head.bpm / (60.0 * 48000.0);
        plugin.processBlock(audio, midi);
        for (int i = 0; i < 512; ++i) for (int ch = 0; ch < 2; ++ch) result.push_back(audio.getSample(ch, i));
    }
    plugin.releaseResources();
    plugin.setPlayHead(nullptr);
    return result;
}

// The 0.10.2 controls the new build must restore exactly.
inline const juce::StringArray& legacyControls()
{
    static const juce::StringArray names {"Scratch Pattern", "Variation", "Keep Sub", "Bypass", "Generated Pattern", "Pattern Seed",
        "Phrase Length", "Phrase Length Lock", "Pattern Original Length", "Pattern Original Meter", "Phrase Timing",
        "Pattern Grid Shift", "Edited Pattern", "Pattern Motion Version"};
    return names;
}

struct Scenario { const char* file; const char* name; std::function<void(juce::AudioPluginInstance&)> configure; bool customGeometry; };

inline std::vector<Scenario> scenarios()
{
    return {
        {"factory", "factory recipe, dotted, shifted, Keep Sub off", [](juce::AudioPluginInstance& p) {
            set(p, "Scratch Pattern", 37.0f / 74.0f);
            set(p, "Phrase Timing", 0.5f);       // dotted
            set(p, "Pattern Grid Shift", 5.0f / 15.0f);
            set(p, "Keep Sub", 0.0f);
        }, false},
        {"generated", "generated pattern, two bars, triplet", [](juce::AudioPluginInstance& p) {
            set(p, "Generated Pattern", 1.0f);
            set(p, "Pattern Seed", 714923.0f / 1048575.0f);
            set(p, "Pattern Original Length", (8.0f - 0.25f) / (256.0f - 0.25f));
            set(p, "Phrase Length", 4.0f / 7.0f);
            set(p, "Phrase Timing", 1.0f);       // triplet
        }, false},
        {"legacy", "legacy motion recipe", [](juce::AudioPluginInstance& p) {
            set(p, "Scratch Pattern", 12.0f / 74.0f);
            set(p, "Pattern Motion Version", 0.0f);
        }, false},
        {"drawn", "hand-drawn custom pattern", [](juce::AudioPluginInstance&) {}, true},
    };
}

inline void writeAudio(const juce::File& file, const std::vector<float>& audio)
{
    file.deleteFile();
    juce::FileOutputStream output(file);
    require(output.openedOk() && output.write(audio.data(), audio.size() * sizeof(float)), "compatibility audio saved");
}

inline std::vector<float> readAudio(const juce::File& file)
{
    juce::MemoryBlock data;
    require(file.loadFileAsData(data), "compatibility audio found: " + file.getFullPathName().toStdString());
    std::vector<float> audio(data.getSize() / sizeof(float));
    std::memcpy(audio.data(), data.getData(), audio.size() * sizeof(float));
    return audio;
}

// Step 1, with the 0.10.2 payload VST3.
inline void saveWithPayload(const juce::String& oldPath, const juce::File& directory)
{
    require(directory.createDirectory().wasOk(), "compatibility folder");
    for (const auto& scenario : scenarios()) {
        // A fresh 0.10.2 instance per project, as when a user adds the
        // plug-in and sets it up; the 0.10.2 build writes the project state.
        auto old = load(oldPath);
        configured().clear();
        scenario.configure(*old);
        juce::MemoryBlock saved;
        old->getStateInformation(saved);
        if (scenario.customGeometry) {
            // Draw through the 0.10.2 state reader itself: give it a schema 9
            // project with a custom contour, then let it save that project.
            auto container = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
            require(container != nullptr, "the 0.10.2 VST3 state container decodes");
            juce::MemoryBlock component;
            for (auto* node : container->getChildIterator())
                if (node->getTagName() == "IComponent") component.fromBase64Encoding(node->getAllSubText());
            auto xml = juce::AudioProcessor::getXmlFromBinary(component.getData(), static_cast<int>(component.getSize()));
            require(xml != nullptr && xml->getIntAttribute("engineVersion") == 9, "the 0.10.2 build writes schema 9");
            ScratchPattern drawn;
            drawn.beats = 1.0; drawn.count = 4; drawn.editingRange = 0.25;
            drawn.segments[0] = {0.0, 0.25, 0.0, 0.0, 1.0f, 1.0f, 0.0f, false};
            drawn.segments[1] = {0.25, 0.4, 0.0, 0.12, 1.0f, 1.0f, 1.0f, true};
            drawn.segments[2] = {0.4, 0.7, 0.12, 0.0, 1.0f, 0.0f, 1.0f, true};
            drawn.segments[3] = {0.7, 1.0, 0.0, 0.0, 1.0f, 1.0f, 0.0f, false};
            for (auto* child : xml->getChildIterator())
                if (child->getStringAttribute("id") == "edited") child->setAttribute("value", 1.0);
            if (auto* oldGeometry = xml->getChildByName("EDITED_PATTERN")) xml->removeChildElement(oldGeometry, true);
            xml->addChildElement(editedPatternToXml(drawn).release());
            juce::MemoryBlock drawnState;
            juce::AudioProcessor::copyXmlToBinary(*xml, drawnState);
            for (auto* node : container->getChildIterator())
                if (node->getTagName() == "IComponent") { node->deleteAllTextElements(); node->addTextElement(drawnState.toBase64Encoding()); }
            saved.reset();
            juce::AudioProcessor::copyXmlToBinary(*container, saved);
            old->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            require(parameter(*old, "Edited Pattern").getValue() > 0.5f, "the 0.10.2 build accepts the drawn project");
            old->getStateInformation(saved);
        }
        require(directory.getChildFile(juce::String(scenario.file) + ".state").replaceWithData(saved.getData(), saved.getSize()),
                "0.10.2 project state saved");
        const auto audio = render(*old, saved, 240);
        writeAudio(directory.getChildFile(juce::String(scenario.file) + ".f32"), audio);
        // The saved project really holds the scenario's settings...
        for (const auto& [name, value] : configured())
            require(std::abs(parameter(*old, name).getValue() - value) < 1.0e-4f,
                    std::string(scenario.name) + ": the 0.10.2 project keeps " + name.toStdString());
        // ...and the 0.10.2 build is scratching, not passing the input through.
        double scratch = 0.0;
        for (int frame = 730; frame < static_cast<int>(audio.size() / 2); ++frame)
            scratch = std::max(scratch, static_cast<double>(std::abs(audio[static_cast<size_t>(frame) * 2]
                                                            - processorTests::testSound(frame - 730, 0))));
        require(scratch > 0.05, std::string(scenario.name) + ": the 0.10.2 render is scratched");
        juce::StringArray values;
        for (const auto& name : legacyControls()) values.add(name + "=" + juce::String(parameter(*old, name).getValue(), 8));
        require(directory.getChildFile(juce::String(scenario.file) + ".controls").replaceWithText(values.joinIntoString("\n")),
                "0.10.2 control values saved");
        std::cout << "0.10.2 payload saved and rendered: " << scenario.name << '\n';
    }
}

// Step 2, with the new VST3.
inline void checkAgainstPayload(const juce::String& newPath, const juce::File& directory)
{
    for (const auto& scenario : scenarios()) {
        auto current = load(newPath);   // a fresh instance, as when a project opens
        juce::MemoryBlock saved;
        require(directory.getChildFile(juce::String(scenario.file) + ".state").loadFileAsData(saved), "0.10.2 project state found");
        const auto before = readAudio(directory.getChildFile(juce::String(scenario.file) + ".f32"));
        const auto after = render(*current, saved, 240);
        require(before.size() == after.size(), "both builds render the same length");
        juce::StringArray lines;
        lines.addLines(directory.getChildFile(juce::String(scenario.file) + ".controls").loadFileAsString());
        for (const auto& line : lines) {
            if (line.isEmpty()) continue;
            const auto name = line.upToFirstOccurrenceOf("=", false, false);
            const float expected = line.fromFirstOccurrenceOf("=", false, false).getFloatValue();
            require(std::abs(parameter(*current, name).getValue() - expected) < 1.0e-6f,
                    std::string(scenario.name) + ": restores " + name.toStdString());
        }
        for (const auto* added : {"Engage", "Play Every", "Mix", "Output", "Depth", "Look Back", "Keep Sub Split", "Slot Chain",
                                  "Pattern Slot", "Retrigger", "Engage Quantize"}) {
            auto& control = parameter(*current, added);
            require(std::abs(control.getValue() - control.getDefaultValue()) < 1.0e-6f,
                    std::string(scenario.name) + ": a 0.10.2 project leaves " + added + " at its neutral default");
        }
        double worst = 0.0, energy = 0.0;
        for (size_t i = 0; i < before.size(); ++i) {
            worst = std::max(worst, static_cast<double>(std::abs(before[i] - after[i])));
            energy += static_cast<double>(before[i]) * before[i];
        }
        std::cout << "0.10.2 payload -> 0.11 " << scenario.name << ": maximum difference " << worst << " over "
                  << before.size() / 2 << " stereo frames\n";
        require(energy > 1.0, "the comparison renders real audio");
        require(worst < 0.00003, std::string(scenario.name) + ": a 0.10.2 project renders the same in 0.11");
    }
    std::cout << "BACK-SCRATCH 0.10.2 project compatibility passed\n";
}

} // namespace backScratch::compatibilityTests
