#include "FormantEngine.h"

#include <algorithm>
#include <cmath>

namespace shifty
{

namespace
{
constexpr float pi = juce::MathConstants<float>::pi;
constexpr float decibelsToLog = 0.11512925465f;
constexpr float maximumLogGain = 24.0f * decibelsToLog;

float guideEnvelope (float logFrequency, float width) noexcept
{
    // Optional colour only: these resonances are never part of the default
    // source-envelope analysis or formant relocation.
    const auto bell = [logFrequency, width] (float centre, float scale) noexcept
    {
        const float distance = (logFrequency - centre) / (width * scale);
        return std::exp (-0.5f * distance * distance);
    };
    return bell (8.81378119f, 1.0f)       // 450 Hz
         + 0.88f * bell (10.1032878f, 0.90f) // 1100 Hz
         + 0.68f * bell (11.2877124f, 0.85f); // 2500 Hz
}

float clampFinite (float value, float low, float high, float fallback) noexcept
{
    return std::isfinite (value) ? juce::jlimit (low, high, value) : fallback;
}

float addDrive (float sample, float amount) noexcept
{
    if (amount <= 0.00001f)
        return sample;

    // The dry interpolation makes zero drive exactly transparent. Saturation
    // adds harmonics before envelope estimation, so sparse basses also shift.
    const float driven = std::tanh (sample * (1.0f + 4.0f * amount))
                         / (1.0f + 0.5f * amount);
    return sample + amount * (driven - sample);
}
} // namespace

void FormantEngine::prepare (double sampleRate, int maxBlockSize, int channels)
{
    juce::ignoreUnused (maxBlockSize);
    sampleRate_ = (std::isfinite (sampleRate) && sampleRate > 1000.0) ? sampleRate : 48000.0;

    // Around 40 ms gives enough frequency detail for bass envelopes. The
    // latency is fixed for a given sample rate and reported to the host.
    fftSize_ = 1024;
    while (fftSize_ < sampleRate_ * 0.04 && fftSize_ < 8192)
        fftSize_ *= 2;

    hopSize_ = fftSize_ / 4;
    outputRingSize_ = fftSize_ * 2;
    levelAttackCoefficient_ = static_cast<float> (1.0 - std::exp (-hopSize_ / (0.035 * sampleRate_)));
    levelReleaseCoefficient_ = static_cast<float> (1.0 - std::exp (-hopSize_ / (0.120 * sampleRate_)));
    smoothingCoefficient_ = static_cast<float> (1.0 - std::exp (-1.0 / (0.02 * sampleRate_)));

    int order = 0;
    while ((1 << order) < fftSize_)
        ++order;

    bitReversal_.resize (static_cast<size_t> (fftSize_));
    twiddles_.resize (static_cast<size_t> (fftSize_ / 2));
    window_.resize (static_cast<size_t> (fftSize_));
    const auto bins = static_cast<size_t> (fftSize_ / 2 + 1);
    logFrequencies_.resize (bins);
    spectralFade_.resize (bins);
    guideDifference_.resize (bins);
    originalWeights_.resize (bins);
    normalizationWeights_.resize (bins);
    const float nyquist = static_cast<float> (sampleRate_ * 0.5);
    for (int bin = 0; bin <= fftSize_ / 2; ++bin)
    {
        const float frequency = static_cast<float> (bin * sampleRate_ / fftSize_);
        logFrequencies_[static_cast<size_t> (bin)] = std::log2 (std::max (frequency, 25.0f));
        const float lowFade = juce::jlimit (0.0f, 1.0f, (frequency - 25.0f) / 100.0f);
        const float highFade = juce::jlimit (0.0f, 1.0f, (nyquist - frequency) / (nyquist * 0.1f));
        spectralFade_[static_cast<size_t> (bin)] = lowFade * highFade;
    }

    for (int i = 0; i < fftSize_; ++i)
    {
        unsigned int reversed = 0;
        unsigned int value = static_cast<unsigned int> (i);
        for (int bit = 0; bit < order; ++bit)
        {
            reversed = (reversed << 1) | (value & 1u);
            value >>= 1;
        }
        bitReversal_[static_cast<size_t> (i)] = static_cast<int> (reversed);
        window_[static_cast<size_t> (i)] = std::sin (pi * (static_cast<float> (i) + 0.5f)
                                                   / static_cast<float> (fftSize_));
    }

    for (int i = 0; i < fftSize_ / 2; ++i)
    {
        const float angle = -2.0f * pi * static_cast<float> (i) / static_cast<float> (fftSize_);
        twiddles_[static_cast<size_t> (i)] = { std::cos (angle), std::sin (angle) };
    }

    channelStates_.resize (static_cast<size_t> (std::max (1, channels)));
    for (auto& state : channelStates_)
    {
        state.inputRing.resize (static_cast<size_t> (fftSize_));
        state.originalRing.resize (static_cast<size_t> (fftSize_));
        state.dryDelay.resize (static_cast<size_t> (fftSize_));
        state.outputRing.resize (static_cast<size_t> (outputRingSize_));
        state.logEnvelope.resize (static_cast<size_t> (fftSize_ / 2 + 1));
        state.envelopeTarget.resize (static_cast<size_t> (fftSize_ / 2 + 1));
        state.logGains.resize (static_cast<size_t> (fftSize_ / 2 + 1));
        state.peakDistances.resize (static_cast<size_t> (fftSize_ / 2 + 1));
        state.spectrum.resize (static_cast<size_t> (fftSize_));
        state.originalSpectrum.resize (static_cast<size_t> (fftSize_));
        state.cepstrum.resize (static_cast<size_t> (fftSize_));
    }

    reset();
}

void FormantEngine::reset()
{
    for (auto& state : channelStates_)
    {
        std::fill (state.inputRing.begin(), state.inputRing.end(), 0.0f);
        std::fill (state.originalRing.begin(), state.originalRing.end(), 0.0f);
        std::fill (state.dryDelay.begin(), state.dryDelay.end(), 0.0f);
        std::fill (state.outputRing.begin(), state.outputRing.end(), 0.0f);
        std::fill (state.logEnvelope.begin(), state.logEnvelope.end(), 0.0f);
        std::fill (state.envelopeTarget.begin(), state.envelopeTarget.end(), 0.0f);
        std::fill (state.logGains.begin(), state.logGains.end(), 0.0f);
        std::fill (state.peakDistances.begin(), state.peakDistances.end(), 0);
        state.frameActive = false;
        std::fill (state.spectrum.begin(), state.spectrum.end(), Complex {});
        std::fill (state.originalSpectrum.begin(), state.originalSpectrum.end(), Complex {});
        std::fill (state.cepstrum.begin(), state.cepstrum.end(), Complex {});
    }

    levelCompensation_ = 0.0f;
    inputPosition_ = 0;
    outputPosition_ = 0;
    samplesUntilFrame_ = hopSize_;
    current_.shift = targetShift_.load (std::memory_order_relaxed);
    current_.focus = targetFocus_.load (std::memory_order_relaxed);
    current_.bite = targetBite_.load (std::memory_order_relaxed);
    current_.drive = targetDrive_.load (std::memory_order_relaxed);
    current_.mix = targetMix_.load (std::memory_order_relaxed);
    current_.subCut = targetSubCut_.load (std::memory_order_relaxed);
    current_.subSafe = targetSubSafe_.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    current_.vowel = targetVowel_.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
}

void FormantEngine::setParameters (const EngineParams& params) noexcept
{
    targetShift_.store (clampFinite (params.shiftSemitones, -12.0f, 12.0f, 0.0f), std::memory_order_relaxed);
    targetFocus_.store (clampFinite (params.focus, 0.0f, 1.0f, 0.5f), std::memory_order_relaxed);
    targetBite_.store (clampFinite (params.bite, 0.0f, 1.0f, 0.0f), std::memory_order_relaxed);
    targetDrive_.store (clampFinite (params.drive, 0.0f, 1.0f, 0.0f), std::memory_order_relaxed);
    targetMix_.store (clampFinite (params.mix, 0.0f, 1.0f, 1.0f), std::memory_order_relaxed);
    targetSubCut_.store (clampFinite (params.subCutHz, 55.0f, 240.0f, 120.0f), std::memory_order_relaxed);
    targetSubSafe_.store (params.subSafe, std::memory_order_relaxed);
    targetVowel_.store (params.vowel, std::memory_order_relaxed);
}

void FormantEngine::transform (std::vector<Complex>& data, bool inverse) const noexcept
{
    const int size = fftSize_;
    for (int i = 0; i < size; ++i)
    {
        const int j = bitReversal_[static_cast<size_t> (i)];
        if (j > i)
            std::swap (data[static_cast<size_t> (i)], data[static_cast<size_t> (j)]);
    }

    for (int length = 2; length <= size; length *= 2)
    {
        const int half = length / 2;
        const int twiddleStep = size / length;
        for (int start = 0; start < size; start += length)
        {
            for (int i = 0; i < half; ++i)
            {
                Complex twiddle = twiddles_[static_cast<size_t> (i * twiddleStep)];
                if (inverse)
                    twiddle = std::conj (twiddle);

                const int a = start + i;
                const int b = a + half;
                const Complex rotated = data[static_cast<size_t> (b)] * twiddle;
                const Complex original = data[static_cast<size_t> (a)];
                data[static_cast<size_t> (a)] = original + rotated;
                data[static_cast<size_t> (b)] = original - rotated;
            }
        }
    }

    if (inverse)
    {
        const float scale = 1.0f / static_cast<float> (size);
        for (auto& sample : data)
            sample *= scale;
    }
}

float FormantEngine::envelopeAt (const ChannelState& state, float bin) const noexcept
{
    const float clamped = juce::jlimit (0.0f, static_cast<float> (fftSize_ / 2), bin);
    const int lower = static_cast<int> (clamped);
    const int upper = std::min (lower + 1, fftSize_ / 2);
    const float fraction = clamped - static_cast<float> (lower);
    return state.logEnvelope[static_cast<size_t> (lower)]
         + fraction * (state.logEnvelope[static_cast<size_t> (upper)]
                       - state.logEnvelope[static_cast<size_t> (lower)]);
}

void FormantEngine::processFrame (int inputStart, int outputStart, int channels) noexcept
{
    const bool needsFormant = std::abs (current_.shift) >= 0.0001f;
    const bool needsOriginalSpectrum = current_.subSafe > 0.0001f && current_.drive > 0.0001f;
    if (! needsFormant && ! needsOriginalSpectrum)
    {
        // Preserve the exact neutral overlap-add identity path.
        const int inputMask = fftSize_ - 1;
        const int outputMask = outputRingSize_ - 1;
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& state = channelStates_[static_cast<size_t> (channel)];
            for (int i = 0; i < fftSize_; ++i)
            {
                const float window = window_[static_cast<size_t> (i)];
                state.outputRing[static_cast<size_t> ((outputStart + i) & outputMask)] +=
                    0.5f * window * window
                    * state.inputRing[static_cast<size_t> ((inputStart + i) & inputMask)];
            }
        }
        levelCompensation_ = 0.0f;
        return;
    }

    const float shiftOctaves = current_.shift / 12.0f;
    const float width = 0.55f - 0.33f * current_.focus;
    const float guideDepth = current_.vowel * (8.0f + 10.0f * current_.bite) * decibelsToLog;
    const float binToHz = static_cast<float> (sampleRate_ / fftSize_);
    for (int i = 0; i <= fftSize_ / 2; ++i)
    {
        const auto bin = static_cast<size_t> (i);
        const float logFrequency = logFrequencies_[bin];
        guideDifference_[bin] = needsFormant && current_.vowel > 0.0001f
            ? guideDepth * (guideEnvelope (logFrequency - shiftOctaves, width)
                          - guideEnvelope (logFrequency, width)) : 0.0f;
        const float frequency = i * binToHz;
        const float transition = juce::jlimit (0.0f, 1.0f,
                                               (frequency - current_.subCut) / current_.subCut);
        const float lowWeight = 1.0f - transition * transition * (3.0f - 2.0f * transition);
        originalWeights_[bin] = current_.subSafe * lowWeight;
        normalizationWeights_[bin] = (1.0f - originalWeights_[bin]) * spectralFade_[bin];
    }

    double referenceEnergy = 0.0;
    double processedEnergy = 0.0;
    for (int channel = 0; channel < channels; ++channel)
        analyseFrame (channelStates_[static_cast<size_t> (channel)], inputStart,
                      needsFormant, needsOriginalSpectrum, referenceEnergy, processedEnergy);

    // One correction for the entire stereo frame prevents an independent
    // loudness follower from moving the stereo image. Measure the processed
    // band before restoring the original sub. Drive keeps its harmonic effect;
    // this correction removes gain changes caused by the formant relocation.
    float targetCompensation = 0.0f;
    const double noiseFloor = static_cast<double> (fftSize_) * fftSize_ * 1.0e-12;
    if (needsFormant && referenceEnergy > noiseFloor && processedEnergy > noiseFloor)
        targetCompensation = juce::jlimit (-12.0f * decibelsToLog, 6.0f * decibelsToLog,
            static_cast<float> (0.5 * std::log (referenceEnergy / processedEnergy)));
    const float coefficient = targetCompensation < levelCompensation_
                                  ? levelAttackCoefficient_ : levelReleaseCoefficient_;
    levelCompensation_ += coefficient * (targetCompensation - levelCompensation_);
    if (! needsFormant)
        levelCompensation_ = 0.0f;

    for (int channel = 0; channel < channels; ++channel)
        synthesiseFrame (channelStates_[static_cast<size_t> (channel)], outputStart,
                         needsOriginalSpectrum);
}

void FormantEngine::estimateEnvelope (ChannelState& state) noexcept
{
    const int lastBin = fftSize_ / 2;
    float maximumMagnitude = 0.0f;
    for (int i = 0; i <= lastBin; ++i)
        maximumMagnitude = std::max (maximumMagnitude,
                                     std::abs (state.spectrum[static_cast<size_t> (i)]));

    // A relative floor gives quiet and loud versions of the same sound the
    // same envelope. It also limits empty-bin influence between harmonics.
    const float magnitudeFloor = std::max (1.0e-20f, maximumMagnitude * 1.0e-6f);
    const float peakFloor = std::log (std::max (magnitudeFloor, maximumMagnitude * 0.001f));
    for (int i = 0; i <= lastBin; ++i)
        state.envelopeTarget[static_cast<size_t> (i)] =
            std::log (std::max (magnitudeFloor, std::abs (state.spectrum[static_cast<size_t> (i)])));

    // Focus requests finer detail in the source contour. Limit that detail
    // using prominent-peak spacing: following individual harmonics would
    // confuse pitch with formant shape. This also handles odd-only spectra
    // without requiring a monophonic pitch tracker or an extra FFT.
    int maximumQuefrency = juce::jlimit (8, fftSize_ / 6,
        static_cast<int> (std::lround (sampleRate_ * (0.0015 + 0.0055 * current_.focus))));
    int previousPeak = -1;
    int distanceCount = 0;
    int peakRadius = 1;
    const float binToHz = static_cast<float> (sampleRate_ / fftSize_);
    const int firstPeakBin = std::max (2, static_cast<int> (150.0f / binToHz));
    const int finalPeakBin = std::min (lastBin - 1, static_cast<int> (8000.0f / binToHz));
    for (int i = firstPeakBin; i <= finalPeakBin; ++i)
    {
        const auto bin = static_cast<size_t> (i);
        const float value = state.envelopeTarget[bin];
        if (value > peakFloor && value > state.envelopeTarget[bin - 1]
                              && value >= state.envelopeTarget[bin + 1])
        {
            if (previousPeak >= 0)
                state.peakDistances[static_cast<size_t> (distanceCount++)] = i - previousPeak;
            previousPeak = i;
        }
    }
    if (distanceCount >= 5)
    {
        const int quartile = 3 * distanceCount / 4;
        std::nth_element (state.peakDistances.begin(), state.peakDistances.begin() + quartile,
                          state.peakDistances.begin() + distanceCount);
        const int spacing = std::max (1, state.peakDistances[static_cast<size_t> (quartile)]);
        maximumQuefrency = std::min (maximumQuefrency,
                                    std::max (8, static_cast<int> (0.72f * fftSize_ / spacing)));
        peakRadius = juce::jlimit (1, 8, static_cast<int> (std::lround (0.25f * spacing)));
    }

    // Seed from a narrow maximum filter, not the deep gaps between partials.
    // Otherwise a fixed number of refinements retains gap/phase patterns on
    // higher notes and mistakes those patterns for broad source resonances.
    // Its width follows a fraction of the observed partial spacing; it never
    // supplies resonance locations or a fixed tonal contour of its own.
    for (int i = 0; i <= lastBin; ++i)
    {
        float peak = state.envelopeTarget[static_cast<size_t> (i)];
        for (int j = std::max (0, i - peakRadius); j <= std::min (lastBin, i + peakRadius); ++j)
            peak = std::max (peak, state.envelopeTarget[static_cast<size_t> (j)]);
        state.logEnvelope[static_cast<size_t> (i)] = peak;
    }
    std::copy (state.logEnvelope.begin(), state.logEnvelope.end(), state.envelopeTarget.begin());

    // Bounded true-envelope refinement, following the peak-filling approach
    // of Roebel/Rodet, DAFx 2005 (https://www.dafx.de/paper-archive/2005/P_030.pdf).
    // Ordinary cepstral averaging sinks into harmonic gaps and blurs away the
    // very resonances being shifted. Two refinements lift those gaps toward
    // the source peaks; a tapered lifter suppresses ringing. Three total
    // passes keep the real-time workload fixed rather than waiting to converge.
    for (int pass = 0; pass < 3; ++pass)
    {
        for (int i = 0; i <= lastBin; ++i)
        {
            const auto bin = static_cast<size_t> (i);
            if (pass != 0)
                state.envelopeTarget[bin] = std::max (state.envelopeTarget[bin], state.logEnvelope[bin]);
            const Complex value { state.envelopeTarget[bin], 0.0f };
            state.cepstrum[bin] = value;
            if (i > 0 && i < lastBin)
                state.cepstrum[static_cast<size_t> (fftSize_ - i)] = value;
        }
        transform (state.cepstrum, true);
        for (int i = 0; i < fftSize_; ++i)
        {
            const int quefrency = std::min (i, fftSize_ - i);
            if (quefrency > maximumQuefrency)
                state.cepstrum[static_cast<size_t> (i)] = {};
            else
            {
                const float taper = 0.5f + 0.5f * std::cos (pi * static_cast<float> (quefrency)
                                                              / static_cast<float> (maximumQuefrency + 1));
                state.cepstrum[static_cast<size_t> (i)] *= taper;
            }
        }
        transform (state.cepstrum, false);
        for (int i = 0; i <= lastBin; ++i)
            state.logEnvelope[static_cast<size_t> (i)] = state.cepstrum[static_cast<size_t> (i)].real();
    }
}

void FormantEngine::analyseFrame (ChannelState& state, int inputStart, bool needsFormant,
                                 bool needsOriginalSpectrum, double& referenceEnergy,
                                 double& processedEnergy) noexcept
{
    const int inputMask = fftSize_ - 1;
    float energy = 0.0f;
    for (int i = 0; i < fftSize_; ++i)
    {
        const float sample = state.inputRing[static_cast<size_t> ((inputStart + i) & inputMask)]
                             * window_[static_cast<size_t> (i)];
        state.spectrum[static_cast<size_t> (i)] = { sample, 0.0f };
        energy += sample * sample;
    }
    state.frameActive = energy >= 1.0e-15f;
    if (! state.frameActive)
        return;
    transform (state.spectrum, false);

    if (needsOriginalSpectrum)
    {
        for (int i = 0; i < fftSize_; ++i)
            state.originalSpectrum[static_cast<size_t> (i)] = {
                state.originalRing[static_cast<size_t> ((inputStart + i) & inputMask)]
                * window_[static_cast<size_t> (i)], 0.0f
            };
        transform (state.originalSpectrum, false);
    }

    if (needsFormant)
    {
        estimateEnvelope (state);
    }

    const float ratio = std::exp2 (current_.shift / 12.0f);
    const float strength = 1.0f + 1.25f * current_.bite;
    for (int i = 0; i <= fftSize_ / 2; ++i)
    {
        const auto bin = static_cast<size_t> (i);
        float logGain = 0.0f;
        if (needsFormant)
        {
            const float original = state.logEnvelope[bin];
            const float moved = envelopeAt (state, static_cast<float> (i) / ratio);
            logGain = juce::jlimit (-maximumLogGain, maximumLogGain,
                ((moved - original) * strength + guideDifference_[bin]) * spectralFade_[bin]);
        }
        state.logGains[bin] = logGain;
        const double weightedEnergy = static_cast<double> (std::norm (state.spectrum[bin]))
                                      * normalizationWeights_[bin];
        referenceEnergy += weightedEnergy;
        processedEnergy += weightedEnergy * std::exp (2.0f * logGain);
    }
}

void FormantEngine::synthesiseFrame (ChannelState& state, int outputStart,
                                   bool needsOriginalSpectrum) noexcept
{
    if (! state.frameActive)
        return;
    // Let the neutral point remain transparent even while the linked level
    // follower is settling from a large sweep in the preceding frame.
    const float neutralFade = juce::jlimit (0.0f, 1.0f, std::abs (current_.shift));
    for (int i = 0; i <= fftSize_ / 2; ++i)
    {
        const auto bin = static_cast<size_t> (i);
        const float logGain = juce::jlimit (-maximumLogGain, maximumLogGain,
            state.logGains[bin] + levelCompensation_ * neutralFade * spectralFade_[bin]);
        const Complex source = state.spectrum[bin];
        Complex shifted = source * std::exp (logGain);
        // Original complex bins retain the low-end phase as well as level.
        const Complex original = needsOriginalSpectrum ? state.originalSpectrum[bin] : source;
        shifted += originalWeights_[bin] * (original - shifted);
        state.spectrum[bin] = shifted;
        if (i > 0 && i < fftSize_ / 2)
            state.spectrum[static_cast<size_t> (fftSize_ - i)] = std::conj (shifted);
    }
    transform (state.spectrum, true);
    const int outputMask = outputRingSize_ - 1;
    for (int i = 0; i < fftSize_; ++i)
        state.outputRing[static_cast<size_t> ((outputStart + i) & outputMask)] +=
            0.5f * window_[static_cast<size_t> (i)] * state.spectrum[static_cast<size_t> (i)].real();
}

void FormantEngine::processBlock (juce::AudioBuffer<float>& buffer)
{
    if (fftSize_ == 0 || buffer.getNumSamples() == 0)
        return;

    juce::ScopedNoDenormals noDenormals;
    const int channels = std::min (buffer.getNumChannels(), static_cast<int> (channelStates_.size()));
    const int inputMask = fftSize_ - 1;
    const int outputMask = outputRingSize_ - 1;

    SmoothedParams target;
    target.shift = targetShift_.load (std::memory_order_relaxed);
    target.focus = targetFocus_.load (std::memory_order_relaxed);
    target.bite = targetBite_.load (std::memory_order_relaxed);
    target.drive = targetDrive_.load (std::memory_order_relaxed);
    target.mix = targetMix_.load (std::memory_order_relaxed);
    target.subCut = targetSubCut_.load (std::memory_order_relaxed);
    target.subSafe = targetSubSafe_.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    target.vowel = targetVowel_.load (std::memory_order_relaxed) ? 1.0f : 0.0f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        auto smooth = [this] (float& value, float destination) noexcept
        {
            value += smoothingCoefficient_ * (destination - value);
        };
        smooth (current_.shift, target.shift);
        smooth (current_.focus, target.focus);
        smooth (current_.bite, target.bite);
        smooth (current_.drive, target.drive);
        smooth (current_.mix, target.mix);
        smooth (current_.subCut, target.subCut);
        smooth (current_.subSafe, target.subSafe);
        smooth (current_.vowel, target.vowel);

        const bool frameDue = --samplesUntilFrame_ == 0;
        const int inputStart = (inputPosition_ + 1) & inputMask;
        const int outputStart = (outputPosition_ + 1) & outputMask;

        for (int channel = 0; channel < channels; ++channel)
        {
            auto& state = channelStates_[static_cast<size_t> (channel)];
            float* data = buffer.getWritePointer (channel);
            const float input = std::isfinite (data[sample]) ? data[sample] : 0.0f;
            state.originalRing[static_cast<size_t> (inputPosition_)] = input;
            state.inputRing[static_cast<size_t> (inputPosition_)] = addDrive (input, current_.drive);
        }

        if (frameDue)
            processFrame (inputStart, outputStart, channels);

        for (int channel = 0; channel < channels; ++channel)
        {
            auto& state = channelStates_[static_cast<size_t> (channel)];
            float* data = buffer.getWritePointer (channel);
            const float dry = state.dryDelay[static_cast<size_t> (inputPosition_)];
            state.dryDelay[static_cast<size_t> (inputPosition_)] =
                state.originalRing[static_cast<size_t> (inputPosition_)];
            const float wet = state.outputRing[static_cast<size_t> (outputPosition_)];
            state.outputRing[static_cast<size_t> (outputPosition_)] = 0.0f;
            data[sample] = dry + current_.mix * (wet - dry);
        }

        if (frameDue)
            samplesUntilFrame_ = hopSize_;
        inputPosition_ = inputStart;
        outputPosition_ = (outputPosition_ + 1) & outputMask;
    }
}

} // namespace shifty
