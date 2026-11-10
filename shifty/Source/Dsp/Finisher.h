#pragma once

#include "DspCommon.h"

#include <array>
#include <vector>

namespace shifty
{

/**
    SMASH: the finisher at the very end of the chain, before OUTPUT. A three-band
    upward and downward compressor in the spirit of the classic multiband "OTT"
    setting, with analog manners.

    - Bands: Linkwitz-Riley 4 crossovers at 2.5 kHz and, with SUB SAFE on, at the
      SUB SAFE frequency (90 Hz with it off). The low band of the 2.5 kHz split is
      all-passed so the three bands sum flat.
    - Each band is compressed downward above its threshold (8:1, soft knee) and
      upward below a lower threshold (3.5:1, at most +16 to +18 dB), so loud parts
      come down and quiet parts, tails and fizz come up: density.
    - Thresholds follow the program: they sit relative to a slow (1 s), gated
      RMS of the input, so SMASH behaves the same on a quiet or a hot track and
      keeps the level close to the input. A gentle loudness trim (2 s) holds the
      long-term level within about 1 dB of the input.
    - Analog manners: RMS detection, a soft knee, a release that slows after
      heavy compression (opto-like), and a soft saturating gain stage on the mid
      and high bands. A soft ceiling keeps the output below 0 dBFS.
    - The sub is protected: with SUB SAFE on, the band below the SUB SAFE
      frequency passes untouched. With it off, the low band only gets gentle
      downward compression, never upward, so it can't pump or swell.
    - AMOUNT is a parallel (dry/wet) blend per band; TIME scales attack and
      release (0.25x to 4x); BALANCE trades the downward part (left) against the
      upward part (right).

    At AMOUNT 0 the buffer is not touched at all. Zero latency.
    prepare() off the audio thread; process() allocates nothing.
*/
class Finisher
{
public:
    struct Params
    {
        float amount = 0.0f;   // 0..1
        float time = 0.5f;     // 0..1, 0.5 = 1x
        float balance = 0.0f;  // -1 downward only .. +1 upward only
        bool subSafe = true;
        float subCut = 120.0f;
        bool bypass = false;
    };

    /** Live values for the screen (written each block). */
    struct Meter
    {
        std::array<float, 3> gainDb {};  // current gain per band (low, mid, high), dB
        std::array<float, 3> levelDb {}; // band level relative to the program reference, dB
        float engaged = 0.0f;
    };

    void prepare (double sampleRate, int channels);
    void reset() noexcept;
    void setParameters (const Params&) noexcept;
    void process (juce::AudioBuffer<float>&) noexcept;
    const Meter& meter() const noexcept { return meter_; }

    static float timeFactor (float time) noexcept { return std::pow (4.0f, 2.0f * time - 1.0f); }

private:
    struct ChannelState
    {
        LinkwitzRiley4 lowSplit, highSplit, lowAllpass;
    };

    struct Band
    {
        float detector = 0.0f;  // mean square, linked across channels
        float gainDb = 0.0f;    // smoothed gain
        float memory = 0.0f;    // opto memory, 0..1
    };

    std::vector<ChannelState> channels_;
    std::array<Band, 3> bands_ {};
    Params target_;
    double sampleRate_ = 48000.0;
    float amount_ = 0.0f, time_ = 0.5f, balance_ = 0.0f, split_ = 120.0f, lastSplit_ = -1.0f;
    float engaged_ = 0.0f, engageStep_ = 0.001f;
    float smoothing_ = 0.001f;
    float fastLevel_ = 0.0f, reference_ = 0.0f;
    bool referenceSet_ = false;
    float loudIn_ = 0.0f, loudOut_ = 0.0f, trimDb_ = 0.0f;
    int samplesSinceStart_ = 0;
    std::array<float, 3> detectorCoefficient_ {}, attackMs_ {}, releaseMs_ {};
    float fastCoefficient_ = 0.0f, referenceCoefficient_ = 0.0f, memoryStep_ = 0.0f;
    static constexpr int controlInterval = 8; // the gain computer's step, in samples
    int controlCountdown_ = 0;
    std::array<float, 3> gainNow_ {}, gainStep_ {};
    float trimNow_ = 1.0f, trimStep_ = 0.0f, headroom_ = 0.0f;
    float loudCoefficient_ = 0.0f, loudStartCoefficient_ = 0.0f;
    Meter meter_;
};

} // namespace shifty
