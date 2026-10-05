#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <complex>
#include <memory>
#include <vector>

namespace shifty
{

struct EngineParams
{
    float shiftSemitones = 0.0f;
    float focus = 0.5f;
    float bite = 0.0f;
    float drive = 0.0f;
    float mix = 1.0f;
    float subCutHz = 120.0f;
    bool subSafe = true;
    bool vowel = false;
};

/**
    Streaming formant shifter for bass sounds. It estimates and relocates the
    source envelope while preserving FFT bin frequencies/phases. An optional
    vowel colour layer is disabled by default.
    Formant level compensation is linked across channels; protected sub bins
    are restored from the original signal after the spectral processing.

    prepare() and reset() are called off the audio thread. setParameters() may
    be called from another thread; processBlock() allocates no memory.

    processBlock() optionally takes one Motion offset per sample, in semitones.
    Offsets bypass the 20 ms parameter smoothing and are read at the centre of
    each analysis frame, so modulation stays aligned with the audio it shapes.
*/
class FormantEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int channels);
    void reset();
    void setParameters (const EngineParams&) noexcept;
    void processBlock (juce::AudioBuffer<float>&, const float* motionOffsets = nullptr);
    int getLatencySamples() const noexcept { return fftSize_; }

private:
    using Complex = juce::dsp::Complex<float>;

    struct ChannelState
    {
        std::vector<float> inputRing;
        std::vector<float> originalRing;
        std::vector<float> dryDelay;
        std::vector<float> outputRing;
        std::vector<float> logEnvelope;
        std::vector<float> envelopeTarget;
        std::vector<float> logGains;
        std::vector<float> gainScratch;
        std::vector<int> peakDistances;
        bool frameActive = false;
        // Each holds fftSize_ complex values (2 * fftSize_ floats), the work
        // area required by juce::dsp::FFT's in-place real transforms.
        std::vector<Complex> spectrum;
        std::vector<Complex> originalSpectrum;
        std::vector<Complex> cepstrum;
    };

    struct SmoothedParams
    {
        float shift = 0.0f;
        float focus = 0.5f;
        float bite = 0.0f;
        float drive = 0.0f;
        float mix = 1.0f;
        float subCut = 120.0f;
        float subSafe = 1.0f;
        float vowel = 0.0f;
    };

    void forwardReal (std::vector<Complex>& data) const noexcept;
    void inverseReal (std::vector<Complex>& data) const noexcept;
    void processFrame (int inputStart, int outputStart, int channels) noexcept;
    void analyseFrame (ChannelState&, int inputStart, bool needsFormant,
                       bool needsOriginalSpectrum, double& referenceEnergy,
                       double& processedEnergy) noexcept;
    void synthesiseFrame (ChannelState&, int outputStart, bool needsOriginalSpectrum) noexcept;
    void smoothMovingGains (ChannelState&) const noexcept;
    void computeGuide (float shift, std::vector<float>& destination) const noexcept;
    void computeLogGains (const ChannelState&, float shift, const std::vector<float>& guide,
                          std::vector<float>& destination) const noexcept;
    void estimateEnvelope (ChannelState&) noexcept;
    float envelopeAt (const ChannelState&, float bin) const noexcept;

    std::unique_ptr<juce::dsp::FFT> fft_;
    std::vector<ChannelState> channelStates_;
    std::vector<float> window_;
    std::vector<float> logFrequencies_;
    std::vector<float> spectralFade_;
    std::vector<float> guideDifference_;
    std::vector<float> originalWeights_;
    std::vector<float> normalizationWeights_;
    std::vector<float> motionRing_;
    std::vector<float> lifterTaper_;

    std::atomic<float> targetShift_ { 0.0f };
    std::atomic<float> targetFocus_ { 0.5f };
    std::atomic<float> targetBite_ { 0.0f };
    std::atomic<float> targetDrive_ { 0.0f };
    std::atomic<float> targetMix_ { 1.0f };
    std::atomic<float> targetSubCut_ { 120.0f };
    std::atomic<bool> targetSubSafe_ { true };
    std::atomic<bool> targetVowel_ { false };

    SmoothedParams current_;
    double sampleRate_ = 48000.0;
    float smoothingCoefficient_ = 1.0f;
    float levelCompensation_ = 0.0f;
    float levelSmoothingCoefficient_ = 1.0f;
    bool compensationSeeded_ = false;
    // SHIFT (base + Motion) at the current frame's centre, and how strongly
    // the frame's gains are smoothed because Motion is moving (0..1).
    float frameShift_ = 0.0f;
    float motionSmoothing_ = 0.0f;
    int fftSize_ = 0;
    int hopSize_ = 0;
    int outputRingSize_ = 0;
    int inputPosition_ = 0;
    int outputPosition_ = 0;
    int samplesUntilFrame_ = 0;
};

} // namespace shifty
