#pragma once

#include "ScratchPattern.h"
#include <array>
#include <string>
#include <vector>

namespace backScratch {

// Settings a factory preset defines besides its pattern. Choice indices match
// the host parameters (see BackScratchProcessor::makeLayout).
struct PresetSettings {
    int phraseDuration = 3;   // 0 = 1/2 beat ... 3 = 1 bar ... 7 = 16 bars
    int timing = 0;           // straight / dotted / triplet
    int engageQuantize = 0;   // 1/16, 1/8, 1/4, 1/2, 1 bar
    bool retrigger = false;
    int every = 0;            // every phrase, 2nd, 4th, 8th
    int chain = 0;            // off, A-B, A-D, A-H
    float mix = 100.0f;       // percent
    float depth = 100.0f;     // percent
    int split = 2;            // 120, 160, 200, 300 Hz
    int lookback = 0;         // off, 1 beat, 1 bar, 2 bars, 4 bars
};

using PatternBuild = ScratchPattern (*)();

// One entry of the factory browser. Classic entries refer to one of the 75
// original recipes: exact ones (folder "Classic/...") are the 0.10 presets
// unchanged; the others reuse the recipe with tuned 0.11 settings. New
// entries carry their own geometry (and optionally pattern slots B-H).
struct BankPreset {
    std::string folder, name, description;
    int classic = -1;
    bool exact = false;
    PresetSettings settings;
    PatternBuild build = nullptr;
    std::array<PatternBuild, 7> slots {};
};

const std::vector<BankPreset>& bankPresets();
// Folders in browser order; a "/" separates a subfolder.
const std::vector<std::string>& bankFolders();
// Index of the exact Classic entry for an original recipe.
int classicBankIndex(int factoryIndex);

} // namespace backScratch
