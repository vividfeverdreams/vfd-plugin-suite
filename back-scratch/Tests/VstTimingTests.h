#pragma once

#include "BackScratchProcessor.h"
#include "PlaybackAlignmentTests.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace backScratch::vstTimingTests {

inline void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

inline juce::AudioProcessorParameter& parameterNamed(juce::AudioPluginInstance& plugin, const char* name)
{
    juce::AudioProcessorParameter* found = nullptr;
    for (auto* parameter : plugin.getParameters()) {
        if (parameter->getName(128) != name) continue;
        require(found == nullptr, "packaged VST3 control names identify parameters uniquely");
        found = parameter;
    }
    if (found == nullptr) throw std::runtime_error(std::string("Packaged VST3 is missing control: ") + name);
    return *found;
}

inline void setBoth(juce::AudioPluginInstance& plugin, BackScratchProcessor& direct,
                    const char* id, const char* name, float actual)
{
    auto* parameter = direct.parameters().getParameter(id);
    require(parameter != nullptr, "direct timing control exists");
    auto& hosted = parameterNamed(plugin, name);
    const float normalized = parameter->convertTo0to1(actual);
    hosted.beginChangeGesture(); hosted.setValueNotifyingHost(normalized); hosted.endChangeGesture();
    direct.setHostParameter(id, actual);
}

inline void compare(const std::vector<float>& a, const std::vector<float>& b, const char* message)
{
    require(a.size() == b.size(), message);
    double largest = 0.0;
    for (size_t index = 0; index < a.size(); ++index) {
        require(std::isfinite(a[index]) && std::isfinite(b[index]), "packaged timing output is finite on both channels");
        largest = std::max(largest, std::abs(static_cast<double>(a[index] - b[index])));
    }
    if (largest >= 0.00003) std::cerr << message << "; maximum difference " << largest << '\n';
    require(largest < 0.00003, message);
}

// Since 0.11 a project restore starts a new history without emptying the dry
// latency pipeline, so the first latency samples still carry the audio that
// was playing. Everything after them must match a freshly prepared instance.
inline void compareAfterRestore(const std::vector<float>& fresh, const std::vector<float>& restored,
                                int latencySamples, const char* message)
{
    require(fresh.size() == restored.size(), message);
    const auto skip = static_cast<std::ptrdiff_t>(latencySamples * 2);
    compare(std::vector<float>(fresh.begin() + skip, fresh.end()),
            std::vector<float>(restored.begin() + skip, restored.end()), message);
}

inline std::vector<float> render(juce::AudioPluginInstance& plugin, BackScratchProcessor& direct,
                                 playbackAlignmentTests::Head& head, bool automate, int blocks)
{
    constexpr int blockSize = 512;
    constexpr double rate = 48000.0, samplesPerBeat = 24000.0, firstBeat = 2.375;
    std::vector<float> hosted(static_cast<size_t>(blocks * blockSize * 2));
    std::vector<float> reference(hosted.size());
    juce::MidiBuffer midi;
    double maximumDifference = 0.0;
    for (int block = 0; block < blocks; ++block) {
        if (automate) {
            if (block == 3) setBoth(plugin, direct, "motionVersion", "Pattern Motion Version", 1.0f);
            if (block == 12) setBoth(plugin, direct, "motionVersion", "Pattern Motion Version", 2.0f);
            if (block == 5) setBoth(plugin, direct, "phraseTiming", "Phrase Timing", 2.0f);
            if (block == 9) setBoth(plugin, direct, "patternShift", "Pattern Grid Shift", 15.0f);
            if (block == 14) setBoth(plugin, direct, "phraseDuration", "Phrase Length", 3.0f);
            if (block == 17) setBoth(plugin, direct, "keepSub", "Keep Sub", 1.0f);
            if (block == 20) {
                setBoth(plugin, direct, "generationSeed", "Pattern Seed", 714923.0f);
                setBoth(plugin, direct, "sourceBeats", "Pattern Original Length", 8.0f);
                setBoth(plugin, direct, "generated", "Generated Pattern", 1.0f);
            }
            if (block == 23) setBoth(plugin, direct, "phraseDuration", "Phrase Length", 1.0f);
            if (block == 30) setBoth(plugin, direct, "phraseTiming", "Phrase Timing", 0.0f);
            if (block == 35) setBoth(plugin, direct, "patternShift", "Pattern Grid Shift", 7.0f);
        }
        const int start = block * blockSize;
        const double unwrapped = firstBeat + start / samplesPerBeat;
        head.beat = unwrapped < head.loopEnd ? unwrapped
            : head.loopStart + std::fmod(unwrapped - head.loopStart, head.loopEnd - head.loopStart);
        juce::AudioBuffer<float> a(2, blockSize), b(2, blockSize);
        for (int sample = 0; sample < blockSize; ++sample)
            for (int channel = 0; channel < 2; ++channel)
                a.setSample(channel, sample, playbackAlignmentTests::source(start + sample, rate, channel));
        b.makeCopyOf(a);
        plugin.processBlock(a, midi); direct.processBlock(b, midi);
        for (int sample = 0; sample < blockSize; ++sample) {
            for (int channel = 0; channel < 2; ++channel) {
                const auto index = static_cast<size_t>((start + sample) * 2 + channel);
                hosted[index] = a.getSample(channel, sample);
                reference[index] = b.getSample(channel, sample);
                maximumDifference = std::max(maximumDifference,
                    std::abs(static_cast<double>(hosted[index] - reference[index])));
            }
        }
    }
    compare(hosted, reference, "packaged VST3 timing, shifts and host-loop playback match the direct processor");
    std::cout << "Packaged VST3 " << (automate ? "automated" : "recalled/static")
              << " timing and looping maximum difference " << maximumDifference << '\n';
    return hosted;
}

} // namespace backScratch::vstTimingTests

inline void runVstTimingTests(juce::AudioPluginInstance& plugin, backScratch::BackScratchProcessor& direct)
{
    using namespace backScratch;
    using namespace backScratch::vstTimingTests;
    playbackAlignmentTests::Head head;
    head.looping = true; head.loopStart = 2.125; head.loopEnd = 2.875;
    struct RestorePlayHeads {
        juce::AudioPluginInstance& plugin;
        BackScratchProcessor& direct;
        juce::AudioPlayHead* oldPlugin;
        juce::AudioPlayHead* oldDirect;
        ~RestorePlayHeads() { plugin.setPlayHead(oldPlugin); direct.setPlayHead(oldDirect); }
    } restore {plugin, direct, plugin.getPlayHead(), direct.getPlayHead()};
    plugin.releaseResources(); direct.releaseResources();
    plugin.setPlayHead(&head); direct.setPlayHead(&head);
    setBoth(plugin, direct, "factory", "Scratch Pattern", 8.0f);
    setBoth(plugin, direct, "variation", "Variation", 0.0f);
    setBoth(plugin, direct, "generated", "Generated Pattern", 0.0f);
    setBoth(plugin, direct, "generationSeed", "Pattern Seed", 83177.0f);
    setBoth(plugin, direct, "sourceBeats", "Pattern Original Length", 4.0f);
    setBoth(plugin, direct, "sourceBarBeats", "Pattern Original Meter", 4.0f);
    setBoth(plugin, direct, "phraseDuration", "Phrase Length", 0.0f);
    setBoth(plugin, direct, "phraseTiming", "Phrase Timing", 1.0f);
    setBoth(plugin, direct, "patternShift", "Pattern Grid Shift", 5.0f);
    setBoth(plugin, direct, "keepSub", "Keep Sub", 0.0f);
    // Bypass is also the host's bypass (since 0.11.3); it stays off here.
    direct.setHostParameter("bypass", 0.0f);
    plugin.prepareToPlay(48000.0, 512); direct.prepareToPlay(48000.0, 512);
    require(plugin.getLatencySamples() == direct.getLatencySamples(), "packaged timing mode keeps reported audio latency");
    render(plugin, direct, head, true, 144);

    // Save a triplet-generated pattern with a visible offset, change all of
    // those properties, then restore through the actual VST3 state container.
    setBoth(plugin, direct, "phraseDuration", "Phrase Length", 0.0f);
    setBoth(plugin, direct, "phraseTiming", "Phrase Timing", 2.0f);
    setBoth(plugin, direct, "patternShift", "Pattern Grid Shift", 9.0f);
    setBoth(plugin, direct, "keepSub", "Keep Sub", 0.0f);
    plugin.releaseResources(); direct.releaseResources();
    plugin.prepareToPlay(48000.0, 512); direct.prepareToPlay(48000.0, 512);
    const auto beforeRecall = render(plugin, direct, head, false, 80);
    juce::MemoryBlock hostedState, directState;
    plugin.getStateInformation(hostedState); direct.getStateInformation(directState);
    require(hostedState.getSize() > 0 && hostedState.getSize() < 1024 * 1024,
            "packaged VST3 serializes the new controls without embedded audio history");
    const auto timingValue = parameterNamed(plugin, "Phrase Timing").getValue();
    const auto shiftValue = parameterNamed(plugin, "Pattern Grid Shift").getValue();
    const auto durationValue = parameterNamed(plugin, "Phrase Length").getValue();
    setBoth(plugin, direct, "phraseDuration", "Phrase Length", 7.0f);
    setBoth(plugin, direct, "phraseTiming", "Phrase Timing", 1.0f);
    setBoth(plugin, direct, "patternShift", "Pattern Grid Shift", 0.0f);
    setBoth(plugin, direct, "generated", "Generated Pattern", 0.0f);
    render(plugin, direct, head, false, 8);
    plugin.setStateInformation(hostedState.getData(), static_cast<int>(hostedState.getSize()));
    direct.setStateInformation(directState.getData(), static_cast<int>(directState.getSize()));
    require(std::abs(parameterNamed(plugin, "Phrase Timing").getValue() - timingValue) < 1e-7f
            && std::abs(parameterNamed(plugin, "Pattern Grid Shift").getValue() - shiftValue) < 1e-7f
            && std::abs(parameterNamed(plugin, "Phrase Length").getValue() - durationValue) < 1e-7f,
            "VST3 project recall restores timing, length and grid-shift host parameters");
    const auto afterRecall = render(plugin, direct, head, false, 80);
    compareAfterRestore(beforeRecall, afterRecall, plugin.getLatencySamples(),
                        "VST3 state recall reproduces exact shifted triplet audio through loop wraps");
    std::cout << "Packaged VST3 dotted/triplet, grid-shift, loop and project-recall tests passed\n";
}
