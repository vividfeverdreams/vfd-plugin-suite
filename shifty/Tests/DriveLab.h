#pragma once

// DRIVE and SMASH bench tools for ShiftyDspTests:
//   --calibrate-drive <wav>...   level compensation tables for the DRIVE models
//   --drive <in.wav> <out.wav> <model> <amount>
//   --smash <in.wav> <out.wav> <amount> <time> <balance> <subSafe 0/1> <subCut>
// Audio files are read with JUCE, processed at their own rate, written as 32-bit float WAV.

#include "../Source/Dsp/Drive.h"
#include "../Source/Dsp/Finisher.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iomanip>
#include <iostream>

namespace drivelab
{

inline bool readAudio (const juce::File& file, juce::AudioBuffer<float>& audio, double& rate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return false;
    rate = reader->sampleRate;
    audio.setSize (2, static_cast<int> (reader->lengthInSamples));
    reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
    if (reader->numChannels == 1)
        audio.copyFrom (1, 0, audio, 0, 0, audio.getNumSamples());
    return true;
}

inline bool writeAudio (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                                   .withNumChannels (audio.getNumChannels())
                                                   .withBitsPerSample (32)
                                                   .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer == nullptr)
        return false;
    return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

inline double meanSquare (const juce::AudioBuffer<float>& audio, int from = 0)
{
    double sum = 0.0;
    int count = 0;
    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int i = from; i < audio.getNumSamples(); ++i)
        {
            sum += static_cast<double> (audio.getSample (c, i)) * audio.getSample (c, i);
            ++count;
        }
    return sum / std::max (1, count);
}

inline void runDrive (juce::AudioBuffer<float>& audio, double rate, int model, float amount)
{
    std::array<shifty::DriveStage, 2> stages;
    for (auto& s : stages)
        s.prepare (rate);
    for (int c = 0; c < 2; ++c)
    {
        auto* data = audio.getWritePointer (c);
        for (int i = 0; i < audio.getNumSamples(); ++i)
            data[i] = stages[static_cast<size_t> (c)].process (data[i], amount, model);
    }
}

inline int calibrateDrive (const juce::StringArray& paths)
{
    std::vector<std::pair<juce::AudioBuffer<float>, double>> sources;
    for (const auto& path : paths)
    {
        juce::AudioBuffer<float> audio;
        double rate = 48000.0;
        if (! readAudio (juce::File (path), audio, rate))
        {
            std::cerr << "Can't read " << path << "\n";
            return 1;
        }
        // Loop short files to at least 3 s, then set the level to -12 dBFS RMS.
        const int wanted = static_cast<int> (3.0 * rate);
        if (audio.getNumSamples() < wanted)
        {
            juce::AudioBuffer<float> looped (2, wanted);
            for (int i = 0; i < wanted; ++i)
                for (int c = 0; c < 2; ++c)
                    looped.setSample (c, i, audio.getSample (c, i % audio.getNumSamples()));
            audio = looped;
        }
        audio.applyGain (static_cast<float> (std::pow (10.0, -12.0 / 20.0) / std::sqrt (meanSquare (audio))));
        sources.emplace_back (audio, rate);
    }
    std::cout << std::fixed << std::setprecision (2);
    for (int model = 1; model < shifty::driveModelCount; ++model)
    {
        std::cout << "model " << model << ":";
        for (int step = 0; step <= 16; ++step)
        {
            const float amount = static_cast<float> (step) / 16.0f;
            double sumDb = 0.0, lowest = 100.0, highest = -100.0;
            for (const auto& [source, rate] : sources)
            {
                auto copy = source;
                runDrive (copy, rate, model, amount);
                const int skip = static_cast<int> (0.3 * rate);
                const double db = 10.0 * std::log10 (meanSquare (source, skip) / std::max (1.0e-20, meanSquare (copy, skip)));
                sumDb += db;
                lowest = std::min (lowest, db);
                highest = std::max (highest, db);
            }
            std::cout << " " << sumDb / static_cast<double> (sources.size());
            if (step % 4 == 0) std::cout << "[" << lowest << ".." << highest << "]";
        }
        std::cout << "\n";
    }
    return 0;
}

inline int driveFile (const juce::String& in, const juce::String& out, int model, float amount)
{
    juce::AudioBuffer<float> audio;
    double rate = 48000.0;
    if (! readAudio (juce::File (in), audio, rate))
        return 1;
    const double before = meanSquare (audio);
    runDrive (audio, rate, model, amount);
    std::cout << "level change " << 10.0 * std::log10 (meanSquare (audio) / before) << " dB, peak "
              << audio.getMagnitude (0, audio.getNumSamples()) << "\n";
    return writeAudio (juce::File (out), audio, rate) ? 0 : 1;
}

inline int smashFile (const juce::String& in, const juce::String& out, float amount, float time, float balance,
                      bool subSafe, float subCut)
{
    juce::AudioBuffer<float> audio;
    double rate = 48000.0;
    if (! readAudio (juce::File (in), audio, rate))
        return 1;
    const double before = meanSquare (audio);
    shifty::Finisher finisher;
    finisher.prepare (rate, 2);
    shifty::Finisher::Params p;
    p.amount = amount;
    p.time = time;
    p.balance = balance;
    p.subSafe = subSafe;
    p.subCut = subCut;
    finisher.setParameters (p);
    for (int start = 0; start < audio.getNumSamples(); start += 256)
    {
        const int count = std::min (256, audio.getNumSamples() - start);
        juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), 2, start, count);
        finisher.process (view);
        if (std::getenv ("SMASH_DEBUG") != nullptr && (start / 256) % 94 == 0)
        {
            const auto& m = finisher.meter();
            std::cout << std::fixed << std::setprecision (1) << start / rate << "s gain " << m.gainDb[0] << " " << m.gainDb[1] << " "
                      << m.gainDb[2] << "  level " << m.levelDb[0] << " " << m.levelDb[1] << " " << m.levelDb[2] << "\n";
        }
    }
    std::cout << "level change " << 10.0 * std::log10 (meanSquare (audio) / before) << " dB, peak "
              << audio.getMagnitude (0, audio.getNumSamples()) << "\n";
    return writeAudio (juce::File (out), audio, rate) ? 0 : 1;
}

} // namespace drivelab
