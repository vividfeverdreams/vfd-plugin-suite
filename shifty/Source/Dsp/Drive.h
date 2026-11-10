#pragma once

#include "DspCommon.h"

#include <array>

namespace shifty
{

/** DRIVE models. SOFT is the 0.5 drive, kept bit-for-bit; it is the default
    for every project and preset made before 0.6.0. Append only. */
enum class DriveModel { soft = 0, tape = 1, tube = 2, fuzz = 3 };
constexpr int driveModelCount = 4;

/** Second-order section, transposed direct form II in double precision (the
    low shelves sit far below Nyquist). */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void reset() noexcept { z1 = z2 = 0.0; }
    double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    // RBJ cookbook designs. A shelf of +g dB and one of -g dB at the same
    // frequency and slope are exact inverses.
    void setLowShelf (double sampleRate, double frequency, double gainDb, double slope = 0.7) noexcept;
    void setHighShelf (double sampleRate, double frequency, double gainDb, double slope = 0.7) noexcept;
    void setPeak (double sampleRate, double frequency, double gainDb, double q) noexcept;
    void setLowPass (double sampleRate, double frequency, double q) noexcept;
    void setIdentity() noexcept { b0 = 1.0; b1 = b2 = a1 = a2 = 0.0; }
};

/** 2x oversampling with polyphase IIR half-band filters (allpass pairs), one
    sample at a time. No reported latency: about 1.5 samples of group delay
    at low frequencies, phase-only. Coefficients come from JUCE's half-band
    design and are shared by every instance. */
class HalfBand2x
{
public:
    static constexpr int maxStages = 8;
    void reset() noexcept;
    /** One input sample in, two samples at twice the rate out. */
    void up (float x, float& first, float& second) noexcept;
    /** Two samples at twice the rate in, one sample out. */
    float down (float first, float second) noexcept;

private:
    std::array<float, maxStages> upState {}, downState {};
    float downDelay = 0.0f;
};

/**
    DRIVE: the saturation in front of the formant engines.

    SOFT (model 0) is the 0.5 curve (AdaaDrive), bit-for-bit. The three analog
    models share one structure:

        pre-emphasis EQ -> input gain -> 2x up -> nonlinearity (ADAA) -> 2x down
        -> de-emphasis EQ -> DC block -> level compensation

    - TAPE: record pre-emphasis (+4 dB above 3 kHz) into a soft magnetic
      curve with a little magnetic memory (the output feeds back into the core,
      so the curve lags the input and opens into a hysteresis loop at high
      levels), then playback de-emphasis. Loud highs saturate first and come
      back compressed: tape's HF compression. Odd harmonics, glue.
    - TUBE: a triode gain stage. The lows are shelved down before the grid and
      restored after it, so they stay clean while the mids saturate. The curve
      is asymmetric (grid conduction clips the positive swing early, cutoff
      the negative swing late) and biased, for second-harmonic warmth. Loud
      notes shift the bias (grid current charging the coupling capacitor) and
      sag the supply, so the tone blooms and breathes with the playing.
    - FUZZ: a mid push (900 Hz lifted, the lows only slightly cut, so the
      fundamental itself squares up) into an asymmetric diode pair, then a
      transistor stage that clips hard; the lows restored and a gentle tone
      filter after. Dense odd harmonics through the vowel range: growl and spit.

    The nonlinearities use first-order antiderivative anti-aliasing at twice
    the sample rate. The level stays about the same as DRIVE rises (calibrated
    on bass at about -12 dBFS RMS), so DRIVE changes tone and density rather
    than loudness. DRIVE 0 passes the input untouched in every model.
    Changing model crossfades over 10 ms.

    prepare() off the audio thread; process() allocates nothing.
*/
class DriveStage
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    float process (float input, float amount, int model) noexcept;

    /** Small-signal-free static picture of a model for the UI: output for a
        steady input level x at the given drive (no memory, no bias shift). */
    static float transferCurve (int model, float x, float amount) noexcept;

private:
    struct Analog
    {
        Biquad pre1, pre2, pre3, post1, post2, tone;
        HalfBand2x oversampler;
        double previous = 0.0, previous2 = 0.0, feedback = 0.0;
        double bias = 0.0, sag = 0.0;
        double dcIn = 0.0, dcOut = 0.0;
        void reset() noexcept;
    };

    float processAnalog (Analog&, int model, float input, float amount) noexcept;
    double nonlinear (Analog&, int model, double x, double gain, double amount) noexcept;

    AdaaDrive legacy;
    std::array<Analog, driveModelCount> analog {};
    double sampleRate_ = 48000.0;
    double dcCoefficient_ = 0.999;
    double biasAttack_ = 0.0, biasRelease_ = 0.0, sagAttack_ = 0.0, sagRelease_ = 0.0;
    int activeModel_ = 0;
    int fadeFrom_ = -1;
    int fadeLength_ = 480;
    int fadeLeft_ = 0;
    bool fresh_ = true;
};

} // namespace shifty
