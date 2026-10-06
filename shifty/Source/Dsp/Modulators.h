#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

namespace shifty
{

/** Peak envelope follower with separate attack and release, reported as 0..1
    over -48..-6 dBFS so the same setting behaves alike on quiet and loud parts. */
class EnvelopeFollower
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
        lastAttack_ = lastRelease_ = -1.0f;
        reset();
    }

    void reset() noexcept { level_ = 0.0f; }

    float process (float magnitude, float attackMs, float releaseMs) noexcept
    {
        if (! juce::exactlyEqual (attackMs, lastAttack_))
        {
            attack_ = coefficient (attackMs);
            lastAttack_ = attackMs;
        }
        if (! juce::exactlyEqual (releaseMs, lastRelease_))
        {
            release_ = coefficient (releaseMs);
            lastRelease_ = releaseMs;
        }
        const float x = std::isfinite (magnitude) ? std::abs (magnitude) : 0.0f;
        level_ += (x > level_ ? attack_ : release_) * (x - level_);
        const float decibels = 20.0f * std::log10 (level_ + 1.0e-9f);
        return juce::jlimit (0.0f, 1.0f, (decibels + 48.0f) / 42.0f);
    }

private:
    float coefficient (float milliseconds) const noexcept
    {
        return static_cast<float> (1.0 - std::exp (-1.0 / (std::max (0.05, static_cast<double> (milliseconds)) * 0.001 * sampleRate_)));
    }

    double sampleRate_ = 48000.0;
    float level_ = 0.0f;
    float attack_ = 1.0f, release_ = 1.0f;
    float lastAttack_ = -1.0f, lastRelease_ = -1.0f;
};

/** YOI: a note-onset detector that restarts an A -> B sweep. A fast envelope
    jumping 6 dB above a slow one (and above -46 dBFS) counts as an onset; a
    50 ms hold-off ignores the ripple of a single attack. The slow envelope
    releases in 60 ms, so after a 100 ms gap a note up to about 8 dB quieter
    than the last one still restarts the sweep. progress() runs from 0
    to 1 over the sweep time with an ease-out curve. */
class YoiSweep
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
        fastAttack_ = coefficient (0.5);
        fastRelease_ = coefficient (15.0);
        slowAttack_ = coefficient (30.0);
        slowRelease_ = coefficient (60.0); // a quieter note after a short gap still reads as an onset
        holdOff_ = static_cast<int> (0.05 * sampleRate_);
        reset();
    }

    void reset() noexcept
    {
        fast_ = slow_ = 0.0f;
        position_ = 1.0f;
        sinceTrigger_ = holdOff_;
        triggered_ = false;
    }

    /** Returns the eased sweep position 0..1 (1 = settled at B). */
    float process (float magnitude, float sweepMs) noexcept
    {
        const float x = std::isfinite (magnitude) ? std::abs (magnitude) : 0.0f;
        fast_ += (x > fast_ ? fastAttack_ : fastRelease_) * (x - fast_);
        slow_ += (x > slow_ ? slowAttack_ : slowRelease_) * (x - slow_);
        triggered_ = false;
        if (sinceTrigger_ < holdOff_)
            ++sinceTrigger_;
        else if (fast_ > 0.005f && fast_ > 2.0f * slow_ + 0.0005f)
        {
            position_ = 0.0f;
            sinceTrigger_ = 0;
            triggered_ = true;
        }
        if (position_ < 1.0f)
            position_ = std::min (1.0f, position_ + static_cast<float> (1000.0 / (std::max (5.0f, sweepMs) * sampleRate_)));
        const float remaining = 1.0f - position_;
        return 1.0f - remaining * remaining;
    }

    bool triggered() const noexcept { return triggered_; }
    float rawPosition() const noexcept { return position_; }

private:
    float coefficient (double milliseconds) const noexcept
    {
        return static_cast<float> (1.0 - std::exp (-1.0 / (milliseconds * 0.001 * sampleRate_)));
    }

    double sampleRate_ = 48000.0;
    float fast_ = 0.0f, slow_ = 0.0f;
    float fastAttack_ = 1.0f, fastRelease_ = 1.0f, slowAttack_ = 1.0f, slowRelease_ = 1.0f;
    float position_ = 1.0f;
    int holdOff_ = 2400;
    int sinceTrigger_ = 0;
    bool triggered_ = false;
};

} // namespace shifty
