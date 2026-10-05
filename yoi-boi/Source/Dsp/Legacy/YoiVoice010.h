#pragma once

#include <array>

namespace yoiboi::dsp::legacy010
{
// All values are plain numbers so a host adapter can hand the voice an immutable
// copy at the start of a render block. setControls and renderSample run on the
// audio thread; neither method allocates or takes a lock.
struct VoiceControls
{
    float mouth = 0.65f;       // 0..1: vowel travel depth
    float size = 0.5f;         // 0..1: larger means lower formants
    float bite = 0.5f;         // 0..1: harmonic density and articulation
    float dirt = 0.3f;         // 0..1: pre/post saturation
    float sub = 0.55f;         // 0..1: clean sub gain
    float output = 0.8f;      // 0..1: global linear trim
    float lengthSeconds = 0.25f;
    float bodyTime = 0.48f;   // normalized gesture time, 0.2..0.8
    float curve = 0.5f;       // 0..1: below/above 0.5 ease differently
    float fmRatio = 2.0f;     // modulator/carrier ratio
    float fmIndex = 4.0f;     // peak phase modulation in radians
    float resonance = 0.5f;   // 0..1; bounded Q mapping
    int startPose = 0;        // EE, EH, AH, OH, OO = 0..4
    int bodyPose = 3;
    int endPose = 0;
    bool loop = false;
};

class YoiVoice final
{
public:
    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;
    void setControls (const VoiceControls& newControls) noexcept;

    // The caller owns MIDI note priority, sustain, and preview arbitration.
    // Passing retrigger=false makes a connected legato note retain its envelope
    // and gesture phase. glideMs=0 snaps immediately to the new pitch.
    void noteOn (int midiNote, float velocity, float glideMs, bool retrigger = true) noexcept;
    void noteOff() noexcept;
    void setPitchBend (float semitones) noexcept;
    float renderSample() noexcept;
    [[nodiscard]] bool isAudible() const noexcept;
    [[nodiscard]] float gesturePhase() const noexcept { return gesturePosition; }

    // Public solely to permit a constant pose table in the implementation.
    struct VowelPose
    {
        std::array<float, 3> frequency;
        std::array<float, 3> gain;
    };

private:
    struct FormantBand
    {
        float ic1 = 0.0f;
        float ic2 = 0.0f;
        float smoothedFrequency = 1000.0f;
        float smoothedGain = 0.0f;
        float process (float input, float frequency, float q, double sampleRate) noexcept;
        void reset() noexcept;
    };

    [[nodiscard]] static VowelPose interpolatePose (const VowelPose& a,
                                                      const VowelPose& b,
                                                      float fraction) noexcept;
    [[nodiscard]] VowelPose currentPose() const noexcept;
    [[nodiscard]] float frequencyForCurrentNote() const noexcept;

    std::array<FormantBand, 3> formants {};
    VoiceControls controls {};
    double sampleRate = 48000.0;
    double carrierPhase = 0.0;
    double modulatorPhase = 0.0;
    double subPhase = 0.0;
    float currentFrequency = 110.0f;
    float targetFrequency = 110.0f;
    float glideCoefficient = 1.0f;
    float envelope = 0.0f;
    float subEnvelope = 0.0f;
    float velocityGain = 1.0f;
    float gesturePosition = 0.0f;
    float noteAgeSeconds = 0.0f;
    float pitchBendSemitones = 0.0f;
    float smoothedFmIndex = 4.0f;
    float smoothedDirt = 0.3f;
    float smoothedBite = 0.5f;
    float smoothedBodyGain = 0.8f;
    float smoothedSubGain = 0.55f;
    float dcPreviousInput = 0.0f;
    float dcPreviousOutput = 0.0f;
    float dcPole = 0.999f;
    std::array<float, 3> formantTargets { 300.0f, 2250.0f, 3050.0f };
    std::array<float, 3> gainTargets { 1.0f, 0.83f, 0.43f };
    int poseUpdateCountdown = 0;
    int currentNote = 45;
    bool gate = false;
    bool hasPlayed = false;
};
} // namespace yoiboi::dsp::legacy010
