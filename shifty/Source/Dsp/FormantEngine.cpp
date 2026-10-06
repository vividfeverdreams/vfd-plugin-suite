#include "FormantEngine.h"

#include <algorithm>
#include <cmath>

namespace shifty
{

namespace
{
constexpr float pi = juce::MathConstants<float>::pi;
constexpr float decibelsToLog = 0.11512925465f;
constexpr float logToDecibels = 8.68588963807f;
constexpr float maximumLogGain = 24.0f * decibelsToLog;
// Latest mode: a short symmetric smoother on each frame's own measured energy
// ratio. The +12 dB ceiling lets bright sources keep their level when shifted
// far down; the result is energy matched, not boosted.
constexpr float minimumCompensation = -12.0f * decibelsToLog;
constexpr float maximumCompensation = 12.0f * decibelsToLog;
constexpr double compensationSmoothingSeconds = 0.005;
// Latest mode: the frame is zero-padded to twice its length for the envelope
// estimate, and the estimate is averaged across frames. Notes below about
// 47 Hz put two partials into one analysis bin, and the per-frame estimate then
// flickered with the waveform's phase ("warble"): 1.04 dB per-partial level
// deviation on a 41 Hz note in 0.4.2, 0.28 dB now (0.23 dB at low latency).
constexpr int envelopePadding = 2;
constexpr double envelopeSmoothingSeconds = 0.045;
// Moving-gain smoothing: 8 binomial passes (about +/-2 bins), fully applied
// once SHIFT changes by 2 semitones across one frame.
constexpr int motionSmoothingPasses = 8;
constexpr float fullSmoothingShiftChange = 2.0f;

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

int orderOf (int size) noexcept
{
    int order = 0;
    while ((1 << order) < size)
        ++order;
    return order;
}
} // namespace

int FormantEngine::latencyFor (double sampleRate, bool lowLatency) noexcept
{
    const double rate = (std::isfinite (sampleRate) && sampleRate > 1000.0) ? sampleRate : 48000.0;
    // Around 40 ms gives enough frequency detail for bass envelopes; the
    // low-latency resolution uses half of that frame.
    int size = 1024;
    while (size < rate * 0.04 && size < 8192)
        size *= 2;
    return lowLatency ? size / 2 : size;
}

void FormantEngine::configure (Mode mode, bool lowLatency) noexcept
{
    mode_ = mode;
    lowLatency_ = mode == Mode::latest && lowLatency;
}

void FormantEngine::prepare (double sampleRate, int maxBlockSize, int channels)
{
    juce::ignoreUnused (maxBlockSize);
    sampleRate_ = (std::isfinite (sampleRate) && sampleRate > 1000.0) ? sampleRate : 48000.0;

    fftSize_ = latencyFor (sampleRate_, lowLatency_);
    padding_ = mode_ == Mode::latest ? envelopePadding : 1;
    analysisSize_ = fftSize_ * padding_;
    hopSize_ = fftSize_ / 4;
    outputRingSize_ = fftSize_ * 2;
    levelSmoothingCoefficient_ = static_cast<float> (
        1.0 - std::exp (-hopSize_ / (compensationSmoothingSeconds * sampleRate_)));
    originalAttackCoefficient_ = static_cast<float> (1.0 - std::exp (-hopSize_ / (0.035 * sampleRate_)));
    originalReleaseCoefficient_ = static_cast<float> (1.0 - std::exp (-hopSize_ / (0.120 * sampleRate_)));
    envelopeSmoothingCoefficient_ = static_cast<float> (1.0 - std::exp (
        -hopSize_ / (envelopeSmoothingSeconds * sampleRate_)));
    smoothingCoefficient_ = static_cast<float> (1.0 - std::exp (-1.0 / (0.02 * sampleRate_)));

    // juce::dsp::FFT uses vDSP on Apple platforms. Construction allocates, so
    // it happens here rather than on the audio thread.
    if (analysisFft_ == nullptr || analysisFft_->getSize() != analysisSize_)
        analysisFft_ = std::make_unique<juce::dsp::FFT> (orderOf (analysisSize_));
    if (synthesisFft_ == nullptr || synthesisFft_->getSize() != fftSize_)
        synthesisFft_ = std::make_unique<juce::dsp::FFT> (orderOf (fftSize_));

    window_.resize (static_cast<size_t> (fftSize_));
    motionRing_.resize (static_cast<size_t> (fftSize_));
    lifterTaper_.resize (static_cast<size_t> (fftSize_ / 6 + 2));
    const auto bins = static_cast<size_t> (fftSize_ / 2 + 1);
    const auto analysisBins = static_cast<size_t> (analysisSize_ / 2 + 1);
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
        window_[static_cast<size_t> (i)] = std::sin (pi * (static_cast<float> (i) + 0.5f)
                                                   / static_cast<float> (fftSize_));

    channelStates_.resize (static_cast<size_t> (std::max (1, channels)));
    for (auto& state : channelStates_)
    {
        state.inputRing.resize (static_cast<size_t> (fftSize_));
        state.originalRing.resize (static_cast<size_t> (fftSize_));
        state.dryDelay.resize (static_cast<size_t> (fftSize_));
        state.outputRing.resize (static_cast<size_t> (outputRingSize_));
        state.logEnvelope.resize (analysisBins);
        state.smoothedEnvelope.resize (analysisBins);
        state.envelopeTarget.resize (std::max (analysisBins, bins));
        state.logGains.resize (bins);
        state.gainScratch.resize (bins);
        state.peakDistances.resize (analysisBins);
        state.analysis.resize (static_cast<size_t> (analysisSize_));
        state.originalAnalysis.resize (static_cast<size_t> (analysisSize_));
        state.cepstrum.resize (static_cast<size_t> (analysisSize_));
        state.spectrum.resize (static_cast<size_t> (fftSize_));
        state.originalSpectrum.resize (static_cast<size_t> (fftSize_));
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
        std::fill (state.smoothedEnvelope.begin(), state.smoothedEnvelope.end(), 0.0f);
        std::fill (state.envelopeTarget.begin(), state.envelopeTarget.end(), 0.0f);
        std::fill (state.logGains.begin(), state.logGains.end(), 0.0f);
        std::fill (state.gainScratch.begin(), state.gainScratch.end(), 0.0f);
        std::fill (state.peakDistances.begin(), state.peakDistances.end(), 0);
        state.frameActive = false;
        state.envelopeSeeded = false;
        state.drive.reset();
        for (auto* buffer : { &state.analysis, &state.originalAnalysis, &state.cepstrum,
                              &state.spectrum, &state.originalSpectrum })
            std::fill (buffer->begin(), buffer->end(), Complex {});
    }

    std::fill (motionRing_.begin(), motionRing_.end(), 0.0f);
    levelCompensation_ = 0.0f;
    compensationSeeded_ = false;
    motionSmoothing_ = 0.0f;
    displayValid_ = false;
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
    frameShift_ = current_.shift;
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

void FormantEngine::analyseReal (std::vector<Complex>& data) const noexcept
{
    // Input: analysisSize_ real samples in the first half of the float view.
    // Output: bins 0..analysisSize_/2 as complex values (unnormalised DFT).
    analysisFft_->performRealOnlyForwardTransform (reinterpret_cast<float*> (data.data()), true);
}

void FormantEngine::synthesiseReal (std::vector<Complex>& data) const noexcept
{
    // Input: bins 0..fftSize_/2. Output: fftSize_ real samples scaled by 1/fftSize_.
    synthesisFft_->performRealOnlyInverseTransform (reinterpret_cast<float*> (data.data()));
}

float FormantEngine::envelopeAt (const std::vector<float>& envelope, float analysisBin) const noexcept
{
    const int lastBin = analysisSize_ / 2;
    const float clamped = juce::jlimit (0.0f, static_cast<float> (lastBin), analysisBin);
    const int lower = static_cast<int> (clamped);
    const int upper = std::min (lower + 1, lastBin);
    const float fraction = clamped - static_cast<float> (lower);
    return envelope[static_cast<size_t> (lower)]
         + fraction * (envelope[static_cast<size_t> (upper)] - envelope[static_cast<size_t> (lower)]);
}

void FormantEngine::processFrame (int inputStart, int outputStart, int channels) noexcept
{
    // Latest mode: Motion offsets travel with the input samples, so each frame
    // applies the offset at its own centre (between window samples N/2-1 and
    // N/2) and the modulation stays aligned with the audio it shapes. The
    // change across the frame measures how fast Motion is moving.
    const int inputMask = fftSize_ - 1;
    const float motionCentre = 0.5f * (motionRing_[static_cast<size_t> ((inputStart + fftSize_ / 2 - 1) & inputMask)]
                                     + motionRing_[static_cast<size_t> ((inputStart + fftSize_ / 2) & inputMask)]);
    const float motionStart = motionRing_[static_cast<size_t> (inputStart & inputMask)];
    const float motionEnd = motionRing_[static_cast<size_t> ((inputStart + fftSize_ - 1) & inputMask)];
    frameShift_ = juce::jlimit (-12.0f, 12.0f, current_.shift + motionCentre);
    const float shiftChange = std::abs (juce::jlimit (-12.0f, 12.0f, current_.shift + motionEnd)
                                      - juce::jlimit (-12.0f, 12.0f, current_.shift + motionStart));
    motionSmoothing_ = mode_ == Mode::latest
        ? juce::jlimit (0.0f, 1.0f, shiftChange / fullSmoothingShiftChange) : 0.0f;

    const bool needsFormant = std::abs (frameShift_) >= 0.0001f;
    const bool needsOriginalSpectrum = current_.subSafe > 0.0001f && current_.drive > 0.0001f;
    if (! needsFormant && ! needsOriginalSpectrum)
    {
        // Preserve the exact neutral overlap-add identity path.
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
            state.envelopeSeeded = false;
        }
        levelCompensation_ = 0.0f;
        compensationSeeded_ = false;
        displayValid_ = false;
        return;
    }

    computeGuide (frameShift_, guideDifference_);
    const float binToHz = static_cast<float> (sampleRate_ / fftSize_);
    for (int i = 0; i <= fftSize_ / 2; ++i)
    {
        const auto bin = static_cast<size_t> (i);
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
    const double noiseFloor = static_cast<double> (fftSize_) * fftSize_ * 1.0e-12;
    if (mode_ == Mode::original042)
    {
        // 0.4.2: a 35 ms / 120 ms follower, limited to -12/+6 dB.
        float targetCompensation = 0.0f;
        if (needsFormant && referenceEnergy > noiseFloor && processedEnergy > noiseFloor)
            targetCompensation = juce::jlimit (-12.0f * decibelsToLog, 6.0f * decibelsToLog,
                static_cast<float> (0.5 * std::log (referenceEnergy / processedEnergy)));
        const float coefficient = targetCompensation < levelCompensation_
                                      ? originalAttackCoefficient_ : originalReleaseCoefficient_;
        levelCompensation_ += coefficient * (targetCompensation - levelCompensation_);
        if (! needsFormant)
            levelCompensation_ = 0.0f;
    }
    else if (! needsFormant)
    {
        levelCompensation_ = 0.0f;
        compensationSeeded_ = false;
    }
    else if (referenceEnergy > noiseFloor && processedEnergy > noiseFloor)
    {
        // The ratio is measured on this frame, so it needs only light smoothing.
        // It is seeded directly when leaving the neutral path or silence.
        const float targetCompensation = juce::jlimit (minimumCompensation, maximumCompensation,
            static_cast<float> (0.5 * std::log (referenceEnergy / processedEnergy)));
        if (compensationSeeded_)
            levelCompensation_ += levelSmoothingCoefficient_ * (targetCompensation - levelCompensation_);
        else
            levelCompensation_ = targetCompensation;
        compensationSeeded_ = true;
    }
    else
    {
        // Too quiet to measure: hold the last value and seed on the next frame.
        compensationSeeded_ = false;
    }

    for (int channel = 0; channel < channels; ++channel)
        synthesiseFrame (channelStates_[static_cast<size_t> (channel)], outputStart,
                         needsOriginalSpectrum);
}

void FormantEngine::estimateEnvelope (ChannelState& state) noexcept
{
    const int lastBin = analysisSize_ / 2;
    float maximumMagnitude = 0.0f;
    for (int i = 0; i <= lastBin; ++i)
        maximumMagnitude = std::max (maximumMagnitude,
                                     std::abs (state.analysis[static_cast<size_t> (i)]));

    // A relative floor gives quiet and loud versions of the same sound the
    // same envelope. It also limits empty-bin influence between harmonics.
    const float magnitudeFloor = std::max (1.0e-20f, maximumMagnitude * 1.0e-6f);
    const float peakFloor = std::log (std::max (magnitudeFloor, maximumMagnitude * 0.001f));
    for (int i = 0; i <= lastBin; ++i)
        state.envelopeTarget[static_cast<size_t> (i)] =
            std::log (std::max (magnitudeFloor, std::abs (state.analysis[static_cast<size_t> (i)])));

    // Focus requests finer detail in the source contour. Limit that detail
    // using prominent-peak spacing: following individual harmonics would
    // confuse pitch with formant shape. This also handles odd-only spectra
    // without requiring a monophonic pitch tracker or an extra FFT.
    int maximumQuefrency = juce::jlimit (8, fftSize_ / 6,
        static_cast<int> (std::lround (sampleRate_ * (0.0015 + 0.0055 * current_.focus))));
    int previousPeak = -1;
    int distanceCount = 0;
    int peakRadius = 1;
    const float binToHz = static_cast<float> (sampleRate_ / analysisSize_);
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
                                    std::max (8, static_cast<int> (0.72f * analysisSize_ / spacing)));
        peakRadius = juce::jlimit (1, 8, static_cast<int> (std::lround (0.25f * spacing)));
    }

    // Seed from a narrow maximum filter, not the deep gaps between partials.
    // Otherwise a fixed number of refinements retains gap/phase patterns on
    // higher notes and mistakes those patterns for broad source resonances.
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
    for (int quefrency = 0; quefrency <= maximumQuefrency; ++quefrency)
        lifterTaper_[static_cast<size_t> (quefrency)] =
            0.5f + 0.5f * std::cos (pi * static_cast<float> (quefrency)
                                    / static_cast<float> (maximumQuefrency + 1));
    for (int pass = 0; pass < 3; ++pass)
    {
        for (int i = 0; i <= lastBin; ++i)
        {
            const auto bin = static_cast<size_t> (i);
            if (pass != 0)
                state.envelopeTarget[bin] = std::max (state.envelopeTarget[bin], state.logEnvelope[bin]);
            // The log spectrum is real and even, so its cepstrum is real.
            state.cepstrum[bin] = Complex { state.envelopeTarget[bin], 0.0f };
        }
        analysisFft_->performRealOnlyInverseTransform (reinterpret_cast<float*> (state.cepstrum.data()));
        float* const cepstrum = reinterpret_cast<float*> (state.cepstrum.data());
        for (int i = 0; i < analysisSize_; ++i)
        {
            const int quefrency = std::min (i, analysisSize_ - i);
            if (quefrency > maximumQuefrency)
                cepstrum[i] = 0.0f;
            else
                cepstrum[i] *= lifterTaper_[static_cast<size_t> (quefrency)];
        }
        analyseReal (state.cepstrum);
        for (int i = 0; i <= lastBin; ++i)
            state.logEnvelope[static_cast<size_t> (i)] = state.cepstrum[static_cast<size_t> (i)].real();
    }

    if (mode_ == Mode::latest)
    {
        // Average the estimate across frames; seed it on the first frame after
        // silence or the neutral path so a new note does not fade in.
        if (state.envelopeSeeded)
            for (size_t bin = 0; bin < state.logEnvelope.size(); ++bin)
                state.smoothedEnvelope[bin] += envelopeSmoothingCoefficient_
                                               * (state.logEnvelope[bin] - state.smoothedEnvelope[bin]);
        else
            std::copy (state.logEnvelope.begin(), state.logEnvelope.end(), state.smoothedEnvelope.begin());
        state.envelopeSeeded = true;
    }
}

void FormantEngine::analyseFrame (ChannelState& state, int inputStart, bool needsFormant,
                                 bool needsOriginalSpectrum, double& referenceEnergy,
                                 double& processedEnergy) noexcept
{
    const int inputMask = fftSize_ - 1;
    float energy = 0.0f;
    float* const frame = reinterpret_cast<float*> (state.analysis.data());
    for (int i = 0; i < fftSize_; ++i)
    {
        const float sample = state.inputRing[static_cast<size_t> ((inputStart + i) & inputMask)]
                             * window_[static_cast<size_t> (i)];
        frame[i] = sample;
        energy += sample * sample;
    }
    state.frameActive = energy >= 1.0e-15f;
    if (! state.frameActive)
    {
        state.envelopeSeeded = false;
        return;
    }
    // Low-latency resolution: zero-pad the frame. Every padding_-th analysis
    // bin is exactly the synthesis frame's own DFT bin.
    std::fill (frame + fftSize_, frame + analysisSize_, 0.0f);
    analyseReal (state.analysis);
    for (int i = 0; i <= fftSize_ / 2; ++i)
        state.spectrum[static_cast<size_t> (i)] = state.analysis[static_cast<size_t> (i * padding_)];

    if (needsOriginalSpectrum)
    {
        float* const original = reinterpret_cast<float*> (state.originalAnalysis.data());
        for (int i = 0; i < fftSize_; ++i)
            original[i] = state.originalRing[static_cast<size_t> ((inputStart + i) & inputMask)]
                          * window_[static_cast<size_t> (i)];
        std::fill (original + fftSize_, original + analysisSize_, 0.0f);
        analyseReal (state.originalAnalysis);
        for (int i = 0; i <= fftSize_ / 2; ++i)
            state.originalSpectrum[static_cast<size_t> (i)] = state.originalAnalysis[static_cast<size_t> (i * padding_)];
    }

    if (needsFormant)
    {
        estimateEnvelope (state);
        computeLogGains (state, frameShift_, guideDifference_, state.logGains);
        if (motionSmoothing_ > 0.0f)
            smoothMovingGains (state);
        if (&state == &channelStates_.front())
            updateDisplay (state);
    }
    else
    {
        std::fill (state.logGains.begin(), state.logGains.end(), 0.0f);
    }

    for (int i = 0; i <= fftSize_ / 2; ++i)
    {
        const auto bin = static_cast<size_t> (i);
        const double weightedEnergy = static_cast<double> (std::norm (state.spectrum[bin]))
                                      * normalizationWeights_[bin];
        referenceEnergy += weightedEnergy;
        processedEnergy += weightedEnergy * std::exp (2.0f * state.logGains[bin]);
    }
}

void FormantEngine::updateDisplay (const ChannelState& state) noexcept
{
    const auto& envelope = mode_ == Mode::latest ? state.smoothedEnvelope : state.logEnvelope;
    const float ratio = std::exp2 (frameShift_ / 12.0f);
    const float binsPerHz = static_cast<float> (analysisSize_ / sampleRate_);
    float peak = -1.0e9f;
    for (int point = 0; point < displayPoints; ++point)
    {
        const float bin = displayFrequency (point, sampleRate_) * binsPerHz;
        displayDetected_[static_cast<size_t> (point)] = envelopeAt (envelope, bin) * logToDecibels;
        displayMoved_[static_cast<size_t> (point)] = envelopeAt (envelope, bin / ratio) * logToDecibels;
        peak = std::max (peak, displayDetected_[static_cast<size_t> (point)]);
    }
    for (int point = 0; point < displayPoints; ++point)
    {
        displayDetected_[static_cast<size_t> (point)] -= peak;
        displayMoved_[static_cast<size_t> (point)] -= peak;
    }
    displayValid_ = true;
}

void FormantEngine::smoothMovingGains (ChannelState& state) const noexcept
{
    // A partial spans neighbouring bins. When their gains differ steeply and
    // change every frame (fast, deep Motion with BITE), overlap-add no longer
    // rebuilds the partial cleanly and it can momentarily drop far below any
    // static setting. While Motion moves, blend toward a frequency-smoothed
    // gain curve, in proportion to the speed: none at the LFO's turning points
    // or when Motion is still, so static sound is unchanged. envelopeTarget is
    // free scratch space once the envelope has been estimated.
    auto& smoothed = state.gainScratch;
    auto& scratch = state.envelopeTarget;
    std::copy (state.logGains.begin(), state.logGains.end(), smoothed.begin());
    const auto last = static_cast<size_t> (fftSize_ / 2);
    for (int pass = 0; pass < motionSmoothingPasses; ++pass)
    {
        scratch[0] = 0.75f * smoothed[0] + 0.25f * smoothed[1];
        for (size_t i = 1; i < last; ++i)
            scratch[i] = 0.25f * smoothed[i - 1] + 0.5f * smoothed[i] + 0.25f * smoothed[i + 1];
        scratch[last] = 0.25f * smoothed[last - 1] + 0.75f * smoothed[last];
        std::copy (scratch.begin(), scratch.begin() + static_cast<std::ptrdiff_t> (last + 1), smoothed.begin());
    }
    for (size_t i = 0; i <= last; ++i)
        state.logGains[i] += motionSmoothing_ * (smoothed[i] - state.logGains[i]);
}

void FormantEngine::computeGuide (float shift, std::vector<float>& destination) const noexcept
{
    if (current_.vowel <= 0.0001f || std::abs (shift) < 0.0001f)
    {
        std::fill (destination.begin(), destination.end(), 0.0f);
        return;
    }
    const float shiftOctaves = shift / 12.0f;
    const float width = 0.55f - 0.33f * current_.focus;
    const float guideDepth = current_.vowel * (8.0f + 10.0f * current_.bite) * decibelsToLog;
    for (int i = 0; i <= fftSize_ / 2; ++i)
    {
        const auto bin = static_cast<size_t> (i);
        const float logFrequency = logFrequencies_[bin];
        destination[bin] = guideDepth * (guideEnvelope (logFrequency - shiftOctaves, width)
                                       - guideEnvelope (logFrequency, width));
    }
}

void FormantEngine::computeLogGains (const ChannelState& state, float shift,
                                     const std::vector<float>& guide,
                                     std::vector<float>& destination) const noexcept
{
    const auto& envelope = mode_ == Mode::latest ? state.smoothedEnvelope : state.logEnvelope;
    const float ratio = std::exp2 (shift / 12.0f);
    const float strength = 1.0f + 1.25f * current_.bite;
    for (int i = 0; i <= fftSize_ / 2; ++i)
    {
        const auto bin = static_cast<size_t> (i);
        const float analysisBin = static_cast<float> (i * padding_);
        const float original = envelope[static_cast<size_t> (i * padding_)];
        const float moved = envelopeAt (envelope, analysisBin / ratio);
        destination[bin] = juce::jlimit (-maximumLogGain, maximumLogGain,
            ((moved - original) * strength + guide[bin]) * spectralFade_[bin]);
    }
}

void FormantEngine::synthesiseFrame (ChannelState& state, int outputStart,
                                   bool needsOriginalSpectrum) noexcept
{
    if (! state.frameActive)
        return;
    // Let the neutral point remain transparent even while the linked level
    // follower is settling from a large sweep in the preceding frame.
    const float neutralFade = juce::jlimit (0.0f, 1.0f, std::abs (frameShift_));
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
    }
    // Only bins 0..N/2 are needed: the real inverse supplies the conjugate half.
    synthesiseReal (state.spectrum);
    const float* const frame = reinterpret_cast<const float*> (state.spectrum.data());
    const int outputMask = outputRingSize_ - 1;
    for (int i = 0; i < fftSize_; ++i)
        state.outputRing[static_cast<size_t> ((outputStart + i) & outputMask)] +=
            0.5f * window_[static_cast<size_t> (i)] * frame[i];
}

void FormantEngine::processBlock (juce::AudioBuffer<float>& buffer, const float* motionOffsets)
{
    if (fftSize_ == 0 || buffer.getNumSamples() == 0)
        return;

    juce::ScopedNoDenormals noDenormals;
    const int channels = std::min (buffer.getNumChannels(), static_cast<int> (channelStates_.size()));
    const int inputMask = fftSize_ - 1;
    const int outputMask = outputRingSize_ - 1;
    const bool original = mode_ == Mode::original042;

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

        // Motion offsets travel with the input samples they belong to.
        const float motion = (! original && motionOffsets != nullptr) ? motionOffsets[sample] : 0.0f;
        motionRing_[static_cast<size_t> (inputPosition_)] = std::isfinite (motion) ? motion : 0.0f;

        for (int channel = 0; channel < channels; ++channel)
        {
            auto& state = channelStates_[static_cast<size_t> (channel)];
            float* data = buffer.getWritePointer (channel);
            const float input = std::isfinite (data[sample]) ? data[sample] : 0.0f;
            state.originalRing[static_cast<size_t> (inputPosition_)] = input;
            state.inputRing[static_cast<size_t> (inputPosition_)] =
                original ? originalDrive (input, current_.drive) : state.drive.process (input, current_.drive);
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
