#include "FactoryPresets.h"

namespace shifty {

namespace {
// Motion shapes, rates and targets, for readability.
constexpr int sine = 0, triangle = 1, rampUp = 2, rampDown = 3;
constexpr int r16 = 0, r8 = 1, r4 = 2, r2 = 3, bar = 4;
constexpr int toShift = 0, toVowel = 1, toBoth = 2;
// Vowels and voice sets.
constexpr float OO = 0.0f, OH = 1.0f, AH = 2.0f, EH = 3.0f, EE = 4.0f;
constexpr int growl = 1, wow = 2; // HUMAN (0) is the default voice
// DRIVE models (0.6.0).
constexpr int soft = 0, tape = 1, tube = 2, fuzz = 3;

FactoryPreset P(const char* folder, const char* name, EngineMode engine)
{
    FactoryPreset p;
    p.folder = folder;
    p.name = name;
    p.engine = engine;
    return p;
}

constexpr auto TALK = EngineMode::talk;
constexpr auto MOUTH = EngineMode::mouth;
constexpr auto CLASSIC = EngineMode::classic;
constexpr auto ORIGINAL = EngineMode::original;

// The 24 sounds of 0.4.2, exactly as they were: CLASSIC (original sound),
// Sub Safe on at 120 Hz, Output 0 dB, Vowel Color off.
FactoryPreset original(const char* name, float shift, float focus, float bite, float drive, float mix,
                       bool motion, int shape, int rate, float depth, bool sync = true, float freeHz = 2.0f)
{
    auto p = P("Classic", name, ORIGINAL).sh(shift).fo(focus).bi(bite).dr(drive).mx(mix);
    p.motion = motion;
    p.shape = shape;
    p.rate = rate;
    p.depth = depth;
    p.sync = sync;
    p.freeHz = freeHz;
    return p;
}

std::vector<FactoryPreset> makeBank()
{
    std::vector<FactoryPreset> bank;
    const auto add = [&bank](const FactoryPreset& p) { bank.push_back(p); };

    // TALK: vowel-filter speech on the bass.
    add(P("Talk", "Rubber Throat", TALK).sh(-2).fo(.55f).bi(.4f).vowel(1.5f).env(.35f, 5, 180).macros(.2f, .3f).dr(.45f).model(tube).smashed(.55f));
    add(P("Talk", "Goblin Talk", TALK).sh(3).fo(.65f).bi(.55f).mx(.95f).vowel(AH).lfo(triangle, r8, 6, toVowel).macros(.3f, .4f).dr(.35f).model(fuzz).smashed(.5f));
    add(P("Talk", "Nasal Fold", TALK).sh(2).fo(.8f).bi(.6f).mx(.9f).vowel(3.3f).lfo(sine, r4, 3, toVowel).dr(.3f).model(tube).smashed(.4f));
    add(P("Talk", "Talking Metal", TALK).sh(1).fo(.85f).bi(.7f).vowel(2.5f, wow).lfo(triangle, r16, 7, toBoth).macros(.5f, 0).dr(.45f).model(fuzz).smashed(.55f));
    add(P("Talk", "Yap Machine", TALK).fo(.6f).bi(.5f).vowel(OH).lfo(rampDown, r16, 9, toVowel).macros(.25f, .5f).dr(.4f).model(tube).smashed(.5f, .4f));
    add(P("Talk", "Chatterbox", TALK).bi(.45f).vowel(AH).lfo(sine, r8, 9, toVowel).env(.3f).dr(.35f).model(tube).smashed(.45f));
    add(P("Talk", "Rubber Lips", TALK).sh(-1).bi(.5f).vowel(.5f).env(.6f, 3, 120).macros(.3f, 0).dr(.4f).model(tape).smashed(.4f));
    add(P("Talk", "Throat Singer", TALK).sh(-3).fo(.75f).bi(.7f).vowel(OH, growl).lfoFree(sine, .4f, 4, toVowel).macros(.45f, 0).dr(.5f).model(tube).smashed(.5f));
    add(P("Talk", "Mumble Box", TALK).fo(.4f).bi(.3f).mx(.9f).vowel(.8f).lfo(triangle, r4, 4, toVowel).env(.25f).dr(.3f).model(tape).smashed(.35f));
    add(P("Talk", "Speak Easy", TALK).bi(.4f).vowel(2.5f).lfo(sine, r2, 6, toVowel).macros(0, .3f).dr(.3f).model(tape).smashed(.35f));
    add(P("Talk", "Lip Flap", TALK).bi(.45f).vowel(1.5f).env(.5f).yoi(.4f, .5f, EH, 120).lfo(rampUp, r8, 4, toVowel).dr(.35f).model(tube).smashed(.45f));

    // YOI: an onset-triggered vowel sweep (plus a formant rise) on every note.
    add(P("Yoi", "Riser Syllable", TALK).bi(.5f).vowel(EH).yoi(1, OO, EE, 260, 4).lfo(rampUp, r2, 3, toShift).dr(.4f).model(tube).smashed(.45f));
    add(P("Yoi", "Short Croak", TALK).bi(.65f).vowel(AH, growl).yoi(.9f, OH, EH, 90, -3).dr(.4f).model(fuzz).smashed(.5f));
    add(P("Yoi", "Yoi Classic", TALK).bi(.5f).vowel(EE).yoi(1, OO, EE, 180, 3).macros(.3f, 0).dr(.4f).model(tube).smashed(.5f));
    add(P("Yoi", "Yoi Snap", TALK).bi(.6f).vowel(EE).yoi(1, OO, EE, 70, 5).dr(.4f).model(fuzz).smashed(.55f, .35f));
    add(P("Yoi", "Yoi Slow", TALK).bi(.45f).vowel(EE).yoi(1, OO, EE, 420, 2).dr(.3f).model(tape).smashed(.4f, .6f));
    add(P("Yoi", "Reverse Yoi", TALK).bi(.5f).vowel(OO).yoi(1, EE, OO, 220, -3).dr(.35f).model(tube).smashed(.45f));
    add(P("Yoi", "Yoi Stab", TALK).bi(.6f).vowel(EH, wow).yoi(1, OO, EH, 120, 6).env(.2f).macros(.4f, 0).dr(.5f).model(fuzz).smashed(.6f));
    add(P("Yoi", "Wow Yoi", TALK).bi(.5f).vowel(AH, wow).yoi(1, OO, AH, 300).dr(.35f).model(tube).smashed(.5f));
    add(P("Yoi", "Yoi Climb", TALK).bi(.5f).vowel(EE).yoi(1, OH, EE, 250, 7).lfo(rampUp, bar, 4, toShift).dr(.4f).model(tube).smashed(.5f));
    add(P("Yoi", "Yoi Drip", TALK).bi(.4f).mx(.9f).vowel(.5f).yoi(.8f, EE, .5f, 350, -2).dr(.3f).model(tape).smashed(.4f));

    // GROWL: throat and gargle.
    add(P("Growl", "Basement Growl", TALK).sh(-5).fo(.7f).bi(.65f).vowel(1.5f, growl).lfo(triangle, r8, 3, toVowel).macros(.5f, 0).dr(.55f).model(fuzz).smashed(.65f));
    add(P("Growl", "Warm Snarl", TALK).sh(-2).fo(.6f).bi(.55f).vowel(AH, growl).env(.25f).macros(.35f, 0).dr(.6f).model(tube).smashed(.55f));
    add(P("Growl", "Tar Pit", MOUTH).sh(-7).fo(.6f).bi(.45f).vowel(OH, growl).macros(.55f, 0).dr(.55f).model(tube).smashed(.55f));
    add(P("Growl", "Cellar Beast", TALK).sh(-6).fo(.75f).bi(.75f).vowel(OH, growl).lfoFree(sine, .25f, 2, toVowel).macros(.6f, 0).dr(.55f).model(fuzz).smashed(.65f));
    add(P("Growl", "Gravel Throat", MOUTH).sh(-4).bi(.7f).macros(.5f, 0).dr(.6f).model(fuzz).smashed(.65f));
    add(P("Growl", "Grime Gargle", TALK).bi(.6f).vowel(2.5f, growl).lfo(sine, r16, 5, toVowel).macros(.4f, 0).dr(.5f).model(fuzz).smashed(.6f));
    add(P("Growl", "Ogre Breath", CLASSIC).sh(-8).fo(.75f).bi(.7f).colour().macros(.5f, 0).dr(.6f).model(tube).smashed(.55f));
    add(P("Growl", "Rust Belly", TALK).sh(-3).bi(.7f).vowel(.5f, growl).env(.3f).macros(.7f, 0).dr(.6f).model(fuzz).smashed(.65f));
    add(P("Growl", "Low Rumble Mouth", MOUTH).sh(-10).fo(.45f).bi(.3f).sub(true, 150).vowel(OO).macros(.45f, 0).dr(.5f).model(tape).smashed(.5f));

    // WOBBLE: tempo-synced vowel and formant movement.
    add(P("Wobble", "Slow Lurch", TALK).sh(-2).bi(.5f).vowel(1.5f).lfo(sine, r2, 9, toVowel).dr(.4f).model(tube).smashed(.5f));
    add(P("Wobble", "Pocket Wobble", TALK).bi(.45f).vowel(AH).lfo(sine, r4, 6, toVowel).dr(.35f).model(tube).smashed(.45f));
    add(P("Wobble", "Liquid Twitch", TALK).bi(.4f).mx(.85f).vowel(2.5f).lfoFree(sine, 3.7f, 5, toVowel).dr(.3f).model(tape).smashed(.4f));
    add(P("Wobble", "Ratcheted Vowel", TALK).bi(.55f).vowel(AH).lfo(rampUp, r16, 7, toVowel).dr(.4f).model(fuzz).smashed(.5f));
    add(P("Wobble", "Eighth Wob", TALK).bi(.55f).vowel(1.5f).lfo(sine, r8, 9, toVowel).macros(.3f, 0).dr(.45f).model(tube).smashed(.5f));
    add(P("Wobble", "Sixteenth Chop", TALK).bi(.5f).vowel(AH).lfo(rampDown, r16, 8, toBoth).dr(.45f).model(fuzz).smashed(.6f, .35f));
    add(P("Wobble", "Half Bar Swell", TALK).bi(.5f).vowel(OH).lfo(triangle, r2, 10, toVowel).dr(.4f).model(tube).smashed(.5f));
    add(P("Wobble", "Bar Breather", TALK).bi(.4f).vowel(AH).lfo(sine, bar, 9, toBoth).dr(.35f).model(tape).smashed(.45f));
    add(P("Wobble", "Wob Wob", TALK).bi(.6f).vowel(1.5f, wow).lfo(sine, r8, 12, toVowel).macros(.3f, 0).dr(.5f).model(fuzz).smashed(.6f));
    add(P("Wobble", "Robot Wob", MOUTH).bi(.55f).lfo(triangle, r8, 7, toShift).dr(.45f).model(fuzz).smashed(.5f));
    add(P("Wobble", "Swing Gargle", TALK).bi(.6f).vowel(1.5f, growl).lfo(triangle, r4, 7, toVowel).dr(.5f).model(tube).smashed(.55f));

    // MOUTH: pitch-synchronous mouth size, works on plain saws too.
    add(P("Mouth", "Motor Mouth", MOUTH).sh(2).bi(.45f).lfo(triangle, r16, 5, toShift).dr(.45f).model(tube).smashed(.45f));
    add(P("Mouth", "Hollow Knock", MOUTH).sh(-3).fo(.35f).bi(.65f).mx(.85f).vowel(OH).macros(.4f, 0).dr(.35f).model(tape).smashed(.4f));
    add(P("Mouth", "Glass Jaw", MOUTH).sh(8).fo(.7f).bi(.4f).mx(.85f).vowel(EE).macros(.35f, 0).dr(.4f).model(tape).smashed(.45f));
    add(P("Mouth", "Big Mouth", MOUTH).sh(-5).fo(.6f).bi(.3f).vowel(AH).macros(.7f, 0).dr(.4f).model(tube).smashed(.45f));
    add(P("Mouth", "Small Mouth", MOUTH).sh(5).fo(.6f).bi(.3f).vowel(EH).macros(.4f, 0).dr(.35f).model(tube).smashed(.45f));
    add(P("Mouth", "Chipmunk Bass", MOUTH).sh(10).fo(.5f).bi(.25f).mx(.9f).vowel(EE).macros(.3f, 0).dr(.3f).model(tape).smashed(.4f));
    add(P("Mouth", "Giant Jaw", MOUTH).sh(-9).fo(.5f).bi(.35f).vowel(OH).macros(.75f, 0).dr(.45f).model(tube).smashed(.5f));
    add(P("Mouth", "Fry Buzz", MOUTH).sh(1).bi(.9f).macros(.4f, 0).dr(.5f).model(fuzz).smashed(.5f));
    add(P("Mouth", "Throat Shrink", MOUTH).bi(.35f).lfo(rampUp, r2, 8, toShift).dr(.35f).model(tape).smashed(.4f));
    add(P("Mouth", "Gummy Mouth", MOUTH).sh(-2).bi(.5f).env(.4f, 10, 200).dr(.4f).model(tube).smashed(.45f));

    // SQUELCH: envelope-driven, snappy and wet.
    add(P("Squelch", "Elastic Bite", TALK).sh(2).bi(.75f).vowel(OH).env(.7f, 2, 160).macros(.4f, 0).dr(.45f).model(fuzz).smashed(.55f));
    add(P("Squelch", "Bright Chew", TALK).sh(4).fo(.75f).bi(.6f).vowel(EH).env(.4f).dr(.35f).model(tube).smashed(.45f));
    add(P("Squelch", "Narrow Grit", TALK).sh(-1).fo(.9f).bi(.8f).vowel(2.5f).macros(.5f, 0).dr(.5f).model(fuzz).smashed(.5f));
    add(P("Squelch", "Acid Chew", TALK).bi(.8f).vowel(.5f, wow).env(.8f, 1, 90).macros(.6f, 0).dr(.55f).model(fuzz).smashed(.6f));
    add(P("Squelch", "Squelch Box", TALK).bi(.7f).vowel(OH).env(.65f, 3, 120).yoi(.5f, OO, EH, 80).dr(.45f).model(tube).smashed(.5f));
    add(P("Squelch", "Snap Squelch", TALK).bi(.85f).vowel(OO).env(.9f, .5f, 60).macros(.5f, 0).dr(.45f).model(fuzz).smashed(.6f, .35f));
    add(P("Squelch", "Wet Squelch", TALK).bi(.65f).vowel(1.5f, wow).env(.55f).lfo(sine, r16, 3, toVowel).dr(.35f).model(tape).smashed(.45f));
    add(P("Squelch", "Rubber Band", CLASSIC).sh(5).bi(.7f).env(.4f).colour().macros(.4f, 0).dr(.5f).model(tube).smashed(.5f));
    add(P("Squelch", "Zap Mouth", MOUTH).sh(6).bi(.6f).env(-.4f).dr(.5f).model(fuzz).smashed(.5f));
    add(P("Squelch", "Tight Squelch", TALK).fo(.85f).bi(.7f).vowel(AH).env(.6f, 1, 50).dr(.4f).model(tube).smashed(.5f, .35f));
    add(P("Squelch", "Envelope Pop", TALK).bi(.6f).vowel(.5f).env(1, 2, 250).dr(.4f).model(tube).smashed(.5f));

    // SUBTLE MID: lighter touches for mixes, still audible on plain saws.
    add(P("Subtle Mid", "Subtle Shape", TALK).sh(-2.5f).fo(.3f).bi(.25f).mx(.55f).vowel(1.5f).dr(.15f).model(tube).smashed(.2f));
    add(P("Subtle Mid", "Midrange Nudge", MOUTH).sh(1.5f).fo(.6f).bi(.15f).mx(.7f).vowel(AH).macros(.3f, 0).dr(.3f).model(tube).smashed(.3f));
    add(P("Subtle Mid", "Mid Lift", TALK).fo(.3f).bi(.15f).mx(.45f).vowel(2.5f).dr(.15f).model(tube).smashed(.25f));
    add(P("Subtle Mid", "Gentle Vowel", TALK).fo(.35f).bi(.2f).mx(.5f).vowel(1.5f).lfo(sine, bar, 3, toVowel).dr(.1f).model(tube).smashed(.2f));
    add(P("Subtle Mid", "Warm Glue", MOUTH).sh(-1.5f).bi(.2f).mx(.7f).vowel(AH).macros(.3f, 0).dr(.35f).model(tape).smashed(.3f));
    add(P("Subtle Mid", "Just a Hint", TALK).fo(.25f).bi(.1f).mx(.35f).vowel(AH).dr(0).model(soft).smashed(.15f));
    add(P("Subtle Mid", "Mix Thickener", TALK).fo(.3f).bi(.2f).mx(.4f).vowel(1.5f, growl).dr(.25f).model(tube).smashed(.3f));
    add(P("Subtle Mid", "Soft Edges", MOUTH).sh(-3).fo(.4f).bi(.3f).mx(.65f).vowel(OH).macros(.3f, 0).dr(.2f).model(tube).smashed(.2f));
    add(P("Subtle Mid", "Air Vowel", TALK).fo(.3f).bi(.15f).mx(.4f).vowel(3.5f, wow).dr(.15f).model(tube).smashed(.25f, .5f, .3f));

    // VOWEL MORPHS: slow and stepped vowel journeys.
    add(P("Vowel Morphs", "Vowel Roll", TALK).bi(.5f).vowel(AH).lfo(sine, r8, 7, toVowel).dr(.35f).model(tube).smashed(.45f));
    add(P("Vowel Morphs", "Lazy Chew", TALK).bi(.45f).vowel(1.5f).lfoFree(triangle, .63f, 6, toVowel).dr(.35f).model(tape).smashed(.4f));
    add(P("Vowel Morphs", "OO to EE", TALK).bi(.5f).vowel(AH).lfo(rampUp, bar, 6, toVowel).dr(.35f).model(tube).smashed(.45f));
    add(P("Vowel Morphs", "AH to OO", TALK).bi(.45f).vowel(OH).lfo(rampDown, r2, 3, toVowel).dr(.35f).model(tape).smashed(.4f));
    add(P("Vowel Morphs", "Vowel Circle", TALK).bi(.55f).vowel(AH, wow).lfo(triangle, r4, 6, toVowel).dr(.4f).model(tube).smashed(.45f));
    add(P("Vowel Morphs", "Slow Speak", TALK).bi(.45f).vowel(AH).lfoFree(sine, .2f, 6, toVowel).env(.25f).dr(.35f).model(tape).smashed(.4f, .6f));
    add(P("Vowel Morphs", "Evolving Throat", TALK).bi(.55f).vowel(AH, growl).lfoFree(sine, .12f, 6, toBoth).macros(.3f, 0).dr(.45f).model(fuzz).smashed(.5f));
    add(P("Vowel Morphs", "Breathing Vowel", TALK).bi(.4f).vowel(1.5f).env(.5f, 20, 400).dr(.3f).model(tape).smashed(.4f, .65f));
    add(P("Vowel Morphs", "Morph Drift", TALK).bi(.45f).vowel(2.5f, wow).lfoFree(triangle, .35f, 4, toVowel).yoi(.3f, OO, EH, 300).macros(0, .4f).dr(.35f).model(tube).smashed(.4f, .6f));
    add(P("Vowel Morphs", "Vowel Stepper", TALK).bi(.5f).vowel(AH).lfo(rampDown, r4, 6, toVowel).dr(.4f).model(tube).smashed(.45f));

    // SPACE BASS: wide, slow, and strange.
    add(P("Space Bass", "Hollow Reese", CLASSIC).sh(-5).fo(.45f).bi(.5f).mx(.85f).colour().lfo(sine, bar, 2, toShift).dr(.4f).model(tape).smashed(.45f, .65f));
    add(P("Space Bass", "Dark Chamber", MOUTH).sh(-9).fo(.4f).bi(.3f).vowel(OO).macros(.45f, 0).dr(.3f).model(tape).smashed(.4f, .7f));
    add(P("Space Bass", "Nebula Mouth", MOUTH).sh(4).bi(.3f).mx(.85f).vowel(AH).macros(.35f, 0).lfoFree(sine, .3f, 3, toShift).dr(.3f).model(tape).smashed(.45f, .7f));
    add(P("Space Bass", "Orbit Vowel", TALK).bi(.5f).vowel(EH, wow).lfo(sine, bar, 9, toBoth).dr(.4f).model(tube).smashed(.5f, .6f));
    add(P("Space Bass", "Zero G", CLASSIC).sh(7).fo(.8f).bi(.7f).colour().lfo(sine, r2, 3, toShift).macros(.5f, 0).dr(.5f).model(fuzz).smashed(.5f));
    add(P("Space Bass", "Plasma Throat", TALK).bi(.6f).vowel(AH, wow).yoi(.6f, OO, EE, 400, 4).lfo(triangle, r2, 4, toVowel).macros(.4f, 0).dr(.45f).model(fuzz).smashed(.6f));
    add(P("Space Bass", "Comet Tail", MOUTH).sh(3).bi(.4f).env(.35f).lfo(rampDown, bar, 5, toShift).dr(.35f).model(tape).smashed(.45f, .7f));
    add(P("Space Bass", "Deep Space Growl", TALK).sh(-4).bi(.7f).vowel(OH, growl).lfoFree(sine, .15f, 6, toVowel).macros(.55f, 0).dr(.55f).model(fuzz).smashed(.65f));
    add(P("Space Bass", "Warp Drive", CLASSIC).sh(4).bi(.6f).lowLat().colour().lfo(rampUp, r4, 8, toShift).dr(.5f).model(tube).smashed(.5f));

    // CLASSIC: the 24 sounds of 0.4.2, unchanged, on the original sound.
    add(original("Rubber Throat", -4.0f, .55f, .35f, .20f, 1.0f, false, 1, 1, 6.0f));
    add(original("Basement Growl", -8.0f, .72f, .58f, .30f, 1.0f, false, 1, 2, 5.0f));
    add(original("Goblin Talk", 4.0f, .67f, .66f, .22f, .92f, true, 1, 1, 3.0f));
    add(original("Hollow Reese", -5.5f, .34f, .42f, .08f, .85f, false, 0, 2, 4.0f));
    add(original("Elastic Bite", 6.0f, .76f, .81f, .34f, .95f, false, 1, 1, 6.0f));
    add(original("Dark Chamber", -10.0f, .26f, .30f, .10f, 1.0f, false, 0, 3, 4.0f));
    add(original("Vowel Roll", -2.0f, .61f, .56f, .18f, 1.0f, true, 0, 1, 7.0f));
    add(original("Motor Mouth", 1.0f, .66f, .52f, .31f, 1.0f, true, 1, 0, 5.0f));
    add(original("Slow Lurch", -6.0f, .52f, .45f, .25f, 1.0f, true, 0, 3, 8.0f));
    add(original("Bright Chew", 7.0f, .78f, .57f, .16f, .9f, false, 1, 1, 4.0f));
    add(original("Subtle Shape", -2.5f, .46f, .18f, .0f, .55f, false, 0, 2, 3.0f));
    add(original("Riser Syllable", 3.0f, .62f, .65f, .26f, 1.0f, true, 2, 2, 8.0f));
    add(original("Midrange Nudge", 1.5f, .58f, .22f, .06f, .72f, false, 0, 2, 1.5f));
    add(original("Nasal Fold", 3.5f, .84f, .62f, .12f, .90f, false, 1, 1, 2.5f));
    add(original("Hollow Knock", -3.0f, .22f, .56f, .09f, .82f, false, 0, 2, 2.0f));
    add(original("Glass Jaw", 8.5f, .70f, .72f, .18f, .78f, false, 1, 1, 3.0f));
    add(original("Warm Snarl", -2.0f, .65f, .48f, .42f, .88f, false, 0, 2, 2.0f));
    add(original("Narrow Grit", -1.0f, .92f, .80f, .26f, .76f, false, 1, 1, 1.0f));
    add(original("Pocket Wobble", -2.5f, .60f, .40f, .10f, .92f, true, 0, 2, 2.8f));
    add(original("Short Croak", -4.5f, .74f, .68f, .24f, .95f, true, 3, 1, 3.5f));
    add(original("Talking Metal", 2.0f, .87f, .76f, .28f, .86f, true, 1, 0, 4.0f));
    add(original("Liquid Twitch", .5f, .44f, .36f, .05f, .80f, true, 0, 1, 2.2f, false, 3.7f));
    add(original("Lazy Chew", -1.5f, .52f, .44f, .15f, .90f, true, 1, 3, 3.2f, false, .63f));
    add(original("Ratcheted Vowel", 2.5f, .78f, .60f, .20f, .94f, true, 2, 0, 4.5f));
    return bank;
}
} // namespace

const std::vector<FactoryPreset>& factoryBank()
{
    static const std::vector<FactoryPreset> bank = makeBank();
    return bank;
}

juce::StringArray factoryFolders()
{
    juce::StringArray folders;
    for (const auto& preset : factoryBank())
        folders.addIfNotAlreadyThere(preset.folder);
    return folders;
}

} // namespace shifty
