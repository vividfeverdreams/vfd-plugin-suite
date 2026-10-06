#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

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

// Engine-4 vowel list. The first five keep the original EE, EH, AH, OH, OO
// indices, so an original pose converts to the same index.
enum Vowel : int { vowelEE, vowelEH, vowelAH, vowelOH, vowelOO, vowelIH, vowelUH, vowelAE, vowelER, vowelCount };

struct Patch
{
    static constexpr int currentSchemaVersion = 1;
    static constexpr int currentRandomizerVersion = 3;
    // 1: 0.1.0/0.1.1, 2: 0.1.2, 3: 0.1.3-0.1.6, 4: 0.2.0 ("latest"). Older
    // engines are preserved bit-exactly and ignore every engine-4 field below.
    static constexpr int currentSoundEngine = 4;
    static constexpr int firstExtendedEngine = 4;
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

    // ---- Engine 4 (0.2.0). Defaults reproduce the original behaviour. ----
    int talkMode = 0;          // Per note, synced talk, mod wheel, aftertouch
    int talkDivision = 1;      // 1/16, 1/8, 1/4, 1/2, 1 bar
    int talkFeel = 0;          // Straight, triplet, dotted
    int bendRange = 2;         // semitones, 1..24
    int glideMode = 0;         // Always, legato only
    float pitchEnvSemitones = 0.0f;      // starting offset that settles on the note
    float pitchEnvMilliseconds = 120.0f; // time to settle
    float width = 0.0f;        // body stereo width; the sub always stays mono
    bool subPhaseReset = false;
    int shaper = 0;            // Classic, Diode, Fold, Crush
    float squash = 0.0f;       // three-band upward/downward compression of the body
    int vowelStart = 4, vowelBody = 2, vowelEnd = 0; // Vowel enum
    int onsetGlide = 0;        // None, W, Y
    int consonant = 0;         // None, T, K, CH
    float consonantLevel = 0.5f;
    float velocityToMouth = 0.0f, velocityToBite = 0.0f, velocityToDirt = 0.0f;
    float attackMilliseconds = 3.0f, releaseMilliseconds = 75.0f;
    bool meterBars = false;    // opt-in: "1 Bar" follows the host time signature

    // Library metadata for factory sounds; not part of the sound or saved state.
    juce::String folder;
    juce::StringArray tags;
};

[[nodiscard]] const char* familyName (Family family) noexcept;
[[nodiscard]] const char* vowelName (int vowel) noexcept;
[[nodiscard]] Patch makeDefaultPatch();

// These functions allocate strings and are intended for the message thread.
// The explicit seed, stable PRNG and saved resolved values make state recall
// independent of the current randomizer implementation.
[[nodiscard]] Patch newYoi (const Patch& current, const PatchLocks& locks,
                            std::uint64_t seed,
                            std::optional<Family> requestedFamily = std::nullopt);
[[nodiscard]] Patch mutatePatch (const Patch& current, const PatchLocks& locks,
                                 std::uint64_t seed, float amount = 0.25f);
// The twelve original factory sounds (ids factory:0..11, engine 3). They
// recall exactly as in 0.1.3-0.1.6 and appear in the Library's Classic folder.
[[nodiscard]] const std::array<Patch, 12>& factoryPresets();
// Every factory sound: the twelve originals first (same ids), then the 0.2.0
// engine-4 bank (factory:12 onward), each with a Library folder and tags.
[[nodiscard]] const std::vector<Patch>& allFactoryPresets();
// Upgrades a sound to the latest engine without changing its settings: the
// original poses carry over to the engine-4 vowels and every new control
// keeps its neutral default.
[[nodiscard]] Patch upgradedToLatestEngine (Patch patch);

// All DSP-facing values are finite and bounded after sanitizing/loading.
[[nodiscard]] Patch sanitizedPatch (Patch patch);
[[nodiscard]] juce::ValueTree toValueTree (const Patch& patch);
// Rejects unrelated or newer schemas. Missing properties in version 1 use
// defaults; malformed numeric values use safe defaults or bounded values.
[[nodiscard]] std::optional<Patch> fromValueTree (const juce::ValueTree& tree);
} // namespace yoiboi
