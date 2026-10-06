#include "Patch.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

namespace yoiboi
{
namespace
{
constexpr const char* stateType = "YoiBoiPatch";

// SplitMix64 with an explicitly defined float conversion. Unlike standard
// distribution classes, its sequence is identical across library vendors.
class Random final
{
public:
    explicit Random (std::uint64_t initialSeed) noexcept : state (initialSeed) {}

    std::uint64_t next() noexcept
    {
        auto z = (state += UINT64_C (0x9e3779b97f4a7c15));
        z = (z ^ (z >> 30)) * UINT64_C (0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)) * UINT64_C (0x94d049bb133111eb);
        return z ^ (z >> 31);
    }

    float unit() noexcept
    {
        return static_cast<float> (next() >> 40) * (1.0f / 16777216.0f);
    }

    float between (float low, float high) noexcept { return low + unit() * (high - low); }
    float centered() noexcept { return unit() - unit(); }
    int index (int count) noexcept
    {
        return std::min (count - 1, static_cast<int> (unit() * static_cast<float> (count)));
    }

private:
    std::uint64_t state;
};

struct Range
{
    float low;
    float high;
    float choose (Random& random) const noexcept { return random.between (low, high); }
    float constrain (float value) const noexcept { return std::clamp (value, low, high); }
};

struct Profile
{
    Range mouth, size, bite, dirt, fmIndex, resonance, length, bodyTime, curve;
    std::array<float, 5> ratios;
    std::array<std::array<int, 3>, 3> poses;
};

constexpr std::array<Profile, 4> profiles {{
    { { .84f, .99f }, { .40f, .64f }, { .44f, .72f }, { .07f, .26f },
      { 1.5f, 2.9f }, { .50f, .72f }, { .16f, .45f }, { .25f, .46f }, { .25f, .58f },
      { 1.0f, 2.0f, 2.0f, 2.0f, 3.0f }, {{{ 4, 2, 0 }, { 3, 1, 0 }, { 4, 3, 0 }}} },
    { { .78f, .98f }, { .26f, .52f }, { .62f, .84f }, { .10f, .32f },
      { 2.0f, 3.6f }, { .48f, .70f }, { .13f, .38f }, { .24f, .43f }, { .25f, .62f },
      { 2.0f, 2.0f, 3.0f, 3.0f, 4.0f }, {{{ 3, 2, 0 }, { 4, 1, 0 }, { 2, 3, 0 }}} },
    { { .85f, .99f }, { .62f, .82f }, { .40f, .68f }, { .10f, .30f },
      { 1.3f, 2.8f }, { .55f, .75f }, { .22f, .65f }, { .32f, .57f }, { .34f, .66f },
      { 1.0f, 1.0f, 2.0f, 2.0f, 3.0f }, {{{ 4, 2, 0 }, { 3, 2, 1 }, { 4, 3, 0 }}} },
    { { .86f, .99f }, { .16f, .37f }, { .50f, .76f }, { .04f, .20f },
      { 1.0f, 2.4f }, { .48f, .69f }, { .08f, .24f }, { .22f, .39f }, { .18f, .48f },
      { 1.0f, 2.0f, 2.0f, 3.0f, 4.0f }, {{{ 3, 1, 0 }, { 4, 2, 0 }, { 2, 1, 0 }}} }
}};

int familyIndex (Family family) noexcept
{
    return std::clamp (static_cast<int> (family), 0, 3);
}

float bounded (float value, float low, float high, float fallback) noexcept
{
    return std::isfinite (value) ? std::clamp (value, low, high) : fallback;
}

int nearestDivision (float seconds) noexcept
{
    constexpr std::array<float, 5> durations { .125f, .25f, .5f, 1.0f, 2.0f };
    int nearest = 0;
    for (int i = 1; i < static_cast<int> (durations.size()); ++i)
        if (std::abs (seconds - durations[static_cast<std::size_t> (i)])
            < std::abs (seconds - durations[static_cast<std::size_t> (nearest)])) nearest = i;
    return nearest;
}

void preservePerformance (Patch& next, const Patch& current)
{
    next.tempoSync = current.tempoSync;
    next.loop = current.loop;
    next.glideMilliseconds = current.glideMilliseconds;
    next.outputDecibels = current.outputDecibels;
    next.subMode = current.subMode;
    next.lowCut = current.lowCut;
    next.lengthFeel = current.lengthFeel;
    next.freeLengthMilliseconds = next.lengthSeconds * 1000.0f;
    next.lengthDivision = nearestDivision (next.lengthSeconds);
    // Engine-4 performance settings belong to the player, not the sound.
    next.talkMode = current.talkMode;
    next.talkDivision = current.talkDivision;
    next.talkFeel = current.talkFeel;
    next.bendRange = current.bendRange;
    next.glideMode = current.glideMode;
    next.width = current.width;
    next.subPhaseReset = current.subPhaseReset;
    next.velocityToMouth = current.velocityToMouth;
    next.velocityToBite = current.velocityToBite;
    next.velocityToDirt = current.velocityToDirt;
    next.attackMilliseconds = current.attackMilliseconds;
    next.releaseMilliseconds = current.releaseMilliseconds;
    next.meterBars = current.meterBars;
}

void applyLocks (Patch& next, const Patch& current, const PatchLocks& locks)
{
    if (locks[0])
    {
        next.mouth = current.mouth;
        next.startPose = current.startPose;
        next.bodyPose = current.bodyPose;
        next.endPose = current.endPose;
        next.vowelStart = current.vowelStart;
        next.vowelBody = current.vowelBody;
        next.vowelEnd = current.vowelEnd;
        next.onsetGlide = current.onsetGlide;
        next.consonant = current.consonant;
        next.consonantLevel = current.consonantLevel;
    }
    if (locks[1])
        next.size = current.size;
    if (locks[2])
    {
        next.bite = current.bite;
        next.fmRatio = current.fmRatio;
        next.fmIndex = current.fmIndex;
        next.resonance = current.resonance;
        next.pitchEnvSemitones = current.pitchEnvSemitones;
        next.pitchEnvMilliseconds = current.pitchEnvMilliseconds;
    }
    if (locks[3])
    {
        next.dirt = current.dirt;
        next.shaper = current.shaper;
        next.squash = current.squash;
    }
    if (locks[4])
    {
        next.lengthSeconds = current.lengthSeconds;
        next.freeLengthMilliseconds = current.freeLengthMilliseconds;
        next.lengthDivision = current.lengthDivision;
        next.lengthFeel = current.lengthFeel;
        next.bodyTime = current.bodyTime;
        next.curve = current.curve;
    }
    if (locks[5])
        next.sub = current.sub;
}

// Engine-4 extras use their own stream, so the original fields keep exactly
// the values the version-2 randomizer chose for the same seed.
constexpr std::uint64_t latestStreamSalt = UINT64_C (0x4c4154455354594f);

void randomizeLatest (Patch& next, std::uint64_t seed)
{
    Random random (seed ^ latestStreamSalt);
    const auto chance = [&random] (float probability) { return random.unit() < probability; };
    next.vowelStart = next.startPose;
    next.vowelBody = next.bodyPose;
    next.vowelEnd = next.endPose;
    switch (next.family)
    {
        case Family::Rubber:
            if (chance (.3f)) next.vowelBody = vowelUH;
            if (chance (.2f)) next.vowelStart = vowelUH;
            if (chance (.35f)) next.onsetGlide = chance (.6f) ? 2 : 1;
            if (chance (.25f)) next.consonant = chance (.5f) ? 2 : 1;
            next.shaper = chance (.3f) ? 1 : 0;
            next.squash = random.between (0.0f, .35f);
            break;
        case Family::Metal:
            if (chance (.3f)) next.vowelBody = vowelAE;
            if (chance (.25f)) next.vowelEnd = vowelIH;
            if (chance (.2f)) next.onsetGlide = 1;
            if (chance (.3f)) next.consonant = chance (.5f) ? 1 : 3;
            next.shaper = chance (.45f) ? 2 : chance (.25f) ? 3 : 0;
            next.squash = random.between (.1f, .5f);
            break;
        case Family::Throat:
            if (chance (.35f)) next.vowelBody = chance (.5f) ? vowelER : vowelUH;
            if (chance (.3f)) next.onsetGlide = 1;
            if (chance (.25f)) next.consonant = 2;
            next.shaper = chance (.45f) ? 1 : 0;
            next.squash = random.between (.05f, .4f);
            break;
        case Family::Squeak:
            if (chance (.3f)) next.vowelEnd = vowelIH;
            if (chance (.2f)) next.vowelStart = vowelIH;
            if (chance (.4f)) next.onsetGlide = 2;
            if (chance (.3f)) next.consonant = 1;
            next.shaper = chance (.25f) ? 2 : 0;
            next.squash = random.between (0.0f, .3f);
            break;
    }
    next.consonantLevel = random.between (.3f, .7f);
    if (chance (.3f))
    {
        const bool rise = next.family == Family::Squeak && chance (.4f);
        next.pitchEnvSemitones = (rise ? -1.0f : 1.0f) * random.between (3.0f, 12.0f);
        next.pitchEnvMilliseconds = random.between (40.0f, 220.0f);
    }
    else
    {
        next.pitchEnvSemitones = 0.0f;
        next.pitchEnvMilliseconds = 120.0f;
    }
}

void mutateLatest (Patch& next, const Patch& base, std::uint64_t seed, float amount)
{
    Random random (seed ^ latestStreamSalt);
    next.squash = std::clamp (base.squash + random.centered() * amount * .4f, 0.0f, 1.0f);
    next.consonantLevel = std::clamp (base.consonantLevel + random.centered() * amount * .5f, 0.0f, 1.0f);
    if (std::abs (base.pitchEnvSemitones) > .01f)
        next.pitchEnvSemitones = std::clamp (base.pitchEnvSemitones + random.centered() * amount * 8.0f, -24.0f, 24.0f);
    if (random.unit() < amount * .3f)
    {
        // The poses may have changed above; carry them into the vowel list.
        next.vowelStart = next.startPose;
        next.vowelBody = next.bodyPose;
        next.vowelEnd = next.endPose;
    }
    if (random.unit() < amount * .25f)
    {
        constexpr std::array<int, 4> extended { vowelIH, vowelUH, vowelAE, vowelER };
        next.vowelBody = extended[static_cast<std::size_t> (random.index (4))];
    }
}

juce::String generatedName (Family family, std::uint64_t seed, const char* action)
{
    return juce::String (familyName (family)) + " " + action + " "
         + juce::String::toHexString (static_cast<juce::int64> (seed & 0xffffff)).paddedLeft ('0', 6);
}

float readFloat (const juce::ValueTree& tree, const char* name, float fallback)
{
    const auto value = tree.getProperty (name);
    if (value.isDouble() || value.isInt() || value.isInt64())
        return static_cast<float> (static_cast<double> (value));
    if (! value.isString())
        return fallback;

    // XML ValueTree roundtrips turn numeric properties into strings. Validate
    // the whole decimal before using JUCE's locale-independent conversion.
    const auto text = value.toString().trim().toStdString();
    std::size_t offset = 0;
    if (offset < text.size() && (text[offset] == '-' || text[offset] == '+'))
        ++offset;
    const auto digits = [&text, &offset]
    {
        const auto start = offset;
        while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9')
            ++offset;
        return offset - start;
    };
    auto digitCount = digits();
    if (offset < text.size() && text[offset] == '.')
    {
        ++offset;
        digitCount += digits();
    }
    if (digitCount == 0)
        return fallback;
    if (offset < text.size() && (text[offset] == 'e' || text[offset] == 'E'))
    {
        ++offset;
        if (offset < text.size() && (text[offset] == '-' || text[offset] == '+'))
            ++offset;
        if (digits() == 0)
            return fallback;
    }
    return offset == text.size() ? static_cast<float> (value.toString().getDoubleValue()) : fallback;
}

int readInt (const juce::ValueTree& tree, const char* name, int fallback)
{
    const auto value = tree.getProperty (name);
    juce::int64 integer = 0;
    if (value.isInt() || value.isInt64())
        integer = static_cast<juce::int64> (value);
    else if (value.isString())
    {
        const auto text = value.toString().trim().toStdString();
        const auto parsed = std::from_chars (text.data(), text.data() + text.size(), integer);
        if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size())
            return fallback;
    }
    else
        return fallback;
    return static_cast<int> (std::clamp<juce::int64> (integer,
                                                    std::numeric_limits<int>::min(),
                                                    std::numeric_limits<int>::max()));
}

std::uint64_t readSeed (const juce::var& value)
{
    const auto string = value.toString();
    if (string.isEmpty() || string.length() > 16 || string.containsOnly ("0123456789abcdefABCDEF") == false)
        return 0;

    std::uint64_t result = 0;
    for (const auto character : string)
    {
        const auto digit = character >= '0' && character <= '9' ? character - '0'
                         : character >= 'a' && character <= 'f' ? character - 'a' + 10
                         : character - 'A' + 10;
        result = (result << 4) | static_cast<std::uint64_t> (digit);
    }
    return result;
}
} // namespace

const char* familyName (Family family) noexcept
{
    constexpr std::array<const char*, 4> names { "Rubber", "Metal", "Throat", "Squeak" };
    return names[static_cast<std::size_t> (familyIndex (family))];
}

const char* vowelName (int vowel) noexcept
{
    constexpr std::array<const char*, vowelCount> names { "EE", "EH", "AH", "OH", "OO", "IH", "UH", "AE", "ER" };
    return names[static_cast<std::size_t> (std::clamp (vowel, 0, vowelCount - 1))];
}

Patch makeDefaultPatch() { return {}; }

Patch upgradedToLatestEngine (Patch patch)
{
    patch = sanitizedPatch (std::move (patch));
    if (patch.soundEngine < Patch::firstExtendedEngine)
    {
        // The original five poses keep their index in the engine-4 vowel list.
        patch.vowelStart = patch.startPose;
        patch.vowelBody = patch.bodyPose;
        patch.vowelEnd = patch.endPose;
    }
    patch.soundEngine = Patch::currentSoundEngine;
    return patch;
}

Patch sanitizedPatch (Patch patch)
{
    const Patch fallback;
    patch.family = static_cast<Family> (familyIndex (patch.family));
    if (patch.name.trim().isEmpty())
        patch.name = fallback.name;
    patch.name = patch.name.substring (0, 128);
    patch.presetId = patch.presetId.substring (0, 256);
    patch.randomizerVersion = std::max (1, patch.randomizerVersion);
    patch.soundEngine = std::clamp (patch.soundEngine, 1, Patch::currentSoundEngine);
    patch.mouth = bounded (patch.mouth, 0.0f, 1.0f, fallback.mouth);
    patch.size = bounded (patch.size, 0.0f, 1.0f, fallback.size);
    patch.bite = bounded (patch.bite, 0.0f, 1.0f, fallback.bite);
    patch.dirt = bounded (patch.dirt, 0.0f, 1.0f, fallback.dirt);
    patch.lengthSeconds = bounded (patch.lengthSeconds, .04f, 3.0f, fallback.lengthSeconds);
    patch.sub = bounded (patch.sub, 0.0f, 1.0f, fallback.sub);
    patch.subMode = std::clamp (patch.subMode, 0, 2);
    patch.freeLengthMilliseconds = bounded (patch.freeLengthMilliseconds, 80.0f, 2000.0f, fallback.freeLengthMilliseconds);
    patch.lengthDivision = std::clamp (patch.lengthDivision, 0, 4);
    patch.lengthFeel = std::clamp (patch.lengthFeel, 0, 2);
    patch.glideMilliseconds = bounded (patch.glideMilliseconds, 0.0f, 500.0f, fallback.glideMilliseconds);
    patch.outputDecibels = bounded (patch.outputDecibels, -24.0f, 6.0f, fallback.outputDecibels);
    patch.startPose = std::clamp (patch.startPose, 0, 4);
    patch.bodyPose = std::clamp (patch.bodyPose, 0, 4);
    patch.endPose = std::clamp (patch.endPose, 0, 4);
    patch.fmRatio = bounded (patch.fmRatio, .5f, 8.0f, fallback.fmRatio);
    patch.fmIndex = bounded (patch.fmIndex, .1f, 8.0f, fallback.fmIndex);
    patch.resonance = bounded (patch.resonance, 0.0f, 1.0f, fallback.resonance);
    patch.bodyTime = bounded (patch.bodyTime, .2f, .8f, fallback.bodyTime);
    patch.curve = bounded (patch.curve, 0.0f, 1.0f, fallback.curve);
    patch.talkMode = std::clamp (patch.talkMode, 0, 3);
    patch.talkDivision = std::clamp (patch.talkDivision, 0, 4);
    patch.talkFeel = std::clamp (patch.talkFeel, 0, 2);
    patch.bendRange = std::clamp (patch.bendRange, 1, 24);
    patch.glideMode = std::clamp (patch.glideMode, 0, 1);
    patch.pitchEnvSemitones = bounded (patch.pitchEnvSemitones, -24.0f, 24.0f, 0.0f);
    patch.pitchEnvMilliseconds = bounded (patch.pitchEnvMilliseconds, 5.0f, 2000.0f, fallback.pitchEnvMilliseconds);
    patch.width = bounded (patch.width, 0.0f, 1.0f, 0.0f);
    patch.shaper = std::clamp (patch.shaper, 0, 3);
    patch.squash = bounded (patch.squash, 0.0f, 1.0f, 0.0f);
    patch.vowelStart = std::clamp (patch.vowelStart, 0, vowelCount - 1);
    patch.vowelBody = std::clamp (patch.vowelBody, 0, vowelCount - 1);
    patch.vowelEnd = std::clamp (patch.vowelEnd, 0, vowelCount - 1);
    patch.onsetGlide = std::clamp (patch.onsetGlide, 0, 2);
    patch.consonant = std::clamp (patch.consonant, 0, 3);
    patch.consonantLevel = bounded (patch.consonantLevel, 0.0f, 1.0f, fallback.consonantLevel);
    patch.velocityToMouth = bounded (patch.velocityToMouth, 0.0f, 1.0f, 0.0f);
    patch.velocityToBite = bounded (patch.velocityToBite, 0.0f, 1.0f, 0.0f);
    patch.velocityToDirt = bounded (patch.velocityToDirt, 0.0f, 1.0f, 0.0f);
    patch.attackMilliseconds = bounded (patch.attackMilliseconds, 0.5f, 200.0f, fallback.attackMilliseconds);
    patch.releaseMilliseconds = bounded (patch.releaseMilliseconds, 5.0f, 2000.0f, fallback.releaseMilliseconds);
    return patch;
}

Patch newYoi (const Patch& current, const PatchLocks& locks, std::uint64_t seed,
              std::optional<Family> requestedFamily)
{
    Random random (seed);
    Patch next;
    next.family = static_cast<Family> (requestedFamily ? familyIndex (*requestedFamily) : random.index (4));
    next.seed = seed;
    next.name = generatedName (next.family, seed, "Yoi");
    const auto& profile = profiles[static_cast<std::size_t> (familyIndex (next.family))];
    next.mouth = profile.mouth.choose (random);
    next.size = profile.size.choose (random);
    next.bite = profile.bite.choose (random);
    next.dirt = profile.dirt.choose (random);
    next.lengthSeconds = profile.length.choose (random);
    next.sub = random.between (.32f, .52f);
    next.fmRatio = profile.ratios[static_cast<std::size_t> (random.index (5))];
    next.fmIndex = profile.fmIndex.choose (random);
    next.resonance = profile.resonance.choose (random);
    next.bodyTime = profile.bodyTime.choose (random);
    next.curve = profile.curve.choose (random);

    // Higher index and resonance already increase density. Ease the drive to
    // keep those correlated choices from always accumulating maximum bite.
    const float density = std::clamp ((next.fmIndex - 3.0f) / 3.2f, 0.0f, 1.0f);
    next.dirt = profile.dirt.constrain (next.dirt - density * .12f);
    next.resonance = profile.resonance.constrain (next.resonance - density * .08f);

    const auto& poses = profile.poses[static_cast<std::size_t> (random.index (3))];
    next.startPose = poses[0];
    next.bodyPose = poses[1];
    next.endPose = poses[2];
    if (next.soundEngine >= Patch::firstExtendedEngine)
        randomizeLatest (next, seed);
    preservePerformance (next, sanitizedPatch (current));
    applyLocks (next, sanitizedPatch (current), locks);
    return sanitizedPatch (std::move (next));
}

Patch mutatePatch (const Patch& current, const PatchLocks& locks, std::uint64_t seed, float amount)
{
    const auto base = sanitizedPatch (current);
    Patch next = base;
    Random random (seed);
    amount = bounded (amount, 0.0f, 1.0f, .25f);
    if (amount == 0.0f)
        return next;

    const auto& profile = profiles[static_cast<std::size_t> (familyIndex (base.family))];
    // For manually adjusted patches, blend toward the family boundary rather
    // than snapping back into its range on the first small mutation.
    const auto nudge = [&random, amount] (float value, Range range)
    {
        const auto candidate = value + random.centered() * amount * (range.high - range.low);
        const auto constrained = range.constrain (candidate);
        const auto blended = candidate + (constrained - candidate) * amount;
        return blended;
    };
    next.mouth = nudge (base.mouth, profile.mouth);
    next.size = nudge (base.size, profile.size);
    next.bite = nudge (base.bite, profile.bite);
    next.dirt = nudge (base.dirt, profile.dirt);
    next.lengthSeconds = nudge (base.lengthSeconds, profile.length);
    next.sub = nudge (base.sub, { .32f, .52f });
    next.fmIndex = nudge (base.fmIndex, profile.fmIndex);
    next.resonance = nudge (base.resonance, profile.resonance);
    next.bodyTime = nudge (base.bodyTime, profile.bodyTime);
    next.curve = nudge (base.curve, profile.curve);
    // Discrete changes are infrequent for small mutations; pitch ratios always
    // stay in the selected family's authored harmonic choices.
    if (random.unit() < amount * .4f)
        next.fmRatio = profile.ratios[static_cast<std::size_t> (random.index (5))];
    if (random.unit() < amount * .3f)
    {
        const auto& poses = profile.poses[static_cast<std::size_t> (random.index (3))];
        next.startPose = poses[0];
        next.bodyPose = poses[1];
        next.endPose = poses[2];
    }
    next.seed = seed;
    next.randomizerVersion = Patch::currentRandomizerVersion;
    next.name = generatedName (next.family, seed, "Mutant");
    next.presetId.clear();
    if (next.soundEngine >= Patch::firstExtendedEngine)
        mutateLatest (next, base, seed, amount);
    preservePerformance (next, base);
    applyLocks (next, base, locks);
    return sanitizedPatch (std::move (next));
}

const std::array<Patch, 12>& factoryPresets()
{
    static const auto presets = []
    {
        std::array<Patch, 12> result;
        // Authored consonant-to-vowel sweeps, not renamed randomizer seeds.
        // Tape Bounce is an original tape-era bounce direction, not an artist's
        // preset or a recreation of a recording. IDs remain stable on upgrade.
        struct Recipe
        {
            const char* name;
            float mouth, size, bite, dirt, sub, ratio, index, resonance, bodyTime, curve;
            int start, body, end, division, feel;
        };
        constexpr std::array<Recipe, 12> recipes {{
            { "Tape Bounce",       .94f, .54f, .62f, .14f, .42f, 2.f, 2.2f, .64f, .32f, .40f, 4, 2, 0, 1, 0 },
            { "Bubble Trouble",    .97f, .47f, .56f, .09f, .40f, 1.f, 1.7f, .68f, .27f, .30f, 4, 3, 0, 1, 1 },
            { "Elastic Yoi",       .91f, .61f, .66f, .18f, .43f, 2.f, 2.6f, .59f, .42f, .56f, 3, 2, 0, 1, 2 },
            { "Chrome Chew",       .92f, .39f, .77f, .19f, .37f, 3.f, 2.8f, .63f, .29f, .37f, 4, 1, 0, 1, 0 },
            { "Laser Reply",       .88f, .29f, .81f, .12f, .35f, 4.f, 2.1f, .57f, .24f, .24f, 3, 1, 0, 0, 0 },
            { "Steel Syllable",     .98f, .44f, .72f, .24f, .39f, 2.f, 3.2f, .66f, .38f, .48f, 2, 3, 0, 1, 1 },
            { "Low Talker",        .96f, .77f, .51f, .13f, .46f, 1.f, 1.8f, .70f, .46f, .58f, 4, 2, 0, 2, 0 },
            { "Vowel Throw",       .99f, .68f, .61f, .21f, .42f, 2.f, 2.5f, .73f, .35f, .43f, 3, 2, 0, 1, 2 },
            { "Basement Answer",   .92f, .82f, .46f, .10f, .48f, 1.f, 1.5f, .62f, .54f, .61f, 4, 3, 1, 2, 1 },
            { "Pocket Gremlin",    .96f, .28f, .69f, .10f, .34f, 2.f, 1.9f, .61f, .26f, .28f, 3, 1, 0, 0, 0 },
            { "Squeaky Clean",     .98f, .20f, .58f, .04f, .32f, 1.f, 1.3f, .58f, .22f, .20f, 4, 2, 0, 0, 1 },
            { "Neon Chirp",        .91f, .35f, .73f, .16f, .36f, 3.f, 2.2f, .65f, .34f, .36f, 2, 1, 0, 1, 0 }
        }};
        for (std::size_t i = 0; i < result.size(); ++i)
        {
            auto& p = result[i];
            const auto& r = recipes[i];
            p.name = r.name; p.family = static_cast<Family> (i / 3);
            p.mouth = r.mouth; p.size = r.size; p.bite = r.bite; p.dirt = r.dirt; p.sub = r.sub;
            p.fmRatio = r.ratio; p.fmIndex = r.index; p.resonance = r.resonance;
            p.bodyTime = r.bodyTime; p.curve = r.curve;
            p.startPose = r.start; p.bodyPose = r.body; p.endPose = r.end;
            p.lengthDivision = r.division; p.lengthFeel = r.feel;
            constexpr std::array<float, 5> seconds { .125f, .25f, .5f, 1.f, 2.f };
            p.lengthSeconds = seconds[static_cast<std::size_t> (r.division)]
                * (r.feel == 1 ? 2.f / 3.f : r.feel == 2 ? 1.5f : 1.f);
            p.freeLengthMilliseconds = std::max (80.f, p.lengthSeconds * 1000.f);
            p.seed = UINT64_C (0x594f4901) + i;
            // The originals keep the engine and randomizer they were authored
            // with, so sessions and Favorites that reference them recall exactly.
            p.soundEngine = 3;
            p.randomizerVersion = 2;
            p.folder = "Classic";
            p.tags = juce::StringArray { "Classic", familyName (p.family) };
            result[i].presetId = "factory:" + juce::String (static_cast<int> (i));
        }
        return result;
    }();
    return presets;
}

juce::ValueTree toValueTree (const Patch& source)
{
    const auto patch = sanitizedPatch (source);
    juce::ValueTree tree (stateType);
    const auto set = [&tree] (const char* key, const juce::var& value)
    {
        tree.setProperty (key, value, nullptr);
    };
    set ("schemaVersion", Patch::currentSchemaVersion);
    set ("soundEngine", patch.soundEngine);
    set ("randomizerVersion", patch.randomizerVersion);
    set ("name", patch.name);
    set ("presetId", patch.presetId);
    set ("family", familyIndex (patch.family));
    // Store unsigned seed as hex text: no precision loss through JSON doubles
    // or signed ValueTree integer representations.
    set ("seed", juce::String::toHexString (static_cast<juce::int64> (patch.seed)).paddedLeft ('0', 16));
    set ("mouth", patch.mouth);
    set ("size", patch.size);
    set ("bite", patch.bite);
    set ("dirt", patch.dirt);
    set ("lengthSeconds", patch.lengthSeconds);
    set ("sub", patch.sub);
    set ("subMode", patch.subMode);
    set ("lowCut", patch.lowCut ? 1 : 0);
    set ("tempoSync", patch.tempoSync ? 1 : 0);
    set ("loop", patch.loop ? 1 : 0);
    set ("freeLengthMilliseconds", patch.freeLengthMilliseconds);
    set ("lengthDivision", patch.lengthDivision);
    set ("lengthFeel", patch.lengthFeel);
    set ("glideMilliseconds", patch.glideMilliseconds);
    set ("outputDecibels", patch.outputDecibels);
    set ("startPose", patch.startPose);
    set ("bodyPose", patch.bodyPose);
    set ("endPose", patch.endPose);
    set ("fmRatio", patch.fmRatio);
    set ("fmIndex", patch.fmIndex);
    set ("resonance", patch.resonance);
    set ("bodyTime", patch.bodyTime);
    set ("curve", patch.curve);
    // Engine-4 settings (0.2.0). Earlier releases ignore unknown properties.
    set ("talkMode", patch.talkMode);
    set ("talkDivision", patch.talkDivision);
    set ("talkFeel", patch.talkFeel);
    set ("bendRange", patch.bendRange);
    set ("glideMode", patch.glideMode);
    set ("pitchEnvSemitones", patch.pitchEnvSemitones);
    set ("pitchEnvMilliseconds", patch.pitchEnvMilliseconds);
    set ("width", patch.width);
    set ("subPhaseReset", patch.subPhaseReset ? 1 : 0);
    set ("shaper", patch.shaper);
    set ("squash", patch.squash);
    set ("vowelStart", patch.vowelStart);
    set ("vowelBody", patch.vowelBody);
    set ("vowelEnd", patch.vowelEnd);
    set ("onsetGlide", patch.onsetGlide);
    set ("consonant", patch.consonant);
    set ("consonantLevel", patch.consonantLevel);
    set ("velocityToMouth", patch.velocityToMouth);
    set ("velocityToBite", patch.velocityToBite);
    set ("velocityToDirt", patch.velocityToDirt);
    set ("attackMilliseconds", patch.attackMilliseconds);
    set ("releaseMilliseconds", patch.releaseMilliseconds);
    set ("meterBars", patch.meterBars ? 1 : 0);
    return tree;
}

std::optional<Patch> fromValueTree (const juce::ValueTree& tree)
{
    if (! tree.hasType (stateType)
        || readInt (tree, "schemaVersion", 0) != Patch::currentSchemaVersion)
        return std::nullopt;

    Patch patch;
    // Releases before 0.1.4 had no engine tag. Their patch layouts provide
    // unambiguous migration markers. Never silently render a future engine
    // with an older algorithm if a future file is opened in this build.
    const int inferredEngine = tree.hasProperty ("subMode") ? 3
                             : tree.hasProperty ("tempoSync") ? 2 : 1;
    patch.soundEngine = readInt (tree, "soundEngine", inferredEngine);
    if (patch.soundEngine < 1 || patch.soundEngine > Patch::currentSoundEngine)
        return std::nullopt;
    const auto name = tree.getProperty ("name");
    if (name.isString())
        patch.name = name.toString();
    patch.presetId = tree.getProperty ("presetId").toString();
    patch.family = static_cast<Family> (std::clamp (readInt (tree, "family", 0), 0, 3));
    patch.seed = readSeed (tree.getProperty ("seed"));
    patch.randomizerVersion = readInt (tree, "randomizerVersion", Patch::currentRandomizerVersion);
    patch.mouth = readFloat (tree, "mouth", patch.mouth);
    patch.size = readFloat (tree, "size", patch.size);
    patch.bite = readFloat (tree, "bite", patch.bite);
    patch.dirt = readFloat (tree, "dirt", patch.dirt);
    patch.lengthSeconds = readFloat (tree, "lengthSeconds", patch.lengthSeconds);
    patch.sub = readFloat (tree, "sub", patch.sub);
    // Existing presets retain the driven sub and full-range character path.
    patch.subMode = readInt (tree, "subMode", 2);
    patch.lowCut = readInt (tree, "lowCut", 0) != 0;
    patch.tempoSync = readInt (tree, "tempoSync", 1) != 0;
    patch.loop = readInt (tree, "loop", 0) != 0;
    patch.freeLengthMilliseconds = readFloat (tree, "freeLengthMilliseconds", patch.lengthSeconds * 1000.0f);
    patch.lengthDivision = readInt (tree, "lengthDivision", nearestDivision (patch.lengthSeconds));
    patch.lengthFeel = readInt (tree, "lengthFeel", 0);
    patch.glideMilliseconds = readFloat (tree, "glideMilliseconds", patch.glideMilliseconds);
    patch.outputDecibels = readFloat (tree, "outputDecibels", patch.outputDecibels);
    patch.startPose = readInt (tree, "startPose", patch.startPose);
    patch.bodyPose = readInt (tree, "bodyPose", patch.bodyPose);
    patch.endPose = readInt (tree, "endPose", patch.endPose);
    patch.fmRatio = readFloat (tree, "fmRatio", patch.fmRatio);
    patch.fmIndex = readFloat (tree, "fmIndex", patch.fmIndex);
    patch.resonance = readFloat (tree, "resonance", patch.resonance);
    patch.bodyTime = readFloat (tree, "bodyTime", patch.bodyTime);
    patch.curve = readFloat (tree, "curve", patch.curve);
    patch.talkMode = readInt (tree, "talkMode", patch.talkMode);
    patch.talkDivision = readInt (tree, "talkDivision", patch.talkDivision);
    patch.talkFeel = readInt (tree, "talkFeel", patch.talkFeel);
    patch.bendRange = readInt (tree, "bendRange", patch.bendRange);
    patch.glideMode = readInt (tree, "glideMode", patch.glideMode);
    patch.pitchEnvSemitones = readFloat (tree, "pitchEnvSemitones", patch.pitchEnvSemitones);
    patch.pitchEnvMilliseconds = readFloat (tree, "pitchEnvMilliseconds", patch.pitchEnvMilliseconds);
    patch.width = readFloat (tree, "width", patch.width);
    patch.subPhaseReset = readInt (tree, "subPhaseReset", 0) != 0;
    patch.shaper = readInt (tree, "shaper", patch.shaper);
    patch.squash = readFloat (tree, "squash", patch.squash);
    // Files written before 0.2.0 have no vowel list: carry the poses over.
    patch.vowelStart = readInt (tree, "vowelStart", patch.startPose);
    patch.vowelBody = readInt (tree, "vowelBody", patch.bodyPose);
    patch.vowelEnd = readInt (tree, "vowelEnd", patch.endPose);
    patch.onsetGlide = readInt (tree, "onsetGlide", patch.onsetGlide);
    patch.consonant = readInt (tree, "consonant", patch.consonant);
    patch.consonantLevel = readFloat (tree, "consonantLevel", patch.consonantLevel);
    patch.velocityToMouth = readFloat (tree, "velocityToMouth", patch.velocityToMouth);
    patch.velocityToBite = readFloat (tree, "velocityToBite", patch.velocityToBite);
    patch.velocityToDirt = readFloat (tree, "velocityToDirt", patch.velocityToDirt);
    patch.attackMilliseconds = readFloat (tree, "attackMilliseconds", patch.attackMilliseconds);
    patch.releaseMilliseconds = readFloat (tree, "releaseMilliseconds", patch.releaseMilliseconds);
    patch.meterBars = readInt (tree, "meterBars", 0) != 0;
    return sanitizedPatch (std::move (patch));
}
} // namespace yoiboi
