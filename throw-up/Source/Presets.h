#pragma once
#include "ReverbEngine.h"
#include <array>
#include <cstddef>
#include <vector>

namespace throwup
{
// Choice indices shared by the host parameters, presets and tests.
enum class PredelaySync { ms = 0, n64, n32, n16, n8, n4, n2 };
enum class PredelayFeel { straight = 0, dotted, triplet };
enum class ShimmerPitch { down12 = 0, down5, up7, up12 };
enum class SwellLength { off = 0, quarter, half, bar, twoBars };
enum class ReleaseSync { ms = 0, n16, n8, n4, n2, bar, twoBars };

// Everything a room preset recalls. THROW, Output, Bypass and the performance
// controls (THROW! key, trigger mode, Freeze, Dry, Safe, Return trim, duck
// source) are never part of a room.
struct Room
{
    float size = 60.0f, decay = 2.4f, predelay = 24.0f, lowCut = 180.0f, highCut = 6500.0f;
    float diffusion = 75.0f, motion = 20.0f, width = 110.0f, grit = 10.0f, duck = 15.0f;
    PredelaySync predelaySync = PredelaySync::ms;
    PredelayFeel predelayFeel = PredelayFeel::straight;
    float shimmer = 0.0f;
    ShimmerPitch shimmerPitch = ShimmerPitch::up12;
    SwellLength swell = SwellLength::off;
    float duckRelease = 160.0f, duckThreshold = -24.0f;
    SoundVersion version = SoundVersion::latest;
};

struct FactoryPreset
{
    const char* name;
    const char* group;       // folder in the preset menu
    const char* description;
    Room room;
    bool legacy = false;     // the original 0.1.5 bank, played with the 0.1.5 sound
};

// Programs 0 .. legacyStart-1 are the current bank (by folder); programs
// legacyStart .. legacyStart+49 are the 0.1.5 bank in its original order, so a
// project saved with 0.1.5 recalls "factory program i" as legacyStart + i.
const std::vector<FactoryPreset>& factoryPresets();
int legacyPresetStart();
constexpr int legacyPresetCount = 50;
// Folder names of the current bank, in menu order.
const std::vector<const char*>& presetFolders();

// Tempo-dependent conversions (beats are quarter notes).
double predelayBeats (PredelaySync sync, PredelayFeel feel);
double swellBeats (SwellLength length, double beatsPerBar);
double releaseBeats (ReleaseSync sync, double beatsPerBar);
float shimmerSemitones (ShimmerPitch pitch);

// Engine settings for a room at a tempo (THROW 0, Output 0 dB, sync resolved).
Settings settingsFor (const Room& room, double bpm = 120.0, double sampleRate = 48000.0);
} // namespace throwup
