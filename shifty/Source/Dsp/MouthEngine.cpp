#include "MouthEngine.h"
#include "TalkEngine.h"

#include <cmath>

namespace shifty
{

namespace
{
int nextPowerOfTwo (int value) noexcept
{
    int size = 1;
    while (size < value)
        size <<= 1;
    return size;
}

constexpr float yinThreshold = 0.2f;
constexpr float confidentTracking = 0.5f;
} // namespace

int MouthEngine::latencyFor (double sampleRate) noexcept
{
    const double rate = (std::isfinite (sampleRate) && sampleRate > 1000.0) ? sampleRate : 48000.0;
    return std::max (64, static_cast<int> (std::lround (rate * 1024.0 / 48000.0)));
}

void MouthEngine::prepare (double sampleRate, int maxBlockSize, int channels)
{
    juce::ignoreUnused (maxBlockSize);
    sampleRate_ = (std::isfinite (sampleRate) && sampleRate > 1000.0) ? sampleRate : 48000.0;
    latency_ = latencyFor (sampleRate_);
    // A grain at +12 st reads twice its half-length ahead of its mark and is
    // written up to one half-length behind it: latency >= 3 half-lengths.
    maximumHalfLength_ = (latency_ - 6) / 3.0;
    ringSize_ = nextPowerOfTwo (4 * latency_ + 256);
    ringMask_ = ringSize_ - 1;
    smoothing_ = static_cast<float> (1.0 - std::exp (-1.0 / (0.02 * sampleRate_)));
    powerCoefficient_ = static_cast<float> (1.0 - std::exp (-1.0 / (0.3 * sampleRate_)));
    autoGainCoefficient_ = static_cast<float> (1.0 - std::exp (-1.0 / (0.15 * sampleRate_)));

    channels_.resize (static_cast<size_t> (std::max (1, channels)));
    for (auto& channel : channels_)
    {
        channel.input.assign (static_cast<size_t> (ringSize_), 0.0f);
        channel.original.assign (static_cast<size_t> (ringSize_), 0.0f);
        channel.output.assign (static_cast<size_t> (ringSize_), 0.0f);
    }
    weight_.assign (static_cast<size_t> (ringSize_), 0.0f);
    shiftRing_.assign (static_cast<size_t> (ringSize_), 0.0f);

    decimation_ = std::max (1, static_cast<int> (std::lround (sampleRate_ / 12000.0)));
    decimatedRate_ = sampleRate_ / decimation_;
    yinWindow_ = static_cast<int> (std::lround (0.024 * decimatedRate_));
    minimumLag_ = std::max (2, static_cast<int> (std::floor (decimatedRate_ / 650.0)));
    maximumLag_ = static_cast<int> (std::ceil (decimatedRate_ / 32.0));
    trackerRing_.assign (static_cast<size_t> (nextPowerOfTwo (yinWindow_ + maximumLag_ + 2)), 0.0f);
    yinFrame_.assign (static_cast<size_t> (yinWindow_ + maximumLag_ + 2), 0.0f);
    yinDifference_.assign (static_cast<size_t> (maximumLag_ + 2), 0.0f);
    trackerHop_ = std::max (16, static_cast<int> (std::lround (0.0053 * decimatedRate_)));
    holdSamples_ = static_cast<int> (0.06 * sampleRate_);
    for (auto& section : trackerLowpass_)
        section.setCutoff (static_cast<float> (std::min (1800.0, 0.2 * decimatedRate_)), sampleRate_);
    reset();
}

void MouthEngine::reset()
{
    for (auto& channel : channels_)
    {
        std::fill (channel.input.begin(), channel.input.end(), 0.0f);
        std::fill (channel.original.begin(), channel.original.end(), 0.0f);
        std::fill (channel.output.begin(), channel.output.end(), 0.0f);
        channel.drySplit.reset();
        channel.wetSplit.reset();
        channel.drive.reset();
        channel.mouthState.fill (0.0f);
        channel.dryPower = channel.wetPower = 0.0f;
    }
    mouthCountdown_ = 0;
    std::fill (weight_.begin(), weight_.end(), 0.0f);
    std::fill (shiftRing_.begin(), shiftRing_.end(), 0.0f);
    std::fill (trackerRing_.begin(), trackerRing_.end(), 0.0f);
    for (auto& section : trackerLowpass_)
        section.reset();
    now_ = 0;
    nextMark_ = 0.0;
    decimationPhase_ = 0;
    trackerWrite_ = 0;
    trackerFilled_ = 0;
    trackerCountdown_ = trackerHop_;
    period_ = sampleRate_ / 110.0;
    periodValid_ = false;
    confidence_ = 0.0f;
    samplesSinceConfident_ = 1 << 30;
    autoGain_ = 1.0f;
    lastSubCut_ = -1.0f;
    current_ = target_;
}

void MouthEngine::setParameters (const EngineParams& params) noexcept
{
    target_.shift = clampFinite (params.shiftSemitones, -12.0f, 12.0f, 0.0f);
    target_.focus = clampFinite (params.focus, 0.0f, 1.0f, 0.5f);
    target_.bite = clampFinite (params.bite, 0.0f, 1.0f, 0.0f);
    target_.drive = clampFinite (params.drive, 0.0f, 1.0f, 0.0f);
    target_.mix = clampFinite (params.mix, 0.0f, 1.0f, 1.0f);
    target_.subCut = clampFinite (params.subCutHz, 55.0f, 240.0f, 120.0f);
    target_.subSafe = params.subSafe ? 1.0f : 0.0f;
    target_.bypass = params.bypass ? 1.0f : 0.0f;
    target_.character = clampFinite (params.character, 0.0f, 1.0f, 0.0f);
    target_.morph = clampFinite (params.vowelMorph, 0.0f, 4.0f, 2.0f);
    voiceSet_ = juce::jlimit (0, 2, params.voiceSet);
}

void MouthEngine::updateMouthShape() noexcept
{
    // Band-pass resonators at the vowel's first three formants (bandwidth x2.5
    // so they shape a bass rather than whistle), added to the input.
    const auto vowel = interpolateVowel (voiceSet_, current_.morph);
    const double highest = 0.42 * sampleRate_;
    for (size_t k = 0; k < 3; ++k)
    {
        const double f = std::min (highest, static_cast<double> (vowel.frequency[k]));
        const float g = static_cast<float> (std::tan (juce::MathConstants<double>::pi * f / sampleRate_));
        const float damping = juce::jlimit (0.05f, 1.4f, 2.5f * vowel.bandwidth[k] / static_cast<float> (f));
        mouthK_[k] = damping;
        mouthA1_[k] = 1.0f / (1.0f + g * (g + damping));
        mouthA2_[k] = g * mouthA1_[k];
        mouthA3_[k] = g * mouthA2_[k];
        mouthGain_[k] = juce::Decibels::decibelsToGain (vowel.decibels[k]);
    }
}

float MouthEngine::readInput (const std::vector<float>& ring, double time) const noexcept
{
    // Catmull-Rom interpolation at an absolute input time.
    const double floorTime = std::floor (time);
    const auto index = static_cast<std::int64_t> (floorTime);
    const float t = static_cast<float> (time - floorTime);
    const float y0 = ring[static_cast<size_t> ((index - 1) & ringMask_)];
    const float y1 = ring[static_cast<size_t> (index & ringMask_)];
    const float y2 = ring[static_cast<size_t> ((index + 1) & ringMask_)];
    const float y3 = ring[static_cast<size_t> ((index + 2) & ringMask_)];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void MouthEngine::synthesiseGrain (double mark, double halfLength, double ratio, double window,
                                   int channels) noexcept
{
    // Output sample at offset u from the mark reads the input at mark + ratio * u.
    const auto first = static_cast<std::int64_t> (std::ceil (mark - window + 1.0e-9));
    const auto last = static_cast<std::int64_t> (std::floor (mark + window - 1.0e-9));
    juce::ignoreUnused (halfLength);
    for (std::int64_t t = first; t <= last; ++t)
    {
        const double u = static_cast<double> (t) - mark;
        const float w = static_cast<float> (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * u / window));
        const double source = mark + ratio * u;
        const auto index = static_cast<size_t> (t & ringMask_);
        weight_[index] += w;
        for (int c = 0; c < channels; ++c)
        {
            auto& state = channels_[static_cast<size_t> (c)];
            state.output[index] += w * readInput (state.input, source);
        }
    }
}

void MouthEngine::track() noexcept
{
    const int length = yinWindow_ + maximumLag_;
    const int ringMask = static_cast<int> (trackerRing_.size()) - 1;
    double energy = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const float x = trackerRing_[static_cast<size_t> ((trackerWrite_ - length + i) & ringMask)];
        yinFrame_[static_cast<size_t> (i)] = x;
        energy += static_cast<double> (x) * x;
    }
    if (energy / length < 1.0e-9)
    {
        confidence_ *= 0.5f;
        return;
    }

    // YIN difference function and cumulative-mean normalisation.
    float runningSum = 0.0f;
    yinDifference_[0] = 1.0f;
    for (int lag = 1; lag <= maximumLag_; ++lag)
    {
        float sum = 0.0f;
        for (int i = 0; i < yinWindow_; ++i)
        {
            const float difference = yinFrame_[static_cast<size_t> (i)] - yinFrame_[static_cast<size_t> (i + lag)];
            sum += difference * difference;
        }
        runningSum += sum;
        yinDifference_[static_cast<size_t> (lag)] = runningSum > 0.0f ? sum * static_cast<float> (lag) / runningSum : 1.0f;
    }

    int best = -1;
    for (int lag = minimumLag_; lag <= maximumLag_; ++lag)
    {
        if (yinDifference_[static_cast<size_t> (lag)] < yinThreshold)
        {
            while (lag + 1 <= maximumLag_ && yinDifference_[static_cast<size_t> (lag + 1)] < yinDifference_[static_cast<size_t> (lag)])
                ++lag;
            best = lag;
            break;
        }
    }
    if (best < 0)
    {
        best = minimumLag_;
        for (int lag = minimumLag_ + 1; lag <= maximumLag_; ++lag)
            if (yinDifference_[static_cast<size_t> (lag)] < yinDifference_[static_cast<size_t> (best)])
                best = lag;
    }
    double refined = best;
    if (best > 1 && best < maximumLag_)
    {
        const float a = yinDifference_[static_cast<size_t> (best - 1)];
        const float b = yinDifference_[static_cast<size_t> (best)];
        const float c = yinDifference_[static_cast<size_t> (best + 1)];
        const float denominator = a - 2.0f * b + c;
        if (std::abs (denominator) > 1.0e-9f)
            refined += juce::jlimit (-0.5, 0.5, 0.5 * static_cast<double> (a - c) / denominator);
    }
    const float confidence = juce::jlimit (0.0f, 1.0f, 1.0f - yinDifference_[static_cast<size_t> (best)]);
    confidence_ = 0.6f * confidence + 0.4f * confidence_;
    if (confidence >= confidentTracking)
    {
        const double measured = refined * decimation_;
        if (! periodValid_ || std::abs (std::log (measured / period_)) > 0.05)
            period_ = measured;
        else
            period_ = 0.5 * period_ + 0.5 * measured;
        periodValid_ = true;
        samplesSinceConfident_ = 0;
    }
}

void MouthEngine::processBlock (juce::AudioBuffer<float>& buffer, const Modulation& modulation)
{
    const int samples = buffer.getNumSamples();
    const int channels = std::min (buffer.getNumChannels(), static_cast<int> (channels_.size()));
    if (samples == 0 || channels == 0 || ringSize_ == 0)
        return;
    juce::ScopedNoDenormals noDenormals;
    const int trackerMask = static_cast<int> (trackerRing_.size()) - 1;

    for (int n = 0; n < samples; ++n)
    {
        // One-pole smoothing that lands exactly on its target. In float a one-pole
        // stalls a few hundred ulps short (the step rounds to nothing), which left a
        // finished bypass 3e-5 short of the dry signal; once a step no longer
        // moves the value, it takes the target.
        auto smooth = [this] (float& value, float destination) noexcept
        {
            const float next = value + smoothing_ * (destination - value);
            value = juce::exactlyEqual (next, value) ? destination : next;
        };
        smooth (current_.shift, target_.shift);
        smooth (current_.focus, target_.focus);
        smooth (current_.bite, target_.bite);
        smooth (current_.drive, target_.drive);
        smooth (current_.mix, target_.mix);
        smooth (current_.subCut, target_.subCut);
        smooth (current_.subSafe, target_.subSafe);
        smooth (current_.bypass, target_.bypass);
        smooth (current_.character, target_.character);
        smooth (current_.morph, target_.morph);
        if (--mouthCountdown_ <= 0)
        {
            updateMouthShape();
            mouthCountdown_ = 32;
        }
        const float mouthAmount = 2.0f * current_.character;
        if (std::abs (current_.subCut - lastSubCut_) > 0.01f)
        {
            for (auto& channel : channels_)
            {
                channel.drySplit.setCutoff (current_.subCut, sampleRate_);
                channel.wetSplit.setCutoff (current_.subCut, sampleRate_);
            }
            lastSubCut_ = current_.subCut;
        }

        const auto writeIndex = static_cast<size_t> (now_ & ringMask_);
        const float shiftOffset = modulation.shift != nullptr && std::isfinite (modulation.shift[n]) ? modulation.shift[n] : 0.0f;
        shiftRing_[writeIndex] = juce::jlimit (-12.0f, 12.0f, current_.shift + shiftOffset);

        float mono = 0.0f;
        for (int c = 0; c < channels; ++c)
        {
            auto& state = channels_[static_cast<size_t> (c)];
            const float* data = buffer.getReadPointer (c);
            const float original = std::isfinite (data[n]) ? data[n] : 0.0f;
            float driven = state.drive.process (original, current_.drive);
            if (mouthAmount > 0.0001f)
            {
                float resonance = 0.0f;
                for (size_t k = 0; k < 3; ++k)
                {
                    float& ic1 = state.mouthState[2 * k];
                    float& ic2 = state.mouthState[2 * k + 1];
                    const float v3 = driven - ic2;
                    const float v1 = mouthA1_[k] * ic1 + mouthA2_[k] * v3;
                    const float v2 = ic2 + mouthA2_[k] * ic1 + mouthA3_[k] * v3;
                    ic1 = 2.0f * v1 - ic1;
                    ic2 = 2.0f * v2 - ic2;
                    resonance += mouthGain_[k] * mouthK_[k] * v1;
                }
                driven += mouthAmount * resonance;
            }
            state.input[writeIndex] = driven;
            state.original[writeIndex] = original;
            mono += driven;
        }
        mono /= static_cast<float> (channels);

        // Pitch tracker on a low-passed, decimated mono sum.
        float low, high;
        trackerLowpass_[0].process (mono, low, high);
        trackerLowpass_[1].process (low, low, high);
        if (++decimationPhase_ >= decimation_)
        {
            decimationPhase_ = 0;
            trackerRing_[static_cast<size_t> (trackerWrite_)] = low;
            trackerWrite_ = (trackerWrite_ + 1) & trackerMask;
            trackerFilled_ = std::min (trackerFilled_ + 1, 1 << 20);
            if (--trackerCountdown_ <= 0)
            {
                trackerCountdown_ = trackerHop_;
                if (trackerFilled_ >= yinWindow_ + maximumLag_)
                    track();
            }
        }
        if (samplesSinceConfident_ < (1 << 30))
            ++samplesSinceConfident_;

        // Grains: marks a whole fraction of the tracked period apart.
        const double longest = maximumHalfLength_ * (0.45 + 0.55 * current_.focus);
        const float buzz = juce::jlimit (0.0f, 0.9f, 0.85f * current_.bite);
        const double windowFraction = 1.0 - 0.65 * buzz;
        if (nextMark_ < static_cast<double> (now_ - latency_ / 2))
            nextMark_ = static_cast<double> (now_ - latency_ / 2); // recover after a long stall
        for (;;)
        {
            double halfLength = longest * 0.6;
            if (periodValid_ && samplesSinceConfident_ <= holdSamples_)
            {
                const double slots = std::ceil (period_ / longest);
                halfLength = period_ / slots;
            }
            const auto markIndex = static_cast<size_t> (static_cast<std::int64_t> (std::llround (nextMark_)) & ringMask_);
            const double ratio = std::exp2 (static_cast<double> (shiftRing_[markIndex]) / 12.0);
            if (nextMark_ + ratio * halfLength + 2.0 > static_cast<double> (now_))
                break;
            synthesiseGrain (nextMark_, halfLength, ratio, halfLength * windowFraction, channels);
            nextMark_ += halfLength;
        }

        // Emit the sample one latency behind the input.
        const std::int64_t outputTime = now_ - latency_;
        const auto outputIndex = static_cast<size_t> (outputTime & ringMask_);
        const float weight = std::max (weight_[outputIndex], static_cast<float> (windowFraction));
        weight_[outputIndex] = 0.0f;
        float dryPower = 0.0f, wetPower = 0.0f;
        for (int c = 0; c < channels; ++c)
        {
            auto& state = channels_[static_cast<size_t> (c)];
            float* data = buffer.getWritePointer (c);
            const float grains = state.output[outputIndex] / weight;
            state.output[outputIndex] = 0.0f;
            const float dryOriginal = outputTime >= 0 ? state.original[outputIndex] : 0.0f;
            float dryLow, dryHigh, wetLow, wetHigh;
            state.drySplit.process (dryOriginal, dryLow, dryHigh);
            state.wetSplit.process (grains, wetLow, wetHigh);
            juce::ignoreUnused (wetLow);
            const float subWeight = current_.subSafe;
            const float wetBand = wetHigh * subWeight + grains * (1.0f - subWeight);
            const float dryBand = dryHigh * subWeight + dryOriginal * (1.0f - subWeight);
            const float wet = dryLow * subWeight + wetBand * autoGain_;
            const float dry = (dryLow + dryHigh) * subWeight + dryOriginal * (1.0f - subWeight);
            float out = dry + current_.mix * (wet - dry);
            out += current_.bypass * (dryOriginal - out);
            data[n] = std::isfinite (out) ? out : dryOriginal;
            state.dryPower += powerCoefficient_ * (dryBand * dryBand - state.dryPower);
            state.wetPower += powerCoefficient_ * (wetBand * wetBand - state.wetPower);
            dryPower += state.dryPower;
            wetPower += state.wetPower;
        }
        if (dryPower > 1.0e-10f && wetPower > 1.0e-12f)
            autoGain_ += autoGainCoefficient_ * (juce::jlimit (0.35f, 2.8f, std::sqrt (dryPower / wetPower)) - autoGain_);
        ++now_;
    }
}

} // namespace shifty
