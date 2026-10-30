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
    subSafeHz = requestedSubSafeHz = -1.0f;
    hpFadeSamples = std::max (1, (int) std::ceil (0.01 * sr));
    hp1 = hp2 = hpTarget = {};
    hpTargetOn = hpOn = false;
    reset();
}

void SlapDelay::reset()
{
    for (auto* line : { &left, &right }) {
        std::fill (line->buffer.begin(), line->buffer.end(), 0.0f);
        line->toneState = line->guardIn = line->guardOut = 0.0f;
        line->delaySamples = line->targetSamples;
    }
    hp1 = hp2 = hpTarget;
    hp1.clear(); hp2.clear();
    hpFadeRemaining = 0;
    subSafeHz = requestedSubSafeHz;
    hpOn = hpTargetOn;
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
    if (std::abs (s.subSafeHz - requestedSubSafeHz) > 0.01f) {
        requestedSubSafeHz = s.subSafeHz;
        hpTargetOn = requestedSubSafeHz > 0.0f;
        // Two Butterworth sections make a 24 dB/oct Linkwitz-Riley.
        hpTarget = {};
        if (hpTargetOn) hpTarget.setHighPass (sr, requestedSubSafeHz, 0.70710678);
        if (subSafeHz < 0.0f) {
            hp1 = hp2 = hpTarget;
            hpOn = hpTargetOn;
            subSafeHz = requestedSubSafeHz;
        } else if (hpFadeRemaining == 0) {
            beginSubSafeChange();
        }
        // During a fade retain just the latest request. Finish the current 10 ms fade first,
        // so even automation faster than the fade never interrupts a partly blended filter.
    }
}

void SlapDelay::beginSubSafeChange() noexcept
{
    previousHp1 = hp1; previousHp2 = hp2; previousHpOn = hpOn;
    hp1 = hp2 = hpTarget;
    hpOn = hpTargetOn;
    subSafeHz = requestedSubSafeHz;
    hpFadeRemaining = hpFadeSamples;
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
    float x = hpOn ? hp2.process (hp1.process (in)) : in;
    if (hpFadeRemaining > 0) {
        const float previous = previousHpOn ? previousHp2.process (previousHp1.process (in)) : in;
        const float mix = 1.0f - (float) --hpFadeRemaining / (float) hpFadeSamples;
        x = previous + (x - previous) * mix;
        if (hpFadeRemaining == 0 && std::abs (requestedSubSafeHz - subSafeHz) > 0.01f)
            beginSubSafeChange();
    }
    outLeft = runLine (left, x);
    outRight = runLine (right, x);
    if (++writeIndex >= size) writeIndex = 0;
}

} // namespace stutter
