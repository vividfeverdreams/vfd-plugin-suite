#pragma once

#include <array>
#include <cstdint>

namespace backScratch {

// The display and pattern-shift buttons share exactly sixteen equal cells.
constexpr int patternGridDivisions = 16;

// All positions are quarter-note beats. Patterns read only already-arrived
// audio: delay >= 0. A rising delay slows/reverses the incoming audio.
struct ScratchPoint {
    double beat = 0.0;
    double delay = 0.0;
    float gate = 1.0f;
    float wet = 1.0f;
};

struct ScratchSegment {
    double start = 0.0, end = 1.0;
    double delayStart = 0.0, delayEnd = 0.0;
    float gateStart = 1.0f, gateEnd = 1.0f;
    float wet = 1.0f;
    bool curved = false;
    // Clipping a smoothstep retains its original domain, preserving both the
    // untouched curve and its analytic readhead speed exactly.
    double curveStart = 0.0, curveEnd = 1.0;
};

struct ScratchPattern {
    static constexpr int maximumSegments = 512;
    double beats = 4.0;
    std::array<ScratchSegment, maximumSegments> segments {};
    int count = 0;
    // Positive offsets move the complete effect to the right/later. Retiming
    // preserves this fraction of the phrase without changing any segment.
    int gridOffset = 0;
    // Once edited, retain the graph's vertical range across gestures. It is a
    // beat delay like segment depths and scales when the phrase is retimed.
    double editingRange = 0.0;
};

struct PatternSample {
    double delayBeats = 0.0;
    double playbackRate = 1.0;
    float gate = 1.0f, wet = 0.0f;
    int segment = 0;
};

struct FactoryPreset {
    const char* name;
    const char* category;
    const char* description;
};

constexpr int factoryPresetCount = 75;
const std::array<FactoryPreset, factoryPresetCount>& factoryPresets() noexcept;
// Version 1 reproduces saved pre-v0.10 recipes exactly. Version 2 gives new
// gestures continuous acceleration rather than sustained pitched ramps.
ScratchPattern makePattern(int factoryIndex, std::uint32_t variationSeed = 0,
                           int motionVersion = 2) noexcept;
// Seeded composition builds a complete phrase, independently of the factory
// bank. Both builders use only fixed storage and are safe on the audio thread.
ScratchPattern generatePattern(std::uint32_t seed, double phraseBeats, double barBeats = 4.0,
                               int motionVersion = 2) noexcept;
// Retime the existing trajectory once: time and source delay scale together.
// No strokes are added, repeated, trimmed, or regenerated.
ScratchPattern fitPatternToPhrase(const ScratchPattern&, double phraseBeats) noexcept;
int wrapPatternGridOffset(int offset) noexcept;
double patternGridStepBeats(const ScratchPattern&) noexcept;
double patternOffsetBeats(const ScratchPattern&) noexcept;
// The absolute phrase clock shown by the playhead: normalized [0,1), ignores
// gridOffset, and uses the same canonical loop boundary as audio evaluation.
double patternDisplayPhase(const ScratchPattern&, double songBeat) noexcept;
// Maps the displayed/host beat to the original segment coordinates. The UI
// uses the same mapping as playback when drawing intervals split at a seam.
double patternPhaseBeats(const ScratchPattern&, double songBeat) noexcept;
// Evaluate a known segment in original, unshifted coordinates. Endpoints are
// inclusive, so drawing the left side of a retrigger needs no nextafter trick.
PatternSample evaluatePatternSegment(const ScratchPattern&, int segmentIndex, double sourceBeat) noexcept;
PatternSample evaluatePattern(const ScratchPattern&, double songBeat) noexcept;

} // namespace backScratch
