#include "Presets.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace throwup
{
namespace
{
struct LegacyRow
{
    const char* name;
    const char* group;
    const char* description;
    float size, decay, predelay, lowCut, highCut, diffusion, motion, width, grit, duck;
};

// The 0.1.5 bank, unchanged in values and order. It plays with the 0.1.5 sound.
const std::array<LegacyRow, 50> legacyBank {{
    { "Pocket Slap", "Small", "A tiny, dry-edged room for the last note of a bass phrase.", 8, 0.28f, 7, 180, 5800, 30, 4, 90, 12, 8 },
    { "Orange Crate", "Small", "Warm plywood reflections with a soft, low-mid knock.", 14, 0.42f, 12, 180, 3700, 62, 8, 100, 18, 18 },
    { "Tile Bite", "Small", "Bright, tightly packed reflections for talking basses.", 11, 0.36f, 3, 360, 12800, 42, 2, 116, 8, 6 },
    { "Pocket Lint", "Small", "A dark, fuzzy little space that hugs gritty bass stabs.", 18, 0.55f, 9, 300, 2100, 78, 17, 84, 52, 28 },
    { "Airlock", "Small", "A narrow pressure chamber with a clean, delayed entrance.", 20, 0.62f, 34, 220, 5400, 70, 6, 55, 4, 35 },
    { "Tin Teeth", "Small", "Low diffusion exposes a wiry ring around short bass hits.", 6, 0.72f, 0, 520, 9800, 8, 0, 125, 40, 0 },
    { "Duck Booth", "Small", "A compact room that gets out of the way while the bass plays.", 22, 0.68f, 16, 200, 6200, 84, 12, 110, 10, 82 },
    { "Mono Nook", "Small", "Centered, dark ambience for a tightly focused bass break.", 12, 0.48f, 21, 260, 2900, 66, 0, 0, 24, 16 },
    { "Static Closet", "Small", "A fluttery, overdriven chamber for electrical mid-bass.", 24, 0.82f, 4, 420, 7200, 22, 58, 132, 72, 12 },
    { "Glass Pebble", "Small", "A polished, airy tail that clears before the next phrase.", 16, 0.58f, 27, 600, 16200, 92, 10, 145, 0, 24 },

    { "Back Room", "Medium", "A balanced first stop for weighty, syncopated bass throws.", 36, 1.15f, 24, 210, 6400, 76, 18, 110, 14, 30 },
    { "Concrete Mouth", "Medium", "Grainy warehouse edges with a low-mid emphasis.", 42, 1.45f, 18, 170, 4200, 48, 9, 120, 44, 22 },
    { "Velvet Vent", "Medium", "Dark and dense, with a gentle movement around the source.", 38, 1.75f, 38, 280, 2300, 95, 32, 128, 16, 48 },
    { "Chrome Alley", "Medium", "A bright, lean passageway for raspy formant basses.", 46, 1.28f, 46, 480, 11600, 52, 15, 138, 26, 20 },
    { "Elastic Room", "Medium", "A distinctly moving room for bent and rubbery bass notes.", 40, 1.9f, 11, 300, 6800, 82, 72, 125, 22, 24 },
    { "Side Pocket", "Medium", "Wide upper mids, tucked behind a clean bass attack.", 32, 1.35f, 62, 560, 8900, 88, 24, 150, 6, 55 },
    { "Underpass", "Medium", "A gritty concrete pocket with exposed early reflections.", 49, 1.62f, 28, 190, 3600, 28, 7, 105, 58, 28 },
    { "Soft Collision", "Medium", "Dense, softened reflections for aggressive reese endings.", 35, 1.05f, 6, 330, 5100, 100, 26, 118, 32, 62 },
    { "Bent Stairwell", "Medium", "A spiraling, slightly metallic space with lively motion.", 44, 2.1f, 33, 390, 10100, 35, 54, 142, 38, 14 },
    { "After You", "Medium", "A delayed room that blossoms in the gap after the bass.", 47, 1.85f, 105, 250, 7600, 85, 20, 124, 12, 72 },

    { "Bass Hangar", "Large", "A broad, controlled warehouse tail for emphatic phrase endings.", 62, 2.8f, 36, 220, 6500, 82, 22, 128, 18, 42 },
    { "Rust Cathedral", "Large", "A rough, dark hall for distorted mid-bass punctuation.", 72, 3.65f, 52, 280, 3300, 58, 28, 136, 66, 34 },
    { "Neon Gallery", "Large", "Bright, smooth upper-mid space with a wide stereo finish.", 60, 2.95f, 68, 500, 13200, 94, 32, 150, 8, 48 },
    { "Concrete Bloom", "Large", "A dense tail that rises behind sustained bass and releases slowly.", 68, 3.2f, 22, 260, 5600, 100, 18, 125, 26, 85 },
    { "Hollow Giant", "Large", "Sparse, cavernous reflections that preserve a grainy character.", 77, 4.1f, 14, 190, 4700, 18, 4, 116, 35, 20 },
    { "Liquid Atrium", "Large", "A flowing, animated hall for watery and stretched bass tones.", 65, 3.55f, 44, 350, 8400, 88, 76, 140, 20, 38 },
    { "Highline", "Large", "An elevated, airy throw with most low mids removed.", 58, 2.45f, 92, 780, 17000, 91, 40, 145, 4, 60 },
    { "Smoke Chamber", "Large", "Muted high frequencies and a thick, softly driven decay.", 70, 4.4f, 30, 240, 1850, 96, 45, 130, 48, 54 },
    { "Wide Exit", "Large", "A clean, open tail with room for the original transient.", 74, 3.15f, 125, 320, 9300, 80, 12, 150, 0, 65 },
    { "Iron Lung", "Large", "Compressed-feeling grit and strong ducking for dense drops.", 67, 3.8f, 20, 410, 5900, 68, 35, 122, 85, 94 },

    { "Event Horizon", "Huge", "A vast, dark decay for the empty space after a drop.", 94, 9.5f, 78, 260, 3100, 96, 30, 145, 18, 55 },
    { "Orbit Debris", "Huge", "Long, scattered reflections orbit a rough-edged bass hit.", 88, 7.2f, 42, 380, 9200, 26, 48, 150, 64, 25 },
    { "Amber Ocean", "Huge", "A warm, densely moving wash with a slow and generous decay.", 100, 11.0f, 115, 300, 4900, 100, 68, 144, 28, 62 },
    { "Empty Terminal", "Huge", "A cold, towering room with a clearly separated entrance.", 90, 6.4f, 165, 420, 11800, 72, 14, 137, 6, 38 },
    { "Subtracted Sky", "Huge", "An immense, airy tail that stays above the body of the bass.", 96, 12.5f, 92, 900, 17500, 95, 45, 150, 0, 68 },
    { "Deep End", "Huge", "A slow, centered cavern with tightly shaded upper frequencies.", 85, 8.8f, 35, 160, 1600, 88, 20, 80, 34, 48 },
    { "Night Reservoir", "Huge", "A huge, breathing dark space for sparse halftime arrangements.", 98, 14.0f, 140, 340, 2600, 100, 58, 132, 22, 88 },
    { "Satellite Ghost", "Huge", "A distant, shifting tail with a very late arrival.", 92, 10.5f, 225, 610, 10800, 84, 84, 148, 10, 72 },
    { "Endless Stair", "Huge", "An intentionally exposed, towering network of reflections.", 100, 16.0f, 8, 450, 6800, 12, 8, 125, 46, 20 },
    { "Last Light", "Huge", "A smooth maximum-length send-off for a final bass note.", 97, 18.0f, 185, 520, 7600, 98, 38, 150, 12, 80 },

    { "Sewer Halo", "Creative", "A filthy small space with an unusually long, wobbling tail.", 7, 6.8f, 18, 420, 3200, 16, 100, 145, 100, 34 },
    { "Rubber Universe", "Creative", "Maximum motion stretches a giant, diffuse bass afterimage.", 100, 7.7f, 65, 360, 7900, 98, 100, 150, 42, 62 },
    { "Needle Rain", "Creative", "Tiny, bright, low-diffusion reflections persist like metallic rain.", 2, 4.5f, 0, 1000, 18000, 0, 3, 146, 14, 0 },
    { "Late Regret", "Creative", "The maximum predelay leaves a dramatic gap before a dirty room.", 52, 3.8f, 250, 300, 6300, 60, 32, 135, 75, 45 },
    { "Vacuum Kiss", "Creative", "Maximum ducking makes a long tail surge into the rests.", 80, 8.4f, 10, 540, 4400, 100, 60, 150, 34, 100 },
    { "Phone In A Cave", "Creative", "A tightly band-limited mono tail for a strange, distant voice.", 86, 5.2f, 110, 1000, 1500, 78, 16, 0, 62, 12 },
    { "Crushed Velvet", "Creative", "Full wet drive inside a very dark, densely packed room.", 43, 2.6f, 6, 380, 1000, 100, 40, 118, 100, 74 },
    { "Wrong Elevator", "Creative", "A tall space with a short decay and unstable-feeling motion.", 100, 0.7f, 75, 260, 9600, 10, 95, 70, 55, 18 },
    { "Glitter Gutter", "Creative", "Bright grit, wide reflections, and strong motion leave a jagged trail.", 28, 5.5f, 48, 850, 15500, 38, 88, 150, 90, 42 },
    { "Orange Oblivion", "Creative", "A maximal, dirty, breathing cloud for the final impossible throw.", 100, 18.0f, 200, 620, 8800, 100, 100, 150, 88, 92 }
}};

Room legacyRoom (const LegacyRow& row)
{
    Room room;
    room.size = row.size; room.decay = row.decay; room.predelay = row.predelay;
    room.lowCut = row.lowCut; room.highCut = row.highCut; room.diffusion = row.diffusion;
    room.motion = row.motion; room.width = row.width; room.grit = row.grit; room.duck = row.duck;
    room.version = SoundVersion::v015;
    return room;
}

// The improved latest-sound version of a 0.1.5 room. 0.1.5's low cut sat about
// 2.5x above its label; the accurate cut at 2x the old label keeps the sub as
// clean while restoring some low-mid body. Grit now really saturates, so light
// settings move up a little and the heaviest come down; Duck is now in dB.
Room reworked (const LegacyRow& row)
{
    Room room = legacyRoom (row);
    room.version = SoundVersion::latest;
    room.lowCut = std::clamp (std::round (row.lowCut * 2.0f / 5.0f) * 5.0f, 80.0f, 1000.0f);
    room.grit = row.grit <= 0.0f ? 0.0f : std::min (90.0f, std::round (row.grit * 0.75f + 10.0f));
    // Same reduction as 0.1.5 at a typical mid-bass level (envelope about 0.35),
    // now constant above the threshold instead of growing with input level.
    room.duck = std::round (std::min (100.0f, 20.0f * std::log10 (1.0f + row.duck * 0.12f * 0.35f) / 0.3f));
    room.duckThreshold = -30.0f;
    room.duckRelease = 160.0f; // the 0.1.5 release
    return room;
}

// Hand-tuned exceptions to the rework rules, checked with the wet-level audit:
// heavily ducked or narrow-band rooms keep their bloom after the note.
void tune (const char* name, Room& room)
{
    const std::string n (name);
    if (n == "Pocket Lint") room.lowCut = 450;
    else if (n == "Mono Nook") room.lowCut = 400;
    else if (n == "Soft Collision") room.duck = 30;
    else if (n == "Iron Lung") { room.duck = 34; room.grit = 60; }
    else if (n == "Night Reservoir") room.duck = 36;
    else if (n == "Crushed Velvet") { room.duck = 28; room.lowCut = 600; }
    else if (n == "Orange Oblivion") room.duck = 38;
}

struct NewRow
{
    const char* name;
    const char* group;
    const char* description;
    Room room;
};

Room make (float size, float decay, float predelay, float lowCut, float highCut, float diffusion,
           float motion, float width, float grit, float duck)
{
    Room room;
    room.size = size; room.decay = decay; room.predelay = predelay; room.lowCut = lowCut;
    room.highCut = highCut; room.diffusion = diffusion; room.motion = motion; room.width = width;
    room.grit = grit; room.duck = duck; room.duckThreshold = -30.0f;
    return room;
}

Room synced (Room room, PredelaySync sync, PredelayFeel feel = PredelayFeel::straight)
{
    room.predelaySync = sync; room.predelayFeel = feel;
    return room;
}

Room shimmered (Room room, ShimmerPitch pitch, float amount)
{
    room.shimmerPitch = pitch; room.shimmer = amount;
    return room;
}

Room swelling (Room room, SwellLength length)
{
    room.swell = length;
    return room;
}

Room ducked (Room room, float release, float threshold)
{
    room.duckRelease = release; room.duckThreshold = threshold;
    return room;
}

std::vector<NewRow> newRooms()
{
    using PS = PredelaySync; using PF = PredelayFeel; using SP = ShimmerPitch; using SL = SwellLength;
    return {
        // Synced Throws: pre-delay locked to the host tempo, so the tail lands on the grid.
        { "Sixteenth Slap", "Synced Throws", "A tight room that answers each note a sixteenth later.", synced (make (18, 0.6f, 0, 300, 6000, 60, 6, 115, 18, 20), PS::n16) },
        { "Eighth Echo Room", "Synced Throws", "An eighth-note pre-delay turns the room into a rhythmic answer.", synced (make (34, 1.3f, 0, 320, 5500, 55, 10, 125, 22, 28), PS::n8) },
        { "Dotted Drop", "Synced Throws", "A dotted-eighth bloom that swings against straight riddim patterns.", synced (make (48, 2.2f, 0, 280, 6200, 70, 14, 135, 20, 36), PS::n8, PF::dotted) },
        { "Triplet Bounce", "Synced Throws", "Triplet-eighth arrival for bouncy, rolling mid-bass.", synced (make (30, 1.1f, 0, 350, 7500, 50, 12, 130, 24, 24), PS::n8, PF::triplet) },
        { "Quarter Hall", "Synced Throws", "A hall that enters on the next beat, clear of the original hit.", synced (make (70, 3.6f, 0, 260, 5200, 88, 18, 140, 14, 40), PS::n4) },
        { "Half-Time Gap", "Synced Throws", "A half-note gap, then a big dark space for halftime drops.", synced (make (82, 5.5f, 0, 300, 4400, 92, 22, 145, 12, 48), PS::n2) },
        { "Dotted Quarter Bloom", "Synced Throws", "A late, wide bloom a dotted quarter behind the bass.", synced (make (64, 4.2f, 0, 340, 6800, 90, 25, 142, 18, 44), PS::n4, PF::dotted) },
        { "Thirty-Second Rattle", "Synced Throws", "A tiny bright room on a thirty-second grid for rattling stabs.", synced (make (10, 0.45f, 0, 450, 9500, 30, 4, 120, 32, 10), PS::n32) },

        // Freeze Pads: long, dense, smooth rooms built for the FREEZE key.
        { "Ice Sheet", "Freeze Pads", "Press FREEZE on a held note for a smooth, wide pad.", make (90, 12.0f, 20, 300, 5000, 100, 30, 140, 12, 22) },
        { "Frozen Sub Choir", "Freeze Pads", "A dark, chorused hold that sits under the next phrase.", make (80, 9.0f, 12, 220, 2800, 96, 40, 130, 18, 30) },
        { "Glass Drone", "Freeze Pads", "A bright octave-lit drone; freeze it for a shimmering bed.", shimmered (make (70, 10.0f, 10, 600, 12000, 92, 20, 145, 10, 22), SP::up12, 20) },
        { "Static Hold", "Freeze Pads", "A gritty, buzzing hold for electrical, glitchy breaks.", make (50, 8.0f, 6, 400, 7000, 70, 55, 135, 50, 20) },
        { "Cold Storage", "Freeze Pads", "Huge and soft; freeze a stab into a slow, dark cloud.", make (95, 15.0f, 30, 260, 3600, 100, 15, 120, 14, 28) },
        { "Permafrost", "Freeze Pads", "The longest, widest room in the bank, made to be frozen.", make (100, 18.0f, 40, 350, 6200, 100, 35, 150, 22, 25) },

        // Shimmer: pitch inside the tail.
        { "Octave Halo", "Shimmer", "An octave-up halo rises out of the tail behind melodic basses.", shimmered (make (70, 6.0f, 24, 400, 10000, 95, 20, 145, 0, 30), SP::up12, 45) },
        { "Fifth Heaven", "Shimmer", "A fifth above the bass blooms into a bright, open chord.", shimmered (make (64, 5.0f, 30, 380, 9000, 92, 18, 140, 8, 30), SP::up7, 40) },
        { "Sub Shadow", "Shimmer", "An octave-down shadow: dark space-bass weight in the tail.", shimmered (make (75, 5.0f, 18, 180, 3000, 94, 16, 130, 18, 34), SP::down12, 40) },
        { "Falling Fourth", "Shimmer", "A fourth below the note sinks slowly into the reverb.", shimmered (make (60, 4.2f, 20, 300, 6000, 90, 22, 135, 10, 30), SP::down5, 35) },
        { "Riser Glow", "Shimmer", "Strong octave climb with a long tail, for build-ups.", shimmered (make (88, 9.0f, 40, 450, 12500, 98, 26, 150, 0, 40), SP::up12, 60) },
        { "Dark Mirror", "Shimmer", "A heavy octave-down mirror in a dark, dense room.", shimmered (make (82, 8.0f, 28, 250, 2400, 98, 30, 135, 24, 40), SP::down12, 60) },
        { "Crystal Throw", "Shimmer", "A short, synced shimmer throw that sparkles on the eighth.", synced (shimmered (make (40, 2.4f, 0, 420, 11000, 85, 12, 140, 6, 24), SP::up12, 30), PS::n8) },
        { "Nebula Drift", "Shimmer", "A huge, moving fifth-shimmer cloud for space-bass outros.", shimmered (make (100, 12.0f, 60, 360, 8000, 100, 60, 150, 10, 50), SP::up7, 55) },

        // Swells: a reverse-style rise that cuts on the bar line (needs THROW up).
        { "Bar Swell", "Swells", "The tail rises through each bar and cuts on the downbeat.", swelling (make (80, 6.0f, 20, 300, 6000, 95, 22, 140, 10, 0), SL::bar) },
        { "Two-Bar Rise", "Swells", "A long, two-bar suck-in for the gap before a drop.", swelling (make (95, 10.0f, 30, 320, 7000, 98, 28, 148, 12, 0), SL::twoBars) },
        { "Half-Bar Suck", "Swells", "A half-bar reverse pull for fills and transitions.", swelling (make (60, 3.0f, 10, 350, 6500, 90, 16, 135, 14, 0), SL::half) },
        { "Quarter Pump", "Swells", "Beat-length swells that pump the space on every quarter.", swelling (make (40, 1.8f, 0, 380, 7500, 85, 12, 130, 16, 0), SL::quarter) },
        { "Reverse Drop", "Swells", "A dark bar-long swell with an octave-down shadow.", swelling (shimmered (make (85, 7.0f, 20, 260, 4000, 96, 24, 140, 18, 0), SP::down12, 25), SL::bar) },
        { "Pre-Drop Wash", "Swells", "A bright two-bar wash with an octave-up glow into the drop.", swelling (shimmered (make (96, 14.0f, 40, 420, 11000, 100, 30, 150, 0, 0), SP::up12, 30), SL::twoBars) },

        // Sidechain Pumps: strong, timed ducking. Set Duck source to External for a kick or snare key.
        { "Kick Pocket", "Sidechain Pumps", "A medium tail that drops out under every hit and pumps back fast.", ducked (make (60, 3.0f, 18, 300, 6000, 88, 16, 135, 14, 80), 120, -30) },
        { "Snare Gate Hall", "Sidechain Pumps", "A big hall that clears for the snare and breathes back.", ducked (make (75, 4.5f, 24, 320, 6800, 92, 18, 140, 12, 90), 250, -24) },
        { "Halftime Breather", "Sidechain Pumps", "Slow, deep breathing for halftime riddim and wobble.", ducked (make (85, 6.0f, 30, 280, 5200, 95, 20, 140, 16, 75), 400, -30) },
        { "Sixteenth Pump", "Sidechain Pumps", "A fast pump on a sixteenth-synced room for busy patterns.", ducked (synced (make (40, 2.0f, 0, 350, 7000, 80, 10, 130, 18, 70), PS::n16), 60, -28) },
        { "Swing Duck", "Sidechain Pumps", "Dotted-eighth arrival with a swung, ducked release.", ducked (synced (make (55, 2.8f, 0, 330, 6400, 86, 14, 135, 16, 78), PS::n8, PF::dotted), 180, -26) },
        { "Wobble Breath", "Sidechain Pumps", "A moving room that ducks hard and wobbles back into gaps.", ducked (make (58, 3.4f, 12, 300, 6000, 84, 60, 138, 22, 65), 90, -24) },

        // Send FX: wet-only returns. Switch DRY off when THROW-UP sits on a return track.
        { "Return Hall", "Send FX", "A general-purpose, gently ducked hall for a send/return track (DRY off).", make (72, 3.4f, 30, 420, 7000, 92, 18, 130, 20, 26) },
        { "Return Plate", "Send FX", "A dense, bright, plate-like return for snares and stabs (DRY off).", make (30, 1.8f, 8, 520, 10000, 100, 12, 140, 22, 24) },
        { "Return Space", "Send FX", "A huge, wide return for big throws and atmospheres (DRY off).", make (96, 9.0f, 40, 480, 7000, 98, 30, 150, 18, 30) },
        { "Return Dirt", "Send FX", "A driven return to put OTT-style grit after the space (DRY off).", make (50, 2.5f, 12, 450, 6000, 80, 20, 130, 60, 22) },
        { "Return Shimmer", "Send FX", "An octave-up shimmer return for melodic layers (DRY off).", shimmered (make (80, 7.0f, 30, 560, 11000, 96, 22, 145, 16, 28), SP::up12, 40) },
        { "Return Slapback", "Send FX", "A synced sixteenth slap return for tight rhythmic doubling (DRY off).", synced (make (12, 0.4f, 0, 480, 8000, 25, 4, 120, 18, 18), PS::n16) }
    };
}

struct Bank
{
    std::vector<FactoryPreset> presets;
    std::vector<const char*> folders;
    int legacyStart = 0;
};

const Bank& bank()
{
    static const Bank instance = [] {
        Bank b;
        b.folders = { "Small", "Medium", "Large", "Huge", "Creative", "Synced Throws",
                      "Freeze Pads", "Shimmer", "Swells", "Sidechain Pumps", "Send FX" };
        for (const auto& row : legacyBank)
        {
            auto room = reworked (row);
            tune (row.name, room);
            b.presets.push_back ({ row.name, row.group, row.description, room, false });
        }
        for (const auto& row : newRooms())
            b.presets.push_back ({ row.name, row.group, row.description, row.room, false });
        b.legacyStart = static_cast<int> (b.presets.size());
        for (const auto& row : legacyBank)
            b.presets.push_back ({ row.name, row.group, row.description, legacyRoom (row), true });
        return b;
    }();
    return instance;
}
} // namespace

const std::vector<FactoryPreset>& factoryPresets() { return bank().presets; }
int legacyPresetStart() { return bank().legacyStart; }
const std::vector<const char*>& presetFolders() { return bank().folders; }

double predelayBeats (PredelaySync sync, PredelayFeel feel)
{
    double beats = 0.0;
    switch (sync)
    {
        case PredelaySync::n64: beats = 1.0 / 16.0; break;
        case PredelaySync::n32: beats = 1.0 / 8.0; break;
        case PredelaySync::n16: beats = 0.25; break;
        case PredelaySync::n8: beats = 0.5; break;
        case PredelaySync::n4: beats = 1.0; break;
        case PredelaySync::n2: beats = 2.0; break;
        case PredelaySync::ms: default: return 0.0;
    }
    if (feel == PredelayFeel::dotted) beats *= 1.5;
    if (feel == PredelayFeel::triplet) beats *= 2.0 / 3.0;
    return beats;
}

double swellBeats (SwellLength length, double beatsPerBar)
{
    switch (length)
    {
        case SwellLength::quarter: return 1.0;
        case SwellLength::half: return 2.0;
        case SwellLength::bar: return beatsPerBar;
        case SwellLength::twoBars: return 2.0 * beatsPerBar;
        case SwellLength::off: default: return 0.0;
    }
}

double releaseBeats (ReleaseSync sync, double beatsPerBar)
{
    switch (sync)
    {
        case ReleaseSync::n16: return 0.25;
        case ReleaseSync::n8: return 0.5;
        case ReleaseSync::n4: return 1.0;
        case ReleaseSync::n2: return 2.0;
        case ReleaseSync::bar: return beatsPerBar;
        case ReleaseSync::twoBars: return 2.0 * beatsPerBar;
        case ReleaseSync::ms: default: return 0.0;
    }
}

float shimmerSemitones (ShimmerPitch pitch)
{
    switch (pitch)
    {
        case ShimmerPitch::down12: return -12.0f;
        case ShimmerPitch::down5: return -5.0f;
        case ShimmerPitch::up7: return 7.0f;
        case ShimmerPitch::up12: default: return 12.0f;
    }
}

Settings settingsFor (const Room& room, double bpm, double sampleRate)
{
    Settings s;
    s.size = room.size; s.decay = room.decay; s.lowCut = room.lowCut; s.highCut = room.highCut;
    s.diffusion = room.diffusion; s.motion = room.motion; s.width = room.width; s.grit = room.grit;
    s.duck = room.duck; s.duckRelease = room.duckRelease; s.duckThreshold = room.duckThreshold;
    s.shimmer = room.shimmer; s.shimmerPitch = shimmerSemitones (room.shimmerPitch);
    s.version = room.version;
    const double beats = predelayBeats (room.predelaySync, room.predelayFeel);
    s.predelay = beats > 0.0 ? static_cast<float> (std::min (beats * 60000.0 / bpm, static_cast<double> (ReverbEngine::maximumPredelayMs)))
                             : room.predelay;
    const double swell = swellBeats (room.swell, 4.0);
    s.swell = swell > 0.0;
    s.swellPhaseStep = swell > 0.0 ? bpm / 60.0 / swell / sampleRate : 0.0;
    return s;
}
} // namespace throwup
