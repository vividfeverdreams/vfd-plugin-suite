#include "SlapDelay.h"
#include <algorithm>

namespace stutter {

namespace { constexpr double twoPi = 6.283185307179586; }

void SlapDelay::Biquad::setHighPass (double sampleRate, double hz, double q)
{
    const double w = twoPi * std::clamp (hz, 10.0, sampleRate * 0.45) / sampleRate;
    const double cosw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = (float) ((1.0 + cosw) * 0.5 / a0);
    b1 = (float) (-(1.0 + cosw) / a0);
    b2 = b0;
    a1 = (float) (-2.0 * cosw / a0);
    a2 = (float) ((1.0 - alpha) / a0);
}

void SlapDelay::prepare (double sampleRate)
{
    sr = sampleRate;
    size = (int) std::ceil (maxSeconds * sr) + 8;
    for (auto* line : { &left, &right }) line->buffer.assign ((size_t) size, 0.0f);
    slewCoeff = (float) (1.0 - std::exp (-1.0 / (0.06 * sr)));  // 60 ms time slew: big jumps glide like tape
    paramCoeff = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));
    guardCoeff = (float) std::exp (-twoPi * 60.0 / sr);           // keeps lows from building up in the loop
    subSafeHz = -1.0f;
    reset();
}

void SlapDelay::reset()
{
    for (auto* line : { &left, &right }) {
        std::fill (line->buffer.begin(), line->buffer.end(), 0.0f);
        line->toneState = line->guardIn = line->guardOut = 0.0f;
        line->delaySamples = line->targetSamples;
    }
    hp1.clear(); hp2.clear();
    writeIndex = 0;
    feedback = feedbackTarget;
    toneCoeff = toneTarget;
}

void SlapDelay::setSettings (const Settings& s)
{
    const float maxDelay = (float) (maxSeconds * sr) - 4.0f;
    left.targetSamples = std::clamp (s.timeLeft * (float) sr, 2.0f, maxDelay);
    right.targetSamples = std::clamp (s.timeRight * (float) sr, 2.0f, maxDelay);
    if (left.delaySamples <= 0.0f) left.delaySamples = left.targetSamples;
    if (right.delaySamples <= 0.0f) right.delaySamples = right.targetSamples;
    feedbackTarget = std::clamp (s.feedback, 0.0f, 0.75f);
    toneTarget = (float) (1.0 - std::exp (-twoPi * std::clamp ((double) s.toneHz, 100.0, sr * 0.45) / sr));
    if (std::abs (s.subSafeHz - subSafeHz) > 0.01f) {
        subSafeHz = s.subSafeHz;
        hpOn = subSafeHz > 0.0f;
        // Two Butterworth sections make a 24 dB/oct Linkwitz-Riley: nothing below SUB SAFE ever repeats.
        if (hpOn) { hp1.setHighPass (sr, subSafeHz, 0.70710678); hp2.setHighPass (sr, subSafeHz, 0.70710678); }
    }
}

float SlapDelay::Line::read (int write) const noexcept
{
    const int n = (int) buffer.size();
    const float pos = (float) write - delaySamples;
    const float fl = std::floor (pos);
    const float frac = pos - fl;
    int i1 = (int) fl % n;
    if (i1 < 0) i1 += n;
    const int i0 = (i1 - 1 + n) % n, i2 = (i1 + 1) % n, i3 = (i1 + 2) % n;
    const float y0 = buffer[(size_t) i0], y1 = buffer[(size_t) i1], y2 = buffer[(size_t) i2], y3 = buffer[(size_t) i3];
    // 4-point Hermite
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + y1;
}

float SlapDelay::runLine (Line& line, float in) noexcept
{
    line.delaySamples += (line.targetSamples - line.delaySamples) * slewCoeff;
    const float r = line.read (writeIndex);
    line.toneState += (r - line.toneState) * toneCoeff;
    // DC / low guard (one-pole high-pass around 60 Hz)
    const float guarded = guardCoeff * (line.guardOut + line.toneState - line.guardIn);
    line.guardIn = line.toneState;
    line.guardOut = guarded;
    line.buffer[(size_t) writeIndex] = in + std::tanh (feedback * guarded);
    return guarded;
}

void SlapDelay::process (float in, float& outLeft, float& outRight) noexcept
{
    feedback += (feedbackTarget - feedback) * paramCoeff;
    toneCoeff += (toneTarget - toneCoeff) * paramCoeff;
    float x = in;
    if (hpOn) x = hp2.process (hp1.process (x));
    outLeft = runLine (left, x);
    outRight = runLine (right, x);
    if (++writeIndex >= size) writeIndex = 0;
}

} // namespace stutter
