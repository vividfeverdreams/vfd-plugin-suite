#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "HalfbandDecimator.h"
#include "YoiVoice.h"

namespace yoiboi::dsp
{
// Engine 4 (0.2.0) controls. The base fields mean exactly what they mean for
// the earlier engines; engines 1-3 only ever see the base slice.
struct LatestControls : VoiceControls
{
    int talkMode = 0;             // 0 one sweep per note, 1 synced talk, 2 mod wheel, 3 aftertouch
    double talkIncrement = 0.0;   // synced talk: cycles per host sample
    double talkSyncPhase = -1.0;  // host-locked cycle phase at this block's first sample (< 0: free-running)
    int glideMode = 0;            // 0 always (original), 1 legato only
    float pitchEnvSemitones = 0.0f;
    float pitchEnvSeconds = 0.12f;
    float width = 0.0f;           // body only; the sub stays mono
    bool subPhaseReset = false;
    int shaper = 0;               // Classic, Diode, Fold, Crush
    float squash = 0.0f;
    int vowelStart = 4, vowelBody = 2, vowelEnd = 0; // EE EH AH OH OO IH UH AE ER
    int onsetGlide = 0;           // none, W, Y
    int consonant = 0;            // none, T, K, CH
    float consonantLevel = 0.5f;
    float velocityToMouth = 0.0f, velocityToBite = 0.0f, velocityToDirt = 0.0f;
    float attackSeconds = 0.003f, releaseSeconds = 0.075f;
    bool separateSub = false;     // the host enabled the "Sub" output bus
};

// Engine 4: the 0.1.3 4x architecture with extended vowels, articulation,
// talk motion, a pitch envelope, selectable shapers, a three-band Squash, body
// width and an oversampled soft ceiling. Every output path has the same 24
// samples of latency. Allocation happens only in prepare().
class YoiVoice4 final
{
public:
    struct Frame { float left = 0.0f, right = 0.0f, sub = 0.0f; };

    void prepare (double newSampleRate);
    void reset() noexcept;
    void setControls (const LatestControls& newControls) noexcept;
    void noteOn (int midiNote, float velocity, float glideMs, bool retrigger = true) noexcept;
    void noteOff() noexcept;
    void setPitchBend (float semitones) noexcept;
    void setTalkScrub (float position) noexcept; // 0..1 from mod wheel or aftertouch
    Frame renderFrame() noexcept;
    float renderSample() noexcept { return renderFrame().left; }
    [[nodiscard]] bool isAudible() const noexcept;
    [[nodiscard]] float gesturePhase() const noexcept { return displayedPosition; }
    [[nodiscard]] static constexpr int latencySamples() noexcept { return 24; }

    struct Pose { std::array<float, 3> frequency; std::array<float, 3> gain; };

private:
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
        void configure (float frequency, float q, double sampleRate) noexcept;
        void process (float input, float& low, float& band, float& high) noexcept;
        void reset() noexcept { ic1 = ic2 = 0.0f; }
    };
    struct Allpass
    {
        std::vector<float> buffer;
        std::size_t position = 0, length = 1;
        float process (float input, float gain) noexcept;
    };
    struct Squash
    {
        Svf lowSplit, highSplit;
        std::array<float, 3> envelope {}, gain { 1.0f, 1.0f, 1.0f };
        float attack = 0.01f, release = 0.0003f;
        void prepare (double internalRate) noexcept;
        void reset() noexcept;
        void updateGains (float depth) noexcept;
        float process (float input, float depth) noexcept;
    };

    [[nodiscard]] Pose poseAt (double position, bool seam) const noexcept;
    [[nodiscard]] float shapePre (float input) noexcept;
    [[nodiscard]] float shapePost (float input) noexcept;
    void configureConsonant() noexcept;

    std::array<Svf, 3> formants {};
    std::array<float, 3> formantFrequency { 1000.0f, 1000.0f, 1000.0f }, formantGain {};
    std::array<float, 3> formantTargets { 300.0f, 2250.0f, 3050.0f }, gainTargets { 1.0f, 0.83f, 0.43f };
    std::array<Svf, 2> bodyHighPass {};
    Svf sideHighPass, consonantFilter;
    Allpass decorrelateA, decorrelateB;
    Squash squash;
    std::array<HalfbandDecimator, 3> decimate4x, decimate2x; // left, right, sub
    std::array<float, 3> dcInput {}, dcOutput {};
    LatestControls controls {};
    double sampleRate = 48000.0, internalRate = 192000.0;
    double carrierPhase = 0.0, modulatorPhase = 0.0, subPhase = 0.0;
    double gesturePosition = 0.0, talkPhase = 0.0;
    float noteSemitones = 45.0f, targetSemitones = 45.0f, glideCoefficient = 1.0f;
    float bendTarget = 0.0f, bendSmoothed = 0.0f, bendSmoothing = 0.007f;
    float pitchEnvTime = 10.0f;
    float scrubTarget = 0.0f, scrubSmoothed = 0.0f, scrubSmoothing = 0.003f;
    float envelope = 0.0f, subEnvelope = 0.0f, velocityGain = 1.0f, velocity = 1.0f;
    float noteAgeSeconds = 0.0f, onsetProgress = 1.0f, onsetStep = 0.0003f;
    float burstEnvelope = 0.0f, burstAttack = 1.0f, burstDecay = 0.001f, burstAttackStep = 0.01f;
    float smoothedFmIndex = 4.0f, smoothedFmRatio = 2.0f, smoothedResonance = 0.5f;
    float smoothedDirt = 0.3f, smoothedBite = 0.5f, smoothedBodyGain = 0.8f, smoothedSubGain = 0.55f;
    float smoothedDirectMix = 1.0f, smoothedDrivenMix = 0.0f, smoothedLowCut = 1.0f;
    float smoothedWidth = 0.0f, smoothedSquash = 0.0f;
    float dcPole = 0.9998f;
    float attackAmount = 0.0069f, releaseAmount = 0.00028f, subAttackAmount = 0.003f, subReleaseAmount = 0.00025f;
    float appliedAttack = -1.0f, appliedRelease = -1.0f;
    float controlSmoothingAmount = 0.0035f, formantSmoothingAmount = 0.0052f;
    float crushHeld = 0.0f, displayedPosition = 0.0f;
    std::uint32_t noiseState = 0x9e3779b9u;
    int crushCountdown = 0, poseUpdateCountdown = 0, currentNote = 45;
    bool gate = false, hasPlayed = false;
};
} // namespace yoiboi::dsp
