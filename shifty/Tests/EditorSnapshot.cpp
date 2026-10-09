#include "../Source/Plugin/ShiftyProcessor.h"
#include "../Source/UI/ShiftyEditor.h"
#include "FeatureTests.h"
#include "RedesignTests.h"
#include "Compatibility.h"

#include <juce_graphics/juce_graphics.h>
#include <iostream>
#if JUCE_MAC
 #include <AudioToolbox/AudioToolbox.h>
 #include <CoreFoundation/CoreFoundation.h>
#endif

namespace {
juce::Component* findComponent(juce::Component& root, const juce::String& name) {
    if (root.getName() == name) return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findComponent(*child, name)) return found;
    return nullptr;
}

bool checkInfoPage(shifty::ShiftyProcessor& processor, shifty::ShiftyEditor& editor) {
    editor.setVisible(true);
    auto* info = dynamic_cast<juce::Button*>(findComponent(editor, "INFO"));
    auto* page = findComponent(editor, "VFD Info Page");
    auto* close = dynamic_cast<juce::Button*>(findComponent(editor, "Close info"));
    if (!info || !info->onClick || !page || !close || !close->onClick || page->isVisible()) return false;
    std::vector<float> before;
    for (auto* parameter : processor.getParameters()) before.push_back(parameter->getValue());
    info->onClick();
    if (!page->isVisible() || page->getBounds() != editor.getLocalBounds()) return false;
    std::set<juce::String> urls;
    const auto collectLinks = [&](const auto& self, juce::Component& root) -> void {
        if (auto* link = dynamic_cast<juce::HyperlinkButton*>(&root)) urls.insert(link->getURL().toString(true));
        for (auto* child : root.getChildren()) self(self, *child);
    };
    collectLinks(collectLinks, *page);
    if (urls != std::set<juce::String>{"https://x.com/cxy", "https://x.com/clt",
            "https://www.vividfeverdreams.xyz", "https://fundraising.fracturedatlas.org/dream-sequence"}) return false;
    auto* random = findComponent(editor, "Randomize");
    if (!random) return false;
    auto* hit = editor.getComponentAt(editor.getLocalPoint(random, random->getLocalBounds().getCentre()));
    if (hit != page && !page->isParentOf(hit)) return false;
    if (!page->keyPressed(juce::KeyPress(juce::KeyPress::downKey)) ||
        !page->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))) return false;
    if (!page->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) || page->isVisible()) return false;
    info->onClick();
    close->onClick();
    if (page->isVisible()) return false;
    for (int index = 0; index < processor.getParameters().size(); ++index)
        if (std::bit_cast<std::uint32_t>(before[static_cast<size_t>(index)]) !=
            std::bit_cast<std::uint32_t>(processor.getParameters()[index]->getValue())) return false;
    std::cout << "INFO page open, Escape, close, links, blocked underlying controls, bounds and parameter-preservation checks passed.\n";
    return true;
}

/** Wraps SHIFTY's own state the way JUCE's VST3 host stores a plugin's state. */
juce::MemoryBlock hostedVst3State(const juce::MemoryBlock& pluginState) {
    juce::XmlElement state("VST3PluginState");
    state.createNewChildElement("IComponent")->addTextElement(pluginState.toBase64Encoding());
    juce::MemoryBlock result;
    juce::AudioProcessor::copyXmlToBinary(state, result);
    return result;
}

int checkVst3HostLatency(const juce::String& pluginPath) {
    if (!juce::File::isAbsolutePath(pluginPath) || !juce::File(pluginPath).exists()) {
        std::cerr << "VST3 path must be an existing absolute path.\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI gui;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions, pluginPath);
    if (descriptions.isEmpty()) {
        std::cerr << "Host could not find a VST3 plugin type in the bundle.\n";
        return 1;
    }

    juce::String error;
    auto hosted = format.createInstanceFromDescription(*descriptions[0], 48000.0, 512, error);
    if (!hosted) {
        std::cerr << "Host could not instantiate VST3: " << error << '\n';
        return 1;
    }

    // Latency per engine, set through saved states as a host would restore them.
    bool latencyOk = true;
    const struct { shifty::EngineMode mode; bool lowLatency; const char* name; int at48k, at96k; } engines[] = {
        { shifty::EngineMode::talk, false, "TALK", 0, 0 },
        { shifty::EngineMode::mouth, false, "MOUTH", 1024, 2048 },
        { shifty::EngineMode::classic, false, "CLASSIC", 2048, 4096 },
        { shifty::EngineMode::classic, true, "CLASSIC low latency", 1024, 2048 },
        { shifty::EngineMode::original, false, "CLASSIC original 0.4.2 sound", 2048, 4096 },
    };
    const int newInstanceLatency = hosted->getLatencySamples();
    for (const auto& engine : engines) {
        shifty::ShiftyProcessor source;
        source.setEngine(engine.mode, engine.lowLatency);
        juce::MemoryBlock pluginState;
        source.getStateInformation(pluginState);
        const auto stateBlock = hostedVst3State(pluginState);
        hosted->setStateInformation(stateBlock.getData(), static_cast<int>(stateBlock.getSize()));
        hosted->prepareToPlay(48000.0, 512);
        const int at48k = hosted->getLatencySamples();
        hosted->releaseResources();
        hosted->prepareToPlay(96000.0, 512);
        const int at96k = hosted->getLatencySamples();
        hosted->releaseResources();
        std::cout << "Hosted VST3 latency, " << engine.name << ": 48 kHz " << at48k << ", 96 kHz " << at96k << " samples.\n";
        latencyOk = latencyOk && at48k == engine.at48k && at96k == engine.at96k;
    }
    std::cout << "Hosted VST3 latency of a new instance (TALK) before prepare: " << newInstanceLatency << ".\n";

    // The exported parameter list stays append-only: the 16 controls of 0.4.2
    // in order, the 13 controls added in 0.5.0, then the VST3 wrapper's own host bypass.
    const auto& parameters = hosted->getParameters();
    juce::StringArray names;
    for (auto* parameter : parameters) names.add(parameter->getName(64));
    auto* hostBypass = hosted->getBypassParameter();
    std::cout << "Hosted VST3 parameters (" << parameters.size() << "): "
              << names.joinIntoString(", ") << "; host bypass: "
              << (hostBypass != nullptr ? hostBypass->getName(64) : juce::String("none")) << ".\n";
    const juce::StringArray expected { "Shift", "Focus", "Bite", "Drive", "Mix", "Sub Safe",
        "Sub Safe Frequency", "Output", "Motion Enabled", "Motion Shape", "Motion Rate", "Motion Depth",
        "Motion Sync", "Motion Free Rate", "Bypass", "Vowel Color",
        "Vowel Morph", "Voice", "Character", "Talk", "Envelope Amount", "Envelope Attack", "Envelope Release",
        "Yoi Amount", "Yoi From", "Yoi To", "Yoi Time", "Yoi Shift", "Motion Target", "Bypass" };
    const bool parameterListOk = names == expected && hostBypass != nullptr
                              && hostBypass == parameters.getLast();
    return latencyOk && newInstanceLatency == 0 && parameterListOk ? 0 : 1;
}

#if JUCE_MAC
/** Puts SHIFTY's own state into an AU ClassInfo property list taken from the hosted AU. */
juce::MemoryBlock auStateWith(const juce::MemoryBlock& hostedState, const juce::MemoryBlock& pluginState) {
    juce::MemoryBlock result;
    CFDataRef data = CFDataCreate(nullptr, static_cast<const UInt8*>(hostedState.getData()), static_cast<CFIndex>(hostedState.getSize()));
    CFPropertyListFormat format = kCFPropertyListBinaryFormat_v1_0;
    CFPropertyListRef list = CFPropertyListCreateWithData(nullptr, data, kCFPropertyListImmutable, &format, nullptr);
    CFRelease(data);
    if (list == nullptr || CFGetTypeID(list) != CFDictionaryGetTypeID()) { if (list) CFRelease(list); return result; }
    CFMutableDictionaryRef copy = CFDictionaryCreateMutableCopy(nullptr, 0, static_cast<CFDictionaryRef>(list));
    CFRelease(list);
    CFDataRef juceState = CFDataCreate(nullptr, static_cast<const UInt8*>(pluginState.getData()), static_cast<CFIndex>(pluginState.getSize()));
    CFDictionarySetValue(copy, CFSTR("jucePluginState"), juceState);
    CFRelease(juceState);
    if (CFDataRef out = CFPropertyListCreateData(nullptr, copy, kCFPropertyListBinaryFormat_v1_0, 0, nullptr)) {
        result.append(CFDataGetBytePtr(out), static_cast<size_t>(CFDataGetLength(out)));
        CFRelease(out);
    }
    CFRelease(copy);
    return result;
}

/** Validates the AU from the build folder without installing it: the component's factory is
    registered for this process only (AudioComponentRegister), then hosted through JUCE's AU host. */
int checkAudioUnitInProcess(const juce::String& componentPath) {
    if (!juce::File::isAbsolutePath(componentPath) || !juce::File(componentPath).isDirectory()) {
        std::cerr << "AU path must be an existing absolute .component path.\n";
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    const auto utf8 = componentPath.toRawUTF8();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(utf8),
                                                           static_cast<CFIndex>(std::strlen(utf8)), true);
    CFBundleRef bundle = CFBundleCreate(nullptr, url);
    CFRelease(url);
    if (bundle == nullptr || !CFBundleLoadExecutable(bundle)) { std::cerr << "Could not load the AU bundle.\n"; return 1; }
    auto* factory = reinterpret_cast<AudioComponentFactoryFunction>(CFBundleGetFunctionPointerForName(bundle, CFSTR("SHIFTYAUFactory")));
    if (factory == nullptr) { std::cerr << "SHIFTYAUFactory not exported.\n"; return 1; }
    // Register under the bundle's own version, so the check follows the release being tested
    // (an installed copy with a higher version would otherwise win the lookup).
    juce::String bundleVersion;
    if (auto shortVersion = static_cast<CFStringRef>(CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("CFBundleShortVersionString"))))
        bundleVersion = juce::String::fromCFString(shortVersion);
    const auto parts = juce::StringArray::fromTokens(bundleVersion, ".", {});
    const auto packed = static_cast<UInt32>((parts[0].getIntValue() << 16) | (parts[1].getIntValue() << 8) | parts[2].getIntValue());
    AudioComponentDescription description {};
    description.componentType = 'aufx';
    description.componentSubType = 'Shf1';
    description.componentManufacturer = 'Yoib';
    if (bundleVersion.isEmpty() || AudioComponentRegister(&description, CFSTR("VIVID FEVER DREAMS: SHIFTY"), packed, factory) == nullptr) {
        std::cerr << "AudioComponentRegister failed.\n";
        return 1;
    }

    juce::AudioUnitPluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types, "AudioUnit:Effects/aufx,Shf1,Yoib");
    if (types.isEmpty()) { std::cerr << "JUCE's AU host did not find the registered component.\n"; return 1; }
    std::cout << "AU found in process: " << types[0]->manufacturerName << " / " << types[0]->name << " "
              << types[0]->version << " (" << types[0]->fileOrIdentifier << ")\n";
    juce::String error;
    auto hosted = format.createInstanceFromDescription(*types[0], 48000.0, 512, error);
    if (!hosted) { std::cerr << "Could not instantiate the AU: " << error << '\n'; return 1; }
    bool ok = types[0]->version == bundleVersion;
    if (!ok) std::cout << "Hosted AU is " << types[0]->version << ", the bundle is " << bundleVersion << ".\n";

    // Parameters: the 29 SHIFTY controls (an AU's host bypass is a property, not a parameter). AU
    // parameter IDs are hashes of SHIFTY's string IDs, so hosts list them in hash order.
    juce::StringArray names;
    for (auto* parameter : hosted->getParameters()) names.add(parameter->getName(64));
    std::cout << "AU parameters (" << names.size() << "): " << names.joinIntoString(", ") << "\n";
    shifty::ShiftyProcessor inProcess;
    bool allPresent = names.size() == inProcess.getParameters().size();
    for (auto* parameter : inProcess.getParameters()) allPresent = allPresent && names.contains(parameter->getName(64));
    ok = ok && names.size() == 29 && allPresent;

    // Latency per engine, through saved states as a host restores them.
    juce::MemoryBlock hostedState;
    hosted->getStateInformation(hostedState);
    const struct { shifty::EngineMode mode; bool low; const char* name; int at48k; } engines[] = {
        { shifty::EngineMode::talk, false, "TALK", 0 }, { shifty::EngineMode::mouth, false, "MOUTH", 1024 },
        { shifty::EngineMode::classic, false, "CLASSIC", 2048 }, { shifty::EngineMode::classic, true, "CLASSIC low latency", 1024 },
        { shifty::EngineMode::original, false, "original 0.4.2 sound", 2048 } };
    for (const auto& engine : engines) {
        shifty::ShiftyProcessor source;
        if (engine.mode == shifty::EngineMode::original)
            source.loadFactoryPreset(shifty::ShiftyProcessor::factoryPresetIndex("Classic", "Rubber Throat"));
        else
            source.setEngine(engine.mode, engine.low);
        juce::MemoryBlock pluginState;
        source.getStateInformation(pluginState);
        const auto state = auStateWith(hostedState, pluginState);
        hosted->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        hosted->prepareToPlay(48000.0, 512);
        const int latency = hosted->getLatencySamples();
        hosted->releaseResources();
        std::cout << "AU latency, " << engine.name << ": " << latency << " samples at 48 kHz.\n";
        ok = ok && latency == engine.at48k;
    }

    // Audio: the AU renders what SHIFTY renders in process (TALK factory default, 1 s of saw).
    {
        shifty::ShiftyProcessor reference;
        juce::MemoryBlock pluginState;
        reference.getStateInformation(pluginState);
        const auto state = auStateWith(hostedState, pluginState);
        hosted->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        hosted->setPlayConfigDetails(2, 2, 48000.0, 512);
        hosted->prepareToPlay(48000.0, 512);
        reference.setPlayConfigDetails(2, 2, 48000.0, 512);
        reference.prepareToPlay(48000.0, 512);
        juce::MidiBuffer midi;
        double phase = 0.0, difference = 0.0, energy = 0.0;
        bool finite = true;
        for (int block = 0; block < 94; ++block) {
            juce::AudioBuffer<float> a(2, 512), b(2, 512);
            for (int i = 0; i < 512; ++i) {
                phase += 55.0 / 48000.0;
                phase -= std::floor(phase);
                const float x = 0.4f * static_cast<float>(2.0 * phase - 1.0);
                for (int c = 0; c < 2; ++c) { a.setSample(c, i, x); b.setSample(c, i, x); }
            }
            hosted->processBlock(a, midi);
            reference.processBlock(b, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i) {
                    finite = finite && std::isfinite(a.getSample(c, i));
                    difference = std::max(difference, static_cast<double>(std::abs(a.getSample(c, i) - b.getSample(c, i))));
                    energy += static_cast<double>(a.getSample(c, i)) * a.getSample(c, i);
                }
        }
        hosted->releaseResources();
        std::cout << "AU audio vs in-process SHIFTY (TALK, 1 s saw): max |difference| " << difference
                  << ", energy " << energy << (finite ? ", finite" : ", NOT FINITE") << ".\n";
        ok = ok && finite && energy > 1.0 && difference < 1.0e-5;
    }

    // Editor opens at the default size.
    if (auto* editor = hosted->createEditorAndMakeActive()) {
        std::cout << "AU editor: " << editor->getWidth() << " x " << editor->getHeight() << ".\n";
        ok = ok && editor->getWidth() == 650 && editor->getHeight() == 849;
        delete editor;
    } else {
        std::cout << "AU editor: none.\n";
        ok = false;
    }
    hosted.reset();
    std::cout << (ok ? "AU in-process validation passed.\n" : "AU in-process validation FAILED.\n");
    return ok ? 0 : 1;
}
#endif

bool writePng(const juce::Image& image, const juce::File& output) {
    if (output.getParentDirectory().createDirectory().failed()) return false;
    output.deleteFile();
    juce::FileOutputStream stream(output);
    if (!stream.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, stream)) return false;
    stream.flush();
    return stream.getStatus().wasOk();
}

// Plays a gated 55 Hz saw (notes 300 ms on, 100 ms off) so the live displays have something to show.
// It stops 70 ms into a note so a YOI sweep is mid-way.
void playGatedSaw(shifty::ShiftyProcessor& processor) {
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    processor.setPlayConfigDetails(2, 2, rate, block);
    processor.prepareToPlay(rate, block);
    juce::AudioBuffer<float> buffer(2, block);
    juce::MidiBuffer midi;
    double phase = 0.0;
    const int total = static_cast<int>(rate * 1.27);
    for (int start = 0; start < total; start += block) {
        for (int i = 0; i < block; ++i) {
            const int n = start + i;
            const double t = n / rate;
            const double inNote = std::fmod(t, 0.4);
            const float gate = inNote < 0.3 ? 1.0f : 0.0f;
            phase += 55.0 / rate;
            phase -= std::floor(phase);
            const float saw = static_cast<float>(2.0 * phase - 1.0) * 0.5f * gate;
            buffer.setSample(0, i, saw);
            buffer.setSample(1, i, saw);
        }
        processor.processBlock(buffer, midi);
    }
}

// Opens every dropdown in a real on-screen window and measures where JUCE actually puts the menu:
// each must hang from (or sit just above) the control that opened it, never under the window.
// Right-click menus open at the mouse pointer, so they are not part of this check.
class MenuPlacementCheck final : private juce::Timer {
public:
    MenuPlacementCheck() {
        processor.setUserDataRootForTesting(juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("shifty-menus-" + juce::String(juce::Time::currentTimeMillis())));
        processor.parameters().getParameter("motionSync")->setValueNotifyingHost(1.0f);
        editor = std::make_unique<shifty::ShiftyEditor>(processor);
        window = std::make_unique<juce::DocumentWindow>("SHIFTY menu check", juce::Colours::black, 0);
        window->setUsingNativeTitleBar(true);
        window->setContentNonOwned(editor.get(), true);
        window->centreWithSize(window->getWidth(), window->getHeight());
        window->setVisible(true);
        startTimer(350);
    }
    int failures = 0;

private:
    using Page = shifty::ShiftyEditor::Page;
    using Tab = shifty::ShiftyEditor::MotionTab;
    struct Case { const char* name; Page page; Tab tab; juce::Rectangle<float> design; bool saving; const char* button; };
    const std::vector<Case> cases {
        { "LFO shape", Page::motion, Tab::lfo, {170, 416, 160, 55}, false, nullptr },
        { "LFO rate", Page::motion, Tab::lfo, {330, 416, 190, 55}, false, nullptr },
        { "LFO target", Page::motion, Tab::lfo, {830, 416, 121, 55}, false, nullptr },
        { "YOI from", Page::motion, Tab::yoi, {350, 416, 140, 55}, false, nullptr },
        { "YOI to", Page::motion, Tab::yoi, {490, 416, 140, 55}, false, nullptr },
        { "Preset folder", Page::presets, Tab::lfo, {640, 204, 290, 38}, false, nullptr },
        { "Save folder", Page::presets, Tab::lfo, {728, 357, 112, 40}, true, nullptr },
        { "LOCKS", Page::sound, Tab::lfo, {}, false, "Randomization locks" },
    };

    juce::Rectangle<int> screenAreaFor(const juce::Rectangle<float>& design) const {
        // Design (shell art) coordinates to the editor: the crop starts at (57, 73) and is 1008 x 1317.
        const float sx = editor->getWidth() / 1008.0f, sy = editor->getHeight() / 1317.0f;
        const juce::Rectangle<float> local((design.getX() - 57.0f) * sx, (design.getY() - 73.0f) * sy,
                                           design.getWidth() * sx, design.getHeight() * sy);
        return editor->localAreaToGlobal(local.toNearestInt());
    }

    std::vector<juce::Component*> desktopWindows() const {
        std::vector<juce::Component*> list;
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i) list.push_back(desktop.getComponent(i));
        return list;
    }

    void timerCallback() override {
        if (index >= cases.size()) {
            stopTimer();
            juce::MessageManager::getInstance()->stopDispatchLoop();
            return;
        }
        const auto& c = cases[index];
        if (!menuOpen) {
            editor->showPageForTesting(c.page, c.tab);
            if (c.saving) editor->beginPresetSaveForTesting();
            before = desktopWindows();
            if (c.button != nullptr) {
                auto* button = dynamic_cast<juce::Button*>(findComponent(*editor, c.button));
                anchor = button->getScreenBounds();
                button->onClick(); // what a click runs, synchronously
            } else {
                anchor = screenAreaFor(c.design);
                const auto at = editor->getLocalPoint(nullptr, anchor.getCentre()).toFloat();
                const auto now = juce::Time::getCurrentTime();
                editor->mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                    juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), juce::MouseInputSource::defaultPressure,
                    juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                    juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                    editor.get(), editor.get(), now, at, now, 1, false));
            }
            menuOpen = true;
        }
        // A dropdown's window exists as soon as it is shown; measure it before the real pointer
        // (which is not over it) makes JUCE close it.
        juce::Component* menu = nullptr;
        for (auto* w : desktopWindows())
            if (std::find(before.begin(), before.end(), w) == before.end() && w->isVisible()) menu = w;
        if (menu == nullptr) {
            std::cout << "FAIL " << c.name << ": no menu opened\n";
            ++failures;
        } else {
            const auto m = menu->getScreenBounds();
            const bool alignedLeft = std::abs(m.getX() - anchor.getX()) <= 16;
            const bool below = std::abs(m.getY() - anchor.getBottom()) <= 16;
            const bool above = std::abs(m.getBottom() - anchor.getY()) <= 16;
            const bool ok = alignedLeft && (below || above);
            if (!ok) ++failures;
            std::cout << (ok ? "ok   " : "FAIL ") << c.name << ": control " << anchor.toString() << ", menu "
                      << m.toString() << (below ? " (drops below it)" : above ? " (opens above it)" : " (detached)") << "\n";
        }
        juce::PopupMenu::dismissAllActiveMenus();
        menuOpen = false;
        ++index;
    }

    shifty::ShiftyProcessor processor;
    std::unique_ptr<shifty::ShiftyEditor> editor;
    std::unique_ptr<juce::DocumentWindow> window;
    std::vector<juce::Component*> before;
    juce::Rectangle<int> anchor;
    size_t index = 0;
    bool menuOpen = false;
};

int renderGallery(const juce::File& directory) {
    juce::ScopedJuceInitialiser_GUI gui;
    using shifty::EngineMode;
    using Page = shifty::ShiftyEditor::Page;
    using Tab = shifty::ShiftyEditor::MotionTab;
    // User presets come from a scratch root, never the real preset folder.
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("shifty-gallery-" + juce::String(juce::Time::currentTimeMillis()));
    {
        shifty::ShiftyProcessor seeder;
        seeder.setUserDataRootForTesting(root);
        seeder.saveUserPreset("Rough Idea");
        seeder.saveUserPreset("My Growl", "Basses");
        seeder.saveUserPreset("Night Talk", "Basses");
        seeder.saveUserPreset("Vox Chop", "Live Set");
    }
    int classicOriginal = -1;
    for (size_t i = 0; i < shifty::ShiftyProcessor::factoryPresets().size(); ++i)
        if (shifty::ShiftyProcessor::factoryPresets()[i].engine == EngineMode::original) { classicOriginal = static_cast<int>(i); break; }

    struct Shot {
        const char* name;
        EngineMode mode;
        bool lowLatency;
        Page page;
        Tab tab;
        double scale;
        std::function<void(shifty::ShiftyProcessor&)> setupProcessor;
        std::function<void(shifty::ShiftyEditor&)> setupEditor;
    };
    const auto set = [](shifty::ShiftyProcessor& p, const char* id, float value) {
        auto* parameter = p.parameters().getParameter(id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    const auto loadOriginal = [classicOriginal](shifty::ShiftyProcessor& p) { p.loadFactoryPreset(classicOriginal); };
    const auto motionOn = [set](shifty::ShiftyProcessor& p) { set(p, "motionEnable", 1.0f); set(p, "motionDepth", 6.0f); };
    const std::vector<Shot> shots {
        { "01-sound-talk", EngineMode::talk, false, Page::sound, Tab::lfo, 1.0, {}, {} },
        { "02-sound-mouth", EngineMode::mouth, false, Page::sound, Tab::lfo, 1.0, {}, {} },
        { "03-sound-classic", EngineMode::classic, false, Page::sound, Tab::lfo, 1.0, {}, {} },
        { "04-sound-original", EngineMode::original, false, Page::sound, Tab::lfo, 1.0, loadOriginal, {} },
        { "05-voice-talk", EngineMode::talk, false, Page::voice, Tab::lfo, 1.0, {}, {} },
        { "06-voice-mouth", EngineMode::mouth, false, Page::voice, Tab::lfo, 1.0, {}, {} },
        { "07-voice-classic", EngineMode::classic, false, Page::voice, Tab::lfo, 1.0, {}, {} },
        { "08-voice-classic-lowlatency-vowel", EngineMode::classic, true, Page::voice, Tab::lfo, 1.0,
          [set](shifty::ShiftyProcessor& p) { set(p, "vowel", 1.0f); }, {} },
        { "09-voice-original", EngineMode::original, false, Page::voice, Tab::lfo, 1.0, loadOriginal, {} },
        { "10-motion-lfo-talk", EngineMode::talk, false, Page::motion, Tab::lfo, 1.0,
          [motionOn, set](shifty::ShiftyProcessor& p) { motionOn(p); set(p, "motionTarget", 2.0f); }, {} },
        { "11-motion-lfo-classic-free", EngineMode::classic, false, Page::motion, Tab::lfo, 1.0,
          [motionOn, set](shifty::ShiftyProcessor& p) { motionOn(p); set(p, "motionSync", 0.0f); }, {} },
        { "12-motion-env-talk", EngineMode::talk, false, Page::motion, Tab::envelope, 1.0,
          [set](shifty::ShiftyProcessor& p) { set(p, "envAmount", 0.6f); }, {} },
        { "13-motion-env-mouth", EngineMode::mouth, false, Page::motion, Tab::envelope, 1.0,
          [set](shifty::ShiftyProcessor& p) { set(p, "envAmount", -0.4f); }, {} },
        { "14-motion-yoi-talk", EngineMode::talk, false, Page::motion, Tab::yoi, 1.0,
          [set](shifty::ShiftyProcessor& p) { set(p, "yoiAmount", 1.0f); set(p, "yoiFrom", 0.0f); set(p, "yoiTo", 2.0f); set(p, "yoiTime", 300.0f); }, {} },
        { "15-motion-yoi-original", EngineMode::original, false, Page::motion, Tab::yoi, 1.0, loadOriginal, {} },
        { "16-presets-factory", EngineMode::talk, false, Page::presets, Tab::lfo, 1.0, {}, {} },
        { "17-presets-user-folders", EngineMode::talk, false, Page::presets, Tab::lfo, 1.0, {},
          [](shifty::ShiftyEditor& e) { e.selectPresetFilterForTesting(1, {}); } },
        { "18-presets-classic-folder", EngineMode::talk, false, Page::presets, Tab::lfo, 1.0, {},
          [](shifty::ShiftyEditor& e) { e.selectPresetFilterForTesting(0, "Classic"); } },
        { "19-presets-save", EngineMode::talk, false, Page::presets, Tab::lfo, 1.0, {},
          [](shifty::ShiftyEditor& e) { e.beginPresetSaveForTesting(); } },
        { "20-size-90", EngineMode::talk, false, Page::sound, Tab::lfo, 0.9, {}, {} },
        { "21-size-150", EngineMode::talk, false, Page::voice, Tab::lfo, 1.5, {}, {} },
        // The shell's glow: low formants light the bottom of the shell, high ones the top; bypass is dark.
        { "22-glow-shift-down", EngineMode::talk, false, Page::sound, Tab::lfo, 1.0,
          [set](shifty::ShiftyProcessor& p) { set(p, "shift", -12.0f); set(p, "vowelMorph", 0.0f); }, {} },
        { "23-glow-shift-up", EngineMode::talk, false, Page::sound, Tab::lfo, 1.0,
          [set](shifty::ShiftyProcessor& p) { set(p, "shift", 12.0f); set(p, "vowelMorph", 4.0f); }, {} },
        { "24-glow-bypassed", EngineMode::talk, false, Page::sound, Tab::lfo, 1.0,
          [set](shifty::ShiftyProcessor& p) { set(p, "bypass", 1.0f); }, {} },
    };
    bool ok = classicOriginal >= 0;
    for (const auto& shot : shots) {
        shifty::ShiftyProcessor processor;
        processor.setUserDataRootForTesting(root);
        if (shot.mode != EngineMode::original) processor.setEngine(shot.mode, shot.lowLatency);
        if (shot.setupProcessor) shot.setupProcessor(processor);
        playGatedSaw(processor);
        shifty::ShiftyEditor editor(processor);
        const auto size = shifty::ShiftyEditor::sizeFor(shot.scale);
        editor.setSize(size.x, size.y);
        editor.showPageForTesting(shot.page, shot.tab);
        if (shot.setupEditor) shot.setupEditor(editor);
        // Half a second of display updates, so the meters and the glow settle as they would on screen.
        for (int frame = 0; frame < 15; ++frame) editor.refreshForTesting();
        const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 2.0f);
        const auto file = directory.getChildFile(juce::String(shot.name) + ".png");
        const bool written = image.isValid() && writePng(image, file);
        std::cout << (written ? "wrote " : "FAILED ") << file.getFullPathName() << " (" << image.getWidth()
                  << " x " << image.getHeight() << ")\n";
        ok = ok && written;
        processor.releaseResources();
    }
    root.deleteRecursively();
    return ok ? 0 : 1;
}
}

int main(int argc, char* argv[]) {
    if (argc == 2 && juce::String::fromUTF8(argv[1]) == "--feature-tests") {
        juce::ScopedJuceInitialiser_GUI gui;
        const int original = shifty::tests::runFeatureTests();
        const int redesign = shifty::tests::runRedesignTests();
        return original == 0 && redesign == 0 ? 0 : 1;
    }

    if (argc == 3 && juce::String::fromUTF8(argv[1]) == "--host-latency")
        return checkVst3HostLatency(juce::String::fromUTF8(argv[2]));

    if (argc == 4 && juce::String::fromUTF8(argv[1]) == "--compat") {
        juce::ScopedJuceInitialiser_GUI gui;
        return shifty::compat::runCompatibility(juce::String::fromUTF8(argv[2]), juce::String::fromUTF8(argv[3]));
    }

#if JUCE_MAC
    if (argc == 3 && juce::String::fromUTF8(argv[1]) == "--au")
        return checkAudioUnitInProcess(juce::String::fromUTF8(argv[2]));
#endif

    if (argc == 3 && juce::String::fromUTF8(argv[1]) == "--gallery") {
        const auto path = juce::String::fromUTF8(argv[2]);
        if (!juce::File::isAbsolutePath(path)) { std::cerr << "Gallery directory must be absolute.\n"; return 2; }
        return renderGallery(juce::File(path));
    }

    if (argc == 2 && juce::String::fromUTF8(argv[1]) == "--menu-placement") {
        juce::ScopedJuceInitialiser_GUI gui;
        int failures = 0;
        {
            MenuPlacementCheck check;
            juce::MessageManager::getInstance()->runDispatchLoop();
            failures = check.failures;
        }
        std::cout << (failures == 0 ? "Every dropdown opens at its control.\n" : "Some menus open away from their control.\n");
        return failures == 0 ? 0 : 1;
    }

    if (argc == 2 && juce::String::fromUTF8(argv[1]) == "--preset-audit") {
        juce::ScopedJuceInitialiser_GUI gui;
        return shifty::compat::runPresetAudit();
    }

    const bool motionOnSnapshot = argc == 3 && juce::String::fromUTF8(argv[1]) == "--motion-on";
    const bool vowelOnSnapshot = argc == 3 && juce::String::fromUTF8(argv[1]) == "--vowel-on";
    const bool infoSnapshot = argc == 3 && juce::String::fromUTF8(argv[1]) == "--info";
    if (argc != 2 && !motionOnSnapshot && !vowelOnSnapshot && !infoSnapshot) {
        std::cerr << "Usage: ShiftyEditorSnapshot /absolute/path/to/snapshot.png\n"
                  << "   or: ShiftyEditorSnapshot --feature-tests\n"
                  << "   or: ShiftyEditorSnapshot --motion-on /absolute/path/to/snapshot.png\n"
                  << "   or: ShiftyEditorSnapshot --vowel-on /absolute/path/to/snapshot.png\n"
                  << "   or: ShiftyEditorSnapshot --info /absolute/path/to/snapshot.png\n"
                  << "   or: ShiftyEditorSnapshot --host-latency /absolute/path/to/SHIFTY.vst3\n";
        return 2;
    }

    const juce::String outputPath = juce::String::fromUTF8(argv[(motionOnSnapshot || vowelOnSnapshot || infoSnapshot) ? 2 : 1]);
    if (!juce::File::isAbsolutePath(outputPath)) {
        std::cerr << "Output path must be absolute.\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI gui;
    shifty::ShiftyProcessor processor;
    // The editor lists user presets: point it at a scratch folder, never the real one.
    const auto scratchRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("shifty-snapshot-" + juce::String(juce::Time::currentTimeMillis()));
    processor.setUserDataRootForTesting(scratchRoot);
    if (processor.getLatencySamples() != 0 || processor.engineMode() != shifty::EngineMode::talk) {
        std::cerr << "A new instance should open on TALK with no latency.\n";
        return 1;
    }
    processor.setEngine(shifty::EngineMode::classic, false);
    processor.prepareToPlay(96000.0, 512);
    if (processor.getLatencySamples() != 4096) {
        std::cerr << "Unexpected CLASSIC latency at 96 kHz.\n";
        return 1;
    }
    processor.prepareToPlay(48000.0, 512);
    if (processor.getLatencySamples() != 2048) {
        std::cerr << "Unexpected CLASSIC latency at 48 kHz.\n";
        return 1;
    }
    processor.undo();
    if (processor.engineMode() != shifty::EngineMode::talk || processor.getLatencySamples() != 0) {
        std::cerr << "Undo did not restore TALK and its latency.\n";
        return 1;
    }
    if (motionOnSnapshot) {
        processor.parameters().getParameter("motionEnable")->setValueNotifyingHost(1.0f);
        processor.setParameterLocked("shift", true);
        processor.setParameterLocked("mix", true);
    }
    if (vowelOnSnapshot)
        processor.parameters().getParameter("vowel")->setValueNotifyingHost(1.0f);
    shifty::ShiftyEditor editor(processor);
    if (!checkInfoPage(processor, editor)) {
        std::cerr << "INFO page interaction checks failed.\n";
        return 1;
    }
    if (infoSnapshot) dynamic_cast<juce::Button*>(findComponent(editor, "INFO"))->onClick();
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true);
    if (!image.isValid() || !image.hasAlphaChannel()) {
        std::cerr << "Editor snapshot was not a valid ARGB image.\n";
        return 1;
    }

    const int lastX = image.getWidth() - 1;
    const int lastY = image.getHeight() - 1;
    // The device fills the window edge to edge: every edge pixel is painted, corners included.
    bool filledEdges = true;
    for (int x = 0; x <= lastX; ++x)
        filledEdges = filledEdges && image.getPixelAt(x, 0).getAlpha() == 255 && image.getPixelAt(x, lastY).getAlpha() == 255;
    for (int y = 0; y <= lastY; ++y)
        filledEdges = filledEdges && image.getPixelAt(0, y).getAlpha() == 255 && image.getPixelAt(lastX, y).getAlpha() == 255;
    const bool visibleDevice =
        image.getPixelAt(image.getWidth() / 2, image.getHeight() / 2).getAlpha() > 0;

    const juce::File output(outputPath);
    if (output.getParentDirectory().createDirectory().failed()) {
        std::cerr << "Could not create the output directory.\n";
        return 1;
    }
    juce::FileOutputStream stream(output);
    if (!stream.openedOk() || !stream.setPosition(0) || stream.truncate().failed() ||
        !juce::PNGImageFormat().writeImageToStream(image, stream)) {
        std::cerr << "Could not write the PNG snapshot.\n";
        return 1;
    }
    stream.flush();
    if (stream.getStatus().failed()) {
        std::cerr << "Could not flush the PNG snapshot.\n";
        return 1;
    }

    std::cout << output.getFullPathName().toStdString() << " ("
              << image.getWidth() << " x " << image.getHeight() << ")\n";
    if ((!infoSnapshot && !filledEdges) || !visibleDevice) {
        std::cerr << "Edge check failed: edges="
                  << (filledEdges ? "filled" : "partly clear")
                  << ", center=" << (visibleDevice ? "visible" : "clear") << "\n";
        return 1;
    }
    std::cout << (infoSnapshot ? "Info page snapshot check passed.\n" : "Edge-to-edge shell check passed.\n");
    return 0;
}
