#pragma once
#include "BackScratchProcessor.h"
#include "AutomationBridge.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace backScratch::processorTests {
inline void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
struct Head final : juce::AudioPlayHead {
    bool playing = true;
    double beat = 0, bpm = 140;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setIsPlaying(playing); p.setBpm(bpm); p.setPpqPosition(beat);
        p.setTimeSignature(TimeSignature{4, 4}); return p;
    }
};
inline float testSound(int frame, int channel = 0) {
    const double t = frame / 48000.0;
    return static_cast<float>(0.24 * std::sin(t * 6.283185307179586 * 49)
        + 0.18 * std::sin(t * 6.283185307179586 * (channel == 0 ? 441 : 667))
        * (0.65 + 0.35 * std::sin(t * 23)));
}
inline std::vector<float> render(int blockSize, const juce::MemoryBlock* saved = nullptr, int frames = 96000) {
    Head head;
    BackScratchProcessor p;
    p.setPlayHead(&head);
    p.prepareToPlay(48000, blockSize);
    if (saved) p.setStateInformation(saved->getData(), static_cast<int>(saved->getSize()));
    else { p.setHostParameter("factory", 5); p.setHostParameter("variation", 18421); }
    std::vector<float> result(static_cast<size_t>(frames * 2));
    juce::MidiBuffer midi;
    for (int start = 0; start < frames; start += blockSize) {
        const int n = std::min(blockSize, frames - start);
        juce::AudioBuffer<float> audio(2, n);
        for (int i = 0; i < n; ++i) for (int ch = 0; ch < 2; ++ch) audio.setSample(ch, i, testSound(start + i, ch));
        head.beat = start * head.bpm / (60.0 * 48000);
        p.processBlock(audio, midi);
        for (int i = 0; i < n; ++i) for (int ch = 0; ch < 2; ++ch) {
            const float v = audio.getSample(ch, i);
            check(std::isfinite(v), "processor output finite");
            result[static_cast<size_t>((start + i) * 2 + ch)] = v;
        }
    }
    return result;
}
inline std::vector<float> timedRender(int blockSize, bool restoreBeforeFirstBlock = false) {
    Head head;
    BackScratchProcessor p;
    p.setPlayHead(&head); p.prepareToPlay(48000, blockSize);
    if (restoreBeforeFirstBlock) {
        juce::MemoryBlock state; p.getStateInformation(state);
        p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    }
    const struct Change { int sample; const char* id; float value; } changes[] {
        {255, "factory", 17}, {255, "variation", 2712}, {900, "keepSub", 0},
        {3371, "factory", 11}, {3371, "variation", 0}, {17555, "keepSub", 1},
        {38005, "generated", 1}, {38005, "generationSeed", 864111}, {38005, "phraseDuration", 5},
        {52007, "generationSeed", 1048575}, {61000, "bypass", 1}, {64000, "bypass", 0}
    };
    const int frames = 96000;
    std::vector<float> result(static_cast<size_t>(frames));
    juce::MidiBuffer midi;
    for (int start = 0; start < frames; start += blockSize) {
        const int n = std::min(blockSize, frames - start);
        backScratchBridge::current.clear();
        for (const auto& c : changes) if (c.sample >= start && c.sample < start + n) {
            auto* parameter = p.parameters().getParameter(c.id);
            const float normalized = parameter->convertTo0to1(c.value);
            backScratchBridge::current.push(parameter, c.sample - start, normalized);
            parameter->setValueNotifyingHost(normalized);
        }
        juce::AudioBuffer<float> audio(2, n);
        for (int i = 0; i < n; ++i) audio.setSample(0, i, testSound(start + i)), audio.setSample(1, i, testSound(start + i, 1));
        head.beat = start * head.bpm / (60.0 * 48000);
        p.processBlock(audio, midi);
        for (int i = 0; i < n; ++i) result[static_cast<size_t>(start + i)] = audio.getSample(0, i);
    }
    return result;
}
inline void same(const std::vector<float>& a, const std::vector<float>& b, const char* message) {
    check(a.size() == b.size(), message);
    double worst = 0;
    for (size_t i = 0; i < a.size(); ++i) worst = std::max(worst, static_cast<double>(std::abs(a[i] - b[i])));
    if (worst > 0.00003) { std::cerr << message << " maximum difference " << worst << '\n'; throw std::runtime_error(message); }
}
}

inline void runProcessorTests()
{
    using namespace backScratch;
    using namespace backScratch::processorTests;
    BackScratchProcessor p;
    p.prepareToPlay(48000, 512);
    check(p.keepSub(), "Keep Sub is on by default");
    check(p.getNumPrograms() == 75, "75 factory presets exposed to host");
    p.setKeepSub(false);
    for (int i = 0; i < 32; ++i) {
        const auto before = p.generationSeed();
        const int factoryBefore = p.selectedFactoryPreset();
        p.randomizePattern();
        check(p.isGenerated() && p.generationSeed() != before, "randomize composes a fresh generated pattern");
        check(p.selectedFactoryPreset() == factoryBefore, "randomize does not choose a factory preset");
        check(!p.keepSub(), "randomize preserves chosen sub mode");
    }
    p.selectFactoryPreset(5); p.setHostParameter("variation", 18421); p.setKeepSub(true);
    juce::MemoryBlock saved; p.getStateInformation(saved);
    check(saved.getSize() < 16384, "state contains settings, not captured audio");
    same(render(64), render(257), "preset playback independent of 64/257 block size");
    same(render(64), render(1024), "preset playback independent of 64/1024 block size");
    same(render(257), render(257, &saved), "saved settings reproduce exact generated variation");
    same(timedRender(64), timedRender(257), "sample-timed pattern/sub changes independent of buffer size");
    same(timedRender(64), timedRender(1024), "sample-timed randomization repeats with large buffers");
    same(timedRender(1024), timedRender(1024, true), "restoring state does not anticipate future automation points");

    const auto preset = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("BACK-SCRATCH-Test", ".backscratch");
    const auto invalid = preset.getSiblingFile(preset.getFileNameWithoutExtension() + "-invalid.backscratch");
    check(p.savePreset(preset).wasOk(), "custom preset saves");
    p.randomizePattern(); p.setKeepSub(false);
    p.setPhraseLocked(false); // a locked phrase keeps its own length and timing on preset loads
    check(p.loadPreset(preset).wasOk(), "custom preset loads");
    check(p.selectedFactoryPreset() == 5 && p.variationSeed() == 18421 && p.keepSub(), "preset restores exact recipe and sub choice");
    check(p.currentPresetName() == preset.getFileNameWithoutExtension(), "saved preset name visible");
    invalid.replaceWithText("<BACK_SCRATCH_PRESET version=\"4\" factory=\"500\" variation=\"1\" keepSub=\"1\"/>");
    check(p.loadPreset(invalid).failed(), "invalid preset rejected");
    check(p.selectedFactoryPreset() == 5 && p.variationSeed() == 18421, "invalid load leaves sound unchanged");
    preset.deleteFile(); invalid.deleteFile();
    juce::MemoryBlock namedState; p.getStateInformation(namedState);
    BackScratchProcessor restored; restored.prepareToPlay(48000, 64);
    restored.setStateInformation(namedState.getData(), static_cast<int>(namedState.getSize()));
    check(restored.currentPresetName() == p.currentPresetName(), "custom name survives project save");

    // A jump starts a new history. Once the dry latency pipeline has drained,
    // the result matches a new processor started at the destination, so the
    // previous phrase never leaks into a scratch. Inside that window the
    // pre-seek dry audio plays out: there is no 15 ms hole.
    Head headA, headB;
    BackScratchProcessor sought, fresh;
    sought.setPlayHead(&headA); fresh.setPlayHead(&headB);
    sought.prepareToPlay(48000, 512); fresh.prepareToPlay(48000, 512);
    juce::AudioBuffer<float> a(2, 512), b(2, 512); juce::MidiBuffer midi;
    for (int block = 0; block < 100; ++block) {
        for (int i = 0; i < 512; ++i) a.setSample(0, i, testSound(block * 512 + i)), a.setSample(1, i, testSound(block * 512 + i, 1));
        headA.beat = block * 512.0 * headA.bpm / (48000.0 * 60.0);
        sought.processBlock(a, midi);
    }
    const int latency = sought.getLatencySamples();
    check(latency > 0, "shared audio path latency is reported to the host");
    double tailEnergy = 0.0;
    for (int block = 0; block < 10; ++block) {
        headA.beat = headB.beat = 24 + block * 512.0 * headA.bpm / (48000.0 * 60.0);
        for (int i = 0; i < 512; ++i) for (int ch = 0; ch < 2; ++ch) a.setSample(ch, i, testSound(block * 512 + i, ch));
        b.makeCopyOf(a); sought.processBlock(a, midi); fresh.processBlock(b, midi);
        for (int i = 0; i < 512; ++i) {
            const int frame = block * 512 + i;
            if (frame < latency) {
                tailEnergy += static_cast<double>(a.getSample(0, i)) * a.getSample(0, i);
                check(std::abs(a.getSample(0, i) - testSound(100 * 512 - latency + frame)) < 0.000001f,
                      "a seek plays out the pre-seek dry pipeline instead of a gap");
            } else {
                check(std::abs(a.getSample(0, i) - b.getSample(0, i)) < 0.000001f, "seek does not replay stale audio");
            }
        }
    }
    check(tailEnergy > 1.0, "the dry tail after a seek is audible, not silent");
    headA.playing = false;
    for (int block = 0; block < 8; ++block) {
        for (int i = 0; i < 512; ++i) for (int ch = 0; ch < 2; ++ch) a.setSample(ch, i, testSound(block * 512 + i, ch));
        sought.processBlock(a, midi);
        for (int i = 0; i < 512; ++i) {
            const int sourceFrame = block * 512 + i - latency;
            // Before the new input arrives, the last played audio finishes.
            const float expected = sourceFrame >= 0 ? testSound(sourceFrame) : testSound(10 * 512 + sourceFrame);
            check(std::abs(a.getSample(0, i) - expected) < 0.000001f, "stopped transport is transparent at the reported latency");
        }
    }

    juce::MemoryBlock oldState;
    juce::MemoryOutputStream oldStream(oldState, false); oldStream.writeInt(0x42534331); oldStream.writeInt(2);
    restored.setStateInformation(oldState.getData(), static_cast<int>(oldState.getSize()));
    check(restored.parameters().getRawParameterValue("bypass")->load() == 1, "old captured-wheel projects open dry");
    restored.selectFactoryPreset(1);
    check(restored.parameters().getRawParameterValue("bypass")->load() == 0, "choosing new pattern enables live engine");
    std::cout << "BACK-SCRATCH live processor, automation, preset and transport tests passed\n";
}
