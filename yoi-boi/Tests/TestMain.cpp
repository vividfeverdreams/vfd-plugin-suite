#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/YoiBoiProcessor.h"
#include "PresetRender.h"
#include "CompatibilityFixtures.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#if JUCE_MAC && JUCE_PLUGINHOST_AU
 #include <AudioToolbox/AudioToolbox.h>
 #include <dlfcn.h>
#endif

namespace
{
#if JUCE_MAC && JUCE_PLUGINHOST_AU
// Validates the built Audio Unit in-process without installing it: the bundle's
// own factory is registered for this process only, then hosted through JUCE.
bool checkAudioUnit (const juce::String& path)
{
    const juce::File bundle (path);
    auto plist = juce::parseXML (bundle.getChildFile ("Contents/Info.plist"));
    if (plist == nullptr) { std::cerr << "AU: no Info.plist\n"; return false; }
    juce::String type, subtype, manufacturer, factory, name;
    int version = 0;
    if (auto* dict = plist->getChildByName ("dict"))
        for (auto* key = dict->getFirstChildElement(); key != nullptr; key = key->getNextElement())
            if (key->getAllSubText() == "AudioComponents")
                if (auto* array = key->getNextElement())
                    if (auto* component = array->getChildByName ("dict"))
                        for (auto* entry = component->getFirstChildElement(); entry != nullptr; entry = entry->getNextElement())
                        {
                            const auto field = entry->getAllSubText();
                            auto* value = entry->getNextElement();
                            if (value == nullptr) break;
                            if (field == "type") type = value->getAllSubText();
                            else if (field == "subtype") subtype = value->getAllSubText();
                            else if (field == "manufacturer") manufacturer = value->getAllSubText();
                            else if (field == "factoryFunction") factory = value->getAllSubText();
                            else if (field == "name") name = value->getAllSubText();
                            else if (field == "version") version = value->getAllSubText().getIntValue();
                            entry = value;
                        }
    const auto fourCC = [] (const juce::String& code) -> OSType
    {
        if (code.length() != 4) return 0;
        return static_cast<OSType> ((static_cast<juce::uint32> (code[0]) << 24) | (static_cast<juce::uint32> (code[1]) << 16)
                                    | (static_cast<juce::uint32> (code[2]) << 8) | static_cast<juce::uint32> (code[3]));
    };
    std::cout << "AU component: " << name << " type=" << type << " subtype=" << subtype << " manufacturer=" << manufacturer
              << " version=" << version << " factory=" << factory << '\n';
    if (type != "aumu" || subtype.length() != 4 || manufacturer.length() != 4 || factory.isEmpty())
    { std::cerr << "AU: unexpected identifiers\n"; return false; }
    void* image = dlopen (bundle.getChildFile ("Contents/MacOS").getChildFile (bundle.getFileNameWithoutExtension())
                              .getFullPathName().toRawUTF8(), RTLD_NOW | RTLD_LOCAL);
    if (image == nullptr) { std::cerr << "AU: dlopen failed: " << dlerror() << '\n'; return false; }
    auto* entry = reinterpret_cast<AudioComponentFactoryFunction> (dlsym (image, factory.toRawUTF8()));
    if (entry == nullptr) { std::cerr << "AU: factory symbol missing\n"; return false; }
    AudioComponentDescription description { fourCC (type), fourCC (subtype), fourCC (manufacturer), 0, 0 };
    if (AudioComponentRegister (&description, CFSTR ("VIVID FEVER DREAMS: YOI-BOI (in-process test)"),
                                static_cast<UInt32> (version), entry) == nullptr)
    { std::cerr << "AU: registration failed\n"; return false; }
    juce::AudioUnitPluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, path);
    if (found.isEmpty()) { std::cerr << "AU: JUCE host found no instrument\n"; return false; }
    juce::String error;
    auto plugin = format.createInstanceFromDescription (*found[0], 48000.0, 512, error);
    if (plugin == nullptr) { std::cerr << "AU: instantiate failed: " << error << '\n'; return false; }
    plugin->setPlayConfigDetails (0, 2, 48000.0, 512);
    plugin->prepareToPlay (48000.0, 512);
    float peak = 0.0f;
    bool finite = true;
    juce::AudioBuffer<float> audio (2, 512);
    for (int block = 0; block < 40; ++block)
    {
        audio.clear();
        juce::MidiBuffer midi;
        if (block == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110), 0);
        if (block == 30) midi.addEvent (juce::MidiMessage::noteOff (1, 36), 0);
        plugin->processBlock (audio, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
            {
                const float v = audio.getSample (c, i);
                finite = finite && std::isfinite (v);
                peak = std::max (peak, std::abs (v));
            }
    }
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    plugin->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    const auto parameters = plugin->getParameters().size();
    plugin->releaseResources();
    std::cout << "AU in-process: " << found[0]->name << " v" << found[0]->version << ", " << parameters
              << " parameters, latency " << plugin->getLatencySamples() << ", peak " << peak
              << ", state " << state.getSize() << " bytes\n";
    plugin.reset();
    return finite && peak > 0.001f && peak <= 1.0f && parameters >= 42 && state.getSize() > 0;
}
#endif

bool smokeTestVst3 (const juce::String& path)
{
    juce::VST3PluginFormatHeadless format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile (descriptions, path);
    if (descriptions.isEmpty())
    {
        std::cerr << "VST3 scan found no plug-in at " << path << '\n';
        return false;
    }

    juce::String error;
    auto plugin = format.createInstanceFromDescription (*descriptions[0], 48000.0, 512, error);
    if (plugin == nullptr)
    {
        std::cerr << "VST3 host could not instantiate: " << error << '\n';
        return false;
    }
    plugin->setPlayConfigDetails (0, 2, 48000.0, 512);
    plugin->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> audio (2, 512);
    audio.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
    plugin->processBlock (audio, midi);
    float peak = 0.0f;
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const auto sample = audio.getSample (0, i);
        if (! std::isfinite (sample)) return false;
        peak = std::max (peak, std::abs (sample));
    }
    plugin->releaseResources();
    std::cout << "VST3 host scan: " << descriptions[0]->name << ", peak " << peak << '\n';
    return peak > 0.0001f;
}

bool renderEditor (const juce::String& path)
{
    juce::MessageManager::getInstance();
    const auto presetRoot = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("YOI-BOI-ui-" + juce::Uuid().toString());
    yoiboi::YoiBoiProcessor synth (presetRoot);
    auto callbacks = synth.makeEditorCallbacks();
    for (const auto& item : callbacks.listPresets())
        if (! item.recent && ! item.user && item.name == "Tape Bounce II")
            callbacks.loadPreset(item.id);
    synth.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> previewAudio (2, 512);
    juce::MidiBuffer previewMidi;
    for (int block = 0; block < 28; ++block)
    {
        previewAudio.clear();
        previewMidi.clear();
        if (block == 0 || block == 27)
            previewMidi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
        synth.processBlock (previewAudio, previewMidi);
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor (synth.createEditor());
    if (editor == nullptr) return false;
    const auto save = [&] (const juce::String& destination)
    {
        const auto width = editor->getWidth(), height = editor->getHeight();
        if (width <= 0 || height <= 0) return false;
        juce::Image image (juce::Image::ARGB, width, height, true);
        juce::Graphics graphics (image);
        editor->paintEntireComponent (graphics, true);
        auto stream = juce::File (destination).createOutputStream();
        if (stream == nullptr) return false;
        stream->setPosition (0);
        if (stream->truncate().failed()) return false;
        juce::PNGImageFormat png;
        return png.writeImageToStream (image, *stream);
    };
    const auto findTab = [&] (const auto& self, juce::Component* parent, const juce::String& name) -> juce::TextButton*
    {
        if (auto* button = dynamic_cast<juce::TextButton*> (parent))
            if (button->getButtonText() == name) return button;
        for (auto* child : parent->getChildren())
            if (auto* button = self (self, child, name)) return button;
        return nullptr;
    };
    if (! save (path)) return false;
    const auto file = juce::File (path);
    const auto findComponent = [&] (const auto& self, juce::Component* root,
                                   const juce::String& name) -> juce::Component*
    {
        if (root->getName() == name) return root;
        for (auto* child : root->getChildren())
            if (auto* match = self(self, child, name)) return match;
        return nullptr;
    };
    auto* info = dynamic_cast<juce::Button*>(findComponent(findComponent, editor.get(), "INFO"));
    auto* page = findComponent(findComponent, editor.get(), "VFD Info Page");
    if (!info || !info->onClick || !page || page->isVisible()) return false;
    info->onClick();
    if (!page->isVisible() || !save(file.getSiblingFile(file.getFileNameWithoutExtension()
                    + "-info.png").getFullPathName())) return false;
    if (!page->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) || page->isVisible()) return false;
    const auto pump = []
    {
        for (int i = 0; i < 6; ++i) { juce::Timer::callPendingTimersSynchronously(); juce::Thread::sleep (12); }
    };
    const auto shot = [&] (const juce::String& suffix)
    {
        pump();
        return save (file.getSiblingFile (file.getFileNameWithoutExtension() + "-" + suffix + ".png").getFullPathName());
    };
    const auto clickTab = [&] (const juce::String& name)
    {
        auto* tab = findTab (findTab, editor.get(), name);
        if (tab == nullptr || ! tab->onClick) return false;
        tab->onClick();
        return true;
    };
    for (const auto* name : { "SHAPE", "MOD", "LIBRARY" })
        if (! clickTab (name) || ! shot (juce::String (name).toLowerCase())) return false;
    // Bounce eight variations into a temporary folder and show the tiles.
    const auto bounceFolder = presetRoot.getSiblingFile (presetRoot.getFileName() + "-bounces");
    if (! callbacks.startBounce (8, false, bounceFolder)) return false;
    for (int wait = 0; wait < 600 && callbacks.bounceInfo().running; ++wait) juce::Thread::sleep (50);
    if (! clickTab ("BOUNCES") || ! shot ("library-bounces")) return false;
    // An original (engine 3) sound: MOD disabled with its notice; the nine vowels give way to five.
    callbacks.loadPreset ("factory:0");
    if (! clickTab ("MOD") || ! shot ("mod-classic")) return false;
    if (! clickTab ("SHAPE") || ! shot ("shape-classic")) return false;
    for (const auto& item : callbacks.listPresets())
        if (! item.user && item.name == "Chatterbox") callbacks.loadPreset (item.id);
    if (! clickTab ("SHAPE") || ! shot ("shape-latest")) return false;
    if (! clickTab ("PLAY")) return false;
    for (const float scale : { 0.9f, 1.25f, 1.5f })
    {
        editor->setSize(juce::roundToInt(1055 * scale), juce::roundToInt(717 * scale));
        if (! shot (juce::String (juce::roundToInt (scale * 100)))) return false;
    }
    // Random preset: a random pick, then the die under the pointer and pressed beside the name, under the
    // pointer in the Library, and disabled there when the view (no Favorites yet) is empty.
    editor->setSize (1055, 717);
    const auto dieIn = [&] (juce::Button::ButtonState state)
    {
        auto* die = findTab (findTab, editor.get(), "RANDOM");
        if (die == nullptr || ! die->isVisible()) return false;
        die->setState (state);
        return true;
    };
    if (auto* die = findTab (findTab, editor.get(), "RANDOM"); die == nullptr || ! die->onClick) return false;
    else die->onClick();
    if (! dieIn (juce::Button::buttonOver) || ! shot ("random")) return false;
    if (! dieIn (juce::Button::buttonDown) || ! shot ("random-down")) return false;
    if (! clickTab ("LIBRARY") || ! clickTab ("SOUNDS") || ! dieIn (juce::Button::buttonOver) || ! shot ("library-random")) return false;
    if (! clickTab ("FAVORITES") || ! dieIn (juce::Button::buttonNormal) || ! shot ("library-random-empty")) return false;
    editor.reset();
    presetRoot.deleteRecursively();
    bounceFolder.deleteRecursively();
    return true;
}
} // namespace

int main (int argc, char* argv[])
{
    // Every mode runs with a private HOME, so nothing here can read or write
    // the user's real preset, settings or Music folders.
    const auto testHome = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("YOI-BOI-test-home-" + juce::Uuid().toString());
    testHome.getChildFile ("Library/Application Support").createDirectory();
    testHome.getChildFile ("Music").createDirectory();
    setenv ("HOME", testHome.getFullPathName().toRawUTF8(), 1);
    struct CleanUp { juce::File folder; ~CleanUp() { folder.deleteRecursively(); } } cleanUp { testHome };
#if JUCE_MAC && JUCE_PLUGINHOST_AU
    if (argc >= 3 && juce::String::fromUTF8(argv[1]) == "--check-au")
        return checkAudioUnit (juce::String::fromUTF8 (argv[2])) ? 0 : 1;
#endif
    if (argc >= 3 && juce::String::fromUTF8(argv[1]) == "--preset-stats")
        return yoiboi::renderFactoryDemos (juce::File (juce::String::fromUTF8 (argv[2])), false) ? 0 : 1;
    if (argc >= 4 && juce::String::fromUTF8(argv[1]) == "--check-compat")
        return yoiboi::testPluginCompatibility (juce::String::fromUTF8(argv[2]),
                                               juce::String::fromUTF8(argv[3])) ? 0 : 1;
    if (argc >= 3 && juce::String::fromUTF8(argv[1]) == "--render-demos")
        return yoiboi::renderFactoryDemos (juce::File (juce::String::fromUTF8 (argv[2]))) ? 0 : 1;
    if (argc >= 3 && juce::String::fromUTF8(argv[1]) == "--render-ui")
        return renderEditor(juce::String::fromUTF8(argv[2])) ? 0 : 1;
    // --host-keys: the editor in an off-screen window, keys and clicks sent the way a host sends them.
    const bool hostKeysOnly = argc >= 2 && juce::String::fromUTF8 (argv[1]) == "--host-keys";
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory (hostKeysOnly ? "YOI-BOI host keys" : "YOI-BOI", 12345);

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (const auto* result = runner.getResult (i))
        {
            failures += result->failures;
            std::cout << result->unitTestName << " / " << result->subcategoryName
                      << ": " << result->passes << " passed, " << result->failures << " failed\n";
            for (const auto& message : result->messages)
                std::cout << "  " << message << '\n';
        }
    }
    std::cout << "YOI-BOI tests: " << failures << " failures\n";
    if (hostKeysOnly)
        return failures == 0 && runner.getNumResults() > 0 ? 0 : 1;
    const bool hostPassed = argc < 2 || smokeTestVst3 (juce::String::fromUTF8 (argv[1]));
    const bool editorPassed = argc < 3 || renderEditor (juce::String::fromUTF8 (argv[2]));
    if (argc >= 3) std::cout << "Editor offscreen render: " << (editorPassed ? "passed" : "failed") << '\n';
    return failures == 0 && runner.getNumResults() > 0 && hostPassed && editorPassed ? 0 : 1;
}
