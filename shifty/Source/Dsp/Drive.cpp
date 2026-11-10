#include "Drive.h"

#include <cmath>

namespace shifty
{

namespace
{
constexpr double pi = juce::MathConstants<double>::pi;
constexpr double ln2 = 0.69314718055994531;

// ---------------------------------------------------------------------------
// Half-band coefficients (JUCE's polyphase allpass design, 2x stage).

struct HalfBandDesign
{
    std::array<float, HalfBand2x::maxStages> up {}, down {};
    int upStages = 0, upDirect = 0, downStages = 0, downDirect = 0;
};

const HalfBandDesign& halfBandDesign()
{
    static const HalfBandDesign design = []
    {
        HalfBandDesign d;
        const auto make = [] (float transition, float stopbandDb, std::array<float, HalfBand2x::maxStages>& c,
                              int& stages, int& direct)
        {
            auto structure = juce::dsp::FilterDesign<float>::designIIRLowpassHalfBandPolyphaseAllpassMethod (
                transition, stopbandDb);
            juce::Array<float> list;
            for (int i = 0; i < structure.directPath.size(); ++i)
                list.add (structure.directPath.getObjectPointer (i)->coefficients[0]);
            for (int i = 1; i < structure.delayedPath.size(); ++i)
                list.add (structure.delayedPath.getObjectPointer (i)->coefficients[0]);
            stages = std::min (list.size(), HalfBand2x::maxStages);
            for (int i = 0; i < stages; ++i)
                c[static_cast<size_t> (i)] = list[i];
            direct = stages - stages / 2; // as juce::dsp::Oversampling splits them
        };
        // Pass band to 0.44 fs, stop band from 0.56 fs at about -80 dB.
        make (0.06f, -80.0f, d.up, d.upStages, d.upDirect);
        make (0.06f, -80.0f, d.down, d.downStages, d.downDirect);
        return d;
    }();
    return design;
}

// ---------------------------------------------------------------------------
// Nonlinearities and their antiderivatives (double precision).

inline double logCosh (double x) noexcept
{
    const double a = std::abs (x);
    return a + std::log1p (std::exp (-2.0 * a)) - ln2;
}

// Asymmetric soft clipper: the positive swing clips at 1, the negative at 1/k.
inline double asymTanh (double v, double k) noexcept
{
    return v >= 0.0 ? std::tanh (v) : std::tanh (k * v) / k;
}

inline double asymTanhIntegral (double v, double k) noexcept
{
    return v >= 0.0 ? logCosh (v) : logCosh (k * v) / (k * k);
}

inline double hardClip (double v) noexcept { return juce::jlimit (-1.0, 1.0, v); }

inline double hardClipIntegral (double v) noexcept
{
    const double a = std::abs (v);
    return a <= 1.0 ? 0.5 * v * v : a - 0.5;
}

// First-order antiderivative anti-aliasing.
template <typename Curve, typename Integral>
inline double adaa (double previous, double current, Curve curve, Integral integral) noexcept
{
    const double d = current - previous;
    if (std::abs (d) > 1.0e-5)
        return (integral (current) - integral (previous)) / d;
    return curve (0.5 * (current + previous));
}

// ---------------------------------------------------------------------------
// Model voicing.

constexpr double tapeFeedback = 0.22;   // magnetic memory: output fed back into the core
constexpr double tubeNegative = 0.45;   // the negative swing clips at 1 / 0.45
constexpr double tubeBias = 0.3;        // operating point toward grid conduction
constexpr double tubeBiasShift = 0.25;  // bias shift per unit of positive envelope (at most 0.6 x this)
constexpr double tubeSag = 0.3;         // supply sag per unit of output envelope
constexpr double fuzzNegative = 0.5;    // diode pair: one diode one way, two the other
constexpr double fuzzStage = 2.6;       // transistor stage gain
constexpr double fuzzOffset = -0.08;    // transistor stage bias

/** Input gain of a model at a DRIVE amount, in dB. */
double gainDb (int model, double amount) noexcept
{
    const double a = juce::jlimit (0.0, 1.0, amount);
    switch (model)
    {
        case 1: return 18.0 * std::pow (a, 0.85);
        case 2: return 27.0 * std::pow (a, 0.85);
        case 3: return 36.0 * std::pow (a, 0.8);
        default: break;
    }
    return 0.0;
}

// Level compensation, dB on top of the inverse input gain, at DRIVE 0, 1/16 .. 1.
// Measured so a bass at about -12 dBFS RMS keeps its RMS level as DRIVE rises
// (Tests/DspSmoke.cpp, --calibrate-drive).
constexpr std::array<std::array<float, 17>, driveModelCount> compensationDb {{
    {{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    {{ 0.00f, 1.02f, 1.27f, 1.56f, 1.88f, 2.24f, 2.64f, 3.08f, 3.56f, 4.09f, 4.65f, 5.25f, 5.88f, 6.54f, 7.23f, 7.95f, 8.68f }},
    {{ 0.00f, 0.91f, 1.06f, 1.25f, 1.51f, 1.85f, 2.25f, 2.73f, 3.28f, 3.91f, 4.59f, 5.33f, 6.12f, 6.96f, 7.85f, 8.78f, 9.75f }},
    {{ 0.00f, -6.27f, -4.59f, -2.59f, -0.54f, 1.50f, 3.54f, 5.55f, 7.54f, 9.50f, 11.43f, 13.34f, 15.22f, 17.08f, 18.91f, 20.72f, 22.51f }},
}};

double makeupGain (int model, double amount) noexcept
{
    const double position = juce::jlimit (0.0, 1.0, amount) * 16.0;
    const int lower = juce::jlimit (0, 15, static_cast<int> (position));
    const double t = position - lower;
    const auto& table = compensationDb[static_cast<size_t> (juce::jlimit (0, driveModelCount - 1, model))];
    const double correction = (1.0 - t) * table[static_cast<size_t> (lower)] + t * table[static_cast<size_t> (lower + 1)];
    return juce::Decibels::decibelsToGain (correction - gainDb (model, amount), -200.0);
}

// Below this DRIVE the model blends in from the dry input, so DRIVE 0 is exactly dry.
constexpr float blendRange = 0.06f;
} // namespace

// ---------------------------------------------------------------------------
// Biquad designs (RBJ Audio EQ Cookbook).

void Biquad::setLowShelf (double rate, double frequency, double gainDb, double slope) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * pi * juce::jlimit (5.0, 0.45 * rate, frequency) / rate;
    const double c = std::cos (w), s = std::sin (w);
    const double alpha = s / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
    const double r = 2.0 * std::sqrt (A) * alpha;
    const double a0 = (A + 1.0) + (A - 1.0) * c + r;
    b0 = A * ((A + 1.0) - (A - 1.0) * c + r) / a0;
    b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * c) / a0;
    b2 = A * ((A + 1.0) - (A - 1.0) * c - r) / a0;
    a1 = -2.0 * ((A - 1.0) + (A + 1.0) * c) / a0;
    a2 = ((A + 1.0) + (A - 1.0) * c - r) / a0;
}

void Biquad::setHighShelf (double rate, double frequency, double gainDb, double slope) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * pi * juce::jlimit (5.0, 0.45 * rate, frequency) / rate;
    const double c = std::cos (w), s = std::sin (w);
    const double alpha = s / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
    const double r = 2.0 * std::sqrt (A) * alpha;
    const double a0 = (A + 1.0) - (A - 1.0) * c + r;
    b0 = A * ((A + 1.0) + (A - 1.0) * c + r) / a0;
    b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0;
    b2 = A * ((A + 1.0) + (A - 1.0) * c - r) / a0;
    a1 = 2.0 * ((A - 1.0) - (A + 1.0) * c) / a0;
    a2 = ((A + 1.0) - (A - 1.0) * c - r) / a0;
}

void Biquad::setPeak (double rate, double frequency, double gainDb, double q) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * pi * juce::jlimit (5.0, 0.45 * rate, frequency) / rate;
    const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
    const double a0 = 1.0 + alpha / A;
    b0 = (1.0 + alpha * A) / a0;
    b1 = -2.0 * c / a0;
    b2 = (1.0 - alpha * A) / a0;
    a1 = -2.0 * c / a0;
    a2 = (1.0 - alpha / A) / a0;
}

void Biquad::setLowPass (double rate, double frequency, double q) noexcept
{
    const double w = 2.0 * pi * juce::jlimit (5.0, 0.45 * rate, frequency) / rate;
    const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = (1.0 - c) * 0.5 / a0;
    b1 = (1.0 - c) / a0;
    b2 = (1.0 - c) * 0.5 / a0;
    a1 = -2.0 * c / a0;
    a2 = (1.0 - alpha) / a0;
}

// ---------------------------------------------------------------------------
// HalfBand2x

void HalfBand2x::reset() noexcept
{
    upState.fill (0.0f);
    downState.fill (0.0f);
    downDelay = 0.0f;
}

void HalfBand2x::up (float x, float& first, float& second) noexcept
{
    const auto& d = halfBandDesign();
    float in = x;
    for (int n = 0; n < d.upDirect; ++n)
    {
        const float alpha = d.up[static_cast<size_t> (n)];
        const float out = alpha * in + upState[static_cast<size_t> (n)];
        upState[static_cast<size_t> (n)] = in - alpha * out;
        in = out;
    }
    first = in;
    in = x;
    for (int n = d.upDirect; n < d.upStages; ++n)
    {
        const float alpha = d.up[static_cast<size_t> (n)];
        const float out = alpha * in + upState[static_cast<size_t> (n)];
        upState[static_cast<size_t> (n)] = in - alpha * out;
        in = out;
    }
    second = in;
}

float HalfBand2x::down (float first, float second) noexcept
{
    const auto& d = halfBandDesign();
    float in = first;
    for (int n = 0; n < d.downDirect; ++n)
    {
        const float alpha = d.down[static_cast<size_t> (n)];
        const float out = alpha * in + downState[static_cast<size_t> (n)];
        downState[static_cast<size_t> (n)] = in - alpha * out;
        in = out;
    }
    const float direct = in;
    in = second;
    for (int n = d.downDirect; n < d.downStages; ++n)
    {
        const float alpha = d.down[static_cast<size_t> (n)];
        const float out = alpha * in + downState[static_cast<size_t> (n)];
        downState[static_cast<size_t> (n)] = in - alpha * out;
        in = out;
    }
    const float result = (downDelay + direct) * 0.5f;
    downDelay = in;
    return result;
}

// ---------------------------------------------------------------------------
// DriveStage

void DriveStage::Analog::reset() noexcept
{
    for (auto* f : { &pre1, &pre2, &pre3, &post1, &post2, &tone })
        f->reset();
    oversampler.reset();
    previous = previous2 = feedback = 0.0;
    bias = sag = 0.0;
    dcIn = dcOut = 0.0;
}

void DriveStage::prepare (double sampleRate) noexcept
{
    (void) halfBandDesign(); // build the shared coefficients here, not on the audio thread
    sampleRate_ = (std::isfinite (sampleRate) && sampleRate > 1000.0) ? sampleRate : 48000.0;
    const double fs = sampleRate_;
    const double os = 2.0 * fs;
    dcCoefficient_ = std::exp (-2.0 * pi * 10.0 / fs);
    biasAttack_ = 1.0 - std::exp (-1.0 / (0.004 * os));
    biasRelease_ = 1.0 - std::exp (-1.0 / (0.120 * os));
    sagAttack_ = 1.0 - std::exp (-1.0 / (0.015 * os));
    sagRelease_ = 1.0 - std::exp (-1.0 / (0.200 * os));
    fadeLength_ = std::max (16, static_cast<int> (0.010 * fs));

    for (auto& a : analog)
    {
        a.pre1.setIdentity();
        a.pre2.setIdentity();
        a.pre3.setIdentity();
        a.post1.setIdentity();
        a.post2.setIdentity();
        a.tone.setIdentity();
    }
    auto& tape = analog[1];
    tape.pre1.setHighShelf (fs, 3000.0, 4.0);
    tape.post1.setHighShelf (fs, 3000.0, -4.0);

    auto& tube = analog[2];
    tube.pre1.setLowShelf (fs, 220.0, -8.0);
    tube.pre2.setPeak (fs, 1400.0, 2.5, 0.7);
    tube.post2.setPeak (fs, 1400.0, -2.5, 0.7);
    tube.post1.setLowShelf (fs, 220.0, 8.0);

    auto& fuzz = analog[3];
    // The fundamental drives the clipper too (only a gentle low cut), so a bass
    // note squares up into dense harmonics right through the vowel range.
    fuzz.pre1.setLowShelf (fs, 200.0, -4.0);
    fuzz.pre2.setPeak (fs, 900.0, 4.0, 0.7);
    fuzz.pre3.setLowPass (fs, std::min (12000.0, 0.4 * fs), 0.707); // the input cap: less fizz into the clipper
    fuzz.post1.setLowShelf (fs, 200.0, 4.0);
    fuzz.post2.setPeak (fs, 900.0, -2.0, 0.7);
    fuzz.tone.setLowPass (fs, std::min (13000.0, 0.42 * fs), 0.707);
    reset();
}

void DriveStage::reset() noexcept
{
    legacy.reset();
    for (auto& a : analog)
        a.reset();
    fadeFrom_ = -1;
    fadeLeft_ = 0;
    fresh_ = true;
}

double DriveStage::nonlinear (Analog& a, int model, double x, double gain, double amount) noexcept
{
    switch (model)
    {
        case 1:
        {
            // TAPE: soft core with magnetic memory.
            const double v = gain * (1.0 - tapeFeedback) * x + tapeFeedback * a.feedback;
            const double y = adaa (a.previous, v, [] (double t) { return std::tanh (t); }, logCosh);
            a.previous = v;
            a.feedback = y;
            return y;
        }
        case 2:
        {
            // TUBE: biased asymmetric triode, bias shift and sag.
            const double driven = gain * x / (1.0 + tubeSag * amount * a.sag);
            const double positive = std::max (0.0, driven);
            a.bias += (positive > a.bias ? biasAttack_ : biasRelease_) * (positive - a.bias);
            const double operating = tubeBias - tubeBiasShift * std::min (0.6, a.bias);
            const double v = driven + operating;
            const auto curve = [] (double t) { return asymTanh (t, tubeNegative); };
            const auto integral = [] (double t) { return asymTanhIntegral (t, tubeNegative); };
            const double y = adaa (a.previous, v, curve, integral) - curve (operating);
            a.previous = v;
            const double level = std::abs (y);
            a.sag += (level > a.sag ? sagAttack_ : sagRelease_) * (level - a.sag);
            return y;
        }
        case 3:
        {
            // FUZZ: diode pair, then a hard-clipping transistor stage.
            const double v = gain * x;
            const auto diode = [] (double t) { return asymTanh (t, fuzzNegative); };
            const auto diodeIntegral = [] (double t) { return asymTanhIntegral (t, fuzzNegative); };
            const double first = adaa (a.previous, v, diode, diodeIntegral);
            a.previous = v;
            const double w = fuzzStage * first + fuzzOffset;
            const double y = adaa (a.previous2, w, hardClip, hardClipIntegral) - hardClip (fuzzOffset);
            a.previous2 = w;
            return y;
        }
        default: break;
    }
    return x;
}

float DriveStage::processAnalog (Analog& a, int model, float input, float amount) noexcept
{
    const double drive = juce::jlimit (0.0, 1.0, static_cast<double> (amount));
    const double gain = juce::Decibels::decibelsToGain (gainDb (model, drive));
    const double emphasised = a.pre3.process (a.pre2.process (a.pre1.process (input)));
    float first, second;
    a.oversampler.up (static_cast<float> (emphasised), first, second);
    const auto y0 = static_cast<float> (nonlinear (a, model, first, gain, drive));
    const auto y1 = static_cast<float> (nonlinear (a, model, second, gain, drive));
    double y = a.oversampler.down (y0, y1);
    y = a.tone.process (a.post2.process (a.post1.process (y)));
    // DC block (the asymmetric curves make some).
    const double blocked = y - a.dcIn + dcCoefficient_ * a.dcOut;
    a.dcIn = y;
    a.dcOut = blocked;
    const double wet = blocked * makeupGain (model, drive);
    if (drive <= 0.00001)
        return input;
    const auto blend = static_cast<float> (std::min (1.0, drive / blendRange));
    const auto out = input + blend * (static_cast<float> (wet) - input);
    return std::isfinite (out) ? out : input;
}

float DriveStage::process (float input, float amount, int model) noexcept
{
    // The 0.5 drive always runs, so its state is current whichever model is on.
    const float soft = legacy.process (input, amount);
    model = juce::jlimit (0, driveModelCount - 1, model);
    if (fresh_)
    {
        activeModel_ = model;
        fresh_ = false;
    }
    else if (model != activeModel_)
    {
        fadeFrom_ = activeModel_;
        activeModel_ = model;
        fadeLeft_ = fadeLength_;
        if (model != 0)
            analog[static_cast<size_t> (model)].reset();
    }
    const auto run = [&] (int m) { return m == 0 ? soft : processAnalog (analog[static_cast<size_t> (m)], m, input, amount); };
    float out = run (activeModel_);
    if (fadeLeft_ > 0 && fadeFrom_ >= 0)
    {
        const float older = run (fadeFrom_);
        const float t = 1.0f - static_cast<float> (fadeLeft_) / static_cast<float> (fadeLength_);
        out = older + t * (out - older);
        if (--fadeLeft_ == 0)
            fadeFrom_ = -1;
    }
    return out;
}

float DriveStage::transferCurve (int model, float x, float amount) noexcept
{
    const double a = juce::jlimit (0.0, 1.0, static_cast<double> (amount));
    const double in = x;
    if (model <= 0 || a <= 0.00001)
    {
        if (a <= 0.00001)
            return x;
        // The 0.5 drive (AdaaDrive's curve).
        const double g = 1.0 + 4.0 * a;
        const double norm = 1.0 + 0.5 * a;
        return static_cast<float> (in + a * (std::tanh (in * g) / norm - in));
    }
    const double gain = juce::Decibels::decibelsToGain (gainDb (model, a));
    double y = in;
    switch (model)
    {
        case 1: y = std::tanh (gain * in); break;
        case 2: y = asymTanh (gain * in + tubeBias, tubeNegative) - asymTanh (tubeBias, tubeNegative); break;
        case 3: y = hardClip (fuzzStage * asymTanh (gain * in, fuzzNegative) + fuzzOffset) - hardClip (fuzzOffset); break;
        default: break;
    }
    const double blend = std::min (1.0, a / blendRange);
    return static_cast<float> (in + blend * (y * makeupGain (model, a) - in));
}

} // namespace shifty
