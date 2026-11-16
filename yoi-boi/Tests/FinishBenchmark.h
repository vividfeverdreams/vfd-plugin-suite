#pragma once
#include "CompatibilityFixtures.h"
#include <chrono>
namespace yoiboi
{
inline bool benchmarkFinish (const juce::String& oldPath, const juce::String& newPath)
{
    juce::VST3PluginFormatHeadless format;
    double totals[2][2] {};
    for (int width = 0; width < 2; ++width)
        for (int which = 0; which < 2; ++which)
        {
            juce::OwnedArray<juce::PluginDescription> descriptions;
            format.findAllTypesForFile (descriptions, which == 0 ? oldPath : newPath);
            if (descriptions.size() != 1) return false;
            juce::String error; auto plugin = format.createInstanceFromDescription (*descriptions[0], 48000, 256, error);
            if (!plugin) { std::cerr << error << '\n'; return false; }
            if (!compatibility::setParameter (*plugin, "Body Width", width == 0 ? 0.f : .75f)) return false;
            plugin->setPlayConfigDetails (0, 2, 48000, 256);
            std::vector<double> times;
            for (int run = 0; run < 7; ++run)
            {
                plugin->prepareToPlay (48000, 256);
                compatibility::FixturePlayHead head; head.bpm = 140; plugin->setPlayHead (&head);
                juce::AudioBuffer<float> buffer (2, 256); juce::MidiBuffer midi;
                double seconds = 0;
                for (int start = 0; start < 48000 * 8; start += 256)
                {
                    buffer.clear(); midi.clear();
                    if (start % 24000 < 256) midi.addEvent (juce::MidiMessage::noteOn (1, (start / 24000) % 2 ? 43 : 36, (juce::uint8)112), 0);
                    if (start % 24000 >= 18000 && start % 24000 < 18256) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                    head.sample = start;
                    const auto began = std::chrono::steady_clock::now(); plugin->processBlock (buffer, midi);
                    seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - began).count();
                }
                plugin->releaseResources(); plugin->setPlayHead (nullptr);
                if (run > 0) times.push_back (seconds);
            }
            std::sort (times.begin(), times.end()); const double median = .5 * (times[2] + times[3]);
            totals[width][which] = median;
            std::cout << descriptions[0]->version << " width=" << width * .75 << " callback median=" << median
                      << " seconds / 8 sec audio, thread=" << median / 8 * 100 << "%; runs=";
            for (auto t : times) std::cout << t << ',';
            std::cout << '\n';
        }
    bool ok = true;
    for (int width = 0; width < 2; ++width)
    {
        const auto ratio = totals[width][1] / totals[width][0];
        std::cout << "CPU width=" << width * .75 << " ratio=" << ratio << '\n'; ok = ok && ratio < 1.6;
    }
    return ok;
}
}
