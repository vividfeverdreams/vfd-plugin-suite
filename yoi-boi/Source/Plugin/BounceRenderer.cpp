#include "BounceRenderer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "Dsp/CompatibleVoice.h"
#include "ParameterMap.h"

namespace yoiboi
{
BounceRenderer::BounceRenderer() : juce::Thread ("YOI-BOI bounce") {}

BounceRenderer::~BounceRenderer() { stopThread (15000); }

juce::File BounceRenderer::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
        .getChildFile ("YOI-BOI Bounces")
        .getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d"));
}

bool BounceRenderer::start (Request request)
{
    {
        const juce::ScopedLock scoped (lock);
        if (current.running) return false;
    }
    if (request.variants.empty() || ! std::isfinite (request.bpm) || request.bpm <= 0.0) return false;
    stopThread (2000); // the previous run has already finished; join it
    if (! request.folder.createDirectory())
    {
        const juce::ScopedLock scoped (lock);
        current = {};
        current.error = "Could not create folder " + request.folder.getFileName();
        return false;
    }
    {
        const juce::ScopedLock scoped (lock);
        current = {};
        current.running = true;
        current.total = static_cast<int> (request.variants.size());
        current.folder = request.folder;
        for (const auto& variant : request.variants) current.names.push_back (variant.name);
        current.files.resize (request.variants.size());
        pending = std::move (request);
    }
    startThread (juce::Thread::Priority::low);
    return true;
}

void BounceRenderer::cancel() { signalThreadShouldExit(); }

BounceRenderer::Status BounceRenderer::status() const
{
    const juce::ScopedLock scoped (lock);
    return current;
}

bool BounceRenderer::waitUntilFinished (int timeoutMilliseconds)
{
    return waitForThreadToExit (timeoutMilliseconds);
}

bool BounceRenderer::renderVariant (const Patch& patch, double bpm, double sampleRate,
                                    std::vector<float>& left, std::vector<float>& right)
{
    auto voice = std::make_unique<dsp::CompatibleVoice>();
    voice->prepare (sampleRate);
    const auto values = parameterValuesFor (patch);
    const int engine = std::clamp (patch.soundEngine, 1, Patch::currentSoundEngine);
    const int latency = dsp::CompatibleVoice::latencyFor (engine);
    const double sixteenth = 60.0 / bpm / 4.0 * sampleRate;
    const int barFrames = static_cast<int> (std::lround (phraseSixteenths * sixteenth));
    const double releaseSeconds = engine >= Patch::firstExtendedEngine ? patch.releaseMilliseconds * 0.001 : 0.085;
    const int tailFrames = static_cast<int> (std::lround (std::clamp (0.35 + releaseSeconds * 6.0, 0.5, 4.0) * sampleRate));
    const int outputFrames = barFrames + tailFrames;
    left.assign (static_cast<std::size_t> (outputFrames), 0.0f);
    right.assign (static_cast<std::size_t> (outputFrames), 0.0f);

    HostTiming timing;
    timing.bpm = bpm; timing.hasBpm = true; timing.playing = true; timing.hasPpq = true;
    constexpr int block = 64;
    std::size_t step = 0;
    int noteOffAt = -1;
    const auto stepStart = [&] (std::size_t index) { return static_cast<int> (std::lround (phraseSteps[index].start * sixteenth)); };
    for (int start = 0; start < outputFrames + latency; start += block)
    {
        timing.ppq = static_cast<double> (start) / sampleRate * bpm / 60.0;
        voice->setControls (controlsFor (values, timing, engine, sampleRate, patch.fmRatio, patch.fmIndex, patch.resonance), engine);
        for (int n = 0; n < block; ++n)
        {
            const int position = start + n;
            if (position == noteOffAt) voice->noteOff();
            if (step < phraseSteps.size() && position == stepStart (step))
            {
                const auto& note = phraseSteps[step];
                voice->noteOn (note.note, note.velocity, patch.glideMilliseconds, true);
                noteOffAt = position + std::max (1, static_cast<int> (std::lround (note.length * sixteenth * 0.85)));
                ++step;
            }
            const auto frame = voice->renderFrame();
            const int out = position - latency;
            if (out >= 0 && out < outputFrames)
            {
                const float l = frame.left + frame.sub, r = frame.right + frame.sub;
                if (! std::isfinite (l) || ! std::isfinite (r)) return false;
                left[static_cast<std::size_t> (out)] = l;
                right[static_cast<std::size_t> (out)] = r;
            }
        }
    }
    return true;
}

void BounceRenderer::run()
{
    Request request;
    {
        const juce::ScopedLock scoped (lock);
        request = pending;
    }
    juce::WavAudioFormat wav;
    for (std::size_t i = 0; i < request.variants.size() && ! threadShouldExit(); ++i)
    {
        const auto& variant = request.variants[i];
        std::vector<float> left, right;
        juce::File written;
        if (renderVariant (variant, request.bpm, request.sampleRate, left, right))
        {
            const auto stem = juce::String (static_cast<int> (i + 1)).paddedLeft ('0', 2) + " "
                            + juce::File::createLegalFileName (variant.name);
            // Never overwrite: an existing name gets a numbered sibling.
            const auto file = request.folder.getNonexistentChildFile (stem, ".wav", false);
            juce::AudioBuffer<float> audio (2, static_cast<int> (left.size()));
            std::copy (left.begin(), left.end(), audio.getWritePointer (0));
            std::copy (right.begin(), right.end(), audio.getWritePointer (1));
            if (auto fileStream = file.createOutputStream())
            {
                std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                    .withSampleRate (request.sampleRate).withNumChannels (2).withBitsPerSample (24));
                if (writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
                    written = file;
            }
        }
        const juce::ScopedLock scoped (lock);
        current.files[i] = written;
        if (written == juce::File {}) current.error = "Could not write " + variant.name;
        ++current.done;
    }
    const juce::ScopedLock scoped (lock);
    current.running = false;
}
} // namespace yoiboi
