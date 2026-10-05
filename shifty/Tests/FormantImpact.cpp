#include "FormantEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

// The required behaviour is relocation of the INPUT's envelope, not imposing
// a particular vowel or requiring a large change from a featureless spectrum.
// Fixtures and analysis are independent of the engine's cepstral estimator.
namespace
{
using Audio = std::vector<std::vector<float>>;
constexpr double pi = juce::MathConstants<double>::pi;
constexpr double activeSeconds = 1.8;
constexpr double measurementStartSeconds = 0.45;
constexpr double spectralCeiling = 12000.0;

enum class Kind { resonant, flat, saw, square, reese, fm };
struct Fixture
{
    Kind kind;
    const char* name;
    double fundamental;
    std::array<double, 2> centres;
};
constexpr Fixture trackedFixtures[] {
    { Kind::resonant, "source-730-2070", 80.0, { 730.0, 2070.0 } },
    { Kind::resonant, "source-970-2890", 110.0, { 970.0, 2890.0 } },
    { Kind::resonant, "source-1270-3670", 165.0, { 1270.0, 3670.0 } }
};
constexpr Fixture flatFixture { Kind::flat, "flat-harmonic", 110.0, {} };
constexpr Fixture demoFixtures[] {
    { Kind::saw, "harmonic-saw", 110.0, {} },
    { Kind::square, "odd-square", 110.0, {} },
    { Kind::reese, "detuned-reese", 110.0, {} },
    { Kind::fm, "fm-bass", 110.0, {} }
};

Audio emptyAudio (int samples)
{
    return Audio (2, std::vector<float> (static_cast<size_t> (samples), 0.0f));
}

double rms (const Audio& audio, int start, int count)
{
    double sum = 0.0;
    for (const auto& channel : audio)
        for (int i = start; i < start + count; ++i)
            sum += static_cast<double> (channel[static_cast<size_t> (i)])
                 * channel[static_cast<size_t> (i)];
    return std::sqrt (sum / (count * static_cast<double> (audio.size())));
}

// Known log-amplitude Gaussian resonances on a flat harmonic carrier. Their
// locations do not come from the optional vowel guide. The two different
// widths also test envelope shape, not just movement of one loud partial.
double sourceEnvelopeDb (const Fixture& fixture, double frequency)
{
    double result = 0.0;
    for (size_t i = 0; i < fixture.centres.size(); ++i)
    {
        const double centre = fixture.centres[i];
        const double width = centre * (i == 0 ? 0.23 : 0.19);
        const double distance = (frequency - centre) / width;
        result += (i == 0 ? 16.0 : 14.0) * std::exp (-0.5 * distance * distance);
    }
    return result;
}

Audio makeFixture (const Fixture& fixture, double sampleRate, int tail)
{
    const int active = static_cast<int> (sampleRate * activeSeconds);
    auto audio = emptyAudio (active + tail);
    const int harmonics = static_cast<int> (spectralCeiling / fixture.fundamental);
    for (int channel = 0; channel < 2; ++channel)
    {
        for (int i = 0; i < active; ++i)
        {
            const double t = i / sampleRate;
            const double phase = 2.0 * pi * fixture.fundamental * t;
            double value = 0.0;
            if (fixture.kind == Kind::fm)
                value = std::tanh (1.3 * std::sin (phase + 2.4 * std::sin (phase))
                                + 0.35 * std::sin (3.0 * phase + 1.6 * std::sin (2.0 * phase)));
            else
                for (int h = 1; h <= harmonics; ++h)
                {
                    if (fixture.kind == Kind::square && h % 2 == 0)
                        continue;
                    double amplitude = 1.0 / h;
                    if (fixture.kind == Kind::flat)
                        amplitude = 1.0;
                    if (fixture.kind == Kind::resonant)
                        amplitude = std::pow (10.0, sourceEnvelopeDb (fixture, h * fixture.fundamental) / 20.0);
                    const double initialPhase = 0.31 * h * h;
                    if (fixture.kind == Kind::reese)
                    {
                        const double a = 2.0 * pi * (fixture.fundamental + (channel == 0 ? -0.45 : -0.39)) * t;
                        const double b = 2.0 * pi * (fixture.fundamental + (channel == 0 ? 0.37 : 0.44)) * t;
                        value += 0.5 * amplitude * (std::sin (h * a + initialPhase)
                                                  + std::sin (h * b + initialPhase + 0.61));
                    }
                    else
                        value += amplitude * std::sin (h * phase + initialPhase);
                }
            const double fade = std::max (0.0, std::min ({ 1.0, t / 0.015, (activeSeconds - t) / 0.08 }));
            audio[static_cast<size_t> (channel)][static_cast<size_t> (i)] = static_cast<float> (value * fade);
        }
    }
    const double scale = 0.12 / std::max (rms (audio, static_cast<int> (sampleRate * 0.25),
                                              static_cast<int> (sampleRate)), 1.0e-12);
    for (auto& channel : audio)
        for (auto& sample : channel)
            sample *= static_cast<float> (scale);
    return audio;
}

struct Render { Audio aligned; int latency = 0; bool finite = true; double peak = 0.0; };

Render process (const Audio& input, double sampleRate, float semitones, bool vowel = false)
{
    shifty::EngineParams params;
    params.shiftSemitones = semitones;
    params.focus = 0.70f;
    params.bite = 0.0f; // Unit envelope displacement; Bite's emphasis is a separate creative control.
    params.drive = 0.0f;
    params.mix = 1.0f;
    params.subSafe = false; // Isolate formant accuracy; sub protection has separate DSP checks.
    params.vowel = vowel;
    shifty::FormantEngine engine;
    engine.setParameters (params);
    engine.prepare (sampleRate, 1024, 2);
    const int samples = static_cast<int> (input.front().size());
    auto output = emptyAudio (samples);
    constexpr int blocks[] { 257, 1024, 37, 511, 1, 191 };
    int position = 0;
    size_t blockIndex = 0;
    double processingSeconds = 0.0;
    while (position < samples)
    {
        const int count = std::min (samples - position, blocks[blockIndex++ % std::size (blocks)]);
        juce::AudioBuffer<float> buffer (2, count);
        for (int channel = 0; channel < 2; ++channel)
            buffer.copyFrom (channel, 0, input[static_cast<size_t> (channel)].data() + position, count);
        const auto began = std::chrono::steady_clock::now();
        engine.processBlock (buffer);
        processingSeconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - began).count();
        for (int channel = 0; channel < 2; ++channel)
            std::copy_n (buffer.getReadPointer (channel), count,
                         output[static_cast<size_t> (channel)].begin() + position);
        position += count;
    }
    // Callback-only wall-clock timing: fixture generation, allocations, copies,
    // and analysis are excluded. Report, never gate, on this noisy workstation.
    std::cout << "TIMING stereo " << sampleRate << " Hz, " << semitones << " st, Vowel "
              << (vowel ? "ON" : "OFF") << ": " << processingSeconds << " s callbacks / "
              << samples / sampleRate << " s audio = " << 100.0 * processingSeconds * sampleRate / samples
              << "% of one realtime thread (informational)\n";
    Render result;
    result.latency = engine.getLatencySamples();
    const int active = static_cast<int> (sampleRate * activeSeconds);
    result.aligned = emptyAudio (active);
    for (size_t channel = 0; channel < output.size(); ++channel)
    {
        std::copy_n (output[channel].begin() + result.latency, active, result.aligned[channel].begin());
        for (float sample : output[channel])
        {
            result.finite = result.finite && std::isfinite (sample);
            result.peak = std::max (result.peak, std::abs (static_cast<double> (sample)));
        }
    }
    return result;
}

std::vector<double> spectrum (const Audio& audio, int start, int fftOrder)
{
    const int size = 1 << fftOrder;
    std::vector<double> power (static_cast<size_t> (size / 2 + 1), 0.0);
    juce::dsp::FFT fft (fftOrder);
    std::vector<juce::dsp::Complex<float>> input (static_cast<size_t> (size));
    std::vector<juce::dsp::Complex<float>> output (static_cast<size_t> (size));
    for (const auto& channel : audio)
    {
        for (int i = 0; i < size; ++i)
        {
            const double window = 0.5 - 0.5 * std::cos (2.0 * pi * i / (size - 1));
            input[static_cast<size_t> (i)] = { static_cast<float> (window * channel[static_cast<size_t> (start + i)]), 0.0f };
        }
        fft.perform (input.data(), output.data(), false);
        for (int i = 0; i <= size / 2; ++i)
            power[static_cast<size_t> (i)] += static_cast<double> (std::norm (output[static_cast<size_t> (i)]));
    }
    return power;
}

double bandPower (const std::vector<double>& power, double binHz, double low, double high)
{
    const int first = std::max (1, static_cast<int> (std::ceil (low / binHz)));
    const int last = std::min (static_cast<int> (power.size()) - 1, static_cast<int> (std::floor (high / binHz)));
    double sum = 0.0;
    for (int i = first; i <= last; ++i)
        sum += power[static_cast<size_t> (i)];
    return sum;
}

std::vector<double> harmonicDb (const std::vector<double>& power, double sampleRate, int order, double f0)
{
    std::vector<double> result (static_cast<size_t> (std::floor (7000.0 / f0)) + 1, -300.0);
    for (size_t h = 1; h < result.size(); ++h)
        result[h] = 10.0 * std::log10 (std::max (bandPower (power, sampleRate / (1 << order),
                                                           h * f0 - 5.0, h * f0 + 5.0), 1.0e-30));
    return result;
}

double centredRms (const std::vector<double>& values)
{
    const double mean = std::accumulate (values.begin(), values.end(), 0.0) / values.size();
    double sum = 0.0;
    for (double value : values)
        sum += (value - mean) * (value - mean);
    return std::sqrt (sum / values.size());
}

double correlation (const std::vector<double>& a, const std::vector<double>& b)
{
    const double ma = std::accumulate (a.begin(), a.end(), 0.0) / a.size();
    const double mb = std::accumulate (b.begin(), b.end(), 0.0) / b.size();
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const double x = a[i] - ma, y = b[i] - mb;
        ab += x * y; aa += x * x; bb += y * y;
    }
    return ab / std::max (std::sqrt (aa * bb), 1.0e-30);
}

double templateCorrelation (const std::vector<double>& db, const Fixture& fixture, double semitones)
{
    const double ratio = std::pow (2.0, semitones / 12.0);
    std::vector<double> measured, expected;
    for (size_t h = 1; h < db.size(); ++h)
        if (h * fixture.fundamental >= 300.0 && h * fixture.fundamental <= 6500.0)
        {
            measured.push_back (db[h]);
            expected.push_back (sourceEnvelopeDb (fixture, h * fixture.fundamental / ratio));
        }
    return correlation (measured, expected);
}

// Quadratic interpolation in dB over neighboring harmonics. The search covers
// BOTH original and requested locations, so an unmoved resonance cannot pass
// merely because the analyzer only looked where it wanted the peak to be.
double resonancePeak (const std::vector<double>& db, double f0, double sourceCentre, double ratio)
{
    const double low = sourceCentre * std::min (1.0, ratio) * 0.75;
    const double high = sourceCentre * std::max (1.0, ratio) * 1.25;
    const int first = std::max (2, static_cast<int> (std::ceil (low / f0)));
    const int last = std::min (static_cast<int> (db.size()) - 2, static_cast<int> (std::floor (high / f0)));
    int peak = first;
    for (int h = first + 1; h <= last; ++h)
        if (db[static_cast<size_t> (h)] > db[static_cast<size_t> (peak)])
            peak = h;
    const double left = db[static_cast<size_t> (peak - 1)];
    const double middle = db[static_cast<size_t> (peak)];
    const double right = db[static_cast<size_t> (peak + 1)];
    const double denominator = left - 2.0 * middle + right;
    const double fraction = std::abs (denominator) > 1.0e-12
        ? std::clamp (0.5 * (left - right) / denominator, -0.5, 0.5) : 0.0;
    return (peak + fraction) * f0;
}

double outsideHarmonics (const std::vector<double>& power, double binHz, double f0)
{
    double total = 0.0, outside = 0.0;
    for (size_t i = 1; i < power.size(); ++i)
    {
        const double hz = i * binHz;
        if (hz < 200.0 || hz > 7000.0)
            continue;
        const double h = std::round (hz / f0);
        total += power[i];
        if (h < 1.0 || std::abs (hz - h * f0) > 5.0)
            outside += power[i];
    }
    return outside / std::max (total, 1.0e-30);
}

double matchedResidual (const Audio& dry, const Audio& wet, int start, int count)
{
    const double dryRms = rms (dry, start, count);
    const double scale = dryRms / std::max (rms (wet, start, count), 1.0e-12);
    double energy = 0.0;
    for (size_t ch = 0; ch < dry.size(); ++ch)
        for (int i = start; i < start + count; ++i)
        {
            const double delta = dry[ch][static_cast<size_t> (i)] - scale * wet[ch][static_cast<size_t> (i)];
            energy += delta * delta;
        }
    return std::sqrt (energy / (count * static_cast<double> (dry.size()))) / std::max (dryRms, 1.0e-12);
}

bool writeWav (const juce::File& directory, const juce::String& name,
               const Audio& audio, double sampleRate, double gain)
{
    const auto file = directory.getChildFile (name);
    if (file.existsAsFile() && ! file.deleteFile())
        return false;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (! stream)
        return false;
    juce::WavAudioFormat format;
    auto options = juce::AudioFormatWriterOptions {}
        .withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (32)
        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor (stream, options);
    if (! writer)
        return false;
    const int active = static_cast<int> (sampleRate * activeSeconds);
    juce::AudioBuffer<float> buffer (2, active);
    for (int channel = 0; channel < 2; ++channel)
    {
        buffer.copyFrom (channel, 0, audio[static_cast<size_t> (channel)].data(), active);
        buffer.applyGain (channel, 0, active, static_cast<float> (gain));
    }
    return writer->writeFromAudioSampleBuffer (buffer, 0, active);
}
} // namespace

int main (int argc, char* argv[])
{
    juce::String renderPath;
    if (argc != 1)
    {
        if (argc != 3 || std::string (argv[1]) != "--render" || ! juce::File::isAbsolutePath (argv[2]))
        {
            std::cerr << "Usage: ShiftyFormantImpact [--render <absolute-directory>]\n";
            return 2;
        }
        renderPath = argv[2];
    }
    const auto directory = renderPath.isNotEmpty() ? juce::File (renderPath) : juce::File {};
    if (renderPath.isNotEmpty() && ! directory.createDirectory())
        return 2;

    std::cout << "SHIFTY SOURCE formant tracking: ±6 st; Focus .70; Bite/Drive 0; Mix 1; Vowel OFF.\n"
              << "Known input resonances must move by 2^(shift/12) while harmonic pitch stays fixed.\n";
    std::ostringstream csv;
    csv << "fixture,sample_rate,shift_semitones,target_correlation,unshifted_correlation,"
           "estimated_shift_semitones,peak1_hz,expected_peak1_hz,peak2_hz,expected_peak2_hz,"
           "off_harmonic_fraction,reported_latency,passed\n";
    csv << std::setprecision (8);
    int passed = 0, checks = 0;
    bool filesOkay = true;
    const auto check = [&] (bool okay, const std::string& description)
    {
        ++checks;
        passed += okay ? 1 : 0;
        std::cout << (okay ? "PASS " : "FAIL ") << description << '\n';
    };
    for (double sampleRate : { 48000.0, 96000.0 })
    {
        const int order = sampleRate == 48000.0 ? 15 : 16;
        const int start = static_cast<int> (sampleRate * measurementStartSeconds);
        const int count = 1 << order;
        shifty::FormantEngine probe;
        probe.prepare (sampleRate, 1024, 2);
        const auto save = [&] (const Fixture& fixture, const Audio& dry, const Audio& wet, const char* suffix)
        {
            if (renderPath.isEmpty())
                return;
            const auto base = juce::String (fixture.name) + "-" + juce::String (static_cast<int> (sampleRate));
            const double gain = rms (dry, start, count) / std::max (rms (wet, start, count), 1.0e-12);
            filesOkay = writeWav (directory, base + suffix + ".wav", wet, sampleRate, gain) && filesOkay;
        };
        for (const auto& fixture : trackedFixtures)
        {
            const auto dry = makeFixture (fixture, sampleRate, 2 * probe.getLatencySamples());
            const auto dryPower = spectrum (dry, start, order);
            const auto dryDb = harmonicDb (dryPower, sampleRate, order, fixture.fundamental);
            save (fixture, dry, dry, "-dry");
            for (float shift : { -6.0f, 6.0f })
            {
                const auto wet = process (dry, sampleRate, shift);
                const auto power = spectrum (wet.aligned, start, order);
                const auto db = harmonicDb (power, sampleRate, order, fixture.fundamental);
                const double ratio = std::pow (2.0, shift / 12.0);
                const double targetCorrelation = templateCorrelation (db, fixture, shift);
                const double originalCorrelation = templateCorrelation (db, fixture, 0.0);
                double bestCorrelation = -2.0, estimatedShift = 0.0;
                for (int step = -40; step <= 40; ++step)
                {
                    const double candidate = step * 0.25;
                    const double value = templateCorrelation (db, fixture, candidate);
                    if (value > bestCorrelation)
                    {
                        bestCorrelation = value;
                        estimatedShift = candidate;
                    }
                }
                std::array<double, 2> peaks {};
                bool peaksOkay = true;
                for (size_t i = 0; i < peaks.size(); ++i)
                {
                    const double centre = fixture.centres[i];
                    peaks[i] = resonancePeak (db, fixture.fundamental, centre, ratio);
                    const double dryPeak = resonancePeak (dryDb, fixture.fundamental, centre, 1.0);
                    const double expected = centre * ratio;
                    // A 40 ms analysis cannot resolve every bass partial. This
                    // allows one input harmonic or 15%, whichever is larger.
                    // Both direction and proximity to the ratio are required.
                    const double tolerance = std::max (fixture.fundamental, 0.15 * expected);
                    peaksOkay = peaksOkay && std::abs (peaks[i] - expected) <= tolerance
                        && (peaks[i] - dryPeak) * shift > 0.0;
                }
                const double offGrid = outsideHarmonics (power, sampleRate / count, fixture.fundamental);
                const double dryOffGrid = outsideHarmonics (dryPower, sampleRate / count, fixture.fundamental);
                // A high shape correlation, a clearly better shifted template,
                // and <=1.5st fitted error rule out gain-only/unchanged output.
                const bool okay = wet.finite && wet.peak < 8.0 && peaksOkay
                    && targetCorrelation >= 0.85 && targetCorrelation - originalCorrelation >= 0.15
                    && std::abs (estimatedShift - shift) <= 1.5
                    && offGrid <= std::max (0.03, dryOffGrid + 0.02);
                std::ostringstream description;
                description << std::fixed << std::setprecision (3) << fixture.name << ' ' << sampleRate
                    << " Hz " << shift << " st: target r=" << targetCorrelation << ", original r="
                    << originalCorrelation << ", fitted shift=" << estimatedShift << ", peaks="
                    << peaks[0] << '/' << peaks[1] << " Hz, off-grid=" << offGrid;
                check (okay, description.str());
                csv << fixture.name << ',' << sampleRate << ',' << shift << ',' << targetCorrelation << ','
                    << originalCorrelation << ',' << estimatedShift << ',' << peaks[0] << ','
                    << fixture.centres[0] * ratio << ',' << peaks[1] << ',' << fixture.centres[1] * ratio
                    << ',' << offGrid << ',' << wet.latency << ',' << (okay ? "true" : "false") << '\n';
                save (fixture, dry, wet.aligned, shift < 0.0f ? "-shift-down" : "-shift-up");
            }
        }

        const auto flat = makeFixture (flatFixture, sampleRate, 2 * probe.getLatencySamples());
        const auto dryDb = harmonicDb (spectrum (flat, start, order), sampleRate, order, flatFixture.fundamental);
        save (flatFixture, flat, flat, "-dry");
        for (float shift : { -6.0f, 6.0f })
        {
            const auto wet = process (flat, sampleRate, shift);
            const auto db = harmonicDb (spectrum (wet.aligned, start, order), sampleRate, order, flatFixture.fundamental);
            std::vector<double> gains;
            for (size_t h = 1; h < db.size(); ++h)
                if (h * flatFixture.fundamental >= 350.0 && h * flatFixture.fundamental <= 4500.0)
                    gains.push_back (db[h] - dryDb[h]);
            const double spread = centredRms (gains);
            check (wet.finite && spread < 1.5,
                   "flat carrier " + std::to_string (static_cast<int> (sampleRate)) + " Hz "
                   + std::to_string (shift) + " st: centred gain spread=" + std::to_string (spread)
                   + " dB (<1.5 dB; no mandatory vowel imprint)");
            save (flatFixture, flat, wet.aligned, shift < 0.0f ? "-shift-down" : "-shift-up");
            if (shift > 0.0f)
            {
                const auto vowel = process (flat, sampleRate, shift, true);
                const double difference = matchedResidual (wet.aligned, vowel.aligned, start, count);
                check (vowel.finite && difference > 0.10,
                       "optional Vowel " + std::to_string (static_cast<int> (sampleRate))
                       + " Hz: gain-matched difference=" + std::to_string (difference));
                save (flatFixture, flat, vowel.aligned, "-vowel-up");
            }
        }
        const auto neutralInput = makeFixture (trackedFixtures[1], sampleRate, 2 * probe.getLatencySamples());
        for (bool vowel : { false, true })
        {
            const auto neutral = process (neutralInput, sampleRate, 0.0f, vowel);
            const double error = matchedResidual (neutralInput, neutral.aligned, start, count);
            const double gainError = std::abs (20.0 * std::log10 (rms (neutral.aligned, start, count)
                                                               / rms (neutralInput, start, count)));
            check (neutral.finite && error < 1.0e-5 && gainError < 0.001,
                   "zero shift " + std::to_string (static_cast<int> (sampleRate)) + " Hz, Vowel "
                   + (vowel ? "ON" : "OFF") + ": residual=" + std::to_string (error)
                   + ", level error=" + std::to_string (gainError) + " dB");
        }

        // These are listening examples only: a smooth saw envelope is allowed
        // to change subtly, and a flat spectrum should not acquire vowels.
        if (renderPath.isNotEmpty())
            for (const auto& fixture : demoFixtures)
            {
                const auto dry = makeFixture (fixture, sampleRate, 2 * probe.getLatencySamples());
                save (fixture, dry, dry, "-dry");
                for (float shift : { -6.0f, 6.0f })
                {
                    const auto wet = process (dry, sampleRate, shift);
                    save (fixture, dry, wet.aligned, shift < 0.0f ? "-shift-down" : "-shift-up");
                }
            }
    }

    if (renderPath.isNotEmpty())
    {
        filesOkay = directory.getChildFile ("formant-impact.csv").replaceWithText (csv.str()) && filesOkay;
        const juce::String notes =
            "Synthetic source-formant comparisons; not user recordings or a listening-test claim.\n"
            "The source-* fixtures contain known, arbitrary pairs of broad log-amplitude resonances\n"
            "on flat harmonic carriers at 80, 110, and 165 Hz. Source-formant mode must move their\n"
            "envelopes by the requested frequency ratio, while preserving the original harmonic grid.\n"
            "The tests measure shape correlation after removing level, two independent peak locations,\n"
            "and the best-fit shift over -10 to +10 semitones. Peak tolerance is 15% or one harmonic;\n"
            "fitted envelope shift tolerance is 1.5 semitones. These allow finite bass resolution.\n"
            "A flat carrier must have less than 1.5 dB centred harmonic-gain spread between 350-4500 Hz.\n"
            "Optional Vowel coloration is checked separately; zero Shift is neutral in either mode.\n"
            "Saw, square, Reese and FM examples are for listening, without a forced-change threshold.\n"
            "A source with a smooth or flat spectral envelope naturally gives a subtler formant shift.\n"
            "Settings: Focus .70, Bite/Drive 0, Mix 100%, Sub Safe OFF, Vowel OFF unless marked.\n"
            "Wet WAVs are latency-aligned and RMS-matched to dry over the same steady-state interval.\n"
            "CSV measurements use original engine output, without render gain matching.\n";
        filesOkay = directory.getChildFile ("README.txt").replaceWithText (notes) && filesOkay;
        std::cout << "Renders and measurements: " << directory.getFullPathName() << '\n';
    }
    std::cout << passed << '/' << checks << " source-formant checks passed.\n";
    return passed == checks && filesOkay ? 0 : 1;
}
