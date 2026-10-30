// A plain VST3 host for the 0.10.2 -> 0.11 project compatibility check. It
// contains no BACK-SCRATCH code, so the old payload plug-in and the new build
// each run against nothing but their own code.
//   BackScratchCompatHost --save  <0.10.2 BACK-SCRATCH.vst3> <folder>
//   BackScratchCompatHost --check <new BACK-SCRATCH.vst3>    <folder>
//   BackScratchCompatHost --au <new BACK-SCRATCH.component> <new BACK-SCRATCH.vst3>
//   BackScratchCompatHost --bypass-save  <0.11.2 BACK-SCRATCH.vst3> <folder>
//   BackScratchCompatHost --bypass-check <new BACK-SCRATCH.vst3>    <folder>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
#include <cstring>

namespace backScratch::processorTests {
struct Head final : juce::AudioPlayHead {
    bool playing = true;
    double beat = 0, bpm = 140;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setIsPlaying(playing); p.setBpm(bpm); p.setPpqPosition(beat);
        p.setTimeSignature(TimeSignature{4, 4}); return p;
    }
};
inline float testSound(int frame, int channel = 0) {
    const double t = frame / 48000.0;
    return static_cast<float>(0.24 * std::sin(t * 6.283185307179586 * 49)
        + 0.18 * std::sin(t * 6.283185307179586 * (channel == 0 ? 441 : 667))
        * (0.65 + 0.35 * std::sin(t * 23)));
}
}

// The edited-pattern XML writer, as used by the plug-in (format 1).
namespace backScratch {
struct ScratchSegment { double start, end, delayStart, delayEnd; float gateStart, gateEnd, wet; bool curved; double curveStart = 0.0, curveEnd = 1.0; };
struct ScratchPattern { double beats = 1.0; ScratchSegment segments[4]; int count = 0; double editingRange = 0.0; };
inline std::unique_ptr<juce::XmlElement> editedPatternToXml(const ScratchPattern& pattern)
{
    auto xml = std::make_unique<juce::XmlElement>("EDITED_PATTERN");
    xml->setAttribute("format", 1);
    xml->setAttribute("beats", pattern.beats);
    xml->setAttribute("range", pattern.editingRange);
    for (int i = 0; i < pattern.count; ++i) {
        const auto& s = pattern.segments[i];
        auto* node = xml->createNewChildElement("SEGMENT");
        node->setAttribute("start", s.start); node->setAttribute("end", s.end);
        node->setAttribute("delayStart", s.delayStart); node->setAttribute("delayEnd", s.delayEnd);
        node->setAttribute("gateStart", static_cast<double>(s.gateStart)); node->setAttribute("gateEnd", static_cast<double>(s.gateEnd));
        node->setAttribute("wet", static_cast<double>(s.wet)); node->setAttribute("curved", s.curved);
        node->setAttribute("curveStart", s.curveStart); node->setAttribute("curveEnd", s.curveEnd);
    }
    return xml;
}
}

#define BACK_SCRATCH_COMPAT_HOST 1
#include "CompatibilityTests.h"

#if JUCE_MAC
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

// The Audio Unit from the build folder, checked without installing it: the
// .component is registered with this process only (AudioComponentRegister),
// hosted through JUCE's AU format and compared with the VST3 of the same build.
namespace backScratch::audioUnitCheck {
using compatibilityTests::require;

inline juce::String text(CFStringRef value) { return value != nullptr ? juce::String::fromCFString(value) : juce::String(); }

inline OSType code(CFDictionaryRef entry, CFStringRef key)
{
    const auto value = text(static_cast<CFStringRef>(CFDictionaryGetValue(entry, key)));
    require(value.length() == 4, "AU Info.plist has a four-character " + text(key).toStdString());
    return static_cast<OSType>((value[0] << 24) | (value[1] << 16) | (value[2] << 8) | value[3]);
}

inline std::unique_ptr<juce::AudioPluginInstance> createUnit(juce::AudioUnitPluginFormat& format, const juce::PluginDescription& type)
{
    juce::String error;
    auto unit = format.createInstanceFromDescription(type, 48000.0, 512, error);
    require(unit != nullptr, "the AU instantiates: " + error.toStdString());
    return unit;
}

inline std::vector<float> play(juce::AudioPluginInstance& plugin, int blocks)
{
    processorTests::Head head;
    plugin.setPlayHead(&head);
    plugin.prepareToPlay(48000.0, 512);
    std::vector<float> result;
    juce::MidiBuffer midi;
    for (int block = 0; block < blocks; ++block) {
        juce::AudioBuffer<float> audio(2, 512);
        for (int i = 0; i < 512; ++i) for (int ch = 0; ch < 2; ++ch) audio.setSample(ch, i, processorTests::testSound(block * 512 + i, ch));
        head.beat = block * 512.0 * head.bpm / (60.0 * 48000.0);
        plugin.processBlock(audio, midi);
        for (int i = 0; i < 512; ++i) for (int ch = 0; ch < 2; ++ch) result.push_back(audio.getSample(ch, i));
    }
    plugin.releaseResources();
    plugin.setPlayHead(nullptr);
    return result;
}

inline double largestDifference(const std::vector<float>& a, const std::vector<float>& b)
{
    require(a.size() == b.size(), "renders have the same length");
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i) worst = std::max(worst, static_cast<double>(std::abs(a[i] - b[i])));
    return worst;
}

inline void run(const juce::String& componentPath, const juce::String& vst3Path)
{
    // Identity straight from the bundle's Info.plist.
    const juce::File component(componentPath);
    auto url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(component.getFullPathName().toRawUTF8()),
                                                       static_cast<CFIndex>(strlen(component.getFullPathName().toRawUTF8())), true);
    auto bundle = CFBundleCreate(nullptr, url);
    CFRelease(url);
    require(bundle != nullptr, "the .component bundle opens");
    require(text(CFBundleGetIdentifier(bundle)) == "com.yoiboi.backscratch", "AU bundle ID is com.yoiboi.backscratch");
    auto components = static_cast<CFArrayRef>(CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("AudioComponents")));
    require(components != nullptr && CFArrayGetCount(components) == 1, "the AU declares one AudioComponents entry");
    auto entry = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(components, 0));
    AudioComponentDescription description {};
    description.componentType = code(entry, CFSTR("type"));
    description.componentSubType = code(entry, CFSTR("subtype"));
    description.componentManufacturer = code(entry, CFSTR("manufacturer"));
    require(description.componentType == 'aufx' && description.componentSubType == 'Bsc1' && description.componentManufacturer == 'Yoib',
            "AU identity is aufx / Bsc1 / Yoib");
    SInt32 version = 0;
    CFNumberGetValue(static_cast<CFNumberRef>(CFDictionaryGetValue(entry, CFSTR("version"))), kCFNumberSInt32Type, &version);
    const auto factoryName = text(static_cast<CFStringRef>(CFDictionaryGetValue(entry, CFSTR("factoryFunction"))));
    auto factory = reinterpret_cast<AudioComponentFactoryFunction>(CFBundleGetFunctionPointerForName(bundle, factoryName.toCFString()));
    require(factory != nullptr, "the AU binary loads and exports " + factoryName.toStdString());
    auto name = static_cast<CFStringRef>(CFDictionaryGetValue(entry, CFSTR("name")));
    require(AudioComponentRegister(&description, name, static_cast<UInt32>(version), factory) != nullptr,
            "the AU registers with this process");
    std::cout << "AU bundle " << component.getFileName() << ": " << text(name) << ", aufx/Bsc1/Yoib, version 0x"
              << juce::String::toHexString(version) << ", registered in-process only\n";

    juce::AudioUnitPluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types, "AudioUnit:Effects/aufx,Bsc1,Yoib");
    require(types.size() == 1, "JUCE's AU host finds the registered unit");
    require(types[0]->name == "BACK-SCRATCH" && types[0]->manufacturerName == "VIVID FEVER DREAMS", "AU name and manufacturer");
    auto unit = createUnit(format, *types[0]);
    auto vst3 = compatibilityTests::load(vst3Path);

    // Same controls, in the same order, with the same latency.
    juce::StringArray unitNames, vstNames;
    for (auto* parameter : unit->getParameters()) unitNames.add(parameter->getName(128));
    for (auto* parameter : vst3->getParameters()) vstNames.add(parameter->getName(128));
    // The VST3 wrapper adds a Program entry (and, before 0.11.3, its own host
    // Bypass), and the AU host lists controls by their hashed IDs, so compare the sets.
    vstNames.removeString("Program");
    vstNames.removeDuplicates(false);
    unitNames.sort(false);
    vstNames.sort(false);
    if (unitNames != vstNames)
        std::cerr << "AU controls: " << unitNames.joinIntoString(", ") << "\nVST3 controls: " << vstNames.joinIntoString(", ") << '\n';
    require(unitNames == vstNames, "the AU publishes the same controls as the VST3");
    std::cout << "AU and VST3 publish the same " << unitNames.size() << " plug-in controls\n";
    unit->prepareToPlay(48000.0, 512);
    vst3->prepareToPlay(48000.0, 512);
    require(unit->getLatencySamples() == 730 && vst3->getLatencySamples() == 730, "AU and VST3 both report 730 samples of latency");
    unit->releaseResources();
    vst3->releaseResources();

    // Default sound: the AU renders what the VST3 renders.
    const auto unitAudio = play(*unit, 240);
    const auto vstAudio = play(*vst3, 240);
    const auto formatDifference = largestDifference(unitAudio, vstAudio);
    std::cout << "AU vs VST3 default render: maximum difference " << formatDifference << " over " << unitAudio.size() / 2 << " stereo frames\n";
    require(formatDifference < 0.00003, "the AU renders the same audio as the VST3");

    // Settings saved by one AU instance reopen in another.
    compatibilityTests::set(*unit, "Scratch Pattern", 37.0f / 74.0f);
    compatibilityTests::set(*unit, "Phrase Timing", 0.5f);
    compatibilityTests::set(*unit, "Mix", 0.7f);
    compatibilityTests::set(*unit, "Depth", 0.7f);
    compatibilityTests::set(*unit, "Keep Sub Split", 1.0f / 3.0f);
    const auto changed = play(*unit, 240);
    require(largestDifference(changed, unitAudio) > 0.05, "AU parameter changes are heard");
    juce::MemoryBlock state;
    unit->getStateInformation(state);
    auto reopened = createUnit(format, *types[0]);
    reopened->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (const auto& [parameterName, value] : compatibilityTests::configured())
        require(std::abs(compatibilityTests::parameter(*reopened, parameterName).getValue() - value) < 1.0e-4f,
                "a reopened AU keeps " + parameterName.toStdString());
    const auto recalled = largestDifference(play(*reopened, 240), changed);
    std::cout << "AU state reopened in a new instance: maximum difference " << recalled << '\n';
    require(recalled < 0.00003, "a reopened AU sounds the same");
    reopened.reset();
    unit.reset();
    vst3.reset();
    CFRelease(bundle);
    std::cout << "BACK-SCRATCH AU in-process check passed\n";
}
} // namespace backScratch::audioUnitCheck
#endif

// 0.11.3: the plug-in's Bypass became the host's bypass. Step 1 (--bypass-save) lets the 0.11.2 VST3 save
// projects with its Bypass off and on, and with the JUCE wrapper's separate host Bypass on, and records
// what each renders. Step 2 (--bypass-check), in a second process, opens them in the new VST3: the
// controls and audio must come back, and a fresh new instance given the same settings must write the
// same plug-in state bytes. The JUCE private data the wrapper appends after those bytes is reported.
namespace backScratch::bypassCheck {
using compatibilityTests::require;

struct Scenario { const char* file; const char* name; float bypass; bool hostBypass; };
constexpr std::array<Scenario, 3> scenarios {{
    {"bypass-off", "Bypass off", 0.0f, false},
    {"bypass-on", "Bypass on", 1.0f, false},
    {"host-bypass-on", "0.11.2 wrapper host Bypass on", 0.0f, true}}};

// The plug-in's own Bypass: the sixth control in both builds (0.11.2 also has a wrapper "Bypass" at the end).
inline juce::AudioProcessorParameter& ownBypass(juce::AudioPluginInstance& plugin)
{
    auto* parameter = plugin.getParameters()[5];
    require(parameter != nullptr && parameter->getName(128) == "Bypass", "the sixth control is the plug-in's Bypass");
    return *parameter;
}

inline void configure(juce::AudioPluginInstance& plugin, const Scenario& scenario)
{
    compatibilityTests::configured().clear();
    compatibilityTests::set(plugin, "Scratch Pattern", 37.0f / 74.0f);
    compatibilityTests::set(plugin, "Phrase Timing", 0.5f);
    compatibilityTests::set(plugin, "Pattern Grid Shift", 5.0f / 15.0f);
    compatibilityTests::set(plugin, "Mix", 0.7f);
    auto& bypass = ownBypass(plugin);
    bypass.beginChangeGesture(); bypass.setValueNotifyingHost(scenario.bypass); bypass.endChangeGesture();
    if (scenario.hostBypass) {
        auto* host = plugin.getBypassParameter();
        require(host != nullptr && host != &bypass, "0.11.2 has a separate wrapper host Bypass");
        host->setValueNotifyingHost(1.0f);
    }
}

// The IComponent chunk of a hosted VST3 state: the plug-in's getStateInformation bytes, then the wrapper's
// trailer (int64 0, private data, int64 private-data size, "JUCEPrivateData").
inline std::pair<juce::MemoryBlock, juce::MemoryBlock> split(const juce::MemoryBlock& state)
{
    auto container = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    require(container != nullptr, "the VST3 state container decodes");
    juce::MemoryBlock component;
    for (auto* node : container->getChildIterator())
        if (node->getTagName() == "IComponent") component.fromBase64Encoding(node->getAllSubText());
    const juce::String id("JUCEPrivateData");
    const auto idSize = static_cast<size_t>(id.length());
    const auto* bytes = static_cast<const char*>(component.getData());
    require(component.getSize() > idSize + 16 && std::memcmp(bytes + component.getSize() - idSize, id.toRawUTF8(), idSize) == 0,
            "the IComponent chunk ends with JUCE's private data");
    juce::int64 privateSize = 0;
    std::memcpy(&privateSize, bytes + component.getSize() - idSize - 8, 8);
    const auto ownSize = component.getSize() - idSize - 16 - static_cast<size_t>(privateSize);
    return {juce::MemoryBlock(bytes, ownSize), juce::MemoryBlock(bytes + ownSize, component.getSize() - ownSize)};
}

inline juce::String describe(juce::AudioPluginInstance& plugin)
{
    auto* host = plugin.getBypassParameter();
    juce::String text = juce::String(plugin.getParameters().size()) + " controls; host bypass = ";
    if (host == nullptr) return text + "none";
    text << "'" << host->getName(128) << "' at index " << host->getParameterIndex();
    if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*>(host)) text << ", VST3 ID " << hosted->getParameterID();
    return text;
}

inline void save(const juce::String& oldPath, const juce::File& directory)
{
    require(directory.createDirectory().wasOk(), "bypass folder");
    for (const auto& scenario : scenarios) {
        auto old = compatibilityTests::load(oldPath);
        configure(*old, scenario);
        juce::MemoryBlock saved;
        old->getStateInformation(saved);
        const auto [own, trailer] = split(saved);
        require(directory.getChildFile(juce::String(scenario.file) + ".state").replaceWithData(saved.getData(), saved.getSize())
                    && directory.getChildFile(juce::String(scenario.file) + ".own").replaceWithData(own.getData(), own.getSize()),
                "0.11.2 bypass state saved");
        compatibilityTests::writeAudio(directory.getChildFile(juce::String(scenario.file) + ".f32"),
                                       compatibilityTests::render(*old, saved, 120));
        std::cout << "0.11.2 " << scenario.name << ": " << describe(*old) << "; own state " << own.getSize()
                  << " bytes, wrapper trailer " << trailer.getSize() << " bytes\n";
    }
}

inline void check(const juce::String& newPath, const juce::File& directory)
{
    for (const auto& scenario : scenarios) {
        juce::MemoryBlock saved, oldOwn;
        require(directory.getChildFile(juce::String(scenario.file) + ".state").loadFileAsData(saved)
                    && directory.getChildFile(juce::String(scenario.file) + ".own").loadFileAsData(oldOwn),
                "0.11.2 bypass state found");
        // Same settings in a fresh new instance: the plug-in's own state bytes are unchanged.
        auto fresh = compatibilityTests::load(newPath);
        if (!scenario.hostBypass) {
            configure(*fresh, scenario);
            juce::MemoryBlock state;
            fresh->getStateInformation(state);
            const auto [own, trailer] = split(state);
            require(own == oldOwn, std::string(scenario.name) + ": the same settings write the same plug-in state bytes");
            require(&ownBypass(*fresh) == fresh->getBypassParameter(), "the host's bypass is the plug-in's Bypass");
            std::cout << "0.11.3 " << scenario.name << ": " << describe(*fresh) << "; own state bytes identical to 0.11.2 ("
                      << own.getSize() << "), wrapper trailer " << trailer.getSize() << " bytes\n";
        }
        // The 0.11.2 project opens in a fresh new instance.
        auto current = compatibilityTests::load(newPath);
        const auto before = compatibilityTests::readAudio(directory.getChildFile(juce::String(scenario.file) + ".f32"));
        const auto after = compatibilityTests::render(*current, saved, 120);
        require(before.size() == after.size(), "both builds render the same length");
        double worst = 0.0;
        for (size_t i = 0; i < before.size(); ++i) worst = std::max(worst, static_cast<double>(std::abs(before[i] - after[i])));
        const float bypass = ownBypass(*current).getValue();
        std::cout << "0.11.2 " << scenario.name << " -> 0.11.3: Bypass " << bypass << ", maximum difference " << worst << '\n';
        if (scenario.hostBypass) continue; // reported, not required: see README "Updating"
        for (const auto& [name, value] : compatibilityTests::configured())
            require(std::abs(compatibilityTests::parameter(*current, name).getValue() - value) < 1.0e-6f,
                    std::string(scenario.name) + ": restores " + name.toStdString());
        require(juce::approximatelyEqual(bypass, scenario.bypass), std::string(scenario.name) + ": restores Bypass");
        require(worst < 0.00003, std::string(scenario.name) + ": a 0.11.2 project renders the same in 0.11.3");
        juce::MemoryBlock resaved;
        current->getStateInformation(resaved);
        std::cout << "  resaved own state " << (split(resaved).first == oldOwn ? "identical to" : "differs from")
                  << " the 0.11.2 bytes\n";
    }
    std::cout << "BACK-SCRATCH 0.11.2 bypass state compatibility passed\n";
}
} // namespace backScratch::bypassCheck

int main(int argc, char* argv[])
{
    try {
        juce::ScopedJuceInitialiser_GUI gui;
        if (argc == 4 && juce::String(argv[1]) == "--bypass-save") {
            backScratch::bypassCheck::save(juce::String::fromUTF8(argv[2]), juce::File(juce::String::fromUTF8(argv[3])));
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--bypass-check") {
            backScratch::bypassCheck::check(juce::String::fromUTF8(argv[2]), juce::File(juce::String::fromUTF8(argv[3])));
            return 0;
        }
#if JUCE_MAC
        if (argc == 4 && juce::String(argv[1]) == "--au") {
            backScratch::audioUnitCheck::run(juce::String::fromUTF8(argv[2]), juce::String::fromUTF8(argv[3]));
            return 0;
        }
#endif
        if (argc == 4 && juce::String(argv[1]) == "--save") {
            backScratch::compatibilityTests::saveWithPayload(juce::String::fromUTF8(argv[2]), juce::File(juce::String::fromUTF8(argv[3])));
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--check") {
            backScratch::compatibilityTests::checkAgainstPayload(juce::String::fromUTF8(argv[2]), juce::File(juce::String::fromUTF8(argv[3])));
            return 0;
        }
        std::cerr << "usage: --save <0.10.2 vst3> <folder> | --check <new vst3> <folder> | --au <new component> <new vst3>"
                     " | --bypass-save <0.11.2 vst3> <folder> | --bypass-check <new vst3> <folder>\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
