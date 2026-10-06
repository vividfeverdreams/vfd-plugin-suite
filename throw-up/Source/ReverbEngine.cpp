#include "ReverbEngine.h"

#include <algorithm>
#include <cmath>

namespace throwup
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float sqrtOneEighth = 0.3535533905932738f;
// The network is energy-normalised for stable feedback, which leaves its
// filtered return too quiet beside a unity dry bass. Restore a useful musical
// level at the return, outside the feedback/drive stages: +18 dB at full THROW.
// This is the 0.1.5 calibration, kept bit-exact for sound version 1.
constexpr float wetReturnGain = 7.943282347f;
// Latest sound: Grit no longer adds hidden make-up gain (0.1.5's Grit raised the
// wet by up to 9.5 dB), so the flat return is calibrated slightly higher for the
// same audibility at full THROW (see VALIDATION.md).
constexpr float wetReturnGainV2 = 9.4406087629f; // +19.5 dB
constexpr std::array<float, 8> delaySeconds {
    0.02171f, 0.02603f, 0.03137f, 0.03743f,
    0.04469f, 0.05329f, 0.06377f, 0.07621f
};
constexpr std::array<float, 8> motionRates {
    0.71f, 0.83f, 1.0f, 1.13f, 1.37f, 1.61f, 1.79f, 1.97f
};
// Shimmer directions: four rows of the normalised 8x8 Hadamard matrix, so they
// are orthonormal and each voice shifts an independent part of the loop.
constexpr std::array<int, 4> shimmerRows { 1, 2, 4, 7 };
constexpr float hadamardSign (int row, std::size_t column)
{
    int bits = row & static_cast<int> (column), parity = 0;
    while (bits != 0) { parity ^= bits & 1; bits >>= 1; }
    return parity != 0 ? -sqrtOneEighth : sqrtOneEighth;
}
// Parameter-change smoothing for delay lengths (Size, Pre-delay) and bypass.
// Small moves glide the read position slowly (0.02 samples/sample is about
// 0.35 semitone, 0.03 about 0.5 semitone). Moves that would take longer than
// the glide budget crossfade between the old and new read taps instead, so room
// and pre-delay changes never zipper, pitch-dive or play the buffer backwards.
constexpr double delayFadeSeconds = 0.040;
constexpr float predelayGlidePerSample = 0.02f;
constexpr double predelayGlideSeconds = 0.040;
// Tail taps crossfade linearly (the only law that cannot add loop gain), which
// dips decorrelated tails by up to 3 dB mid-fade; ordinary knob moves glide instead.
constexpr float tailGlidePerSample = 0.03f;
constexpr double tailGlideSeconds = 0.250;
constexpr double bypassFadeSeconds = 0.015;
// Butterworth fourth-order high-pass split into its two second-order sections:
// the full wet path is -3 dB at the Low cut label (latest sound).
constexpr float lowCutQInput = 1.3065630f;
constexpr float lowCutQOutput = 0.5411961f;
// Trigger and catch timing.
constexpr float minimumTriggerAttackMs = 4.0f; // never faster than the THROW ramp
constexpr double catchPhraseSeconds = 0.060;   // a phrase must last this long...
constexpr double catchGapSeconds = 0.060;      // ...and then stop for this long

float bounded (float value, float minimum, float maximum, float fallback)
{
    return std::isfinite (value) ? std::clamp (value, minimum, maximum) : fallback;
}

float noDenormal (float value)
{
    return std::abs (value) < 1.0e-20f ? 0.0f : value;
}

double logCosh (double x)
{
    const double a = std::abs (x);
    return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994530942;
}

float decibelsToGain (float decibels)
{
    return decibels == 0.0f ? 1.0f : std::pow (10.0f, decibels / 20.0f);
}
} // namespace

void ReverbEngine::Delay::prepare (std::size_t capacity)
{
    data.assign (std::max<std::size_t> (capacity, 4), 0.0f);
    write = 0;
}

void ReverbEngine::Delay::clear()
{
    std::fill (data.begin(), data.end(), 0.0f);
    write = 0;
}

float ReverbEngine::Delay::read (float delaySamples) const
{
    delaySamples = std::clamp (delaySamples, 1.0f, static_cast<float> (data.size() - 2));
    const auto whole = static_cast<std::size_t> (delaySamples);
    const float fraction = delaySamples - static_cast<float> (whole);
    // Wrap integer indices, not a floating position: rounding a tiny negative
    // position up to the ring length would otherwise read one past the buffer.
    const auto first = write >= whole ? write - whole : write + data.size() - whole;
    const auto second = first == 0 ? data.size() - 1 : first - 1;
    return data[first] + fraction * (data[second] - data[first]);
}

void ReverbEngine::Delay::push (float value)
{
    data[write] = noDenormal (value);
    if (++write == data.size())
        write = 0;
}

float ReverbEngine::Allpass::process (float input, float coefficient)
{
    const float delayed = delay.read (length);
    const float output = delayed - coefficient * input;
    delay.push (input + coefficient * output);
    return output;
}

void ReverbEngine::Allpass::clear() { delay.clear(); }

float ReverbEngine::OnePole::lowpass (float input, float coefficient)
{
    state = noDenormal (input + coefficient * (state - input));
    return state;
}

float ReverbEngine::OnePole::highpass (float input, float coefficient)
{
    return input - lowpass (input, coefficient);
}

void ReverbEngine::SvfCoefficients::set (float cutoff, float q, float rate)
{
    const float g = std::tan (pi * std::min (cutoff, rate * 0.45f) / rate);
    k = 1.0f / q;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

float ReverbEngine::HighpassSvf::process (float input, float a1, float a2, float a3, float k)
{
    const float v3 = input - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = noDenormal (2.0f * v1 - ic1);
    ic2 = noDenormal (2.0f * v2 - ic2);
    return input - k * v1 - v2;
}

float ReverbEngine::AntialiasedSaturator::process (float input, double ceiling)
{
    // y = C tanh (x / C): unity gain for small signals, never above the ceiling.
    // First-order antiderivative antialiasing (F = C^2 log cosh (x / C)) removes
    // most of the aliasing a plain tanh would fold back at high drive.
    const double x = input;
    const double difference = x - previous;
    double output;
    if (std::abs (difference) > 1.0e-6 * ceiling)
        output = ceiling * ceiling * (logCosh (x / ceiling) - logCosh (previous / ceiling)) / difference;
    else
        output = ceiling * std::tanh (0.5 * (x + previous) / ceiling);
    previous = x;
    return static_cast<float> (output);
}

Settings ReverbEngine::sanitise (const Settings& settings)
{
    Settings s = settings;
    s.mix = bounded (s.mix, 0.0f, 100.0f, 0.0f);
    s.size = bounded (s.size, 0.0f, 100.0f, 60.0f);
    s.decay = bounded (s.decay, 0.15f, 18.0f, 2.4f);
    s.predelay = bounded (s.predelay, 0.0f, maximumPredelayMs, 24.0f);
    s.lowCut = bounded (s.lowCut, 80.0f, 1000.0f, 180.0f);
    s.highCut = bounded (s.highCut, 1000.0f, 18000.0f, 6500.0f);
    s.diffusion = bounded (s.diffusion, 0.0f, 100.0f, 75.0f);
    s.motion = bounded (s.motion, 0.0f, 100.0f, 20.0f);
    s.width = bounded (s.width, 0.0f, 150.0f, 110.0f);
    s.grit = bounded (s.grit, 0.0f, 100.0f, 10.0f);
    s.duck = bounded (s.duck, 0.0f, 100.0f, 15.0f);
    s.output = bounded (s.output, -18.0f, 6.0f, 0.0f);
    // The host parameter's 0.1 dB interval snapping turns a UI edit or reset to
    // 0 dB into about 2.7e-7 dB; treat that as exact unity so dry stays bit-exact.
    if (std::abs (s.output) < 0.0005f)
        s.output = 0.0f;
    if (s.version != SoundVersion::v015) s.version = SoundVersion::latest;
    s.returnTrim = bounded (s.returnTrim, -24.0f, 6.0f, 0.0f);
    if (std::abs (s.returnTrim) < 0.0005f)
        s.returnTrim = 0.0f;
    s.shimmer = bounded (s.shimmer, 0.0f, 100.0f, 0.0f);
    s.shimmerPitch = bounded (s.shimmerPitch, -24.0f, 24.0f, 12.0f);
    s.duckRelease = bounded (s.duckRelease, 20.0f, 2000.0f, 160.0f);
    // Same snapping concern: the default 160 ms must mean the 0.1.5 release exactly.
    if (std::abs (s.duckRelease - 160.0f) < 0.01f)
        s.duckRelease = 160.0f;
    s.duckThreshold = bounded (s.duckThreshold, -60.0f, 0.0f, -24.0f);
    s.triggerAttack = bounded (s.triggerAttack, 0.0f, 2000.0f, 5.0f);
    s.triggerRelease = bounded (s.triggerRelease, 10.0f, 20000.0f, 500.0f);
    s.triggerAmount = bounded (s.triggerAmount, 0.0f, 100.0f, 100.0f);
    s.catchThreshold = bounded (s.catchThreshold, -72.0f, 0.0f, -30.0f);
    if (! std::isfinite (s.swellPhase)) s.swellPhase = 0.0;
    s.swellPhase -= std::floor (s.swellPhase);
    s.swellPhaseStep = std::isfinite (s.swellPhaseStep) ? std::clamp (s.swellPhaseStep, 0.0, 0.01) : 0.0;
    return s;
}

float ReverbEngine::safeSample (float sample)
{
    return std::isfinite (sample) ? sample : 0.0f;
}

float ReverbEngine::softClip (float sample)
{
    // Transparent below -3 dBFS, then a smooth knee that never exceeds 0 dBFS.
    constexpr float knee = 0.7f, range = 0.3f;
    const float magnitude = std::abs (sample);
    if (magnitude <= knee) return sample;
    return std::copysign (knee + range * std::tanh ((magnitude - knee) / range), sample);
}

void ReverbEngine::prepare (double newSampleRate, int maxBlockSize)
{
    (void) maxBlockSize;
    sampleRate = std::isfinite (newSampleRate) && newSampleRate >= 8000.0
                   ? newSampleRate : 48000.0;
    parameterSlew = static_cast<float> (1.0 - std::exp (-1.0 / (sampleRate * 0.035)));
    fastSlew = static_cast<float> (1.0 - std::exp (-1.0 / (sampleRate * 0.008)));
    envelopeAttack = static_cast<float> (std::exp (-1.0 / (sampleRate * 0.003)));
    envelopeReleaseMs = target.duckRelease;
    envelopeRelease = static_cast<float> (std::exp (-1.0 / (sampleRate * (static_cast<double> (envelopeReleaseMs) / 1000.0))));
    bypassStep = static_cast<float> (1.0 / (sampleRate * bypassFadeSeconds));
    delayFadeStep = static_cast<float> (1.0 / (sampleRate * delayFadeSeconds));
    predelayGlideRate = predelayGlidePerSample;
    predelayJumpLimit = static_cast<float> (predelayGlidePerSample * sampleRate * predelayGlideSeconds);
    tailGlideRate = tailGlidePerSample;
    tailJumpLimit = static_cast<float> (tailGlidePerSample * sampleRate * tailGlideSeconds);
    predelayCorrelatedLimit = static_cast<float> (sampleRate * 0.002);
    dryStep = static_cast<float> (1.0 / (sampleRate * 0.010));
    freezeStep = static_cast<float> (1.0 / (sampleRate * 0.030));
    swellStep = static_cast<float> (1.0 / (sampleRate * 0.020));
    swellFadeSamples = static_cast<float> (sampleRate * 0.003);
    catchAttack = static_cast<float> (std::exp (-1.0 / (sampleRate * 0.001)));
    catchRelease = static_cast<float> (std::exp (-1.0 / (sampleRate * 0.020)));
    catchMinimumPhrase = std::max (1, static_cast<int> (sampleRate * catchPhraseSeconds));
    catchGap = std::max (1, static_cast<int> (sampleRate * catchGapSeconds));

    for (auto& delay : predelayLines)
        delay.prepare (static_cast<std::size_t> (sampleRate * (maximumPredelayMs * 0.001 + 0.002)) + 8);
    for (auto& delay : tailLines)
        delay.prepare (static_cast<std::size_t> (sampleRate * 0.250) + 8);
    // Shimmer grains: 60 ms windows, so the line holds two windows plus margin.
    shimmerWindow = static_cast<float> (sampleRate * 0.060);
    shimmerEnergyCoefficient = static_cast<float> (std::exp (-1.0 / (sampleRate * 0.020)));
    for (auto& line : shimmerLines)
        line.prepare (static_cast<std::size_t> (shimmerWindow * 2.0f) + 16);

    constexpr std::array<float, 4> diffuserSeconds { 0.00311f, 0.00473f, 0.00703f, 0.01117f };
    for (std::size_t channel = 0; channel < diffusers.size(); ++channel)
        for (std::size_t stage = 0; stage < diffusers[channel].size(); ++stage)
        {
            auto& diffuser = diffusers[channel][stage];
            // Integer allpass lengths preserve their magnitude response.
            diffuser.length = std::floor (static_cast<float> (sampleRate)
                                         * diffuserSeconds[stage]
                                         * (channel == 0 ? 1.0f : 1.127f));
            diffuser.delay.prepare (static_cast<std::size_t> (diffuser.length) + 4);
        }
    prepared = true;
    reset();
}

void ReverbEngine::reset()
{
    for (auto& line : predelayLines) line.clear();
    for (auto& line : tailLines) line.clear();
    for (auto& channel : diffusers)
        for (auto& diffuser : channel) diffuser.clear();
    for (auto& filter : tailDamping) filter.state = 0.0f;
    for (auto& channel : inputHighpass)
        for (auto& filter : channel) filter.state = 0.0f;
    for (auto& channel : outputHighpass)
        for (auto& filter : channel) filter.state = 0.0f;
    for (auto& filter : outputLowpass) filter.state = 0.0f;
    for (auto& filter : inputHighpassV2) filter.clear();
    for (auto& filter : outputHighpassV2) filter.clear();
    for (auto& saturator : gritV2) saturator.clear();
    for (std::size_t voice = 0; voice < shimmerLines.size(); ++voice)
    {
        shimmerLines[voice].clear();
        shimmerPhases[voice] = 0.25f * static_cast<float> (voice);
    }
    shimmerEnergyIn = shimmerEnergyOut = 0.0f;
    for (std::size_t i = 0; i < modulationPhases.size(); ++i)
        modulationPhases[i] = static_cast<float> (i) * 0.773f;
    envelope = 0.0f;
    current = target;
    bypassFade = target.bypass ? 1.0f : 0.0f;
    mixRampFrom = current.mix;
    mixRampLength = 1.0f;
    mixRampPosition = 0.0f;
    const float sr = static_cast<float> (sampleRate);
    predelayActive = predelayNext = target.predelay * sr * 0.001f;
    predelayFade = 1.0f;
    const float sizeFactor = 0.30f + current.size * 0.025f;
    for (std::size_t i = 0; i < tailLines.size(); ++i)
        tailLengths[i] = tailNext[i] = delaySeconds[i] * sr * sizeFactor;
    tailFade = 1.0f;
    returnTrimGain = decibelsToGain (current.returnTrim);
    dryGain = current.dry ? 1.0f : 0.0f;
    freezeAmount = current.freeze ? 1.0f : 0.0f;
    swellAmount = current.swell ? 1.0f : 0.0f;
    swellPhase = current.swellPhase;
    swellPhaseInput = current.swellPhase;
    eventCount = 0;
    eventLevel = 0.0f;
    triggerEnvelope = 0.0f;
    catchEnvelope = catchTarget = 0.0f;
    catchActiveSamples = catchQuietSamples = 0;
    coefficientCountdown = 0;
    updateCoefficients();
    excitationGain = excitationTarget;
}

void ReverbEngine::updateDelayTargets()
{
    const float sr = static_cast<float> (sampleRate);

    // Pre-delay: the read position is never slewed across large distances.
    const float predelayGoal = target.predelay * sr * 0.001f;
    if (predelayFade >= 1.0f && std::abs (predelayGoal - predelayActive) > 0.0f)
    {
        const float distance = std::abs (predelayGoal - predelayActive);
        if (distance <= predelayJumpLimit)
        {
            predelayActive += std::clamp (predelayGoal - predelayActive, -predelayGlideRate, predelayGlideRate);
        }
        else
        {
            predelayNext = predelayGoal;
            predelayFade = 0.0f;
            // Nearby taps are correlated (a linear fade keeps their level);
            // distant taps are not (equal power avoids a level dip).
            predelayEqualPower = distance >= predelayCorrelatedLimit;
        }
    }

    // Tail lengths follow Size per sample. All eight lines glide together, or
    // all eight crossfade together when any line would move too far to glide.
    if (tailFade >= 1.0f)
    {
        const float sizeFactor = 0.30f + target.size * 0.025f;
        std::array<float, 8> desired {};
        bool jump = false;
        bool moving = false;
        for (std::size_t i = 0; i < tailLines.size(); ++i)
        {
            desired[i] = delaySeconds[i] * sr * sizeFactor;
            // Frozen loops use whole-sample lengths, so reads are exact and lossless.
            if (target.freeze)
                desired[i] = std::round (desired[i]);
            const float distance = std::abs (desired[i] - tailLengths[i]);
            jump = jump || distance > tailJumpLimit;
            moving = moving || distance > 0.0f;
        }
        if (jump)
        {
            tailNext = desired;
            tailFade = 0.0f;
        }
        else if (moving)
        {
            for (std::size_t i = 0; i < tailLines.size(); ++i)
                tailLengths[i] += std::clamp (desired[i] - tailLengths[i], -tailGlideRate, tailGlideRate);
        }
    }
}

float ReverbEngine::predelayTap (std::size_t channel, float delaySamples, float original) const
{
    // Subsample predelay blends the current sample with the previous one.
    const float delayed = predelayLines[channel].read (std::max (1.0f, delaySamples));
    return delaySamples < 1.0f ? original + delaySamples * (delayed - original) : delayed;
}

void ReverbEngine::setSettings (const Settings& settings)
{
    target = sanitise (settings);
    if (! prepared)
        current = target;
}

void ReverbEngine::setTriggerEvents (const TriggerEvent* newEvents, int count)
{
    eventCount = 0;
    if (newEvents == nullptr)
        return;
    for (int i = 0; i < count && eventCount < static_cast<int> (events.size()); ++i)
    {
        auto event = newEvents[i];
        event.level = bounded (event.level, 0.0f, 100.0f, 0.0f);
        events[static_cast<std::size_t> (eventCount++)] = event;
    }
}

void ReverbEngine::hadamard (std::array<float, 8>& values)
{
    // Normalised Walsh-Hadamard matrix: orthogonal, so feedback cannot add energy.
    for (std::size_t stride = 1; stride < values.size(); stride *= 2)
        for (std::size_t start = 0; start < values.size(); start += stride * 2)
            for (std::size_t j = 0; j < stride; ++j)
            {
                const float a = values[start + j];
                const float b = values[start + j + stride];
                values[start + j] = a + b;
                values[start + j + stride] = a - b;
            }
    for (auto& value : values) value *= sqrtOneEighth;
}

void ReverbEngine::updateCoefficients()
{
    const float sr = static_cast<float> (sampleRate);
    lowCutCoefficient = std::exp (-2.0f * pi * std::min (current.lowCut, sr * 0.45f) / sr);
    highCutCoefficient = std::exp (-2.0f * pi * std::min (current.highCut, sr * 0.45f) / sr);
    if (current.version == SoundVersion::latest)
    {
        inputHighpassCoefficients.set (current.lowCut, lowCutQInput, sr);
        outputHighpassCoefficients.set (current.lowCut, lowCutQOutput, sr);
    }
    float feedbackEnergy = 0.0f;
    for (std::size_t i = 0; i < tailLines.size(); ++i)
    {
        // During a Size crossfade, decay follows the blended loop length.
        const float length = tailFade < 1.0f
            ? tailLengths[i] + tailFade * (tailNext[i] - tailLengths[i])
            : tailLengths[i];
        // RT60 is measured in seconds, independently of room size and sample rate.
        feedbackGains[i] = std::exp (-6.907755279f * length / (sr * current.decay));
        feedbackEnergy += feedbackGains[i] * feedbackGains[i];
    }
    // Balance energy when decay/size changes: long, tiny rooms otherwise become
    // much louder than short rooms because they recirculate far more energy.
    excitationTarget = std::sqrt (std::max (0.001f, 1.0f - feedbackEnergy * 0.125f));
    mixWet = current.mix * 0.01f;
    outputGain = current.output == 0.0f ? 1.0f : std::pow (10.0f, current.output / 20.0f);
}

float ReverbEngine::shimmerProcess (std::size_t voice, float input)
{
    // Two read heads, half a window apart, sweep the delay at the pitch ratio's
    // rate. Their raised-cosine windows always sum to one, so the shifted signal
    // never exceeds the recent input: the shimmer path cannot add loop gain.
    auto& line = shimmerLines[voice];
    auto& phase = shimmerPhases[voice];
    line.push (input);
    const float ratio = std::pow (2.0f, current.shimmerPitch / 12.0f);
    phase += std::abs (1.0f - ratio) / shimmerWindow;
    phase -= std::floor (phase);
    float output = 0.0f;
    for (int head = 0; head < 2; ++head)
    {
        float position = phase + 0.5f * static_cast<float> (head);
        position -= std::floor (position);
        const float delay = 1.0f + shimmerWindow * (ratio > 1.0f ? 1.0f - position : position);
        const float window = std::sin (pi * position);
        output += window * window * line.read (delay);
    }
    return output;
}

void ReverbEngine::updateTrigger (float catchDetector)
{
    // Auto-catch: a phrase that played for a while and then stopped opens a throw.
    if (current.autoCatch)
    {
        const float coefficient = catchDetector > catchEnvelope ? catchAttack : catchRelease;
        catchEnvelope = noDenormal (catchDetector + coefficient * (catchEnvelope - catchDetector));
        const float threshold = std::pow (10.0f, current.catchThreshold / 20.0f);
        if (catchEnvelope > threshold)
        {
            catchActiveSamples = std::min (catchActiveSamples + 1, catchMinimumPhrase);
            catchQuietSamples = 0;
        }
        else if (catchActiveSamples >= catchMinimumPhrase && ++catchQuietSamples >= catchGap)
        {
            catchTarget = current.triggerAmount; // one-shot: rise, then release
            catchActiveSamples = 0;
            catchQuietSamples = 0;
        }
        else if (catchActiveSamples < catchMinimumPhrase && catchEnvelope <= threshold)
        {
            catchActiveSamples = 0;
        }
    }
    else
    {
        catchTarget = 0.0f;
        catchActiveSamples = catchQuietSamples = 0;
    }

    const float held = current.triggerHeld ? current.triggerAmount : 0.0f;
    const float goal = std::max ({ eventLevel, held, catchTarget });
    const float sr = static_cast<float> (sampleRate);
    if (triggerEnvelope < goal)
    {
        const float attackMs = std::max (minimumTriggerAttackMs, current.triggerAttack);
        triggerEnvelope = std::min (goal, triggerEnvelope + 100.0f / (sr * attackMs * 0.001f));
    }
    else if (triggerEnvelope > goal)
    {
        triggerEnvelope = std::max (goal, triggerEnvelope - 100.0f / (sr * current.triggerRelease * 0.001f));
    }
    if (catchTarget > 0.0f && triggerEnvelope >= catchTarget)
        catchTarget = 0.0f; // the catch reached its level: let it release
}

void ReverbEngine::process (float* const* channels, int numChannels, int numSamples,
                            const float* const* sidechain, int sidechainChannels)
{
    if (! prepared || channels == nullptr || numChannels < 1 || numSamples <= 0
        || channels[0] == nullptr || (numChannels > 1 && channels[1] == nullptr))
    {
        eventCount = 0;
        return;
    }
    const bool stereo = numChannels > 1;
    const bool hasSidechain = sidechain != nullptr && sidechainChannels > 0 && sidechain[0] != nullptr
                           && (sidechainChannels < 2 || sidechain[1] != nullptr);
    const float sr = static_cast<float> (sampleRate);
    // Host automation arrives once per block. Spread each THROW change across
    // the whole block instead of stepping to it, bounded by the 4 ms full-range
    // rate (a larger jump continues into following blocks at that rate).
    const float maxMixStep = 100.0f / (sr * 0.004f);
    mixRampFrom = current.mix;
    mixRampLength = std::max (static_cast<float> (numSamples), std::ceil (std::abs (target.mix - mixRampFrom) / maxMixStep));
    mixRampPosition = 0.0f;
    // Per-block controls that need no smoothing beyond their own ramps.
    current.version = target.version;
    current.dry = target.dry;
    current.safeClip = target.safeClip;
    current.freeze = target.freeze;
    current.shimmerPitch = target.shimmerPitch;
    current.duckExternal = target.duckExternal;
    current.duckThreshold = target.duckThreshold;
    current.triggerAttack = target.triggerAttack;
    current.triggerRelease = target.triggerRelease;
    current.triggerAmount = target.triggerAmount;
    current.triggerHeld = target.triggerHeld;
    current.autoCatch = target.autoCatch;
    current.catchThreshold = target.catchThreshold;
    current.swell = target.swell;
    current.swellPhaseStep = target.swellPhaseStep;
    if (target.swell && std::abs (target.swellPhase - swellPhaseInput) > 0.0)
    {
        // A new host position re-syncs the cycle; otherwise it runs on by itself.
        swellPhase = target.swellPhase;
        swellPhaseInput = target.swellPhase;
    }
    if (std::abs (target.duckRelease - envelopeReleaseMs) > 0.0f)
    {
        envelopeReleaseMs = target.duckRelease;
        envelopeRelease = static_cast<float> (std::exp (-1.0 / (sampleRate * (static_cast<double> (envelopeReleaseMs) / 1000.0))));
    }
    const bool latest = current.version == SoundVersion::latest;
    int nextEvent = 0;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto slew = [] (float& value, float destination, float speed)
        {
            const float previous = value;
            value += speed * (destination - value);
            if (value == previous || std::abs (destination - value) < 0.0001f)
                value = destination;
        };
        while (nextEvent < eventCount && events[static_cast<std::size_t> (nextEvent)].offset <= sample)
            eventLevel = events[static_cast<std::size_t> (nextEvent++)].level;
        const float previousOutput = current.output;
        if (std::abs (target.mix - current.mix) > 0.0f)
        {
            mixRampPosition += 1.0f;
            current.mix = mixRampPosition >= mixRampLength
                ? target.mix
                : mixRampFrom + (target.mix - mixRampFrom) * (mixRampPosition / mixRampLength);
        }
        mixWet = current.mix * 0.01f;
        bypassFade += std::clamp ((target.bypass ? 1.0f : 0.0f) - bypassFade, -bypassStep, bypassStep);
        slew (current.output, target.output, fastSlew);
        // Size and pre-delay are smoothed by glide/crossfade of their read taps.
        current.size = target.size;
        current.predelay = target.predelay;
        updateDelayTargets();
        slew (current.decay, target.decay, parameterSlew);
        slew (current.lowCut, target.lowCut, parameterSlew);
        slew (current.highCut, target.highCut, parameterSlew);
        slew (current.diffusion, target.diffusion, parameterSlew);
        slew (current.motion, target.motion, parameterSlew);
        slew (current.width, target.width, parameterSlew);
        slew (current.grit, target.grit, parameterSlew);
        slew (current.duck, target.duck, parameterSlew);
        if (current.output != previousOutput)
            outputGain = current.output == 0.0f ? 1.0f : std::pow (10.0f, current.output / 20.0f);
        if (std::abs (current.returnTrim - target.returnTrim) > 0.0f)
        {
            slew (current.returnTrim, target.returnTrim, parameterSlew);
            returnTrimGain = decibelsToGain (current.returnTrim);
        }
        slew (current.shimmer, target.shimmer, parameterSlew);
        if (--coefficientCountdown <= 0)
        {
            updateCoefficients();
            coefficientCountdown = 16;
        }
        slew (excitationGain, excitationTarget, parameterSlew);
        dryGain += std::clamp ((current.dry ? 1.0f : 0.0f) - dryGain, -dryStep, dryStep);
        freezeAmount += std::clamp ((current.freeze ? 1.0f : 0.0f) - freezeAmount, -freezeStep, freezeStep);
        swellAmount += std::clamp ((current.swell ? 1.0f : 0.0f) - swellAmount, -swellStep, swellStep);

        const float dryL = safeSample (channels[0][sample]);
        const float dryR = stereo ? safeSample (channels[1][sample]) : dryL;
        std::array<float, 2> input { std::clamp (dryL, -8.0f, 8.0f),
                                    std::clamp (dryR, -8.0f, 8.0f) };
        float detector = std::max (std::abs (input[0]), std::abs (input[1]));
        updateTrigger (detector);
        // The larger of THROW and the trigger envelope drives the wet return.
        if (triggerEnvelope > current.mix)
            mixWet = triggerEnvelope * 0.01f;
        if (current.duckExternal)
        {
            detector = 0.0f;
            if (hasSidechain)
                for (int channel = 0; channel < std::min (2, sidechainChannels); ++channel)
                    detector = std::max (detector, std::min (8.0f, std::abs (safeSample (sidechain[channel][sample]))));
        }
        const float envelopeCoefficient = detector > envelope ? envelopeAttack : envelopeRelease;
        envelope = noDenormal (detector + envelopeCoefficient * (envelope - detector));
        if (bypassFade > 0.0f)
        {
            // A bypassed reverb stops taking new input; its existing tail decays.
            const float feed = 1.0f - bypassFade;
            input[0] *= feed;
            input[1] *= feed;
        }
        float predelayOld = 1.0f, predelayNew = 0.0f;
        if (predelayFade < 1.0f)
        {
            if (predelayEqualPower)
            {
                predelayOld = std::cos (predelayFade * 0.5f * pi);
                predelayNew = std::sin (predelayFade * 0.5f * pi);
            }
            else
            {
                predelayOld = 1.0f - predelayFade;
                predelayNew = predelayFade;
            }
        }
        const float diffusionCoefficient = current.diffusion * 0.0072f;
        for (std::size_t channel = 0; channel < input.size(); ++channel)
        {
            if (latest)
                input[channel] = inputHighpassV2[channel].process (input[channel], inputHighpassCoefficients.a1,
                                                                   inputHighpassCoefficients.a2, inputHighpassCoefficients.a3,
                                                                   inputHighpassCoefficients.k);
            else
                for (auto& filter : inputHighpass[channel])
                    input[channel] = filter.highpass (input[channel], lowCutCoefficient);
            const float original = input[channel];
            input[channel] = predelayTap (channel, predelayActive, original);
            if (predelayFade < 1.0f)
                input[channel] = predelayOld * input[channel]
                               + predelayNew * predelayTap (channel, predelayNext, original);
            predelayLines[channel].push (original);
            for (auto& diffuser : diffusers[channel])
                input[channel] = diffuser.process (input[channel], diffusionCoefficient);
        }
        if (predelayFade < 1.0f)
        {
            predelayFade += delayFadeStep;
            if (predelayFade >= 1.0f)
            {
                predelayFade = 1.0f;
                predelayActive = predelayNext;
            }
        }

        std::array<float, 8> tail;
        float motionDepth = current.motion * 0.000019f * sr;
        if (freezeAmount > 0.0f)
            motionDepth *= 1.0f - freezeAmount; // a frozen tail holds still instead of slowly dulling
        const float motionSpeed = 0.11f + current.motion * 0.0038f;
        for (std::size_t i = 0; i < tail.size(); ++i)
        {
            const float modulation = std::sin (modulationPhases[i]) * motionDepth;
            modulationPhases[i] += 2.0f * pi * motionSpeed * motionRates[i] / sr;
            if (modulationPhases[i] >= 2.0f * pi) modulationPhases[i] -= 2.0f * pi;
            tail[i] = tailLines[i].read (tailLengths[i] + modulation);
            // Inside the feedback loop a linear (convex) crossfade can never add gain.
            if (tailFade < 1.0f)
                tail[i] += tailFade * (tailLines[i].read (tailNext[i] + modulation) - tail[i]);
        }
        if (tailFade < 1.0f)
        {
            tailFade += delayFadeStep;
            if (tailFade >= 1.0f)
            {
                tailFade = 1.0f;
                tailLengths = tailNext;
            }
        }

        // Two different orthogonal output vectors produce a decorrelated stereo tail.
        float wetL = (tail[0] + tail[1] - tail[2] + tail[3]
                    - tail[4] + tail[5] - tail[6] - tail[7]) * sqrtOneEighth;
        float wetR = (tail[0] - tail[1] + tail[2] + tail[3]
                    + tail[4] - tail[5] - tail[6] - tail[7]) * sqrtOneEighth;

        if (freezeAmount > 0.0f)
        {
            // Freeze: the loop becomes lossless (no damping, unity gain) and stops taking input.
            const float damping = highCutCoefficient * (1.0f - freezeAmount);
            for (std::size_t i = 0; i < tail.size(); ++i)
                tail[i] = tailDamping[i].lowpass (tail[i], damping)
                        * (feedbackGains[i] + freezeAmount * (1.0f - feedbackGains[i]));
        }
        else
        {
            for (std::size_t i = 0; i < tail.size(); ++i)
                tail[i] = tailDamping[i].lowpass (tail[i], highCutCoefficient) * feedbackGains[i];
        }
        hadamard (tail);
        if (current.shimmer > 0.0f)
        {
            // Four orthonormal directions of the feedback vector are each replaced
            // by a convex blend of themselves and their pitch-shifted copy, so the
            // loop gain stays below one while half the loop can climb or fall in pitch.
            // Freeze holds whatever is in the loop, so shimmer bows out while frozen.
            const float amount = current.shimmer * 0.008f * (1.0f - freezeAmount);
            float energyIn = 0.0f, energyOut = 0.0f;
            for (const auto value : tail) energyIn += value * value;
            for (std::size_t voice = 0; voice < shimmerRows.size(); ++voice)
            {
                float bus = 0.0f;
                for (std::size_t i = 0; i < tail.size(); ++i) bus += hadamardSign (shimmerRows[voice], i) * tail[i];
                const float change = amount * (shimmerProcess (voice, bus) - bus);
                for (std::size_t i = 0; i < tail.size(); ++i) tail[i] += hadamardSign (shimmerRows[voice], i) * change;
            }
            for (const auto value : tail) energyOut += value * value;
            // Blending a signal with its decorrelated, shifted copy loses power on
            // every pass. Restore the stage's short-term energy (never adding any):
            // with feedback gains below one the loop still decays at its set time.
            shimmerEnergyIn = noDenormal (energyIn + shimmerEnergyCoefficient * (shimmerEnergyIn - energyIn));
            shimmerEnergyOut = noDenormal (energyOut + shimmerEnergyCoefficient * (shimmerEnergyOut - energyOut));
            if (shimmerEnergyOut > 1.0e-12f)
            {
                const float correction = std::clamp (std::sqrt (shimmerEnergyIn / shimmerEnergyOut), 0.5f, 1.5f);
                for (auto& value : tail) value *= correction;
            }
        }
        const float feed = freezeAmount > 0.0f ? 1.0f - freezeAmount : 1.0f;
        for (std::size_t i = 0; i < tail.size(); ++i)
        {
            float injection = ((i & 1) == 0 ? input[0] : input[1]) * (0.5f * excitationGain);
            if (freezeAmount > 0.0f) injection *= feed;
            // The guard is normally inactive; protects against pathological input/automation.
            tailLines[i].push (std::clamp (safeSample (tail[i] + injection), -32.0f, 32.0f));
        }

        std::array<float, 2> wet { wetL, wetR };
        float mid = 0.0f, duckGain = 1.0f, wetMono = 0.0f;
        if (! latest)
        {
            // 0.1.5 tone: pre-return grit blend, two one-pole high-passes after it.
            const float drive = 1.0f + current.grit * 0.08f;
            const float saturationBlend = current.grit * 0.01f;
            for (std::size_t channel = 0; channel < wet.size(); ++channel)
            {
                wet[channel] = outputLowpass[channel].lowpass (wet[channel], highCutCoefficient);
                if (saturationBlend > 0.00001f)
                {
                    const float saturated = std::tanh (wet[channel] * drive) / std::sqrt (drive);
                    wet[channel] += saturationBlend * (saturated - wet[channel]);
                }
                // Filter after grit, too: saturation must never add sub energy to the wet return.
                for (auto& filter : outputHighpass[channel])
                    wet[channel] = filter.highpass (wet[channel], lowCutCoefficient);
            }
            mid = (wet[0] + wet[1]) * 0.5f;
            const float side = (wet[0] - wet[1]) * (0.005f * current.width);
            duckGain = 1.0f / (1.0f + current.duck * 0.12f * envelope);
            wetL = (mid + side) * duckGain * wetReturnGain;
            wetR = (mid - side) * duckGain * wetReturnGain;
            wetMono = mid * duckGain * wetReturnGain;
            if (returnTrimGain != 1.0f)
            {
                wetL *= returnTrimGain;
                wetR *= returnTrimGain;
                wetMono *= returnTrimGain;
            }
        }
        else
        {
            // Latest tone: duck in dB above a threshold, then return gain, then real
            // saturation at the heard level, then the second low-cut section.
            for (std::size_t channel = 0; channel < wet.size(); ++channel)
                wet[channel] = outputLowpass[channel].lowpass (wet[channel], highCutCoefficient);
            mid = (wet[0] + wet[1]) * 0.5f;
            const float side = (wet[0] - wet[1]) * (0.005f * current.width);
            if (current.duck > 0.0f)
            {
                const float level = 20.0f * std::log10 (envelope + 1.0e-9f);
                const float amount = std::clamp ((level - (current.duckThreshold - 12.0f)) / 12.0f, 0.0f, 1.0f);
                duckGain = std::pow (10.0f, -0.015f * current.duck * amount); // up to -30 dB
            }
            const float gain = duckGain * wetReturnGainV2 * returnTrimGain;
            wet[0] = (mid + side) * gain;
            wet[1] = (mid - side) * gain;
            // Grit sets the saturation ceiling: +9 dBFS (just above loud throws) to -6 dBFS.
            const double ceiling = std::pow (10.0, (9.0 - 0.15 * current.grit) / 20.0);
            for (std::size_t channel = 0; channel < wet.size(); ++channel)
            {
                if (current.grit > 0.01f)
                    wet[channel] = gritV2[channel].process (wet[channel], ceiling);
                else
                    gritV2[channel].previous = wet[channel];
                wet[channel] = outputHighpassV2[channel].process (wet[channel], outputHighpassCoefficients.a1,
                                                                  outputHighpassCoefficients.a2, outputHighpassCoefficients.a3,
                                                                  outputHighpassCoefficients.k);
            }
            wetL = wet[0];
            wetR = wet[1];
            wetMono = 0.5f * (wetL + wetR);
        }
        if (swellAmount > 0.0f)
        {
            // Reverse-style swell: rises through the cycle, then a 3 ms cut at the grid line.
            const double cycleSamples = current.swellPhaseStep > 0.0 ? 1.0 / current.swellPhaseStep : 1.0e9;
            const float position = static_cast<float> (swellPhase);
            const float fade = std::min (1.0f, static_cast<float> ((1.0 - swellPhase) * cycleSamples) / swellFadeSamples);
            const float shape = position * position * position * fade;
            const float gain = 1.0f + swellAmount * (shape - 1.0f);
            wetL *= gain;
            wetR *= gain;
            wetMono *= gain;
        }
        if (current.swellPhaseStep > 0.0)
        {
            swellPhase += current.swellPhaseStep;
            if (swellPhase >= 1.0) swellPhase -= 1.0;
        }

        // Exact dry only once bypass has fully faded in (or nothing is added).
        if (bypassFade >= 1.0f
            || (mixWet == 0.0f && outputGain == 1.0f && dryGain == 1.0f && ! current.safeClip))
        {
            channels[0][sample] = dryL;
            if (stereo) channels[1][sample] = dryR;
        }
        else
        {
            // THROW changes only the wet return. Keep the original attack/body
            // at unity while adding the independently excited reverb tail.
            const float sourceL = dryGain == 1.0f ? dryL : dryL * dryGain;
            const float sourceR = dryGain == 1.0f ? dryR : dryR * dryGain;
            float outL = safeSample ((sourceL + (stereo ? wetL : wetMono) * mixWet) * outputGain);
            float outR = stereo ? safeSample ((sourceR + wetR * mixWet) * outputGain) : 0.0f;
            if (current.safeClip)
            {
                outL = softClip (outL);
                outR = softClip (outR);
            }
            if (bypassFade > 0.0f)
            {
                // Bypass crossfades the processed output to the untouched input.
                outL += bypassFade * (dryL - outL);
                outR += bypassFade * (dryR - outR);
            }
            channels[0][sample] = outL;
            if (stereo)
                channels[1][sample] = outR;
        }
    }
    eventCount = 0;
}
} // namespace throwup
