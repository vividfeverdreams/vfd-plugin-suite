#include "Plugin/YoiBoiProcessor.h"
#include "Tests/HostKeysTest.h" // ../shared: the suite's host-keys checks

#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <typeinfo>

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

// A click as a list row receives it, for calling a ListBoxModel directly.
juce::MouseEvent fakeClick (juce::Component& target)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), {}, {}, juce::MouseInputSource::defaultPressure,
             juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
             juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, &target, &target, now, {}, now, 1, false };
}

// ---- Host keys through a real window: the path a host's key presses and clicks take (VfdHostKeys.h).
void pumpMessages (int milliseconds) { juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds); }

// A click as the user makes it, at a point in target's own coordinates.
bool clickInWindow (juce::Component& target, juce::Point<int> local)
{
    auto* peer = target.getPeer();
    if (peer == nullptr || ! target.isShowing()) return false;
    const auto at = peer->getComponent().getLocalPoint (&target, local).toFloat();
    const auto t = juce::Time::currentTimeMillis();
    peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, at,
                            juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), juce::MouseInputSource::defaultPressure,
                            juce::MouseInputSource::defaultOrientation, t);
    peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, at, juce::ModifierKeys(),
                            juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, t + 30);
    pumpMessages (30);
    return true;
}

bool clickInWindow (juce::Component& target) { return clickInWindow (target, target.getLocalBounds().getCentre()); }

void hoverInWindow (juce::Component& target, juce::Point<int> local)
{
    if (auto* peer = target.getPeer())
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse,
                                peer->getComponent().getLocalPoint (&target, local).toFloat(), juce::ModifierKeys(),
                                juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                juce::Time::currentTimeMillis());
    pumpMessages (20);
}

// True when the plug-in kept the key, false when it went on to the host. A real key carries its character.
bool keptByPlugin (juce::Component& inWindow, int key, juce::ModifierKeys mods = {})
{
    auto* peer = inWindow.getPeer();
    if (peer == nullptr) return false;
    const auto text = key == juce::KeyPress::spaceKey ? juce::juce_wchar (' ')
                      : (key >= 'a' && key <= 'z' && ! mods.isCommandDown()) ? juce::juce_wchar (key) : juce::juce_wchar (0);
    return peer->handleKeyPress (juce::KeyPress (key, mods, text));
}

// The editor in an off-screen window, held the way a host's wrapper component holds it.
struct OffscreenWindow
{
    explicit OffscreenWindow (juce::Component& editorToShow) : editor (editorToShow)
    {
        holder.setSize (editor.getWidth(), editor.getHeight());
        holder.addAndMakeVisible (editor);
        holder.setTopLeftPosition (-6000, -6000);
        holder.addToDesktop (juce::ComponentPeer::windowIsTemporary);
        holder.setVisible (true);
        pumpMessages (600);
    }
    ~OffscreenWindow()
    {
        holder.removeFromDesktop();
        holder.removeChildComponent (&editor);
    }
    bool isOpen() const { return holder.getPeer() != nullptr && editor.isShowing(); }
    juce::Component& editor;
    juce::Component holder;
};

// The Library's view in list order, filtered as the editor filters it: a folder ("" = ALL SOUNDS) and a search.
juce::StringArray libraryView (EditorCallbacks& callbacks, const juce::String& folder, const juce::String& query)
{
    juce::StringArray ids;
    for (const auto& p : callbacks.listPresets())
    {
        if (folder.isNotEmpty() && p.folder != folder) continue;
        if (query.isNotEmpty() && ! (p.name + " " + p.family + " " + p.folder + " " + p.tags.joinIntoString (" ")).containsIgnoreCase (query))
            continue;
        ids.add (p.id);
    }
    return ids;
}

// Controls other than text fields and allowed pages that could take keyboard focus.
juce::StringArray keyboardGrabbers (juce::Component& root)
{
    juce::StringArray grabbers;
    for (auto* child : root.getChildren())
    {
        const bool allowed = vfd::HostKeys::isTextField (child)
                             || static_cast<bool> (child->getProperties().getWithDefault ("vfdHostKeysAllowFocus", false));
        if (! allowed && child->getWantsKeyboardFocus())
            grabbers.add ("'" + child->getTitle() + child->getName() + "' (" + juce::String (typeid (*child).name()) + ")");
        grabbers.addArray (keyboardGrabbers (*child));
    }
    return grabbers;
}
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
                expect(page->keyPressed(juce::KeyPress(juce::KeyPress::downKey)), "INFO holds the arrows so presets can't change under it");
                expect(!page->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)), "Cmd-Z reaches the host while INFO is open");
                expect(!page->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey, {}, ' ')), "Space reaches the host while INFO is open");
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

        beginTest ("Macro edits reach the engine as one host gesture and can be undone");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            std::unique_ptr<juce::AudioProcessorEditor> editor (synth.createEditor());
            auto* mouth = findControl<juce::Slider> (*editor, "MOUTH");
            expect (mouth != nullptr);
            if (mouth != nullptr)
            {
                // Knobs leave every key to the host; the arrows browse presets instead (host keys).
                expect (! mouth->getWantsKeyboardFocus() && ! mouth->getMouseClickGrabsKeyboardFocus());
                const auto original = synth.parameters().getRawParameterValue ("mouth")->load();
                auto* parameter = synth.parameters().getParameter ("mouth");
                GestureCounter gestures;
                parameter->addListener (&gestures);
                expectWithinAbsoluteError (mouth->getValueFromText ("62%"), 0.62, 0.00001);
                {
                    // The path a mouse drag takes: drag start, a new value, drag end.
                    const juce::Slider::ScopedDragNotification drag (*mouth);
                    const auto range = mouth->getMaximum() - mouth->getMinimum();
                    const auto middle = mouth->getMinimum() + range * 0.5;
                    mouth->setValue (mouth->getValue() + (mouth->getValue() > middle ? -0.2 : 0.2) * range, juce::sendNotificationSync);
                }
                expect (std::abs (synth.parameters().getRawParameterValue ("mouth")->load() - original) > 0.05f);
                expectEquals (gestures.begins, 1, "Host automation must receive a gesture start.");
                expectEquals (gestures.ends, 1, "Host automation must receive a matching gesture end.");
                parameter->removeListener (&gestures);
                expect (callbacks.canUndo(), "A knob edit must create an Undo entry.");
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

        beginTest ("Library selection is non-destructive and LOAD explicitly loads");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            int loadCount = 0;
            auto realLoad = callbacks.loadPreset;
            callbacks.loadPreset = [&] (const juce::String& id) { ++loadCount; realLoad (id); };
            YoiBoiEditor editor (synth, synth.parameters(), callbacks);
            expect (clickControl (editor, "LIBRARY"));
            auto* library = findControl<juce::ListBox> (editor, "Presets. Select a row, then double-click it or press LOAD.");
            auto* favorite = findControl<juce::Button> (editor, "FAVORITE");
            auto* load = findControl<juce::Button> (editor, "LOAD");
            expect (library != nullptr && favorite != nullptr && load != nullptr);
            if (library != nullptr && load != nullptr)
            {
                expect (! library->getWantsKeyboardFocus(), "The list never takes the keyboard (Return and the arrows are the host's)");
                expect (library->getListBoxModel()->getNumRows() >= 12);
                library->selectRow (1);
                expectEquals (loadCount, 0);
                expect (waitForEditor ([&] { return load->isEnabled(); }));
                expect (clickControl (editor, "LOAD"));
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
                expect (! dice->getWantsKeyboardFocus() && ! dice->getMouseClickGrabsKeyboardFocus(), "The die leaves the keyboard to the host");
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
                    folders->getListBoxModel()->listBoxItemClicked (2, fakeClick (*folders));
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
            auto* library = findControl<juce::ListBox> (*editor, "Presets. Select a row, then double-click it or press LOAD.");
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

        beginTest ("In-device close calls the supplied window action and is hidden where it can't close");
        {
            YoiBoiProcessor synth;
            auto callbacks = synth.makeEditorCallbacks();
            int closes = 0;
            bool closable = true;
            callbacks.requestClose = [&] { ++closes; };
            callbacks.canClose = [&] { return closable; };
            YoiBoiEditor editor (synth, synth.parameters(), callbacks);
            auto* close = findControl<juce::Button> (editor, "Close plug-in window");
            expect (close != nullptr && close->isVisible());
            expect (clickControl (editor, "Close plug-in window"));
            expectEquals (closes, 1);
            expect (findControl<juce::Button> (editor, "Interface zoom and settings") != nullptr);
            if (close != nullptr)
            {
                closable = false;
                expect (waitForEditor ([&] { return ! close->isVisible(); }), "No dead close control where the window can't be closed");
                closable = true;
                expect (waitForEditor ([&] { return close->isVisible(); }));
            }
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
                    expect (! slider->getWantsKeyboardFocus(), "Knobs leave the keyboard to the host");
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

// Host keys (../shared/VfdHostKeys.h) through a real off-screen window: YoiBoiTests --host-keys.
class HostKeysTests final : public juce::UnitTest
{
public:
    HostKeysTests() : UnitTest ("YOI-BOI host keys", "YOI-BOI host keys") {}

    void runTest() override
    {
        juce::MessageManager::getInstance();
        // The arrows browse presets while the window is focused; space, Return, Tab, letters, Escape and every
        // shortcut reach the host. Needs a display; skips without one.
        beginTest ("Host keys: the arrows browse presets and every other key reaches the host");
        {
            TemporaryPresetFolder folder;
            YoiBoiProcessor synth (folder.path);
            auto callbacks = synth.makeEditorCallbacks();
            juce::StringArray loaded;
            auto realLoad = callbacks.loadPreset;
            callbacks.loadPreset = [&] (const juce::String& id) { loaded.add (id); realLoad (id); };
            expect (currentPresetId (callbacks).isNotEmpty(), "A new instance starts on a Library preset");
            YoiBoiEditor editor (synth, synth.parameters(), callbacks);
            auto* device = editor.getNumChildComponents() > 0 ? editor.getChildComponent (0) : nullptr;
            auto* mouth = findControl<juce::Slider> (editor, "MOUTH");
            auto* size = findControl<juce::Slider> (editor, "SIZE");
            auto* preview = findControl<juce::Button> (editor, "PREVIEW");
            auto* mutate = findControl<juce::Button> (editor, "MUTATE");
            auto* libraryTab = findControl<juce::Button> (editor, "LIBRARY");
            auto* search = findControl<juce::TextEditor> (editor, "Search sounds");
            auto* canvas = findControl<juce::Component> (editor, "Mouth motion: Start, Body, End");
            auto* list = findControl<juce::ListBox> (editor, "Presets. Select a row, then double-click it or press LOAD.");
            auto* folders = findControl<juce::ListBox> (editor, "Preset folders");
            auto* die = findControl<juce::Button> (editor, "Random preset");
            const bool found = device != nullptr && mouth != nullptr && size != nullptr && preview != nullptr && mutate != nullptr
                               && libraryTab != nullptr && search != nullptr && canvas != nullptr && list != nullptr
                               && folders != nullptr && die != nullptr;
            expect (found, "The editor has the controls this test uses");
            expect (keyboardGrabbers (editor).isEmpty(), "No control can take the keyboard: " + keyboardGrabbers (editor).joinIntoString (", "));
            const juce::Point<int> blankInDevice { 40, 300 }; // plain shell, left of the screen, on every page
            if (found)
            {
                // The suite's shared run. Its control spots are two knob caps and the PREVIEW key; MUTATE is
                // clicked below, because it replaces the preset with a new sound, which the shared run's
                // "the left arrow goes back" checks can't follow.
                vfd::test::HostKeysProbe probe;
                probe.currentPreset = [&] { return callbacks.currentPatchName(); }; // the name PLAY shows
                probe.blankSpot = editor.getLocalPoint (device, blankInDevice);
                probe.openTextField = [&]() -> juce::TextEditor*
                {
                    clickInWindow (*libraryTab);
                    clickInWindow (*search);
                    return search;
                };
                probe.controlSpots = { editor.getLocalPoint (mouth, juce::Point<int> (mouth->getWidth() / 2, 60)),
                                       editor.getLocalPoint (size, juce::Point<int> (size->getWidth() / 2, 60)),
                                       editor.getLocalPoint (preview, preview->getLocalBounds().getCentre()) };
                expectEquals (vfd::test::runHostKeys (editor, probe, std::cout), 0, "The shared host-keys checks pass");
            }
            const bool display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr;
            if (found && display)
            {
                // YOI-BOI's own cases, through a window the same way.
                OffscreenWindow window (editor);
                expect (window.isOpen(), "The editor window opens");
                if (window.isOpen())
                {
                    const auto focused = [] { return juce::Component::getCurrentlyFocusedComponent(); };
                    const auto focusName = [&]
                    {
                        auto* c = focused();
                        return c == nullptr ? juce::String ("nothing") : "'" + c->getTitle() + c->getName() + "' " + juce::String (typeid (*c).name());
                    };
                    const auto reachesHost = [&] (int key, juce::ModifierKeys mods = {}) { return ! keptByPlugin (editor, key, mods); };
                    const auto tab = [&] (const juce::String& name)
                    {
                        auto* button = findControl<juce::Button> (editor, name);
                        return button != nullptr && clickInWindow (*button);
                    };
                    const auto onPage = [&] (const juce::String& name)
                    {
                        auto* button = findControl<juce::Button> (editor, name);
                        return button != nullptr && button->getToggleState();
                    };
                    const auto arrow = [&] (int key)
                    {
                        keptByPlugin (editor, key);
                        pumpMessages (40);
                        return currentPresetId (callbacks);
                    };
                    const auto blank = [&] { clickInWindow (*device, blankInDevice); };

                    // The vowel canvas (YB-F1 d, YB-F2): a mouse target with a tooltip and a pointer; keys never click it.
                    expect (tab ("PLAY") && onPage ("PLAY"));
                    blank();
                    expect (focused() == &editor, "A click in the device focuses the editor (focus on " + focusName() + ")");
                    auto* tip = dynamic_cast<juce::TooltipClient*> (canvas);
                    expect (! canvas->getWantsKeyboardFocus(), "The vowel canvas never takes the keyboard");
                    expect (tip != nullptr && tip->getTooltip().containsIgnoreCase ("click"), "On PLAY the canvas tooltip says to click it");
                    expect (canvas->getMouseCursor() == juce::MouseCursor::PointingHandCursor, "A pointing hand over the motion on PLAY");
                    expect (reachesHost (juce::KeyPress::spaceKey) && reachesHost (juce::KeyPress::returnKey) && onPage ("PLAY"),
                            "Space and Return reach the host and don't open SHAPE");
                    clickInWindow (*canvas);
                    expect (onPage ("SHAPE"), "Clicking the motion opens SHAPE");
                    expect (focused() == &editor, "Clicking the canvas focuses only the editor (focus on " + focusName() + ")");
                    expect (tip != nullptr && tip->getTooltip().containsIgnoreCase ("drag"), "On SHAPE the canvas tooltip explains dragging");
                    hoverInWindow (*canvas, { 3, 3 }); // a corner, away from the three points
                    expect (canvas->getMouseCursor() == juce::MouseCursor::CrosshairCursor, "A crosshair away from the points on SHAPE");
                    expect (reachesHost (juce::KeyPress::spaceKey) && onPage ("SHAPE"), "Space reaches the host on SHAPE");

                    // The search field (YB-F1 a): it keeps the keys while you type; Return hands them back.
                    expect (tab ("LIBRARY") && onPage ("LIBRARY"));
                    clickInWindow (*search);
                    expect (focused() == search, "Clicking the search field lets you type (focus on " + focusName() + ")");
                    keptByPlugin (editor, 'a', juce::ModifierKeys::commandModifier); // replace what the shared run typed
                    keptByPlugin (editor, juce::KeyPress::backspaceKey);
                    for (const int key : { 'y', 'o', 'i' })
                        expect (keptByPlugin (editor, key), "Letters type into the search field");
                    pumpMessages (40);
                    expectEquals (search->getText(), juce::String ("yoi"));
                    keptByPlugin (editor, juce::KeyPress::returnKey);
                    pumpMessages (60); // the field reports Return asynchronously
                    expect (focused() == &editor, "Return ends typing and leaves only the editor focused (focus on " + focusName() + ")");
                    expect (reachesHost (juce::KeyPress::spaceKey) && search->getText() == "yoi", "Space reaches the host after Return");

                    // The arrows step through the searched view in list order, wrap, and each is one Undo step.
                    const auto searched = libraryView (callbacks, {}, "yoi");
                    expect (searched.size() >= 3, "The search leaves a few sounds");
                    if (searched.size() >= 3)
                    {
                        const auto last = searched[searched.size() - 1];
                        realLoad (last);
                        const auto loads = loaded.size();
                        expectEquals (arrow (juce::KeyPress::rightKey), searched[0], "The right arrow wraps from the last sound to the first");
                        expectEquals (arrow (juce::KeyPress::downKey), searched[1], "The down arrow loads the next sound in list order");
                        expectEquals (arrow (juce::KeyPress::upKey), searched[0], "The up arrow loads the previous sound");
                        expectEquals (arrow (juce::KeyPress::leftKey), last, "The left arrow wraps from the first sound to the last");
                        expectEquals (loaded.size(), loads + 4, "Each arrow loads one preset through LOAD's path");
                        const auto items = callbacks.listPresets();
                        const auto* shown = findPreset (items, last);
                        expect (shown != nullptr && callbacks.currentPatchName() == shown->name, "PLAY shows the loaded preset's name");
                        callbacks.undo();
                        expectEquals (currentPresetId (callbacks), searched[0], "Undo goes back one arrow step");
                        callbacks.redo();
                        expectEquals (currentPresetId (callbacks), last);
                        expect (reachesHost (juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier)
                                    && reachesHost (juce::KeyPress::rightKey, juce::ModifierKeys::altModifier)
                                    && reachesHost (juce::KeyPress::downKey, juce::ModifierKeys::ctrlModifier)
                                    && currentPresetId (callbacks) == last,
                                "Arrows with a modifier go to the host");
                    }

                    // Clearing the search by typing, then clicking elsewhere, releases the field.
                    clickInWindow (*search, { 8, search->getHeight() / 2 });
                    expect (focused() == search);
                    keptByPlugin (editor, 'a', juce::ModifierKeys::commandModifier);
                    keptByPlugin (editor, juce::KeyPress::backspaceKey);
                    pumpMessages (40);
                    expect (search->getText().isEmpty(), "Cmd-A and Backspace clear the field while typing");
                    blank();
                    expect (focused() == &editor, "Clicking elsewhere releases the field (focus on " + focusName() + ")");

                    // Preset and folder rows (YB-F1 b): a click selects, the list never takes the keyboard.
                    const auto before = currentPresetId (callbacks);
                    const auto loads = loaded.size();
                    list->scrollToEnsureRowIsOnscreen (1);
                    clickInWindow (*list, list->getRowPosition (1, true).getCentre());
                    expectEquals (list->getSelectedRow(), 1, "Clicking a preset row selects it");
                    expect (focused() == &editor, "Clicking a preset row leaves only the editor focused (focus on " + focusName() + ")");
                    for (const int key : { juce::KeyPress::returnKey, juce::KeyPress::deleteKey, juce::KeyPress::backspaceKey,
                                           juce::KeyPress::pageDownKey, juce::KeyPress::homeKey, juce::KeyPress::endKey })
                        expect (reachesHost (key), "Keys a focused list used to swallow reach the host: " + juce::KeyPress (key).getTextDescription());
                    expectEquals (loaded.size(), loads, "Selecting a row loads nothing");
                    expectEquals (currentPresetId (callbacks), before);

                    juce::StringArray folderKeys;
                    for (const auto& item : callbacks.listPresets())
                        if (item.folder.isNotEmpty() && item.folder != "Saved") folderKeys.addIfNotAlreadyThere (item.folder);
                    // A folder (list row = folder) that doesn't hold the loaded sound.
                    int folderRow = -1;
                    juce::StringArray inFolder;
                    for (int row = 0; row < folderKeys.size() && folderRow < 0; ++row)
                        if (const auto view = libraryView (callbacks, folderKeys[row], {}); view.size() >= 3 && ! view.contains (currentPresetId (callbacks)))
                        {
                            folderRow = row;
                            inFolder = view;
                        }
                    expect (folderRow >= 0, "A folder without the loaded sound");
                    if (folderRow >= 0)
                    {
                        folders->scrollToEnsureRowIsOnscreen (folderRow);
                        clickInWindow (*folders, folders->getRowPosition (folderRow, true).getCentre());
                    }
                    expect (focused() == &editor, "Clicking a folder leaves only the editor focused (focus on " + focusName() + ")");
                    expect (reachesHost (juce::KeyPress::returnKey) && reachesHost (juce::KeyPress::spaceKey));
                    if (inFolder.size() >= 3)
                    {
                        expectEquals (arrow (juce::KeyPress::leftKey), inFolder[inFolder.size() - 1],
                                      "With the sound outside the view, the left arrow loads the view's last sound");
                        expectEquals (arrow (juce::KeyPress::rightKey), inFolder[0], "The right arrow wraps to the folder's first sound");
                    }

                    // Switching pages while typing (YB-F1 c) leaves the editor, not a control, with the keyboard.
                    clickInWindow (*search);
                    expect (focused() == search);
                    expect (tab ("PLAY") && onPage ("PLAY"));
                    expect (focused() == &editor, "Clicking a tab while typing focuses only the editor (focus on " + focusName() + ")");
                    expect (tab ("LIBRARY"));
                    clickInWindow (*search);
                    if (auto* play = findControl<juce::Button> (editor, "PLAY"); play != nullptr && play->onClick)
                        play->onClick(); // a page change that isn't a click: the hidden field lets go
                    expect (onPage ("PLAY") && focused() == &editor, "A page change hides the field and focuses only the editor (focus on " + focusName() + ")");
                    expect (reachesHost (juce::KeyPress::spaceKey) && reachesHost (juce::KeyPress::returnKey));

                    // MUTATE: the click leaves no focus behind, and the new sound is outside every view.
                    if (inFolder.size() >= 3)
                    {
                        clickInWindow (*mutate);
                        expect (focused() == &editor, "Clicking MUTATE leaves only the editor focused (focus on " + focusName() + ")");
                        expect (reachesHost (juce::KeyPress::returnKey) && reachesHost (juce::KeyPress::spaceKey),
                                "Return and space reach the host after MUTATE");
                        expect (currentPresetId (callbacks).isEmpty(), "MUTATE made a new sound");
                        expectEquals (arrow (juce::KeyPress::rightKey), inFolder[0], "After MUTATE the right arrow loads the view's first sound");
                        pumpMessages (450); // not a double-click
                        clickInWindow (*mutate);
                        expectEquals (arrow (juce::KeyPress::leftKey), inFolder[inFolder.size() - 1], "After MUTATE the left arrow loads the last");

                        // MOD hides the die, and the arrows still browse.
                        expect (tab ("MOD") && onPage ("MOD") && ! die->isVisible());
                        expectEquals (arrow (juce::KeyPress::leftKey), inFolder[inFolder.size() - 2], "On MOD the arrows still browse");
                        expect (reachesHost (juce::KeyPress::escapeKey), "Escape reaches the host when nothing is open");
                        expect (reachesHost (juce::KeyPress::tabKey) && focused() == nullptr, "Tab reaches the host");
                    }

                    // Original and latest sounds enable and disable controls; none of them may take the keyboard after.
                    realLoad ("factory:0");
                    pumpMessages (120);
                    if (inFolder.size() > 0) realLoad (inFolder[0]);
                    pumpMessages (120);
                    expect (keyboardGrabbers (editor).isEmpty(), "Still no control can take the keyboard: " + keyboardGrabbers (editor).joinIntoString (", "));
                }
            }
            else if (found)
                logMessage ("YOI-BOI host keys: skipped (no display)");
        }
    }
};

static HostKeysTests hostKeysTests;
} // namespace yoiboi
