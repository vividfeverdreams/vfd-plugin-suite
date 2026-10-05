#pragma once

#include "ScratchPattern.h"

namespace backScratch {

// A stable graph height while drawing: one quarter of the phrase, or enough
// to retain the complete currently visible shape. All values are beat delays.
double patternEditingDelayRange(const ScratchPattern&) noexcept;

// Validate imported/custom trajectories before handing them to the renderer.
// Checks complete coverage, finite values, causal delay, gain bounds and the
// actual analytic readhead speed, including clipped smoothstep extrema.
bool validateEditedPattern(const ScratchPattern&) noexcept;

// Grid coordinates are the visible sixteen-cell display, including its shift.
// The first endpoint is the drag anchor. The destination is constrained to
// source speeds between -2x and +2x; appliedToDelay reports its actual height.
// The caller owns the mutable working copy; publish only complete snapshots.
// Invalid input or insufficient segment space leaves the pattern unchanged.
bool drawPatternGridLine(ScratchPattern&, int fromBoundary, double fromDelayBeats,
                         int toBoundary, double toDelayBeats,
                         double* appliedToDelay = nullptr) noexcept;

// Fractional visible positions [0, 1] follow the cursor instead of snapping to
// the sixteen-cell grid. A curved line eases the read speed at both ends. Its
// complete analytic derivative, rather than just its average, is constrained.
bool drawPatternLine(ScratchPattern&, double fromPhase, double fromDelayBeats,
                     double toPhase, double toDelayBeats, bool curved = false,
                     double* appliedToDelay = nullptr) noexcept;

// Resample freehand motion in space, not in mouse-event time. A slow machine
// and a fast mouse produce the same line; revisiting an area redraws that area.
// render always starts from the original phrase, so previews leave no trails.
class PatternFreeDrawStroke {
public:
    bool begin(const ScratchPattern&, double phase, double delayBeats) noexcept;
    bool append(double phase, double delayBeats) noexcept;
    bool render(ScratchPattern&, double* appliedDelay = nullptr) const noexcept;

private:
    static constexpr int resolution = 128;
    ScratchPattern original;
    std::array<double, resolution + 1> depths {};
    double range = 1.0;
    double anchorPhase = 0.0, anchorDepth = 0.0;
    double lastPhase = 0.0, lastDepth = 0.0;
    double firstPhase = 0.0, firstDepth = 0.0;
    double finalPhase = 0.0, finalDepth = 0.0;
    bool active = false;
};

// Small starting contours, separate from the factory preset bank. They keep
// the current timing and shift, and become ordinary editable custom geometry.
constexpr int scratchShapeCount = 10;
const char* scratchShapeName(int index) noexcept;
ScratchPattern makeScratchShape(const ScratchPattern& templatePattern, int index) noexcept;

// Erasing replaces the indicated inclusive cell range with live/dry audio.
// It does not mute the input, remove other cells, or change the grid shift.
bool erasePatternGridCells(ScratchPattern&, int firstCell, int lastCellInclusive) noexcept;

} // namespace backScratch
