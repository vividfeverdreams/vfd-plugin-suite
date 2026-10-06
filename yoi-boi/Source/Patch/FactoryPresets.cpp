#include "Patch.h"

#include <algorithm>

// The 0.2.0 factory bank. The twelve originals (factory:0..11, engine 3) come
// first and never change; every sound below uses engine 4 and gets the next id.
// Recipes are authored settings, not randomizer seeds. Output trims were set
// from objective renders (see FACTORY-PRESETS.md) so the bank sits within a
// few dB of itself without touching the protective ceiling.
namespace yoiboi
{
namespace
{
constexpr int EE = vowelEE, EH = vowelEH, AH = vowelAH, OH = vowelOH, OO = vowelOO,
              IH = vowelIH, UH = vowelUH, AE = vowelAE, ER = vowelER;
enum : int { perNote = 0, synced = 1, modWheel = 2, aftertouch = 3 };
enum : int { d16 = 0, d8 = 1, d4 = 2, d2 = 3, bar = 4 };
enum : int { straight = 0, triplet = 1, dotted = 2 };
enum : int { classic = 0, diode = 1, fold = 2, crush = 3 };
enum : int { noOnset = 0, onsetW = 1, onsetY = 2 };
enum : int { noConsonant = 0, consT = 1, consK = 2, consCH = 3 };

// Engines 1-3 read the five original poses; keep them close to the vowels.
int classicPose (int vowel) noexcept
{
    switch (vowel)
    {
        case IH: return EH;
        case UH: return AH;
        case AE: return AH;
        case ER: return OH;
        default: return std::clamp (vowel, 0, 4);
    }
}

float feelMultiplier (int feel) noexcept { return feel == 1 ? 2.0f / 3.0f : feel == 2 ? 1.5f : 1.0f; }

class Build
{
public:
    Build (const char* name, const char* folder, Family family, juce::StringArray tags)
    {
        p.name = name;
        p.folder = folder;
        p.family = family;
        p.tags = std::move (tags);
        p.tags.addIfNotAlreadyThere (folder);
        p.soundEngine = Patch::currentSoundEngine;
        p.subMode = 1;
        p.lowCut = true;
        p.outputDecibels = -6.0f;
        length (d8, straight);
    }
    Build& tone (float mouth, float size, float bite, float dirt) { p.mouth = mouth; p.size = size; p.bite = bite; p.dirt = dirt; return *this; }
    Build& fm (float ratio, float index, float resonance) { p.fmRatio = ratio; p.fmIndex = index; p.resonance = resonance; return *this; }
    Build& vowels (int start, int body, int end, float bodyTime = .35f, float curve = .45f)
    {
        p.vowelStart = start; p.vowelBody = body; p.vowelEnd = end;
        p.startPose = classicPose (start); p.bodyPose = classicPose (body); p.endPose = classicPose (end);
        p.bodyTime = bodyTime; p.curve = curve;
        return *this;
    }
    Build& length (int division, int feel = straight, bool loop = false)
    {
        p.tempoSync = true; p.lengthDivision = division; p.lengthFeel = feel; p.loop = loop;
        constexpr std::array<float, 5> seconds { .125f, .25f, .5f, 1.f, 2.f };
        p.lengthSeconds = seconds[static_cast<std::size_t> (division)] * feelMultiplier (feel);
        p.freeLengthMilliseconds = std::max (80.0f, p.lengthSeconds * 1000.0f);
        return *this;
    }
    Build& sub (float level, int mode = 1, bool lowCut = true) { p.sub = level; p.subMode = mode; p.lowCut = lowCut; return *this; }
    Build& talk (int mode, int division = d8, int feel = straight) { p.talkMode = mode; p.talkDivision = division; p.talkFeel = feel; return *this; }
    Build& pitch (float semitones, float milliseconds) { p.pitchEnvSemitones = semitones; p.pitchEnvMilliseconds = milliseconds; return *this; }
    Build& glide (float milliseconds, int mode = 0) { p.glideMilliseconds = milliseconds; p.glideMode = mode; return *this; }
    Build& bend (int range) { p.bendRange = range; return *this; }
    Build& shape (int shaper, float squash) { p.shaper = shaper; p.squash = squash; return *this; }
    Build& wide (float width) { p.width = width; return *this; }
    Build& onset (int glideIn) { p.onsetGlide = glideIn; return *this; }
    Build& consonant (int type, float level) { p.consonant = type; p.consonantLevel = level; return *this; }
    Build& velocity (float mouth, float bite, float dirt) { p.velocityToMouth = mouth; p.velocityToBite = bite; p.velocityToDirt = dirt; return *this; }
    Build& env (float attack, float release) { p.attackMilliseconds = attack; p.releaseMilliseconds = release; return *this; }
    Build& output (float decibels) { p.outputDecibels = decibels; return *this; }
    Build& phaseReset() { p.subPhaseReset = true; return *this; }
    Build& seed (std::uint64_t value) { p.seed = value; return *this; }
    Patch patch() const { return p; }

private:
    Patch p;
};

constexpr Family rubber = Family::Rubber, metal = Family::Metal, throat = Family::Throat, squeak = Family::Squeak;

std::vector<Patch> latestBank()
{
    std::vector<Build> b;
    // ---- Yoi: the twelve originals, reworked for engine 4, plus new calls.
    b.push_back (Build ("Tape Bounce II", "Yoi", rubber, { "Yoi", "Bounce" }).tone (.95f, .55f, .63f, .16f).fm (2.f, 2.3f, .66f)
        .vowels (OO, AH, EE, .30f, .40f).onset (onsetY).consonant (consK, .35f).shape (classic, .25f).velocity (.35f, .2f, 0.f).output (-5.0f));
    b.push_back (Build ("Bubble Trouble II", "Yoi", rubber, { "Yoi", "Triplet" }).tone (.97f, .47f, .57f, .10f).fm (1.f, 1.8f, .70f)
        .vowels (OO, UH, EE, .27f, .30f).length (d8, triplet).onset (onsetW).shape (classic, .2f).velocity (.3f, 0.f, 0.f).output (-2.5f));
    b.push_back (Build ("Elastic Yoi II", "Yoi", rubber, { "Yoi", "Dotted" }).tone (.92f, .61f, .66f, .20f).fm (2.f, 2.6f, .60f)
        .vowels (OH, AH, IH, .42f, .56f).length (d8, dotted).onset (onsetY).glide (40.f, 1).shape (diode, .25f).output (-5.5f));
    b.push_back (Build ("Chrome Chew II", "Yoi", metal, { "Yoi", "Metal" }).tone (.93f, .39f, .78f, .21f).fm (3.f, 2.9f, .64f)
        .vowels (OO, AE, EE, .29f, .37f).consonant (consT, .45f).shape (fold, .3f).velocity (0.f, .35f, .2f).output (-8.5f));
    b.push_back (Build ("Laser Reply II", "Yoi", metal, { "Yoi", "Short" }).tone (.88f, .29f, .82f, .13f).fm (4.f, 2.2f, .58f)
        .vowels (OH, EH, IH, .24f, .24f).length (d16).pitch (7.f, 45.f).consonant (consT, .4f).shape (classic, .3f).output (-9.0f));
    b.push_back (Build ("Steel Syllable II", "Yoi", metal, { "Yoi", "Dense" }).tone (.98f, .44f, .73f, .26f).fm (2.f, 3.2f, .66f)
        .vowels (AH, ER, EE, .38f, .48f).length (d8, triplet).shape (fold, .35f).velocity (0.f, .3f, .3f).output (-10.0f));
    b.push_back (Build ("Low Talker II", "Yoi", throat, { "Yoi", "Deep" }).tone (.96f, .78f, .52f, .15f).fm (1.f, 1.9f, .71f)
        .vowels (OO, UH, EE, .46f, .58f).length (d4).onset (onsetW).shape (diode, .2f).sub (.48f).output (-2.0f));
    b.push_back (Build ("Vowel Throw II", "Yoi", throat, { "Yoi", "Dotted" }).tone (.99f, .68f, .62f, .22f).fm (2.f, 2.5f, .74f)
        .vowels (OH, AH, IH, .35f, .43f).length (d8, dotted).onset (onsetY).shape (classic, .3f).velocity (.4f, 0.f, .2f).output (-7.0f));
    b.push_back (Build ("Basement Answer II", "Yoi", throat, { "Yoi", "Dark" }).tone (.92f, .83f, .47f, .11f).fm (1.f, 1.5f, .63f)
        .vowels (OO, ER, EH, .54f, .61f).length (d4, triplet).shape (diode, .15f).sub (.5f).output (-1.5f));
    b.push_back (Build ("Pocket Gremlin II", "Yoi", squeak, { "Yoi", "Short" }).tone (.96f, .27f, .70f, .11f).fm (2.f, 2.0f, .62f)
        .vowels (OH, IH, EE, .26f, .28f).length (d16).onset (onsetY).consonant (consT, .3f).pitch (-3.f, 60.f).output (-8.0f));
    b.push_back (Build ("Squeaky Clean II", "Yoi", squeak, { "Yoi", "Clean" }).tone (.98f, .20f, .58f, .04f).fm (1.f, 1.3f, .58f)
        .vowels (OO, AH, IH, .22f, .20f).length (d16, triplet).onset (onsetY).shape (classic, .1f).output (0.5f));
    b.push_back (Build ("Neon Chirp II", "Yoi", squeak, { "Yoi", "Melodic" }).tone (.91f, .35f, .74f, .17f).fm (3.f, 2.3f, .66f)
        .vowels (AH, EH, IH, .34f, .36f).glide (35.f, 1).shape (fold, .2f).velocity (.3f, .2f, 0.f).output (-9.0f));
    b.push_back (Build ("Yoi Boi Prime", "Yoi", rubber, { "Yoi", "Signature" }).tone (.98f, .5f, .66f, .18f).fm (2.f, 2.5f, .7f)
        .vowels (UH, OH, EE, .3f, .35f).onset (onsetY).consonant (consK, .3f).shape (classic, .35f).wide (.2f).velocity (.3f, .2f, .1f).output (-9.5f));
    b.push_back (Build ("Oi Oi Oi", "Yoi", rubber, { "Yoi", "Talk" }).tone (.97f, .52f, .6f, .15f).fm (2.f, 2.1f, .68f)
        .vowels (OH, OH, EE, .5f, .4f).talk (synced, d8).onset (onsetY).shape (classic, .3f).output (-3.5f));
    b.push_back (Build ("Y-Glide Gremlin", "Yoi", squeak, { "Yoi", "Glide" }).tone (.95f, .3f, .7f, .14f).fm (2.f, 2.2f, .66f)
        .vowels (OO, AE, IH, .3f, .3f).length (d16).onset (onsetY).glide (60.f, 1).pitch (-5.f, 80.f).output (-4.0f));
    b.push_back (Build ("Yoink", "Yoi", metal, { "Yoi", "Pitch" }).tone (.9f, .42f, .76f, .22f).fm (3.f, 2.6f, .64f)
        .vowels (OO, AH, EE, .28f, .35f).pitch (12.f, 90.f).consonant (consK, .45f).shape (fold, .3f).output (-9.0f));

    // ---- Riddim: short, metallic, grinding stabs for 140 half-time.
    b.push_back (Build ("Riddim Ritual", "Riddim", metal, { "Riddim", "Metal" }).tone (.9f, .45f, .86f, .42f).fm (3.f, 3.6f, .7f)
        .vowels (OO, AE, EE, .3f, .4f).length (d16).talk (synced, d16, triplet).shape (fold, .55f).velocity (0.f, .3f, .4f).output (-9.0f));
    b.push_back (Build ("Lunch Tray", "Riddim", metal, { "Riddim", "Clang" }).tone (.85f, .38f, .9f, .38f).fm (4.f, 3.2f, .66f)
        .vowels (OH, EH, IH, .25f, .3f).length (d16).consonant (consT, .55f).shape (crush, .45f).output (-9.5f));
    b.push_back (Build ("Knock Knock Riddim", "Riddim", rubber, { "Riddim", "Knock" }).tone (.92f, .6f, .74f, .3f).fm (2.f, 3.0f, .7f)
        .vowels (OO, UH, AH, .3f, .35f).length (d16).consonant (consK, .6f).pitch (5.f, 35.f).shape (diode, .5f).velocity (.3f, .3f, .3f).output (-4.0f));
    b.push_back (Build ("Dungeon Step", "Riddim", throat, { "Riddim", "Dark" }).tone (.88f, .78f, .7f, .4f).fm (1.f, 3.4f, .62f)
        .vowels (OO, ER, UH, .4f, .5f).length (d8).talk (synced, d8, triplet).shape (diode, .55f).output (-4.5f));
    b.push_back (Build ("Tin Can Shuffle", "Riddim", metal, { "Riddim", "Triplet" }).tone (.9f, .33f, .82f, .32f).fm (3.f, 3.0f, .72f)
        .vowels (OH, AE, IH, .3f, .3f).length (d16, triplet).talk (synced, d16, triplet).shape (crush, .5f).output (-8.0f));
    b.push_back (Build ("Robo Grime", "Riddim", metal, { "Riddim", "Robot" }).tone (.8f, .5f, .9f, .48f).fm (2.f, 4.0f, .6f)
        .vowels (EH, ER, AE, .35f, .45f).talk (synced, d16).shape (fold, .6f).wide (.15f).output (-8.5f));
    b.push_back (Build ("Clang Dance", "Riddim", metal, { "Riddim", "Clang" }).tone (.94f, .4f, .8f, .36f).fm (4.f, 3.4f, .7f)
        .vowels (OO, EH, EE, .28f, .32f).length (d8, dotted).consonant (consCH, .4f).shape (fold, .5f).velocity (0.f, .4f, .3f).output (-9.0f));

    // ---- Talk: tempo-synced vowel motion and onset glides.
    b.push_back (Build ("Chatterbox", "Talk", rubber, { "Talk", "Synced" }).tone (.98f, .5f, .62f, .16f).fm (2.f, 2.3f, .72f)
        .vowels (OO, AH, EE, .45f, .45f).talk (synced, d16).onset (onsetW).shape (classic, .3f).output (-5.0f));
    b.push_back (Build ("Mouth Off", "Talk", rubber, { "Talk", "Triplet" }).tone (.97f, .55f, .66f, .2f).fm (2.f, 2.5f, .7f)
        .vowels (UH, AE, IH, .4f, .5f).talk (synced, d8, triplet).shape (classic, .35f).velocity (.4f, 0.f, 0.f).output (-5.0f));
    b.push_back (Build ("Say What", "Talk", throat, { "Talk", "Question" }).tone (.98f, .66f, .58f, .17f).fm (1.f, 2.1f, .73f)
        .vowels (UH, AH, EH, .5f, .6f).talk (synced, d4).onset (onsetW).pitch (-4.f, 160.f).shape (diode, .25f).output (-1.5f));
    b.push_back (Build ("Back Talk", "Talk", rubber, { "Talk", "Mod Wheel" }).tone (1.f, .52f, .64f, .18f).fm (2.f, 2.4f, .72f)
        .vowels (OO, AH, IH, .5f, .5f).talk (modWheel).shape (classic, .3f).output (-6.0f));
    b.push_back (Build ("Gossip Line", "Talk", squeak, { "Talk", "Fast" }).tone (.96f, .3f, .64f, .12f).fm (2.f, 2.0f, .7f)
        .vowels (EH, OH, IH, .4f, .4f).talk (synced, d16, triplet).onset (onsetY).consonant (consT, .25f).output (-3.0f));
    b.push_back (Build ("Wah Wah Who", "Talk", throat, { "Talk", "Wah" }).tone (.98f, .62f, .6f, .2f).fm (1.f, 2.2f, .76f)
        .vowels (OO, AH, OO, .5f, .5f).talk (synced, d8).onset (onsetW).shape (classic, .3f).sub (.45f).output (-3.0f));
    b.push_back (Build ("Yap Trap", "Talk", squeak, { "Talk", "Aftertouch" }).tone (.96f, .32f, .7f, .16f).fm (3.f, 2.3f, .68f)
        .vowels (OO, IH, EE, .4f, .4f).talk (aftertouch).onset (onsetY).output (-9.5f));

    // ---- Growl: throaty, folded and diode-driven mid-bass.
    b.push_back (Build ("Grizzly", "Growl", throat, { "Growl", "Diode" }).tone (.92f, .8f, .72f, .5f).fm (1.f, 3.4f, .66f)
        .vowels (OO, ER, UH, .4f, .5f).length (d4).shape (diode, .5f).sub (.45f, 1).output (-6.0f));
    b.push_back (Build ("Gut Rumble", "Growl", throat, { "Growl", "Low" }).tone (.9f, .86f, .64f, .44f).fm (.5f, 3.0f, .6f)
        .vowels (UH, ER, OH, .5f, .55f).length (d4).talk (synced, d4, triplet).shape (diode, .45f).sub (.5f).output (-3.0f));
    b.push_back (Build ("Snarl Engine", "Growl", metal, { "Growl", "Mod Wheel" }).tone (.98f, .62f, .8f, .52f).fm (2.f, 3.8f, .7f)
        .vowels (OO, ER, AE, .5f, .5f).talk (modWheel).shape (fold, .55f).output (-9.5f));
    b.push_back (Build ("Beast Mode", "Growl", throat, { "Growl", "Heavy" }).tone (.94f, .72f, .84f, .6f).fm (1.f, 4.2f, .64f)
        .vowels (OH, ER, AH, .35f, .45f).talk (synced, d8).shape (fold, .6f).velocity (0.f, .3f, .3f).output (-8.0f));
    b.push_back (Build ("Fang Fold", "Growl", metal, { "Growl", "Fold" }).tone (.9f, .56f, .78f, .55f).fm (3.f, 3.4f, .68f)
        .vowels (OO, AE, ER, .3f, .4f).length (d8, triplet).shape (fold, .5f).consonant (consK, .35f).output (-10.5f));
    b.push_back (Build ("Gravel Throat", "Growl", throat, { "Growl", "Crush" }).tone (.92f, .76f, .7f, .48f).fm (1.f, 3.0f, .62f)
        .vowels (UH, OH, ER, .45f, .55f).length (d4).shape (crush, .4f).sub (.45f).output (-6.5f));
    b.push_back (Build ("Howl Pack", "Growl", throat, { "Growl", "Aftertouch" }).tone (.96f, .7f, .7f, .45f).fm (2.f, 3.2f, .7f)
        .vowels (OO, AH, ER, .5f, .5f).talk (aftertouch).shape (diode, .45f).wide (.2f).output (-7.0f));

    // ---- Squelch: resonant, wet, rubbery blips.
    b.push_back (Build ("Squelch Pit", "Squelch", squeak, { "Squelch", "Resonant" }).tone (.96f, .3f, .7f, .26f).fm (2.f, 2.6f, .95f)
        .vowels (OO, AE, IH, .25f, .3f).length (d16).shape (classic, .35f).output (-6.0f));
    b.push_back (Build ("Wet Sock", "Squelch", rubber, { "Squelch", "Rubber" }).tone (.98f, .48f, .62f, .2f).fm (1.f, 2.0f, .92f)
        .vowels (UH, OH, EH, .3f, .35f).length (d16, triplet).onset (onsetW).shape (diode, .3f).output (-0.5f));
    b.push_back (Build ("Acid Gargle", "Squelch", metal, { "Squelch", "Acid" }).tone (.95f, .36f, .76f, .34f).fm (3.f, 2.8f, .97f)
        .vowels (OO, AH, IH, .4f, .5f).talk (synced, d16).shape (diode, .4f).glide (45.f, 1).output (-7.0f));
    b.push_back (Build ("Bog Blip", "Squelch", throat, { "Squelch", "Short" }).tone (.94f, .6f, .6f, .22f).fm (1.f, 2.2f, .9f)
        .vowels (OO, ER, AH, .25f, .3f).length (d16).consonant (consK, .4f).pitch (6.f, 30.f).output (-2.5f));
    b.push_back (Build ("Slime Time", "Squelch", squeak, { "Squelch", "Triplet" }).tone (.97f, .26f, .66f, .24f).fm (2.f, 2.4f, .94f)
        .vowels (EH, OO, IH, .35f, .4f).talk (synced, d8, triplet).shape (classic, .4f).output (-4.5f));
    b.push_back (Build ("Plunger", "Squelch", rubber, { "Squelch", "Pitch" }).tone (.98f, .5f, .58f, .18f).fm (1.f, 1.9f, .9f)
        .vowels (OO, UH, OO, .4f, .4f).length (d8).pitch (-7.f, 120.f).shape (diode, .25f).output (-0.5f));

    // ---- Wobble: classic wub motion locked to the bar.
    b.push_back (Build ("Wub Lord", "Wobble", throat, { "Wobble", "Synced" }).tone (1.f, .66f, .64f, .3f).fm (1.f, 2.6f, .74f)
        .vowels (OO, AH, OO, .5f, .5f).talk (synced, d8).shape (classic, .4f).sub (.5f).output (-4.0f));
    b.push_back (Build ("Classic Wub", "Wobble", rubber, { "Wobble", "Quarter" }).tone (1.f, .6f, .6f, .26f).fm (1.f, 2.3f, .72f)
        .vowels (OO, AH, OO, .5f, .5f).talk (synced, d4).shape (classic, .35f).sub (.5f).output (-3.0f));
    b.push_back (Build ("Triplet Womp", "Wobble", throat, { "Wobble", "Triplet" }).tone (.98f, .68f, .66f, .32f).fm (1.f, 2.7f, .72f)
        .vowels (UH, AH, OO, .5f, .5f).talk (synced, d8, triplet).shape (diode, .4f).sub (.5f).output (-3.0f));
    b.push_back (Build ("Half-Time Wobble", "Wobble", throat, { "Wobble", "Slow" }).tone (1.f, .7f, .6f, .28f).fm (1.f, 2.4f, .74f)
        .vowels (OO, ER, OO, .5f, .5f).talk (synced, d2).shape (classic, .35f).sub (.5f).output (-5.0f));
    b.push_back (Build ("Dotted Drift", "Wobble", rubber, { "Wobble", "Dotted" }).tone (.98f, .58f, .62f, .26f).fm (2.f, 2.4f, .72f)
        .vowels (OO, AE, OO, .5f, .5f).talk (synced, d8, dotted).shape (classic, .35f).wide (.2f).output (-8.0f));
    b.push_back (Build ("Slow Oo-Ah", "Wobble", rubber, { "Wobble", "Bar" }).tone (1.f, .62f, .56f, .2f).fm (1.f, 2.0f, .7f)
        .vowels (OO, AH, OO, .5f, .5f).talk (synced, bar).sub (.5f).output (-3.0f));
    b.push_back (Build ("Wobble Bell", "Wobble", metal, { "Wobble", "Metal" }).tone (.96f, .4f, .76f, .3f).fm (3.5f, 2.8f, .72f)
        .vowels (OO, IH, OO, .5f, .5f).talk (synced, d16).shape (fold, .4f).output (-9.5f));

    // ---- Reese: saw-heavy, wide bodies over a mono sub.
    b.push_back (Build ("Reese Piece", "Reese", throat, { "Reese", "Wide" }).tone (.6f, .7f, .95f, .32f).fm (1.f, .8f, .5f)
        .vowels (UH, OH, UH, .5f, .5f).length (d2).talk (synced, bar).wide (.6f).shape (classic, .3f).sub (.55f).env (6.f, 160.f).output (-5.5f));
    b.push_back (Build ("Dark Reese Talk", "Reese", throat, { "Reese", "Talk" }).tone (.85f, .8f, .9f, .35f).fm (.5f, 1.2f, .58f)
        .vowels (OO, ER, UH, .5f, .5f).talk (synced, d2).wide (.5f).shape (diode, .35f).sub (.55f).env (5.f, 150.f).output (-4.0f));
    b.push_back (Build ("Widescreen Reese", "Reese", rubber, { "Reese", "Wide" }).tone (.55f, .64f, 1.f, .28f).fm (1.f, .6f, .5f)
        .vowels (OH, UH, OH, .5f, .5f).length (d2).wide (.85f).shape (classic, .3f).sub (.55f).env (8.f, 220.f).output (-5.0f));
    b.push_back (Build ("Neuro Mumble", "Reese", metal, { "Reese", "Neuro" }).tone (.8f, .62f, .92f, .45f).fm (2.f, 1.8f, .62f)
        .vowels (UH, AE, ER, .45f, .5f).talk (synced, d4, triplet).wide (.45f).shape (fold, .45f).sub (.5f).output (-8.5f));
    b.push_back (Build ("Rolling Reese", "Reese", throat, { "Reese", "Rolling" }).tone (.7f, .74f, .94f, .3f).fm (1.f, 1.0f, .55f)
        .vowels (OO, UH, OH, .5f, .5f).talk (synced, d4, dotted).wide (.55f).shape (classic, .35f).sub (.55f).output (-5.0f));
    b.push_back (Build ("Gutter Reese", "Reese", throat, { "Reese", "Dirty" }).tone (.75f, .8f, .9f, .5f).fm (.5f, 1.4f, .56f)
        .vowels (ER, UH, ER, .5f, .5f).length (d2).wide (.4f).shape (crush, .35f).sub (.55f).output (-4.0f));

    // ---- Sub: clean low end with a little speech.
    b.push_back (Build ("Clean Sub Talk", "Sub", throat, { "Sub", "Clean" }).tone (.8f, .82f, .35f, .05f).fm (1.f, 1.0f, .55f)
        .vowels (OO, UH, OH, .5f, .5f).length (d4).sub (.75f).phaseReset().output (-4.0f));
    b.push_back (Build ("808 Yoi", "Sub", rubber, { "Sub", "808" }).tone (.85f, .72f, .4f, .12f).fm (1.f, 1.2f, .6f)
        .vowels (OO, OH, EE, .4f, .45f).length (d4).sub (.8f).pitch (12.f, 70.f).phaseReset().env (1.f, 420.f).output (-6.0f));
    b.push_back (Build ("Pressure Drop", "Sub", throat, { "Sub", "Pitch" }).tone (.82f, .8f, .45f, .15f).fm (1.f, 1.4f, .6f)
        .vowels (UH, OO, OO, .5f, .5f).length (d2).sub (.75f).pitch (24.f, 400.f).phaseReset().env (2.f, 300.f).output (-7.5f));
    b.push_back (Build ("Deep Mumble", "Sub", throat, { "Sub", "Talk" }).tone (.9f, .86f, .38f, .08f).fm (1.f, 1.1f, .58f)
        .vowels (OO, ER, OO, .5f, .5f).talk (synced, d4).sub (.7f).phaseReset().output (-3.5f));
    b.push_back (Build ("Subterranean", "Sub", throat, { "Sub", "Driven" }).tone (.75f, .88f, .42f, .25f).fm (.5f, 1.2f, .55f)
        .vowels (OO, UH, OO, .5f, .5f).length (d2).sub (.7f, 2).phaseReset().env (4.f, 250.f).output (-5.5f));
    b.push_back (Build ("Sine Speech", "Sub", rubber, { "Sub", "Clean" }).tone (.92f, .7f, .3f, .03f).fm (1.f, .6f, .62f)
        .vowels (OO, AH, EE, .4f, .45f).length (d8).sub (.7f).phaseReset().onset (onsetW).velocity (.3f, 0.f, 0.f).output (-3.0f));

    // ---- Space: wide, airy and rising space-bass.
    b.push_back (Build ("Space Bass Cadet", "Space", squeak, { "Space", "Wide" }).tone (.95f, .4f, .7f, .26f).fm (3.f, 2.4f, .74f)
        .vowels (OO, AE, IH, .45f, .5f).talk (synced, d4, dotted).wide (.6f).shape (fold, .4f).env (6.f, 400.f).output (-11.0f));
    b.push_back (Build ("Nebula Yoi", "Space", rubber, { "Space", "Yoi" }).tone (.97f, .5f, .64f, .2f).fm (2.f, 2.2f, .72f)
        .vowels (OO, OH, IH, .4f, .45f).onset (onsetY).wide (.7f).pitch (-5.f, 250.f).env (8.f, 500.f).output (-9.0f));
    b.push_back (Build ("Orbit Talk", "Space", throat, { "Space", "Talk" }).tone (.96f, .6f, .66f, .25f).fm (1.5f, 2.6f, .72f)
        .vowels (UH, AE, OO, .5f, .5f).talk (synced, d2, triplet).wide (.55f).shape (classic, .4f).env (5.f, 450.f).output (-6.0f));
    b.push_back (Build ("Wormhole Wah", "Space", throat, { "Space", "Wah" }).tone (1.f, .66f, .6f, .3f).fm (1.f, 2.4f, .78f)
        .vowels (OO, AH, ER, .5f, .5f).talk (synced, bar, dotted).wide (.65f).shape (diode, .35f).env (10.f, 600.f).output (-3.5f));
    b.push_back (Build ("Starlight Squeal", "Space", squeak, { "Space", "Lead" }).tone (.94f, .24f, .76f, .22f).fm (4.f, 2.4f, .74f)
        .vowels (AH, IH, EE, .4f, .4f).pitch (-12.f, 300.f).wide (.6f).shape (fold, .3f).glide (80.f, 1).env (4.f, 500.f).output (-10.0f));
    b.push_back (Build ("Zero Gravity", "Space", rubber, { "Space", "Slow" }).tone (.98f, .54f, .6f, .18f).fm (1.f, 2.0f, .7f)
        .vowels (OO, UH, AE, .6f, .6f).length (bar).wide (.75f).env (40.f, 800.f).output (-6.0f));

    // ---- Leads/FX: pitch dives, rises and glitchy mouths.
    b.push_back (Build ("Laser Gremlin", "Leads/FX", squeak, { "Lead", "Laser" }).tone (.92f, .25f, .8f, .2f).fm (4.f, 2.6f, .7f)
        .vowels (OH, IH, EE, .3f, .3f).length (d16).pitch (12.f, 60.f).consonant (consT, .35f).shape (fold, .3f).output (-8.0f));
    b.push_back (Build ("Siren Yoi", "Leads/FX", squeak, { "FX", "Siren" }).tone (.96f, .3f, .7f, .18f).fm (2.f, 2.2f, .72f)
        .vowels (OO, AH, IH, .5f, .5f).talk (synced, d2).pitch (-12.f, 900.f).glide (120.f, 1).wide (.3f).output (-2.5f));
    b.push_back (Build ("Dive Bomb", "Leads/FX", metal, { "FX", "Dive" }).tone (.9f, .45f, .8f, .35f).fm (3.f, 3.0f, .68f)
        .vowels (EE, AH, OO, .5f, .5f).length (d2).pitch (24.f, 1200.f).bend (24).shape (fold, .45f).output (-13.0f));
    b.push_back (Build ("Riser Chirp", "Leads/FX", squeak, { "FX", "Rise" }).tone (.94f, .28f, .72f, .16f).fm (2.f, 2.2f, .7f)
        .vowels (OO, EH, IH, .4f, .4f).length (d8).pitch (-24.f, 700.f).bend (12).output (-1.0f));
    b.push_back (Build ("Glitch Mouth", "Leads/FX", metal, { "FX", "Glitch" }).tone (.9f, .4f, .84f, .45f).fm (3.f, 3.2f, .7f)
        .vowels (AE, ER, IH, .3f, .35f).talk (synced, d16, triplet).shape (crush, .5f).consonant (consCH, .4f).output (-9.0f));
    b.push_back (Build ("Robo Pluck", "Leads/FX", metal, { "Lead", "Pluck" }).tone (.88f, .35f, .78f, .25f).fm (2.f, 2.6f, .66f)
        .vowels (OH, EH, IH, .3f, .3f).length (d16).consonant (consT, .45f).env (1.f, 140.f).velocity (.3f, .4f, .2f).output (-7.5f));
    b.push_back (Build ("Zap Reply", "Leads/FX", squeak, { "FX", "Zap" }).tone (.95f, .22f, .78f, .2f).fm (4.f, 2.4f, .72f)
        .vowels (OO, IH, EE, .25f, .3f).length (d16).pitch (18.f, 40.f).consonant (consCH, .3f).shape (fold, .25f).output (-8.0f));

    std::vector<Patch> result;
    result.reserve (b.size());
    for (std::size_t i = 0; i < b.size(); ++i)
    {
        auto patch = b[i].seed (UINT64_C (0x594f4a00) + i).patch();
        patch.presetId = "factory:" + juce::String (static_cast<int> (12 + i));
        result.push_back (sanitizedPatch (std::move (patch)));
        result.back().folder = b[i].patch().folder;
        result.back().tags = b[i].patch().tags;
    }
    return result;
}
} // namespace

const std::vector<Patch>& allFactoryPresets()
{
    static const auto presets = []
    {
        std::vector<Patch> result (factoryPresets().begin(), factoryPresets().end());
        for (auto& patch : latestBank()) result.push_back (std::move (patch));
        return result;
    }();
    return presets;
}
} // namespace yoiboi
