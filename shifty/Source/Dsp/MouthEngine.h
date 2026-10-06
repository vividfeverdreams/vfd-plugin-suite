#pragma once

#include "DspCommon.h"

#include <cstdint>
#include <vector>

namespace shifty
{

/**
    MOUTH: pitch-synchronous (PSOLA-style) formant shifting.

    A YIN pitch tracker follows the bass fundamental. Grains are cut at marks
    spaced by a whole fraction of the tracked period, time-scaled by the formant
    ratio and overlap-added at the same marks, so the output repeats exactly at
    the input's period: the notes keep their pitch while the waveform inside
    each period is squeezed (SHIFT up) or stretched (SHIFT down). Unlike CLASSIC
    this changes even a plain saw. When tracking confidence drops (noise, chords,
    note changes) the last good period is held briefly, then grains fall back to
    a short fixed spacing.

    FOCUS sets the longest grain (broad and smooth at 0, more detail at 1).
    BITE narrows each grain inside its slot for a pitch-synchronous buzz ("fry").
    CHARACTER adds a mouth shape before the grains: resonances at the first
    three formants of the selected vowel, which SHIFT then moves like the
    source's own. A plain saw has no formants of its own, so without CHARACTER,
    BITE or modulation MOUTH changes it very little.
    SUB SAFE keeps the original sub with a Linkwitz-Riley 4 split.

    Fixed latency: about 21.3 ms (1024 samples at 48 kHz), the longest grain
    reach at +12 semitones. setParameters() and processBlock() run on the audio
    thread; processBlock() allocates no memory.
*/
class MouthEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int channels);
    void reset();
    void setParameters (const EngineParams&) noexcept;
    void processBlock (juce::AudioBuffer<float>&, const Modulation& modulation);
    int getLatencySamples() const noexcept { return latency_; }
    static int latencyFor (double sampleRate) noexcept;

    /** Tracked fundamental in Hz (0 when nothing is tracked) and its confidence 0..1. */
    float trackedPitch() const noexcept { return periodValid_ ? static_cast<float> (sampleRate_ / period_) : 0.0f; }
    float trackingConfidence() const noexcept { return confidence_; }

private:
    struct ChannelState
    {
        std::vector<float> input;     // driven input, absolute-time ring
        std::vector<float> original;  // undriven input, for the dry path
        std::vector<float> output;    // grain accumulator, absolute-time ring
        LinkwitzRiley4 drySplit;
        LinkwitzRiley4 wetSplit;
        AdaaDrive drive;
        std::array<float, 6> mouthState {}; // three band-pass resonators, two states each
        float dryPower = 0.0f;
        float wetPower = 0.0f;
    };

    struct Smoothed
    {
        float shift = 0.0f, focus = 0.5f, bite = 0.0f, drive = 0.0f, mix = 1.0f;
        float subCut = 120.0f, subSafe = 1.0f, bypass = 0.0f;
        float character = 0.0f, morph = 2.0f;
    };
    void updateMouthShape() noexcept;
    int voiceSet_ = 0;
    int mouthCountdown_ = 0;
    std::array<float, 3> mouthA1_ {}, mouthA2_ {}, mouthA3_ {}, mouthK_ {}, mouthGain_ {};

    void track() noexcept;
    void synthesiseGrain (double mark, double halfLength, double ratio, double window, int channels) noexcept;
    float readInput (const std::vector<float>& ring, double time) const noexcept;

    std::vector<ChannelState> channels_;
    std::vector<float> weight_;     // shared grain-window accumulator
    std::vector<float> shiftRing_;  // SHIFT (base + modulation) per input sample
    Smoothed current_, target_;
    double sampleRate_ = 48000.0;
    int latency_ = 1024;
    int ringSize_ = 0;
    int ringMask_ = 0;
    std::int64_t now_ = 0;
    double nextMark_ = 0.0;
    double maximumHalfLength_ = 300.0;
    float smoothing_ = 1.0f;
    float powerCoefficient_ = 0.0f;
    float autoGainCoefficient_ = 0.0f;
    float autoGain_ = 1.0f;
    float lastSubCut_ = -1.0f;

    // Pitch tracker (decimated mono)
    int decimation_ = 4;
    int decimationPhase_ = 0;
    double decimatedRate_ = 12000.0;
    std::array<ButterworthSection, 2> trackerLowpass_;
    std::vector<float> trackerRing_;
    int trackerWrite_ = 0;
    int trackerFilled_ = 0;
    int trackerCountdown_ = 0;
    int trackerHop_ = 64;
    int yinWindow_ = 288;
    int minimumLag_ = 18;
    int maximumLag_ = 375;
    std::vector<float> yinFrame_;
    std::vector<float> yinDifference_;
    double period_ = 480.0;         // input samples
    bool periodValid_ = false;
    float confidence_ = 0.0f;
    int samplesSinceConfident_ = 1 << 30;
    int holdSamples_ = 2880;
};

} // namespace shifty
