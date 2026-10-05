#include "ReverbEngine.h"
#include "Presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
bool countAllocations = false;
std::size_t realtimeAllocations = 0;
}

void* operator new (std::size_t size)
{
    if (countAllocations) ++realtimeAllocations;
    if (auto* memory = std::malloc (std::max<std::size_t> (size, 1))) return memory;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return ::operator new (size); }
void operator delete (void* memory) noexcept { std::free (memory); }
void operator delete[] (void* memory) noexcept { std::free (memory); }
void operator delete (void* memory, std::size_t) noexcept { std::free (memory); }
void operator delete[] (void* memory, std::size_t) noexcept { std::free (memory); }

namespace
{
using throwup::ReverbEngine;
using throwup::Settings;
constexpr double pi = 3.14159265358979323846;

void require (bool condition, const std::string& message)
{
    if (! condition) throw std::runtime_error (message);
}

struct Audio
{
    explicit Audio (std::size_t length) : left (length), right (length) {}
    std::vector<float> left, right;
};

constexpr std::array<int, 7> blockSizes { 1, 7, 64, 511, 1024, 13, 257 };

void process (ReverbEngine& engine, Audio& audio, bool mono = false, bool variable = true)
{
    std::size_t position = 0, block = 0;
    while (position < audio.left.size())
    {
        const int samples = std::min<int> (variable ? blockSizes[block++ % blockSizes.size()] : 256,
                                          static_cast<int> (audio.left.size() - position));
        float* channels[] { audio.left.data() + position, audio.right.data() + position };
        countAllocations = true;
        engine.process (channels, mono ? 1 : 2, samples);
        countAllocations = false;
        position += static_cast<std::size_t> (samples);
    }
}

// Subtract the retained dry path so tail tests continue to measure only reverb.
void subtractDry (Audio& audio, const Audio& dry, float gain = 1.0f, bool mono = false)
{
    for (std::size_t i = 0; i < audio.left.size(); ++i)
    {
        audio.left[i] -= dry.left[i] * gain;
        if (! mono) audio.right[i] -= dry.right[i] * gain;
    }
}

Audio wetImpulse (double sr, Settings settings, double seconds, bool variable = true)
{
    ReverbEngine engine;
    engine.setSettings (settings);
    engine.prepare (sr, 512);
    Audio audio (static_cast<std::size_t> (sr * seconds));
    audio.left[0] = audio.right[0] = 0.5f;
    process (engine, audio, false, variable);
    const float gain = settings.bypass ? 1.0f : std::pow (10.0f, settings.output / 20.0f);
    audio.left[0] -= 0.5f * gain;
    audio.right[0] -= 0.5f * gain;
    return audio;
}

Audio inputSignal (double sr, std::size_t length, std::size_t offset = 0)
{
    Audio audio (length);
    for (std::size_t i = 0; i < length; ++i)
    {
        const double time = static_cast<double> (i + offset) / sr;
        audio.left[i] = static_cast<float> (0.8 * std::sin (2 * pi * 637 * time)
                                          + 0.3 * std::cos (2 * pi * 1237 * time));
        audio.right[i] = static_cast<float> (0.65 * std::cos (2 * pi * 383 * time)
                                           - 0.2 * std::sin (2 * pi * 1901 * time));
    }
    return audio;
}

double energy (const Audio& audio, std::size_t begin, std::size_t end)
{
    double result = 0;
    for (std::size_t i = begin; i < std::min (end, audio.left.size()); ++i)
        result += static_cast<double> (audio.left[i]) * audio.left[i]
                + static_cast<double> (audio.right[i]) * audio.right[i];
    return result;
}

std::size_t onset (const Audio& audio)
{
    for (std::size_t i = 0; i < audio.left.size(); ++i)
        if (std::abs (audio.left[i]) + std::abs (audio.right[i]) > 1.0e-8f) return i;
    return audio.left.size();
}

void dryAndBypass (double sr)
{
    Audio original (7919);
    for (std::size_t i = 0; i < original.left.size(); ++i)
    {
        original.left[i] = static_cast<float> (std::sin (i * 0.19) * 0.79);
        original.right[i] = static_cast<float> (std::cos (i * 0.131) * 0.63);
    }
    original.left[11] = -0.0f;
    original.right[37] = -0.0f;
    for (int mode = 0; mode < 3; ++mode)
    {
        Settings settings;
        settings.mix = mode == 1 ? 100.0f : 0.0f;
        settings.bypass = mode == 1;
        settings.output = mode == 1 ? 6.0f : 0.0f;
        ReverbEngine engine;
        engine.setSettings (settings);
        engine.prepare (sr, 32);
        Audio rendered = original;
        process (engine, rendered, mode == 2);
        require (std::memcmp (rendered.left.data(), original.left.data(), original.left.size() * sizeof (float)) == 0,
                 "Dry/bypass path must be bit exact and zero latency");
        require (std::memcmp (rendered.right.data(), original.right.data(), original.right.size() * sizeof (float)) == 0,
                 "Dry/bypass must preserve right/unused channel");
    }
}

void timingAndBlocks (double sr)
{
    Settings settings;
    settings.mix = 100; settings.motion = 0; settings.grit = 0; settings.duck = 0;
    settings.predelay = 0;
    const auto immediate = wetImpulse (sr, settings, 0.8);
    require (onset (immediate) > 0 && immediate.left[0] == 0 && immediate.right[0] == 0,
             "The wet return must contain no second direct-dry signal, even with zero predelay");
    settings.predelay = 100;
    const auto delayed = wetImpulse (sr, settings, 0.8);
    const auto shift = static_cast<long> (onset (delayed)) - static_cast<long> (onset (immediate));
    require (std::abs (shift - static_cast<long> (sr * 0.1)) <= 2,
             "Predelay must shift the wet impulse by its advertised time");
    const auto constantBlocks = wetImpulse (sr, settings, 0.8, false);
    require (constantBlocks.left == delayed.left && constantBlocks.right == delayed.right,
             "Rendering must be independent of host block boundaries");
    require (energy (delayed, 0, delayed.left.size()) > 1.0e-5, "Reverb must produce a wet tail");

    ReverbEngine engine;
    settings.mix = 0;
    engine.setSettings (settings);
    engine.prepare (sr, 512);
    settings.mix = 100;
    engine.setSettings (settings);
    Audio ramp (static_cast<std::size_t> (sr * 0.006));
    std::fill (ramp.left.begin(), ramp.left.end(), 0.25f);
    std::fill (ramp.right.begin(), ramp.right.end(), 0.25f);
    process (engine, ramp);
    require (std::all_of (ramp.left.begin(), ramp.left.end(), [] (float sample) { return sample == 0.25f; })
             && ramp.left == ramp.right,
             "Increasing THROW must never attenuate the dry signal during predelay");
    settings.mix = 0;
    engine.setSettings (settings);
    std::fill (ramp.left.begin(), ramp.left.end(), 0.25f);
    std::fill (ramp.right.begin(), ramp.right.end(), 0.25f);
    process (engine, ramp);
    require (std::all_of (ramp.left.begin(), ramp.left.end(), [] (float sample) { return sample == 0.25f; })
             && ramp.left == ramp.right,
             "Decreasing THROW must never attenuate the dry signal during predelay");
}

void parallelReturn (double sr)
{
    const auto original = inputSignal (sr, static_cast<std::size_t> (sr * 0.40));
    for (const bool mono : { false, true })
    {
        Settings settings;
        settings.mix = 100; settings.predelay = 100; settings.grit = 40;
        settings.motion = 65; settings.duck = 50;
        ReverbEngine fullEngine;
        fullEngine.setSettings (settings);
        fullEngine.prepare (sr, 64);
        auto full = original;
        process (fullEngine, full, mono);
        auto wet = full;
        subtractDry (wet, original, 1.0f, mono);
        double wetEnergy = 0;
        for (std::size_t i = 0; i < wet.left.size(); ++i)
            wetEnergy += wet.left[i] * wet.left[i];
        require (wetEnergy > 0.01, "The parallel wet return must remain audible at 100% THROW");

        for (const float output : { -6.0f, 0.0f, 6.0f })
            for (const float amount : { 0.0f, 1.0f, 25.0f, 50.0f, 75.0f, 99.0f, 100.0f })
            {
                settings.mix = amount;
                settings.output = output;
                ReverbEngine engine;
                engine.setSettings (settings);
                engine.prepare (sr, 17);
                auto rendered = original;
                process (engine, rendered, mono);
                const float gain = output == 0.0f ? 1.0f : std::pow (10.0f, output / 20.0f);
                for (std::size_t i = 0; i < rendered.left.size(); ++i)
                {
                    const float expectedL = (original.left[i] + wet.left[i] * (amount * 0.01f)) * gain;
                    require (std::abs (rendered.left[i] - expectedL) < 1.0e-6f,
                             "THROW must add a scaled wet return over the entire dry signal");
                    if (mono)
                        require (rendered.right[i] == original.right[i],
                                 "Mono processing must not modify an unused right channel");
                    else
                    {
                        const float expectedR = (original.right[i] + wet.right[i] * (amount * 0.01f)) * gain;
                        require (std::abs (rendered.right[i] - expectedR) < 1.0e-6f,
                                 "Both stereo channels must retain dry while adding wet");
                    }
                    if (i < static_cast<std::size_t> (sr * 0.1))
                    {
                        require (rendered.left[i] == original.left[i] * gain,
                                 "Every THROW level must preserve dry exactly before reverb arrives");
                        if (! mono)
                            require (rendered.right[i] == original.right[i] * gain,
                                     "Right dry must also remain exact throughout predelay");
                    }
                }
            }
    }
}

void automatedReturn (double sr)
{
    for (const bool mono : { false, true })
    {
        Settings settings;
        settings.mix = 0; settings.predelay = 0; settings.motion = 30;
        settings.grit = 25; settings.duck = 20;
        ReverbEngine automated, fullEngine;
        automated.setSettings (settings);
        automated.prepare (sr, 64);
        settings.mix = 100;
        fullEngine.setSettings (settings);
        fullEngine.prepare (sr, 64);

        // Fill identical reverb networks while the automated return is silent.
        std::size_t offset = static_cast<std::size_t> (sr * 0.3);
        auto warm = inputSignal (sr, offset);
        auto fullWarm = warm;
        process (automated, warm, mono);
        process (fullEngine, fullWarm, mono);

        float smoothedAmount = 0;
        const float step = 100.0f / (static_cast<float> (sr) * 0.004f);
        double movingWetEnergy = 0;
        std::size_t exactDrySamples = 0;
        constexpr std::array<float, 9> amounts { 100, 0, 80, 15, 100, 0, 50, 100, 0 };
        for (std::size_t transition = 0; transition < amounts.size(); ++transition)
        {
            const auto length = transition == 2 || transition == 6
                ? 7u : static_cast<std::size_t> (sr * 0.006);
            const auto original = inputSignal (sr, length, offset);
            auto full = original;
            auto rendered = original;
            settings.mix = amounts[transition];
            automated.setSettings (settings);
            process (fullEngine, full, mono);
            process (automated, rendered, mono);
            // Model: within each processed block THROW moves linearly to the new
            // host value, never faster than the 4 ms full-range rate.
            std::size_t blockEnd = 0, blockIndex = 0;
            float rampFrom = 0, rampLength = 1, rampPosition = 0;
            for (std::size_t i = 0; i < length; ++i)
            {
                if (i == blockEnd)
                {
                    const auto count = std::min<std::size_t> (static_cast<std::size_t> (blockSizes[blockIndex++ % blockSizes.size()]),
                                                              length - i);
                    blockEnd = i + count;
                    rampFrom = smoothedAmount;
                    rampLength = std::max (static_cast<float> (count), std::ceil (std::abs (settings.mix - rampFrom) / step));
                    rampPosition = 0;
                }
                const float previousAmount = smoothedAmount;
                if (std::abs (settings.mix - smoothedAmount) > 0.0f)
                {
                    rampPosition += 1.0f;
                    smoothedAmount = rampPosition >= rampLength
                        ? settings.mix : rampFrom + (settings.mix - rampFrom) * (rampPosition / rampLength);
                }
                require (std::abs (smoothedAmount - previousAmount) <= step * 1.0001f,
                         "THROW must never move faster than its 4 ms full-range ramp");
                const float wetL = full.left[i] - original.left[i];
                const float expectedL = original.left[i] + wetL * (smoothedAmount * 0.01f);
                require (std::abs (rendered.left[i] - expectedL) < 1.0e-6f,
                         "Automated THROW must ramp only wet over 4 ms, retaining dry at unity");
                movingWetEnergy += wetL * wetL;
                if (! mono)
                {
                    const float wetR = full.right[i] - original.right[i];
                    const float expectedR = original.right[i] + wetR * (smoothedAmount * 0.01f);
                    require (std::abs (rendered.right[i] - expectedR) < 1.0e-6f,
                             "Right-channel THROW automation must ramp only the wet return");
                }
                else
                    require (rendered.right[i] == original.right[i],
                             "Mono automation must leave unused channels untouched");
                if (smoothedAmount == 0)
                {
                    ++exactDrySamples;
                    require (rendered.left[i] == original.left[i]
                             && rendered.right[i] == original.right[i],
                             "Returning THROW to zero must mute the tail and restore exact dry");
                }
                if (smoothedAmount == 100)
                    require (rendered.left[i] == full.left[i] && rendered.right[i] == full.right[i],
                             "THROW must reach full wet-return level after its short ramp");
            }
            if (length > static_cast<std::size_t> (sr * 0.005))
                require (smoothedAmount == settings.mix, "THROW ramp must settle by the end of its block");
            offset += length;
        }
        require (movingWetEnergy > 0.01 && exactDrySamples > 0,
                 "Automation assertions must exercise audible tails and settled zero return");
    }
}

void filteringDecayAndStereo (double sr)
{
    Settings settings;
    settings.mix = 100; settings.motion = 0; settings.grit = 0; settings.duck = 0;
    settings.predelay = 0; settings.lowCut = 300; settings.highCut = 12000;
    const auto sineEnergy = [&] (double frequency)
    {
        ReverbEngine engine;
        engine.setSettings (settings);
        engine.prepare (sr, 512);
        Audio audio (static_cast<std::size_t> (sr * 2));
        for (std::size_t i = 0; i < audio.left.size(); ++i)
            audio.left[i] = audio.right[i] = static_cast<float> (0.1 * std::sin (2 * pi * frequency * i / sr));
        const auto dry = audio;
        process (engine, audio);
        subtractDry (audio, dry);
        return energy (audio, static_cast<std::size_t> (sr), audio.left.size());
    };
    const double subEnergy = sineEnergy (55);
    const double midEnergy = sineEnergy (800);
    require (subEnergy < midEnergy * 0.005,
             "Wet low-cut must strongly reject sub bass relative to the midrange");

    settings.decay = 0.35f;
    const auto shortTail = wetImpulse (sr, settings, 2.4);
    settings.decay = 3.5f;
    const auto longTail = wetImpulse (sr, settings, 2.4);
    const auto begin = static_cast<std::size_t> (sr * 0.9);
    require (energy (longTail, begin, longTail.left.size())
                > energy (shortTail, begin, shortTail.left.size()) * 100,
             "Longer decay must produce substantially more late-tail energy");
    require (energy (longTail, static_cast<std::size_t> (sr * 1.6), longTail.left.size())
                < energy (longTail, static_cast<std::size_t> (sr * 0.1), static_cast<std::size_t> (sr * 0.9)),
             "Tail energy must decay over time");

    settings.width = 0;
    const auto monoTail = wetImpulse (sr, settings, 0.8);
    require (monoTail.left == monoTail.right, "Zero width must produce a mono wet return");
    settings.width = 150;
    const auto wideTail = wetImpulse (sr, settings, 0.8);
    double difference = 0;
    for (std::size_t i = 0; i < wideTail.left.size(); ++i)
        difference += std::abs (wideTail.left[i] - wideTail.right[i]);
    require (difference > 0.05, "Wide stereo tail must have audible decorrelation");
}

void longDecayHeadroom()
{
    Settings settings;
    settings.mix = 100; settings.size = 0; settings.decay = 18;
    settings.predelay = 0; settings.lowCut = 80; settings.highCut = 18000;
    settings.diffusion = 75; settings.motion = 0; settings.width = 100;
    settings.grit = 0; settings.duck = 0;
    ReverbEngine engine;
    engine.setSettings (settings);
    engine.prepare (48000, 512);
    Audio audio (48000 * 8);
    for (std::size_t i = 0; i < audio.left.size(); ++i)
        audio.left[i] = audio.right[i] = static_cast<float> (0.5 * std::sin (2 * pi * 1200 * i / 48000));
    const auto dry = audio;
    process (engine, audio);
    subtractDry (audio, dry);
    float peak = 0;
    for (std::size_t i = 0; i < audio.left.size(); ++i)
        peak = std::max (peak, std::max (std::abs (audio.left[i]), std::abs (audio.right[i])));
    // Full THROW is an emphatic, calibrated wet return. Floating-point audio
    // may exceed 0 dBFS; this extreme resonance must remain bounded and decay,
    // rather than being clipped or silently attenuated to satisfy a unity cap.
    require (peak < 4.0f, "Tiny rooms with 18-second decay must remain below 12 dB of wet headroom");
    Audio tail (48000 * 4);
    process (engine, tail);
    require (energy (tail, 48000 * 3, tail.left.size()) < energy (tail, 0, 48000) * 0.4,
             "An extreme, loud wet return must still decay after the input stops");

    settings.output = -18;
    ReverbEngine trimmedEngine;
    trimmedEngine.setSettings (settings);
    trimmedEngine.prepare (48000, 512);
    auto trimmed = dry;
    process (trimmedEngine, trimmed);
    const float trimGain = std::pow (10.0f, settings.output / 20.0f);
    float trimmedPeak = 0;
    for (std::size_t i = 0; i < trimmed.left.size(); ++i)
    {
        require (std::abs (trimmed.left[i] - (dry.left[i] + audio.left[i]) * trimGain) < 1.0e-6f
                 && std::abs (trimmed.right[i] - (dry.right[i] + audio.right[i]) * trimGain) < 1.0e-6f,
                 "Output trim must recover headroom without changing the wet/dry balance");
        trimmedPeak = std::max (trimmedPeak, std::max (std::abs (trimmed.left[i]), std::abs (trimmed.right[i])));
    }
    require (trimmedPeak < 1.0f, "Output trim must bring the extreme parallel return below 0 dBFS");
    std::cout << "Long-decay wet peak " << peak << "; output-trimmed parallel peak " << trimmedPeak << '\n';
}

Settings presetSettings (std::size_t index, float mix)
{
    auto settings = throwup::factoryPresets()[index].settings;
    settings.mix = mix;
    return settings;
}

std::vector<float> wetOnly (const Audio& rendered, const Audio& dry)
{
    std::vector<float> wet (rendered.left.size());
    for (std::size_t i = 0; i < wet.size(); ++i) wet[i] = rendered.left[i] - dry.left[i];
    return wet;
}

// Share of energy above roughly 6 kHz (fourth difference), in dB re the total.
double highFrequencyShare (const std::vector<float>& x, std::size_t begin, std::size_t end)
{
    double high = 0, total = 0;
    for (std::size_t i = std::max<std::size_t> (begin, 4); i < end; ++i)
    {
        const double d = x[i] - 4.0 * x[i - 1] + 6.0 * x[i - 2] - 4.0 * x[i - 3] + x[i - 4];
        high += d * d;
        total += static_cast<double> (x[i]) * x[i];
    }
    return 10.0 * std::log10 (high / total);
}

// Fraction of Hann-windowed energy within +/- band Hz of a frequency.
double toneShare (const std::vector<float>& x, std::size_t begin, std::size_t count,
                  double frequency, double sr, double band)
{
    std::vector<double> windowed (count);
    double total = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        windowed[i] = x[begin + i] * (0.5 - 0.5 * std::cos (2 * pi * static_cast<double> (i) / static_cast<double> (count - 1)));
        total += windowed[i] * windowed[i];
    }
    const double binHz = sr / static_cast<double> (count);
    double inBand = 0;
    for (auto bin = static_cast<long> (std::floor ((frequency - band) / binHz));
         bin <= static_cast<long> (std::ceil ((frequency + band) / binHz)); ++bin)
    {
        double re = 0, im = 0;
        const double omega = 2 * pi * static_cast<double> (bin) / static_cast<double> (count);
        for (std::size_t i = 0; i < count; ++i)
        {
            re += windowed[i] * std::cos (omega * static_cast<double> (i));
            im -= windowed[i] * std::sin (omega * static_cast<double> (i));
        }
        inBand += 2 * (re * re + im * im);
    }
    return inBand / (static_cast<double> (count) * total);
}

void processFixedBlocks (ReverbEngine& engine, Audio& audio, std::size_t begin, std::size_t end, int block)
{
    for (std::size_t position = begin; position < end; position += static_cast<std::size_t> (block))
    {
        const int samples = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), end - position));
        float* channels[] { audio.left.data() + position, audio.right.data() + position };
        countAllocations = true;
        engine.process (channels, 2, samples);
        countAllocations = false;
    }
}

float growl (std::size_t i, double sr)
{
    const double t = static_cast<double> (i) / sr;
    return static_cast<float> (0.5 * std::clamp (2.5 * std::sin (2 * pi * 110 * t + 4.0 * std::sin (2 * pi * 220 * t)), -1.0, 1.0));
}

void sizeChangeWithoutZipper()
{
    // Back Room -> Event Horizon mid-tail. When delay lengths moved in 16-sample
    // steps, the >6 kHz share rose from -58.8 dB to -5.8 dB (zipper).
    constexpr double sr = 48000;
    ReverbEngine engine;
    engine.setSettings (presetSettings (10, 100));
    engine.prepare (sr, 64);
    const auto length = static_cast<std::size_t> (sr * 2), change = static_cast<std::size_t> (sr);
    const auto window = static_cast<std::size_t> (sr * 0.25);
    Audio audio (length);
    for (std::size_t i = 0; i < static_cast<std::size_t> (sr * 0.5); ++i)
        audio.left[i] = audio.right[i] = static_cast<float> (0.5 * std::sin (2 * pi * 98 * static_cast<double> (i) / sr));
    const auto dry = audio;
    processFixedBlocks (engine, audio, 0, change, 64);
    engine.setSettings (presetSettings (30, 100));
    processFixedBlocks (engine, audio, change, length, 64);
    const auto wet = wetOnly (audio, dry);
    const double before = highFrequencyShare (wet, change - window, change);
    const double after = highFrequencyShare (wet, change, change + window);
    require (after < before + 6.0 && after < -45.0, "A Size/preset change mid-tail must not zipper");
    std::cout << "PASS Size change mid-tail (Back Room -> Event Horizon): >6 kHz share " << before
              << " dB before, " << after << " dB after\n";
}

void predelayChangeWithoutPitchGlide()
{
    // A slewed pre-delay read position bent the wet input's pitch (24->105 ms
    // stalled it; 0->250 ms played it backwards). A crossfade keeps a 1 kHz input at 1 kHz.
    constexpr double sr = 48000;
    for (const auto& change : std::array<std::array<float, 2>, 3> {{ { 24, 105 }, { 0, 250 }, { 7, 34 } }})
    {
        Settings settings;
        settings.mix = 100; settings.size = 36; settings.decay = 1.15f; settings.predelay = change[0];
        settings.lowCut = 180; settings.highCut = 18000; settings.diffusion = 76; settings.motion = 0;
        settings.width = 100; settings.grit = 0; settings.duck = 0;
        ReverbEngine engine;
        engine.setSettings (settings);
        engine.prepare (sr, 64);
        const auto length = static_cast<std::size_t> (sr * 1.6), at = static_cast<std::size_t> (sr);
        const auto window = static_cast<std::size_t> (sr * 0.2);
        Audio audio (length);
        for (std::size_t i = 0; i < length; ++i)
            audio.left[i] = audio.right[i] = static_cast<float> (0.5 * std::sin (2 * pi * 1000 * static_cast<double> (i) / sr));
        const auto dry = audio;
        processFixedBlocks (engine, audio, 0, at, 64);
        settings.predelay = change[1];
        engine.setSettings (settings);
        processFixedBlocks (engine, audio, at, length, 64);
        const auto wet = wetOnly (audio, dry);
        const double first = toneShare (wet, at, window, 1000, sr, 60);
        const double second = toneShare (wet, at + window, window, 1000, sr, 60);
        require (first > 0.99 && second > 0.99, "A pre-delay change must not glide, stall or reverse the wet pitch");
        std::cout << "PASS pre-delay " << change[0] << " -> " << change[1] << " ms: 1 kHz energy share "
                  << first << " / " << second << '\n';
    }
}

void bypassFadesWithoutClicks()
{
    constexpr double sr = 48000;
    {
        // Event Horizon tail at about +7 dBFS: bypass used to cut it to zero in one sample.
        ReverbEngine engine;
        auto settings = presetSettings (30, 100);
        engine.setSettings (settings);
        engine.prepare (sr, 64);
        const auto length = static_cast<std::size_t> (sr * 2), toggle = static_cast<std::size_t> (64 * 1100);
        Audio audio (length);
        for (std::size_t i = 0; i < static_cast<std::size_t> (sr); ++i) audio.left[i] = audio.right[i] = growl (i, sr);
        processFixedBlocks (engine, audio, 0, toggle, 64);
        settings.bypass = true;
        engine.setSettings (settings);
        processFixedBlocks (engine, audio, toggle, length, 64);
        float natural = 0, worst = 0;
        for (std::size_t i = toggle - 4800; i < toggle; ++i) natural = std::max (natural, std::abs (audio.left[i] - audio.left[i - 1]));
        for (std::size_t i = toggle; i < toggle + 2400; ++i) worst = std::max (worst, std::abs (audio.left[i] - audio.left[i - 1]));
        require (worst <= natural * 1.25f, "Bypass must fade the reverb out instead of cutting it");
        const auto settled = toggle + static_cast<std::size_t> (sr * 0.016);
        require (std::all_of (audio.left.begin() + static_cast<long> (settled), audio.left.end(), [] (float s) { return s == 0.0f; }),
                 "A completed bypass fade must be exact dry");
        std::cout << "PASS bypass during tail: largest step " << worst << " vs natural " << natural << '\n';
    }
    {
        // THROW 0 with Output -12 dB: bypass used to jump +12 dB in one sample.
        ReverbEngine engine;
        Settings settings;
        settings.mix = 0; settings.output = -12;
        engine.setSettings (settings);
        engine.prepare (sr, 64);
        Audio audio (19200);
        for (std::size_t i = 0; i < audio.left.size(); ++i)
            audio.left[i] = audio.right[i] = static_cast<float> (0.8 * std::sin (2 * pi * 60 * static_cast<double> (i) / sr + 1.0));
        const auto dry = audio;
        processFixedBlocks (engine, audio, 0, 4992, 64);
        settings.bypass = true;
        engine.setSettings (settings);
        processFixedBlocks (engine, audio, 4992, 9984, 64);
        settings.bypass = false;
        engine.setSettings (settings);
        processFixedBlocks (engine, audio, 9984, audio.left.size(), 64);
        const float natural = static_cast<float> (0.8 * 2 * pi * 60 / sr);
        float worst = 0;
        for (std::size_t i = 1; i < audio.left.size(); ++i) worst = std::max (worst, std::abs (audio.left[i] - audio.left[i - 1]));
        require (worst <= natural * 1.25f, "Bypass and un-bypass must ramp the output gain change");
        require (std::equal (audio.left.begin() + 4992 + 800, audio.left.begin() + 9984, dry.left.begin() + 4992 + 800),
                 "Bypassed output must be exact dry after the fade");
        std::cout << "PASS bypass/un-bypass with -12 dB output: largest step " << worst << " vs natural " << natural << '\n';
    }
}

void blockRampedThrow()
{
    // A 1/16-note throw at 140 BPM written as linear automation, delivered once
    // per 1024-sample block. Stepping to each block value used to leave a 22%
    // staircase; ramping across the block follows the host curve.
    constexpr double sr = 44100;
    constexpr int block = 1024;
    constexpr double rampSamples = 4725;
    auto settings = presetSettings (10, 100);
    settings.duck = 0;
    ReverbEngine full, automated;
    full.setSettings (settings);
    full.prepare (sr, block);
    settings.mix = 0;
    automated.setSettings (settings);
    automated.prepare (sr, block);
    const std::size_t warm = static_cast<std::size_t> (block) * 40;
    const std::size_t length = warm + static_cast<std::size_t> (block) * 6;
    Audio input (length);
    for (std::size_t i = 0; i < length; ++i) input.left[i] = input.right[i] = growl (i, sr);
    auto fullOut = input, automatedOut = input;
    processFixedBlocks (full, fullOut, 0, warm, block);
    processFixedBlocks (automated, automatedOut, 0, warm, block);
    double worst = 0;
    for (std::size_t b = 0; b < 6; ++b)
    {
        const auto begin = warm + b * static_cast<std::size_t> (block);
        settings.mix = static_cast<float> (100.0 * std::min (1.0, static_cast<double> ((b + 1) * block) / rampSamples));
        automated.setSettings (settings);
        processFixedBlocks (full, fullOut, begin, begin + block, block);
        processFixedBlocks (automated, automatedOut, begin, begin + block, block);
        for (std::size_t i = begin; i < begin + block && i - warm < 4096; ++i)
        {
            const float wet = fullOut.left[i] - input.left[i];
            if (std::abs (wet) < 0.01f) continue;
            const double implied = 100.0 * (automatedOut.left[i] - input.left[i]) / wet;
            const double ideal = 100.0 * std::min (1.0, static_cast<double> (i - warm + 1) / rampSamples);
            worst = std::max (worst, std::abs (implied - ideal));
        }
    }
    require (worst < 0.5, "Block-rate THROW automation must follow the host curve, not a staircase");
    std::cout << "PASS block-rate THROW automation: largest deviation from host curve " << worst << "% THROW\n";
}

void presetsAndStress()
{
    const auto& presets = throwup::factoryPresets();
    constexpr std::array<const char*, 5> groups { "Small", "Medium", "Large", "Huge", "Creative" };
    std::set<std::string> names;
    for (std::size_t p = 0; p < presets.size(); ++p)
    {
        const auto& preset = presets[p];
        require (preset.group == std::string (groups[p / 10]), "Presets must be grouped in banks of ten");
        require (names.insert (preset.name).second, "Every factory preset needs a unique name");
        require (preset.settings.mix == 0 && preset.settings.output == 0 && ! preset.settings.bypass,
                 "Factory values must leave mix/output/bypass neutral");
        auto settings = preset.settings;
        settings.mix = 100;
        const auto audio = wetImpulse (48000, settings, 0.75);
        require (energy (audio, 0, audio.left.size()) > 1.0e-7, "Every preset must produce a tail");
        for (std::size_t i = 0; i < audio.left.size(); ++i)
            require (std::isfinite (audio.left[i]) && std::isfinite (audio.right[i])
                     && std::abs (audio.left[i]) < 4 && std::abs (audio.right[i]) < 4,
                     "Factory impulse renders must remain finite and bounded");
    }

    ReverbEngine engine;
    engine.prepare (96000, 64);
    Settings settings;
    settings.mix = 100; settings.decay = 18; settings.size = 100;
    settings.diffusion = 100; settings.motion = 100; settings.grit = 100;
    settings.width = 150; settings.duck = 0; settings.output = 6;
    engine.setSettings (settings);
    Audio block (1024);
    std::uint32_t random = 0x43218765u;
    float peak = 0;
    for (int iteration = 0; iteration < 1400; ++iteration)
    {
        if (iteration % 31 == 0)
        {
            settings.size = iteration % 62 == 0 ? 0.0f : 100.0f;
            settings.predelay = iteration % 62 == 0 ? 250.0f : 0.0f;
            settings.lowCut = iteration % 62 == 0 ? 80.0f : 1000.0f;
            settings.highCut = iteration % 62 == 0 ? 1000.0f : 18000.0f;
            settings.mix = iteration % 62 == 0 ? 0.0f : 100.0f;
            engine.setSettings (settings);
        }
        for (std::size_t i = 0; i < block.left.size(); ++i)
        {
            random = random * 1664525u + 1013904223u;
            const float noise = (static_cast<float> (random >> 8) / 16777216.0f - 0.5f) * 0.9f;
            block.left[i] = iteration < 600 ? noise : 0.0f;
            block.right[i] = iteration < 600 ? -noise : 0.0f;
        }
        if (iteration == 101)
        {
            block.left[42] = std::numeric_limits<float>::quiet_NaN();
            block.right[78] = std::numeric_limits<float>::infinity();
            settings.decay = std::numeric_limits<float>::quiet_NaN();
            engine.setSettings (settings);
            settings.decay = 18;
        }
        process (engine, block, iteration % 2 == 0);
        for (std::size_t i = 0; i < block.left.size(); ++i)
        {
            require (std::isfinite (block.left[i]) && std::isfinite (block.right[i]),
                     "Extreme automation and invalid input must never produce nonfinite audio");
            peak = std::max (peak, std::max (std::abs (block.left[i]), std::abs (block.right[i])));
        }
    }
    require (peak < 12, "Extreme parameter modulation must stay bounded");
    require (realtimeAllocations == 0, "Audio processing must never allocate memory");
    std::cout << "50 presets verified; extreme-automation peak " << peak
              << "; realtime allocations " << realtimeAllocations << '\n';
}
} // namespace

int main()
{
    try
    {
        require (Settings {}.mix == 0.0f, "Default THROW must be a silent return over unchanged dry");
        for (const double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            dryAndBypass (sampleRate);
            timingAndBlocks (sampleRate);
            parallelReturn (sampleRate);
            automatedReturn (sampleRate);
            filteringDecayAndStereo (sampleRate);
            std::cout << "PASS " << sampleRate << " Hz: exact dry/bypass, parallel return, wet-only path, "
                         "4 ms automation, mono/stereo, output trim, timing, blocks, filtering, decay\n";
        }
        longDecayHeadroom();
        sizeChangeWithoutZipper();
        predelayChangeWithoutPitchGlide();
        bypassFadesWithoutClicks();
        blockRampedThrow();
        presetsAndStress();
        std::cout << "All THROW-UP DSP tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        countAllocations = false;
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
