#pragma once

#include "ScratchPattern.h"
#include <initializer_list>

namespace backScratch {

// Writes a phrase from musical gestures on a normalized timeline (0..1 is the
// whole phrase). Gestures must be added left to right; gaps play dry input.
// Every gesture keeps the source speed between -2x and +2x and reads only
// audio that has already arrived, like the drawing tools. Used by the roll
// tool, the roll starter shapes and the 0.11 factory presets.
class PhraseBuilder {
public:
    explicit PhraseBuilder(double beats = 1.0) noexcept;

    // Live input (no effect) up to t.
    PhraseBuilder& dryTo(double t) noexcept;
    // Repeats the grain that starts at t0 until t1. The first pass plays live;
    // each later pass re-reads the same audio at its original pitch. gap is
    // the closed fraction at the end of every pass (crossfader chop).
    PhraseBuilder& roll(double t0, double t1, double grain, double gap = 0.0) noexcept;
    // As roll, but each successive section uses the next, shorter grain:
    // an accelerating roll that keeps hammering the hit at t0.
    PhraseBuilder& accelerate(double t0, double t1, std::initializer_list<double> grains, double gap = 0.0) noexcept;
    // After one live pass, the grain at t0 repeats backwards.
    PhraseBuilder& reverseRoll(double t0, double t1, double grain, double gap = 0.0) noexcept;
    // Slows from normal speed to a stop over [t0, t1], then stays silent
    // until silentUntil (if later than t1).
    PhraseBuilder& tapeStop(double t0, double t1, double silentUntil = 0.0) noexcept;
    // A stop followed by a spin back up to speed, still running late.
    PhraseBuilder& stopStart(double t0, double stopEnd, double t1) noexcept;
    // Half speed (an octave down) from t0, then back to live.
    PhraseBuilder& halfTime(double t0, double t1) noexcept;
    // A steady source speed between -2x and +2x over [t0, t1]. Speeds above
    // 1x start behind the live input and catch up by t1.
    PhraseBuilder& speed(double t0, double t1, double rate) noexcept;
    // A rounded push/pull. depth is a fraction of the phrase and is limited
    // to the 2x speed range. cut closes the crossfader on the return stroke.
    PhraseBuilder& baby(double t0, double t1, double depth, bool cut = false) noexcept;
    // One smooth backward stroke reaching depth, then back to live.
    PhraseBuilder& reverse(double t0, double t1, double depth) noexcept;
    // Live audio chopped by the crossfader: open for duty of every grain.
    PhraseBuilder& gate(double t0, double t1, double grain, double duty = 0.5) noexcept;
    // A slow baby stroke chopped open/closed every grain.
    PhraseBuilder& transformer(double t0, double t1, double depth, double grain) noexcept;
    // Re-plays audio from `back` (fraction of the phrase) earlier, at normal speed.
    PhraseBuilder& echo(double t0, double t1, double back, double gap = 0.0) noexcept;

    ScratchPattern finish() noexcept;

private:
    ScratchPattern pattern;
    double cursor = 0.0;
    void segment(double t0, double t1, double d0, double d1, bool curved = false,
                 float g0 = 1.0f, float g1 = 1.0f, float wet = 1.0f,
                 double c0 = 0.0, double c1 = 1.0) noexcept;
};

} // namespace backScratch
