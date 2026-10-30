#include "ThrowUpProcessor.h"
#include "ThrowUpEditor.h"
#include "Presets.h"
#include "PresetAutomationTests.h"
#include "Tests/HostKeysTest.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <set>
#include <cmath>
#include <vector>
#include <array>
#include <functional>
#include <cstring>
#include <AudioToolbox/AudioToolbox.h>
#include <atomic>
#include <cstdlib>
#include <new>
#include <thread>

// Counts heap allocations made while `countAllocations` is set, so the audio
// path (processBlock, including MIDI throws and note rooms) can be proven allocation-free.
namespace {
std::atomic<bool> countAllocations { false };
std::atomic<int> audioAllocations { 0 };
}
void* operator new(std::size_t size)
{
    if (countAllocations.load(std::memory_order_relaxed)) audioAllocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}
void set(ThrowUpProcessor& p, const char* id, float value)
{
    auto* v = p.apvts.getParameter(id);
    v->setValueNotifyingHost(v->convertTo0to1(value));
}
float get(ThrowUpProcessor& p, const char* id) { return p.apvts.getRawParameterValue(id)->load(); }
float signal(int i, int channel)
{
    const auto t = static_cast<double>(i) / 48000.0;
    return static_cast<float>(.19 * std::sin(2 * juce::MathConstants<double>::pi * 163.5 * t)
        + .12 * std::sin(2 * juce::MathConstants<double>::pi * (491 + channel * 7) * t));
}
void saveImage(juce::Component& editor, const juce::File& file, bool checkCorners = true)
{
    auto snapshot = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
    check(snapshot.isValid(), "editor renders at native resolution");
    check(!checkCorners || (snapshot.getPixelAt(0, 0).getAlpha() == 255
        && snapshot.getPixelAt(snapshot.getWidth() - 1, 0).getAlpha() == 255
        && snapshot.getPixelAt(0, snapshot.getHeight() - 1).getAlpha() == 255
        && snapshot.getPixelAt(snapshot.getWidth() - 1, snapshot.getHeight() - 1).getAlpha() == 255),
        "hardware enclosure fills the window edge to edge");
    juce::FileOutputStream stream(file);
    check(stream.openedOk(), "snapshot file opens");
    stream.setPosition(0); stream.truncate();
    check(juce::PNGImageFormat().writeImageToStream(snapshot, stream), "editor snapshot saved");
}
juce::Component* findNamed(juce::Component& parent, const juce::String& name)
{
    for (auto* child : parent.getChildren()) {
        if (child->getName() == name) return child;
        if (auto* found = findNamed(*child, name)) return found;
    }
    return nullptr;
}
juce::MouseEvent mouseEvent(juce::Component& target, int clicks)
{
    const auto now = juce::Time::getCurrentTime();
    const auto point = target.getLocalBounds().getCentre().toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys::leftButtonModifier, 1.0f, 0, 0, 0, 0, &target, &target,
        now, point, now, clicks, false);
}

// No message loop runs in this console host: drive JUCE timers by hand.
bool pumpTimersUntil(const std::function<bool()>& done, int milliseconds)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(milliseconds);
    while (juce::Time::getMillisecondCounter() < end) {
        juce::Thread::sleep(20);
        juce::Timer::callPendingTimersSynchronously();
        if (done()) return true;
    }
    return done();
}

int runHostKeysTests()
{
    ThrowUpProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* presets = dynamic_cast<juce::ComboBox*>(findNamed(*editor, "Factory presets"));
    auto* output = dynamic_cast<juce::Label*>(findNamed(*editor, "OUTPUT value"));
    check(presets != nullptr && output != nullptr, "host-keys probe finds the real preset display and Output field");
    vfd::test::HostKeysProbe probe;
    probe.currentPreset = [presets] { return presets->getText(); };
    probe.blankSpot = { 20, editor->getHeight() / 2 };
    probe.openTextField = [output] {
        // The Output box's real single-click path, including its double-click delay.
        juce::Component& box = *output;
        box.mouseDown(mouseEvent(box, 1));
        box.mouseUp(mouseEvent(box, 1));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(juce::MouseEvent::getDoubleClickTimeout() + 100);
        return output->getCurrentTextEditor();
    };
    for (const auto* name : { "THROW", "SIZE", "FREEZE" }) {
        auto* control = findNamed(*editor, name);
        check(control != nullptr, "host-keys probe finds a safe control to click");
        probe.controlSpots.push_back(editor->getLocalPoint(control, control->getLocalBounds().getCentre()));
    }
    const int failures = vfd::test::runHostKeys(*editor, probe, std::cout);
    if (failures != 0 || juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return failures;

    juce::Component holder;
    holder.setSize(editor->getWidth(), editor->getHeight());
    holder.addAndMakeVisible(*editor);
    holder.setTopLeftPosition(-6000, -6000);
    holder.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    holder.setVisible(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    auto* peer = holder.getPeer();
    check(peer != nullptr, "extended host-keys window opens");
    const auto click = [&](juce::Component& target) {
        const auto at = holder.getLocalPoint(&target, target.getLocalBounds().getCentre()).toFloat();
        const auto time = juce::Time::currentTimeMillis();
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, at,
            juce::ModifierKeys::leftButtonModifier, juce::MouseInputSource::defaultPressure,
            juce::MouseInputSource::defaultOrientation, time);
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, at, {},
            juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, time + 30);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    };
    probe.openTextField();
    click(*findNamed(*editor, "SIZE"));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
    check(!output->isBeingEdited() && juce::Component::getCurrentlyFocusedComponent() == editor.get(),
          "clicking a knob releases the Output text field");
    // Space the next click beyond JUCE's double-click window, then click away
    // before Output's deferred single-click edit opens.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(juce::MouseEvent::getDoubleClickTimeout() + 30);
    click(*output);
    click(*findNamed(*editor, "SIZE"));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(juce::MouseEvent::getDoubleClickTimeout() + 100);
    check(!output->isBeingEdited() && juce::Component::getCurrentlyFocusedComponent() == editor.get(),
          "clicking away during Output's edit delay prevents a late focus grab");
    throwup::tests::PresetHostObserver host(processor);
    for (const auto& [code, direction] : std::array<std::pair<int, int>, 4> {{
            { juce::KeyPress::leftKey, -1 }, { juce::KeyPress::upKey, -1 },
            { juce::KeyPress::rightKey, 1 }, { juce::KeyPress::downKey, 1 } }}) {
        processor.loadFactoryPreset(direction < 0 ? 0 : throwup::legacyPresetStart() - 1);
        juce::MemoryBlock before, keyboard, physical;
        processor.getStateInformation(before);
        editor->grabKeyboardFocus();
        host.clear();
        check(peer->handleKeyPress(juce::KeyPress(code)), "a plain arrow is kept for preset browsing");
        processor.getStateInformation(keyboard);
        const auto begins = host.begins, ends = host.ends, changes = host.changes;
        const int metadata = host.metadata;
        processor.setStateInformation(before.getData(), (int) before.getSize());
        auto* button = dynamic_cast<juce::Button*>(findNamed(*editor, direction < 0 ? "Previous preset" : "Next preset"));
        host.clear();
        button->onClick();
        processor.getStateInformation(physical);
        check(keyboard == physical && host.begins == begins && host.ends == ends && host.changes == changes
              && host.metadata == metadata && metadata == 1 && host.program == 0 && host.parameterInfo == 0,
              "keyboard and on-screen arrows have identical state, wrap-around and host gestures");
    }
    editor->grabKeyboardFocus();
    const int before = processor.getCurrentProgram();
    for (const auto modifier : { juce::ModifierKeys::shiftModifier, juce::ModifierKeys::commandModifier,
                                 juce::ModifierKeys::altModifier, juce::ModifierKeys::ctrlModifier })
        for (const int code : { juce::KeyPress::leftKey, juce::KeyPress::upKey, juce::KeyPress::rightKey, juce::KeyPress::downKey })
            check(!peer->handleKeyPress(juce::KeyPress(code, juce::ModifierKeys(modifier), 0))
                      && processor.getCurrentProgram() == before,
                  "modified arrow reaches the host without loading a preset");
    check(!peer->handleKeyPress(juce::KeyPress(juce::KeyPress::escapeKey)), "Escape reaches the host with nothing to cancel");
    presets->showPopup();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
    juce::PopupMenu::dismissAllActiveMenus();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    check(!presets->hasKeyboardFocus(true) && !peer->handleKeyPress(juce::KeyPress(juce::KeyPress::returnKey))
              && !peer->handleKeyPress(juce::KeyPress(juce::KeyPress::spaceKey, {}, ' ')),
          "closing the real preset popup leaves Return and space with the host");

    // Hiding an ancestor of the editor must release a held momentary key too.
    auto* key = findNamed(*editor, "THROW!");
    key->mouseDown(mouseEvent(*key, 1));
    holder.setVisible(false);
    check(juce::exactlyEqual(get(processor, "throwKey"), 0.0f), "hiding the host window releases a held THROW!");
    holder.removeFromDesktop();
    holder.removeChildComponent(editor.get());
    return 0;
}

void runHandsOffRegressionTests()
{
    {
        ThrowUpProcessor p;
        throwup::tests::PresetHostObserver host(p);
        const int index = p.apvts.getParameter("throwKey")->getParameterIndex();
        auto editor = std::make_unique<ThrowUpEditor>(p);
        editor->setVisible(true);
        auto* key = findNamed(*editor, "THROW!");
        check(key != nullptr, "momentary THROW! key exists");
        key->mouseDown(mouseEvent(*key, 1));
        check(juce::exactlyEqual(get(p, "throwKey"), 1.0f), "mouse-down holds THROW!");
        editor->setVisible(false);
        check(juce::exactlyEqual(get(p, "throwKey"), 0.0f) && host.begins[(size_t) index] == 1 && host.ends[(size_t) index] == 1,
              "hiding a held THROW! releases it with a balanced gesture");
        key->mouseUp(mouseEvent(*key, 1));
        check(host.ends[(size_t) index] == 1, "late mouse-up after hiding does not end the gesture twice");
        editor->setVisible(true);
        key->mouseDown(mouseEvent(*key, 1));
        editor.reset();
        check(juce::exactlyEqual(get(p, "throwKey"), 0.0f) && host.begins[(size_t) index] == 2 && host.ends[(size_t) index] == 2,
              "closing a held THROW! releases it with a balanced gesture");
        juce::MemoryBlock state;
        p.getStateInformation(state);
        ThrowUpProcessor recalled;
        recalled.setStateInformation(state.getData(), (int) state.getSize());
        check(juce::exactlyEqual(get(recalled, "throwKey"), 0.0f), "a project saved after closing the editor does not retain a held THROW!");
    }
}

void runConcurrentSaveTests()
{
    {
        ThrowUpProcessor p;
        const auto file = p.saveUserPreset("Save stress", "Concurrent Room");
        check(file.existsAsFile(), "concurrent-save fixture is isolated from real user presets");
        p.selectSlot(1);
        p.setEditorScale(1.25f);
        juce::MemoryBlock original;
        p.getStateInformation(original);
        std::atomic<int> saves { 0 }, invalid { 0 };
        std::atomic<bool> start { false };
        const auto save = [&] {
            while (!start.load()) std::this_thread::yield();
            for (int n = 0; n < 1000; ++n) {
                juce::MemoryBlock data;
                p.getStateInformation(data);
                auto xml = juce::AudioProcessor::getXmlFromBinary(data.getData(), (int) data.getSize());
                bool ok = xml != nullptr && xml->hasTagName("THROW_UP_STATE")
                          && xml->getNumChildElements() == ThrowUpProcessor::parameterCount
                          && xml->getIntAttribute("schemaVersion") == 2
                          && xml->getIntAttribute("activeSlot") >= 0 && xml->getIntAttribute("activeSlot") <= 1;
                if (xml != nullptr) {
                    for (auto* child : xml->getChildIterator())
                        ok = ok && child->hasTagName("PARAM") && std::isfinite(child->getDoubleAttribute("value"));
                    for (const auto* name : { "slotA", "slotB" })
                        if (xml->hasAttribute(name)) {
                            auto slot = juce::parseXML(xml->getStringAttribute(name));
                            ok = ok && slot != nullptr && slot->hasTagName("SLOT");
                        }
                }
                if (!ok) ++invalid;
                ++saves;
            }
        };
        std::thread first(save), second(save);
        start.store(true);
        for (int n = 0; n < 300; ++n) {
            p.loadFactoryPreset(n % p.getNumPrograms());
            p.loadUserPreset(file);
            p.selectSlot(n % 2);
            p.setEditorScale(n % 2 ? .9f : 1.25f);
            if (n % 10 == 0) p.setStateInformation(original.getData(), (int) original.getSize());
        }
        first.join(); second.join();
        check(saves == 2000 && invalid == 0, "2000 concurrent project saves parse while preset names, A/B, size and state change");
        p.setStateInformation(original.getData(), (int) original.getSize());
        juce::MemoryBlock roundTrip;
        p.getStateInformation(roundTrip);
        check(original == roundTrip, "metadata synchronization keeps the project state byte-identical on round trip");
        file.getParentDirectory().deleteRecursively();
    }
}

void runFixRegressionTests(const juce::File* folder)
{
    {
        // Meters keep the highest block peak until the editor collects it.
        ThrowUpProcessor p;
        set(p, "mix", 0); set(p, "output", 0);
        p.prepareToPlay(48000, 256);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> audio(2, 256);
        for (const float peak : { .9f, .1f, .2f }) {
            audio.clear(); audio.setSample(0, 17, peak); audio.setSample(1, 99, -peak * .5f);
            p.processBlock(audio, midi);
        }
        check(juce::exactlyEqual(p.inputLevel.exchange(0.0f), .9f) && juce::exactlyEqual(p.outputLevel.exchange(0.0f), .9f),
              "meters hold the highest peak across blocks until collected");
        check(juce::exactlyEqual(p.inputLevel.load(), 0.0f) && juce::exactlyEqual(p.outputLevel.load(), 0.0f),
              "collecting a meter peak resets it");
    }
    {
        ThrowUpProcessor p;
        ThrowUpEditor editor(p);
        auto* presetMenu = dynamic_cast<juce::ComboBox*>(findNamed(editor, "Factory presets"));
        auto* outputValue = dynamic_cast<juce::Label*>(findNamed(editor, "OUTPUT value"));
        check(presetMenu != nullptr && outputValue != nullptr, "preset menu and Output value box exist");

        // Choosing the ticked room again from the menu reloads it, reverting edits.
        p.loadFactoryPreset(23);
        set(p, "size", 12); set(p, "grit", 77); set(p, "mix", 55);
        std::function<void()> action;
        for (juce::PopupMenu::MenuItemIterator item(*presetMenu->getRootMenu(), true); item.next();)
            if (item.getItem().itemID == 24) action = item.getItem().action;
        check(action != nullptr, "preset menu items carry a load action");
        action();
        const auto& room = throwup::factoryPresets()[23].room;
        check(p.getCurrentProgram() == 23 && std::abs(get(p, "size") - room.size) < .05f
              && std::abs(get(p, "grit") - room.grit) < .05f && std::abs(get(p, "mix") - 55) < .02f,
              "re-selecting the current preset reverts its edits and keeps THROW");

        // Output: double-click resets to 0 dB, a single click opens text entry.
        juce::Component& outputBox = *outputValue; // mouse handlers are public on Component
        set(p, "output", -6.5f);
        outputBox.mouseDown(mouseEvent(outputBox, 1));
        outputBox.mouseUp(mouseEvent(outputBox, 1));
        outputBox.mouseDown(mouseEvent(outputBox, 2));
        outputBox.mouseUp(mouseEvent(outputBox, 2));
        outputBox.mouseDoubleClick(mouseEvent(outputBox, 2));
        check(!outputValue->isBeingEdited(), "double-clicking Output does not open text entry");
        check(std::abs(get(p, "output")) < .001f, "double-clicking Output resets it to 0 dB");
        {
            // The reset value must give exact unity: THROW 0 passes the input bit-exact.
            set(p, "mix", 0);
            p.prepareToPlay(48000, 257);
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> audio(2, 257), original(2, 257);
            bool exact = true;
            for (int b = 0; b < 8; ++b) {
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 257; ++i) audio.setSample(c, i, signal(b * 257 + i, c));
                original.makeCopyOf(audio);
                p.processBlock(audio, midi);
                for (int c = 0; c < 2; ++c)
                    exact = exact && std::memcmp(audio.getReadPointer(c), original.getReadPointer(c), 257 * sizeof(float)) == 0;
            }
            check(exact, "after an Output reset, THROW 0 is bit-exact dry");
        }
        check(!pumpTimersUntil([outputValue] { return outputValue->isBeingEdited(); },
                               juce::MouseEvent::getDoubleClickTimeout() + 600),
              "a double-click on Output does not also open text entry");
        set(p, "output", -3.0f);
        outputBox.mouseDown(mouseEvent(outputBox, 1));
        outputBox.mouseUp(mouseEvent(outputBox, 1));
        check(pumpTimersUntil([outputValue] { return outputValue->isBeingEdited(); }, 3000),
              "a single click on Output opens text entry");
        outputValue->hideEditor(true);
        check(std::abs(get(p, "output") + 3.0f) < .02f, "cancelled Output text entry leaves the value unchanged");

        // The editor collects meter peaks (exchange) on its timer; +2.9 dBFS lights CLIP.
        set(p, "mix", 0); set(p, "output", 6);
        p.prepareToPlay(48000, 256);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> audio(2, 256);
        for (int block = 0; block < 4; ++block) {
            for (int i = 0; i < 256; ++i)
                for (int c = 0; c < 2; ++c)
                    audio.setSample(c, i, .7f * static_cast<float>(std::sin(.07 * (block * 256 + i))));
            p.processBlock(audio, midi);
        }
        check(p.outputLevel.load() > 1.0f, "hot output is held above 0 dBFS for the meter");
        check(pumpTimersUntil([&p] { return juce::exactlyEqual(p.outputLevel.load(), 0.0f) && juce::exactlyEqual(p.inputLevel.load(), 0.0f); }, 3000),
              "editor timer collects held meter peaks");
        if (folder != nullptr)
            saveImage(editor, folder->getChildFile("THROW-UP-meters.png"));
    }
    {
        // Session state from a newer version: unknown children are skipped and a
        // bad value falls back to its default instead of discarding the session.
        ThrowUpProcessor source;
        source.loadFactoryPreset(37);
        set(source, "mix", 41.5f); set(source, "decay", 9.25f); set(source, "output", -2.5f);
        juce::MemoryBlock saved;
        source.getStateInformation(saved);
        auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
        check(xml != nullptr, "saved state parses");
        xml->createNewChildElement("USER_PRESET")->setAttribute("name", "From the future");
        auto* future = xml->createNewChildElement("PARAM");
        future->setAttribute("id", "futureKnob"); future->setAttribute("value", 3.0);
        for (auto* child : xml->getChildIterator())
            if (child->getStringAttribute("id") == "decay") child->setAttribute("value", "nan");
        juce::MemoryBlock edited;
        juce::AudioProcessor::copyXmlToBinary(*xml, edited);
        ThrowUpProcessor restored;
        restored.setStateInformation(edited.getData(), static_cast<int>(edited.getSize()));
        check(restored.getCurrentProgram() == 37 && std::abs(get(restored, "mix") - 41.5f) < .02f
              && std::abs(get(restored, "output") + 2.5f) < .02f && std::abs(get(restored, "decay") - 2.4f) < .01f
              && std::abs(get(restored, "size") - throwup::factoryPresets()[37].room.size) < .05f,
              "state with unknown children and a bad value restores every other parameter");
        set(restored, "mix", 12.0f);
        xml->setTagName("NOT_THROW_UP");
        juce::MemoryBlock wrongRoot;
        juce::AudioProcessor::copyXmlToBinary(*xml, wrongRoot);
        restored.setStateInformation(wrongRoot.getData(), static_cast<int>(wrongRoot.getSize()));
        check(std::abs(get(restored, "mix") - 12.0f) < .02f, "state with a foreign root is still rejected");
    }
}

// ---------------------------------------------------------------------------
// 0.2.0 features
juce::File temporaryPresetRoot()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("throw-up-test-presets-" + juce::Uuid().toString());
}

bool enabledAt(juce::PopupMenu& menu, int id)
{
    for (juce::PopupMenu::MenuItemIterator item(menu, true); item.next();)
        if (item.getItem().itemID == id) return item.getItem().isEnabled;
    return false;
}

void processSine(ThrowUpProcessor& p, juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi, int& clock)
{
    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            audio.setSample(c, i, c < 2 ? .3f * static_cast<float>(std::sin(2 * juce::MathConstants<double>::pi * 330 * (clock + i) / 48000.0)) : 0.0f);
    clock += audio.getNumSamples();
    p.processBlock(audio, midi);
}

void runFeatureHostTests(const juce::File* folder)
{
    using throwup::SoundVersion;
    const int programs = static_cast<int>(throwup::factoryPresets().size());
    {
        // A project saved by 0.1.5: 13 parameters, schema 1, factory program 7.
        juce::XmlElement xml("THROW_UP_STATE");
        xml.setAttribute("factoryProgram", 7);
        xml.setAttribute("schemaVersion", 1);
        const std::array<std::pair<const char*, float>, 13> saved {{ { "mix", 55 }, { "size", 33 }, { "decay", 1.7f },
            { "predelay", 42 }, { "lowCut", 260 }, { "highCut", 5100 }, { "diffusion", 64 }, { "motion", 12 },
            { "width", 120 }, { "grit", 30 }, { "duck", 25 }, { "output", -2.5f }, { "bypass", 0 } }};
        for (const auto& [id, value] : saved) {
            auto* child = xml.createNewChildElement("PARAM");
            child->setAttribute("id", id);
            child->setAttribute("value", value);
        }
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary(xml, block);
        ThrowUpProcessor old;
        old.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        bool all = true;
        for (const auto& [id, value] : saved) all = all && std::abs(get(old, id) - value) < .01f;
        check(all, "a 0.1.5 project restores all 13 of its settings");
        check(old.getSoundVersion() == SoundVersion::v015, "a 0.1.5 project keeps the 0.1.5 sound");
        check(old.getCurrentProgram() == throwup::legacyPresetStart() + 7
              && old.getProgramName(old.getCurrentProgram()).contains("Mono Nook"),
              "a 0.1.5 project's factory room is recalled from the 0.1.5 bank by its old index");
        // JUCE's interval snapping can leave a restored 0 dB as ~3.6e-7 dB; the engine
        // treats that as exact unity (and 160 +/- 0.01 ms as the 0.1.5 duck release).
        const auto is = [&old](const char* id, float value) { return std::abs(get(old, id) - value) < 1.0e-4f; };
        check(is("trigger", 0) && is("shimmer", 0) && is("swell", 0) && is("freeze", 0) && is("dry", 1) && is("returnTrim", 0)
              && is("safeClip", 0) && is("predelaySync", 0) && is("duckRelease", 160) && is("duckSource", 0),
              "every 0.2.0 control starts neutral in a 0.1.5 project");
        juce::MemoryBlock resaved;
        old.getStateInformation(resaved);
        ThrowUpProcessor reopened;
        reopened.setStateInformation(resaved.getData(), static_cast<int>(resaved.getSize()));
        check(reopened.getSoundVersion() == SoundVersion::v015 && reopened.getCurrentProgram() == old.getCurrentProgram(),
              "re-saving a 0.1.5 project keeps its sound and room");
        // Projects from 0.1.0-0.1.3 carried no schema or program at all.
        juce::XmlElement bare("THROW_UP_STATE");
        auto* mix = bare.createNewChildElement("PARAM"); mix->setAttribute("id", "mix"); mix->setAttribute("value", 12.0);
        juce::MemoryBlock bareBlock;
        juce::AudioProcessor::copyXmlToBinary(bare, bareBlock);
        ThrowUpProcessor older;
        older.setStateInformation(bareBlock.getData(), static_cast<int>(bareBlock.getSize()));
        check(older.getSoundVersion() == SoundVersion::v015 && older.getCurrentProgram() == throwup::legacyPresetStart(),
              "older projects without a schema also keep the 0.1.5 sound");
        ThrowUpProcessor fresh;
        check(fresh.getSoundVersion() == SoundVersion::latest && fresh.getCurrentProgram() == 0, "new instances use the latest sound");
        // The upgrade is a normal, undoable host edit.
        throwup::tests::PresetHostObserver observer(old);
        old.setSoundVersion(SoundVersion::latest);
        const int versionIndex = ThrowUpProcessor::parameterCount - 1;
        check(old.getSoundVersion() == SoundVersion::latest && observer.begins[static_cast<size_t>(versionIndex)] == 1
              && observer.ends[static_cast<size_t>(versionIndex)] == 1, "Use latest sound is a single undoable host edit");
        old.setSoundVersion(SoundVersion::v015);
        check(old.getSoundVersion() == SoundVersion::v015, "the 0.1.5 sound can be restored");
    }
    {
        // User presets, in a scratch folder (never the real one).
        const auto root = temporaryPresetRoot();
        ThrowUpProcessor::setUserPresetRootForTesting(root);
        ThrowUpProcessor p;
        p.loadFactoryPreset(12);
        set(p, "size", 44.4f); set(p, "mix", 61);
        const auto first = p.saveUserPreset("My Throws", "Wide Thing");
        check(first.existsAsFile() && first.getParentDirectory() == root.getChildFile("My Throws"), "user preset saved in its chosen folder");
        const auto original = first.loadFileAsString();
        const auto originalTime = first.getLastModificationTime();
        set(p, "size", 70);
        const auto second = p.saveUserPreset("My Throws", "Wide Thing");
        check(second.existsAsFile() && second != first && second.getFileName() == "Wide Thing 2.throwup",
              "saving the same name creates a new file");
        check(first.loadFileAsString() == original && first.getLastModificationTime() == originalTime,
              "an existing user preset is never modified");
        set(p, "size", 10);
        check(p.loadUserPreset(first) && std::abs(get(p, "size") - 44.4f) < .05f && p.getUserPresetName() == "Wide Thing"
              && std::abs(get(p, "mix") - 61) < .02f, "user preset recalls its room and keeps THROW");
        juce::MemoryBlock state;
        p.getStateInformation(state);
        ThrowUpProcessor recalled;
        recalled.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        check(recalled.getUserPresetName() == "Wide Thing", "the project remembers the user preset name");
        ThrowUpEditor editor(p);
        auto* menu = dynamic_cast<juce::ComboBox*>(findNamed(editor, "Factory presets"));
        editor.rebuildPresetMenu();
        check(menu != nullptr && menu->getNumItems() == programs + 2, "preset menu lists factory rooms and both user presets");

        // Random preset: draws from the factory folders and every user preset the menu lists,
        // never the preset already loaded (a user preset by its name: both files above load as
        // "Wide Thing"), skips an unreadable file, and keeps the 0.1.5 rooms to themselves.
        auto* dice = dynamic_cast<juce::Button*>(findNamed(editor, "Random preset"));
        const auto loose = p.saveUserPreset("", "Loose Room");
        root.getChildFile("Broken.throwup").replaceWithText("not a preset");
        check(dice != nullptr && loose.getParentDirectory() == root, "random preset key and a loose user preset exist");
        auto* next = dynamic_cast<juce::Button*>(findNamed(editor, "Next preset"));
        auto* previous = dynamic_cast<juce::Button*>(findNamed(editor, "Previous preset"));
        check(next != nullptr && previous != nullptr, "both on-screen preset arrows exist");
        p.loadFactoryPreset(throwup::legacyPresetStart() - 1);
        next->onClick();
        // Menu order sorts by file name: "Wide Thing 2.throwup" (size 70) comes before "Wide Thing.throwup" (44.4).
        check(p.getUserPresetName() == "Wide Thing" && std::abs(get(p, "size") - 70) < .05f && menu->getText() == "Wide Thing",
              "next from the last factory room loads the first user file in menu order");
        next->onClick();
        check(p.getUserPresetName() == "Wide Thing" && std::abs(get(p, "size") - 44.4f) < .05f,
              "next visits a second user file even when both load under the same name");
        next->onClick();
        check(p.getUserPresetName() == "Loose Room" && menu->getText() == "Loose Room",
              "next skips an unreadable user file and reaches the loose user preset");
        next->onClick();
        check(p.getUserPresetName().isEmpty() && p.getCurrentProgram() == 0 && menu->getSelectedId() == 1,
              "next from the last user preset wraps to the first factory room");
        previous->onClick();
        check(p.getUserPresetName() == "Loose Room", "previous from the first factory room wraps to the last user preset");
        previous->onClick();
        check(p.getUserPresetName() == "Wide Thing" && std::abs(get(p, "size") - 44.4f) < .05f,
              "previous skips an unreadable file and reaches the second same-named user file");
        previous->onClick();
        check(p.getUserPresetName() == "Wide Thing" && std::abs(get(p, "size") - 70) < .05f,
              "previous visits the first same-named user file");
        previous->onClick();
        check(p.getUserPresetName().isEmpty() && p.getCurrentProgram() == throwup::legacyPresetStart() - 1,
              "previous from the first user file returns to the last factory room");
        editor.rebuildPresetMenu();
        menu->setSelectedId(1000, juce::sendNotificationSync);
        check(menu->getSelectedId() == p.getCurrentProgram() + 1,
              "an unsolicited ComboBox user ID cannot leave its display naming an unloaded room");
        p.loadUserPreset(first);
        std::set<std::string> userPicks;
        int factoryPicks = 0;
        bool repeated = false, legacyPicked = false, misnamed = false;
        for (int i = 0; i < 1500; ++i) {
            const auto beforeName = p.getUserPresetName();
            const int beforeProgram = p.getCurrentProgram();
            dice->onClick();
            const auto name = p.getUserPresetName();
            if (name.isNotEmpty()) {
                userPicks.insert(name.toStdString());
                repeated = repeated || name == beforeName;
                misnamed = misnamed || menu->getText() != name;
            } else {
                ++factoryPicks;
                repeated = repeated || (beforeName.isEmpty() && p.getCurrentProgram() == beforeProgram);
                legacyPicked = legacyPicked || p.getCurrentProgram() >= throwup::legacyPresetStart();
                misnamed = misnamed || menu->getSelectedId() != p.getCurrentProgram() + 1;
            }
        }
        check(!repeated, "random preset never re-picks the loaded preset, factory or user, and skips unreadable files");
        check(userPicks == std::set<std::string> { "Wide Thing", "Loose Room" } && factoryPicks > 1000 && !legacyPicked,
              "random preset draws from the factory folders and every user preset the menu lists, never a 0.1.5 room");
        check(!misnamed, "the preset display names every random pick, including one user preset after another");
        p.loadFactoryPreset(throwup::legacyPresetStart() + 3);
        bool stayed = true;
        for (int i = 0; i < 200; ++i) {
            const int before = p.getCurrentProgram();
            dice->onClick();
            stayed = stayed && p.getUserPresetName().isEmpty() && p.getCurrentProgram() >= throwup::legacyPresetStart()
                  && p.getCurrentProgram() != before;
        }
        check(stayed, "from a 0.1.5 room, random preset picks another 0.1.5 room");
        ThrowUpProcessor::setUserPresetRootForTesting({});
        root.deleteRecursively();
    }
    {
        // A/B compare.
        ThrowUpProcessor p;
        set(p, "size", 20); set(p, "mix", 33);
        p.selectSlot(1);
        check(std::abs(get(p, "size") - 20) < .05f, "slot B starts as a copy of A");
        set(p, "size", 80); set(p, "grit", 70);
        p.selectSlot(0);
        check(std::abs(get(p, "size") - 20) < .05f && std::abs(get(p, "mix") - 33) < .02f, "A recalls its room and keeps THROW");
        p.selectSlot(1);
        check(std::abs(get(p, "size") - 80) < .05f && std::abs(get(p, "grit") - 70) < .05f, "B recalls its room");
        juce::MemoryBlock state;
        p.getStateInformation(state);
        ThrowUpProcessor recalled;
        recalled.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        recalled.selectSlot(0);
        check(recalled.getActiveSlot() == 0 && std::abs(get(recalled, "size") - 20) < .05f, "A/B slots are saved with the project");
    }
    {
        // Size menu: sizes that do not fit the display are disabled; the size is saved.
        const juce::Rectangle<int> laptop { 0, 0, 1440, 875 }, desktop { 0, 0, 2560, 1415 };
        check(ThrowUpEditor::scaleFits(0.9f, laptop) && !ThrowUpEditor::scaleFits(1.0f, laptop)
              && ThrowUpEditor::scaleFits(1.5f, desktop), "size fit rule: width and height + 60 within the display");
        check(juce::exactlyEqual(ThrowUpEditor::initialScale(0.0f, desktop), 1.0f) && juce::exactlyEqual(ThrowUpEditor::initialScale(0.0f, laptop), 0.9f)
              && juce::exactlyEqual(ThrowUpEditor::initialScale(1.25f, desktop), 1.25f) && juce::exactlyEqual(ThrowUpEditor::initialScale(1.5f, laptop), 0.9f),
              "editor opens at the saved size, or the largest that fits");
        ThrowUpProcessor p;
        ThrowUpEditor editor(p);
        auto menu = editor.createSizeMenu(laptop);
        check(enabledAt(menu, 1) && !enabledAt(menu, 2) && !enabledAt(menu, 3) && !enabledAt(menu, 4),
              "on a small display only the sizes that fit are enabled");
        auto large = editor.createSizeMenu(desktop);
        check(enabledAt(large, 1) && enabledAt(large, 2) && enabledAt(large, 3) && enabledAt(large, 4), "all sizes fit a large display");
        editor.applyScale(1.25f);
        check(editor.getWidth() == 825 && std::abs(p.getEditorScale() - 1.25f) < .001f, "the chosen size applies and is stored");
        juce::MemoryBlock state;
        p.getStateInformation(state);
        ThrowUpProcessor recalled;
        recalled.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        check(std::abs(recalled.getEditorScale() - 1.25f) < .001f, "the editor size is saved with the project");
    }
    {
        // MIDI: note-on throws (velocity), note-off releases, a note can pick the room.
        ThrowUpProcessor p;
        set(p, "mix", 0); set(p, "trigger", 1); set(p, "throwAttack", 4); set(p, "throwRelease", 100);
        p.prepareToPlay(48000, 256);
        juce::AudioBuffer<float> audio(2, 256);
        juce::MidiBuffer midi, none;
        int clock = 0;
        for (int b = 0; b < 40; ++b) processSine(p, audio, none, clock);
        check(juce::exactlyEqual(p.getTriggerLevel(), 0.0f), "no throw without a note");
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 10);
        double added = 0;
        for (int b = 0; b < 8; ++b) {
            juce::AudioBuffer<float> dry(2, 256);
            processSine(p, audio, b == 0 ? midi : none, clock);
            for (int i = 0; i < 256; ++i)
                added += std::abs(audio.getSample(0, i) - .3f * static_cast<float>(std::sin(2 * juce::MathConstants<double>::pi * 330 * (clock - 256 + i) / 48000.0)));
        }
        check(p.getTriggerLevel() > 99 && added > 1, "a MIDI note-on throws the reverb in");
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        processSine(p, audio, midi, clock);
        for (int b = 0; b < 30; ++b) processSine(p, audio, none, clock);
        check(juce::exactlyEqual(p.getTriggerLevel(), 0.0f), "note-off releases the throw");
        set(p, "noteRooms", 1);
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, ThrowUpProcessor::firstRoomNote + 5, static_cast<juce::uint8>(90)), 0);
        processSine(p, audio, midi, clock);
        p.flushPendingRoomForTesting();
        check(p.getCurrentProgram() == 5 && std::abs(get(p, "size") - throwup::factoryPresets()[5].room.size) < .05f,
              "with Note picks room, the note selects its room");
        // The processor's own 30 Hz message-thread timer applies a picked room (no flush).
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, ThrowUpProcessor::firstRoomNote + 12, static_cast<juce::uint8>(90)), 3);
        processSine(p, audio, midi, clock);
        check(pumpTimersUntil([&p] { return p.getCurrentProgram() == 12; }, 3000)
              && std::abs(get(p, "size") - throwup::factoryPresets()[12].room.size) < .05f,
              "the processor's timer applies a room picked by a MIDI note");
        // The audio thread never allocates, including MIDI throws and note-picked rooms.
        juce::MidiBuffer notes;
        notes.ensureSize(4096);
        int allocations = 0;
        for (int b = 0; b < 200; ++b) {
            notes.clear();
            if (b % 2 == 0) notes.addEvent(juce::MidiMessage::noteOn(1, ThrowUpProcessor::firstRoomNote + (b / 2) % 90, static_cast<juce::uint8>(100)), b % 200);
            else notes.addEvent(juce::MidiMessage::noteOff(1, ThrowUpProcessor::firstRoomNote + (b / 2) % 90), 7);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                    audio.setSample(c, i, .3f * static_cast<float>(std::sin(2 * juce::MathConstants<double>::pi * 330 * (clock + i) / 48000.0)));
            clock += 256;
            audioAllocations.store(0);
            countAllocations.store(true);
            p.processBlock(audio, notes);
            countAllocations.store(false);
            allocations += audioAllocations.load();
        }
        std::cout << "processBlock with MIDI throws and note-picked rooms: " << allocations << " allocations in 200 blocks\n";
        check(allocations == 0, "the audio thread never allocates, including MIDI note rooms");
        p.flushPendingRoomForTesting();
    }
    {
        // Optional sidechain for Duck.
        ThrowUpProcessor p;
        auto layout = p.getBusesLayout();
        check(layout.inputBuses.size() == 2 && layout.getChannelSet(true, 1).isDisabled(), "a sidechain input exists and is off by default");
        layout.inputBuses.getReference(1) = juce::AudioChannelSet::mono();
        check(p.isBusesLayoutSupported(layout), "mono sidechain accepted");
        layout.inputBuses.getReference(1) = juce::AudioChannelSet::create5point1();
        check(!p.isBusesLayoutSupported(layout), "surround sidechain rejected");
        layout.inputBuses.getReference(1) = juce::AudioChannelSet::stereo();
        check(p.setBusesLayout(layout) && p.getTotalNumInputChannels() == 4, "stereo sidechain enabled");
        set(p, "mix", 100); set(p, "duck", 100); set(p, "duckSource", 1); set(p, "size", 60); set(p, "decay", 4);
        p.prepareToPlay(48000, 256);
        juce::AudioBuffer<float> audio(4, 256);
        juce::MidiBuffer none;
        double quiet = 0, keyed = 0;
        for (int b = 0; b < 300; ++b) {
            for (int i = 0; i < 256; ++i) {
                const int n = b * 256 + i;
                const float dry = n < 48000 / 4 ? .3f * static_cast<float>(std::sin(2 * juce::MathConstants<double>::pi * 440 * n / 48000.0)) : 0.0f;
                audio.setSample(0, i, dry); audio.setSample(1, i, dry);
                const float key = n >= 48000 ? .8f : 0.0f;
                audio.setSample(2, i, key); audio.setSample(3, i, key);
            }
            p.processBlock(audio, none);
            if (b >= 150 && b < 185) quiet += audio.getRMSLevel(0, 0, 256);   // ~0.8-0.99 s, no key
            if (b >= 200 && b < 235) keyed += audio.getRMSLevel(0, 0, 256);   // ~1.07-1.25 s, key on
        }
        check(keyed < quiet * 0.1, "the sidechain key ducks the tail (Duck source External)");
    }
    if (folder == nullptr) return;
    // Screenshots of the new editor states.
    ThrowUpProcessor::setUserPresetRootForTesting(temporaryPresetRoot());
    ThrowUpProcessor p;
    p.loadFactoryPreset(14);
    set(p, "mix", 40);
    ThrowUpEditor editor(p);
    editor.applyScale(1.0f);
    // No two visible controls overlap on any page (a bar or knob and its own value
    // label are one control).
    for (const auto page : { ThrowUpEditor::Page::room, ThrowUpEditor::Page::trigger, ThrowUpEditor::Page::space, ThrowUpEditor::Page::mix }) {
        editor.showPage(page);
        std::vector<juce::Component*> visible;
        for (auto* child : editor.getChildren())
            if (child->isVisible() && child->getName() != "VFD Info Page") visible.push_back(child);
        bool clear = true;
        for (size_t a = 0; a < visible.size(); ++a)
            for (size_t b = a + 1; b < visible.size(); ++b) {
                const auto nameA = visible[a]->getName(), nameB = visible[b]->getName();
                if (nameA + " value" == nameB || nameB + " value" == nameA) continue;
                if (visible[a]->getBounds().intersects(visible[b]->getBounds())) {
                    clear = false;
                    std::cout << "  overlap: " << nameA << " / " << nameB << '\n';
                }
            }
        check(clear, "no controls overlap on this display page");
    }
    editor.showPage(ThrowUpEditor::Page::room);
    saveImage(editor, folder->getChildFile("THROW-UP-room-page.png"));
    set(p, "trigger", 1); set(p, "noteRooms", 1); set(p, "releaseSync", 3);
    editor.showPage(ThrowUpEditor::Page::trigger);
    saveImage(editor, folder->getChildFile("THROW-UP-throw-page.png"));
    set(p, "predelaySync", 4); set(p, "predelayFeel", 1); set(p, "swell", 3); set(p, "shimmer", 40); set(p, "shimmerPitch", 0);
    editor.showPage(ThrowUpEditor::Page::space);
    juce::Timer::callPendingTimersSynchronously();
    saveImage(editor, folder->getChildFile("THROW-UP-space-page.png"));
    set(p, "returnTrim", -6); set(p, "duckSource", 1); set(p, "duckThreshold", -30);
    editor.showPage(ThrowUpEditor::Page::mix);
    saveImage(editor, folder->getChildFile("THROW-UP-mix-page.png"));
    p.setSoundVersion(SoundVersion::v015);
    pumpTimersUntil([] { return false; }, 120);
    saveImage(editor, folder->getChildFile("THROW-UP-0.1.5-sound.png"));
    p.setSoundVersion(SoundVersion::latest);
    // Keys lit: FREEZE, SAFE, SEND, and a held THROW! (LED and trigger arc).
    set(p, "freeze", 1); set(p, "safeClip", 1); set(p, "dry", 0); set(p, "throwKey", 1);
    p.prepareToPlay(48000, 256);
    {
        juce::AudioBuffer<float> audio(2, 256);
        juce::MidiBuffer none;
        int clock = 0;
        for (int b = 0; b < 40; ++b) processSine(p, audio, none, clock);
    }
    editor.showPage(ThrowUpEditor::Page::room);
    pumpTimersUntil([] { return false; }, 120);
    saveImage(editor, folder->getChildFile("THROW-UP-keys-lit.png"));
    set(p, "freeze", 0); set(p, "safeClip", 0); set(p, "dry", 1); set(p, "throwKey", 0);
    for (const float scale : { 0.9f, 1.25f, 1.5f }) {
        editor.applyScale(scale);
        check(editor.getWidth() == juce::roundToInt(660 * scale), "editor applies each menu size");
        saveImage(editor, folder->getChildFile("THROW-UP-" + juce::String(juce::roundToInt(scale * 100)) + ".png"));
    }
    editor.applyScale(1.0f);
    editor.openSaveDialog();
    if (auto* modal = juce::ModalComponentManager::getInstance()->getModalComponent(0)) {
        auto shot = modal->createComponentSnapshot(modal->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream stream(folder->getChildFile("THROW-UP-save-dialog.png"));
        stream.setPosition(0); stream.truncate();
        check(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(shot, stream), "save dialog snapshot saved");
        modal->exitModalState(0);
    } else {
        check(false, "save dialog opens");
    }
    pumpTimersUntil([] { return false; }, 50);
    ThrowUpProcessor::setUserPresetRootForTesting({});
}

// ---------------------------------------------------------------------------
// Compatibility: projects saved with the 0.1.5 payload play identically here.
juce::AudioPluginInstance* findParameterOwner(juce::AudioPluginInstance& plugin, const juce::String& name, float plain, float lo, float hi)
{
    for (auto* parameter : plugin.getParameters())
        if (parameter->getName(128) == name) { parameter->setValueNotifyingHost((plain - lo) / (hi - lo)); return &plugin; }
    throw std::runtime_error(("hosted parameter missing: " + name).toStdString());
}

std::vector<float> renderHosted(juce::AudioPluginInstance& plugin, double rate, bool mono, int length)
{
    plugin.prepareToPlay(rate, 1024);
    std::vector<float> left(static_cast<size_t>(length)), right(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i) {
        const double t = i / rate;
        left[static_cast<size_t>(i)] = i < length / 2 ? static_cast<float>(0.6 * std::sin(2 * juce::MathConstants<double>::pi * 110 * t + 3 * std::sin(2 * juce::MathConstants<double>::pi * 220 * t))) : 0.0f;
        right[static_cast<size_t>(i)] = i < length / 2 ? static_cast<float>(0.5 * std::cos(2 * juce::MathConstants<double>::pi * 83 * t)) : 0.0f;
    }
    const int sizes[] { 1, 7, 64, 511, 1024, 13, 257 };
    juce::MidiBuffer midi;
    for (int position = 0, k = 0; position < length; ) {
        const int count = std::min(sizes[k++ % 7], length - position);
        float* channels[] { left.data() + position, right.data() + position };
        juce::AudioBuffer<float> view(channels, mono ? 1 : 2, count);
        plugin.processBlock(view, midi);
        position += count;
    }
    plugin.releaseResources();
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

int runCompatibility(const juce::File& oldPlugin, const juce::File& newPlugin)
{
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> oldTypes, newTypes;
    format.findAllTypesForFile(oldTypes, oldPlugin.getFullPathName());
    format.findAllTypesForFile(newTypes, newPlugin.getFullPathName());
    check(oldTypes.size() == 1 && newTypes.size() == 1 && oldTypes[0]->version == "0.1.5", "0.1.5 payload and new VST3 both scan");
    check(oldTypes[0]->uniqueId == newTypes[0]->uniqueId, "the VST3 identity is unchanged");
    int identical = 0, cases = 0;
    double worst = 0;
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        const int length = static_cast<int>(rate * (rate > 96000 ? 0.6 : 1.5));
        for (int preset = 0; preset < 50; ++preset) {
            for (int mode = 0; mode < 3; ++mode) {
                const bool mono = preset % 7 == 3;
                juce::String error;
                auto old = format.createInstanceFromDescription(*oldTypes[0], rate, 1024, error);
                check(old != nullptr, "0.1.5 payload instantiates");
                if (mono) {
                    auto layout = old->getBusesLayout();
                    layout.inputBuses.getReference(0) = layout.outputBuses.getReference(0) = juce::AudioChannelSet::mono();
                    check(old->setBusesLayout(layout), "0.1.5 accepts mono");
                }
                old->setCurrentProgram(preset);
                findParameterOwner(*old, "THROW", mode == 0 ? 100.0f : mode == 1 ? 37.5f : 0.0f, 0, 100);
                findParameterOwner(*old, "Output", mode == 2 ? -4.0f : mode == 1 ? 3.0f : 0.0f, -18, 6);
                juce::MemoryBlock state;
                old->getStateInformation(state);
                const auto expected = renderHosted(*old, rate, mono, length);
                old.reset();
                auto updated = format.createInstanceFromDescription(*newTypes[0], rate, 1024, error);
                check(updated != nullptr, "new VST3 instantiates");
                if (mono) {
                    auto layout = updated->getBusesLayout();
                    layout.inputBuses.getReference(0) = layout.outputBuses.getReference(0) = juce::AudioChannelSet::mono();
                    check(updated->setBusesLayout(layout), "new build accepts mono");
                }
                updated->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
                const auto actual = renderHosted(*updated, rate, mono, length);
                ++cases;
                if (std::memcmp(expected.data(), actual.data(), expected.size() * sizeof(float)) == 0) ++identical;
                for (size_t i = 0; i < expected.size(); ++i)
                    worst = std::max(worst, static_cast<double>(std::abs(expected[i] - actual[i])));
            }
        }
    }
    std::cout << "0.1.5 payload -> 0.2.0 VST3: " << identical << "/" << cases << " renders bit-identical; maximum difference " << worst << '\n';
    check(identical == cases, "every 0.1.5 project renders bit-identically in this build");
    return 0;
}

// ---------------------------------------------------------------------------
// Audio Unit: register the built component for this process only and validate it.
juce::String fourCharacters(CFTypeRef value)
{
    if (value != nullptr && CFGetTypeID(value) == CFStringGetTypeID()) return juce::String::fromCFString(static_cast<CFStringRef>(value));
    return {};
}

OSType fourCC(const juce::String& text)
{
    jassert(text.length() == 4);
    return static_cast<OSType>((static_cast<juce::uint32>(text[0]) << 24) | (static_cast<juce::uint32>(text[1]) << 16)
                               | (static_cast<juce::uint32>(text[2]) << 8) | static_cast<juce::uint32>(text[3]));
}

int runAudioUnitValidation(const juce::File& component)
{
    auto url = juce::String(component.getFullPathName());
    CFURLRef bundleUrl = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(url.toRawUTF8()),
                                                                 static_cast<CFIndex>(url.getNumBytesAsUTF8()), true);
    CFBundleRef bundle = CFBundleCreate(nullptr, bundleUrl);
    CFRelease(bundleUrl);
    check(bundle != nullptr && CFBundleLoadExecutable(bundle), "AU bundle loads from the build folder");
    check(fourCharacters(CFBundleGetIdentifier(bundle)) == "com.yoiboi.throwup", "AU bundle identifier com.yoiboi.throwup");
    auto components = static_cast<CFArrayRef>(CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("AudioComponents")));
    check(components != nullptr && CFArrayGetCount(components) == 1, "one AudioComponent declared");
    auto entry = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(components, 0));
    const auto type = fourCharacters(CFDictionaryGetValue(entry, CFSTR("type")));
    const auto subtype = fourCharacters(CFDictionaryGetValue(entry, CFSTR("subtype")));
    const auto manufacturer = fourCharacters(CFDictionaryGetValue(entry, CFSTR("manufacturer")));
    const auto factoryName = fourCharacters(CFDictionaryGetValue(entry, CFSTR("factoryFunction")));
    std::cout << "AU identity: type " << type << ", subtype " << subtype << ", manufacturer " << manufacturer << '\n';
    check(subtype == "Thr1" && manufacturer == "Yoib" && (type == "aumf" || type == "aufx"), "AU identity: subtype Thr1, manufacturer Yoib");
    auto factory = reinterpret_cast<AudioComponentFactoryFunction>(
        CFBundleGetFunctionPointerForName(bundle, factoryName.toCFString()));
    check(factory != nullptr, "AU factory function found");
    AudioComponentDescription description { fourCC(type), fourCC(subtype), fourCC(manufacturer), 0, 0 };
    SInt32 version = 0;
    if (auto number = static_cast<CFNumberRef>(CFDictionaryGetValue(entry, CFSTR("version")))) CFNumberGetValue(number, kCFNumberSInt32Type, &version);
    check(version == JucePlugin_VersionCode, "AU version matches this build (" JucePlugin_VersionString ")");
    check(AudioComponentRegister(&description, CFSTR("Vivid Fever Dreams: THROW-UP (test)"), static_cast<UInt32>(version), factory) != nullptr,
          "AU registered for this process only");
    juce::AudioUnitPluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types, "AudioUnit:Effects/" + type + "," + subtype + "," + manufacturer);
    check(types.size() == 1, "AU scans as one plugin");
    for (const double rate : { 44100.0, 48000.0, 96000.0 }) {
        for (const int blockSize : { 64, 512, 1024 }) {
            juce::String error;
            auto au = format.createInstanceFromDescription(*types[0], rate, blockSize, error);
            check(au != nullptr, "AU instantiates");
            ThrowUpProcessor reference;
            check(au->getNumPrograms() == reference.getNumPrograms() && au->getParameters().size() >= ThrowUpProcessor::parameterCount,
                  "AU exposes every program and parameter");
            if (rate == 44100.0 && blockSize == 64)
                std::cout << "AU buses as hosted: inputs " << au->getBusCount(true) << " (" << au->getTotalNumInputChannels()
                          << " channels), outputs " << au->getBusCount(false) << " (" << au->getTotalNumOutputChannels() << " channels)\n";
            au->setCurrentProgram(33); reference.loadFactoryPreset(33);
            for (auto* parameter : au->getParameters())
                if (parameter->getName(128) == "THROW") parameter->setValueNotifyingHost(.7f);
            set(reference, "mix", 70);
            au->prepareToPlay(rate, blockSize); reference.prepareToPlay(rate, blockSize);
            check(au->getLatencySamples() == 0, "AU reports zero latency");
            juce::MidiBuffer midi;
            double worst = 0, energy = 0;
            bool finite = true;
            const int blocks = static_cast<int>(rate * 1.5) / blockSize;
            // The AU host buffer carries every input bus (the sidechain is an AU input
            // element); the sidechain stays silent here, as in the reference.
            const int auChannels = juce::jmax(au->getTotalNumInputChannels(), au->getTotalNumOutputChannels());
            for (int b = 0; b < blocks; ++b) {
                juce::AudioBuffer<float> a(auChannels, blockSize), r(2, blockSize);
                a.clear();
                for (int c = 0; c < 2; ++c) for (int i = 0; i < blockSize; ++i)
                    a.setSample(c, i, b * blockSize + i < rate * 0.5 ? signal(b * blockSize + i, c) : 0.0f);
                r.makeCopyOf(a, true);
                r.setSize(2, blockSize, true);
                au->processBlock(a, midi); reference.processBlock(r, midi);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < blockSize; ++i) {
                    finite = finite && std::isfinite(a.getSample(c, i));
                    worst = std::max(worst, static_cast<double>(std::abs(a.getSample(c, i) - r.getSample(c, i))));
                    if (b * blockSize > rate * 0.6) energy += std::abs(a.getSample(c, i));
                }
            }
            check(finite && worst < 3e-5 && energy > 1, "AU audio is finite, matches the tested processor and leaves a tail");
            juce::MemoryBlock state;
            au->getStateInformation(state);
            auto reopened = format.createInstanceFromDescription(*types[0], rate, blockSize, error);
            reopened->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            bool same = true;
            for (int i = 0; i < ThrowUpProcessor::parameterCount; ++i)
                if (std::abs(au->getParameters()[i]->getValue() - reopened->getParameters()[i]->getValue()) >= 1e-5f) {
                    same = false;
                    std::cout << "  AU parameter " << au->getParameters()[i]->getName(64) << ": " << au->getParameters()[i]->getValue()
                              << " vs " << reopened->getParameters()[i]->getValue() << '\n';
                }
            check(same, "AU state recalls every parameter into a new instance");
            if (reopened->getCurrentProgram() != 33)
                std::cout << "  AU current program after state recall: " << reopened->getCurrentProgram() << " (saved 33)\n";
            au->releaseResources();
        }
    }
    std::cout << "AU validated in-process at 44.1/48/96 kHz with 64/512/1024-sample blocks\n";
    return 0;
}

void checkInfoPage(ThrowUpEditor& editor, ThrowUpProcessor& processor, const juce::File& snapshot)
{
    auto* button = dynamic_cast<juce::Button*>(findNamed(editor, "INFO"));
    auto* overlay = findNamed(editor, "VFD Info Page");
    check(button != nullptr && overlay != nullptr, "INFO key and credits page exist");
    check(!overlay->isVisible(), "credits page starts closed");
    check(editor.getLocalBounds().contains(button->getBounds()), "INFO key stays within shell after resizing");
    for (auto* child : editor.getChildren())
        if (child != button && child != overlay && child->isVisible())
            check(!button->getBounds().intersects(child->getBounds()), "INFO key clears original control geometry");
    std::vector<float> parameters;
    for (auto* parameter : processor.getParameters()) parameters.push_back(parameter->getValue());
    button->onClick();
    check(overlay->isVisible() && overlay->getBounds() == editor.getLocalBounds(), "INFO opens full-editor credits page");
    check(!overlay->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)) && overlay->isVisible(),
          "credits page lets space reach the host while staying open");
    const auto countLinks = [](const auto& self, juce::Component& parent) -> int {
        int count = dynamic_cast<juce::HyperlinkButton*>(&parent) != nullptr ? 1 : 0;
        for (auto* child : parent.getChildren()) count += self(self, *child);
        return count;
    };
    check(countLinks(countLinks, *overlay) == 4, "credits page contains exactly four external links");
    for (const auto& [label, address] : std::array<std::pair<const char*, const char*>, 4> {{
             { "@cxy", "https://x.com/cxy" }, { "@clt", "https://x.com/clt" },
             { "vividfeverdreams.xyz", "https://www.vividfeverdreams.xyz" },
             { "Dream Sequence on Fractured Atlas", "https://fundraising.fracturedatlas.org/dream-sequence" } }}) {
        auto* link = dynamic_cast<juce::HyperlinkButton*>(findNamed(*overlay, label));
        check(link != nullptr && link->getURL().toString(false) == address, "credits link points to approved destination");
    }
    saveImage(editor, snapshot, false);
    check(overlay->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) && !overlay->isVisible(), "Escape closes credits");
    button->onClick();
    auto* close = dynamic_cast<juce::Button*>(findNamed(*overlay, "Close info"));
    check(close != nullptr, "credits page contains close control");
    close->onClick();
    check(!overlay->isVisible(), "close control dismisses credits");
    for (int index = 0; index < processor.getParameters().size(); ++index)
        check(processor.getParameters()[index]->getValue() == parameters[static_cast<size_t>(index)],
              "credits interaction preserves all host parameters");
}
void renderDemo(const juce::File& file)
{
    constexpr int rate = 48000, length = rate * 10, block = 128;
    ThrowUpProcessor p;
    p.loadFactoryPreset(23);
    p.prepareToPlay(rate, block);
    juce::AudioBuffer<float> output(2, length); output.clear();
    juce::AudioBuffer<float> audio(2, block);
    juce::MidiBuffer midi;
    // First phrase is dry; repeat it with identical synthesis and automated throws.
    for (int pos = 0; pos < length; pos += block) {
        const auto count = std::min(block, length - pos);
        audio.setSize(2, count, false, false, true);
        const double time = pos / double(rate);
        const double phrase = std::fmod(time, 5.0);
        const bool throwOn = time >= 5 && ((phrase >= .68 && phrase < 1.18)
            || (phrase >= 1.82 && phrase < 2.48) || (phrase >= 2.95 && phrase < 4.82));
        set(p, "mix", throwOn ? 68.0f : 0.0f);
        for (int i = 0; i < count; ++i) {
            const double t = std::fmod((pos + i) / double(rate), 5.0);
            const double beat = std::fmod(t, .6);
            const double env = beat < .37 && t < 3.55 ? std::min(1.0, beat / .008) * std::exp(-beat * 5) : 0;
            const double base = (static_cast<int>(t / .6) % 3 == 2 ? 146.83 : 164.81);
            for (int ch = 0; ch < 2; ++ch) {
                const double phase = 2 * juce::MathConstants<double>::pi * base * t;
                const double bass = std::tanh(2.2 * std::sin(phase + 1.4 * std::sin(phase * 2.003 + ch * .04)));
                audio.setSample(ch, i, static_cast<float>(.26 * env * bass));
            }
        }
        p.processBlock(audio, midi);
        for (int ch = 0; ch < 2; ++ch) output.copyFrom(ch, pos, audio, ch, 0, count);
    }
    auto stream = file.createOutputStream();
    check(stream != nullptr, "demo file opens");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(), rate, 2, 24, {}, 0));
    check(writer != nullptr && writer->writeFromAudioSampleBuffer(output, 0, length), "dry then automated throw audio demo saved");
}
}

int main(int argc, char** argv)
{
    try {
        juce::ScopedJuceInitialiser_GUI gui;
        if (argc == 4 && juce::String(argv[1]) == "--compat")
            return runCompatibility(juce::File(juce::String::fromUTF8(argv[2])), juce::File(juce::String::fromUTF8(argv[3])));
        if (argc == 3 && juce::String(argv[1]) == "--au")
            return runAudioUnitValidation(juce::File(juce::String::fromUTF8(argv[2])));
        // Tests never read or write the real user presets folder.
        const auto presetRoot = temporaryPresetRoot();
        ThrowUpProcessor::setUserPresetRootForTesting(presetRoot);
        if (argc == 2 && juce::String(argv[1]) == "--state-saves") {
            runConcurrentSaveTests();
            ThrowUpProcessor::setUserPresetRootForTesting({});
            presetRoot.deleteRecursively();
            return 0;
        }
        if (argc == 2 && juce::String(argv[1]) == "--host-keys") {
            const int failures = runHostKeysTests();
            ThrowUpProcessor::setUserPresetRootForTesting({});
            presetRoot.deleteRecursively();
            return failures == 0 ? 0 : 1;
        }
        throwup::tests::runPresetAutomationRegressionTests();
        runHandsOffRegressionTests();
        runConcurrentSaveTests();
        {
            const juce::File folder(argc >= 2 ? juce::String::fromUTF8(argv[1]) : juce::String());
            if (argc >= 2) folder.createDirectory();
            runFixRegressionTests(argc >= 2 ? &folder : nullptr);
            runFeatureHostTests(argc >= 2 ? &folder : nullptr);
            ThrowUpProcessor::setUserPresetRootForTesting(presetRoot);
        }
        ThrowUpProcessor p;
        const int programs = static_cast<int>(throwup::factoryPresets().size());
        check(p.getNumPrograms() == programs && programs == throwup::legacyPresetStart() + 50,
              "factory programs: the current bank, then the 50 rooms of 0.1.5");
        check(p.getParameters().size() == ThrowUpProcessor::parameterCount, "all 34 controls exposed as host parameters");
        check(p.getBypassParameter() == p.apvts.getParameter("bypass"), "host bypass uses dedicated bypass parameter");
        std::set<std::string> names;
        for (int i = 0; i < programs; ++i) {
            p.loadFactoryPreset(i);
            names.insert(p.getProgramName(i).toStdString());
        }
        check(static_cast<int>(names.size()) == programs, "all factory program names unique");
        set(p, "mix", 73.42f); set(p, "output", -3.4f); set(p, "bypass", 1);
        for (int i = 0; i < programs; ++i) p.loadFactoryPreset(i);
        check(std::abs(get(p, "mix") - 73.42f) < .02f && std::abs(get(p, "output") + 3.4f) < .02f && get(p, "bypass") == 1,
            "all preset changes preserve THROW, output, and bypass");
        set(p, "bypass", 0); set(p, "decay", 7.31f);
        juce::MemoryBlock state;
        p.getStateInformation(state);
        ThrowUpProcessor recalled;
        recalled.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        bool equal = recalled.getCurrentProgram() == programs - 1;
        for (int i = 0; i < p.getParameters().size(); ++i)
            equal = equal && std::abs(p.getParameters()[i]->getValue() - recalled.getParameters()[i]->getValue()) < 1e-6f;
        check(equal, "project state recalls every parameter and selected preset");
        recalled.setStateInformation("invalid", 7);
        check(std::abs(get(recalled, "decay") - 7.31f) < .02f, "invalid state safely ignored");
        p.loadFactoryPreset(0); set(p, "mix", 0); set(p, "output", 0);
        p.prepareToPlay(48000, 257);
        check(p.getLatencySamples() == 0, "zero latency reported");
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> audio(2, 257), original(2, 257);
        float worst = 0;
        for (int b = 0; b < 80; ++b) {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 257; ++i) audio.setSample(c, i, signal(b * 257 + i, c));
            original.makeCopyOf(audio);
            p.processBlock(audio, midi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 257; ++i)
                worst = std::max(worst, std::abs(audio.getSample(c, i) - original.getSample(c, i)));
        }
        check(worst < 1e-6f, "THROW zero passes original audio at unity");
        auto mono = p.getBusesLayout(); mono.inputBuses.set(0, juce::AudioChannelSet::mono()); mono.outputBuses.set(0, juce::AudioChannelSet::mono());
        check(p.isBusesLayoutSupported(mono), "mono bus layout accepted");
        mono.outputBuses.set(0, juce::AudioChannelSet::stereo());
        check(!p.isBusesLayoutSupported(mono), "mismatched bus layout rejected");

        if (argc >= 2) {
            const juce::File folder(juce::String::fromUTF8(argv[1])); folder.createDirectory();
            p.loadFactoryPreset(23); set(p, "mix", 64);
            ThrowUpEditor editor(p);
            check(editor.getWidth() >= 600 && editor.getHeight() >= 700, "portrait native hardware editor");
            int sliderCount = 0;
            juce::Slider* throwKnob = nullptr;
            juce::ComboBox* presetMenu = nullptr;
            for (auto* child : editor.getChildren()) {
                if (auto* slider = dynamic_cast<juce::Slider*>(child)) {
                    ++sliderCount;
                    if (slider->getName() == "THROW") throwKnob = slider;
                }
                if (auto* menu = dynamic_cast<juce::ComboBox*>(child)) presetMenu = menu;
            }
            check(sliderCount == 27 && throwKnob != nullptr, "editor exposes giant THROW plus 26 controls across the plate and display pages");
            throwKnob->setValue(47, juce::sendNotificationSync);
            check(std::abs(get(p, "mix") - 47) < .02f, "moving the actual THROW control changes its host parameter");
            set(p, "mix", 64);
            check(std::abs(throwKnob->getValue() - 64) < .02, "host automation updates the actual THROW control");
            check(presetMenu != nullptr && presetMenu->getNumItems() == programs, "editor menu exposes every factory preset by folder");
            presetMenu->setSelectedId(44, juce::sendNotificationSync);
            check(p.getCurrentProgram() == 43 && std::abs(get(p, "mix") - 64) < .02f, "preset menu loads creative sound while preserving THROW");
            presetMenu->setSelectedId(24, juce::sendNotificationSync);
            saveImage(editor, folder.getChildFile("THROW-UP.png"));
            checkInfoPage(editor, p, folder.getChildFile("THROW-UP-info.png"));
            editor.setSize(528, 684);
            saveImage(editor, folder.getChildFile("THROW-UP-compact.png"));
            checkInfoPage(editor, p, folder.getChildFile("THROW-UP-info-compact.png"));
            renderDemo(folder.getChildFile("THROW-UP-dry-then-throws.wav"));
        }
        if (argc >= 3) {
            juce::VST3PluginFormat format;
            juce::OwnedArray<juce::PluginDescription> descriptions;
            format.findAllTypesForFile(descriptions, juce::String::fromUTF8(argv[2]));
            check(descriptions.size() == 1, "compiled VST3 scans as one plugin");
            juce::String error;
            auto hosted = format.createInstanceFromDescription(*descriptions[0], 48000, 128, error);
            check(hosted != nullptr, "compiled VST3 instantiates in test host");
            ThrowUpProcessor reference;
            reference.loadFactoryPreset(33); set(reference, "mix", 70);
            int matched = 0;
            for (auto* parameter : reference.getParameters()) {
                for (auto* hostedParameter : hosted->getParameters()) {
                    if (hostedParameter->getName(128) == parameter->getName(128)) {
                        hostedParameter->setValueNotifyingHost(parameter->getValue());
                        ++matched;
                        break;
                    }
                }
            }
            check(matched == ThrowUpProcessor::parameterCount, "all compiled VST3 controls receive host parameter changes");
            // VST3 hosts wrap component/controller state; round-trip their own blob.
            juce::MemoryBlock hostedState;
            hosted->getStateInformation(hostedState);
            check(hostedState.getSize() > 0, "compiled VST3 project state serializes");
            auto reopened = format.createInstanceFromDescription(*descriptions[0], 48000, 128, error);
            check(reopened != nullptr, "second compiled VST3 instance opens");
            reopened->setStateInformation(hostedState.getData(), static_cast<int>(hostedState.getSize()));
            bool wrappedEqual = hosted->getParameters().size() == reopened->getParameters().size();
            for (int i = 0; i < hosted->getParameters().size() && wrappedEqual; ++i)
                wrappedEqual = std::abs(hosted->getParameters()[i]->getValue() - reopened->getParameters()[i]->getValue()) < 1e-5f;
            check(wrappedEqual, "compiled VST3 state recalls into a new instance");
            reference.prepareToPlay(48000, 128); hosted->prepareToPlay(48000, 128);
            check(hosted->getLatencySamples() == 0, "compiled VST3 reports zero latency");
            double maximumDifference = 0, wetDifference = 0;
            for (int b = 0; b < 600; ++b) {
                juce::AudioBuffer<float> a(2, 128), r(2, 128);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) a.setSample(c, i, signal(b * 128 + i, c));
                r.makeCopyOf(a);
                hosted->processBlock(a, midi); reference.processBlock(r, midi);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) {
                    maximumDifference = std::max(maximumDifference, double(std::abs(a.getSample(c, i) - r.getSample(c, i))));
                    wetDifference += std::abs(a.getSample(c, i) - signal(b * 128 + i, c));
                }
            }
            check(maximumDifference < 3e-5, "compiled VST3 audio matches tested processor");
            check(wetDifference > 100, "compiled VST3 audibly changes bass audio");
            std::cout << "VST3 maximum difference " << maximumDifference << '\n';
            for (auto* parameter : hosted->getParameters())
                if (parameter->getName(128) == "THROW") parameter->setValueNotifyingHost(1.0f);
            double wetEnergy = 0;
            for (int b = 0; b < 375; ++b) {
                audio.setSize(2, 128); audio.clear();
                hosted->processBlock(audio, midi);
                if (b > 3) wetEnergy += audio.getMagnitude(0, audio.getNumSamples());
            }
            check(wetEnergy > .01, "compiled VST3 emits a real reverb tail on silent input at full wet");
            hosted->releaseResources();
        }
        ThrowUpProcessor::setUserPresetRootForTesting({});
        presetRoot.deleteRecursively();
        std::cout << "THROW-UP host checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
