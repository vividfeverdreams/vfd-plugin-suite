#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <vector>

#include "Patch/Patch.h"

namespace yoiboi
{
// Renders sound variations to WAV files on its own thread (never the audio
// thread). Each file is the one-bar phrase from ParameterMap.h at the given
// tempo plus the release tail, latency-compensated so it starts on the beat.
class BounceRenderer final : private juce::Thread
{
public:
    struct Request
    {
        std::vector<Patch> variants;
        double bpm = 120.0;
        double sampleRate = 48000.0;
        juce::File folder;
    };
    struct Status
    {
        bool running = false;
        int done = 0, total = 0;
        juce::File folder;
        std::vector<juce::File> files;   // in variant order; missing entries are invalid files
        std::vector<juce::String> names;
        juce::String error;
    };

    BounceRenderer();
    ~BounceRenderer() override;

    // Returns false while a bounce is already running or the folder can't be created.
    bool start (Request request);
    void cancel();
    [[nodiscard]] Status status() const;
    // Blocks until the current bounce finishes (tests and shutdown only).
    bool waitUntilFinished (int timeoutMilliseconds);

    // Offline render of one variant into an interleaved stereo buffer; public for tests.
    static bool renderVariant (const Patch& patch, double bpm, double sampleRate, std::vector<float>& left, std::vector<float>& right);
    static juce::File defaultFolder();

private:
    void run() override;

    mutable juce::CriticalSection lock;
    Request pending;
    Status current;
};
} // namespace yoiboi
