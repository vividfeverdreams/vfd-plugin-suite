#include "ThrowUpProcessor.h"
#include "ThrowUpEditor.h"
#include "Presets.h"
#include "PresetAutomationTests.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <set>
#include <cmath>
#include <vector>
#include <array>
#include <functional>
#include <cstring>

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
        const auto& room = throwup::factoryPresets()[23].settings;
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
              && std::abs(get(restored, "size") - throwup::factoryPresets()[37].settings.size) < .05f,
              "state with unknown children and a bad value restores every other parameter");
        set(restored, "mix", 12.0f);
        xml->setTagName("NOT_THROW_UP");
        juce::MemoryBlock wrongRoot;
        juce::AudioProcessor::copyXmlToBinary(*xml, wrongRoot);
        restored.setStateInformation(wrongRoot.getData(), static_cast<int>(wrongRoot.getSize()));
        check(std::abs(get(restored, "mix") - 12.0f) < .02f, "state with a foreign root is still rejected");
    }
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
    check(overlay->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)) && overlay->isVisible(),
          "credits page consumes unrelated keys without activating underlying controls");
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
        throwup::tests::runPresetAutomationRegressionTests();
        {
            const juce::File folder(argc >= 2 ? juce::String::fromUTF8(argv[1]) : juce::String());
            if (argc >= 2) folder.createDirectory();
            runFixRegressionTests(argc >= 2 ? &folder : nullptr);
        }
        ThrowUpProcessor p;
        check(p.getNumPrograms() == 50, "50 factory programs exposed to host");
        check(p.getParameters().size() == 13, "all 13 controls exposed as host parameters");
        check(p.getBypassParameter() == p.apvts.getParameter("bypass"), "host bypass uses dedicated bypass parameter");
        std::set<std::string> names;
        for (int i = 0; i < 50; ++i) {
            p.loadFactoryPreset(i);
            names.insert(p.getProgramName(i).toStdString());
        }
        check(names.size() == 50, "all factory program names unique");
        set(p, "mix", 73.42f); set(p, "output", -3.4f); set(p, "bypass", 1);
        for (int i = 0; i < 50; ++i) p.loadFactoryPreset(i);
        check(std::abs(get(p, "mix") - 73.42f) < .02f && std::abs(get(p, "output") + 3.4f) < .02f && get(p, "bypass") == 1,
            "all preset changes preserve THROW, output, and bypass");
        set(p, "bypass", 0); set(p, "decay", 7.31f);
        juce::MemoryBlock state;
        p.getStateInformation(state);
        ThrowUpProcessor recalled;
        recalled.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        bool equal = recalled.getCurrentProgram() == 49;
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
            check(sliderCount == 12 && throwKnob != nullptr, "editor exposes giant THROW plus 11 shaping controls");
            throwKnob->setValue(47, juce::sendNotificationSync);
            check(std::abs(get(p, "mix") - 47) < .02f, "moving the actual THROW control changes its host parameter");
            set(p, "mix", 64);
            check(std::abs(throwKnob->getValue() - 64) < .02, "host automation updates the actual THROW control");
            check(presetMenu != nullptr && presetMenu->getNumItems() == 50, "editor menu exposes all 50 grouped presets");
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
            check(matched == 13, "all compiled VST3 controls receive host parameter changes");
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
        std::cout << "THROW-UP host checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
