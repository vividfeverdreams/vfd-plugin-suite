#pragma once
#include <cmath>
#include <vector>

namespace stutter {

// A short stereo slap: sub-safe high-pass on the send, two delay lines, tone and soft clip inside the feedback loop.
// Real-time safe after prepare(): no allocation in setSettings() or process().
class SlapDelay
{
public:
    struct Settings
    {
        float timeLeft = 0.09f, timeRight = 0.104f; // seconds
        float feedback = 0.22f;                      // 0..0.75
        float toneHz = 2000.0f;                      // feedback-loop low-pass
        float subSafeHz = 120.0f;                    // 0 = off
    };

    void prepare (double sampleRate);
    void reset();
    void setSettings (const Settings&); // call once per block
    void process (float in, float& outLeft, float& outRight) noexcept;

    static constexpr double maxSeconds = 1.0;

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void setHighPass (double sampleRate, double hz, double q);
        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept { z1 = z2 = 0; }
    };
    struct Line
    {
        std::vector<float> buffer;
        float delaySamples = 0, targetSamples = 0;
        float toneState = 0, guardIn = 0, guardOut = 0;
        float read (int writeIndex) const noexcept;
    };

    float runLine (Line&, float in) noexcept;

    double sr = 48000.0;
    Line left, right;
    Biquad hp1, hp2;
    int writeIndex = 0, size = 0;
    float feedback = 0, feedbackTarget = 0, toneCoeff = 0.2f, toneTarget = 0.2f;
    float slewCoeff = 0.0005f, guardCoeff = 0.995f, paramCoeff = 0.001f;
    float subSafeHz = -1.0f;
    bool hpOn = true;
};

} // namespace stutter
