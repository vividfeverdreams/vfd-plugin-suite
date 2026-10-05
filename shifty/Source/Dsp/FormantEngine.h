#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <complex>
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
*/
class FormantEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int channels);
    void reset();
    void setParameters (const EngineParams&) noexcept;
    void processBlock (juce::AudioBuffer<float>&);
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
        std::vector<int> peakDistances;
        bool frameActive = false;
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

    void transform (std::vector<Complex>& data, bool inverse) const noexcept;
    void processFrame (int inputStart, int outputStart, int channels) noexcept;
    void analyseFrame (ChannelState&, int inputStart, bool needsFormant,
                       bool needsOriginalSpectrum, double& referenceEnergy,
                       double& processedEnergy) noexcept;
    void synthesiseFrame (ChannelState&, int outputStart, bool needsOriginalSpectrum) noexcept;
    void estimateEnvelope (ChannelState&) noexcept;
    float envelopeAt (const ChannelState&, float bin) const noexcept;

    std::vector<ChannelState> channelStates_;
    std::vector<int> bitReversal_;
    std::vector<Complex> twiddles_;
    std::vector<float> window_;
    std::vector<float> logFrequencies_;
    std::vector<float> spectralFade_;
    std::vector<float> guideDifference_;
    std::vector<float> originalWeights_;
    std::vector<float> normalizationWeights_;

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
    float levelAttackCoefficient_ = 1.0f;
    float levelReleaseCoefficient_ = 1.0f;
    int fftSize_ = 0;
    int hopSize_ = 0;
    int outputRingSize_ = 0;
    int inputPosition_ = 0;
    int outputPosition_ = 0;
    int samplesUntilFrame_ = 0;
};

} // namespace shifty
