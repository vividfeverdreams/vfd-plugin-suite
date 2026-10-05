#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <cstdint>
#include <optional>

namespace yoiboi
{
enum class Family { Rubber, Metal, Throat, Squeak };

// UI order: Mouth, Size, Bite, Dirt, Length, Sub. A true value protects that
// control and its associated synthesis settings during either random action.
using PatchLocks = std::array<bool, 6>;
[[nodiscard]] constexpr PatchLocks defaultPatchLocks() noexcept
{
    return { false, false, false, false, true, true };
}

struct Patch
{
    static constexpr int currentSchemaVersion = 1;
    static constexpr int currentRandomizerVersion = 2;
    static constexpr int currentSoundEngine = 3;
    int soundEngine = currentSoundEngine;

    juce::String name = "Tape Bounce";
    juce::String presetId;
    Family family = Family::Rubber;
    std::uint64_t seed = 0;
    int randomizerVersion = currentRandomizerVersion;

    float mouth = 0.94f;
    float size = 0.54f;
    float bite = 0.62f;
    float dirt = 0.14f;
    float lengthSeconds = 0.28f;
    float sub = 0.42f;
    int subMode = 1; // Off, Direct, Driven. Direct bypasses the character chain.
    bool lowCut = true;

    // Retain both timing modes and performance settings in presets/history.
    // These optional schema-1 properties preserve compatibility with v0.1.0.
    bool tempoSync = true;
    bool loop = false;
    float freeLengthMilliseconds = 280.0f;
    int lengthDivision = 1;
    int lengthFeel = 0; // Straight, triplet, dotted; division indices stay stable.
    float glideMilliseconds = 24.0f;
    float outputDecibels = -6.0f;

    // Vowel poses are EE, EH, AH, OH, OO = 0..4, matching YoiVoice.
    int startPose = 4;
    int bodyPose = 2;
    int endPose = 0;
    float fmRatio = 2.0f;
    float fmIndex = 2.2f;
    float resonance = 0.64f;
    float bodyTime = 0.32f;
    float curve = 0.40f;
};

[[nodiscard]] const char* familyName (Family family) noexcept;
[[nodiscard]] Patch makeDefaultPatch();

// These functions allocate strings and are intended for the message thread.
// The explicit seed, stable PRNG and saved resolved values make state recall
// independent of the current randomizer implementation.
[[nodiscard]] Patch newYoi (const Patch& current, const PatchLocks& locks,
                            std::uint64_t seed,
                            std::optional<Family> requestedFamily = std::nullopt);
[[nodiscard]] Patch mutatePatch (const Patch& current, const PatchLocks& locks,
                                 std::uint64_t seed, float amount = 0.25f);
[[nodiscard]] const std::array<Patch, 12>& factoryPresets();

// All DSP-facing values are finite and bounded after sanitizing/loading.
[[nodiscard]] Patch sanitizedPatch (Patch patch);
[[nodiscard]] juce::ValueTree toValueTree (const Patch& patch);
// Rejects unrelated or newer schemas. Missing properties in version 1 use
// defaults; malformed numeric values use safe defaults or bounded values.
[[nodiscard]] std::optional<Patch> fromValueTree (const juce::ValueTree& tree);
} // namespace yoiboi
