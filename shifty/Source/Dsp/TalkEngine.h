#pragma once

#include "DspCommon.h"

#include <array>
#include <vector>

namespace shifty
{

/** Five-formant vowel tables. Vowels run OO, OH, AH, EH, EE (morph 0..4). */
struct VowelFormants
{
    std::array<float, 5> frequency;
    std::array<float, 5> decibels;
    std::array<float, 5> bandwidth;
};

const std::array<VowelFormants, 5>& vowelTable (int voiceSet) noexcept;
const char* vowelName (int vowel) noexcept;
const char* voiceSetName (int voiceSet) noexcept;
/** Formants of a vowel-morph position (0..4) in one voice set, interpolated in log frequency. */
VowelFormants interpolateVowel (int voiceSet, float morph) noexcept;

/**
    TALK: zero-latency morphing formant filter bank.

    Five resonant band-pass filters (TPT state-variable, safe under audio-rate
    modulation) are tuned to a vowel that morphs continuously from OO to EE.
    SHIFT scales every formant (constant Q); FOCUS sets their bandwidth; BITE
    drives the bank harder and raises its resonance; CHARACTER sharpens the
    resonances further and trims the unfiltered "body". SUB SAFE is a
    Linkwitz-Riley 4 split: the original sub passes untouched, and the dry
    path goes through the same all-pass so MIX does not comb near the split.

    Per-sample modulation (SHIFT and vowel offsets) is applied without
    smoothing; base parameters are smoothed over 20 ms. No latency.
    setParameters() and processBlock() are called from the audio thread.
*/
class TalkEngine
{
public:
    struct Display
    {
        std::array<float, 5> frequency {};
        std::array<float, 5> bandwidth {};
        std::array<float, 5> gain {};
        float body = 0.0f;
        float morph = 2.0f;
    };

    void prepare (double sampleRate, int maxBlockSize, int channels);
    void reset();
    void setParameters (const EngineParams&) noexcept;
    void processBlock (juce::AudioBuffer<float>&, const Modulation& modulation);
    int getLatencySamples() const noexcept { return 0; }
    const Display& display() const noexcept { return display_; }

    /** Static response of the bank for given formants, used by tests and the UI. */
    static float responseMagnitude (const Display&, float frequency) noexcept;

private:
    struct Formant
    {
        float ic1 = 0.0f, ic2 = 0.0f;
    };

    struct ChannelState
    {
        std::array<Formant, 5> formants;
        LinkwitzRiley4 split;      // original input: low band and its high band (dry all-pass)
        LinkwitzRiley4 drivenSplit; // driven input: high band into the bank
        AdaaDrive drive;
        float inputPower = 0.0f;
        float outputPower = 0.0f;
    };

    struct Smoothed
    {
        float shift = 0.0f, morph = 2.0f, focus = 0.5f, bite = 0.0f, drive = 0.0f;
        float mix = 1.0f, subCut = 120.0f, subSafe = 1.0f, character = 0.0f, bypass = 0.0f;
        std::array<float, 3> setWeight { 1.0f, 0.0f, 0.0f };
    };

    void computeFormants (float shift, float morph, std::array<float, 5>& frequency,
                          std::array<float, 5>& bandwidth, std::array<float, 5>& gain) const noexcept;

    std::vector<ChannelState> channels_;
    Smoothed current_;
    Smoothed target_;
    double sampleRate_ = 48000.0;
    float smoothing_ = 1.0f;
    float powerCoefficient_ = 0.0f;
    float autoGain_ = 1.0f;
    float autoGainCoefficient_ = 0.0f;
    float referencePower_ = 1.0f;
    float lastSubCut_ = -1.0f;
    Display display_;
};

} // namespace shifty
