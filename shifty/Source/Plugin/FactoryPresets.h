#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace shifty {

enum class EngineMode { original = 0, classic = 1, talk = 2, mouth = 3 };

/** One factory sound. Every field is recalled, so a preset sounds the same
    whatever was loaded before it. */
struct FactoryPreset {
    juce::String folder, name;
    EngineMode engine = EngineMode::talk;
    bool lowLatency = false;
    float shift = 0.0f, focus = 0.55f, bite = 0.35f, drive = 0.1f, mix = 1.0f;
    bool subSafe = true;
    float subCut = 120.0f, output = 0.0f;
    bool motion = false;
    int shape = 0, rate = 1;
    float depth = 3.0f;
    bool sync = true;
    float freeHz = 2.0f;
    int target = 0; // 0 Shift, 1 Vowel, 2 Both
    bool vowelColor = false;
    float morph = 2.0f;
    int voiceSet = 0;
    float character = 0.0f, talk = 0.0f;
    float envAmount = 0.0f, envAttack = 5.0f, envRelease = 150.0f;
    float yoiAmount = 0.0f, yoiFrom = 0.0f, yoiTo = 4.0f, yoiTime = 200.0f, yoiShift = 0.0f;

    // Builder helpers keep the bank readable.
    FactoryPreset& sh(float v) { shift = v; return *this; }
    FactoryPreset& fo(float v) { focus = v; return *this; }
    FactoryPreset& bi(float v) { bite = v; return *this; }
    FactoryPreset& dr(float v) { drive = v; return *this; }
    FactoryPreset& mx(float v) { mix = v; return *this; }
    FactoryPreset& sub(bool on, float hz = 120.0f) { subSafe = on; subCut = hz; return *this; }
    FactoryPreset& out(float db) { output = db; return *this; }
    FactoryPreset& lfo(int shapeIndex, int rateIndex, float depthSt, int targetIndex = 0) {
        motion = true; shape = shapeIndex; rate = rateIndex; depth = depthSt; sync = true; target = targetIndex; return *this;
    }
    FactoryPreset& lfoFree(int shapeIndex, float hz, float depthSt, int targetIndex = 0) {
        motion = true; shape = shapeIndex; freeHz = hz; depth = depthSt; sync = false; target = targetIndex; return *this;
    }
    FactoryPreset& vowel(float position, int set = 0) { morph = position; voiceSet = set; return *this; }
    FactoryPreset& macros(float characterAmount, float talkAmount) { character = characterAmount; talk = talkAmount; return *this; }
    FactoryPreset& env(float amount, float attackMs = 5.0f, float releaseMs = 150.0f) {
        envAmount = amount; envAttack = attackMs; envRelease = releaseMs; return *this;
    }
    FactoryPreset& yoi(float amount, float from, float to, float timeMs, float shiftSt = 0.0f) {
        yoiAmount = amount; yoiFrom = from; yoiTo = to; yoiTime = timeMs; yoiShift = shiftSt; return *this;
    }
    FactoryPreset& lowLat() { lowLatency = true; return *this; }
    FactoryPreset& colour() { vowelColor = true; return *this; }
};

/** The factory bank, in display order: folders, then sounds within a folder. */
const std::vector<FactoryPreset>& factoryBank();
juce::StringArray factoryFolders();

} // namespace shifty
