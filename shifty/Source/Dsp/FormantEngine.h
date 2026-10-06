#pragma once

#include "DspCommon.h"

#include <atomic>
#include <complex>
#include <memory>
#include <vector>

namespace shifty
{

/**
    CLASSIC: streaming formant shifter. It estimates and relocates the source's
    spectral envelope while preserving FFT bin frequencies and phases. An
    optional vowel colour layer is disabled by default. Formant level
    compensation is linked across channels; protected sub bins are restored from
    the original signal after the spectral processing.

    Two modes:
    - original042: the 0.4.2 sound, kept for projects and presets made with it
      (slow level follower, original DRIVE, no envelope smoothing). Motion is
      applied by the caller through setParameters(), as 0.4.2 did.
    - latest: frame-matched level compensation, ADAA DRIVE, inter-frame envelope
      smoothing, per-sample Motion offsets read at each frame's centre, and an
      optional low-latency resolution (half the frame, zero-padded analysis).

    configure() and prepare() run off the audio thread. setParameters() may be
    called from another thread; processBlock() allocates no memory.
*/
class FormantEngine
{
public:
    enum class Mode { original042, latest };

    void configure (Mode mode, bool lowLatency) noexcept;
    void prepare (double sampleRate, int maxBlockSize, int channels);
    void reset();
    void setParameters (const EngineParams&) noexcept;
    void processBlock (juce::AudioBuffer<float>&, const float* shiftOffsets = nullptr);
    int getLatencySamples() const noexcept { return fftSize_; }
    static int latencyFor (double sampleRate, bool lowLatency) noexcept;

    /** Detected and relocated envelope of channel 0 at the shared display
        points, in dB relative to the detected envelope's peak. Written on the
        audio thread at each frame; read it from the same thread. */
    const std::array<float, displayPoints>& displayDetected() const noexcept { return displayDetected_; }
    const std::array<float, displayPoints>& displayMoved() const noexcept { return displayMoved_; }
    bool hasDisplay() const noexcept { return displayValid_; }

private:
    using Complex = juce::dsp::Complex<float>;

    struct ChannelState
    {
        std::vector<float> inputRing;
        std::vector<float> originalRing;
        std::vector<float> dryDelay;
        std::vector<float> outputRing;
        std::vector<float> logEnvelope;      // analysis bins
        std::vector<float> smoothedEnvelope; // analysis bins, latest mode
        std::vector<float> envelopeTarget;   // analysis bins
        std::vector<float> logGains;         // synthesis bins
        std::vector<float> gainScratch;      // synthesis bins
        std::vector<int> peakDistances;
        bool frameActive = false;
        bool envelopeSeeded = false;
        AdaaDrive drive;
        // juce::dsp::FFT work areas: analysis buffers hold analysisSize_
        // complex values, the synthesis buffer fftSize_.
        std::vector<Complex> analysis;
        std::vector<Complex> originalAnalysis;
        std::vector<Complex> cepstrum;
        std::vector<Complex> spectrum;
        std::vector<Complex> originalSpectrum;
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

    void analyseReal (std::vector<Complex>& data) const noexcept;
    void synthesiseReal (std::vector<Complex>& data) const noexcept;
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
    void updateDisplay (const ChannelState&) noexcept;
    float envelopeAt (const std::vector<float>& envelope, float analysisBin) const noexcept;

    Mode mode_ = Mode::latest;
    bool lowLatency_ = false;
    std::unique_ptr<juce::dsp::FFT> analysisFft_;
    std::unique_ptr<juce::dsp::FFT> synthesisFft_;
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
    float originalAttackCoefficient_ = 1.0f;
    float originalReleaseCoefficient_ = 1.0f;
    float envelopeSmoothingCoefficient_ = 1.0f;
    bool compensationSeeded_ = false;
    float frameShift_ = 0.0f;
    float motionSmoothing_ = 0.0f;
    int fftSize_ = 0;      // synthesis frame and latency
    int analysisSize_ = 0; // fftSize_ * zero-padding factor
    int padding_ = 1;
    int hopSize_ = 0;
    int outputRingSize_ = 0;
    int inputPosition_ = 0;
    int outputPosition_ = 0;
    int samplesUntilFrame_ = 0;
    std::array<float, displayPoints> displayDetected_ {};
    std::array<float, displayPoints> displayMoved_ {};
    bool displayValid_ = false;
};

} // namespace shifty
