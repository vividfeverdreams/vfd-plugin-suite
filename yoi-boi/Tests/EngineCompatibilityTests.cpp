#include "Plugin/YoiBoiProcessor.h"

#include <algorithm>
#include <array>

namespace yoiboi
{
namespace
{
struct EnginePresetDirectory
{
    juce::File path = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("YOI-BOI-engine-test-" + juce::Uuid().toString());
    ~EnginePresetDirectory() { path.deleteRecursively(); }
};

juce::ValueTree engineStateOf (YoiBoiProcessor& synth)
{
    juce::MemoryBlock data;
    synth.getStateInformation (data);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (data.getData(), static_cast<int> (data.getSize()));
    return xml == nullptr ? juce::ValueTree {} : juce::ValueTree::fromXml (*xml);
}

void restoreEngineState (YoiBoiProcessor& synth, const juce::ValueTree& tree)
{
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), data);
    synth.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
}

void setEngineParameter (YoiBoiProcessor& synth, const char* id, float value)
{
    auto* parameter = synth.parameters().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

// This constructs each original schema, rather than tagging new-schema data as
// old. The hosted regression suite separately reads actual released VST3 state.
juce::ValueTree historicalEngineState (YoiBoiProcessor& synth, int engine)
{
    setEngineParameter (synth, "mouth", .331f);
    setEngineParameter (synth, "size", .673f);
    setEngineParameter (synth, "sync", 0.f);
    setEngineParameter (synth, "loop", 1.f);
    setEngineParameter (synth, "length_ms", 927.f);
    setEngineParameter (synth, "length_division", 3.f);
    setEngineParameter (synth, "glide_ms", 71.f);
    setEngineParameter (synth, "output_db", -11.7f);
    auto state = engineStateOf (synth);
    auto patch = state.getChildWithName ("YoiBoiPatch");
    patch.removeProperty ("soundEngine", nullptr);
    patch.setProperty ("name", "Historical test sound", nullptr);
    // No release before 0.2.0 wrote engine-4 parameters or patch properties.
    auto allParameters = state.getChildWithName ("PARAMETERS");
    for (int index = originalParameterCount; index < parameterCount; ++index)
        allParameters.removeChild (allParameters.getChildWithProperty ("id", parameterIds[static_cast<std::size_t> (index)]), nullptr);
    for (const auto* property : { "talkMode", "talkDivision", "talkFeel", "bendRange", "glideMode", "pitchEnvSemitones",
                                  "pitchEnvMilliseconds", "width", "subPhaseReset", "shaper", "squash", "vowelStart", "vowelBody",
                                  "vowelEnd", "onsetGlide", "consonant", "consonantLevel", "velocityToMouth", "velocityToBite",
                                  "velocityToDirt", "attackMilliseconds", "releaseMilliseconds", "meterBars" })
        patch.removeProperty (property, nullptr);
    if (engine < 3)
    {
        for (const auto* property : { "subMode", "lowCut", "lengthFeel" })
            patch.removeProperty (property, nullptr);
        auto parameters = state.getChildWithName ("PARAMETERS");
        for (const auto* id : { "sub_mode", "low_cut", "length_feel" })
            parameters.removeChild (parameters.getChildWithProperty ("id", id), nullptr);
    }
    if (engine == 1)
        for (const auto* property : { "tempoSync", "loop", "freeLengthMilliseconds", "lengthDivision",
                                      "glideMilliseconds", "outputDecibels", "presetId" })
            patch.removeProperty (property, nullptr);
    return state;
}
}

class EngineCompatibilityTests final : public juce::UnitTest
{
public:
    EngineCompatibilityTests() : UnitTest ("YOI-BOI saved engine compatibility", "YOI-BOI") {}

    void runTest() override
    {
        beginTest ("Original schemas infer their exact engines and retain every host parameter");
        for (const int engine : { 1, 2, 3 })
        {
            EnginePresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            const auto historical = historicalEngineState (synth, engine);
            const auto parsed = fromValueTree (historical.getChildWithName ("YoiBoiPatch"));
            expect (parsed.has_value());
            if (parsed) expectEquals (parsed->soundEngine, engine);
            restoreEngineState (synth, historical);
            const auto callbacks = synth.makeEditorCallbacks();
            expectEquals (callbacks.soundEngine(), engine);
            expectEquals (synth.getLatencySamples(), engine < 3 ? 0 : 24);
            const auto migrated = engineStateOf (synth);
            expectEquals (static_cast<int> (migrated.getChildWithName ("YoiBoiPatch")
                .getProperty ("soundEngine")), engine);
            for (const auto& original : historical.getChildWithName ("PARAMETERS"))
            {
                const auto id = original.getProperty ("id").toString();
                const auto actual = synth.parameters().getRawParameterValue (id)->load();
                expectWithinAbsoluteError (actual, static_cast<float> (original.getProperty ("value")),
                    .0001f, "Preserved host parameter: " + id);
            }
            for (const auto* property : { "fmRatio", "fmIndex", "resonance" })
                expectEquals (static_cast<float> (migrated.getChildWithName ("YoiBoiPatch").getProperty (property)),
                    static_cast<float> (historical.getChildWithName ("YoiBoiPatch").getProperty (property)));
            expect (! directory.path.exists(), "Opening an older session must not create preset files");
        }

        beginTest ("Explicit saved engines survive new-schema saves, reopen and preparation");
        for (const int engine : { 1, 2, 3 })
        {
            EnginePresetDirectory directory;
            YoiBoiProcessor original (directory.path);
            restoreEngineState (original, historicalEngineState (original, engine));
            const auto saved = engineStateOf (original);
            expect (saved.getChildWithName ("YoiBoiPatch").hasProperty ("subMode"));
            expect (saved.getChildWithName ("YoiBoiPatch").hasProperty ("tempoSync"));
            YoiBoiProcessor reopened (directory.path);
            restoreEngineState (reopened, saved);
            reopened.prepareToPlay (48000.0, 128);
            expectEquals (reopened.makeEditorCallbacks().soundEngine(), engine);
            expectEquals (reopened.getLatencySamples(), engine < 3 ? 0 : 24);
            expectEquals (static_cast<int> (engineStateOf (reopened).getChildWithName ("YoiBoiPatch")
                .getProperty ("soundEngine")), engine);
            juce::AudioBuffer<float> audio (2, 128);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (100)), 0);
            reopened.processBlock (audio, midi);
            expect (audio.getMagnitude (0, 128) > 0.00001f);
            expectEquals (reopened.getLatencySamples(), engine < 3 ? 0 : 24);
            reopened.releaseResources();
        }

        beginTest ("Manual preset files retain classic engines when loaded into a fresh instance");
        for (const int engine : { 1, 2 })
        {
            EnginePresetDirectory directory;
            YoiBoiProcessor original (directory.path);
            restoreEngineState (original, historicalEngineState (original, engine));
            auto callbacks = original.makeEditorCallbacks();
            const juce::String name = "Classic engine " + juce::String (engine);
            expect (callbacks.savePreset (name));
            const auto files = directory.path.findChildFiles (juce::File::findFiles, false, "*.xml");
            expectEquals (files.size(), 1);
            if (! files.isEmpty())
            {
                const auto xml = juce::XmlDocument::parse (files.getReference (0));
                expect (xml != nullptr);
                if (xml)
                    expectEquals (xml->getIntAttribute ("soundEngine"), engine);
            }
            YoiBoiProcessor reopened (directory.path);
            auto reopenedCallbacks = reopened.makeEditorCallbacks();
            expectEquals (reopenedCallbacks.soundEngine(), Patch::currentSoundEngine);
            const auto presets = reopenedCallbacks.listPresets();
            const auto found = std::find_if (presets.begin(), presets.end(), [&] (const auto& item)
            { return item.user && item.name == name; });
            expect (found != presets.end());
            if (found != presets.end()) reopenedCallbacks.loadPreset (found->id);
            expectEquals (reopenedCallbacks.soundEngine(), engine);
            expectEquals (reopened.getLatencySamples(), 0);
            expectWithinAbsoluteError (reopened.parameters().getRawParameterValue ("length_ms")->load(), 927.f, .01f);
            expectWithinAbsoluteError (reopened.parameters().getRawParameterValue ("output_db")->load(), -11.7f, .0001f);
        }

        beginTest ("Opting into latest sound changes only the engine and supports Undo, Redo and reopen");
        for (const int engine : { 1, 2 })
        {
            EnginePresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            restoreEngineState (synth, historicalEngineState (synth, engine));
            auto callbacks = synth.makeEditorCallbacks();
            const auto before = engineStateOf (synth);
            expect (! callbacks.canUndo());
            callbacks.useLatestSoundEngine();
            expectEquals (callbacks.soundEngine(), Patch::currentSoundEngine);
            expectEquals (synth.getLatencySamples(), 24);
            expect (callbacks.canUndo());
            auto beforePatch = before.getChildWithName ("YoiBoiPatch").createCopy();
            auto upgradedPatch = engineStateOf (synth).getChildWithName ("YoiBoiPatch").createCopy();
            beforePatch.removeProperty ("soundEngine", nullptr);
            upgradedPatch.removeProperty ("soundEngine", nullptr);
            expect (beforePatch.isEquivalentTo (upgradedPatch));
            expect (before.getChildWithName ("PARAMETERS")
                .isEquivalentTo (engineStateOf (synth).getChildWithName ("PARAMETERS")));
            callbacks.undo();
            expectEquals (callbacks.soundEngine(), engine);
            expectEquals (synth.getLatencySamples(), 0);
            callbacks.redo();
            expectEquals (callbacks.soundEngine(), Patch::currentSoundEngine);
            expectEquals (synth.getLatencySamples(), 24);
            const auto upgraded = engineStateOf (synth);
            callbacks.useLatestSoundEngine();
            expect (engineStateOf (synth).isEquivalentTo (upgraded));
            callbacks.undo();
            expectEquals (callbacks.soundEngine(), engine, "Repeated opt-in must not create another Undo entry");
            YoiBoiProcessor reopened (directory.path);
            restoreEngineState (reopened, upgraded);
            expectEquals (reopened.makeEditorCallbacks().soundEngine(), Patch::currentSoundEngine);
            expectEquals (reopened.getLatencySamples(), 24);
        }

        beginTest ("Editing and Mutate retain the original engine through individual exclusions and Undo");
        for (const int engine : { 1, 2 })
        {
            EnginePresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            restoreEngineState (synth, historicalEngineState (synth, engine));
            auto callbacks = synth.makeEditorCallbacks();
            callbacks.beginEdit ("Adjust a classic sound");
            setEngineParameter (synth, "mouth", .421f);
            callbacks.endEdit();
            expectEquals (callbacks.soundEngine(), engine);
            callbacks.setParameterLock ("mouth", true);
            callbacks.setParameterLock ("fmRatio", true);
            const auto oldRatio = callbacks.fmRatio();
            for (int i = 0; i < 5; ++i)
            {
                callbacks.mutate();
                expectEquals (callbacks.soundEngine(), engine);
                expectEquals (synth.getLatencySamples(), 0);
                expectWithinAbsoluteError (synth.parameters().getRawParameterValue ("mouth")->load(), .421f, .0001f);
                expectEquals (callbacks.fmRatio(), oldRatio);
            }
            callbacks.undo();
            expectEquals (callbacks.soundEngine(), engine);
            callbacks.redo();
            expectEquals (callbacks.soundEngine(), engine);
            expect (! directory.path.exists(), "Editing and mutating must not save presets automatically");
        }

        beginTest ("New Yoi and new factory presets use the latest engine, originals keep 0.1.3's, and Undo restores classic");
        for (const int engine : { 1, 2 })
        {
            EnginePresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            const auto historical = historicalEngineState (synth, engine);
            restoreEngineState (synth, historical);
            auto callbacks = synth.makeEditorCallbacks();
            callbacks.setParameterLock ("mouth", true);
            callbacks.newYoi();
            expectEquals (callbacks.soundEngine(), Patch::currentSoundEngine);
            expectEquals (synth.getLatencySamples(), 24);
            expectWithinAbsoluteError (synth.parameters().getRawParameterValue ("mouth")->load(), .331f, .0001f);
            callbacks.undo();
            expectEquals (callbacks.soundEngine(), engine);
            const int bankSize = static_cast<int> (allFactoryPresets().size());
            for (const int index : { 0, 3, 6, 9, 11, 12, 30, bankSize - 1 })
            {
                restoreEngineState (synth, historical);
                callbacks.loadPreset ("factory:" + juce::String (index));
                expectEquals (callbacks.soundEngine(), index < 12 ? 3 : Patch::currentSoundEngine,
                              "Original factory sounds keep their engine; the 0.2.0 bank uses the latest");
                expectEquals (synth.getLatencySamples(), 24);
                callbacks.undo();
                expectEquals (callbacks.soundEngine(), engine);
            }
        }

        beginTest ("Unsupported explicit engine versions reject the whole state before changing sound");
        for (const int engine : { 1, 2, 3 })
        {
            EnginePresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            restoreEngineState (synth, historicalEngineState (synth, engine));
            const auto good = engineStateOf (synth);
            for (const int unsupported : { -1, 0, Patch::currentSoundEngine + 1, 999 })
            {
                auto bad = good.createCopy();
                bad.getChildWithName ("YoiBoiPatch").setProperty ("soundEngine", unsupported, nullptr);
                bad.getChildWithName ("PARAMETERS").getChildWithProperty ("id", "mouth")
                    .setProperty ("value", .997f, nullptr);
                expect (! fromValueTree (bad.getChildWithName ("YoiBoiPatch")).has_value());
                restoreEngineState (synth, bad);
                expect (engineStateOf (synth).isEquivalentTo (good));
                expectEquals (synth.makeEditorCallbacks().soundEngine(), engine);
                expectEquals (synth.getLatencySamples(), engine < 3 ? 0 : 24);
            }
        }
    }
};

static EngineCompatibilityTests engineCompatibilityTests;
}
