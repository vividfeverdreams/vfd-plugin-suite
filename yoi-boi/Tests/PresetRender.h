#pragma once

#include "Plugin/YoiBoiProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <iostream>

namespace yoiboi
{
inline bool renderFactoryDemos (const juce::File& folder)
{
    if (folder.createDirectory().failed()) return false;
    struct DemoPlayHead final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo position;
            position.setBpm (140.0);
            position.setIsPlaying (true);
            return position;
        }
    } playhead;
    constexpr int sampleRate = 48000, blockSize = 256, framesPerPreset = sampleRate * 3;
    YoiBoiProcessor synth (folder.getChildFile ("unused-test-presets"));
    synth.setPlayHead (&playhead);
    synth.prepareToPlay (sampleRate, blockSize);
    auto callbacks = synth.makeEditorCallbacks();
    std::vector<PresetItem> patches;
    for (const auto& item : callbacks.listPresets())
        if (! item.user && ! item.recent) patches.push_back (item);
    juce::AudioBuffer<float> tour (2, framesPerPreset * static_cast<int> (patches.size()));
    tour.clear();
    const auto write = [] (const juce::File& file, const juce::AudioBuffer<float>& audio)
    {
        auto fileStream = file.createOutputStream();
        if (fileStream == nullptr) return false;
        fileStream->setPosition (0);
        if (fileStream->truncate().failed()) return false;
        std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
            .withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24));
        return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    };
    int index = 0;
    for (const auto& item : patches)
    {
        callbacks.loadPreset (item.id);
        synth.releaseResources();
        synth.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> audio (2, framesPerPreset);
        audio.clear();
        // A fixed, dry MIDI phrase; preset timing/routing/gain stay as authored.
        struct Note { double start, end; int pitch; };
        constexpr Note notes[] { { 0.0, .38, 43 }, { .57, .77, 43 },
                                 { .86, 1.07, 46 }, { 1.28, 1.86, 43 } };
        double squareSum = 0.0, meanSum = 0.0;
        float peak = 0.0f;
        for (int start = 0; start < framesPerPreset; start += blockSize)
        {
            const int count = juce::jmin (blockSize, framesPerPreset - start);
            juce::AudioBuffer<float> block (audio.getArrayOfWritePointers(), 2, start, count);
            juce::MidiBuffer midi;
            for (const auto& note : notes)
            {
                const int on = juce::roundToInt (note.start * sampleRate);
                const int off = juce::roundToInt (note.end * sampleRate);
                if (on >= start && on < start + count)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note.pitch, (juce::uint8) 112), on - start);
                if (off >= start && off < start + count)
                    midi.addEvent (juce::MidiMessage::noteOff (1, note.pitch), off - start);
            }
            synth.processBlock (block, midi);
            for (int frame = 0; frame < count; ++frame)
            {
                const auto value = block.getSample (0, frame);
                if (! std::isfinite (value)) return false;
                peak = juce::jmax (peak, std::abs (value));
                squareSum += value * value;
                meanSum += value;
            }
        }
        const auto filename = juce::String (index + 1).paddedLeft ('0', 2) + "-"
                              + juce::File::createLegalFileName (item.name) + ".wav";
        if (! write (folder.getChildFile (filename), audio)) return false;
        for (int channel = 0; channel < 2; ++channel)
            tour.copyFrom (channel, index * framesPerPreset, audio, channel, 0, framesPerPreset);
        std::cout << item.name << ": peak=" << peak << ", RMS="
                  << std::sqrt (squareSum / framesPerPreset) << ", DC="
                  << meanSum / framesPerPreset << '\n';
        ++index;
    }
    synth.releaseResources();
    return write (folder.getChildFile ("YOI-BOI-factory-tour.wav"), tour);
}
}
