#pragma once

#include <array>

#include "Dsp/YoiVoice4.h"
#include "Patch/Patch.h"

namespace yoiboi
{
// Host parameter order. Never reorder or rename: hosts store automation by ID
// and some by index. 0.2.0 parameters are appended after "length_feel".
enum ParameterIndex : int
{
    pMouth, pSize, pBite, pDirt, pSub, pLengthMs, pLengthDivision, pSync, pLoop, pGlideMs, pOutputDb,
    pStartPose, pBodyPose, pEndPose, pBodyTime, pCurve, pSubMode, pLowCut, pLengthFeel,
    // 0.2.0
    pTalkMode, pTalkRate, pTalkFeel, pBendRange, pGlideMode, pPitchEnv, pPitchEnvMs, pWidth, pSubReset,
    pShaper, pSquash, pVowelStart, pVowelBody, pVowelEnd, pOnsetGlide, pConsonant, pConsonantLevel,
    pVelocityMouth, pVelocityBite, pVelocityDirt, pAttackMs, pReleaseMs, pMeterBars,
    parameterCount
};
constexpr int originalParameterCount = 19;

inline constexpr std::array<const char*, parameterCount> parameterIds {
    "mouth", "size", "bite", "dirt", "sub", "length_ms", "length_division", "sync",
    "loop", "glide_ms", "output_db", "start_pose", "body_pose", "end_pose", "body_time", "curve",
    "sub_mode", "low_cut", "length_feel",
    "talk_mode", "talk_rate", "talk_feel", "bend_range", "glide_mode", "pitch_env", "pitch_env_ms", "width",
    "sub_reset", "shaper", "squash", "vowel_start", "vowel_body", "vowel_end", "onset_glide", "consonant",
    "consonant_level", "vel_mouth", "vel_bite", "vel_dirt", "attack_ms", "release_ms", "meter_bars"
};

using ParameterValues = std::array<float, parameterCount>;

// Quarter-note beats for 1/16, 1/8, 1/4, 1/2 and 1 bar (4/4).
inline constexpr std::array<float, 5> syncedBeats { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
[[nodiscard]] float lengthFeelMultiplier (float feel) noexcept;

struct HostTiming
{
    double bpm = 120.0;       // already validated (finite, > 0)
    bool hasBpm = false;
    bool playing = false;
    bool hasPpq = false;
    double ppq = 0.0;         // quarter notes at the block's first sample
    int numerator = 4, denominator = 4;
};

// Plain (non-normalised) values for every parameter of a patch.
[[nodiscard]] ParameterValues parameterValuesFor (const Patch& patch);

// The controls one block renders with. Engines 1-3 receive exactly the values
// 0.1.6 computed; the extras are filled for engine 4 only.
[[nodiscard]] dsp::LatestControls controlsFor (const ParameterValues& values, const HostTiming& timing,
                                               int engine, double sampleRate,
                                               float fmRatio, float fmIndex, float resonance);

// The one-bar phrase PREVIEW plays and Bounce renders: sixteenth-note steps.
struct PhraseStep { int start, length, note; float velocity; };
inline constexpr std::array<PhraseStep, 8> phraseSteps { {
    { 0, 2, 36, 0.90f }, { 3, 1, 36, 0.65f }, { 4, 2, 43, 0.85f }, { 6, 2, 36, 0.75f },
    { 8, 3, 41, 0.90f }, { 11, 1, 36, 0.60f }, { 12, 2, 39, 0.85f }, { 14, 2, 36, 0.80f }
} };
constexpr int phraseSixteenths = 16;
} // namespace yoiboi
