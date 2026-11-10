#include "Finisher.h"

#include <cmath>

namespace shifty
{

namespace
{
// Per band: low, mid, high. Levels in dB relative to the program reference.
constexpr float downOffset[] { -1.0f, -1.0f, -8.0f };
constexpr float upOffset[] { 0.0f, -6.0f, -14.0f };
constexpr float downSlope[] { 1.0f - 1.0f / 3.0f, 1.0f - 1.0f / 20.0f, 1.0f - 1.0f / 20.0f };
constexpr float upSlope = 1.0f - 1.0f / 5.0f;
constexpr float maxUp[] { 0.0f, 18.0f, 22.0f };
constexpr float makeupDb[] { 0.5f, 1.5f, 3.0f };
constexpr float kneeDb = 9.0f;
constexpr float floorOffset = -60.0f; // upward gain fades out below this (full above floor + 12 dB)
constexpr float detectorMs[] { 15.0f, 8.0f, 5.0f };
constexpr float attackBaseMs[] { 20.0f, 10.0f, 8.0f };
constexpr float releaseBaseMs[] { 250.0f, 220.0f, 130.0f };
constexpr float highSplitHz = 2500.0f;
constexpr float lowSplitHz = 90.0f;      // with SUB SAFE off
constexpr float gateMeanSquare = 3.2e-6f; // about -55 dBFS RMS
constexpr float saturationHeadroom = 2.0f; // soft gain-stage saturation 6 dB over the program RMS
constexpr float loudnessTargetDb = 2.5f;   // per unit of AMOUNT, so a little louder as it smashes harder,
constexpr float loudnessTargetMaxDb = 1.6f; // but never more than this over the input
constexpr float ceilingKnee = 0.85f, ceilingRange = 0.13f; // output never above 0.98

inline float knee (float x) noexcept
{
    if (x <= -0.5f * kneeDb) return 0.0f;
    if (x >= 0.5f * kneeDb) return x;
    const float t = x + 0.5f * kneeDb;
    return t * t / (2.0f * kneeDb);
}

inline float toDb (float meanSquare) noexcept { return 10.0f * std::log10 (meanSquare + 1.0e-12f); }

inline float coefficient (double seconds, double rate) noexcept
{
    return static_cast<float> (1.0 - std::exp (-1.0 / (std::max (1.0e-4, seconds) * rate)));
}

/** tanh, as a cheap rational fit (within 2.5% up to |x| 3, then exactly +-1). */
inline float softSaturate (float x) noexcept
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float ceiling (float y) noexcept
{
    const float a = std::abs (y);
    if (a <= ceilingKnee) return y;
    const float limited = ceilingKnee + ceilingRange * std::tanh ((a - ceilingKnee) / ceilingRange);
    return y < 0.0f ? -limited : limited;
}
} // namespace

void Finisher::prepare (double sampleRate, int channels)
{
    sampleRate_ = (std::isfinite (sampleRate) && sampleRate > 1000.0) ? sampleRate : 48000.0;
    channels_.resize (static_cast<size_t> (std::max (1, channels)));
    smoothing_ = coefficient (0.02, sampleRate_);
    engageStep_ = static_cast<float> (1.0 / (0.02 * sampleRate_));
    for (size_t b = 0; b < 3; ++b)
    {
        detectorCoefficient_[b] = coefficient (detectorMs[b] * 0.001, sampleRate_);
        attackMs_[b] = attackBaseMs[b];
        releaseMs_[b] = releaseBaseMs[b];
    }
    fastCoefficient_ = coefficient (0.05, sampleRate_);
    referenceCoefficient_ = coefficient (1.0, sampleRate_);
    memoryStep_ = coefficient (1.5, sampleRate_ / controlInterval);
    loudCoefficient_ = coefficient (2.0, sampleRate_);
    loudStartCoefficient_ = coefficient (0.3, sampleRate_);
    for (auto& c : channels_)
    {
        c.highSplit.setCutoff (highSplitHz, sampleRate_);
        c.lowAllpass.setCutoff (highSplitHz, sampleRate_);
    }
    reset();
}

void Finisher::reset() noexcept
{
    for (auto& c : channels_)
    {
        c.lowSplit.reset();
        c.highSplit.reset();
        c.lowAllpass.reset();
    }
    for (auto& b : bands_)
        b = {};
    amount_ = target_.amount;
    time_ = target_.time;
    balance_ = target_.balance;
    split_ = target_.subSafe ? juce::jlimit (40.0f, 400.0f, target_.subCut) : lowSplitHz;
    lastSplit_ = -1.0f;
    engaged_ = 0.0f;
    fastLevel_ = reference_ = 0.0f;
    referenceSet_ = false;
    loudIn_ = loudOut_ = trimDb_ = 0.0f;
    samplesSinceStart_ = 0;
    controlCountdown_ = 0;
    gainNow_.fill (1.0f);
    gainStep_.fill (0.0f);
    trimNow_ = 1.0f;
    trimStep_ = 0.0f;
    headroom_ = saturationHeadroom * std::sqrt (gateMeanSquare);
    meter_ = {};
}

void Finisher::setParameters (const Params& params) noexcept
{
    target_.amount = clampFinite (params.amount, 0.0f, 1.0f, 0.0f);
    target_.time = clampFinite (params.time, 0.0f, 1.0f, 0.5f);
    target_.balance = clampFinite (params.balance, -1.0f, 1.0f, 0.0f);
    target_.subSafe = params.subSafe;
    target_.subCut = clampFinite (params.subCut, 40.0f, 400.0f, 120.0f);
    target_.bypass = params.bypass;
}

void Finisher::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int samples = buffer.getNumSamples();
    const int channels = std::min ({ buffer.getNumChannels(), static_cast<int> (channels_.size()), 2 });
    if (samples == 0 || channels == 0)
        return;
    const bool wanted = target_.amount > 0.0005f && ! target_.bypass;
    const float splitTarget = target_.subSafe ? target_.subCut : lowSplitHz;
    const float inverseChannels = 1.0f / static_cast<float> (channels);
    const double rate = sampleRate_;
    const bool lowProtected = target_.subSafe;
    // Switched off: keep the crossovers, detectors and the program reference running, so SMASH comes in
    // settled, but leave the buffer alone and skip the gain computer.
    const bool idle = ! wanted && engaged_ <= 0.0f;

    std::array<std::array<float, 3>, 2> band {};
    std::array<float, 2> input {};
    for (int n = 0; n < samples; ++n)
    {
        amount_ += smoothing_ * (target_.amount - amount_);
        time_ += smoothing_ * (target_.time - time_);
        balance_ += smoothing_ * (target_.balance - balance_);
        split_ += smoothing_ * (splitTarget - split_);
        if (std::abs (split_ - lastSplit_) > 0.01f)
        {
            for (auto& c : channels_)
                c.lowSplit.setCutoff (split_, rate);
            lastSplit_ = split_;
        }
        engaged_ = wanted ? std::min (1.0f, engaged_ + engageStep_) : std::max (0.0f, engaged_ - engageStep_);

        // Linkwitz-Riley split: the bands sum to an allpass of the input. The low
        // band is all-passed at the upper crossover to stay in phase.
        float inputSquare = 0.0f;
        std::array<float, 3> bandSquare {};
        for (int c = 0; c < channels; ++c)
        {
            auto& state = channels_[static_cast<size_t> (c)];
            const float x = buffer.getSample (c, n);
            const float clean = std::isfinite (x) ? x : 0.0f;
            float low, rest, mid, high, lowLow, lowHigh;
            state.lowSplit.process (clean, low, rest);
            state.highSplit.process (rest, mid, high);
            state.lowAllpass.process (low, lowLow, lowHigh);
            low = lowLow + lowHigh;
            input[static_cast<size_t> (c)] = clean;
            band[static_cast<size_t> (c)] = { low, mid, high };
            inputSquare += clean * clean;
            bandSquare[0] += low * low;
            bandSquare[1] += mid * mid;
            bandSquare[2] += high * high;
        }
        inputSquare *= inverseChannels;
        for (size_t b = 0; b < 3; ++b)
            bands_[b].detector += detectorCoefficient_[b] * (bandSquare[b] * inverseChannels - bands_[b].detector);

        // Program reference: a slow, gated RMS of the input.
        fastLevel_ += fastCoefficient_ * (inputSquare - fastLevel_);
        const bool gateOpen = fastLevel_ > gateMeanSquare;
        if (gateOpen)
        {
            if (! referenceSet_)
            {
                reference_ = fastLevel_;
                referenceSet_ = true;
            }
            reference_ += referenceCoefficient_ * (inputSquare - reference_);
        }
        if (idle)
            continue;

        // The gain computer runs every controlInterval samples; the gains glide linearly in between.
        if (--controlCountdown_ <= 0)
        {
            controlCountdown_ = controlInterval;
            const float referenceDb = referenceSet_ ? toDb (std::max (reference_, gateMeanSquare)) : -60.0f;
            const float factor = timeFactor (time_);
            const float downScale = balance_ > 0.0f ? 1.0f - balance_ : 1.0f;
            const float upScale = balance_ < 0.0f ? 1.0f + balance_ : 1.0f;
            for (size_t b = 0; b < 3; ++b)
            {
                auto& state = bands_[b];
                const float level = toDb (state.detector);
                float targetDb = 0.0f;
                const bool protectedBand = b == 0 && lowProtected;
                if (! protectedBand)
                {
                    const float down = -downSlope[b] * knee (level - (referenceDb + downOffset[b]));
                    float up = 0.0f;
                    if (maxUp[b] > 0.0f)
                    {
                        up = std::min (maxUp[b], upSlope * knee ((referenceDb + upOffset[b]) - level));
                        up *= juce::jlimit (0.0f, 1.0f, (level - (referenceDb + floorOffset)) / 12.0f);
                    }
                    targetDb = down * downScale + up * upScale;
                    state.memory += memoryStep_ * (juce::jlimit (0.0f, 1.0f, -down / 12.0f) - state.memory);
                }
                // Opto-like: the release slows after heavy downward compression.
                const float ms = targetDb < state.gainDb ? attackMs_[b] : releaseMs_[b] * (1.0f + 0.8f * state.memory);
                const float k = static_cast<float> (1.0 - std::exp (-1000.0 * controlInterval
                                                                    / (std::max (0.1, static_cast<double> (ms * factor)) * rate)));
                state.gainDb += k * (targetDb - state.gainDb);
                const float gain = juce::Decibels::decibelsToGain (state.gainDb + (protectedBand ? 0.0f : makeupDb[b]));
                gainStep_[b] = (gain - gainNow_[b]) / static_cast<float> (controlInterval);
                meter_.gainDb[b] = state.gainDb;
                meter_.levelDb[b] = level - referenceDb;
            }
            headroom_ = saturationHeadroom * std::sqrt (std::max (reference_, gateMeanSquare));
            const float trim = juce::Decibels::decibelsToGain (trimDb_);
            trimStep_ = (trim - trimNow_) / static_cast<float> (controlInterval);
        }
        for (size_t b = 0; b < 3; ++b)
            gainNow_[b] += gainStep_[b];
        trimNow_ += trimStep_;

        // Mix, trim and ceiling. The loudness trim acts on the bands SMASH works
        // on, so a protected sub is never raised or lowered by it.
        float dryWorked = 0.0f, wetWorked = 0.0f;
        std::array<float, 2> kept {}, worked {};
        for (int c = 0; c < channels; ++c)
        {
            const auto& b = band[static_cast<size_t> (c)];
            float keep = 0.0f, dry = 0.0f, mixed = 0.0f;
            for (size_t k = 0; k < 3; ++k)
            {
                if (k == 0 && lowProtected)
                {
                    keep += b[k];
                    continue;
                }
                float wet = b[k] * gainNow_[k];
                if (k > 0)
                    wet = headroom_ * softSaturate (wet / headroom_);
                dry += b[k];
                mixed += b[k] + amount_ * (wet - b[k]);
            }
            kept[static_cast<size_t> (c)] = keep;
            worked[static_cast<size_t> (c)] = mixed;
            dryWorked += dry * dry;
            wetWorked += mixed * mixed;
        }
        dryWorked *= inverseChannels;
        wetWorked *= inverseChannels;
        if (gateOpen)
        {
            if (samplesSinceStart_ == 0)
            {
                loudIn_ = std::max (dryWorked, 1.0e-9f);
                loudOut_ = std::max (wetWorked, 1.0e-9f);
            }
            const float k = samplesSinceStart_ < static_cast<int> (1.5 * rate) ? loudStartCoefficient_ : loudCoefficient_;
            loudIn_ += k * (dryWorked - loudIn_);
            loudOut_ += k * (wetWorked - loudOut_);
            ++samplesSinceStart_;
            if (controlCountdown_ == controlInterval)
                trimDb_ = juce::jlimit (-12.0f, 9.0f, toDb (loudIn_) - toDb (loudOut_) + std::min (loudnessTargetMaxDb, loudnessTargetDb * amount_));
        }
        for (int c = 0; c < channels; ++c)
        {
            const float x = input[static_cast<size_t> (c)];
            const float y = kept[static_cast<size_t> (c)] + worked[static_cast<size_t> (c)] * trimNow_;
            // The ceiling also covers the 20 ms switch-in, when the dry input is still in the blend.
            buffer.setSample (c, n, ceiling (x + engaged_ * (y - x)));
        }
    }
    meter_.engaged = engaged_;
    if (idle)
        meter_.gainDb = {};
}

} // namespace shifty
