#include "YoiVoice4.h"

#include <algorithm>
#include <cmath>

namespace yoiboi::dsp
{
namespace
{
constexpr double tau = 6.2831853071795864769;
constexpr float pi = 3.14159265358979323846f;

// EE, EH, AH, OH, OO match the original table; IH, UH, AE and ER extend it.
constexpr std::array<YoiVoice4::Pose, 9> vowelTable { {
    { { 300.0f, 2250.0f, 3050.0f }, { 1.0f, 0.83f, 0.43f } }, // EE
    { { 500.0f, 1850.0f, 2750.0f }, { 1.0f, 0.80f, 0.44f } }, // EH
    { { 730.0f, 1330.0f, 2550.0f }, { 1.0f, 0.75f, 0.48f } }, // AH
    { { 540.0f, 980.0f, 2400.0f }, { 1.0f, 0.84f, 0.43f } },  // OH
    { { 340.0f, 850.0f, 2250.0f }, { 1.0f, 0.74f, 0.36f } },  // OO
    { { 400.0f, 1920.0f, 2560.0f }, { 1.0f, 0.82f, 0.44f } }, // IH
    { { 640.0f, 1190.0f, 2390.0f }, { 1.0f, 0.76f, 0.40f } }, // UH
    { { 660.0f, 1720.0f, 2410.0f }, { 1.0f, 0.80f, 0.46f } }, // AE
    { { 490.0f, 1350.0f, 1690.0f }, { 1.0f, 0.85f, 0.50f } }  // ER
} };
constexpr YoiVoice4::Pose glideW { { 300.0f, 610.0f, 2150.0f }, { 1.0f, 0.70f, 0.30f } };
constexpr YoiVoice4::Pose glideY { { 270.0f, 2290.0f, 3010.0f }, { 1.0f, 0.80f, 0.45f } };

float finiteClamp (float value, float lo, float hi, float fallback) noexcept
{
    return std::clamp (std::isfinite (value) ? value : fallback, lo, hi);
}

float midiFrequency (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

float onePoleAmount (float seconds, double rate) noexcept
{
    return 1.0f - std::exp (-1.0f / (std::max (seconds, 1.0e-5f) * static_cast<float> (rate)));
}

float polyBlep (float phase, float step) noexcept
{
    if (phase < step) { const auto t = phase / step; return t + t - t * t - 1.0f; }
    if (phase > 1.0f - step) { const auto t = (phase - 1.0f) / step; return t * t + t + t + 1.0f; }
    return 0.0f;
}

float shapedFraction (float fraction, float curve) noexcept
{
    return std::pow (std::clamp (fraction, 0.0f, 1.0f), std::exp2 ((0.5f - curve) * 2.4f));
}

YoiVoice4::Pose blend (const YoiVoice4::Pose& a, const YoiVoice4::Pose& b, float fraction) noexcept
{
    YoiVoice4::Pose result {};
    for (std::size_t i = 0; i < 3; ++i)
    {
        result.frequency[i] = std::exp (std::log (a.frequency[i]) + fraction * (std::log (b.frequency[i]) - std::log (a.frequency[i])));
        result.gain[i] = a.gain[i] + fraction * (b.gain[i] - a.gain[i]);
    }
    return result;
}

// Transparent below 0.75; above it the level bends smoothly toward 0.98.
float softCeiling (float x) noexcept
{
    const float magnitude = std::abs (x);
    if (magnitude <= 0.75f) return x;
    return std::copysign (0.75f + 0.23f * std::tanh ((magnitude - 0.75f) / 0.23f), x);
}

float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
} // namespace

void YoiVoice4::Svf::configure (float frequency, float q, double rate) noexcept
{
    const float g = std::tan (pi * std::clamp (frequency, 5.0f, static_cast<float> (rate) * 0.45f) / static_cast<float> (rate));
    k = 1.0f / q;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

void YoiVoice4::Svf::process (float input, float& low, float& band, float& high) noexcept
{
    const float v3 = input - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    low = v2;
    band = v1;
    high = input - k * v1 - v2;
}

float YoiVoice4::Allpass::process (float input, float gain) noexcept
{
    const float delayed = buffer[position];
    const float written = input + gain * delayed;
    buffer[position] = std::abs (written) > 1.0e-15f ? written : 0.0f;
    position = position + 1 < length ? position + 1 : 0;
    return delayed - gain * written;
}

void YoiVoice4::Squash::prepare (double rate) noexcept
{
    lowSplit.configure (130.0f, 0.7071f, rate);
    highSplit.configure (2400.0f, 0.7071f, rate);
    attack = onePoleAmount (0.0015f, rate);
    release = onePoleAmount (0.08f, rate);
    reset();
}

void YoiVoice4::Squash::reset() noexcept
{
    lowSplit.reset(); highSplit.reset();
    envelope.fill (0.0f); gain.fill (1.0f);
}

void YoiVoice4::Squash::updateGains (float depth) noexcept
{
    // Upward and downward compression toward a per-band target, like a
    // three-band "OTT": quiet detail rises and loud bands settle.
    constexpr std::array<float, 3> targets { 0.10f, 0.09f, 0.06f };
    const float exponent = 0.75f * depth;
    for (std::size_t b = 0; b < 3; ++b)
        gain[b] = std::clamp (std::pow ((targets[b] + 1.0e-4f) / (envelope[b] + 1.0e-4f), exponent), 0.25f, 5.0f);
}

float YoiVoice4::Squash::process (float input, float depth) noexcept
{
    float low, band, high;
    lowSplit.process (input, low, band, high);
    const float rest = input - low;
    float restLow, restBand, top;
    highSplit.process (rest, restLow, restBand, top);
    const float mid = rest - top;
    const std::array<float, 3> bands { low, mid, top };
    float sum = 0.0f;
    for (std::size_t b = 0; b < 3; ++b)
    {
        const float level = std::abs (bands[b]);
        envelope[b] += (level - envelope[b]) * (level > envelope[b] ? attack : release);
        if (envelope[b] < 1.0e-12f) envelope[b] = 0.0f;
        sum += bands[b] * gain[b];
    }
    return lerp (input, sum / (1.0f + 0.6f * depth), depth);
}

void YoiVoice4::prepare (double newSampleRate)
{
    sampleRate = std::isfinite (newSampleRate) && newSampleRate >= 8000.0 ? newSampleRate : 48000.0;
    internalRate = sampleRate * 4.0;
    dcPole = static_cast<float> (std::exp (-tau * 5.0 / internalRate));
    controlSmoothingAmount = onePoleAmount (0.006f, sampleRate);
    formantSmoothingAmount = onePoleAmount (0.004f, sampleRate);
    bendSmoothing = onePoleAmount (0.003f, sampleRate);
    scrubSmoothing = onePoleAmount (0.008f, sampleRate);
    onsetStep = static_cast<float> (1.0 / (0.07 * sampleRate));
    burstAttackStep = static_cast<float> (1.0 / (0.0005 * internalRate));
    appliedAttack = appliedRelease = -1.0f;
    for (auto& d : decimate4x) d.prepare();
    for (auto& d : decimate2x) d.prepare();
    bodyHighPass[0].configure (110.0f, 0.5411961f, internalRate);
    bodyHighPass[1].configure (110.0f, 1.3065630f, internalRate);
    sideHighPass.configure (180.0f, 0.7071f, internalRate);
    const auto sizeFor = [this] (double seconds) { return std::max<std::size_t> (1, static_cast<std::size_t> (seconds * internalRate)); };
    decorrelateA.length = sizeFor (0.0037);
    decorrelateB.length = sizeFor (0.0061);
    decorrelateA.buffer.assign (decorrelateA.length, 0.0f);
    decorrelateB.buffer.assign (decorrelateB.length, 0.0f);
    squash.prepare (internalRate);
    configureConsonant();
    reset();
}

void YoiVoice4::reset() noexcept
{
    for (auto& band : formants) band.reset();
    for (auto& band : bodyHighPass) band.reset();
    sideHighPass.reset(); consonantFilter.reset();
    for (auto* line : { &decorrelateA, &decorrelateB })
    {
        std::fill (line->buffer.begin(), line->buffer.end(), 0.0f);
        line->position = 0;
    }
    squash.reset();
    for (auto& d : decimate4x) d.reset();
    for (auto& d : decimate2x) d.reset();
    dcInput.fill (0.0f); dcOutput.fill (0.0f);
    carrierPhase = modulatorPhase = subPhase = 0.0;
    gesturePosition = talkPhase = 0.0;
    bendTarget = bendSmoothed = 0.0f;
    noteSemitones = targetSemitones = static_cast<float> (currentNote);
    glideCoefficient = 1.0f;
    pitchEnvTime = 10.0f;
    envelope = subEnvelope = burstEnvelope = 0.0f;
    burstAttack = 1.0f;
    noteAgeSeconds = 0.0f;
    onsetProgress = 1.0f;
    gate = hasPlayed = false;
    formantFrequency.fill (1000.0f);
    formantGain.fill (0.0f);
    smoothedFmIndex = controls.fmIndex; smoothedFmRatio = controls.fmRatio; smoothedResonance = controls.resonance;
    smoothedDirt = controls.dirt; smoothedBite = controls.bite; smoothedBodyGain = controls.output;
    smoothedSubGain = controls.sub;
    smoothedDirectMix = controls.subMode == 1 ? 1.0f : 0.0f;
    smoothedDrivenMix = controls.subMode == 2 ? 1.0f : 0.0f;
    smoothedLowCut = controls.lowCut ? 1.0f : 0.0f;
    smoothedWidth = controls.width; smoothedSquash = controls.squash;
    scrubSmoothed = scrubTarget;
    crushHeld = 0.0f; crushCountdown = 0;
    poseUpdateCountdown = 0;
    displayedPosition = 0.0f;
}

void YoiVoice4::setControls (const LatestControls& next) noexcept
{
    controls.mouth = finiteClamp (next.mouth, 0.0f, 1.0f, controls.mouth);
    controls.size = finiteClamp (next.size, 0.0f, 1.0f, controls.size);
    controls.bite = finiteClamp (next.bite, 0.0f, 1.0f, controls.bite);
    controls.dirt = finiteClamp (next.dirt, 0.0f, 1.0f, controls.dirt);
    controls.sub = finiteClamp (next.sub, 0.0f, 1.0f, controls.sub);
    controls.subMode = std::clamp (next.subMode, 0, 2);
    controls.lowCut = next.lowCut;
    controls.output = finiteClamp (next.output, 0.0f, 2.0f, controls.output);
    controls.lengthSeconds = finiteClamp (next.lengthSeconds, 0.01f, 18.0f, controls.lengthSeconds);
    controls.bodyTime = finiteClamp (next.bodyTime, 0.2f, 0.8f, controls.bodyTime);
    controls.curve = finiteClamp (next.curve, 0.0f, 1.0f, controls.curve);
    controls.fmRatio = finiteClamp (next.fmRatio, 0.5f, 8.0f, controls.fmRatio);
    controls.fmIndex = finiteClamp (next.fmIndex, 0.0f, 14.0f, controls.fmIndex);
    controls.resonance = finiteClamp (next.resonance, 0.0f, 1.0f, controls.resonance);
    controls.loop = next.loop;
    controls.talkMode = std::clamp (next.talkMode, 0, 3);
    controls.talkIncrement = std::isfinite (next.talkIncrement) ? std::clamp (next.talkIncrement, 0.0, 0.01) : 0.0;
    controls.talkSyncPhase = std::isfinite (next.talkSyncPhase) ? next.talkSyncPhase : -1.0;
    controls.glideMode = std::clamp (next.glideMode, 0, 1);
    controls.pitchEnvSemitones = finiteClamp (next.pitchEnvSemitones, -24.0f, 24.0f, 0.0f);
    controls.pitchEnvSeconds = finiteClamp (next.pitchEnvSeconds, 0.005f, 2.0f, 0.12f);
    controls.width = finiteClamp (next.width, 0.0f, 1.0f, 0.0f);
    controls.subPhaseReset = next.subPhaseReset;
    controls.shaper = std::clamp (next.shaper, 0, 3);
    controls.squash = finiteClamp (next.squash, 0.0f, 1.0f, 0.0f);
    controls.vowelStart = std::clamp (next.vowelStart, 0, 8);
    controls.vowelBody = std::clamp (next.vowelBody, 0, 8);
    controls.vowelEnd = std::clamp (next.vowelEnd, 0, 8);
    controls.onsetGlide = std::clamp (next.onsetGlide, 0, 2);
    if (next.consonant != controls.consonant)
    {
        controls.consonant = std::clamp (next.consonant, 0, 3);
        configureConsonant();
    }
    controls.consonantLevel = finiteClamp (next.consonantLevel, 0.0f, 1.0f, 0.5f);
    controls.velocityToMouth = finiteClamp (next.velocityToMouth, 0.0f, 1.0f, 0.0f);
    controls.velocityToBite = finiteClamp (next.velocityToBite, 0.0f, 1.0f, 0.0f);
    controls.velocityToDirt = finiteClamp (next.velocityToDirt, 0.0f, 1.0f, 0.0f);
    controls.attackSeconds = finiteClamp (next.attackSeconds, 0.0005f, 0.2f, 0.003f);
    controls.releaseSeconds = finiteClamp (next.releaseSeconds, 0.005f, 2.0f, 0.075f);
    controls.separateSub = next.separateSub;
    if (std::abs (controls.attackSeconds - appliedAttack) > 1.0e-9f || std::abs (controls.releaseSeconds - appliedRelease) > 1.0e-9f)
    {
        appliedAttack = controls.attackSeconds;
        appliedRelease = controls.releaseSeconds;
        attackAmount = onePoleAmount (appliedAttack, sampleRate);
        releaseAmount = onePoleAmount (appliedRelease, sampleRate);
        subAttackAmount = onePoleAmount (appliedAttack + 0.004f, sampleRate);
        subReleaseAmount = onePoleAmount (appliedRelease + 0.010f, sampleRate);
    }
    if (controls.talkMode == 1 && controls.talkSyncPhase >= 0.0)
        talkPhase = controls.talkSyncPhase - std::floor (controls.talkSyncPhase);
}

void YoiVoice4::configureConsonant() noexcept
{
    // Short band-limited noise bursts: T is bright and very short, K sits in
    // the middle, CH is a longer airy hiss.
    switch (controls.consonant)
    {
        case 1: consonantFilter.configure (5200.0f, 1.5f, internalRate); burstDecay = onePoleAmount (0.006f, internalRate); break;
        case 2: consonantFilter.configure (1900.0f, 2.5f, internalRate); burstDecay = onePoleAmount (0.009f, internalRate); break;
        case 3: consonantFilter.configure (3600.0f, 0.9f, internalRate); burstDecay = onePoleAmount (0.025f, internalRate); break;
        default: burstDecay = onePoleAmount (0.006f, internalRate); break;
    }
}

void YoiVoice4::noteOn (int midiNote, float noteVelocity, float glideMs, bool retrigger) noexcept
{
    const bool wasAudible = isAudible();
    if (! wasAudible)
    {
        // A note from silence starts from a clean state, so every such attack
        // (and every offline render) is identical. Tails glide on as before.
        carrierPhase = modulatorPhase = subPhase = 0.0;
        for (auto& band : formants) band.reset();
        for (auto& band : bodyHighPass) band.reset();
        sideHighPass.reset(); consonantFilter.reset(); squash.reset();
        for (auto& d : decimate4x) d.reset();
        for (auto& d : decimate2x) d.reset();
        dcInput.fill (0.0f); dcOutput.fill (0.0f);
        crushHeld = 0.0f; crushCountdown = 0;
        noiseState = 0x9e3779b9u;
    }
    currentNote = std::clamp (midiNote, 0, 127);
    velocity = finiteClamp (noteVelocity, 0.0f, 1.0f, 1.0f);
    velocityGain = std::sqrt (velocity);
    targetSemitones = static_cast<float> (currentNote);
    const float safeGlideMs = finiteClamp (glideMs, 0.0f, 2000.0f, 0.0f);
    // Legato-only glide slides just between connected notes.
    const bool glides = safeGlideMs > 0.0f && wasAudible && (controls.glideMode == 0 || ! retrigger);
    glideCoefficient = glides ? onePoleAmount (safeGlideMs * 0.001f, sampleRate) : 1.0f;
    if (! glides) noteSemitones = targetSemitones;
    if (retrigger || ! hasPlayed || ! wasAudible)
    {
        gesturePosition = 0.0;
        noteAgeSeconds = 0.0f;
        poseUpdateCountdown = 0;
        pitchEnvTime = 0.0f;
        onsetProgress = controls.onsetGlide != 0 ? 0.0f : 1.0f;
        if (controls.talkMode == 1 && controls.talkSyncPhase < 0.0) talkPhase = 0.0;
        if (controls.consonant != 0) { burstEnvelope = 1.0f; burstAttack = 0.0f; }
        if (controls.subPhaseReset || ! wasAudible) subPhase = 0.0;
    }
    gate = true;
    hasPlayed = true;
}

void YoiVoice4::noteOff() noexcept { gate = false; }

void YoiVoice4::setPitchBend (float semitones) noexcept { bendTarget = finiteClamp (semitones, -24.0f, 24.0f, 0.0f); }

void YoiVoice4::setTalkScrub (float position) noexcept { scrubTarget = finiteClamp (position, 0.0f, 1.0f, 0.0f); }

bool YoiVoice4::isAudible() const noexcept
{
    return gate || envelope > 0.0001f || subEnvelope > 0.0001f || burstEnvelope > 0.0001f;
}

YoiVoice4::Pose YoiVoice4::poseAt (double position, bool seam) const noexcept
{
    const auto& start = vowelTable[static_cast<std::size_t> (controls.vowelStart)];
    const auto& body = vowelTable[static_cast<std::size_t> (controls.vowelBody)];
    const auto& end = vowelTable[static_cast<std::size_t> (controls.vowelEnd)];
    Pose moving {};
    if (position < controls.bodyTime)
        moving = blend (start, body, shapedFraction (static_cast<float> (position / controls.bodyTime), controls.curve));
    else
        moving = blend (body, end, shapedFraction (static_cast<float> ((position - controls.bodyTime) / (1.0f - controls.bodyTime)), controls.curve));
    if (seam && position > 0.94)
        moving = blend (moving, start, static_cast<float> ((position - 0.94) / 0.06));
    if (onsetProgress < 1.0f && controls.onsetGlide != 0)
    {
        const float t = onsetProgress * onsetProgress * (3.0f - 2.0f * onsetProgress);
        moving = blend (controls.onsetGlide == 1 ? glideW : glideY, moving, t);
    }
    const float mouth = controls.mouth * lerp (1.0f, velocity, controls.velocityToMouth);
    return blend (start, moving, mouth);
}

float YoiVoice4::shapePre (float input) noexcept
{
    switch (controls.shaper)
    {
        case 1: return input >= 0.0f ? std::tanh (input * 1.5f) : std::tanh (input * 0.45f) * 0.7f; // diode
        case 2: return std::sin (input * 1.3f);                                                     // fold
        default: return std::tanh (input);                                                          // classic, crush
    }
}

float YoiVoice4::shapePost (float input) noexcept
{
    if (controls.shaper == 1)
        return input >= 0.0f ? std::tanh (input * 1.25f) : std::tanh (input * 0.6f) * 0.8f;
    if (controls.shaper == 3)
    {
        // Crush: after the mouth, hold and quantize the signal. Dirt lowers
        // both the bit depth (8 to 3 bits) and the effective sample rate.
        if (--crushCountdown <= 0)
        {
            const float levels = std::exp2 (8.0f - 5.0f * smoothedDirt);
            crushHeld = std::round (std::tanh (input) * levels) / levels;
            crushCountdown = 1 + static_cast<int> (smoothedDirt * 15.0f);
        }
        return crushHeld;
    }
    return std::tanh (input);
}

YoiVoice4::Frame YoiVoice4::renderFrame() noexcept
{
    if (! isAudible()) return {};

    const float target = gate ? velocityGain : 0.0f;
    envelope += (target - envelope) * (gate ? attackAmount : releaseAmount);
    subEnvelope += (target - subEnvelope) * (gate ? subAttackAmount : subReleaseAmount);
    if (! gate && envelope < 0.00001f) envelope = 0.0f;
    if (! gate && subEnvelope < 0.00001f) subEnvelope = 0.0f;

    const float biteTarget = controls.bite * lerp (1.0f, velocity, controls.velocityToBite);
    const float dirtTarget = controls.dirt * lerp (1.0f, velocity, controls.velocityToDirt);
    const auto smooth = [this] (float& value, float goal) { value += (goal - value) * controlSmoothingAmount; };
    smooth (smoothedFmIndex, controls.fmIndex);
    smooth (smoothedFmRatio, controls.fmRatio);
    smooth (smoothedResonance, controls.resonance);
    smooth (smoothedDirt, dirtTarget);
    smooth (smoothedBite, biteTarget);
    smooth (smoothedBodyGain, controls.output);
    smooth (smoothedSubGain, controls.sub);
    smooth (smoothedDirectMix, controls.subMode == 1 ? 1.0f : 0.0f);
    smooth (smoothedDrivenMix, controls.subMode == 2 ? 1.0f : 0.0f);
    smooth (smoothedLowCut, controls.lowCut ? 1.0f : 0.0f);
    smooth (smoothedWidth, controls.width);
    smooth (smoothedSquash, controls.squash);

    // Pitch: glide moves the note, bend follows the wheel directly and the
    // envelope starts away from the note and settles onto it.
    noteSemitones += (targetSemitones - noteSemitones) * glideCoefficient;
    bendSmoothed += (bendTarget - bendSmoothed) * bendSmoothing;
    float envelopeOffset = 0.0f;
    if (pitchEnvTime < controls.pitchEnvSeconds)
    {
        const float remaining = 1.0f - pitchEnvTime / controls.pitchEnvSeconds;
        envelopeOffset = controls.pitchEnvSemitones * remaining * remaining;
        pitchEnvTime += 1.0f / static_cast<float> (sampleRate);
    }
    const float frequency = std::min (midiFrequency (noteSemitones + bendSmoothed + envelopeOffset),
                                      static_cast<float> (sampleRate * 0.45));
    const double carrierStep = static_cast<double> (frequency) / internalRate;
    const double modulatorStep = std::min (static_cast<double> (frequency * smoothedFmRatio), internalRate * 0.45) / internalRate;
    const float articulation = 0.78f + 0.22f * std::exp (-noteAgeSeconds / 0.035f);
    const float requestedIndex = smoothedFmIndex * (0.38f + 0.82f * smoothedBite) * articulation;
    const float modHz = frequency * smoothedFmRatio;
    const float sidebandBudget = (static_cast<float> (internalRate * 0.40) - frequency) / std::max (modHz, 1.0f) - 1.0f;
    const float index = std::min (requestedIndex, std::max (sidebandBudget, 0.0f));

    // Gesture position: one sweep per note, synced talk, or a scrub source.
    double position = gesturePosition;
    bool seam = controls.loop;
    if (controls.talkMode == 1) { position = talkPhase; seam = true; }
    else if (controls.talkMode >= 2)
    {
        scrubSmoothed += (scrubTarget - scrubSmoothed) * scrubSmoothing;
        position = scrubSmoothed;
        seam = false;
    }
    if (poseUpdateCountdown <= 0)
    {
        const auto pose = poseAt (position, seam);
        const float formantScale = std::exp (std::log (1.4f) + controls.size * (std::log (0.70f) - std::log (1.4f)));
        for (std::size_t i = 0; i < 3; ++i)
        {
            formantTargets[i] = std::min (pose.frequency[i] * formantScale, static_cast<float> (sampleRate * 0.44));
            gainTargets[i] = pose.gain[i];
        }
        poseUpdateCountdown = 15;
    }
    else --poseUpdateCountdown;

    const float q = 2.8f + smoothedResonance * 7.2f;
    for (std::size_t i = 0; i < 3; ++i)
    {
        formantFrequency[i] += (formantTargets[i] - formantFrequency[i]) * formantSmoothingAmount;
        formantGain[i] += (gainTargets[i] - formantGain[i]) * formantSmoothingAmount;
        formants[i].configure (formantFrequency[i], q, internalRate);
    }
    squash.updateGains (smoothedSquash);

    const float sawMix = 0.18f + 0.30f * smoothedBite;
    const float preDrive = 1.0f + smoothedDirt * 3.0f;
    const float postDrive = 1.15f + smoothedDirt * 2.2f;
    const float bodyLevel = 0.47f - 0.07f * smoothedDirt;
    const float burstLevel = controls.consonantLevel * 0.6f * velocityGain;
    std::array<std::array<float, 4>, 3> oversampled {};
    for (std::size_t k = 0; k < 4; ++k)
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
        const auto saw = (2.0f * sawPhase - 1.0f) - polyBlep (sawPhase, static_cast<float> (carrierStep));
        const auto source = (fm + sawMix * saw) / (1.0f + sawMix);
        const float pre = shapePre (source * preDrive);
        float bands = 0.0f;
        constexpr std::array<float, 3> bandWeights { 0.72f, 1.28f, 1.10f };
        for (std::size_t i = 0; i < 3; ++i)
        {
            float low, band, high;
            formants[i].process (pre, low, band, high);
            bands += band * formants[i].k * formantGain[i] * bandWeights[i];
        }
        const float post = shapePost ((0.065f * pre + 1.65f * bands) * postDrive);
        float body = post * bodyLevel * envelope;

        if (burstEnvelope > 0.0001f && controls.consonant != 0)
        {
            noiseState ^= noiseState << 13; noiseState ^= noiseState >> 17; noiseState ^= noiseState << 5;
            const float noise = static_cast<float> (noiseState) * (2.0f / 4294967295.0f) - 1.0f;
            float low, band, high;
            consonantFilter.process (noise, low, band, high);
            burstAttack = std::min (1.0f, burstAttack + burstAttackStep);
            body += band * consonantFilter.k * burstEnvelope * burstAttack * burstLevel;
            burstEnvelope -= burstEnvelope * burstDecay;
        }

        float low1, band1, high1, low2, band2, high2;
        bodyHighPass[0].process (body, low1, band1, high1);
        bodyHighPass[1].process (high1, low2, band2, high2);
        const float lowCutBody = body + smoothedLowCut * (high2 - body);
        const float squashed = smoothedSquash > 0.0005f ? squash.process (lowCutBody, smoothedSquash) : lowCutBody;

        const auto subSine = static_cast<float> (std::sin (tau * subPhase));
        const float drivenSub = std::tanh (subSine * (1.0f + 5.0f * smoothedDirt)) * smoothedSubGain * 0.25f * subEnvelope * smoothedDrivenMix;
        const float directSub = subSine * smoothedSubGain * 0.28f * subEnvelope * smoothedDirectMix;
        const float subSignal = drivenSub + directSub;

        float side = 0.0f;
        if (smoothedWidth > 0.0005f)
        {
            const float diffused = decorrelateB.process (decorrelateA.process (squashed, 0.55f), 0.55f);
            float sideLow, sideBand, sideHigh;
            sideHighPass.process (diffused, sideLow, sideBand, sideHigh);
            side = sideHigh * smoothedWidth * 0.8f;
        }
        const float subToMain = controls.separateSub ? 0.0f : subSignal;
        const std::array<float, 3> raw { (squashed + side + subToMain) * smoothedBodyGain,
                                         (squashed - side + subToMain) * smoothedBodyGain,
                                         controls.separateSub ? subSignal * smoothedBodyGain : 0.0f };
        for (std::size_t c = 0; c < 3; ++c)
        {
            const float blocked = raw[c] - dcInput[c] + dcPole * dcOutput[c];
            dcInput[c] = raw[c];
            dcOutput[c] = std::abs (blocked) > 1.0e-15f ? blocked : 0.0f;
            oversampled[c][k] = softCeiling (blocked);
        }
    }

    Frame frame;
    float* outputs[3] { &frame.left, &frame.right, &frame.sub };
    for (std::size_t c = 0; c < 3; ++c)
    {
        const float first = decimate4x[c].process (oversampled[c][0], oversampled[c][1]);
        const float second = decimate4x[c].process (oversampled[c][2], oversampled[c][3]);
        *outputs[c] = std::clamp (decimate2x[c].process (first, second), -0.99f, 0.99f);
    }

    noteAgeSeconds = std::min (noteAgeSeconds + 1.0f / static_cast<float> (sampleRate), 10.0f);
    if (onsetProgress < 1.0f) onsetProgress = std::min (1.0f, onsetProgress + onsetStep);
    if (controls.talkMode == 1)
    {
        talkPhase += controls.talkIncrement;
        if (talkPhase >= 1.0) talkPhase -= std::floor (talkPhase);
    }
    if (gate)
    {
        gesturePosition += 1.0 / (static_cast<double> (controls.lengthSeconds) * sampleRate);
        if (controls.loop) { if (gesturePosition >= 1.0) gesturePosition -= 1.0; }
        else gesturePosition = std::min (gesturePosition, 1.0);
    }
    // Reported like the original engines: the position after this sample.
    displayedPosition = static_cast<float> (controls.talkMode == 1 ? talkPhase
                                            : controls.talkMode >= 2 ? static_cast<double> (scrubSmoothed) : gesturePosition);
    return frame;
}
} // namespace yoiboi::dsp
