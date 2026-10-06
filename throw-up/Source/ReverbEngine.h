#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace throwup
{
// Sound generations. Version 1 is the 0.1.5 tone, grit, duck law and return
// level, kept bit-exact for projects saved with 0.1.5. Version 2 is the latest.
enum class SoundVersion { v015 = 1, latest = 2 };

struct Settings
{
    // Stable host/state field: THROW is the wet-return level over unity dry.
    float mix = 0.0f;
    float size = 60.0f;
    float decay = 2.4f;
    float predelay = 24.0f;      // ms; tempo sync converts to ms before reaching the engine
    float lowCut = 180.0f;
    float highCut = 6500.0f;
    float diffusion = 75.0f;
    float motion = 20.0f;
    float width = 110.0f;
    float grit = 10.0f;
    float duck = 15.0f;
    float output = 0.0f;
    bool bypass = false;

    // 0.2.0 additions. Every default reproduces 0.1.5 except `version`.
    SoundVersion version = SoundVersion::latest;
    float returnTrim = 0.0f;     // dB, wet return only
    bool dry = true;             // false = send mode: wet only
    bool safeClip = false;       // visible soft clip on the final output
    bool freeze = false;         // infinite hold: the tail stops decaying and takes no input
    float shimmer = 0.0f;        // 0..100 % of the loop passes through the pitch shifter
    float shimmerPitch = 12.0f;  // semitones
    float duckRelease = 160.0f;  // ms
    float duckThreshold = -24.0f;// dBFS, latest duck law only
    bool duckExternal = false;   // duck detector listens to the sidechain input
    // THROW trigger. The effective THROW is the larger of THROW and the trigger envelope.
    float triggerAttack = 5.0f;  // ms for a full-range rise
    float triggerRelease = 500.0f; // ms for a full-range fall
    float triggerAmount = 100.0f;  // % for the THROW! key and auto-catch
    bool triggerHeld = false;    // THROW! key held
    bool autoCatch = false;      // open a throw when an input phrase ends
    float catchThreshold = -30.0f; // dBFS
    // Swell: the wet rises over a tempo-synced cycle and cuts at the grid line.
    bool swell = false;
    double swellPhase = 0.0;
    double swellPhaseInput = -1.0; // last host-provided phase; the engine free-runs between updates     // cycle position (0..1) at the start of the next block
    double swellPhaseStep = 0.0; // per sample
};

// A trigger level change at a sample offset in the next process() call (MIDI notes).
struct TriggerEvent
{
    int offset = 0;
    float level = 0.0f; // 0..100 % THROW while held, 0 = released
};

// Stereo parallel reverb: output = (dry + THROW * wet-only return) * output gain.
// The dry path has zero latency. All storage is allocated in prepare(), and
// process() supports mono/stereo buffers of any block length without allocation.
class ReverbEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setSettings (const Settings& settings);
    // Events must be sorted by offset; they apply to the next process() call only.
    void setTriggerEvents (const TriggerEvent* events, int count);
    void process (float* const* channels, int numChannels, int numSamples,
                  const float* const* sidechain = nullptr, int sidechainChannels = 0);
    // Current trigger envelope (0..100 %), for metering and tests.
    float getTriggerLevel() const noexcept { return triggerEnvelope; }

    // Maximum tempo-synced pre-delay the engine can hold.
    static constexpr float maximumPredelayMs = 2500.0f;

private:
    struct Delay
    {
        std::vector<float> data;
        std::size_t write = 0;
        void prepare (std::size_t capacity);
        void clear();
        float read (float delaySamples) const;
        void push (float value);
    };

    struct Allpass
    {
        Delay delay;
        float length = 1.0f;
        float process (float input, float coefficient);
        void clear();
    };

    struct OnePole
    {
        float state = 0.0f;
        float lowpass (float input, float coefficient);
        float highpass (float input, float coefficient);
    };

    // Topology-preserving state-variable high-pass: stable for low cut-offs at high sample rates.
    struct HighpassSvf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        float process (float input, float a1, float a2, float a3, float k);
        void clear() { ic1 = ic2 = 0.0f; }
    };

    struct SvfCoefficients
    {
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
        void set (float cutoff, float q, float sampleRate);
    };

    // First-order antiderivative-antialiased soft clip with unity small-signal gain.
    struct AntialiasedSaturator
    {
        double previous = 0.0;
        float process (float input, double ceiling);
        void clear() { previous = 0.0; }
    };

    static Settings sanitise (const Settings& settings);
    static float safeSample (float sample);
    static float softClip (float sample);
    static void hadamard (std::array<float, 8>& values);
    void updateCoefficients();
    void updateDelayTargets();
    float predelayTap (std::size_t channel, float delaySamples, float original) const;
    float shimmerProcess (std::size_t voice, float input);
    void updateTrigger (float catchDetector);

    Settings target;
    Settings current;
    double sampleRate = 48000.0;
    bool prepared = false;
    float parameterSlew = 0.0f;
    float fastSlew = 0.0f;
    float envelopeAttack = 0.0f;
    float envelopeRelease = 0.0f;
    float envelopeReleaseMs = 160.0f;
    float envelope = 0.0f;
    float lowCutCoefficient = 0.0f;
    float highCutCoefficient = 0.0f;
    float mixWet = 0.0f;
    // THROW moves linearly across each processed block toward the host value,
    // never faster than the 4 ms full-range ramp.
    float mixRampFrom = 0.0f;
    float mixRampLength = 1.0f;
    float mixRampPosition = 0.0f;
    float outputGain = 1.0f;
    float excitationGain = 1.0f;
    float excitationTarget = 1.0f;
    // Bypass crossfade: 0 = processing, 1 = fully bypassed (exact dry output).
    float bypassFade = 0.0f;
    float bypassStep = 0.0f;
    // Delay-length changes never slew a read position quickly. Moves short
    // enough to finish within a glide budget glide at a pitch-safe rate;
    // larger moves crossfade between the old and new read taps.
    float predelayGlideRate = 0.0f;
    float predelayJumpLimit = 0.0f;
    float tailGlideRate = 0.0f;
    float tailJumpLimit = 0.0f;
    float delayFadeStep = 0.0f;
    float predelayCorrelatedLimit = 0.0f;
    float predelayActive = 0.0f;
    float predelayNext = 0.0f;
    float predelayFade = 1.0f; // 1 = no crossfade running
    bool predelayEqualPower = true;
    float tailFade = 1.0f; // 1 = no crossfade running
    std::array<float, 8> modulationPhases {};
    int coefficientCountdown = 0;
    std::array<Delay, 2> predelayLines;
    std::array<std::array<Allpass, 4>, 2> diffusers;
    std::array<Delay, 8> tailLines;
    std::array<OnePole, 8> tailDamping;
    std::array<std::array<OnePole, 2>, 2> inputHighpass;
    std::array<std::array<OnePole, 2>, 2> outputHighpass;
    std::array<OnePole, 2> outputLowpass;
    std::array<float, 8> tailLengths {};
    std::array<float, 8> tailNext {};
    std::array<float, 8> feedbackGains {};

    // Latest-sound (version 2) stages.
    std::array<HighpassSvf, 2> inputHighpassV2, outputHighpassV2;
    SvfCoefficients inputHighpassCoefficients, outputHighpassCoefficients;
    std::array<AntialiasedSaturator, 2> gritV2;
    // 0.2.0 controls.
    float returnTrimGain = 1.0f;
    float dryGain = 1.0f, dryStep = 0.0f;
    float freezeAmount = 0.0f, freezeStep = 0.0f;
    float swellAmount = 0.0f, swellStep = 0.0f;
    double swellPhase = 0.0;
    double swellPhaseInput = -1.0; // last host-provided phase; the engine free-runs between updates
    float swellFadeSamples = 1.0f;
    // Shimmer: four voices, each with two crossfading read heads over its own
    // delay, shift four orthogonal directions of the feedback vector.
    std::array<Delay, 4> shimmerLines;
    std::array<float, 4> shimmerPhases {};
    float shimmerWindow = 1.0f;
    // Short-term energy before and after the shimmer stage: the stage is made
    // energy-neutral, so the loop's feedback gain (< 1) alone sets the decay.
    float shimmerEnergyIn = 0.0f, shimmerEnergyOut = 0.0f, shimmerEnergyCoefficient = 0.0f;
    // THROW trigger and auto-catch.
    std::array<TriggerEvent, 64> events {};
    int eventCount = 0;
    float eventLevel = 0.0f;
    float triggerEnvelope = 0.0f;
    float catchEnvelope = 0.0f, catchAttack = 0.0f, catchRelease = 0.0f;
    float catchTarget = 0.0f;
    int catchActiveSamples = 0, catchQuietSamples = 0;
    int catchMinimumPhrase = 1, catchGap = 1;
};
} // namespace throwup
