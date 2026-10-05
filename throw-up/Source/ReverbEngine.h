#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace throwup
{
struct Settings
{
    // Stable host/state field: THROW is the wet-return level over unity dry.
    float mix = 0.0f;
    float size = 60.0f;
    float decay = 2.4f;
    float predelay = 24.0f;
    float lowCut = 180.0f;
    float highCut = 6500.0f;
    float diffusion = 75.0f;
    float motion = 20.0f;
    float width = 110.0f;
    float grit = 10.0f;
    float duck = 15.0f;
    float output = 0.0f;
    bool bypass = false;
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
    void process (float* const* channels, int numChannels, int numSamples);

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

    static Settings sanitise (const Settings& settings);
    static float safeSample (float sample);
    static void hadamard (std::array<float, 8>& values);
    void updateCoefficients();

    Settings target;
    Settings current;
    double sampleRate = 48000.0;
    bool prepared = false;
    float parameterSlew = 0.0f;
    float fastSlew = 0.0f;
    float sizeSlew = 0.0f;
    float envelopeAttack = 0.0f;
    float envelopeRelease = 0.0f;
    float envelope = 0.0f;
    float lowCutCoefficient = 0.0f;
    float highCutCoefficient = 0.0f;
    float mixWet = 0.0f;
    float outputGain = 1.0f;
    float excitationGain = 1.0f;
    float excitationTarget = 1.0f;
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
    std::array<float, 8> feedbackGains {};
};
} // namespace throwup
