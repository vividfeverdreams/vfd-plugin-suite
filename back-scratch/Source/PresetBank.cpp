#include "PresetBank.h"
#include "PhraseBuilder.h"

#include <algorithm>

namespace backScratch {
namespace {

// Note values as fractions of a one-bar (4/4) phrase.
constexpr double beat = 0.25, e8 = 0.125, s16 = 0.0625, t32 = 0.03125, s64 = 1.0 / 64.0;
constexpr double t16 = 1.0 / 24.0, t8 = 1.0 / 12.0;
// Choice indices.
constexpr int oneBar = 3, twoBars = 4, fourBars = 5, eightBars = 6;
constexpr int every2 = 1, every4 = 2, every8 = 3;
constexpr int quantizeBeat = 2, quantizeBar = 4;
constexpr int split120 = 0, split160 = 1;
constexpr int chainAB = 1, chainAD = 2, chainAH = 3;
constexpr int lookBeat = 1, lookBar = 2, lookTwoBars = 3;

ScratchPattern dryPhrase() { return PhraseBuilder().finish(); }

struct Bank {
    std::vector<BankPreset> presets;
    std::vector<std::string> folders;

    void folder(const std::string& name)
    {
        if (std::find(folders.begin(), folders.end(), name) == folders.end()) folders.push_back(name);
    }
    void add(const std::string& folderName, const char* name, const char* description,
             PresetSettings settings, PatternBuild build, std::array<PatternBuild, 7> slots = {})
    {
        folder(folderName);
        BankPreset preset;
        preset.folder = folderName; preset.name = name; preset.description = description;
        preset.settings = settings; preset.build = build; preset.slots = slots;
        presets.push_back(preset);
    }
    void classic(const std::string& folderName, int index, bool exact, PresetSettings settings, const char* description)
    {
        folder(folderName);
        BankPreset preset;
        preset.folder = folderName;
        preset.name = factoryPresets()[static_cast<std::size_t>(index)].name;
        preset.description = description;
        preset.classic = index; preset.exact = exact; preset.settings = settings;
        presets.push_back(preset);
    }
};

Bank makeBank()
{
    Bank bank;
    const auto& factory = factoryPresets();
    const auto category = [&factory](int index) { return std::string(factory[static_cast<std::size_t>(index)].category); };

    // The 75 original recipes, tuned for the 0.11 controls. Bass and drums
    // keep the sub below 120 Hz, so more of a growl or snare body is
    // scratched while the sub and kick fundamental stay solid.
    for (const char* group : {"BASS", "DRUMS", "VOCALS"}) {
        const std::string name = std::string(group) == "BASS" ? "Bass" : std::string(group) == "DRUMS" ? "Drums" : "Vocals";
        for (int index = 0; index < factoryPresetCount; ++index) {
            if (category(index) != group) continue;
            PresetSettings settings;
            if (name != "Vocals") settings.split = split120;
            bank.classic(name, index, false, settings, factory[static_cast<std::size_t>(index)].description);
        }
    }

    // Fills: one bar that plays only at the end of a 4- or 8-bar phrase, so it
    // can stay on the track. Engage snaps to the bar.
    const PresetSettings fill4 {oneBar, 0, quantizeBar, false, every4, 0, 100.0f, 100.0f, split120, 0};
    const PresetSettings fill8 {oneBar, 0, quantizeBar, false, every8, 0, 100.0f, 100.0f, split120, 0};
    const PresetSettings fill2 {oneBar, 0, quantizeBar, false, every2, 0, 100.0f, 100.0f, split120, 0};
    bank.add("Fills", "Bar 4 Rewind", "Every fourth bar, beat four rewinds one beat into the downbeat.",
             fill4, [] { return PhraseBuilder().reverse(0.75, 0.9375, 0.25).finish(); });
    bank.add("Fills", "Bar 8 Tape Stop", "Every eighth bar, the second half of the bar winds down to a stop.",
             fill8, [] { return PhraseBuilder().tapeStop(0.5, 1.0).finish(); });
    bank.add("Fills", "Last Beat Roll", "Every fourth bar, beat four rolls in sixteenths, then thirty-seconds.",
             fill4, [] { return PhraseBuilder().accelerate(0.75, 1.0, {s16, t32}).finish(); });
    bank.add("Fills", "Turnaround Chirps", "Every fourth bar, three triplet chirps close the phrase.",
             fill4, [] { return PhraseBuilder().baby(0.75, 0.75 + t8, 0.02, true).baby(0.75 + t8, 0.75 + 2 * t8, 0.02, true)
                                                .baby(0.75 + 2 * t8, 1.0, 0.025, true).finish(); });
    bank.add("Fills", "Pre-Drop Rewind", "Every eighth bar, a deep two-beat rewind pulls back before the drop.",
             fill8, [] { return PhraseBuilder().reverse(0.5, 1.0, 0.75).finish(); });
    bank.add("Fills", "Snare Flip", "Every second bar, the last beat flips its snare backwards in sixteenths.",
             fill2, [] { return PhraseBuilder().reverseRoll(0.75, 1.0, s16).finish(); });
    bank.add("Fills", "Half Bar Spin", "Every fourth bar, the back half drops to half speed.",
             fill4, [] { return PhraseBuilder().halfTime(0.5, 1.0).finish(); });
    bank.add("Fills", "Gate Rush", "Every fourth bar, beat four is chopped in thirty-seconds.",
             fill4, [] { return PhraseBuilder().gate(0.75, 1.0, t32, 0.5).finish(); });
    bank.add("Fills", "Double Back", "Every fourth bar, a short push on beat three and a rewind on beat four.",
             fill4, [] { return PhraseBuilder().baby(0.5, 0.625, 0.04).reverse(0.75, 1.0, 0.2).finish(); });
    bank.add("Fills", "Stop & Drop", "Every fourth bar, beat four stops dead and spins back up.",
             fill4, [] { return PhraseBuilder().stopStart(0.75, 0.875, 1.0).finish(); });

    // Rolls: beat-repeat style. The first pass plays live, later passes
    // re-read the same slice at its original pitch.
    const PresetSettings roll {oneBar, 0, 0, false, 0, 0, 100.0f, 100.0f, split120, 0};
    bank.add("Rolls", "Roll 1/8", "The second half of the bar repeats in eighth notes.",
             roll, [] { return PhraseBuilder().roll(0.5, 1.0, e8).finish(); });
    bank.add("Rolls", "Roll 1/16", "The second half of the bar repeats in sixteenths.",
             roll, [] { return PhraseBuilder().roll(0.5, 1.0, s16).finish(); });
    bank.add("Rolls", "Roll 1/32", "Beat four repeats in thirty-seconds.",
             roll, [] { return PhraseBuilder().roll(0.75, 1.0, t32).finish(); });
    bank.add("Rolls", "Accelerating Roll", "Eighths, sixteenths, then thirty-seconds over the second half.",
             roll, [] { return PhraseBuilder().accelerate(0.5, 1.0, {e8, s16, t32}).finish(); });
    bank.add("Rolls", "Triplet Roll", "The second half repeats in sixteenth-note triplets.",
             roll, [] { return PhraseBuilder().roll(0.5, 1.0, t16).finish(); });
    bank.add("Rolls", "Reverse Roll", "The second half repeats a sixteenth backwards.",
             roll, [] { return PhraseBuilder().reverseRoll(0.5, 1.0, s16).finish(); });
    bank.add("Rolls", "Chopped Roll", "Sixteenth repeats with a short crossfader gap between hits.",
             roll, [] { return PhraseBuilder().roll(0.5, 1.0, s16, 0.35).finish(); });
    bank.add("Rolls", "Off-Beat Rolls", "Each off-beat eighth stutters in thirty-seconds.",
             roll, [] { PhraseBuilder phrase;
                        for (int b = 0; b < 4; ++b) phrase.roll(b * beat + e8, (b + 1) * beat, t32);
                        return phrase.finish(); });
    bank.add("Rolls", "Machine Gun", "The last eighth fires a sixty-fourth-note burst.",
             roll, [] { return PhraseBuilder().roll(0.875, 1.0, s64).finish(); });
    bank.add("Rolls", "Riser Roll", "Two bars: quarters to thirty-seconds on one hit across the second bar.",
             PresetSettings {twoBars, 0, quantizeBar, false, 0, 0, 100.0f, 100.0f, split120, 0},
             [] { return PhraseBuilder().accelerate(0.5, 1.0, {beat / 2, e8 / 2, s16 / 2, t32 / 2}).finish(); });

    // Stutters: shorter repeats and chops inside the groove.
    const PresetSettings stutter {oneBar, 0, 0, false, 0, 0, 100.0f, 100.0f, split120, 0};
    bank.add("Stutters", "Beat Repeat 1/4", "Beats one and three each play twice.",
             stutter, [] { return PhraseBuilder().roll(0.0, 0.5, beat).roll(0.5, 1.0, beat).finish(); });
    bank.add("Stutters", "Sixteenth Stutter", "Beats two and four stutter in gapped sixteenths.",
             stutter, [] { return PhraseBuilder().roll(0.25, 0.5, s16, 0.3).roll(0.75, 1.0, s16, 0.3).finish(); });
    bank.add("Stutters", "Stutter Gate 1/32", "The second half is chopped in thirty-seconds.",
             stutter, [] { return PhraseBuilder().gate(0.5, 1.0, t32, 0.5).finish(); });
    bank.add("Stutters", "Double Tap", "The first eighth of every beat plays twice.",
             stutter, [] { PhraseBuilder phrase;
                           for (int b = 0; b < 4; ++b) phrase.roll(b * beat, (b + 1) * beat, e8);
                           return phrase.finish(); });
    bank.add("Stutters", "Stutter Edit", "Mixed sixteenth, thirty-second and sixty-fourth repeats.",
             stutter, [] { return PhraseBuilder().roll(0.0, 0.25, s16).roll(0.5, 0.625, t32)
                                                 .accelerate(0.75, 1.0, {s16, t32, s64}).finish(); });
    bank.add("Stutters", "Syncopated Repeat", "Off-beat eighths repeat against the groove.",
             stutter, [] { return PhraseBuilder().roll(0.1875, 0.4375, e8).roll(0.6875, 0.9375, e8).finish(); });
    bank.add("Stutters", "Triplet Stutter", "Beats two and four repeat in sixteenth triplets.",
             stutter, [] { return PhraseBuilder().roll(0.25, 0.5, t16).roll(0.75, 1.0, t16).finish(); });
    bank.add("Stutters", "Glitch Cut", "A thirty-second burst, a sixty-fourth gate, then a gapped roll.",
             stutter, [] { return PhraseBuilder().roll(0.5, 0.625, t32, 0.5).gate(0.625, 0.75, s64, 0.5)
                                                 .roll(0.75, 1.0, s16, 0.25).finish(); });

    // Scratches: new turntable gestures, plus tuned favourites from the bank.
    const PresetSettings scratch {oneBar, 0, 0, false, 0, 0, 100.0f, 100.0f, split120, 0};
    bank.add("Scratches", "Baby Pocket", "Two rounded baby scratches between the kick and snare.",
             scratch, [] { return PhraseBuilder().baby(0.125, 0.375, 0.08).baby(0.625, 0.875, 0.08).finish(); });
    bank.add("Scratches", "Chirp Flare", "Quick chirps with the crossfader closing on each return.",
             scratch, [] { return PhraseBuilder().baby(0.5, 0.5625, 0.02, true).baby(0.625, 0.6875, 0.02, true)
                                                 .baby(0.75, 0.875, 0.04, true).finish(); });
    bank.add("Scratches", "Transformer", "A slow push and pull chopped in thirty-seconds.",
             scratch, [] { return PhraseBuilder().transformer(0.5, 1.0, 0.12, t32).finish(); });
    bank.add("Scratches", "Tear Scratch", "A backward tear on beat two answered by a push.",
             scratch, [] { return PhraseBuilder().reverse(0.25, 0.5, 0.15).baby(0.5, 0.75, 0.07).finish(); });
    bank.add("Scratches", "Rubber Pull", "Two long elastic strokes across the bar.",
             scratch, [] { return PhraseBuilder().baby(0.0, 0.5, 0.12).baby(0.5, 1.0, 0.14).finish(); });
    bank.add("Scratches", "Crab Fill", "Beat four transformer chops in sixty-fourths.",
             scratch, [] { return PhraseBuilder().transformer(0.75, 1.0, 0.05, s64).finish(); });
    {
        PresetSettings deeper = scratch; deeper.depth = 125.0f;
        PresetSettings wide = scratch; wide.depth = 145.0f;
        PresetSettings tight = scratch; tight.depth = 70.0f;
        PresetSettings throat = scratch; throat.depth = 160.0f;
        const auto tuned = [&bank](int index, const char* name, PresetSettings settings, const char* description) {
            bank.folder("Scratches");
            BankPreset entry;
            entry.folder = "Scratches"; entry.name = name; entry.description = description;
            entry.classic = index; entry.settings = settings;
            bank.presets.push_back(entry);
        };
        tuned(0, "Sub Tug Lift", deeper, "Sub Tug with deeper strokes over a 120 Hz Keep Sub split.");
        tuned(1, "Rubber Teeth Wide", wide, "Rubber Teeth pulled 45% further.");
        tuned(5, "Switchblade Tight", tight, "Switchblade with short, tight strokes.");
        tuned(4, "Tape Throat Deep", throat, "Tape Throat bent 60% deeper.");
    }

    // Tape stops.
    const PresetSettings stop {oneBar, 0, 0, false, 0, 0, 100.0f, 100.0f, split120, 0};
    bank.add("Tape Stops", "Stop 1 Beat", "Beat four winds down to a stop.",
             stop, [] { return PhraseBuilder().tapeStop(0.75, 1.0).finish(); });
    bank.add("Tape Stops", "Stop Half Bar", "The second half winds down to a stop.",
             stop, [] { return PhraseBuilder().tapeStop(0.5, 1.0).finish(); });
    bank.add("Tape Stops", "Stop Full Bar", "Two bars: the second bar winds down to a stop.",
             PresetSettings {twoBars, 0, quantizeBar, false, 0, 0, 100.0f, 100.0f, split120, 0},
             [] { return PhraseBuilder().tapeStop(0.5, 1.0).finish(); });
    bank.add("Tape Stops", "Spin Down Spin Up", "Beat three stops, beat four spins back up.",
             stop, [] { return PhraseBuilder().stopStart(0.5, 0.75, 1.0).finish(); });
    bank.add("Tape Stops", "Power Cut", "A sixteenth-long stop, then silence to the bar line.",
             stop, [] { return PhraseBuilder().tapeStop(0.75, 0.8125, 1.0).finish(); });
    bank.add("Tape Stops", "Brake Tail", "A two-beat brake that leaves a short silence.",
             stop, [] { return PhraseBuilder().tapeStop(0.5, 0.875, 1.0).finish(); });

    // Builds: longer phrases that intensify into the next section.
    bank.add("Builds", "8 Bar Roll Build", "Eight bars: the last two roll in quarters down to thirty-seconds.",
             PresetSettings {eightBars, 0, quantizeBar, false, 0, 0, 100.0f, 100.0f, split120, 0},
             [] { return PhraseBuilder().accelerate(0.75, 1.0, {beat / 8, e8 / 8, s16 / 8, t32 / 8}).finish(); });
    bank.add("Builds", "4 Bar Riser Stutter", "Four bars: eighths, sixteenths, then thirty-seconds in the last bar.",
             PresetSettings {fourBars, 0, quantizeBar, false, 0, 0, 100.0f, 100.0f, split120, 0},
             [] { return PhraseBuilder().roll(0.75, 0.875, e8 / 4).roll(0.875, 0.9375, s16 / 4)
                                        .roll(0.9375, 1.0, t32 / 4).finish(); });
    bank.add("Builds", "Snare Build", "Two bars: the second bar hammers one hit faster and faster.",
             PresetSettings {twoBars, 0, quantizeBar, false, 0, 0, 100.0f, 100.0f, split120, 0},
             [] { return PhraseBuilder().accelerate(0.5, 1.0, {beat / 2, e8 / 2, s16 / 2, t32 / 2}).finish(); });
    bank.add("Builds", "Gate Build", "Four bars: the last bar chops faster every two beats.",
             PresetSettings {fourBars, 0, quantizeBar, false, 0, 0, 100.0f, 100.0f, split120, 0},
             [] { return PhraseBuilder().gate(0.75, 0.875, e8 / 4, 0.5).gate(0.875, 0.9375, s16 / 4, 0.5)
                                        .gate(0.9375, 1.0, t32 / 4, 0.5).finish(); });
    bank.add("Builds", "Drop Rewind", "Four bars: the last two beats rewind hard into the drop.",
             PresetSettings {fourBars, 0, quantizeBar, false, 0, 0, 100.0f, 100.0f, split120, 0},
             [] { return PhraseBuilder().reverse(0.875, 1.0, 0.2).finish(); });
    bank.add("Builds", "Tension Half-Time", "Two bars: the second bar drags at half speed.",
             PresetSettings {twoBars, 0, quantizeBar, false, 0, 0, 100.0f, 100.0f, split120, 0},
             [] { return PhraseBuilder().halfTime(0.5, 1.0).finish(); });

    // Half-time: speed and pitch drops for heavier sections.
    const PresetSettings half {oneBar, 0, 0, false, 0, 0, 100.0f, 100.0f, split120, 0};
    bank.add("Halftime", "Half-Time Drag", "The second half plays at half speed, an octave down.",
             half, [] { return PhraseBuilder().halfTime(0.5, 1.0).finish(); });
    bank.add("Halftime", "Half-Time Groove", "Each half bar starts live and drags to half speed.",
             half, [] { return PhraseBuilder().halfTime(0.0, 0.5).halfTime(0.5, 1.0).finish(); });
    bank.add("Halftime", "Slow Motion Tail", "Beat four drags at half speed.",
             half, [] { return PhraseBuilder().halfTime(0.75, 1.0).finish(); });
    bank.add("Halftime", "Half-Time Chop", "Beats three and four each restart at half speed.",
             half, [] { return PhraseBuilder().halfTime(0.5, 0.75).halfTime(0.75, 1.0).finish(); });
    bank.add("Halftime", "Quarter Speed", "The second half crawls at quarter speed, two octaves down.",
             half, [] { return PhraseBuilder().speed(0.5, 1.0, 0.25).finish(); });
    bank.add("Halftime", "Halftime Switch", "Two bars: the second bar switches to half speed.",
             PresetSettings {twoBars, 0, quantizeBar, false, 0, 0, 100.0f, 100.0f, split120, 0},
             [] { return PhraseBuilder().halfTime(0.5, 1.0).finish(); });

    // Vocal chops.
    const PresetSettings vocal {oneBar, 0, 0, false, 0, 0, 100.0f, 100.0f, 2, 0};
    bank.add("Vocal Chops", "Word Repeat", "Beat three repeats in eighths, beat four in sixteenths.",
             vocal, [] { return PhraseBuilder().roll(0.5, 0.75, e8).roll(0.75, 1.0, s16).finish(); });
    bank.add("Vocal Chops", "Syllable Stutter", "Short syllable stutters on beats two and four.",
             vocal, [] { return PhraseBuilder().roll(0.25, 0.375, t32, 0.3).roll(0.75, 0.875, t32, 0.3).finish(); });
    bank.add("Vocal Chops", "Reverse Word", "Beat three plays backwards, then the line continues.",
             vocal, [] { return PhraseBuilder().reverse(0.5, 0.75, 0.25).finish(); });
    {
        PresetSettings call = vocal; call.lookback = lookBar;
        bank.add("Vocal Chops", "Call & Response", "The second half answers with the previous bar's second half.",
                 call, [] { return PhraseBuilder().echo(0.5, 1.0, 0.0).finish(); });
    }
    bank.add("Vocal Chops", "Vocal Gate", "A sixteenth-note crossfader gate.",
             vocal, [] { return PhraseBuilder().gate(0.0, 1.0, s16, 0.6).finish(); });
    {
        PresetSettings stammer = vocal; stammer.lookback = lookBeat;
        bank.add("Vocal Chops", "Stammer", "Beats two and four repeat the beat before them.",
                 stammer, [] { return PhraseBuilder().echo(0.25, 0.5, 0.0, 0.15).echo(0.75, 1.0, 0.0, 0.15).finish(); });
        PresetSettings flashback = fill2; flashback.lookback = lookTwoBars; flashback.split = 2;
        bank.add("Fills", "Flashback", "Every second bar, the second half replays the line from two bars earlier.",
                 flashback, [] { return PhraseBuilder().echo(0.5, 1.0, 0.0).finish(); });
    }
    {
        PresetSettings hook = vocal; hook.every = every2;
        bank.add("Vocal Chops", "Hook Echo", "Every second bar, beat four repeats beat three.",
                 hook, [] { return PhraseBuilder().echo(0.75, 1.0, beat).finish(); });
    }

    // Riddim: tight, repetitive chops that keep the sub locked.
    const PresetSettings riddim {oneBar, 0, 0, false, 0, 0, 100.0f, 100.0f, split120, 0};
    bank.add("Riddim", "Riddim Chop", "An eighth-note chop over the whole bar.",
             riddim, [] { return PhraseBuilder().gate(0.0, 1.0, e8, 0.5).finish(); });
    bank.add("Riddim", "Riddim Repeat", "Beats two and four repeat in eighths.",
             riddim, [] { return PhraseBuilder().roll(0.25, 0.5, e8).roll(0.75, 1.0, e8).finish(); });
    bank.add("Riddim", "Triplet Riddim", "An eighth-triplet chop over the whole bar.",
             riddim, [] { return PhraseBuilder().gate(0.0, 1.0, t8, 0.6).finish(); });
    bank.add("Riddim", "Riddim Stop & Go", "Beats two and four stop and spin back up.",
             riddim, [] { return PhraseBuilder().stopStart(0.25, 0.375, 0.5).stopStart(0.75, 0.875, 1.0).finish(); });
    {
        PresetSettings lock = riddim; lock.split = split160;
        bank.add("Riddim", "Sub Lock Gate", "A sixteenth gate above a 160 Hz Keep Sub split.",
                 lock, [] { return PhraseBuilder().gate(0.0, 1.0, s16, 0.5).finish(); });
    }
    bank.add("Riddim", "Riddim Rewind", "A push on beat three and a rewind on beat four.",
             riddim, [] { return PhraseBuilder().baby(0.5, 0.75, 0.08).reverse(0.75, 1.0, 0.2).finish(); });

    // Space bass: deep, partly blended motion.
    PresetSettings space {oneBar, 0, quantizeBeat, false, 0, 0, 70.0f, 160.0f, split120, 0};
    bank.add("Space Bass", "Space Drift", "A blended half-speed drift, pushed deeper.",
             space, [] { return PhraseBuilder().halfTime(0.5, 1.0).finish(); });
    {
        PresetSettings worm {twoBars, 0, quantizeBar, false, 0, 0, 80.0f, 180.0f, split120, 0};
        bank.add("Space Bass", "Wormhole", "Two bars: the second bar falls backwards through a deep rewind.",
                 worm, [] { return PhraseBuilder().reverse(0.5, 1.0, 0.5).finish(); });
        PresetSettings zero = space; zero.mix = 100.0f; zero.depth = 130.0f;
        bank.add("Space Bass", "Zero G", "The second half stops and floats back up to speed.",
                 zero, [] { return PhraseBuilder().stopStart(0.5, 0.75, 1.0).finish(); });
        PresetSettings smear = space; smear.mix = 60.0f; smear.depth = 150.0f;
        bank.add("Space Bass", "Nebula Smear", "A slow drag blended under the dry bass.",
                 smear, [] { return PhraseBuilder().halfTime(0.0, 1.0).finish(); });
        PresetSettings warp = space; warp.lookback = lookBar; warp.depth = 100.0f;
        bank.add("Space Bass", "Warp Echo", "The second half replays the previous bar, blended.",
                 warp, [] { return PhraseBuilder().echo(0.5, 1.0, 0.0).finish(); });
        PresetSettings orbit = space; orbit.chain = chainAB; orbit.mix = 85.0f;
        bank.add("Space Bass", "Orbit", "Alternates a deep swirl and a reverse roll each bar.",
                 orbit, [] { return PhraseBuilder().baby(0.25, 0.75, 0.15).finish(); },
                 {[] { return PhraseBuilder().reverseRoll(0.5, 1.0, s16).finish(); }});
    }

    // Chains: pattern slots that advance every phrase.
    bank.add("Chains", "Scratch / Roll", "Bar 1 scratches, bar 2 rolls.",
             PresetSettings {oneBar, 0, quantizeBar, false, 0, chainAB, 100.0f, 100.0f, split120, 0},
             [] { return PhraseBuilder().baby(0.125, 0.375, 0.08).baby(0.625, 0.875, 0.08).finish(); },
             {[] { return PhraseBuilder().roll(0.5, 1.0, s16).finish(); }});
    bank.add("Chains", "Call & Answer", "Bar 1 pushes, bar 2 rewinds.",
             PresetSettings {oneBar, 0, quantizeBar, false, 0, chainAB, 100.0f, 100.0f, split120, 0},
             [] { return PhraseBuilder().baby(0.5, 0.75, 0.08).finish(); },
             {[] { return PhraseBuilder().reverse(0.5, 0.875, 0.3).finish(); }});
    bank.add("Chains", "4 Bar Story", "Clean, chirps, roll, then a tape stop over four bars.",
             PresetSettings {oneBar, 0, quantizeBar, false, 0, chainAD, 100.0f, 100.0f, split120, 0},
             dryPhrase,
             {[] { return PhraseBuilder().baby(0.75, 0.75 + t8, 0.02, true).baby(0.75 + t8, 1.0, 0.03, true).finish(); },
              [] { return PhraseBuilder().roll(0.75, 1.0, s16).finish(); },
              [] { return PhraseBuilder().tapeStop(0.5, 1.0).finish(); }});
    bank.add("Chains", "Eight Fills", "A different fill on every bar of an eight-bar phrase.",
             PresetSettings {oneBar, 0, quantizeBar, false, 0, chainAH, 100.0f, 100.0f, split120, 0},
             [] { return PhraseBuilder().reverse(0.75, 0.9375, 0.2).finish(); },
             {[] { return PhraseBuilder().roll(0.75, 1.0, s16).finish(); },
              [] { return PhraseBuilder().baby(0.75, 0.875, 0.04, true).baby(0.875, 1.0, 0.04, true).finish(); },
              [] { return PhraseBuilder().halfTime(0.75, 1.0).finish(); },
              [] { return PhraseBuilder().gate(0.75, 1.0, t32, 0.5).finish(); },
              [] { return PhraseBuilder().reverseRoll(0.75, 1.0, s16).finish(); },
              [] { return PhraseBuilder().stopStart(0.75, 0.875, 1.0).finish(); },
              [] { return PhraseBuilder().accelerate(0.5, 1.0, {e8, s16, t32}).finish(); }});

    // The 75 originals exactly as in 0.10 (projects and automation that use
    // the Scratch Pattern parameter recall these).
    for (const char* group : {"BASS", "DRUMS", "VOCALS"}) {
        const std::string name = std::string("Classic/") + (std::string(group) == "BASS" ? "Bass" : std::string(group) == "DRUMS" ? "Drums" : "Vocals");
        for (int index = 0; index < factoryPresetCount; ++index)
            if (category(index) == group)
                bank.classic(name, index, true, PresetSettings {}, factory[static_cast<std::size_t>(index)].description);
    }
    return bank;
}

const Bank& bank()
{
    static const Bank instance = makeBank();
    return instance;
}

} // namespace

const std::vector<BankPreset>& bankPresets() { return bank().presets; }
const std::vector<std::string>& bankFolders() { return bank().folders; }

int classicBankIndex(int factoryIndex)
{
    const auto& presets = bankPresets();
    for (std::size_t index = 0; index < presets.size(); ++index)
        if (presets[index].exact && presets[index].classic == factoryIndex) return static_cast<int>(index);
    return -1;
}

} // namespace backScratch
