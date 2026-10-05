#include "Plugin/YoiBoiProcessor.h"
#include "Standalone/DeviceSupport.h"

#include <algorithm>
#include <cmath>
#include <memory>

// Regression tests for the 0.1.6 audit fixes: host bypass, zero-sample blocks,
// CC123 release, engine-switch fade, meter peak hold, generation family
// recall, preset rescans, Undo de-duplication, editor focus and gestures, and
// the standalone device settings/drag zone.
namespace yoiboi
{
namespace
{
struct RegressionPresetDirectory
{
    juce::File path = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("YOI-BOI-regression-" + juce::Uuid().toString());
    ~RegressionPresetDirectory() { path.deleteRecursively(); }
};

void setParameter (YoiBoiProcessor& synth, const char* id, float value)
{
    auto* parameter = synth.parameters().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

float parameterValue (YoiBoiProcessor& synth, const char* id)
{
    return synth.parameters().getRawParameterValue (id)->load();
}

juce::ValueTree savedState (YoiBoiProcessor& synth)
{
    juce::MemoryBlock data;
    synth.getStateInformation (data);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (data.getData(), static_cast<int> (data.getSize()));
    return xml == nullptr ? juce::ValueTree {} : juce::ValueTree::fromXml (*xml);
}

void restoreState (YoiBoiProcessor& synth, const juce::ValueTree& tree)
{
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), data);
    synth.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
}

struct BlockRenderer
{
    BlockRenderer (YoiBoiProcessor& processor, int samplesPerBlock)
        : synth (processor), blockSize (samplesPerBlock), audio (2, samplesPerBlock) {}

    YoiBoiProcessor& synth;
    int blockSize;
    juce::AudioBuffer<float> audio;
    juce::MidiBuffer midi;

    float block (std::initializer_list<juce::MidiMessage> events = {}, bool bypassed = false)
    {
        audio.setSize (2, blockSize, false, false, true);
        audio.clear();
        midi.clear();
        for (const auto& event : events) midi.addEvent (event, 0);
        if (bypassed) synth.processBlockBypassed (audio, midi);
        else synth.processBlock (audio, midi);
        return audio.getMagnitude (0, 0, blockSize);
    }
    float silenceAfter (int blocks) { float last = 0.0f; for (int i = 0; i < blocks; ++i) last = block(); return last; }
};

template <typename T>
T* findNamed (juce::Component& root, const juce::String& label)
{
    if (auto* match = dynamic_cast<T*> (&root))
        if (match->getName() == label || match->getTitle() == label) return match;
    for (auto* child : root.getChildren())
        if (auto* match = findNamed<T> (*child, label)) return match;
    return nullptr;
}

void pumpTimers()
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 60.0;
    do
    {
        juce::Timer::callPendingTimersSynchronously();
        juce::Thread::sleep (5);
    }
    while (juce::Time::getMillisecondCounterHiRes() < deadline);
    juce::Timer::callPendingTimersSynchronously();
}

struct Gestures final : juce::AudioProcessorParameter::Listener
{
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool started) override { started ? ++begins : ++ends; }
    int begins = 0, ends = 0;
};

dsp::VoiceControls testControls()
{
    dsp::VoiceControls c;
    c.mouth = .83f; c.size = .41f; c.bite = .66f; c.dirt = .27f; c.sub = .5f;
    c.subMode = 1; c.lowCut = true; c.output = .9f; c.lengthSeconds = .4f;
    c.bodyTime = .35f; c.curve = .45f; c.fmRatio = 2.0f; c.fmIndex = 2.6f; c.resonance = .6f;
    c.startPose = 4; c.bodyPose = 2; c.endPose = 0; c.loop = true;
    return c;
}

template <typename Controls> Controls historical (const dsp::VoiceControls& c)
{
    Controls old;
    old.mouth = c.mouth; old.size = c.size; old.bite = c.bite; old.dirt = c.dirt;
    old.sub = c.subMode == 0 ? 0.0f : c.sub;
    old.output = c.output; old.lengthSeconds = c.lengthSeconds;
    old.bodyTime = c.bodyTime; old.curve = c.curve;
    old.fmRatio = c.fmRatio; old.fmIndex = c.fmIndex; old.resonance = c.resonance;
    old.startPose = c.startPose; old.bodyPose = c.bodyPose; old.endPose = c.endPose;
    old.loop = c.loop;
    return old;
}

int countUserPresetsNamed (const std::vector<PresetItem>& items, const juce::String& name)
{
    return static_cast<int> (std::count_if (items.begin(), items.end(),
        [&] (const PresetItem& item) { return item.user && item.name == name; }));
}

juce::String userPresetId (const std::vector<PresetItem>& items, const juce::String& name)
{
    for (const auto& item : items)
        if (item.user && item.name == name) return item.id;
    return {};
}
} // namespace

class RegressionTests final : public juce::UnitTest
{
public:
    RegressionTests() : UnitTest ("YOI-BOI audit regressions", "YOI-BOI") {}

    void runTest() override
    {
        juce::MessageManager::getInstance();

        beginTest ("Parameters keep no APVTS undo history while the instrument's Undo still works");
        {
            RegressionPresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            expect (synth.parameters().undoManager == nullptr,
                    "Automation flushes must not accumulate in an unused UndoManager");
            auto callbacks = synth.makeEditorCallbacks();
            const auto original = parameterValue (synth, "mouth");
            callbacks.beginEdit ("Mouth"); setParameter (synth, "mouth", .21f); callbacks.endEdit();
            expect (callbacks.canUndo());
            callbacks.undo();
            expectWithinAbsoluteError (parameterValue (synth, "mouth"), original, .0001f);
            callbacks.redo();
            expectWithinAbsoluteError (parameterValue (synth, "mouth"), .21f, .0001f);
        }

        beginTest ("A note released during host bypass does not stick after bypass ends");
        {
            RegressionPresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            synth.prepareToPlay (48000.0, 256);
            auto callbacks = synth.makeEditorCallbacks();
            BlockRenderer render { synth, 256 };
            expect (render.block ({ juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110) }) > 0.0f);
            expect (callbacks.externalNotesHeld());
            expectEquals (render.block ({ juce::MidiMessage::noteOff (1, 36) }, true), 0.0f,
                          "Bypassed blocks are silent");
            expect (! callbacks.externalNotesHeld(), "Bypass releases held notes on entry");
            expectEquals (render.block ({}, true), 0.0f);
            expectEquals (render.silenceAfter (40), 0.0f, "No stuck note after bypass ends");
            expect (! callbacks.externalNotesHeld());

            // Notes started during bypass are not sounded later; new notes work.
            render.block ({ juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) }, true);
            expectEquals (render.silenceAfter (4), 0.0f);
            expect (render.block ({ juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100) }) > 0.0f);
            render.block ({ juce::MidiMessage::noteOff (1, 40) });
            expectEquals (render.silenceAfter (220), 0.0f, "A normal note still decays to silence");

            // The sustain pedal position is tracked through bypass.
            render.block ({ juce::MidiMessage::controllerEvent (1, 64, 127) }, true);
            render.block ({ juce::MidiMessage::noteOn (1, 43, (juce::uint8) 100) });
            render.block ({ juce::MidiMessage::noteOff (1, 43) });
            expect (callbacks.externalNotesHeld(), "A pedal pressed during bypass sustains afterwards");
            render.block ({ juce::MidiMessage::controllerEvent (1, 64, 0) });
            expect (! callbacks.externalNotesHeld());
            expectEquals (render.silenceAfter (220), 0.0f);
        }

        beginTest ("Zero-sample and zero-channel blocks still apply note-offs");
        {
            RegressionPresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            synth.prepareToPlay (48000.0, 256);
            auto callbacks = synth.makeEditorCallbacks();
            BlockRenderer render { synth, 256 };
            render.block ({ juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110) });
            juce::AudioBuffer<float> empty (2, 0);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOff (1, 36), 0);
            synth.processBlock (empty, midi);
            expect (midi.isEmpty());
            expect (! callbacks.externalNotesHeld(), "A note-off in a zero-sample block must release the note");
            expectEquals (render.silenceAfter (220), 0.0f);

            render.block ({ juce::MidiMessage::noteOn (1, 38, (juce::uint8) 110) });
            juce::AudioBuffer<float> noChannels (0, 256);
            midi.addEvent (juce::MidiMessage::noteOff (1, 38), 0);
            synth.processBlock (noChannels, midi);
            expect (midi.isEmpty());
            expect (! callbacks.externalNotesHeld(), "A note-off in a zero-channel block must release the note");
            expectEquals (render.silenceAfter (220), 0.0f);
        }

        beginTest ("CC123 releases current-engine notes; CC120 and preserved engines still cut immediately");
        {
            RegressionPresetDirectory directory;
            {
                YoiBoiProcessor synth (directory.path);
                synth.prepareToPlay (48000.0, 256);
                auto callbacks = synth.makeEditorCallbacks();
                BlockRenderer render { synth, 256 };
                render.block ({ juce::MidiMessage::noteOn (1, 36, (juce::uint8) 120) });
                for (int i = 0; i < 19; ++i) render.block();
                const auto tail = render.block ({ juce::MidiMessage::allNotesOff (1) });
                expect (tail > 0.001f, "All Notes Off lets the release tail continue");
                expect (std::abs (render.audio.getSample (0, 0)) > 0.0f);
                expect (! callbacks.externalNotesHeld());
                expectEquals (render.silenceAfter (300), 0.0f, "The released note decays to silence");

                render.block ({ juce::MidiMessage::noteOn (1, 36, (juce::uint8) 120) });
                for (int i = 0; i < 19; ++i) render.block();
                expectEquals (render.block ({ juce::MidiMessage::allSoundOff (1) }), 0.0f,
                              "All Sound Off still silences immediately");
            }
            for (const int engine : { 1, 2 })
            {
                YoiBoiProcessor synth (directory.path);
                auto state = savedState (synth);
                state.getChildWithName ("YoiBoiPatch").setProperty ("soundEngine", engine, nullptr);
                restoreState (synth, state);
                auto callbacks = synth.makeEditorCallbacks();
                expectEquals (callbacks.soundEngine(), engine);
                synth.prepareToPlay (48000.0, 256);
                BlockRenderer render { synth, 256 };
                render.block ({ juce::MidiMessage::noteOn (1, 36, (juce::uint8) 120) });
                for (int i = 0; i < 19; ++i) render.block();
                expectEquals (render.block ({ juce::MidiMessage::allNotesOff (1) }), 0.0f,
                              "Preserved engine " + juce::String (engine) + " keeps its historical CC123 hard cut");
            }
        }

        beginTest ("Engine switches fade the outgoing engine without changing any engine's steady output");
        {
            const double rate = 48000.0;
            const auto controls = testControls();
            // Steady state: identical to the bare engines, sample for sample.
            for (const int engine : { 1, 2, 3 })
            {
                dsp::CompatibleVoice voice;
                voice.prepare (rate);
                voice.setControls (controls, engine);
                expect (! voice.isFadingOutEngine(), "A silent engine switches without a fade");
                voice.noteOn (40, .9f, 0.0f, true);
                float maximumDifference = 0.0f;
                const auto compareWith = [&] (auto& bare)
                {
                    for (int i = 0; i < 9600; ++i)
                        maximumDifference = std::max (maximumDifference, std::abs (voice.renderSample() - bare.renderSample()));
                };
                if (engine == 1)
                {
                    dsp::legacy010::YoiVoice bare; bare.prepare (rate); bare.reset();
                    bare.setControls (historical<dsp::legacy010::VoiceControls> (controls)); bare.setPitchBend (0.0f);
                    bare.noteOn (40, .9f, 0.0f, true); compareWith (bare);
                }
                else if (engine == 2)
                {
                    dsp::legacy012::YoiVoice bare; bare.prepare (rate); bare.reset();
                    bare.setControls (historical<dsp::legacy012::VoiceControls> (controls)); bare.setPitchBend (0.0f);
                    bare.noteOn (40, .9f, 0.0f, true); compareWith (bare);
                }
                else
                {
                    dsp::YoiVoice bare; bare.prepare (rate); bare.setControls (controls);
                    bare.noteOn (40, .9f, 0.0f, true); compareWith (bare);
                }
                expectEquals (maximumDifference, 0.0f, "Engine " + juce::String (engine) + " output must be unchanged");
            }

            // Switching 3 -> 2 while sounding: continuous at the switch, then
            // exactly the new engine once the 5 ms fade has finished.
            dsp::CompatibleVoice voice;
            voice.prepare (rate);
            voice.setControls (controls, 3);
            voice.noteOn (40, .9f, 0.0f, true);
            dsp::legacy012::YoiVoice incoming;
            incoming.prepare (rate);
            float previous = 0.0f, lastBefore = 0.0f;
            bool found = false;
            for (int i = 0; i < 96000 && ! found; ++i)
            {
                const auto sample = voice.renderSample();
                found = i > 4800 && std::abs (sample) > .08f && std::abs (sample - previous) < .01f;
                previous = lastBefore = sample;
            }
            expect (found, "Fixture needs a loud, smooth switch point");
            voice.setControls (controls, 2);
            expect (voice.isFadingOutEngine());
            incoming.reset();
            incoming.setControls (historical<dsp::legacy012::VoiceControls> (controls));
            incoming.setPitchBend (0.0f);
            incoming.noteOn (40, .9f, 0.0f, true);
            const auto first = voice.renderSample();
            const auto incomingFirst = incoming.renderSample();
            expect (std::abs (incomingFirst - lastBefore) > .07f, "Without a fade the switch would jump");
            expect (std::abs (first - lastBefore) < .03f, "The fade keeps the output continuous at the switch");
            for (int i = 1; i < voice.engineFadeLength(); ++i) { voice.renderSample(); incoming.renderSample(); }
            expect (! voice.isFadingOutEngine());
            float afterFade = 0.0f;
            for (int i = 0; i < 9600; ++i)
                afterFade = std::max (afterFade, std::abs (voice.renderSample() - incoming.renderSample()));
            expectEquals (afterFade, 0.0f, "After the fade only the selected engine is heard, unchanged");
            expectEquals (voice.engineFadeLength(), 240);

            voice.setControls (controls, 3);
            expect (voice.isFadingOutEngine());
            voice.reset();
            expect (! voice.isFadingOutEngine(), "A hard reset also silences the outgoing engine");
            expectEquals (voice.renderSample(), 0.0f);
        }

        beginTest ("The output meter holds the peak since the editor last read it");
        {
            RegressionPresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            synth.prepareToPlay (48000.0, 64);
            auto callbacks = synth.makeEditorCallbacks();
            BlockRenderer render { synth, 64 };
            render.block ({ juce::MidiMessage::noteOn (1, 36, (juce::uint8) 120) });
            for (int i = 0; i < 40; ++i) render.block();
            expectEquals (render.block ({ juce::MidiMessage::allSoundOff (1) }), 0.0f);
            const auto left = callbacks.outputLevelLeft();
            const auto right = callbacks.outputLevelRight();
            expect (left > .01f && right > .01f, "Peaks from earlier blocks survive a silent final block");
            expectEquals (callbacks.outputLevelLeft(), 0.0f, "Reading the meter clears the hold");
            expectEquals (callbacks.outputLevelRight(), 0.0f);
        }

        beginTest ("The generation family is stored canonically and survives session recall");
        {
            RegressionPresetDirectory directory;
            YoiBoiProcessor original (directory.path);
            auto callbacks = original.makeEditorCallbacks();
            callbacks.setGenerationFamily ("RUBBER"); // the editor's combo text
            expectEquals (callbacks.generationFamily(), juce::String ("Rubber"));
            const auto state = savedState (original);
            YoiBoiProcessor restored (directory.path);
            restoreState (restored, state);
            auto restoredCallbacks = restored.makeEditorCallbacks();
            expectEquals (restoredCallbacks.generationFamily(), juce::String ("Rubber"));
            callbacks.newYoi();
            restoredCallbacks.newYoi();
            expectEquals (restoredCallbacks.currentPatchName(), callbacks.currentPatchName());
            expectEquals (restoredCallbacks.familyName(), juce::String ("Rubber"));

            auto legacy = state.createCopy();
            legacy.setProperty ("generationFamily", "METAL", nullptr); // saved by 0.1.0-0.1.6
            restoreState (restored, legacy);
            expectEquals (restoredCallbacks.generationFamily(), juce::String ("Metal"));
            legacy.setProperty ("generationFamily", "Polka", nullptr);
            restoreState (restored, legacy);
            expectEquals (restoredCallbacks.generationFamily(), juce::String ("All Families"));
            restoredCallbacks.setGenerationFamily ("ALL FAMILIES");
            expectEquals (restoredCallbacks.generationFamily(), juce::String ("All Families"));
        }

        beginTest ("Library rescans share presets between instances without duplicate files");
        {
            RegressionPresetDirectory directory;
            YoiBoiProcessor first (directory.path), second (directory.path);
            auto firstCallbacks = first.makeEditorCallbacks();
            auto secondCallbacks = second.makeEditorCallbacks();
            setParameter (first, "mouth", .31f);
            expect (firstCallbacks.savePreset ("Shared Yoi"));
            expectEquals (countUserPresetsNamed (secondCallbacks.listPresets(), "Shared Yoi"), 0);
            secondCallbacks.rescanPresets();
            expectEquals (countUserPresetsNamed (secondCallbacks.listPresets(), "Shared Yoi"), 1,
                          "A preset saved by another instance appears after a rescan");
            const auto firstId = userPresetId (firstCallbacks.listPresets(), "Shared Yoi");

            juce::Thread::sleep (25);
            setParameter (second, "mouth", .77f);
            expect (secondCallbacks.savePreset ("Shared Yoi"));
            expectEquals (directory.path.findChildFiles (juce::File::findFiles, false, "*.xml").size(), 1,
                          "Saving an existing name replaces that file instead of adding a (2) copy");
            firstCallbacks.rescanPresets();
            const auto items = firstCallbacks.listPresets();
            expectEquals (countUserPresetsNamed (items, "Shared Yoi"), 1);
            expectEquals (userPresetId (items, "Shared Yoi"), firstId, "Rescans keep Library ids stable");
            firstCallbacks.loadPreset (firstId);
            expectWithinAbsoluteError (parameterValue (first, "mouth"), .77f, .0011f);

            for (auto& file : directory.path.findChildFiles (juce::File::findFiles, false, "*.xml"))
                expect (file.deleteFile());
            setParameter (first, "mouth", .5f);
            firstCallbacks.rescanPresets();
            expectEquals (countUserPresetsNamed (firstCallbacks.listPresets(), "Shared Yoi"), 0,
                          "A deleted preset leaves the Library");
            firstCallbacks.loadPreset (firstId);
            expectWithinAbsoluteError (parameterValue (first, "mouth"), .5f, .0011f);
        }

        beginTest ("Repeated identical preset loads and resets add no empty Undo steps");
        {
            RegressionPresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            auto callbacks = synth.makeEditorCallbacks();
            const auto initial = parameterValue (synth, "mouth");
            callbacks.loadPreset ("factory:3");
            callbacks.loadPreset ("factory:3");
            expect (callbacks.canUndo());
            callbacks.undo();
            expectWithinAbsoluteError (parameterValue (synth, "mouth"), initial, .0001f);
            expect (! callbacks.canUndo(), "The second identical load must not add a step");

            callbacks.loadPreset ("factory:4");
            callbacks.resetShape();
            callbacks.resetShape();
            callbacks.undo();
            callbacks.undo();
            expectWithinAbsoluteError (parameterValue (synth, "mouth"), initial, .0001f);
        }

        beginTest ("Mouse clicks do not give controls keyboard focus; Tab still reaches them");
        {
            RegressionPresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            std::unique_ptr<juce::AudioProcessorEditor> editor (synth.createEditor());
            juce::KeyboardFocusTraverser traverser;
            const auto focusable = traverser.getAllComponents (editor.get());
            const auto check = [&] (juce::Component* control, const juce::String& title, bool mustBeTabbable)
            {
                expect (control != nullptr, title + " missing");
                if (control == nullptr) return;
                expect (control->getWantsKeyboardFocus(), title + " must stay keyboard accessible");
                expect (! control->getMouseClickGrabsKeyboardFocus(), title + " must not take focus on click");
                if (mustBeTabbable)
                    expect (std::find (focusable.begin(), focusable.end(), control) != focusable.end(),
                            title + " must be reachable with Tab");
            };
            for (const auto* title : { "Sub routing", "Length rhythm", "Generation family" })
                check (findNamed<juce::ComboBox> (*editor, title), title, true);
            for (const auto* title : { "Vowel pose", "Selected gesture point" })
                check (findNamed<juce::ComboBox> (*editor, title), title, false); // Shape page
            for (const auto* title : { "MOUTH", "SIZE", "BITE", "DIRT", "LENGTH", "SUB", "Glide", "Output" })
                check (findNamed<juce::Slider> (*editor, title), title, true);
            for (const auto* title : { "NEW YOI", "MUTATE", "Interface zoom and settings", "PLAY", "LIBRARY", "Lock MOUTH" })
                check (findNamed<juce::Button> (*editor, title), title, true);
            check (findNamed<juce::Component> (*editor, "Mouth motion: Start, Body, End"), "Gesture", true);
            if (auto* search = findNamed<juce::TextEditor> (*editor, "Search sounds"))
                expect (search->getMouseClickGrabsKeyboardFocus(), "Typing in search still needs a click to focus");

            // The scroll wheel still needs a click first, without taking focus.
            if (auto* mouth = findNamed<juce::Slider> (*editor, "MOUTH"))
            {
                auto source = juce::Desktop::getInstance().getMainMouseSource();
                const auto event = [&] (juce::ModifierKeys mods, juce::Time time)
                {
                    return juce::MouseEvent (source, { 74.0f, 60.0f }, mods, juce::MouseInputSource::defaultPressure,
                                             juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                             juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                             mouth, mouth, time, { 74.0f, 60.0f }, time, 1, false);
                };
                juce::MouseWheelDetails wheel {};
                wheel.deltaY = 0.25f;
                const auto start = juce::Time::getCurrentTime();
                const auto before = parameterValue (synth, "mouth");
                mouth->mouseWheelMove (event ({}, start), wheel);
                expectWithinAbsoluteError (parameterValue (synth, "mouth"), before, .0001f,
                                           "An unclicked knob ignores the wheel");
                const auto left = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
                mouth->mouseDown (event (left, start + juce::RelativeTime::milliseconds (10)));
                mouth->mouseUp (event (left, start + juce::RelativeTime::milliseconds (20)));
                expect (! mouth->hasKeyboardFocus (true));
                mouth->mouseWheelMove (event ({}, start + juce::RelativeTime::milliseconds (30)), wheel);
                expect (std::abs (parameterValue (synth, "mouth") - before) > .0001f,
                        "After a click the wheel adjusts the knob");
            }
        }

        beginTest ("Sync changes during a Length drag never leave a host gesture open");
        {
            RegressionPresetDirectory directory;
            YoiBoiProcessor synth (directory.path);
            std::unique_ptr<juce::AudioProcessorEditor> editor (synth.createEditor());
            pumpTimers();
            auto* length = findNamed<juce::Slider> (*editor, "LENGTH");
            expect (length != nullptr);
            if (length != nullptr)
            {
                Gestures division, freeLength;
                synth.parameters().getParameter ("length_division")->addListener (&division);
                synth.parameters().getParameter ("length_ms")->addListener (&freeLength);
                {
                    juce::Slider::ScopedDragNotification drag (*length);
                    setParameter (synth, "sync", 0.0f); // host automation flips Sync mid-drag
                    pumpTimers();
                }
                expectEquals (division.begins, 1);
                expectEquals (division.ends, 1, "The gesture must end on the parameter that started it");
                expectEquals (freeLength.begins, 0);
                expectEquals (freeLength.ends, 0, "No unmatched gesture end reaches the other parameter");
                pumpTimers();
                expect (length->getTextFromValue (length->getValue()).endsWith ("ms"),
                        "Length re-attaches to free milliseconds once the drag ends");
                synth.parameters().getParameter ("length_division")->removeListener (&division);
                synth.parameters().getParameter ("length_ms")->removeListener (&freeLength);
            }
        }

        beginTest ("Standalone settings persist only audio/MIDI devices and the drag zone spares header controls");
        {
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getChildFile ("YOI-BOI-settings-" + juce::Uuid().toString() + ".settings");
            {
                DeviceOnlySettings settings (file);
                settings.setValue ("audioSetup", "<DEVICESETUP deviceType=\"CoreAudio\" audioOutputDeviceName=\"Test Out\"/>");
                settings.setValue ("shouldMuteInput", true);
                settings.setValue ("filterState", "c291bmQ=");
                settings.setValue ("lastStateFile", "/tmp/yoi.state");
                expect (! settings.containsKey ("filterState"), "The sound is never stored");
                expect (! settings.containsKey ("lastStateFile"));
                expect (file.existsAsFile());
            }
            {
                DeviceOnlySettings reopened (file);
                expect (reopened.getValue ("audioSetup").contains ("Test Out"));
                expect (reopened.getBoolValue ("shouldMuteInput", false));
                expect (! reopened.containsKey ("filterState"));
                if (auto xml = reopened.getXmlValue ("audioSetup"))
                    expectEquals (xml->getStringAttribute ("audioOutputDeviceName"), juce::String ("Test Out"));
                else expect (false, "Device XML must round-trip");
            }
            if (auto stored = juce::parseXML (file))
            {
                juce::StringArray names;
                for (auto* value : stored->getChildIterator()) names.add (value->getStringAttribute ("name"));
                names.sort (false);
                expectEquals (names.joinIntoString (","), juce::String ("audioSetup,shouldMuteInput"));
            }
            else expect (false, "Settings file must be readable XML");
            file.deleteFile();

            juce::Component deviceEditor, shell;
            juce::TextButton zoom ("ZOOM");
            deviceEditor.setSize (1070, 724);
            deviceEditor.addAndMakeVisible (shell);
            shell.setBounds (0, 0, 1080, 764);
            shell.addAndMakeVisible (zoom);
            zoom.setBounds (342, 47, 82, 24);
            expect (startsDeviceWindowDrag (deviceEditor, &shell, { 200.0f, 30.0f }, true));
            expect (! startsDeviceWindowDrag (deviceEditor, &zoom, { 350.0f, 55.0f }, true),
                    "ZOOM/CLOSE clicks must not start a window drag");
            expect (! startsDeviceWindowDrag (deviceEditor, &shell, { 200.0f, 30.0f }, false));
            expect (! startsDeviceWindowDrag (deviceEditor, &shell, { 200.0f, 120.0f }, true));
            deviceEditor.setSize (1605, 1086);
            expect (startsDeviceWindowDrag (deviceEditor, &shell, { 300.0f, 90.0f }, true));
        }
    }
};

static RegressionTests regressionTests;
} // namespace yoiboi
