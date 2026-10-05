#include "Patch.h"

#if JUCE_UNIT_TESTS

#include <cmath>
#include <limits>

namespace yoiboi
{
namespace
{
class PatchTests final : public juce::UnitTest
{
public:
    PatchTests() : juce::UnitTest ("YOI-BOI patch state and randomization", "YOI-BOI") {}

    void runTest() override
    {
        const auto initial = makeDefaultPatch();
        constexpr PatchLocks noLocks { false, false, false, false, false, false };

        beginTest ("Seeds produce repeatable New Yoi and Mutate results");
        for (std::uint64_t seed = 0; seed < 256; ++seed)
        {
            const auto a = newYoi (initial, noLocks, seed);
            const auto b = newYoi (initial, noLocks, seed);
            expectSamePatch (a, b);
            expectSamePatch (mutatePatch (a, noLocks, seed), mutatePatch (a, noLocks, seed));
            expect (toValueTree (a).isEquivalentTo (toValueTree (newYoi (initial, noLocks, seed + 1))) == false);
        }

        beginTest ("Every lock protects its exact associated settings");
        auto unusual = initial;
        unusual.mouth = .91f;
        unusual.startPose = 1;
        unusual.bodyPose = 2;
        unusual.endPose = 4;
        unusual.size = .23f;
        unusual.bite = .67f;
        unusual.fmRatio = 6.0f;
        unusual.fmIndex = 6.3f;
        unusual.resonance = .81f;
        unusual.dirt = .82f;
        unusual.lengthSeconds = 1.2f;
        unusual.bodyTime = .73f;
        unusual.curve = .83f;
        unusual.sub = .31f;
        unusual.tempoSync = false;
        unusual.loop = true;
        unusual.freeLengthMilliseconds = 913.0f;
        unusual.lengthDivision = 4;
        unusual.lengthFeel = 2;
        unusual.subMode = 0;
        unusual.lowCut = false;
        unusual.glideMilliseconds = 123.0f;
        unusual.outputDecibels = -13.0f;

        for (std::size_t index = 0; index < 6; ++index)
        {
            auto locks = noLocks;
            locks[index] = true;
            for (std::uint64_t seed = 0; seed < 32; ++seed)
            {
                expectLock (index, unusual, newYoi (unusual, locks, seed));
                expectLock (index, unusual, mutatePatch (unusual, locks, seed, .85f));
                const auto generated = newYoi (unusual, locks, seed);
                expect (generated.tempoSync == unusual.tempoSync && generated.loop == unusual.loop);
                expectEquals (generated.glideMilliseconds, unusual.glideMilliseconds);
                expectEquals (generated.outputDecibels, unusual.outputDecibels);
                expectEquals (generated.subMode, unusual.subMode);
                expect (generated.lowCut == unusual.lowCut);
                expectEquals (generated.lengthFeel, unusual.lengthFeel);
            }
        }
        const auto defaultLocks = defaultPatchLocks();
        expect (! defaultLocks[0] && ! defaultLocks[1] && ! defaultLocks[2] && ! defaultLocks[3]);
        expect (defaultLocks[4] && defaultLocks[5]);
        expectSamePatch (initial, mutatePatch (initial, noLocks, 999, 0.0f));

        beginTest ("Old preset files derive missing timing metadata without changing the authored duration");
        auto old = toValueTree (unusual);
        for (const auto* property : { "tempoSync", "loop", "freeLengthMilliseconds", "lengthDivision", "lengthFeel", "subMode", "lowCut", "glideMilliseconds", "outputDecibels", "presetId" })
            old.removeProperty (property, nullptr);
        const auto legacy = fromValueTree (old);
        expect (legacy.has_value());
        if (legacy)
        {
            expectEquals (legacy->lengthSeconds, unusual.lengthSeconds);
            expectEquals (legacy->freeLengthMilliseconds, unusual.lengthSeconds * 1000.0f);
            expectEquals (legacy->lengthDivision, 3);
            expect (legacy->tempoSync && ! legacy->loop);
            expectEquals (legacy->subMode, 2);
            expect (! legacy->lowCut);
            expectEquals (legacy->lengthFeel, 0);
        }

        beginTest ("Full state and unsigned 64-bit seeds survive binary and XML serialization");
        constexpr std::array<std::uint64_t, 4> seeds {
            0, 1, UINT64_C (0x8000000000000001), UINT64_C (0xffffffffffffffff)
        };
        for (auto seed : seeds)
        {
            auto patch = newYoi (unusual, noLocks, seed);
            patch.name = "Saved yoi <A&B>";
            const auto tree = toValueTree (patch);
            const auto direct = fromValueTree (tree);
            expect (direct.has_value());
            if (direct)
                expectSamePatch (patch, *direct);

            juce::MemoryOutputStream stream;
            tree.writeToStream (stream);
            const auto binary = fromValueTree (juce::ValueTree::readFromData (stream.getData(), stream.getDataSize()));
            expect (binary.has_value());
            if (binary)
                expectSamePatch (patch, *binary);

            const auto xml = tree.createXml();
            expect (xml != nullptr);
            if (xml)
            {
                const auto restored = fromValueTree (juce::ValueTree::fromXml (*xml));
                expect (restored.has_value());
                if (restored)
                    expectSamePatch (patch, *restored);
            }
        }

        beginTest ("Malformed state rejects unknown schemas and sanitizes unsafe values");
        expect (! fromValueTree (juce::ValueTree {}).has_value());
        expect (! fromValueTree (juce::ValueTree ("Unrelated")).has_value());
        auto future = toValueTree (initial);
        future.setProperty ("schemaVersion", Patch::currentSchemaVersion + 1, nullptr);
        expect (! fromValueTree (future).has_value());
        future.removeProperty ("schemaVersion", nullptr);
        expect (! fromValueTree (future).has_value());

        auto malformed = toValueTree (initial);
        malformed.setProperty ("mouth", std::numeric_limits<double>::quiet_NaN(), nullptr);
        malformed.setProperty ("size", std::numeric_limits<double>::infinity(), nullptr);
        malformed.setProperty ("bite", -42.0, nullptr);
        malformed.setProperty ("dirt", 999.0, nullptr);
        malformed.setProperty ("lengthSeconds", "not a number", nullptr);
        malformed.setProperty ("fmIndex", "1.23garbage", nullptr);
        malformed.setProperty ("curve", "1e", nullptr);
        malformed.setProperty ("seed", "not-hex", nullptr);
        malformed.setProperty ("startPose", -20, nullptr);
        malformed.setProperty ("bodyPose", 80, nullptr);
        const auto safe = fromValueTree (malformed);
        expect (safe.has_value());
        if (safe)
        {
            expectEquals (safe->mouth, initial.mouth);
            expectEquals (safe->size, initial.size);
            expectEquals (safe->bite, 0.0f);
            expectEquals (safe->dirt, 1.0f);
            expectEquals (safe->lengthSeconds, initial.lengthSeconds);
            expectEquals (safe->fmIndex, initial.fmIndex);
            expectEquals (safe->curve, initial.curve);
            expect (safe->seed == 0);
            expectEquals (safe->startPose, 0);
            expectEquals (safe->bodyPose, 4);
        }

        beginTest ("Family selection, factory presets and repeated variation remain valid");
        for (int family = 0; family < 4; ++family)
        {
            auto patch = newYoi (initial, noLocks, 1, static_cast<Family> (family));
            expect (patch.family == static_cast<Family> (family));
            for (std::uint64_t seed = 0; seed < 512; ++seed)
            {
                patch = mutatePatch (patch, noLocks, seed, 1.0f);
                expect (patch.family == static_cast<Family> (family));
                expect (std::isfinite (patch.fmIndex) && patch.fmIndex >= .1f && patch.fmIndex <= 8.0f);
                expect (patch.mouth >= 0.0f && patch.mouth <= 1.0f);
                expect (patch.lengthSeconds >= .08f && patch.lengthSeconds <= 2.0f);
                expect (patch.bodyTime >= .2f && patch.bodyTime <= .8f);
                expect (patch.startPose >= 0 && patch.startPose < 5);
                expect (patch.bodyPose >= 0 && patch.bodyPose < 5);
                expect (patch.endPose >= 0 && patch.endPose < 5);
            }
        }
        const auto& presets = factoryPresets();
        expectEquals (static_cast<int> (presets.size()), 12);
        for (std::size_t i = 0; i < presets.size(); ++i)
        {
            expect (presets[i].family == static_cast<Family> (i / 3));
            const auto restored = fromValueTree (toValueTree (presets[i]));
            expect (restored.has_value());
            if (restored)
                expectSamePatch (presets[i], *restored);
        }

        beginTest ("Authored factory sounds emphasize deliberate open-to-bright motion and clean direct subs");
        expectEquals (presets[0].name, juce::String ("Tape Bounce"));
        juce::StringArray names;
        for (const auto& patch : presets)
        {
            expect (patch.startPose >= 2 && patch.endPose <= 1);
            expect (patch.mouth >= .85f && patch.dirt <= .25f);
            expect (patch.subMode == 1 && patch.lowCut);
            expect (patch.fmIndex <= 3.2f);
            expect (! names.contains (patch.name));
            names.add (patch.name);
        }
    }

private:
    void expectSamePatch (const Patch& a, const Patch& b)
    {
        expect (toValueTree (a).isEquivalentTo (toValueTree (b)));
        expect (a.seed == b.seed);
    }

    void expectLock (std::size_t index, const Patch& before, const Patch& after)
    {
        switch (index)
        {
            case 0:
                expectEquals (before.mouth, after.mouth);
                expectEquals (before.startPose, after.startPose);
                expectEquals (before.bodyPose, after.bodyPose);
                expectEquals (before.endPose, after.endPose);
                break;
            case 1:
                expectEquals (before.size, after.size);
                break;
            case 2:
                expectEquals (before.bite, after.bite);
                expectEquals (before.fmRatio, after.fmRatio);
                expectEquals (before.fmIndex, after.fmIndex);
                expectEquals (before.resonance, after.resonance);
                break;
            case 3:
                expectEquals (before.dirt, after.dirt);
                break;
            case 4:
                expectEquals (before.lengthSeconds, after.lengthSeconds);
                expectEquals (before.freeLengthMilliseconds, after.freeLengthMilliseconds);
                expectEquals (before.lengthDivision, after.lengthDivision);
                expectEquals (before.lengthFeel, after.lengthFeel);
                expectEquals (before.bodyTime, after.bodyTime);
                expectEquals (before.curve, after.curve);
                break;
            case 5:
                expectEquals (before.sub, after.sub);
                break;
            default:
                expect (false);
                break;
        }
    }
};

PatchTests patchTests;
} // namespace
} // namespace yoiboi

#endif
