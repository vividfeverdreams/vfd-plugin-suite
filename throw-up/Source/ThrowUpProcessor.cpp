#include "ThrowUpProcessor.h"
#include "ThrowUpEditor.h"
#include "Presets.h"
#include <cmath>

namespace {
constexpr std::array<const char*, 13> ids { "mix", "size", "decay", "predelay", "lowCut", "highCut",
    "diffusion", "motion", "width", "grit", "duck", "output", "bypass" };

// Meters hold the highest block peak until the editor collects it with exchange(0),
// so no block between two UI frames is missed.
void raisePeak(std::atomic<float>& meter, float peak)
{
    float previous = meter.load(std::memory_order_relaxed);
    while (peak > previous && !meter.compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {}
}
}

juce::AudioProcessorValueTreeState::ParameterLayout ThrowUpProcessor::parameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    const auto add = [&p](const char* id, const char* name, float lo, float hi, float step,
                         float initial, float skew, const char* unit) {
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(id, 1), name,
            juce::NormalisableRange<float>(lo, hi, step, skew), initial,
            juce::AudioParameterFloatAttributes().withLabel(unit)));
    };
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
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("bypass", 1), "Bypass", false));
    return p;
}

ThrowUpProcessor::ThrowUpProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "THROW_UP_STATE", parameterLayout())
{
    for (size_t i = 0; i < ids.size(); ++i) values[i] = apvts.getRawParameterValue(ids[i]);
    setLatencySamples(0);
    loadFactoryPreset(0);
}

bool ThrowUpProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && out == l.getMainInputChannelSet();
}

throwup::Settings ThrowUpProcessor::readSettings() const
{
    throwup::Settings s;
    s.mix = values[0]->load(); s.size = values[1]->load(); s.decay = values[2]->load();
    s.predelay = values[3]->load(); s.lowCut = values[4]->load(); s.highCut = values[5]->load();
    s.diffusion = values[6]->load(); s.motion = values[7]->load(); s.width = values[8]->load();
    s.grit = values[9]->load(); s.duck = values[10]->load(); s.output = values[11]->load();
    s.bypass = values[12]->load() >= .5f;
    return s;
}

void ThrowUpProcessor::prepareToPlay(double rate, int blockSize)
{
    engine.setSettings(readSettings());
    engine.prepare(rate, blockSize);
    setLatencySamples(0);
}
void ThrowUpProcessor::releaseResources() { engine.reset(); }
void ThrowUpProcessor::reset() { engine.reset(); }
void ThrowUpProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) { process(audio, false); }
void ThrowUpProcessor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) { process(audio, true); }
void ThrowUpProcessor::process(juce::AudioBuffer<float>& audio, bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch) audio.clear(ch, 0, audio.getNumSamples());
    if (audio.getNumSamples() == 0 || audio.getNumChannels() == 0) return;
    raisePeak(inputLevel, audio.getMagnitude(0, audio.getNumSamples()));
    auto settings = readSettings(); settings.bypass = forceBypass || settings.bypass;
    engine.setSettings(settings);
    engine.process(audio.getArrayOfWritePointers(), juce::jmin(2, audio.getNumChannels()), audio.getNumSamples());
    raisePeak(outputLevel, audio.getMagnitude(0, audio.getNumSamples()));
}

juce::AudioProcessorParameter* ThrowUpProcessor::getBypassParameter() const { return apvts.getParameter("bypass"); }
juce::AudioProcessorEditor* ThrowUpProcessor::createEditor() { return new ThrowUpEditor(*this); }
const juce::String ThrowUpProcessor::getProgramName(int index)
{
    if (index < 0 || index >= getNumPrograms()) return {};
    const auto& preset = throwup::factoryPresets()[static_cast<size_t>(index)];
    return juce::String(preset.group) + " / " + preset.name;
}

void ThrowUpProcessor::loadFactoryPreset(int index)
{
    if (index < 0 || index >= getNumPrograms()) return;
    const auto& s = throwup::factoryPresets()[static_cast<size_t>(index)].settings;
    const std::array<float, 10> sound { s.size, s.decay, s.predelay, s.lowCut, s.highCut,
        s.diffusion, s.motion, s.width, s.grit, s.duck };
    for (size_t i = 0; i < sound.size(); ++i) {
        auto* parameter = apvts.getParameter(ids[i + 1]);
        const auto newValue = parameter->convertTo0to1(sound[i]);
        if (juce::approximatelyEqual(parameter->getValue(), newValue)) continue;
        // Presets edit only the room controls. Keep their notifications scoped to
        // those parameters so an automated THROW is never treated as a user edit.
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(newValue);
        parameter->endChangeGesture();
    }
    const auto previousProgram = currentProgram.exchange(index);
    if (previousProgram != index) {
        // JUCE's VST3 programChanged notification generates a Program gesture and
        // kParamValuesChanged for the entire plugin. A room preset must not tell
        // the DAW to replace THROW automation. Save the changed preset metadata
        // without that global parameter refresh; the editor reads currentProgram.
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
    }
}

void ThrowUpProcessor::getStateInformation(juce::MemoryBlock& data)
{
    auto state = apvts.copyState();
    state.setProperty("factoryProgram", currentProgram.load(), nullptr);
    state.setProperty("schemaVersion", 1, nullptr);
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
    currentProgram.store(juce::jlimit(0, 49, static_cast<int>(state.getProperty("factoryProgram", 0))));
    apvts.replaceState(state);
    // State restoration is requested by the host. The format wrapper refreshes
    // its controller after loading; do not echo a user Program gesture back.
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ThrowUpProcessor(); }
