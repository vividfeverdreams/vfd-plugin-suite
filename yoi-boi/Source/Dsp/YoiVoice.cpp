#include "YoiVoice.h"

#include <algorithm>
#include <cmath>

namespace yoiboi::dsp
{
namespace
{
constexpr double tau = 6.2831853071795864769;
constexpr float pi = 3.14159265358979323846f;

// Ordered from bright/small to dark/round. These are starting points for
// listening calibration, not a claim to reproduce a particular human voice.
constexpr std::array<YoiVoice::VowelPose, 5> vowelPoses { {
    { { 300.0f, 2250.0f, 3050.0f }, { 1.0f, 0.83f, 0.43f } }, // EE
    { { 500.0f, 1850.0f, 2750.0f }, { 1.0f, 0.80f, 0.44f } }, // EH
    { { 730.0f, 1330.0f, 2550.0f }, { 1.0f, 0.75f, 0.48f } }, // AH
    { { 540.0f, 980.0f, 2400.0f }, { 1.0f, 0.84f, 0.43f } }, // OH
    { { 340.0f, 850.0f, 2250.0f }, { 1.0f, 0.74f, 0.36f } }  // OO
} };

float finiteClamp (float value, float lo, float hi, float fallback) noexcept
{
    return std::clamp (std::isfinite (value) ? value : fallback, lo, hi);
}

float midiFrequency (float note) noexcept
{
    return 440.0f * std::exp2 ((note - 69.0f) / 12.0f);
}

float onePoleAmount (float seconds, double sampleRate) noexcept
{
    return 1.0f - std::exp (-1.0f / (seconds * static_cast<float> (sampleRate)));
}

float polyBlep (float phase, float step) noexcept
{
    if (phase < step)
    {
        const auto t = phase / step;
        return t + t - t * t - 1.0f;
    }
    if (phase > 1.0f - step)
    {
        const auto t = (phase - 1.0f) / step;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

float shapedFraction (float fraction, float curve) noexcept
{
    const auto exponent = std::exp2 ((0.5f - curve) * 2.4f);
    return std::pow (std::clamp (fraction, 0.0f, 1.0f), exponent);
}
} // namespace

void YoiVoice::FormantBand::reset() noexcept
{
    ic1 = 0.0f;
    ic2 = 0.0f;
    smoothedFrequency = 1000.0f;
    smoothedGain = 0.0f;
}

void YoiVoice::FormantBand::configure (float frequency, float q, double sr) noexcept
{
    // Topology-preserving state-variable filter. Modulating its cutoff cannot
    // create an unstable intermediate biquad coefficient set.
    const float g = std::tan (pi * frequency / static_cast<float> (sr));
    k = 1.0f / q;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

float YoiVoice::FormantBand::process (float input, bool highPass) noexcept
{
    const float v3 = input - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return highPass ? input - k * v1 - v2 : v1 * k;
}

void YoiVoice::prepare (double newSampleRate) noexcept
{
    sampleRate = std::isfinite (newSampleRate) && newSampleRate >= 8000.0
                     ? newSampleRate : 48000.0;
    dcPole = static_cast<float> (std::exp (-tau * 5.0 / sampleRate));
    attackAmount = onePoleAmount (0.003f, sampleRate);
    releaseAmount = onePoleAmount (0.075f, sampleRate);
    subAttackAmount = onePoleAmount (0.007f, sampleRate);
    subReleaseAmount = onePoleAmount (0.085f, sampleRate);
    controlSmoothingAmount = onePoleAmount (0.006f, sampleRate);
    formantSmoothingAmount = onePoleAmount (0.004f, sampleRate);
    decimate4x.prepare();
    decimate2x.prepare();
    bodyHighPass[0].configure (110.0f, 0.5411961f, sampleRate * 4.0);
    bodyHighPass[1].configure (110.0f, 1.3065630f, sampleRate * 4.0);
    reset();
}

void YoiVoice::reset() noexcept
{
    for (auto& band : formants)
        band.reset();
    for (auto& band : bodyHighPass)
        band.reset();
    decimate4x.reset();
    decimate2x.reset();
    directSubDelay.fill (0.0f);
    subDelayPosition = 0;
    carrierPhase = 0.0;
    modulatorPhase = 0.0;
    subPhase = 0.0;
    pitchBendSemitones = 0.0f;
    currentFrequency = midiFrequency (static_cast<float> (currentNote));
    targetFrequency = currentFrequency;
    glideCoefficient = 1.0f;
    envelope = 0.0f;
    subEnvelope = 0.0f;
    gesturePosition = 0.0f;
    noteAgeSeconds = 0.0f;
    gate = false;
    hasPlayed = false;
    smoothedFmIndex = controls.fmIndex;
    smoothedFmRatio = controls.fmRatio;
    smoothedResonance = controls.resonance;
    smoothedDirt = controls.dirt;
    smoothedBite = controls.bite;
    smoothedBodyGain = controls.output;
    smoothedSubGain = controls.sub;
    smoothedDirectMix = controls.subMode == 1 ? 1.0f : 0.0f;
    smoothedDrivenMix = controls.subMode == 2 ? 1.0f : 0.0f;
    smoothedLowCut = controls.lowCut ? 1.0f : 0.0f;
    dcPreviousInput = 0.0f;
    dcPreviousOutput = 0.0f;
    poseUpdateCountdown = 0;
}

void YoiVoice::setControls (const VoiceControls& next) noexcept
{
    controls.mouth = finiteClamp (next.mouth, 0.0f, 1.0f, controls.mouth);
    controls.size = finiteClamp (next.size, 0.0f, 1.0f, controls.size);
    controls.bite = finiteClamp (next.bite, 0.0f, 1.0f, controls.bite);
    controls.dirt = finiteClamp (next.dirt, 0.0f, 1.0f, controls.dirt);
    controls.sub = finiteClamp (next.sub, 0.0f, 1.0f, controls.sub);
    controls.subMode = std::clamp (next.subMode, 0, 2);
    controls.lowCut = next.lowCut;
    controls.output = finiteClamp (next.output, 0.0f, 2.0f, controls.output);
    controls.lengthSeconds = finiteClamp (next.lengthSeconds, 0.01f, 18.0f,
                                          controls.lengthSeconds);
    controls.bodyTime = finiteClamp (next.bodyTime, 0.2f, 0.8f, controls.bodyTime);
    controls.curve = finiteClamp (next.curve, 0.0f, 1.0f, controls.curve);
    controls.fmRatio = finiteClamp (next.fmRatio, 0.5f, 8.0f, controls.fmRatio);
    controls.fmIndex = finiteClamp (next.fmIndex, 0.0f, 14.0f, controls.fmIndex);
    controls.resonance = finiteClamp (next.resonance, 0.0f, 1.0f, controls.resonance);
    controls.startPose = std::clamp (next.startPose, 0, 4);
    controls.bodyPose = std::clamp (next.bodyPose, 0, 4);
    controls.endPose = std::clamp (next.endPose, 0, 4);
    controls.loop = next.loop;
    // Keep the control-rate clock independent of host block boundaries. The
    // next pose update is at most 15 samples away; restarting the clock here
    // made identical notes sound slightly different at different block sizes.
}

void YoiVoice::noteOn (int midiNote, float velocity, float glideMs, bool retrigger) noexcept
{
    const auto wasAudible = isAudible();
    currentNote = std::clamp (midiNote, 0, 127);
    velocityGain = std::sqrt (finiteClamp (velocity, 0.0f, 1.0f, 1.0f));
    targetFrequency = frequencyForCurrentNote();
    const float safeGlideMs = finiteClamp (glideMs, 0.0f, 2000.0f, 0.0f);
    glideCoefficient = safeGlideMs <= 0.0f ? 1.0f
        : onePoleAmount (safeGlideMs * 0.001f, sampleRate);
    if (! wasAudible || safeGlideMs <= 0.0f)
        currentFrequency = targetFrequency;

    if (retrigger || ! hasPlayed || ! wasAudible)
    {
        gesturePosition = 0.0f;
        noteAgeSeconds = 0.0f;
        poseUpdateCountdown = 0;
        // Keep the existing envelope level on a retrigger. Its upward slew is
        // click-free even for repeated notes during the release tail.
    }
    gate = true;
    hasPlayed = true;
}

void YoiVoice::noteOff() noexcept
{
    gate = false;
}

void YoiVoice::setPitchBend (float semitones) noexcept
{
    pitchBendSemitones = finiteClamp (semitones, -24.0f, 24.0f, 0.0f);
    targetFrequency = frequencyForCurrentNote();
}

float YoiVoice::frequencyForCurrentNote() const noexcept
{
    return midiFrequency (static_cast<float> (currentNote) + pitchBendSemitones);
}

YoiVoice::VowelPose YoiVoice::interpolatePose (const VowelPose& a,
                                               const VowelPose& b,
                                               float fraction) noexcept
{
    VowelPose result {};
    for (int i = 0; i < 3; ++i)
    {
        const auto index = static_cast<std::size_t> (i);
        result.frequency[index] = std::exp (std::log (a.frequency[index])
            + fraction * (std::log (b.frequency[index]) - std::log (a.frequency[index])));
        result.gain[index] = a.gain[index] + fraction * (b.gain[index] - a.gain[index]);
    }
    return result;
}

YoiVoice::VowelPose YoiVoice::currentPose() const noexcept
{
    const auto& start = vowelPoses[static_cast<std::size_t> (controls.startPose)];
    const auto& body = vowelPoses[static_cast<std::size_t> (controls.bodyPose)];
    const auto& end = vowelPoses[static_cast<std::size_t> (controls.endPose)];

    VowelPose moving {};
    if (gesturePosition < controls.bodyTime)
    {
        const auto portion = shapedFraction (static_cast<float> (gesturePosition / controls.bodyTime), controls.curve);
        moving = interpolatePose (start, body, portion);
    }
    else
    {
        const auto portion = shapedFraction (static_cast<float> ((gesturePosition - controls.bodyTime)
                                             / (1.0f - controls.bodyTime)), controls.curve);
        moving = interpolatePose (body, end, portion);
    }

    if (controls.loop && gesturePosition > 0.94f)
    {
        const auto seam = static_cast<float> ((gesturePosition - 0.94) / 0.06);
        moving = interpolatePose (moving, start, seam);
    }
    return interpolatePose (start, moving, controls.mouth);
}

bool YoiVoice::isAudible() const noexcept
{
    return gate || envelope > 0.0001f || subEnvelope > 0.0001f;
}

float YoiVoice::renderSample() noexcept
{
    if (! isAudible())
        return 0.0f;

    const float target = gate ? velocityGain : 0.0f;
    envelope += (target - envelope) * (gate ? attackAmount : releaseAmount);
    subEnvelope += (target - subEnvelope) * (gate ? subAttackAmount : subReleaseAmount);
    if (! gate && envelope < 0.00001f) envelope = 0.0f;
    if (! gate && subEnvelope < 0.00001f) subEnvelope = 0.0f;

    smoothedFmIndex += (controls.fmIndex - smoothedFmIndex) * controlSmoothingAmount;
    smoothedFmRatio += (controls.fmRatio - smoothedFmRatio) * controlSmoothingAmount;
    smoothedResonance += (controls.resonance - smoothedResonance) * controlSmoothingAmount;
    smoothedDirt += (controls.dirt - smoothedDirt) * controlSmoothingAmount;
    smoothedBite += (controls.bite - smoothedBite) * controlSmoothingAmount;
    smoothedBodyGain += (controls.output - smoothedBodyGain) * controlSmoothingAmount;
    smoothedSubGain += (controls.sub - smoothedSubGain) * controlSmoothingAmount;
    smoothedDirectMix += ((controls.subMode == 1 ? 1.0f : 0.0f) - smoothedDirectMix)
                         * controlSmoothingAmount;
    smoothedDrivenMix += ((controls.subMode == 2 ? 1.0f : 0.0f) - smoothedDrivenMix)
                         * controlSmoothingAmount;
    smoothedLowCut += ((controls.lowCut ? 1.0f : 0.0f) - smoothedLowCut)
                     * controlSmoothingAmount;

    currentFrequency += (targetFrequency - currentFrequency) * glideCoefficient;
    const auto internalRate = sampleRate * 4.0;
    const double carrierStep = std::min (static_cast<double> (currentFrequency),
                                         sampleRate * 0.45) / internalRate;
    const double modulatorStep = std::min (static_cast<double> (currentFrequency * smoothedFmRatio),
                                           internalRate * 0.45) / internalRate;

    const float articulation = 0.78f + 0.22f * std::exp (-noteAgeSeconds / 0.035f);
    const float requestedIndex = smoothedFmIndex * (0.38f + 0.82f * smoothedBite)
                                 * articulation;
    const float modHz = currentFrequency * smoothedFmRatio;
    const float sidebandBudget = (static_cast<float> (internalRate * 0.40)
                                  - currentFrequency) / std::max (modHz, 1.0f) - 1.0f;
    const float index = std::min (requestedIndex, std::max (sidebandBudget, 0.0f));

    if (poseUpdateCountdown <= 0)
    {
        const auto pose = currentPose();
        const float formantScale = std::exp (std::log (1.4f)
            + controls.size * (std::log (0.70f) - std::log (1.4f)));
        for (int i = 0; i < 3; ++i)
        {
            const auto bandIndex = static_cast<std::size_t> (i);
            formantTargets[bandIndex] = std::min (pose.frequency[bandIndex] * formantScale,
                                               static_cast<float> (sampleRate * 0.44));
            gainTargets[bandIndex] = pose.gain[bandIndex];
        }
        poseUpdateCountdown = 15;
    }
    else
        --poseUpdateCountdown;

    const float q = 2.8f + smoothedResonance * 7.2f;
    for (int i = 0; i < 3; ++i)
    {
        auto& band = formants[static_cast<std::size_t> (i)];
        band.smoothedFrequency += (formantTargets[static_cast<std::size_t> (i)]
                                  - band.smoothedFrequency) * formantSmoothingAmount;
        band.smoothedGain += (gainTargets[static_cast<std::size_t> (i)] - band.smoothedGain)
                             * formantSmoothingAmount;
        band.configure (band.smoothedFrequency, q, internalRate);
    }

    std::array<float, 4> oversampled {};
    for (auto& sample : oversampled)
    {
        carrierPhase += carrierStep;
        modulatorPhase += modulatorStep;
        subPhase += carrierStep * 0.5;
        if (carrierPhase >= 1.0) carrierPhase -= 1.0;
        if (modulatorPhase >= 1.0) modulatorPhase -= 1.0;
        if (subPhase >= 1.0) subPhase -= 1.0;

        const auto mod = static_cast<float> (std::sin (tau * modulatorPhase));
        const auto fm = static_cast<float> (std::sin (tau * carrierPhase + index * mod));
        const auto sawPhase = static_cast<float> (carrierPhase);
        const auto saw = (2.0f * sawPhase - 1.0f)
                         - polyBlep (sawPhase, static_cast<float> (carrierStep));
        // A little band-limited saw excites the upper vowels even in low-index
        // patches. FM stays defined; heavy distortion is an explicit choice.
        const float sawMix = 0.18f + 0.30f * smoothedBite;
        const auto source = (fm + sawMix * saw) / (1.0f + sawMix);
        const float preDrive = 1.0f + smoothedDirt * 3.0f;
        const float pre = std::tanh (source * preDrive);
        const float bands = formants[0].process (pre) * formants[0].smoothedGain * 0.72f
                          + formants[1].process (pre) * formants[1].smoothedGain * 1.28f
                          + formants[2].process (pre) * formants[2].smoothedGain * 1.10f;
        const float mouthSignal = 0.065f * pre + 1.65f * bands;
        const float post = std::tanh (mouthSignal * (1.15f + smoothedDirt * 2.2f));
        const float body = post * (0.47f - 0.07f * smoothedDirt) * envelope;
        const float filteredBody = bodyHighPass[1].process (
            bodyHighPass[0].process (body, true), true);
        const float lowCutBody = body + smoothedLowCut * (filteredBody - body);
        const float drivenSub = std::tanh (static_cast<float> (std::sin (tau * subPhase))
                                * (1.0f + 5.0f * smoothedDirt))
                                * smoothedSubGain * 0.25f * subEnvelope * smoothedDrivenMix;
        sample = lowCutBody + drivenSub;
    }
    const auto firstPair = decimate4x.process (oversampled[0], oversampled[1]);
    const auto secondPair = decimate4x.process (oversampled[2], oversampled[3]);
    const float bodyAndDrivenSub = decimate2x.process (firstPair, secondPair);
    const float cleanSub = static_cast<float> (std::sin (tau * subPhase))
                           * smoothedSubGain * 0.28f * subEnvelope * smoothedDirectMix;
    const auto delayedCleanSub = directSubDelay[subDelayPosition];
    directSubDelay[subDelayPosition] = cleanSub;
    subDelayPosition = (subDelayPosition + 1u) % directSubDelay.size();

    if (gate)
    {
        noteAgeSeconds = std::min (noteAgeSeconds + 1.0f / static_cast<float> (sampleRate), 10.0f);
        gesturePosition += 1.0 / (static_cast<double> (controls.lengthSeconds) * sampleRate);
        if (controls.loop)
        {
            if (gesturePosition >= 1.0) gesturePosition -= 1.0;
        }
        else
            gesturePosition = std::min (gesturePosition, 1.0);
    }

    // Direct mode is a separate, latency-aligned sine mixed after every body
    // waveshaper and formant. The old always-on master tanh dirtied the clean
    // sub and caused intermodulation between it and the vowel harmonics.
    const float mixed = (bodyAndDrivenSub + delayedCleanSub) * smoothedBodyGain;
    const float dcRemoved = mixed - dcPreviousInput + dcPole * dcPreviousOutput;
    dcPreviousInput = mixed;
    dcPreviousOutput = std::abs (dcRemoved) > 1.0e-15f ? dcRemoved : 0.0f;
    // A DC blocker can momentarily overshoot its input peak after a sudden
    // offset change. Keep the host output bounded even at the +6 dB setting.
    return std::clamp (dcRemoved, -0.98f, 0.98f);
}
} // namespace yoiboi::dsp
