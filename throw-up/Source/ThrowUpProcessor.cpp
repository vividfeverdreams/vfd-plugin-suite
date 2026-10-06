#include "ThrowUpProcessor.h"
#include "ThrowUpEditor.h"
#include "Presets.h"
#include <cmath>
#include <optional>

using throwup::SoundVersion;

namespace {
// Meters hold the highest block peak until the editor collects it with exchange(0),
// so no block between two UI frames is missed.
void raisePeak(std::atomic<float>& meter, float peak)
{
    float previous = meter.load(std::memory_order_relaxed);
    while (peak > previous && !meter.compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {}
}

juce::File& userPresetRootOverride()
{
    static juce::File root;
    return root;
}

constexpr int presetSchemaVersion = 2;
const juce::Identifier presetTag { "THROW_UP_PRESET" };

// Every parameter ID in host order: the original 13, then the 0.2.0 additions.
constexpr std::array<const char*, ThrowUpProcessor::parameterCount> allIds { "mix", "size", "decay", "predelay",
    "lowCut", "highCut", "diffusion", "motion", "width", "grit", "duck", "output", "bypass",
    "throwKey", "trigger", "throwAmount", "throwAttack", "throwRelease", "releaseSync", "noteRooms", "catchLevel",
    "predelaySync", "predelayFeel", "shimmer", "shimmerPitch", "swell", "freeze", "returnTrim", "safeClip", "dry",
    "duckSource", "duckRelease", "duckThreshold", "soundVersion" };

consteval int indexOf(const char* id)
{
    for (int i = 0; i < static_cast<int>(allIds.size()); ++i) {
        const char* a = allIds[static_cast<size_t>(i)];
        const char* b = id;
        while (*a != 0 && *a == *b) { ++a; ++b; }
        if (*a == *b) return i;
    }
    return -1;
}
} // namespace

// Audio-thread parameter reads use pointers cached at construction (no lookups).
#define THROWUP_RAW(id) (parameterValues[static_cast<size_t>(indexOf(id))]->load(std::memory_order_relaxed))
#define THROWUP_CHOICE(id) (juce::roundToInt(THROWUP_RAW(id)))

juce::AudioProcessorValueTreeState::ParameterLayout ThrowUpProcessor::parameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    const auto add = [&p](const char* id, const char* name, float lo, float hi, float step,
                         float initial, float skew, const char* unit) {
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(id, 1), name,
            juce::NormalisableRange<float>(lo, hi, step, skew), initial,
            juce::AudioParameterFloatAttributes().withLabel(unit)));
    };
    const auto addChoice = [&p](const char* id, const char* name, juce::StringArray items, int initial,
                               bool automatable = true) {
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(id, 1), name, items, initial,
            juce::AudioParameterChoiceAttributes().withAutomatable(automatable)));
    };
    const auto addBool = [&p](const char* id, const char* name, bool initial) {
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(id, 1), name, initial));
    };
    // 0.1.5 parameters: IDs, ranges and order unchanged.
    add("mix", "THROW", 0, 100, .01f, 0, 1, "%");
    add("size", "Room Size", 0, 100, .1f, 60, 1, "%");
    add("decay", "Decay", .15f, 18, .01f, 2.4f, .4f, "s");
    add("predelay", "Pre-delay", 0, 250, .1f, 24, .65f, "ms");
    add("lowCut", "Low Cut", 80, 1000, 1, 180, .55f, "Hz");
    add("highCut", "High Cut", 1000, 18000, 1, 6500, .55f, "Hz");
    add("diffusion", "Diffusion", 0, 100, .1f, 75, 1, "%");
    add("motion", "Motion", 0, 100, .1f, 20, 1, "%");
    add("width", "Width", 0, 150, .1f, 110, 1, "%");
    add("grit", "Grit", 0, 100, .1f, 10, 1, "%");
    add("duck", "Duck", 0, 100, .1f, 15, 1, "%");
    add("output", "Output", -18, 6, .1f, 0, 1, "dB");
    addBool("bypass", "Bypass", false);
    // 0.2.0 parameters (appended). Defaults reproduce 0.1.5 except Sound Version.
    addBool("throwKey", "THROW!", false);
    addChoice("trigger", "Trigger Mode", { "Manual", "MIDI", "Auto-catch" }, 0);
    add("throwAmount", "Throw Amount", 0, 100, .1f, 100, 1, "%");
    add("throwAttack", "Throw Attack", 4, 1000, .1f, 10, .4f, "ms");
    add("throwRelease", "Throw Release", 10, 5000, 1, 600, .4f, "ms");
    addChoice("releaseSync", "Release Sync", { "ms", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars" }, 0);
    addBool("noteRooms", "Note Picks Room", false);
    add("catchLevel", "Catch Threshold", -60, -6, .1f, -30, 1, "dB");
    addChoice("predelaySync", "Pre-delay Sync", { "ms", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2" }, 0);
    addChoice("predelayFeel", "Pre-delay Feel", { "Straight", "Dotted", "Triplet" }, 0);
    add("shimmer", "Shimmer", 0, 100, .1f, 0, 1, "%");
    addChoice("shimmerPitch", "Shimmer Pitch", { "-12 st", "-5 st", "+7 st", "+12 st" }, 3);
    addChoice("swell", "Swell", { "Off", "1/4", "1/2", "1 bar", "2 bars" }, 0);
    addBool("freeze", "Freeze", false);
    add("returnTrim", "Return Trim", -24, 6, .1f, 0, 1, "dB");
    addBool("safeClip", "Safe Clip", false);
    addBool("dry", "Dry", true);
    addChoice("duckSource", "Duck Source", { "Dry", "External" }, 0);
    add("duckRelease", "Duck Release", 20, 2000, 1, 160, .4f, "ms");
    add("duckThreshold", "Duck Threshold", -60, 0, .1f, -24, 1, "dB");
    addChoice("soundVersion", "Sound Version", { "0.1.5", "Latest" }, 1, false);
    return p;
}

juce::StringArray ThrowUpProcessor::roomParameterIds()
{
    return { "size", "decay", "predelay", "lowCut", "highCut", "diffusion", "motion", "width", "grit", "duck",
             "predelaySync", "predelayFeel", "shimmer", "shimmerPitch", "swell", "duckRelease", "duckThreshold",
             "soundVersion" };
}

ThrowUpProcessor::ThrowUpProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
        .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "THROW_UP_STATE", parameterLayout())
{
    static_assert(indexOf("soundVersion") == parameterCount - 1 && indexOf("bypass") == 12, "parameter ID table out of order");
    for (size_t i = 0; i < allIds.size(); ++i) {
        parameterValues[i] = apvts.getRawParameterValue(allIds[i]);
        jassert(parameterValues[i] != nullptr);
    }
    setLatencySamples(0);
    loadFactoryPreset(0);
}

ThrowUpProcessor::~ThrowUpProcessor()
{
    cancelPendingUpdate();
}

bool ThrowUpProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (!(out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        || out != l.getMainInputChannelSet())
        return false;
    // Optional sidechain for Duck: off, mono or stereo.
    for (int bus = 1; bus < l.inputBuses.size(); ++bus) {
        const auto set = l.getChannelSet(true, bus);
        if (!(set.isDisabled() || set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo()))
            return false;
    }
    return l.outputBuses.size() == 1;
}

int ThrowUpProcessor::getNumPrograms()
{
    return static_cast<int>(throwup::factoryPresets().size());
}

throwup::Settings ThrowUpProcessor::readSettings(int numSamples)
{
    double bpm = hostTempo.load(std::memory_order_relaxed), beatsPerBar = 4.0;
    juce::Optional<double> ppq;
    bool playing = false;
    if (auto* head = getPlayHead()) {
        if (const auto position = head->getPosition()) {
            if (const auto tempo = position->getBpm(); tempo && *tempo >= 20.0 && *tempo <= 999.0) bpm = *tempo;
            if (const auto signature = position->getTimeSignature(); signature && signature->numerator > 0 && signature->denominator > 0)
                beatsPerBar = signature->numerator * 4.0 / signature->denominator;
            playing = position->getIsPlaying();
            ppq = position->getPpqPosition();
        }
    }
    hostTempo.store(bpm, std::memory_order_relaxed);

    throwup::Settings s;
    s.mix = THROWUP_RAW("mix"); s.size = THROWUP_RAW("size"); s.decay = THROWUP_RAW("decay");
    s.predelay = THROWUP_RAW("predelay"); s.lowCut = THROWUP_RAW("lowCut"); s.highCut = THROWUP_RAW("highCut");
    s.diffusion = THROWUP_RAW("diffusion"); s.motion = THROWUP_RAW("motion"); s.width = THROWUP_RAW("width");
    s.grit = THROWUP_RAW("grit"); s.duck = THROWUP_RAW("duck"); s.output = THROWUP_RAW("output");
    s.bypass = THROWUP_RAW("bypass") >= .5f;
    s.version = THROWUP_CHOICE("soundVersion") == 0 ? SoundVersion::v015 : SoundVersion::latest;
    s.returnTrim = THROWUP_RAW("returnTrim");
    s.dry = THROWUP_RAW("dry") >= .5f;
    s.safeClip = THROWUP_RAW("safeClip") >= .5f;
    s.freeze = THROWUP_RAW("freeze") >= .5f;
    s.shimmer = THROWUP_RAW("shimmer");
    s.shimmerPitch = throwup::shimmerSemitones(static_cast<throwup::ShimmerPitch>(juce::jlimit(0, 3, THROWUP_CHOICE("shimmerPitch"))));
    s.duckRelease = THROWUP_RAW("duckRelease");
    s.duckThreshold = THROWUP_RAW("duckThreshold");
    s.duckExternal = THROWUP_CHOICE("duckSource") == 1;
    auto predelaySync = static_cast<throwup::PredelaySync>(juce::jlimit(0, 6, THROWUP_CHOICE("predelaySync")));
    auto predelayFeel = static_cast<throwup::PredelayFeel>(juce::jlimit(0, 2, THROWUP_CHOICE("predelayFeel")));
    auto swell = static_cast<throwup::SwellLength>(juce::jlimit(0, 4, THROWUP_CHOICE("swell")));

    // A MIDI note picked a room: play it at once, while the message thread
    // catches the visible controls up.
    if (roomOverride >= 0) {
        if (appliedRoom.load() == roomOverride) {
            roomOverride = -1;
        } else {
            const auto& r = throwup::factoryPresets()[static_cast<size_t>(roomOverride)].room;
            s.size = r.size; s.decay = r.decay; s.predelay = r.predelay; s.lowCut = r.lowCut; s.highCut = r.highCut;
            s.diffusion = r.diffusion; s.motion = r.motion; s.width = r.width; s.grit = r.grit; s.duck = r.duck;
            s.shimmer = r.shimmer; s.shimmerPitch = throwup::shimmerSemitones(r.shimmerPitch);
            s.duckRelease = r.duckRelease; s.duckThreshold = r.duckThreshold; s.version = r.version;
            predelaySync = r.predelaySync; predelayFeel = r.predelayFeel; swell = r.swell;
        }
    }
    if (const auto beats = throwup::predelayBeats(predelaySync, predelayFeel); beats > 0.0)
        s.predelay = static_cast<float>(std::min(beats * 60000.0 / bpm, static_cast<double>(throwup::ReverbEngine::maximumPredelayMs)));

    // THROW trigger.
    const int mode = THROWUP_CHOICE("trigger");
    s.triggerHeld = THROWUP_RAW("throwKey") >= .5f;
    s.autoCatch = mode == 2;
    s.triggerAmount = THROWUP_RAW("throwAmount");
    s.triggerAttack = THROWUP_RAW("throwAttack");
    s.catchThreshold = THROWUP_RAW("catchLevel");
    const auto releaseSync = static_cast<throwup::ReleaseSync>(juce::jlimit(0, 6, THROWUP_CHOICE("releaseSync")));
    if (const auto beats = throwup::releaseBeats(releaseSync, beatsPerBar); beats > 0.0)
        s.triggerRelease = static_cast<float>(beats * 60000.0 / bpm);
    else
        s.triggerRelease = THROWUP_RAW("throwRelease");

    // Swell follows the host's bar position while playing, otherwise runs freely.
    if (const auto cycle = throwup::swellBeats(swell, beatsPerBar); cycle > 0.0) {
        s.swell = true;
        s.swellPhaseStep = bpm / 60.0 / cycle / sampleRate;
        if (playing && ppq) {
            s.swellPhase = std::fmod(*ppq, cycle) / cycle;
            if (s.swellPhase < 0.0) s.swellPhase += 1.0;
            freeSwellPhase = s.swellPhase;
        } else {
            s.swellPhase = freeSwellPhase;
        }
        freeSwellPhase += s.swellPhaseStep * numSamples;
        freeSwellPhase -= std::floor(freeSwellPhase);
    }
    return s;
}

void ThrowUpProcessor::prepareToPlay(double rate, int blockSize)
{
    sampleRate = rate > 0 ? rate : 48000.0;
    engine.setSettings(readSettings(blockSize));
    engine.prepare(rate, blockSize);
    setLatencySamples(0);
}
void ThrowUpProcessor::releaseResources() { engine.reset(); }
void ThrowUpProcessor::reset() { engine.reset(); }
void ThrowUpProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) { process(audio, midi, false); }
void ThrowUpProcessor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) { process(audio, midi, true); }

void ThrowUpProcessor::handleMidi(const juce::MidiBuffer& midi, int numSamples)
{
    triggerEventCount = 0;
    const bool midiMode = THROWUP_CHOICE("trigger") == 1;
    const bool noteRooms = THROWUP_RAW("noteRooms") >= .5f;
    const float amount = THROWUP_RAW("throwAmount");
    if (!midiMode) {
        if (midiWasActive) {
            // Leaving MIDI mode releases any held throw.
            heldNotes.fill(false); heldNoteCount = 0; midiLevel = 0.0f;
            triggerEvents[0] = { 0, 0.0f }; triggerEventCount = 1;
            midiWasActive = false;
        }
        return;
    }
    midiWasActive = true;
    for (const auto metadata : midi) {
        const auto message = metadata.getMessage();
        const int offset = juce::jlimit(0, juce::jmax(0, numSamples - 1), metadata.samplePosition);
        bool changed = false;
        if (message.isNoteOn()) {
            const int note = message.getNoteNumber();
            if (!heldNotes[static_cast<size_t>(note)]) { heldNotes[static_cast<size_t>(note)] = true; ++heldNoteCount; }
            midiLevel = message.getFloatVelocity() * amount;
            changed = true;
            const int room = note - firstRoomNote;
            if (noteRooms && room >= 0 && room < throwup::legacyPresetStart()) {
                roomOverride = room;
                appliedRoom.store(-1);
                pendingRoom.store(room);
                triggerAsyncUpdate();
            }
        } else if (message.isNoteOff()) {
            const int note = message.getNoteNumber();
            if (heldNotes[static_cast<size_t>(note)]) { heldNotes[static_cast<size_t>(note)] = false; --heldNoteCount; }
            if (heldNoteCount <= 0) { heldNoteCount = 0; midiLevel = 0.0f; changed = true; }
        } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
            heldNotes.fill(false); heldNoteCount = 0; midiLevel = 0.0f; changed = true;
        }
        if (changed && triggerEventCount < static_cast<int>(triggerEvents.size()))
            triggerEvents[static_cast<size_t>(triggerEventCount++)] = { offset, midiLevel };
    }
}

void ThrowUpProcessor::process(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi, bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch) audio.clear(ch, 0, audio.getNumSamples());
    const int numSamples = audio.getNumSamples();
    if (numSamples == 0 || audio.getNumChannels() == 0) return;
    auto main = getBusBuffer(audio, false, 0);
    if (main.getNumChannels() == 0) return;
    raisePeak(inputLevel, main.getMagnitude(0, numSamples));
    handleMidi(midi, numSamples);
    auto settings = readSettings(numSamples);
    settings.bypass = forceBypass || settings.bypass;
    engine.setSettings(settings);
    engine.setTriggerEvents(triggerEvents.data(), triggerEventCount);
    const float* sidechain[2] { nullptr, nullptr };
    int sidechainChannels = 0;
    if (auto* bus = getBus(true, 1); bus != nullptr && bus->isEnabled()) {
        auto side = getBusBuffer(audio, true, 1);
        sidechainChannels = juce::jmin(2, side.getNumChannels());
        for (int ch = 0; ch < sidechainChannels; ++ch) sidechain[ch] = side.getReadPointer(ch);
    }
    engine.process(main.getArrayOfWritePointers(), juce::jmin(2, main.getNumChannels()), numSamples,
                   sidechainChannels > 0 ? sidechain : nullptr, sidechainChannels);
    triggerLevel.store(engine.getTriggerLevel(), std::memory_order_relaxed);
    raisePeak(outputLevel, main.getMagnitude(0, numSamples));
}

void ThrowUpProcessor::handleAsyncUpdate()
{
    const int room = pendingRoom.exchange(-1);
    if (room >= 0) {
        loadFactoryPreset(room);
        appliedRoom.store(room);
    }
}

juce::AudioProcessorParameter* ThrowUpProcessor::getBypassParameter() const { return apvts.getParameter("bypass"); }
juce::AudioProcessorEditor* ThrowUpProcessor::createEditor() { return new ThrowUpEditor(*this); }
const juce::String ThrowUpProcessor::getProgramName(int index)
{
    if (index < 0 || index >= getNumPrograms()) return {};
    const auto& preset = throwup::factoryPresets()[static_cast<size_t>(index)];
    return (preset.legacy ? juce::String("0.1.5 ") : juce::String()) + preset.group + " / " + preset.name;
}

void ThrowUpProcessor::applyParameter(const juce::String& id, float value)
{
    auto* parameter = apvts.getParameter(id);
    if (parameter == nullptr) return;
    const auto newValue = parameter->convertTo0to1(value);
    if (juce::approximatelyEqual(parameter->getValue(), newValue)) return;
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(newValue);
    parameter->endChangeGesture();
}

void ThrowUpProcessor::loadFactoryPreset(int index)
{
    if (index < 0 || index >= getNumPrograms()) return;
    const auto& r = throwup::factoryPresets()[static_cast<size_t>(index)].room;
    const std::array<float, 18> values { r.size, r.decay, r.predelay, r.lowCut, r.highCut, r.diffusion, r.motion,
        r.width, r.grit, r.duck, static_cast<float>(r.predelaySync), static_cast<float>(r.predelayFeel), r.shimmer,
        static_cast<float>(r.shimmerPitch), static_cast<float>(r.swell), r.duckRelease, r.duckThreshold,
        r.version == SoundVersion::v015 ? 0.0f : 1.0f };
    const auto roomIds = roomParameterIds();
    // Presets edit only the room controls. Keep their notifications scoped to
    // those parameters so an automated THROW is never treated as a user edit.
    for (int i = 0; i < roomIds.size(); ++i)
        applyParameter(roomIds[i], values[static_cast<size_t>(i)]);
    const auto previousProgram = currentProgram.exchange(index);
    const bool wasUser = userPresetName.isNotEmpty();
    userPresetName.clear();
    if (previousProgram != index || wasUser) {
        // JUCE's VST3 programChanged notification generates a Program gesture and
        // kParamValuesChanged for the entire plugin. A room preset must not tell
        // the DAW to replace THROW automation. Save the changed preset metadata
        // without that global parameter refresh; the editor reads currentProgram.
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
    }
}

SoundVersion ThrowUpProcessor::getSoundVersion() const
{
    return THROWUP_CHOICE("soundVersion") == 0 ? SoundVersion::v015 : SoundVersion::latest;
}

void ThrowUpProcessor::setSoundVersion(SoundVersion version)
{
    applyParameter("soundVersion", version == SoundVersion::v015 ? 0.0f : 1.0f);
}

juce::File ThrowUpProcessor::userPresetRoot()
{
    if (userPresetRootOverride() != juce::File()) return userPresetRootOverride();
    const auto environment = juce::SystemStats::getEnvironmentVariable("THROW_UP_PRESET_DIR", {});
    if (environment.isNotEmpty() && juce::File::isAbsolutePath(environment)) return juce::File(environment);
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support").getChildFile("THROW-UP").getChildFile("Presets");
}

void ThrowUpProcessor::setUserPresetRootForTesting(const juce::File& root) { userPresetRootOverride() = root; }

juce::File ThrowUpProcessor::saveUserPreset(const juce::String& folder, const juce::String& name)
{
    const auto cleanName = juce::File::createLegalFileName(name.trim());
    const auto cleanFolder = juce::File::createLegalFileName(folder.trim());
    if (cleanName.isEmpty()) return {};
    auto directory = cleanFolder.isEmpty() ? userPresetRoot() : userPresetRoot().getChildFile(cleanFolder);
    if (!directory.createDirectory()) return {};
    // Never replace an existing preset: find the first free name.
    auto file = directory.getChildFile(cleanName + ".throwup");
    for (int n = 2; file.exists(); ++n)
        file = directory.getChildFile(cleanName + " " + juce::String(n) + ".throwup");
    juce::XmlElement xml(presetTag.toString());
    xml.setAttribute("name", name.trim());
    xml.setAttribute("schemaVersion", presetSchemaVersion);
    xml.setAttribute("pluginVersion", JucePlugin_VersionString);
    for (const auto& id : roomParameterIds()) {
        auto* child = xml.createNewChildElement("PARAM");
        child->setAttribute("id", id);
        child->setAttribute("value", static_cast<double>(apvts.getRawParameterValue(id)->load()));
    }
    if (!xml.writeTo(file)) return {};
    userPresetName = name.trim();
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
    return file;
}

bool ThrowUpProcessor::loadUserPreset(const juce::File& file)
{
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName(presetTag.toString())) return false;
    const auto roomIds = roomParameterIds();
    // A preset saved before Sound Version existed would have played with the 0.1.5 sound.
    applyParameter("soundVersion", 0.0f);
    bool sawVersion = false;
    for (auto* child : xml->getChildWithTagNameIterator("PARAM")) {
        const auto id = child->getStringAttribute("id");
        const auto value = static_cast<float>(child->getDoubleAttribute("value", std::numeric_limits<double>::quiet_NaN()));
        if (!roomIds.contains(id) || !std::isfinite(value)) continue;
        sawVersion = sawVersion || id == "soundVersion";
        applyParameter(id, value);
    }
    if (!sawVersion && xml->getIntAttribute("schemaVersion", 0) >= presetSchemaVersion)
        applyParameter("soundVersion", 1.0f);
    userPresetName = xml->getStringAttribute("name", file.getFileNameWithoutExtension());
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
    return true;
}

juce::ValueTree ThrowUpProcessor::captureSlot() const
{
    juce::ValueTree slot("SLOT");
    for (auto* parameter : getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        if (ranged == nullptr) continue;
        const auto id = ranged->getParameterID();
        if (id == "mix" || id == "output" || id == "bypass" || id == "throwKey") continue;
        slot.setProperty(id, ranged->convertFrom0to1(ranged->getValue()), nullptr);
    }
    slot.setProperty("program", currentProgram.load(), nullptr);
    slot.setProperty("userPreset", userPresetName, nullptr);
    return slot;
}

void ThrowUpProcessor::applySlot(const juce::ValueTree& slot)
{
    for (int i = 0; i < slot.getNumProperties(); ++i) {
        const auto name = slot.getPropertyName(i).toString();
        if (name == "program" || name == "userPreset") continue;
        const auto value = static_cast<float>(slot.getProperty(name));
        if (std::isfinite(value)) applyParameter(name, value);
    }
    currentProgram.store(juce::jlimit(0, getNumPrograms() - 1, static_cast<int>(slot.getProperty("program", 0))));
    userPresetName = slot.getProperty("userPreset").toString();
}

void ThrowUpProcessor::selectSlot(int slot)
{
    slot = juce::jlimit(0, 1, slot);
    if (slot == activeSlot) return;
    slots[static_cast<size_t>(activeSlot)] = captureSlot();
    if (!slots[static_cast<size_t>(slot)].isValid())
        slots[static_cast<size_t>(slot)] = slots[static_cast<size_t>(activeSlot)].createCopy();
    activeSlot = slot;
    applySlot(slots[static_cast<size_t>(slot)]);
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
}

void ThrowUpProcessor::setEditorScale(float scale)
{
    scale = juce::jlimit(0.8f, 1.5f, scale);
    if (std::abs(scale - editorScale) < 0.001f) return;
    editorScale = scale;
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
}

void ThrowUpProcessor::getStateInformation(juce::MemoryBlock& data)
{
    auto state = apvts.copyState();
    state.setProperty("factoryProgram", currentProgram.load(), nullptr);
    state.setProperty("schemaVersion", presetSchemaVersion, nullptr);
    if (editorScale > 0.0f) state.setProperty("editorScale", editorScale, nullptr);
    if (userPresetName.isNotEmpty()) state.setProperty("userPreset", userPresetName, nullptr);
    // A/B slots live in root properties, not child nodes, so 0.1.5 (which rejects
    // unknown children) can still open a project saved with this version.
    state.setProperty("activeSlot", activeSlot, nullptr);
    for (size_t i = 0; i < slots.size(); ++i)
        if (i != static_cast<size_t>(activeSlot) && slots[i].isValid())
            state.setProperty(i == 0 ? "slotA" : "slotB", slots[i].toXmlString(juce::XmlElement::TextFormat().singleLine()), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, data);
}

void ThrowUpProcessor::setStateInformation(const void* data, int size)
{
    if (data == nullptr || size <= 0) return;
    auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || !xml->hasTagName("THROW_UP_STATE")) return;
    const auto loaded = juce::ValueTree::fromXml(*xml);
    if (!loaded.isValid()) return;
    // Rebuild the state from known parameters only. Unknown children (for
    // example, data added by a newer version) are skipped instead of rejecting
    // the whole session, and a non-finite value falls back to that parameter's
    // default so a bad entry cannot poison the audio thread.
    juce::ValueTree state(loaded.getType());
    state.copyPropertiesFrom(loaded, nullptr);
    for (const auto& child : loaded) {
        if (!child.hasType("PARAM") || !child.hasProperty("id") || !child.hasProperty("value")) continue;
        auto* parameter = apvts.getParameter(child.getProperty("id").toString());
        if (parameter == nullptr || state.getChildWithProperty("id", parameter->getParameterID()).isValid()) continue;
        auto restored = child.createCopy();
        if (!std::isfinite(static_cast<float>(child.getProperty("value"))))
            restored.setProperty("value", parameter->convertFrom0to1(parameter->getDefaultValue()), nullptr);
        state.appendChild(restored, nullptr);
    }
    const int schema = static_cast<int>(loaded.getProperty("schemaVersion", 1));
    int program = static_cast<int>(state.getProperty("factoryProgram", 0));
    if (schema < 2) {
        // Saved with 0.1.5 or older: keep its sound exactly (0.1.5 tone, grit, duck
        // and return) and recall its room from the 0.1.5 bank by its old index.
        auto version = state.getChildWithProperty("id", "soundVersion");
        if (!version.isValid()) {
            version = juce::ValueTree("PARAM");
            version.setProperty("id", "soundVersion", nullptr);
            state.appendChild(version, nullptr);
        }
        version.setProperty("value", 0.0f, nullptr);
        program = throwup::legacyPresetStart() + juce::jlimit(0, throwup::legacyPresetCount - 1, program);
    }
    currentProgram.store(juce::jlimit(0, getNumPrograms() - 1, program));
    userPresetName = loaded.getProperty("userPreset").toString();
    editorScale = loaded.hasProperty("editorScale") ? juce::jlimit(0.8f, 1.5f, static_cast<float>(loaded.getProperty("editorScale"))) : 0.0f;
    activeSlot = juce::jlimit(0, 1, static_cast<int>(loaded.getProperty("activeSlot", 0)));
    for (size_t i = 0; i < slots.size(); ++i) {
        slots[i] = {};
        if (auto slotXml = juce::parseXML(loaded.getProperty(i == 0 ? "slotA" : "slotB").toString()))
            slots[i] = juce::ValueTree::fromXml(*slotXml);
    }
    apvts.replaceState(state);
    // State restoration is requested by the host. The format wrapper refreshes
    // its controller after loading; do not echo a user Program gesture back.
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ThrowUpProcessor(); }
