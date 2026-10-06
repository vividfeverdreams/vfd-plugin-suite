#include "Plugin/YoiBoiProcessor.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <limits>

namespace yoiboi
{
namespace
{
void setValue (YoiBoiProcessor& synth, const char* id, float value)
{
    auto* parameter = synth.parameters().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

float valueOf (YoiBoiProcessor& synth, const char* id)
{
    return synth.parameters().getRawParameterValue (id)->load();
}

juce::ValueTree stateOf (YoiBoiProcessor& synth)
{
    juce::MemoryBlock state;
    synth.getStateInformation (state);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    return xml == nullptr ? juce::ValueTree {} : juce::ValueTree::fromXml (*xml);
}

void restore (YoiBoiProcessor& synth, const juce::ValueTree& state)
{
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary (*state.createXml(), data);
    synth.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
}

struct TemporaryPresetDirectory
{
    juce::File path = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("YOI-BOI-test-" + juce::Uuid().toString());
    ~TemporaryPresetDirectory() { path.deleteRecursively(); }
};

struct TempoPlayHead final : juce::AudioPlayHead
{
    double bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo position;
        position.setBpm (bpm);
        return position;
    }
};
} // namespace

class ProcessorTests final : public juce::UnitTest
{
public:
    ProcessorTests() : UnitTest ("YOI-BOI instrument integration", "YOI-BOI") {}

    void runTest() override
    {
        beginTest ("MIDI note events render finite audio at the requested sample offset");
        {
            YoiBoiProcessor synth;
            synth.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> audio (2, 512);
            audio.clear();
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 96);
            synth.processBlock (audio, midi);

            float before = 0.0f, after = 0.0f;
            for (int i = 0; i < 512; ++i)
            {
                const auto sample = audio.getSample (0, i);
                expect (std::isfinite (sample));
                expect (std::abs (audio.getSample (1, i) - sample) < 0.000001f);
                if (i < 96) before = std::max (before, std::abs (sample));
                else after = std::max (after, std::abs (sample));
            }
            expect (before == 0.0f);
            expect (after > 0.0001f);
            expect (midi.isEmpty(), "An instrument that produces no MIDI must consume incoming MIDI");
            const auto waveform = synth.makeEditorCallbacks().waveform();
            expect (std::any_of (waveform.begin(), waveform.end(), [] (float sample) { return std::abs (sample) > 0.000001f; }));

            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOff (1, 36), 0);
            synth.processBlock (audio, midi);
            expect (! synth.makeEditorCallbacks().externalNotesHeld());
            synth.releaseResources();
        }

        beginTest ("Randomization preserves locked controls, and Undo restores the prior patch");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            const auto mouth = synth.parameters().getRawParameterValue ("mouth")->load();
            const auto length = synth.parameters().getRawParameterValue ("length_ms")->load();
            const auto sub = synth.parameters().getRawParameterValue ("sub")->load();
            expect (callbacks.isLocked (4) && callbacks.isLocked (5));
            callbacks.setLock (0, true);
            callbacks.newYoi();
            expect (std::abs (synth.parameters().getRawParameterValue ("mouth")->load() - mouth) < 0.0001f);
            expect (std::abs (synth.parameters().getRawParameterValue ("length_ms")->load() - length) < 0.01f);
            expect (std::abs (synth.parameters().getRawParameterValue ("sub")->load() - sub) < 0.0001f);
            expect (callbacks.canUndo());
            callbacks.undo();
            expect (callbacks.canRedo());
            expectEquals (callbacks.currentPatchName(), juce::String ("Tape Bounce II"));
            callbacks.redo();
            expect (callbacks.canUndo());
        }

        beginTest ("Host state restores current sound, locks and the next random sound without Undo history");
        {
            YoiBoiProcessor original;
            auto originalCallbacks = original.makeEditorCallbacks();
            originalCallbacks.setLock (0, true);
            originalCallbacks.newYoi();
            originalCallbacks.mutate();
            juce::MemoryBlock state;
            original.getStateInformation (state);
            expect (state.getSize() > 0);

            YoiBoiProcessor restored;
            restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
            auto restoredCallbacks = restored.makeEditorCallbacks();
            expect (restoredCallbacks.isLocked (0));
            expect (! restoredCallbacks.canUndo());
            expectEquals (restoredCallbacks.currentPatchName(), originalCallbacks.currentPatchName());
            for (const auto* id : { "mouth", "size", "bite", "dirt", "sub", "length_ms" })
                expect (std::abs (original.parameters().getRawParameterValue (id)->load()
                    - restored.parameters().getRawParameterValue (id)->load()) < 0.001f);

            originalCallbacks.newYoi();
            restoredCallbacks.newYoi();
            expectEquals (restoredCallbacks.currentPatchName(), originalCallbacks.currentPatchName());
            for (const auto* id : { "mouth", "size", "bite", "dirt", "sub", "length_ms" })
                expect (std::abs (original.parameters().getRawParameterValue (id)->load()
                    - restored.parameters().getRawParameterValue (id)->load()) < 0.001f);
        }

        beginTest ("Synced Length edits can be undone and its lock survives generation");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            auto* length = synth.parameters().getParameter ("length_division");
            expect (length != nullptr);
            if (length != nullptr)
            {
                callbacks.beginEdit ("Change synced length");
                length->setValueNotifyingHost (length->convertTo0to1 (3.0f));
                callbacks.endEdit();
                expect (std::abs (synth.parameters().getRawParameterValue ("length_division")->load() - 3.0f) < 0.01f);
                callbacks.newYoi();
                expect (std::abs (synth.parameters().getRawParameterValue ("length_division")->load() - 3.0f) < 0.01f);
                callbacks.undo();
                callbacks.undo();
                expect (std::abs (synth.parameters().getRawParameterValue ("length_division")->load() - 1.0f) < 0.01f);
            }
        }

        beginTest ("Undo restores both timing modes, loop, glide, output and automation edits");
        {
            YoiBoiProcessor synth;
            const auto original = stateOf (synth).getChildWithName ("PARAMETERS");
            auto callbacks = synth.makeEditorCallbacks();
            callbacks.beginEdit ("Performance settings");
            setValue (synth, "sync", 0.0f);
            setValue (synth, "loop", 1.0f);
            setValue (synth, "length_ms", 713.0f);
            setValue (synth, "length_division", 3.0f);
            setValue (synth, "length_feel", 2.0f);
            setValue (synth, "sub_mode", 0.0f);
            setValue (synth, "low_cut", 0.0f);
            setValue (synth, "glide_ms", 111.0f);
            setValue (synth, "output_db", -13.4f);
            callbacks.endEdit();
            const auto edited = stateOf (synth).getChildWithName ("PARAMETERS");
            callbacks.newYoi();
            for (const auto* id : { "sync", "loop", "length_ms", "length_division", "length_feel", "sub_mode", "low_cut", "glide_ms", "output_db" })
                expectEquals (valueOf (synth, id), static_cast<float> (edited.getChildWithProperty ("id", id).getProperty ("value")));
            callbacks.undo();
            expect (stateOf (synth).getChildWithName ("PARAMETERS").isEquivalentTo (edited));
            callbacks.undo();
            // Defaults may differ by one float ULP after APVTS applies its
            // legal-value snapping, so compare sound values, not XML bytes.
            for (const auto& parameter : original)
            {
                const auto id = parameter.getProperty ("id").toString();
                expectWithinAbsoluteError (valueOf (synth, id.toRawUTF8()),
                    static_cast<float> (parameter.getProperty ("value")), 0.0001f, id);
            }
            callbacks.redo();
            expect (stateOf (synth).getChildWithName ("PARAMETERS").isEquivalentTo (edited));

            setValue (synth, "mouth", .123f); // Host automation has no editor gesture.
            callbacks.newYoi();
            callbacks.undo();
            expectWithinAbsoluteError (valueOf (synth, "mouth"), .123f, .0001f);
        }

        beginTest ("Random sequence survives session recall after a factory preset is loaded");
        {
            YoiBoiProcessor original;
            original.makeEditorCallbacks().loadPreset ("factory:7");
            YoiBoiProcessor restored;
            restore (restored, stateOf (original));
            original.makeEditorCallbacks().newYoi();
            restored.makeEditorCallbacks().newYoi();
            expect (stateOf (original).getChildWithName ("YoiBoiPatch")
                .isEquivalentTo (stateOf (restored).getChildWithName ("YoiBoiPatch")));
        }

        beginTest ("Invalid host payloads leave the instrument intact and parameter values remain safe");
        {
            YoiBoiProcessor synth;
            synth.makeEditorCallbacks().newYoi();
            const auto goodState = stateOf (synth);
            auto broken = goodState.createCopy();
            broken.removeChild (broken.getChildWithName ("YoiBoiPatch"), nullptr);
            broken.getChildWithName ("PARAMETERS").getChildWithProperty ("id", "mouth").setProperty ("value", 0.0f, nullptr);
            restore (synth, broken);
            expect (stateOf (synth).isEquivalentTo (goodState));
            broken = goodState.createCopy();
            broken.removeChild (broken.getChildWithName ("PARAMETERS"), nullptr);
            restore (synth, broken);
            expect (stateOf (synth).isEquivalentTo (goodState));

            broken = goodState.createCopy();
            auto params = broken.getChildWithName ("PARAMETERS");
            params.getChildWithProperty ("id", "mouth").setProperty ("value", "nan", nullptr);
            params.getChildWithProperty ("id", "size").setProperty ("value", "1e999", nullptr);
            params.getChildWithProperty ("id", "body_pose").setProperty ("value", 999999, nullptr);
            params.getChildWithProperty ("id", "output_db").setProperty ("value", -999999, nullptr);
            params.getChildWithProperty ("id", "length_ms").setProperty ("value", "20junk", nullptr);
            restore (synth, broken);
            expectWithinAbsoluteError (valueOf (synth, "mouth"), makeDefaultPatch().mouth, .0001f);
            expectWithinAbsoluteError (valueOf (synth, "size"), makeDefaultPatch().size, .0001f);
            expectEquals (valueOf (synth, "body_pose"), 4.0f);
            expectEquals (valueOf (synth, "output_db"), -24.0f);
            expectEquals (valueOf (synth, "length_ms"), 280.0f);
            synth.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> audio (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);
            synth.processBlock (audio, midi);
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                expect (std::isfinite (audio.getSample (0, sample)));
        }

        beginTest ("Stray note-offs cannot revive released notes under sustain; preview yields to MIDI");
        {
            YoiBoiProcessor synth;
            synth.prepareToPlay (48000.0, 512);
            auto callbacks = synth.makeEditorCallbacks();
            juce::AudioBuffer<float> audio (2, 512);
            juce::MidiBuffer midi;
            const auto send = [&] (const juce::MidiMessage& message)
            {
                midi.clear(); midi.addEvent (message, 0); synth.processBlock (audio, midi);
            };
            send (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100));
            send (juce::MidiMessage::noteOff (1, 36));
            send (juce::MidiMessage::controllerEvent (1, 64, 127));
            send (juce::MidiMessage::noteOff (1, 36));
            expect (! callbacks.externalNotesHeld());
            send (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100));
            send (juce::MidiMessage::noteOff (1, 36));
            expect (callbacks.externalNotesHeld());
            send (juce::MidiMessage::controllerEvent (1, 64, 0));
            expect (! callbacks.externalNotesHeld());
            callbacks.togglePreview();
            midi.clear(); synth.processBlock (audio, midi);
            expect (callbacks.previewing());
            send (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100));
            expect (! callbacks.previewing() && callbacks.externalNotesHeld());
            send (juce::MidiMessage::allSoundOff (1));
            expect (! callbacks.externalNotesHeld());
            expectEquals (audio.getMagnitude (0, 512), 0.0f);

            juce::AudioBuffer<float> noChannels (0, 512);
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
            synth.processBlock (noChannels, midi);
            expect (midi.isEmpty());
        }

        beginTest ("Generation is temporary until explicit Save, with no Recent or serialized history");
        {
            TemporaryPresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            auto callbacks = synth.makeEditorCallbacks();
            const int bank = static_cast<int> (allFactoryPresets().size());
            expectEquals (static_cast<int> (callbacks.listPresets().size()), bank);
            callbacks.newYoi();
            callbacks.mutate();
            callbacks.undo();
            callbacks.newYoi();
            callbacks.beginEdit ("Mouth"); setValue (synth, "mouth", .3f); callbacks.endEdit();
            expectEquals (static_cast<int> (callbacks.listPresets().size()), bank);
            expect (! directory.path.exists(), "New Yoi, Mutate and editing must not create preset files");
            const auto savedState = stateOf (synth);
            expect (! savedState.getChildWithName ("History").isValid());
            expect (! savedState.getChildWithName ("Recent").isValid());
            YoiBoiProcessor restored (directory.path);
            restore (restored, savedState);
            expectEquals (static_cast<int> (restored.makeEditorCallbacks().listPresets().size()), bank);
            expect (! restored.makeEditorCallbacks().canUndo());
            expectEquals (restored.makeEditorCallbacks().currentPatchName(), callbacks.currentPatchName());
            expect (callbacks.savePreset ("Only This One"));
            expectEquals (directory.path.findChildFiles (juce::File::findFiles, false, "*.xml").size(), 1);
            expectEquals (static_cast<int> (callbacks.listPresets().size()), bank + 1);
        }

        beginTest ("Preset files recall complete settings and colliding legal filenames stay distinct");
        {
            TemporaryPresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            auto callbacks = synth.makeEditorCallbacks();
            setValue (synth, "sync", 0.0f);
            setValue (synth, "loop", 1.0f);
            setValue (synth, "length_ms", 917.0f);
            setValue (synth, "length_division", 4.0f);
            setValue (synth, "length_feel", 2.0f);
            setValue (synth, "sub_mode", 0.0f);
            setValue (synth, "low_cut", 0.0f);
            setValue (synth, "glide_ms", 143.0f);
            setValue (synth, "output_db", -11.2f);
            expect (callbacks.savePreset ("A/Yoi"));
            expectEquals (callbacks.currentPatchName(), juce::String ("A/Yoi"));
            const auto saved = stateOf (synth).getChildWithName ("PARAMETERS");
            callbacks.newYoi();
            expect (callbacks.savePreset ("A:Yoi"));
            expectEquals (directory.path.findChildFiles (juce::File::findFiles, false, "*.xml").size(), 2);
            YoiBoiProcessor reloaded (directory.path);
            auto loadedCallbacks = reloaded.makeEditorCallbacks();
            const auto items = loadedCallbacks.listPresets();
            const auto found = std::find_if (items.begin(), items.end(), [] (const PresetItem& item) { return item.user && item.name == "A/Yoi"; });
            expect (found != items.end());
            if (found != items.end()) loadedCallbacks.loadPreset (found->id);
            expect (stateOf (reloaded).getChildWithName ("PARAMETERS").isEquivalentTo (saved));
            expect (loadedCallbacks.savePreset ("A/Yoi"));
            expectEquals (directory.path.findChildFiles (juce::File::findFiles, false, "*.xml").size(), 2);
            const auto beforeInvalidId = loadedCallbacks.currentPatchName();
            loadedCallbacks.loadPreset ("factory:not-an-index");
            expectEquals (loadedCallbacks.currentPatchName(), beforeInvalidId);
        }

        beginTest ("Failed preset writes report failure without changing the current sound name");
        {
            TemporaryPresetDirectory directory;
            expect (directory.path.replaceWithText ("A file blocks directory creation"));
            YoiBoiProcessor synth (directory.path);
            auto callbacks = synth.makeEditorCallbacks();
            const auto before = callbacks.currentPatchName();
            expect (! callbacks.savePreset ("Cannot save"));
            expectEquals (callbacks.currentPatchName(), before);
            expect (callbacks.statusText().contains ("Could not"));
            expect (! callbacks.savePreset ("   "));
        }

        beginTest ("User and factory presets with identical names have independent Favorites");
        {
            TemporaryPresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            auto callbacks = synth.makeEditorCallbacks();
            expect (callbacks.savePreset ("Tape Bounce"));
            callbacks.toggleFavorite();
            expect (callbacks.isFavorite());
            callbacks.loadPreset ("factory:0");
            expect (! callbacks.isFavorite());
            const auto items = callbacks.listPresets();
            const auto saved = std::find_if (items.begin(), items.end(), [] (const PresetItem& item) { return item.user && item.name == "Tape Bounce"; });
            expect (saved != items.end() && saved->favorite);
            YoiBoiProcessor restored (directory.path);
            restore (restored, stateOf (synth));
            auto restoredCallbacks = restored.makeEditorCallbacks();
            expect (! restoredCallbacks.isFavorite());
            if (saved != items.end()) restoredCallbacks.loadPreset (saved->id);
            expect (restoredCallbacks.isFavorite());
        }

        beginTest ("Legacy state retains Favorites/current sound and ignores old automatic Recent history");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            callbacks.loadPreset ("factory:0");
            auto legacy = stateOf (synth);
            legacy.getChildWithName ("YoiBoiPatch").removeProperty ("presetId", nullptr);
            juce::ValueTree oldHistory ("History");
            oldHistory.addChild (toValueTree (factoryPresets()[4]), -1, nullptr);
            legacy.addChild (oldHistory, -1, nullptr);
            juce::ValueTree favorite ("Item");
            favorite.setProperty ("name", "Tape Bounce", nullptr);
            legacy.getChildWithName ("Favorites").addChild (favorite, -1, nullptr);
            restore (synth, legacy);
            expect (callbacks.isFavorite());
            const auto items = callbacks.listPresets();
            expect (! std::any_of (items.begin(), items.end(), [] (const PresetItem& item) { return item.recent; }));
            expect (! callbacks.canUndo());
        }

        beginTest ("Individual exclusions preserve only the chosen parameters through New, Mutate and recall");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            for (int i = 0; i < 6; ++i) callbacks.setLock (i, false);
            setValue (synth, "start_pose", 2.f);
            setValue (synth, "body_time", .731f);
            setValue (synth, "dirt", .417f);
            setValue (synth, "length_division", 4.f);
            for (const auto* id : { "start_pose", "body_time", "dirt", "length_division", "fmRatio", "resonance" })
                callbacks.setParameterLock (id, true);
            const auto before = stateOf (synth).getChildWithName ("YoiBoiPatch");
            for (int i = 0; i < 20; ++i)
            {
                if (i % 2 == 0) callbacks.newYoi(); else callbacks.mutate();
                expectEquals (valueOf (synth, "start_pose"), 2.f);
                expectWithinAbsoluteError (valueOf (synth, "body_time"), .731f, .0001f);
                expectWithinAbsoluteError (valueOf (synth, "dirt"), .417f, .0001f);
                expectEquals (valueOf (synth, "length_division"), 4.f);
                const auto now = stateOf (synth).getChildWithName ("YoiBoiPatch");
                expectEquals (static_cast<float> (now.getProperty ("fmRatio")), static_cast<float> (before.getProperty ("fmRatio")));
                expectEquals (static_cast<float> (now.getProperty ("resonance")), static_cast<float> (before.getProperty ("resonance")));
            }
            YoiBoiProcessor restored;
            restore (restored, stateOf (synth));
            for (const auto* id : { "start_pose", "body_time", "dirt", "length_division", "fmRatio", "resonance" })
                expect (restored.makeEditorCallbacks().isParameterLocked (id));
            expect (! restored.makeEditorCallbacks().isParameterLocked ("mouth"));
        }

        beginTest ("Including one parameter splits its group lock while protecting sibling settings");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            expect (callbacks.isLocked (4));
            callbacks.setParameterLock ("body_time", false);
            expect (! callbacks.isLocked (4));
            expect (! callbacks.isParameterLocked ("body_time"));
            for (const auto* id : { "length_ms", "length_division", "length_feel", "curve" })
                expect (callbacks.isParameterLocked (id));
            const auto before = stateOf (synth).getChildWithName ("PARAMETERS");
            callbacks.newYoi();
            for (const auto* id : { "length_ms", "length_division", "length_feel", "curve" })
                expectWithinAbsoluteError (valueOf (synth, id),
                    static_cast<float> (before.getChildWithProperty ("id", id).getProperty ("value")), .0001f);
            callbacks.setParameterLock ("not_a_parameter", true);
            expect (! callbacks.isParameterLocked ("not_a_parameter"));
        }

        beginTest ("Excluding every generated parameter prevents identical variations and empty Undo entries");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            for (int i = 0; i < 6; ++i) callbacks.setLock (i, false);
            for (const auto* id : { "mouth", "size", "bite", "dirt", "sub", "length_ms", "length_division",
                                    "start_pose", "body_pose", "end_pose", "body_time", "curve", "fmRatio", "fmIndex", "resonance",
                                    "vowel_start", "vowel_body", "vowel_end", "onset_glide", "consonant", "consonant_level",
                                    "pitch_env", "pitch_env_ms", "shaper", "squash" })
                callbacks.setParameterLock (id, true);
            const auto originalName = callbacks.currentPatchName();
            callbacks.newYoi();
            expectEquals (callbacks.currentPatchName(), originalName);
            expect (! callbacks.canUndo());
            expect (callbacks.statusText().contains ("Unlock"));
            callbacks.mutate();
            expectEquals (callbacks.currentPatchName(), originalName);
            expect (! callbacks.canUndo());
            expect (callbacks.statusText().contains ("Unlock"));
            callbacks.setParameterLock ("size", false);
            callbacks.newYoi();
            expect (callbacks.canUndo());
        }

        beginTest ("New timing and sub modes preserve original parameter indices and migrate old sessions");
        {
            YoiBoiProcessor synth;
            expectEquals (static_cast<int> (synth.getParameters().size()), static_cast<int> (parameterCount));
            expectEquals (synth.parameters().getParameter ("length_feel")->getParameterIndex(), 18);
            expectEquals (synth.parameters().getParameter ("talk_mode")->getParameterIndex(), 19);
            expectEquals (synth.parameters().getParameter ("curve")->getParameterIndex(), 15);
            expectEquals (synth.parameters().getParameter ("sub_mode")->getParameterIndex(), 16);
            expectEquals (valueOf (synth, "sub_mode"), 1.f);
            expectEquals (valueOf (synth, "low_cut"), 1.f);
            expectEquals (synth.getLatencySamples(), dsp::YoiVoice::latencySamples());
            auto legacy = stateOf (synth);
            for (const auto* id : { "sub_mode", "low_cut", "length_feel" })
            {
                auto params = legacy.getChildWithName ("PARAMETERS");
                params.removeChild (params.getChildWithProperty ("id", id), nullptr);
            }
            for (const auto* property : { "subMode", "lowCut", "lengthFeel" })
                legacy.getChildWithName ("YoiBoiPatch").removeProperty (property, nullptr);
            restore (synth, legacy);
            expectEquals (valueOf (synth, "sub_mode"), 2.f);
            expectEquals (valueOf (synth, "low_cut"), 0.f);
            expectEquals (valueOf (synth, "length_feel"), 0.f);
        }

        beginTest ("Straight, triplet and dotted lengths track host tempo including fast triplets and slow dotted bars");
        {
            struct TimingCase { double bpm; int division, feel, frames; float expectedPhase; };
            constexpr std::array<TimingCase, 5> cases {{
                { 120.0, 2, 0, 4000, 1.f / 6.f },
                { 120.0, 2, 1, 4000, .25f },
                { 120.0, 2, 2, 4000, 1.f / 9.f },
                { 300.0, 0, 1, 800, .5f },
                { 20.0, 4, 2, 4800, 1.f / 180.f }
            }};
            for (const auto& timing : cases)
            {
                TempoPlayHead playhead;
                playhead.bpm = timing.bpm;
                YoiBoiProcessor synth;
                synth.setPlayHead (&playhead);
                setValue (synth, "length_division", static_cast<float> (timing.division));
                setValue (synth, "length_feel", static_cast<float> (timing.feel));
                synth.prepareToPlay (48000.0, timing.frames);
                juce::AudioBuffer<float> audio (2, timing.frames);
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 43, (juce::uint8) 100), 0);
                synth.processBlock (audio, midi);
                expectWithinAbsoluteError (synth.makeEditorCallbacks().gesturePhase(), timing.expectedPhase, .0001f);
            }
        }
    }
};

static ProcessorTests processorTests;
} // namespace yoiboi
