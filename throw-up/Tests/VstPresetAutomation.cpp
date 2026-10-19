// macOS regression host for the actual packaged VST3. The matching JUCE wrapper
// supplies the concrete controller type solely to reach its public
// getPluginInstance() test hook; all tested objects come from the loaded bundle.
// Build as Objective-C++ with the same plugin definitions and JUCE version.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp>
#undef Component
#include "ThrowUpProcessor.h"
#include "Presets.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include <set>
#include <cmath>

namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

template <typename Interface>
Interface* query(FUnknown* from)
{
    Interface* result = nullptr;
    require(from != nullptr && from->queryInterface(Interface::iid, reinterpret_cast<void**>(&result)) == kResultOk
            && result != nullptr, "VST3 interface available");
    return result;
}

class Host final : public IHostApplication, public IComponentHandler, public IComponentHandler2 {
public:
    tresult PLUGIN_API queryInterface(const TUID id, void** object) override
    {
        *object = nullptr;
        if (juce::doUIDsMatch(FUnknown::iid, id) || juce::doUIDsMatch(IHostApplication::iid, id)) *object = static_cast<IHostApplication*>(this);
        else if (juce::doUIDsMatch(IComponentHandler::iid, id)) *object = static_cast<IComponentHandler*>(this);
        else if (juce::doUIDsMatch(IComponentHandler2::iid, id)) *object = static_cast<IComponentHandler2*>(this);
        if (*object == nullptr) return kNoInterface;
        addRef(); return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++references; }
    uint32 PLUGIN_API release() override { return --references; }
    tresult PLUGIN_API getName(String128 name) override
    {
        juce::String("THROW-UP regression host").copyToUTF16(reinterpret_cast<juce::CharPointer_UTF16::CharType*>(name), sizeof(String128));
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID, TUID, void** object) override { *object = nullptr; return kNotImplemented; }
    tresult PLUGIN_API beginEdit(ParamID id) override { begin.push_back(id); return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue) override { edits.push_back(id); return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID id) override { end.push_back(id); return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override { restartFlags |= flags; return kResultOk; }
    tresult PLUGIN_API setDirty(TBool state) override { if (state) ++dirty; return kResultOk; }
    tresult PLUGIN_API requestOpenEditor(FIDString) override { return kNotImplemented; }
    tresult PLUGIN_API startGroupEdit() override { return kResultOk; }
    tresult PLUGIN_API finishGroupEdit() override { return kResultOk; }
    void clear() { begin.clear(); edits.clear(); end.clear(); restartFlags = 0; dirty = 0; }
    uint32 references = 1;
    std::vector<ParamID> begin, edits, end;
    int32 restartFlags = 0;
    int dirty = 0;
};

class Automation final : public IParameterChanges, public IParamValueQueue {
public:
    tresult PLUGIN_API queryInterface(const TUID id, void** object) override
    {
        *object = nullptr;
        if (juce::doUIDsMatch(FUnknown::iid, id) || juce::doUIDsMatch(IParameterChanges::iid, id)) *object = static_cast<IParameterChanges*>(this);
        else if (juce::doUIDsMatch(IParamValueQueue::iid, id)) *object = static_cast<IParamValueQueue*>(this);
        if (*object == nullptr) return kNoInterface;
        addRef(); return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++references; }
    uint32 PLUGIN_API release() override { return --references; }
    int32 PLUGIN_API getParameterCount() override { return 1; }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override { return index == 0 ? this : nullptr; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override { return nullptr; }
    ParamID PLUGIN_API getParameterId() override { return parameter; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32 index, int32& offset, ParamValue& out) override
    { if (index != 0) return kInvalidArgument; offset = 0; out = value; return kResultOk; }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kNotImplemented; }
    uint32 references = 1;
    ParamID parameter = 0;
    ParamValue value = 0;
};

struct Bundle {
    explicit Bundle(const juce::File& file)
    {
        auto path = file.getFullPathName().toRawUTF8();
        auto url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path),
                                                          static_cast<CFIndex>(std::strlen(path)), true);
        bundle = CFBundleCreate(nullptr, url); CFRelease(url);
        require(bundle != nullptr && CFBundleLoadExecutable(bundle), "packaged VST3 bundle loads");
        const auto entry = reinterpret_cast<bool (*)(CFBundleRef)>(CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleEntry")));
        exit = reinterpret_cast<bool (*)()>(CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleExit")));
        const auto getFactory = reinterpret_cast<IPluginFactory* (*)()>(CFBundleGetFunctionPointerForName(bundle, CFSTR("GetPluginFactory")));
        require(entry != nullptr && exit != nullptr && getFactory != nullptr && entry(bundle), "VST3 bundle entry succeeds");
        factory = getFactory(); require(factory != nullptr, "VST3 factory available");
    }
    ~Bundle() { if (factory) factory->release(); if (exit) exit(); if (bundle) CFRelease(bundle); }
    CFBundleRef bundle = nullptr;
    IPluginFactory* factory = nullptr;
    bool (*exit)() = nullptr;
};

juce::Component* named(juce::Component& parent, const juce::String& name)
{
    if (parent.getName() == name) return &parent;
    for (auto* child : parent.getChildren()) if (auto* found = named(*child, name)) return found;
    return nullptr;
}

void checkNotifications(const Host& host, const std::set<ParamID>& soundIds)
{
    if (host.restartFlags != 0) std::cerr << "VST3 restart flags: " << host.restartFlags << '\n';
    require((host.restartFlags & (kParamValuesChanged | kParamTitlesChanged | kReloadComponent | kParamIDMappingChanged)) == 0,
            "preset selection must not invalidate host parameter/automation caches");
    require(host.begin == host.edits && host.edits == host.end, "VST3 shaping parameter edits have balanced gestures");
    for (const auto id : host.edits)
        require(soundIds.count(id) != 0, "preset selection may edit shaping controls only, never THROW or synthetic Program");
}

void test(Bundle& bundle, Host& host)
{
    IComponent* component = nullptr;
    for (int32 i = 0; i < bundle.factory->countClasses(); ++i) {
        PClassInfo info {}; bundle.factory->getClassInfo(i, &info);
        if (std::strcmp(info.category, kVstAudioEffectClass) == 0)
            require(bundle.factory->createInstance(info.cid, IComponent::iid, reinterpret_cast<void**>(&component)) == kResultOk,
                    "packaged VST3 component created");
    }
    require(component != nullptr, "one audio component found");
    TUID controllerId {};
    require(component->initialize(static_cast<IHostApplication*>(&host)) == kResultOk
            && component->getControllerClassId(controllerId) == kResultOk, "VST3 component initialized");
    IEditController* controller = nullptr;
    require(bundle.factory->createInstance(controllerId, IEditController::iid, reinterpret_cast<void**>(&controller)) == kResultOk,
            "packaged VST3 controller created");
    controller->initialize(static_cast<IHostApplication*>(&host));
    controller->setComponentHandler(&host);
    auto* componentConnection = query<IConnectionPoint>(component);
    auto* controllerConnection = query<IConnectionPoint>(controller);
    require(componentConnection->connect(controllerConnection) == kResultOk
            && controllerConnection->connect(componentConnection) == kResultOk, "component/controller connected");
    auto* concrete = query<juce::JuceVST3EditController>(controller);
    auto* actual = concrete->getPluginInstance();
    require(actual != nullptr && actual->getName() == "THROW-UP", "matching JUCE wrapper exposes loaded processor for UI test");
    auto* processor = query<IAudioProcessor>(component);
    struct Cleanup {
        IComponent* component; IEditController* controller; IConnectionPoint* a; IConnectionPoint* b;
        juce::JuceVST3EditController* concrete; IAudioProcessor* processor;
        ~Cleanup() {
            processor->setProcessing(false); component->setActive(false);
            a->disconnect(b); b->disconnect(a); a->release(); b->release();
            controller->setComponentHandler(nullptr); controller->terminate(); component->terminate();
            concrete->release(); processor->release(); controller->release(); component->release();
        }
    } cleanup {component, controller, componentConnection, controllerConnection, concrete, processor};

    std::vector<ParamID> ids;
    ParamID programId = 0;
    int programCount = 0;
    for (int32 i = 0; i < controller->getParameterCount(); ++i) {
        ParameterInfo info {}; controller->getParameterInfo(i, info);
        if ((info.flags & ParameterInfo::kIsProgramChange) != 0) { programId = info.id; ++programCount; }
        else ids.push_back(info.id);
    }
    require(ids.size() == static_cast<size_t>(ThrowUpProcessor::parameterCount) && programCount == 1,
            "34 controls (the 13 of 0.1.5 first, then 21 appended) plus the VST3 Program parameter, and no MIDI-CC proxies");
    const auto hashed = [](const char* id) { return static_cast<ParamID>(juce::String(id).hashCode() & 0x7fffffff); };
    require(ids[0] == hashed("mix") && ids[0] == 108124u, "THROW retains its existing mix-based VST3 parameter ID 108124");
    for (const char* id : { "size", "decay", "predelay", "lowCut", "highCut", "diffusion", "motion", "width", "grit", "duck", "output", "bypass" })
        require(std::find(ids.begin(), ids.end(), hashed(id)) != ids.end(), "every 0.1.5 parameter keeps its VST3 ID");
    std::set<ParamID> soundIds;
    for (const auto& id : ThrowUpProcessor::roomParameterIds()) soundIds.insert(hashed(id.toRawUTF8()));
    std::unique_ptr<juce::AudioProcessorEditor> editor(actual->createEditorAndMakeActive());
    auto* menu = dynamic_cast<juce::ComboBox*>(named(*editor, "Factory presets"));
    auto* previous = dynamic_cast<juce::Button*>(named(*editor, "Previous preset"));
    auto* next = dynamic_cast<juce::Button*>(named(*editor, "Next preset"));
    auto* dice = dynamic_cast<juce::Button*>(named(*editor, "Random preset"));
    auto* throwKnob = dynamic_cast<juce::Slider*>(named(*editor, "THROW"));
    require(menu != nullptr && previous != nullptr && next != nullptr && dice != nullptr && throwKnob != nullptr,
            "actual packaged VST3 menu, arrows, random key and THROW callbacks accessible");
    constexpr int block = 128;
    ProcessSetup setup {}; setup.maxSamplesPerBlock = block; setup.sampleRate = 48000;
    setup.symbolicSampleSize = kSample32; setup.processMode = kRealtime;
    require(component->activateBus(kAudio, kInput, 0, true) == kResultOk
            && component->activateBus(kAudio, kOutput, 0, true) == kResultOk,
            "host activates main audio input and output buses");
    require(processor->setupProcessing(setup) == kResultOk && component->setActive(true) == kResultOk
            && processor->setProcessing(true) == kResultOk, "packaged processor ready for automated audio");
    ThrowUpProcessor reference;
    for (int i = 0; i < actual->getParameters().size(); ++i)
        reference.getParameters()[i]->setValueNotifyingHost(actual->getParameters()[i]->getValue());
    reference.prepareToPlay(48000, block);
    Automation automation; automation.parameter = ids[0];
    juce::AudioBuffer<float> audio(2, block), expected(2, block);
    juce::MidiBuffer midi;
    double worst = 0;
    bool reported = false;
    int totalBlocks = 0;
    const auto render = [&] {
        automation.value = (static_cast<double>(totalBlocks % 91) + 0.5) / 91.0;
        controller->setParamNormalized(automation.parameter, automation.value);
        reference.getParameters()[0]->setValueNotifyingHost(static_cast<float>(automation.value));
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < block; ++i) {
            const double t = (totalBlocks * block + i) / 48000.0;
            audio.setSample(ch, i, static_cast<float>(.21 * std::sin(2 * juce::MathConstants<double>::pi * (173 + ch * 3) * t)));
        }
        expected.makeCopyOf(audio);
        AudioBusBuffers input {}, output {};
        input.numChannels = output.numChannels = 2;
        float* channels[] {audio.getWritePointer(0), audio.getWritePointer(1)};
        input.channelBuffers32 = output.channelBuffers32 = channels;
        ProcessData data {}; data.processMode = kRealtime; data.symbolicSampleSize = kSample32;
        data.numSamples = block; data.numInputs = data.numOutputs = 1;
        data.inputs = &input; data.outputs = &output; data.inputParameterChanges = &automation;
        ProcessContext context {}; context.sampleRate = 48000;
        context.state = ProcessContext::kPlaying;
        context.projectTimeSamples = static_cast<Steinberg::int64>(totalBlocks) * block;
        data.processContext = &context;
        require(processor->process(data) == kResultOk, "VST3 automation/audio block succeeds");
        reference.processBlock(expected, midi);
        for (int index = 0; index < actual->getParameters().size(); ++index)
            require(std::abs(actual->getParameters()[index]->getValue() - reference.getParameters()[index]->getValue()) < .0000001,
                    "actual/reference control values match before audio comparison");
        require(std::abs(actual->getParameters()[0]->getValue() - automation.value) < .00011,
                "continuous THROW automation reaches packaged audio processor after preset selection");
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < block; ++i) {
            require(std::isfinite(audio.getSample(ch, i)), "automated VST3 audio stays finite");
            const auto difference = std::abs(static_cast<double>(audio.getSample(ch, i) - expected.getSample(ch, i)));
            if (!reported && difference > .00003) {
                reported = true;
                std::cerr << "First audio difference at block " << totalBlocks << " sample " << i
                          << " channel " << ch << ": actual=" << audio.getSample(ch, i)
                          << " reference=" << expected.getSample(ch, i) << '\n';
            }
            worst = std::max(worst, difference);
        }
        ++totalBlocks;
    };
    const int programs = actual->getNumPrograms();
    const int legacyStart = throwup::legacyPresetStart();
    require(programs == legacyStart + throwup::legacyPresetCount, "VST3 programs: the current bank, then the 0.1.5 bank");
    // The arrows step within the current bank, or within the 0.1.5 bank.
    const auto stepped = [&](int index, int direction) {
        const int start = index >= legacyStart ? legacyStart : 0;
        const int count = index >= legacyStart ? throwup::legacyPresetCount : legacyStart;
        return start + (index - start + direction + count) % count;
    };
    for (int i = 0; i < programs; ++i) {
        for (int b = 0; b < 4; ++b) render();
        host.clear();
        menu->setSelectedId(i + 1, juce::sendNotificationSync);
        require(actual->getCurrentProgram() == i, "actual packaged menu chooses requested preset");
        reference.setCurrentProgram(i);
        checkNotifications(host, soundIds);
        for (int b = 0; b < 12; ++b) render();
        host.clear(); previous->onClick(); reference.setCurrentProgram(stepped(i, -1));
        checkNotifications(host, soundIds); render();
        host.clear(); next->onClick(); reference.setCurrentProgram(i);
        checkNotifications(host, soundIds); render();
        // The random key: another room from the same bank, with the same scoped edits.
        host.clear(); dice->onClick();
        const int picked = actual->getCurrentProgram();
        require(picked != i && (picked >= legacyStart) == (i >= legacyStart), "random preset picks another room from the arrows' bank");
        reference.setCurrentProgram(picked);
        checkNotifications(host, soundIds); render();
        const auto hostProgram = (i + 17) % programs;
        host.clear();
        controller->setParamNormalized(programId, static_cast<double>(hostProgram) / static_cast<double>(programs - 1));
        require(actual->getCurrentProgram() == hostProgram, "native VST3 Program selection remains available");
        reference.setCurrentProgram(hostProgram);
        checkNotifications(host, soundIds); render();
        // The editor's 24 Hz timer shows a host Program change on the preset display, but this
        // host runs no message loop. Without it, a random pick of room i + 1 would leave the
        // display on the next iteration's item and make that menu choice a no-op.
        menu->setSelectedId(hostProgram + 1, juce::dontSendNotification);
    }
    std::cout << "Packaged/reference maximum audio error " << worst << '\n';
    require(worst < .00003, "actual VST3 queued automation matches reference audio through all preset selections");
    host.clear();
    {
        juce::Slider::ScopedDragNotification drag(*throwKnob);
        throwKnob->setValue(43.21, juce::sendNotificationSync);
    }
    require(host.begin == std::vector<ParamID>{ids[0]} && host.edits == host.begin && host.end == host.begin,
            "manual THROW drag still sends exactly one balanced VST3 automation gesture");
    require(std::abs(controller->getParamNormalized(ids[0]) - .4321) < .00011,
            "manual THROW drag reaches actual VST3 controller");
    require(host.restartFlags == 0, "manual THROW drag does not restart plugin");
    std::cout << "PASS actual VST3: " << programs << " menu, " << programs << " previous, " << programs << " next, "
              << programs << " random, " << programs << " host Program selections; " << totalBlocks
              << " automated blocks; stable THROW ID " << ids[0] << "; maximum audio error " << worst << '\n';
    std::cout << "PASS no THROW/Program edits or global parameter-cache invalidation during preset selection\n";
    std::cout << "PASS manual THROW drag still records normal VST3 automation gestures\n";
}
} // namespace

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "usage: ThrowUpVstPresetAutomation /absolute/path/THROW-UP.vst3");
        juce::ScopedJuceInitialiser_GUI gui;
        // The random key also draws from user presets: keep the real folder out of the test.
        const auto presets = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("throw-up-vst-presets-" + juce::Uuid().toString());
        setenv("THROW_UP_PRESET_DIR", presets.getFullPathName().toRawUTF8(), 1);
        Host host;
        Bundle bundle(juce::File(juce::String::fromUTF8(argv[1])));
        test(bundle, host);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
}
