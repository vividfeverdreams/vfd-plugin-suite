#include "Plugin/YoiBoiProcessor.h"

#include <cmath>
#include <memory>
#include <set>

namespace yoiboi
{
namespace
{
template <typename T>
T* findControl (juce::Component& root, const juce::String& label)
{
    if (auto* match = dynamic_cast<T*> (&root))
        if (match->getName() == label || match->getTitle() == label)
            return match;
    for (auto* child : root.getChildren())
        if (auto* match = findControl<T> (*child, label))
            return match;
    return nullptr;
}

void pumpEditor()
{
    // A single call may only release TimerThread's wait for an earlier posted
    // callback. Dispatch repeatedly so newly added timers advance too, as they
    // would in the host's message loop.
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 50.0;
    do
    {
        juce::Timer::callPendingTimersSynchronously();
        juce::Thread::sleep (5);
    }
    while (juce::Time::getMillisecondCounterHiRes() < deadline);
    juce::Timer::callPendingTimersSynchronously();
}

bool waitForEditor (const std::function<bool()>& ready)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 500.0;
    do
    {
        juce::Timer::callPendingTimersSynchronously();
        if (ready()) return true;
        juce::Thread::sleep (5);
    }
    while (juce::Time::getMillisecondCounterHiRes() < deadline);
    return ready();
}

bool clickControl (juce::Component& editor, const juce::String& title)
{
    if (auto* button = findControl<juce::Button> (editor, title))
    {
        if (! button->isEnabled()) return false;
        if (button->getClickingTogglesState())
            // Notify both Button listeners and onClick exactly once, as an
            // actual click does. Calling onClick alone skips attachments.
            button->setToggleState (! button->getToggleState(), juce::sendNotificationSync);
        else if (button->onClick) button->onClick();
        pumpEditor();
        return true;
    }
    return false;
}

struct TemporaryPresetFolder
{
    juce::File path = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("YOI-BOI-random-preset-" + juce::Uuid().toString());
    ~TemporaryPresetFolder() { path.deleteRecursively(); }
};

// The Library id of the preset the current sound was loaded from, or "" for a generated sound.
juce::String currentPresetId (EditorCallbacks& callbacks)
{
    for (const auto& item : callbacks.listPresets())
        if (item.current) return item.id;
    return {};
}

const PresetItem* findPreset (const std::vector<PresetItem>& items, const juce::String& id)
{
    for (const auto& item : items)
        if (item.id == id) return &item;
    return nullptr;
}

struct GestureCounter final : juce::AudioProcessorParameter::Listener
{
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool started) override
    {
        if (started) ++begins;
        else ++ends;
    }
    int begins = 0, ends = 0;
};
}

class EditorInteractionTests final : public juce::UnitTest
{
public:
    EditorInteractionTests() : UnitTest ("YOI-BOI editor interactions", "YOI-BOI") {}

    void runTest() override
    {
        juce::MessageManager::getInstance();

        beginTest ("INFO opens and closes without changing the sound");
        {
            YoiBoiProcessor synth;
            std::unique_ptr<juce::AudioProcessorEditor> editor(synth.createEditor());
            editor->setVisible(true);
            auto* info = findControl<juce::Button>(*editor, "INFO");
            auto* page = findControl<juce::Component>(*editor, "VFD Info Page");
            auto* close = findControl<juce::Button>(*editor, "Close info");
            expect(info != nullptr && page != nullptr && close != nullptr);
            if (info && page && close)
            {
                std::vector<float> before;
                for (auto* parameter : synth.getParameters()) before.push_back(parameter->getValue());
                expect(!page->isVisible());
                expectEquals(editor->getWidth(), 1055);
                expectEquals(editor->getHeight(), 717);
                expect(clickControl(*editor, "INFO"));
                expect(page->isVisible());
                std::set<juce::String> urls;
                const auto collectLinks = [&](const auto& self, juce::Component& root) -> void
                {
                    if (auto* link = dynamic_cast<juce::HyperlinkButton*>(&root))
                        urls.insert(link->getURL().toString(true));
                    for (auto* child : root.getChildren()) self(self, *child);
                };
                collectLinks(collectLinks, *page);
                expect(urls == std::set<juce::String>{"https://x.com/cxy", "https://x.com/clt",
                    "https://www.vividfeverdreams.xyz", "https://fundraising.fracturedatlas.org/dream-sequence"});
                auto* random = findControl<juce::Button>(*editor, "NEW YOI");
                expect(random != nullptr);
                if (random)
                {
                    auto* hit = editor->getComponentAt(editor->getLocalPoint(random, random->getLocalBounds().getCentre()));
                    expect(hit == page || page->isParentOf(hit), "Info page intercepts clicks over the instrument controls");
                }
                expect(page->keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
                expect(page->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)));
                expect(page->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
                expect(!page->isVisible());
                expect(clickControl(*editor, "INFO"));
                expect(page->isVisible());
                expect(clickControl(*editor, "Close info"));
                expect(!page->isVisible());
                for (int index = 0; index < synth.getParameters().size(); ++index)
                    expectEquals(synth.getParameters()[index]->getValue(), before[static_cast<size_t>(index)]);
            }
        }

        beginTest ("Shape percentage entry uses the same units as its display");
        {
            YoiBoiProcessor synth;
            std::unique_ptr<juce::AudioProcessorEditor> editor (synth.createEditor());
            expect (clickControl (*editor, "SHAPE"));
            auto* body = findControl<juce::Slider> (*editor, "Body timing");
            auto* curve = findControl<juce::Slider> (*editor, "Curve");
            expect (body != nullptr && curve != nullptr);
            if (body != nullptr && curve != nullptr)
            {
                expect (body->isVisible() && curve->isVisible());
                expectWithinAbsoluteError (body->getValueFromText ("50%"), 0.5, 0.00001);
                expectWithinAbsoluteError (body->getValueFromText ("20"), 0.2, 0.00001);
                expectWithinAbsoluteError (body->getValueFromText (body->getTextFromValue (0.65)), 0.65, 0.00001);
                expectWithinAbsoluteError (curve->getValueFromText ("25%"), 0.25, 0.00001);
                expectWithinAbsoluteError (curve->getValueFromText (curve->getTextFromValue (0.75)), 0.75, 0.00001);
            }
        }

        beginTest ("Keyboard macro edits reach the engine and can be undone");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            std::unique_ptr<juce::AudioProcessorEditor> editor (synth.createEditor());
            auto* mouth = findControl<juce::Slider> (*editor, "MOUTH");
            expect (mouth != nullptr);
            if (mouth != nullptr)
            {
                const auto original = synth.parameters().getRawParameterValue ("mouth")->load();
                auto* parameter = synth.parameters().getParameter ("mouth");
                GestureCounter gestures;
                parameter->addListener (&gestures);
                expectWithinAbsoluteError (mouth->getValueFromText ("62%"), 0.62, 0.00001);
                expect (mouth->keyPressed (juce::KeyPress (juce::KeyPress::upKey)));
                expect (synth.parameters().getRawParameterValue ("mouth")->load() > original);
                expectEquals (gestures.begins, 1, "Host automation must receive a gesture start.");
                expectEquals (gestures.ends, 1, "Host automation must receive a matching gesture end.");
                parameter->removeListener (&gestures);
                expect (callbacks.canUndo(), "An arrow-key edit must create an Undo entry.");
                callbacks.undo();
                expectWithinAbsoluteError (synth.parameters().getRawParameterValue ("mouth")->load(), original, 0.00001f);
            }
        }

        beginTest ("Shape selectors change only the chosen pose and are undoable");
        for (const bool latest : { true, false })
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            if (! latest) callbacks.loadPreset ("factory:0"); // an original (engine 3) sound: five poses
            std::unique_ptr<juce::AudioProcessorEditor> editor (synth.createEditor());
            pumpEditor();
            expect (clickControl (*editor, "SHAPE"));
            auto* selected = findControl<juce::ComboBox> (*editor, "Selected gesture point");
            auto* pose = findControl<juce::ComboBox> (*editor, "Vowel pose");
            expect (selected != nullptr && pose != nullptr);
            if (selected != nullptr && pose != nullptr)
            {
                // Engine 4 offers nine vowels in the vowel_* parameters; the originals keep five poses.
                expectEquals (pose->getNumItems(), latest ? 9 : 5);
                const char* startId = latest ? "vowel_start" : "start_pose";
                const char* bodyId = latest ? "vowel_body" : "body_pose";
                const auto originalStart = synth.parameters().getRawParameterValue (startId)->load();
                const auto originalBody = synth.parameters().getRawParameterValue (bodyId)->load();
                const auto changedStart = latest ? (originalStart == 7.0f ? 8.0f : 7.0f) : (originalStart == 4.0f ? 0.0f : 4.0f);
                selected->setSelectedId (1, juce::sendNotificationSync);
                pose->setSelectedId (static_cast<int> (changedStart) + 1, juce::sendNotificationSync);
                expectEquals (synth.parameters().getRawParameterValue (startId)->load(), changedStart);
                expectEquals (synth.parameters().getRawParameterValue (bodyId)->load(), originalBody);
                expect (callbacks.canUndo());
                callbacks.undo();
                expectEquals (synth.parameters().getRawParameterValue (startId)->load(), originalStart);
            }
        }

        beginTest ("All locks disable generation while keeping manual controls usable");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            for (int i = 0; i < 6; ++i) callbacks.setLock (i, true);
            std::unique_ptr<juce::AudioProcessorEditor> editor (synth.createEditor());
            auto* generate = findControl<juce::Button> (*editor, "NEW YOI");
            auto* mutate = findControl<juce::Button> (*editor, "MUTATE");
            auto* mouth = findControl<juce::Slider> (*editor, "MOUTH");
            expect (generate != nullptr && mutate != nullptr && mouth != nullptr);
            if (generate != nullptr && mutate != nullptr && mouth != nullptr)
            {
                expect (! generate->isEnabled() && ! mutate->isEnabled());
                expect (mouth->isEnabled());
                callbacks.setLock (0, false);
                expect (! callbacks.isLocked (0));
                expect (waitForEditor ([&] { return generate->isEnabled() && mutate->isEnabled(); }),
                        "The editor must enable generation after the final protected group is unlocked.");
            }
            expect (clickControl (*editor, "SHAPE"));
            expect (clickControl (*editor, "LOCK"));
            expect (callbacks.isLocked (0) && callbacks.isLocked (4), "Motion lock must lock Mouth and Length together.");
            expect (clickControl (*editor, "LOCK"));
            expect (! callbacks.isLocked (0) && ! callbacks.isLocked (4), "Motion lock must unlock both groups together.");
        }

        beginTest ("Individual exclusions disable generation when every sound parameter is protected");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            for (int group = 0; group < 6; ++group) callbacks.setLock (group, false);
            // Every generated parameter, including the engine-4 vowels, articulation and character.
            for (const auto* id : { "mouth", "size", "bite", "dirt", "sub", "length_ms", "length_division",
                                   "start_pose", "body_pose", "end_pose", "body_time", "curve", "fmRatio", "fmIndex", "resonance",
                                   "vowel_start", "vowel_body", "vowel_end", "onset_glide", "consonant", "consonant_level",
                                   "pitch_env", "pitch_env_ms", "shaper", "squash" })
                callbacks.setParameterLock (id, true);
            std::unique_ptr<juce::AudioProcessorEditor> editor (synth.createEditor());
            auto* generate = findControl<juce::Button> (*editor, "NEW YOI");
            auto* mutate = findControl<juce::Button> (*editor, "MUTATE");
            expect (generate != nullptr && mutate != nullptr);
            if (generate != nullptr && mutate != nullptr)
            {
                expect (! generate->isEnabled() && ! mutate->isEnabled());
                callbacks.setParameterLock ("mouth", false);
                expect (waitForEditor ([&] { return generate->isEnabled() && mutate->isEnabled(); }));
            }
        }

        beginTest ("Library selection is non-destructive and Enter explicitly loads");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            int loadCount = 0;
            auto realLoad = callbacks.loadPreset;
            callbacks.loadPreset = [&] (const juce::String& id) { ++loadCount; realLoad (id); };
            YoiBoiEditor editor (synth, synth.parameters(), callbacks);
            expect (clickControl (editor, "LIBRARY"));
            auto* library = findControl<juce::ListBox> (editor, "Presets. Select a row, then double-click or press Enter to load.");
            auto* favorite = findControl<juce::Button> (editor, "FAVORITE");
            expect (library != nullptr && favorite != nullptr);
            if (library != nullptr)
            {
                expect (library->getListBoxModel()->getNumRows() >= 12);
                library->selectRow (1);
                expectEquals (loadCount, 0);
                library->getListBoxModel()->returnKeyPressed (1);
                expectEquals (loadCount, 1);
                expect (callbacks.canUndo());
            }
            if (favorite != nullptr)
                expect (favorite->isVisible() && favorite->isEnabled(), "Favorite must remain available on the Library screen.");
            auto* search = findControl<juce::TextEditor> (editor, "Search sounds");
            expect (search != nullptr);
            if (search != nullptr && library != nullptr)
            {
                search->setText ("no-matching-yoi-948127", false);
                search->onTextChange();
                pumpEditor();
                expectEquals (library->getListBoxModel()->getNumRows(), 0);
                expectEquals (loadCount, 1);
                search->setText ({}, false);
                search->onTextChange();
                pumpEditor();
                expect (library->getListBoxModel()->getNumRows() >= 12);
            }
        }

        beginTest ("Random preset loads another preset through the same path as LOAD, with Undo");
        {
            TemporaryPresetFolder folder;
            YoiBoiProcessor synth (folder.path);
            auto callbacks = synth.makeEditorCallbacks();
            juce::StringArray loaded;
            auto realLoad = callbacks.loadPreset;
            callbacks.loadPreset = [&] (const juce::String& id) { loaded.add (id); realLoad (id); };
            YoiBoiEditor editor (synth, synth.parameters(), callbacks);
            auto* dice = findControl<juce::Button> (editor, "Random preset");
            auto* favorite = findControl<juce::Button> (editor, "FAVORITE");
            auto* newYoi = findControl<juce::Button> (editor, "NEW YOI");
            expect (dice != nullptr && favorite != nullptr && newYoi != nullptr);
            if (dice != nullptr && favorite != nullptr && newYoi != nullptr)
            {
                expect (dice->isVisible() && dice->isEnabled());
                expect (dice->getTooltip().containsIgnoreCase ("preset") && dice->getTooltip() != newYoi->getTooltip());
                expect (dice->getWantsKeyboardFocus() && ! dice->getMouseClickGrabsKeyboardFocus());
                expect (! dice->getBounds().intersects (favorite->getBounds()), "The die sits beside the star");
                const auto firstName = callbacks.currentPatchName();
                std::set<juce::String> seen;
                for (int pick = 0; pick < 40; ++pick)
                {
                    const auto before = currentPresetId (callbacks);
                    expect (clickControl (editor, "Random preset"));
                    expectEquals (loaded.size(), pick + 1, "Each click loads one preset through loadPreset");
                    if (loaded.size() != pick + 1) break;
                    expect (loaded[pick] != before, "Random preset never picks the preset already loaded");
                    expectEquals (currentPresetId (callbacks), loaded[pick]);
                    const auto items = callbacks.listPresets();
                    if (const auto* item = findPreset (items, loaded[pick])) expectEquals (callbacks.currentPatchName(), item->name);
                    else expect (false, "Loaded an id the Library does not list");
                    seen.insert (loaded[pick]);
                }
                expect (seen.size() > 15, "Picks spread across the whole Library");
                expect (callbacks.canUndo());
                if (loaded.size() >= 2)
                {
                    callbacks.undo();
                    expectEquals (currentPresetId (callbacks), loaded[loaded.size() - 2], "Undo returns to the previous preset");
                }
                for (int i = 0; i < 40; ++i) callbacks.undo();
                expectEquals (callbacks.currentPatchName(), firstName);

                // Beside the sound's name on PLAY and SHAPE, at the Library's list, and away on MOD with the star.
                for (const auto* tab : { "SHAPE", "MOD", "LIBRARY", "PLAY" })
                {
                    expect (clickControl (editor, tab));
                    expect (dice->isVisible() == (juce::String (tab) != "MOD"), juce::String ("Random preset on ") + tab);
                    expect (dice->isVisible() == favorite->isVisible(), juce::String ("Die and star together on ") + tab);
                }
                // A pick made in the Library selects its row, as choosing it by hand does.
                expect (clickControl (editor, "LIBRARY"));
                expect (clickControl (editor, "Random preset"));
                auto* load = findControl<juce::Button> (editor, "LOAD");
                expect (load != nullptr && waitForEditor ([&] { return load->isEnabled(); }));
            }
        }

        beginTest ("Random preset draws only from the Library's filter, folder and search");
        {
            TemporaryPresetFolder folder;
            YoiBoiProcessor synth (folder.path);
            auto callbacks = synth.makeEditorCallbacks();
            juce::StringArray loaded;
            auto realLoad = callbacks.loadPreset;
            callbacks.loadPreset = [&] (const juce::String& id) { loaded.add (id); realLoad (id); };
            expect (callbacks.savePreset ("Alpha Yoi"));
            synth.parameters().getParameter ("mouth")->setValueNotifyingHost (0.2f);
            expect (callbacks.savePreset ("Beta Yoi")); // now the loaded sound
            YoiBoiEditor editor (synth, synth.parameters(), callbacks);
            auto* dice = findControl<juce::Button> (editor, "Random preset");
            auto* search = findControl<juce::TextEditor> (editor, "Search sounds");
            auto* folders = findControl<juce::ListBox> (editor, "Preset folders");
            expect (dice != nullptr && search != nullptr && folders != nullptr);
            const auto nameOf = [&] (const juce::String& id)
            {
                const auto items = callbacks.listPresets();
                const auto* item = findPreset (items, id);
                return item != nullptr ? item->name : juce::String();
            };
            if (dice != nullptr && search != nullptr && folders != nullptr)
            {
                // SAVED holds two sounds, one of them loaded: picks alternate and never leave SAVED.
                expect (clickControl (editor, "LIBRARY"));
                expect (clickControl (editor, "SAVED"));
                for (int pick = 0; pick < 6; ++pick)
                {
                    expect (clickControl (editor, "Random preset"));
                    expectEquals (nameOf (loaded[pick]), juce::String (pick % 2 == 0 ? "Alpha Yoi" : "Beta Yoi"));
                }

                // Search narrows the view further. With only the loaded sound left, nothing loads.
                const auto setSearch = [&] (const juce::String& text)
                {
                    search->setText (text, false);
                    search->onTextChange();
                    pumpEditor();
                };
                setSearch ("Alpha");
                expect (clickControl (editor, "Random preset"));
                expectEquals (nameOf (loaded[loaded.size() - 1]), juce::String ("Alpha Yoi"));
                const auto count = loaded.size();
                expect (clickControl (editor, "Random preset"));
                expectEquals (loaded.size(), count, "The only listed sound is already loaded");
                expectEquals (callbacks.currentPatchName(), juce::String ("Alpha Yoi"));
                setSearch ("no-matching-yoi-948127");
                expect (waitForEditor ([&] { return ! dice->isEnabled(); }), "An empty view disables the die");
                setSearch ({});
                expect (waitForEditor ([&] { return dice->isEnabled(); }));

                // A factory folder: every pick comes from it.
                juce::StringArray folderKeys;
                for (const auto& item : callbacks.listPresets())
                    if (item.folder.isNotEmpty() && item.folder != "Saved") folderKeys.addIfNotAlreadyThere (item.folder);
                expect (folderKeys.size() > 2 && folders->getListBoxModel() != nullptr);
                if (folderKeys.size() > 2 && folders->getListBoxModel() != nullptr)
                {
                    folders->getListBoxModel()->returnKeyPressed (2);
                    pumpEditor();
                    for (int pick = 0; pick < 12; ++pick)
                    {
                        const auto before = currentPresetId (callbacks);
                        expect (clickControl (editor, "Random preset"));
                        const auto items = callbacks.listPresets();
                        const auto* item = findPreset (items, loaded[loaded.size() - 1]);
                        expect (item != nullptr && item->folder == folderKeys[2] && item->id != before,
                                "Picks stay in the " + folderKeys[2] + " folder and never repeat");
                    }
                }

                // FAVORITES: empty at first, then only the three marked sounds.
                expect (clickControl (editor, "FAVORITES"));
                expect (waitForEditor ([&] { return ! dice->isEnabled(); }), "No favorites, no pick");
                for (const auto* id : { "factory:14", "factory:30", "factory:41" })
                {
                    realLoad (id);
                    expect (clickControl (editor, "FAVORITE"));
                }
                expect (waitForEditor ([&] { return dice->isEnabled(); }));
                for (int pick = 0; pick < 9; ++pick)
                {
                    const auto before = currentPresetId (callbacks);
                    expect (clickControl (editor, "Random preset"));
                    const auto id = loaded[loaded.size() - 1];
                    expect ((id == "factory:14" || id == "factory:30" || id == "factory:41") && id != before,
                            "Picks are favorites other than the loaded one");
                }
            }
        }

        beginTest ("Sub routing, body low-cut and musical feel are real undoable controls");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            std::unique_ptr<juce::AudioProcessorEditor> editor (synth.createEditor());
            auto* routing = findControl<juce::ComboBox> (*editor, "Sub routing");
            auto* feel = findControl<juce::ComboBox> (*editor, "Length rhythm");
            auto* length = findControl<juce::Slider> (*editor, "LENGTH");
            expect (routing != nullptr && feel != nullptr && length != nullptr);
            if (routing != nullptr && feel != nullptr && length != nullptr)
            {
                for (int route = 0; route < 3; ++route)
                {
                    routing->setSelectedId (route + 1, juce::sendNotificationSync);
                    expectEquals (synth.parameters().getRawParameterValue ("sub_mode")->load(), static_cast<float> (route));
                }
                expect (callbacks.canUndo());
                callbacks.undo();
                expectEquals (synth.parameters().getRawParameterValue ("sub_mode")->load(), 1.0f);
                const auto previousCut = synth.parameters().getRawParameterValue ("low_cut")->load();
                expect (clickControl (*editor, "BODY LOW-CUT"));
                expectEquals (synth.parameters().getRawParameterValue ("low_cut")->load(), 1.0f - previousCut);

                feel->setSelectedId (2, juce::sendNotificationSync);
                expectEquals (synth.parameters().getRawParameterValue ("length_feel")->load(), 1.0f);
                expect (length->getTextFromValue (length->getValue()).endsWith (" T"));
                feel->setSelectedId (3, juce::sendNotificationSync);
                expectEquals (synth.parameters().getRawParameterValue ("length_feel")->load(), 2.0f);
                expect (length->getTextFromValue (length->getValue()).endsWith (" D"));
                auto* syncParameter = synth.parameters().getParameter ("sync");
                GestureCounter syncGestures;
                syncParameter->addListener (&syncGestures);
                expect (clickControl (*editor, "SYNC"));
                expectEquals (synth.parameters().getRawParameterValue ("sync")->load(), 0.0f);
                expectEquals (syncGestures.begins, 1);
                expectEquals (syncGestures.ends, 1);
                syncParameter->removeListener (&syncGestures);
                expect (waitForEditor ([&] { return ! feel->isEnabled(); }));
                expect (length->getTextFromValue (length->getValue()).endsWith (" ms"));
                callbacks.undo();
                expectEquals (synth.parameters().getRawParameterValue ("sync")->load(), 1.0f);
                expect (waitForEditor ([&] { return feel->isEnabled(); }));
                callbacks.redo();
                expectEquals (synth.parameters().getRawParameterValue ("sync")->load(), 0.0f);
                expect (waitForEditor ([&] { return ! feel->isEnabled(); }));
                expect (clickControl (*editor, "SYNC"));
                expect (waitForEditor ([&] { return feel->isEnabled(); }));
                expectEquals (feel->getSelectedId(), 3, "Free timing must preserve the selected dotted rhythm.");
                expect (length->getTextFromValue (length->getValue()).endsWith (" D"));
            }
        }

        beginTest ("Every exposed parameter exposes its randomization exclusion and keeps manual editing");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            std::unique_ptr<juce::AudioProcessorEditor> editor (synth.createEditor());
            for (const auto* title : { "MOUTH", "SIZE", "BITE", "DIRT", "LENGTH", "SUB", "Glide", "Output",
                                      "SYNC", "ONE SHOT", "LOOP", "Sub routing", "BODY LOW-CUT", "Length rhythm" })
            {
                auto* control = findControl<juce::Component> (*editor, title);
                expect (control != nullptr, juce::String (title) + " is missing");
                if (control == nullptr) continue;
                const auto id = control->getProperties()["parameterId"].toString();
                expect (id.isNotEmpty(), juce::String (title) + " must route right-click to its parameter");
                callbacks.setParameterLock (id, true);
                expect (waitForEditor ([&] { return static_cast<bool> (control->getProperties()["randomExcluded"]); }));
                expect (control->isEnabled(), "Exclusion must not disable manual edits");
                callbacks.setParameterLock (id, false);
                expect (waitForEditor ([&] { return ! static_cast<bool> (control->getProperties()["randomExcluded"]); }));
            }
            expect (clickControl (*editor, "SHAPE"));
            for (const auto* title : { "Body timing", "Curve" })
            {
                auto* control = findControl<juce::Slider> (*editor, title);
                expect (control != nullptr);
                if (control != nullptr) expect (control->getProperties()["parameterId"].toString().isNotEmpty());
            }
            auto* point = findControl<juce::ComboBox> (*editor, "Selected gesture point");
            auto* pose = findControl<juce::ComboBox> (*editor, "Vowel pose");
            expect (point != nullptr && pose != nullptr);
            if (point != nullptr && pose != nullptr)
            {
                // Engine 4 edits the nine-vowel parameters; the original engines the five poses.
                callbacks.setParameterLock (callbacks.soundEngine() >= 4 ? "vowel_start" : "start_pose", true);
                point->setSelectedId (1, juce::sendNotificationSync);
                expect (waitForEditor ([&] { return static_cast<bool> (pose->getProperties()["randomExcluded"]); }));
                point->setSelectedId (2, juce::sendNotificationSync);
                expect (waitForEditor ([&] { return ! static_cast<bool> (pose->getProperties()["randomExcluded"]); }));
            }
        }

        beginTest ("Generating variations does not populate the preset Library");
        {
            YoiBoiProcessor synth;
            std::unique_ptr<juce::AudioProcessorEditor> editor (synth.createEditor());
            expect (clickControl (*editor, "LIBRARY"));
            auto* library = findControl<juce::ListBox> (*editor, "Presets. Select a row, then double-click or press Enter to load.");
            expect (library != nullptr);
            if (library != nullptr)
            {
                const auto count = library->getListBoxModel()->getNumRows();
                for (int i = 0; i < 4; ++i)
                {
                    expect (clickControl (*editor, "NEW YOI"));
                    expect (clickControl (*editor, "MUTATE"));
                }
                expectEquals (library->getListBoxModel()->getNumRows(), count);
                expect (findControl<juce::Button> (*editor, "RECENT") == nullptr);
                expect (findControl<juce::Button> (*editor, "SAVED") != nullptr);
                expect (findControl<juce::Button> (*editor, "SAVE PRESET") != nullptr);
            }
        }

        beginTest ("In-device close calls the supplied window action");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            int closes = 0;
            callbacks.requestClose = [&] { ++closes; };
            callbacks.canClose = [] { return true; };
            YoiBoiEditor editor (synth, synth.parameters(), callbacks);
            expect (clickControl (editor, "Close plug-in window"));
            expectEquals (closes, 1);
            expect (findControl<juce::Button> (editor, "Interface zoom and settings") != nullptr);
        }

        beginTest ("External MIDI disables Preview and resizing retains usable controls");
        {
            YoiBoiProcessor synth;
            synth.prepareToPlay (48000.0, 64);
            juce::AudioBuffer<float> audio (2, 64);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
            synth.processBlock (audio, midi);
            std::unique_ptr<juce::AudioProcessorEditor> editor (synth.createEditor());
            auto* preview = findControl<juce::Button> (*editor, "PREVIEW");
            expect (preview != nullptr);
            if (preview != nullptr) expect (! preview->isEnabled());
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOff (1, 36), 0);
            synth.processBlock (audio, midi);
            expect (! synth.makeEditorCallbacks().externalNotesHeld());
            if (preview != nullptr)
                expect (waitForEditor ([&] { return preview->isEnabled(); }),
                        "The editor must re-enable Preview when external notes have been released.");
            const auto initialWidth = editor->getWidth(), initialHeight = editor->getHeight();
            for (const auto scale : { 1.0, 1.25, 1.5 })
            {
                editor->setSize (juce::roundToInt (initialWidth * scale), juce::roundToInt (initialHeight * scale));
                for (const auto* title : { "MOUTH", "SIZE", "BITE", "DIRT", "LENGTH", "SUB" })
                {
                    auto* slider = findControl<juce::Slider> (*editor, title);
                    expect (slider != nullptr);
                    if (slider == nullptr) continue;
                    const auto bounds = editor->getLocalArea (slider, slider->getLocalBounds());
                    expect (editor->getLocalBounds().contains (bounds), juce::String (title) + " is clipped after resizing");
                    expect (slider->getWantsKeyboardFocus());
                    expect (slider->getTitle().isNotEmpty());
                }
                for (const auto* title : { "Sub routing", "BODY LOW-CUT", "Length rhythm", "Interface zoom and settings", "Close plug-in window",
                                           "Random preset" })
                {
                    auto* control = findControl<juce::Component> (*editor, title);
                    expect (control != nullptr);
                    if (control != nullptr)
                        expect (editor->getLocalBounds().contains (editor->getLocalArea (control, control->getLocalBounds())),
                                juce::String (title) + " is clipped after resizing");
                }
            }
        }
    }
};

static EditorInteractionTests editorInteractionTests;
} // namespace yoiboi
