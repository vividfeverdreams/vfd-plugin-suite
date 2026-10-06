#pragma once

#include "Plugin/YoiBoiProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <iomanip>
#include <iostream>

namespace yoiboi
{
// A short description of the engine-4 systems a factory sound uses.
inline juce::String describeSystems (const Patch& p)
{
    if (p.soundEngine < Patch::firstExtendedEngine) return "original 0.1.3 engine";
    juce::StringArray parts;
    const juce::StringArray rates { "1/16", "1/8", "1/4", "1/2", "1 bar" }, feels { "", "T", "D" };
    if (p.talkMode == 1) parts.add ("talk " + rates[p.talkDivision] + feels[p.talkFeel]);
    if (p.talkMode == 2) parts.add ("mod-wheel talk");
    if (p.talkMode == 3) parts.add ("aftertouch talk");
    parts.add (juce::String (vowelName (p.vowelStart)) + "-" + vowelName (p.vowelBody) + "-" + vowelName (p.vowelEnd));
    if (p.onsetGlide != 0) parts.add (p.onsetGlide == 1 ? "W glide" : "Y glide");
    if (p.consonant != 0) parts.add (juce::StringArray { "", "T", "K", "CH" }[p.consonant] + " attack");
    if (std::abs (p.pitchEnvSemitones) > 0.01f)
        parts.add ((p.pitchEnvSemitones > 0 ? "drop " : "rise ") + juce::String (std::abs (p.pitchEnvSemitones), 0) + " st");
    if (p.shaper != 0) parts.add (juce::StringArray { "", "diode", "fold", "crush" }[p.shaper]);
    if (p.squash > 0.01f) parts.add ("squash " + juce::String (juce::roundToInt (p.squash * 100)) + "%");
    if (p.width > 0.01f) parts.add ("width " + juce::String (juce::roundToInt (p.width * 100)) + "%");
    if (p.velocityToMouth + p.velocityToBite + p.velocityToDirt > 0.01f) parts.add ("velocity");
    if (p.glideMode == 1) parts.add ("legato glide");
    if (p.subPhaseReset) parts.add ("sub reset");
    if (p.subMode == 2) parts.add ("driven sub");
    if (p.bendRange != 2) parts.add ("bend +/-" + juce::String (p.bendRange));
    return parts.joinIntoString (", ");
}

// Objective render statistics for one preset demo.
struct DemoStats
{
    float peak = 0.0f;
    double rms = 0.0, dc = 0.0, loudness = -120.0; // loudness: K-weighted, ungated, LUFS-like
    int clippedSamples = 0;
    bool finite = true;
};

// ITU-R BS.1770 K-weighting (pre-filter shelf + RLB high-pass) as two biquads,
// integrated without gating: a "LUFS-ish" figure for comparing presets.
class KWeighting
{
public:
    explicit KWeighting (double sampleRate)
    {
        // Coefficients from BS.1770-4 for 48 kHz.
        juce::ignoreUnused (sampleRate);
        a = { { { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 },
                { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 } } };
    }
    double process (double x)
    {
        for (std::size_t s = 0; s < 2; ++s)
        {
            const auto& c = a[s];
            const double y = c[0] * x + c[1] * x1[s] + c[2] * x2[s] - c[3] * y1[s] - c[4] * y2[s];
            x2[s] = x1[s]; x1[s] = x; y2[s] = y1[s]; y1[s] = y;
            x = y;
        }
        return x;
    }
private:
    std::array<std::array<double, 5>, 2> a {};
    std::array<double, 2> x1 {}, x2 {}, y1 {}, y2 {};
};

inline bool renderFactoryDemos (const juce::File& folder, bool writeAudio = true)
{
    if (folder.createDirectory().failed()) return false;
    struct DemoPlayHead final : juce::AudioPlayHead
    {
        juce::int64 sample = 0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo position;
            position.setBpm (140.0);
            position.setIsPlaying (true);
            position.setTimeInSamples (sample);
            position.setPpqPosition (static_cast<double> (sample) / 48000.0 * 140.0 / 60.0);
            position.setTimeSignature (TimeSignature { 4, 4 });
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
    juce::AudioBuffer<float> tour (2, writeAudio ? framesPerPreset * static_cast<int> (patches.size()) : 1);
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
    std::cout << "folder\tpreset\tengine\tpeak\tpeak_dBFS\tLUFSish\tRMS_dB\tDC\tclipped\tsystems\n";
    int index = 0;
    bool allSane = true;
    for (const auto& item : patches)
    {
        callbacks.loadPreset (item.id);
        synth.releaseResources();
        synth.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> audio (2, framesPerPreset);
        audio.clear();
        // A fixed, dry MIDI phrase with a mod-wheel and pressure sweep for the
        // scrubbed talk modes; timing, routing and gain stay as authored.
        struct Note { double start, end; int pitch; };
        constexpr Note notes[] { { 0.0, .38, 43 }, { .57, .77, 43 },
                                 { .86, 1.07, 46 }, { 1.28, 1.86, 43 } };
        DemoStats stats;
        KWeighting left (sampleRate), right (sampleRate);
        double weightedSum = 0.0;
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
            const auto sweep = static_cast<int> (64.0 + 63.0 * std::sin (start / static_cast<double> (sampleRate) * 6.0));
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, juce::jlimit (0, 127, sweep)), 0);
            midi.addEvent (juce::MidiMessage::channelPressureChange (1, juce::jlimit (0, 127, 127 - sweep)), 0);
            playhead.sample = start;
            synth.processBlock (block, midi);
            for (int frame = 0; frame < count; ++frame)
            {
                const auto l = block.getSample (0, frame), r = block.getSample (1, frame);
                if (! std::isfinite (l) || ! std::isfinite (r)) { stats.finite = false; continue; }
                stats.peak = juce::jmax (stats.peak, std::abs (l), std::abs (r));
                stats.clippedSamples += (std::abs (l) > 0.985f) + (std::abs (r) > 0.985f);
                stats.rms += 0.5 * (l * l + r * r);
                stats.dc += 0.5 * (l + r);
                const double kl = left.process (l), kr = right.process (r);
                weightedSum += kl * kl + kr * kr;
            }
        }
        stats.rms = std::sqrt (stats.rms / framesPerPreset);
        stats.dc /= framesPerPreset;
        // The demo phrase sounds for about 2 of the 3 seconds; integrate over the whole render.
        stats.loudness = weightedSum > 0.0 ? -0.691 + 10.0 * std::log10 (weightedSum / framesPerPreset) : -120.0;
        const bool sane = stats.finite && stats.peak > 0.01f && stats.peak <= 0.99f && std::abs (stats.dc) < 0.005;
        allSane = allSane && sane;
        if (writeAudio)
        {
            const auto filename = juce::String (index + 1).paddedLeft ('0', 2) + "-"
                                  + juce::File::createLegalFileName (item.name) + ".wav";
            if (! write (folder.getChildFile (filename), audio)) return false;
            for (int channel = 0; channel < 2; ++channel)
                tour.copyFrom (channel, index * framesPerPreset, audio, channel, 0, framesPerPreset);
        }
        std::cout << std::fixed << std::setprecision (3)
                  << item.folder << '\t' << item.name << '\t' << item.engine << '\t' << stats.peak << '\t'
                  << std::setprecision (1) << juce::Decibels::gainToDecibels (stats.peak, -120.0f) << '\t'
                  << stats.loudness << '\t' << juce::Decibels::gainToDecibels (static_cast<float> (stats.rms), -120.0f) << '\t'
                  << std::setprecision (5) << stats.dc << '\t' << stats.clippedSamples << '\t'
                  << describeSystems (allFactoryPresets()[static_cast<std::size_t> (item.id.fromFirstOccurrenceOf (":", false, false).getIntValue())])
                  << (sane ? "" : "\tCHECK") << std::defaultfloat << '\n';
        ++index;
    }
    synth.releaseResources();
    synth.setPlayHead (nullptr);
    std::cout << "Rendered " << index << " factory sounds; " << (allSane ? "all sane" : "SOME NEED ATTENTION") << '\n';
    if (writeAudio && ! write (folder.getChildFile ("YOI-BOI-factory-tour.wav"), tour)) return false;
    return allSane;
}
}
