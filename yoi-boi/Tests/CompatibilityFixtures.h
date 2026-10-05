#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

namespace yoiboi
{
namespace compatibility
{
// Exercise the distributed binaries through the host API. This deliberately
// does not rebuild an old processor against today's headers or translate an
// old session before delivering it to the new VST3.
constexpr std::array<const char*, 19> soundParameterNames {
    "Mouth", "Size", "Bite", "Dirt", "Sub", "Free Length", "Synced Length", "Sync",
    "Loop", "Glide", "Output", "Start Vowel", "Body Vowel", "End Vowel", "Body Time", "Curve",
    "Sub Routing", "Body Low Cut", "Length Feel"
};

inline juce::AudioProcessorParameter* parameterNamed (juce::AudioPluginInstance& plugin,
                                                     const juce::String& name)
{
    for (auto* parameter : plugin.getParameters())
        if (parameter->getName (128) == name) return parameter;
    return nullptr;
}

inline bool setParameter (juce::AudioPluginInstance& plugin, const char* name, float normalised,
                         bool optional = false)
{
    if (auto* parameter = parameterNamed (plugin, name))
    {
        parameter->setValueNotifyingHost (normalised);
        return true;
    }
    if (! optional) std::cerr << "Missing fixture parameter: " << name << '\n';
    return optional;
}

inline juce::String parameterId (juce::AudioProcessorParameter& parameter)
{
    if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (&parameter))
        return hosted->getParameterID();
    return {};
}

inline juce::StringArray moduleClassIds (const juce::File& bundle)
{
    // Steinberg's moduleinfo writer emits trailing commas. Reading the CID
    // lines avoids treating that quasi-JSON formatting as a test failure.
    juce::StringArray ids;
    for (const auto& line : juce::StringArray::fromLines
         (bundle.getChildFile ("Contents/Resources/moduleinfo.json").loadFileAsString()))
        if (line.trimStart().startsWith ("\"CID\""))
            ids.add (line.fromFirstOccurrenceOf (":", false, false).trim()
                         .fromFirstOccurrenceOf ("\"", false, false)
                         .upToFirstOccurrenceOf ("\"", false, false));
    ids.sort (false);
    return ids;
}

inline juce::ValueTree processorState (const juce::MemoryBlock& hostedState)
{
    const auto outer = juce::AudioProcessor::getXmlFromBinary
        (hostedState.getData(), static_cast<int> (hostedState.getSize()));
    if (outer == nullptr) return {};
    const auto* component = outer->getChildByName ("IComponent");
    if (component == nullptr) return {};
    juce::MemoryBlock componentState;
    if (! componentState.fromBase64Encoding (component->getAllSubText())) return {};
    const auto inner = juce::AudioProcessor::getXmlFromBinary
        (componentState.getData(), static_cast<int> (componentState.getSize()));
    return inner != nullptr ? juce::ValueTree::fromXml (*inner) : juce::ValueTree {};
}

struct FixturePlayHead final : juce::AudioPlayHead
{
    double sampleRate = 48000.0;
    double bpm = 142.0;
    juce::int64 sample = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo result;
        result.setBpm (bpm);
        result.setTimeInSamples (sample);
        result.setTimeInSeconds (static_cast<double> (sample) / sampleRate);
        result.setPpqPosition (static_cast<double> (sample) / sampleRate * bpm / 60.0);
        result.setTimeSignature (TimeSignature { 4, 4 });
        result.setIsPlaying (true);
        return result;
    }
};

// Time of the fixture's final CC123 (All Notes Off). Since 0.1.6 the current
// engine (3) answers CC123 with a normal release instead of a hard reset, so
// for engine-3 sounds the comparison with releases 0.1.3-0.1.5 stops at this
// frame. Everything before it must still match exactly, and the frames after
// it must be a decaying release. Engines 1 and 2 keep the hard cut and are
// compared over the full render.
constexpr double allNotesOffSeconds = 1.79;

struct TimedMidi
{
    int sample;
    juce::MidiMessage message;
};

struct RenderedAudio
{
    std::vector<float> samples;
    int latency = 0;
    bool finite = true;
};

inline RenderedAudio render (juce::AudioPluginInstance& plugin, double sampleRate,
                             int blockSize, bool automate, double tempo = 142.0)
{
    FixturePlayHead playHead;
    playHead.sampleRate = sampleRate;
    playHead.bpm = tempo;
    plugin.setPlayHead (&playHead);
    plugin.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    plugin.prepareToPlay (sampleRate, blockSize);

    const auto at = [sampleRate] (double seconds) { return juce::roundToInt (seconds * sampleRate); };
    const std::array<TimedMidi, 14> events {{
        { 17, juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (109)) },
        { at (.13), juce::MidiMessage::pitchWheel (1, 9800) },
        { at (.23), juce::MidiMessage::noteOn (1, 43, static_cast<juce::uint8> (72)) },
        { at (.32), juce::MidiMessage::noteOff (1, 36) },
        { at (.36), juce::MidiMessage::controllerEvent (1, 64, 127) },
        { at (.40), juce::MidiMessage::noteOff (1, 43) },
        { at (.53), juce::MidiMessage::controllerEvent (1, 64, 0) },
        { at (.60), juce::MidiMessage::pitchWheel (1, 8192) },
        { at (.65), juce::MidiMessage::noteOn (1, 29, static_cast<juce::uint8> (96)) },
        { at (.91), juce::MidiMessage::noteOff (1, 29) },
        { at (1.1), juce::MidiMessage::allSoundOff (1) },
        { at (1.2), juce::MidiMessage::noteOn (1, 55, static_cast<juce::uint8> (127)) },
        { at (1.43), juce::MidiMessage::noteOff (1, 55) },
        { at (allNotesOffSeconds), juce::MidiMessage::allNotesOff (1) }
    }};

    RenderedAudio result;
    result.latency = plugin.getLatencySamples();
    result.samples.resize (static_cast<std::size_t> (at (2.0)));
    std::size_t eventIndex = 0;
    int automationIndex = 0;
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int start = 0; start < static_cast<int> (result.samples.size()); start += blockSize)
    {
        const auto count = std::min (blockSize, static_cast<int> (result.samples.size()) - start);
        buffer.setSize (2, count, false, false, true);
        buffer.clear();
        midi.clear();
        playHead.sample = start;
        if (automate)
        {
            if (automationIndex == 0 && start >= at (.19))
            {
                setParameter (plugin, "Mouth", .37f);
                setParameter (plugin, "Bite", .83f);
                setParameter (plugin, "Dirt", .48f);
                ++automationIndex;
            }
            if (automationIndex == 1 && start >= at (.71))
            {
                setParameter (plugin, "Size", .81f);
                setParameter (plugin, "Output", .41f);
                setParameter (plugin, "Sub", .68f);
                setParameter (plugin, "Body Time", .27f);
                ++automationIndex;
            }
            if (automationIndex == 2 && start >= at (1.14))
            {
                setParameter (plugin, "End Vowel", .75f);
                setParameter (plugin, "Curve", .72f);
                setParameter (plugin, "Synced Length", .75f);
                ++automationIndex;
            }
        }
        while (eventIndex < events.size() && events[eventIndex].sample < start + count)
        {
            midi.addEvent (events[eventIndex].message, events[eventIndex].sample - start);
            ++eventIndex;
        }
        plugin.processBlock (buffer, midi);
        for (int frame = 0; frame < count; ++frame)
        {
            const auto left = buffer.getSample (0, frame);
            const auto right = buffer.getSample (1, frame);
            result.finite = result.finite && std::isfinite (left) && std::isfinite (right)
                            && std::abs (left - right) <= 1.0e-7f;
            result.samples[static_cast<std::size_t> (start + frame)] = left;
        }
    }
    plugin.releaseResources();
    plugin.setPlayHead (nullptr);
    return result;
}

struct Difference
{
    double maximum = 0.0;
    double rms = 0.0;
    double peak = 0.0;
    std::size_t maximumFrame = 0;
    bool passed = false;
};

inline Difference compare (const RenderedAudio& expected, const RenderedAudio& actual,
                           std::size_t comparedFrames = std::numeric_limits<std::size_t>::max())
{
    Difference result;
    if (! expected.finite || ! actual.finite || expected.samples.size() != actual.samples.size())
        return result;
    comparedFrames = std::min (comparedFrames, expected.samples.size());
    for (std::size_t i = 0; i < comparedFrames; ++i)
    {
        const auto error = std::abs (static_cast<double> (expected.samples[i]) - actual.samples[i]);
        if (error > result.maximum) { result.maximum = error; result.maximumFrame = i; }
        result.rms += error * error;
        result.peak = std::max (result.peak, std::abs (static_cast<double> (expected.samples[i])));
    }
    result.rms = std::sqrt (result.rms / static_cast<double> (std::max<std::size_t> (1, comparedFrames)));
    // Compare raw sample positions and host latency independently. An accidental
    // latency change must not be hidden by aligning otherwise similar waves.
    result.passed = result.maximum <= 2.0e-6 && result.peak > 1.0e-4
                    && expected.latency == actual.latency;
    return result;
}

inline bool applyFixture (juce::AudioPluginInstance& plugin, int fixture)
{
    if (fixture == 0) return true; // Untouched state from the historical binary.
    const std::array<std::array<float, 19>, 3> settings {{
        {{ .82f, .26f, .34f, .11f, .51f, .54f, .25f, 0.f, 0.f, .21f, .60f,
           1.f, .50f, 0.f, .30f, .21f, .50f, 1.f, 0.f }},
        {{ .93f, .72f, .91f, .72f, .30f, .33f, .50f, 1.f, 1.f, .38f, .52f,
           .75f, .25f, .50f, .74f, .86f, 1.f, 0.f, .50f }},
        {{ .46f, .39f, .17f, .05f, .88f, .72f, .75f, 1.f, 0.f, .05f, .70f,
           .25f, 1.f, 0.f, .50f, .42f, 0.f, 1.f, 1.f }}
    }};
    bool passed = true;
    for (std::size_t index = 0; index < soundParameterNames.size(); ++index)
        passed = setParameter (plugin, soundParameterNames[index],
                               settings[static_cast<std::size_t> (fixture - 1)][index], index >= 16) && passed;
    return passed;
}

struct ParameterSnapshot
{
    juce::String name, id;
    int index = 0, steps = 0;
    float value = 0.0f;
};

inline std::vector<ParameterSnapshot> snapshotParameters (juce::AudioPluginInstance& plugin)
{
    std::vector<ParameterSnapshot> result;
    for (const auto* name : soundParameterNames)
        if (auto* parameter = parameterNamed (plugin, name))
            result.push_back ({ name, parameterId (*parameter), parameter->getParameterIndex(),
                                parameter->getNumSteps(), parameter->getValue() });
    return result;
}

inline bool verifyParameters (const std::vector<ParameterSnapshot>& original, juce::AudioPluginInstance& restored)
{
    bool passed = true;
    for (const auto& before : original)
    {
        auto* after = parameterNamed (restored, before.name);
        const bool matches = after != nullptr && before.id.isNotEmpty()
            && before.id == parameterId (*after)
            && before.index == after->getParameterIndex()
            && before.steps == after->getNumSteps()
            && std::abs (before.value - after->getValue()) < 1.0e-6f;
        if (! matches)
        {
            std::cerr << "Parameter identity/value changed: " << before.name
                      << " old=" << before.value
                      << " new=" << (after != nullptr ? after->getValue() : -1.f) << '\n';
            passed = false;
        }
    }
    return passed;
}
} // namespace compatibility

inline bool testPluginCompatibility (const juce::String& oldPath, const juce::String& newPath)
{
    using namespace compatibility;
    juce::MessageManager::getInstance();
    const auto oldIds = moduleClassIds (juce::File (oldPath));
    const auto newIds = moduleClassIds (juce::File (newPath));
    if (oldIds.isEmpty() || oldIds != newIds)
    {
        std::cerr << "VST3 component/controller identity differs or moduleinfo is missing\n";
        return false;
    }
    juce::VST3PluginFormatHeadless format;
    juce::OwnedArray<juce::PluginDescription> oldDescriptions, newDescriptions;
    format.findAllTypesForFile (oldDescriptions, oldPath);
    format.findAllTypesForFile (newDescriptions, newPath);
    if (oldDescriptions.size() != 1 || newDescriptions.size() != 1)
    {
        std::cerr << "Expected one instrument in each VST3 bundle\n";
        return false;
    }
    std::cout << "Historical VST3 compatibility: " << oldDescriptions[0]->version
              << " -> " << newDescriptions[0]->version << "\nComponent/controller IDs: "
              << oldIds.joinIntoString (", ") << '\n';
    const auto instantiate = [&] (const juce::PluginDescription& description, double rate, int block)
    {
        juce::String error;
        auto result = format.createInstanceFromDescription (description, rate, block, error);
        if (result == nullptr) std::cerr << "Could not instantiate " << description.version << ": " << error << '\n';
        return result;
    };

    int passed = 0, failed = 0;
    double worst = 0.0;
    for (int fixture = 0; fixture < 4; ++fixture)
        for (const auto rate : { 44100.0, 48000.0, 96000.0 })
            for (const auto block : { 64, 257, 1024 })
            {
                auto original = instantiate (*oldDescriptions[0], rate, block);
                auto migrated = instantiate (*newDescriptions[0], rate, block);
                auto reopened = instantiate (*newDescriptions[0], rate, block);
                if (original == nullptr || migrated == nullptr || reopened == nullptr) return false;
                if (! applyFixture (*original, fixture)) return false;
                juce::MemoryBlock historicalState;
                original->getStateInformation (historicalState);
                if (historicalState.isEmpty() || ! processorState (historicalState).hasType ("YoiBoiState"))
                {
                    std::cerr << "Historical binary did not return readable real processor state\n";
                    return false;
                }
                // Reopen the reference too: compare how the very same saved
                // project plays in its original release and after updating.
                original->setStateInformation (historicalState.getData(), static_cast<int> (historicalState.getSize()));
                const auto originalParameters = snapshotParameters (*original);
                migrated->setStateInformation (historicalState.getData(), static_cast<int> (historicalState.getSize()));
                const auto firstParameterCheck = verifyParameters (originalParameters, *migrated);
                juce::MemoryBlock savedMigratedState;
                migrated->getStateInformation (savedMigratedState);
                const auto oldPatch = processorState (historicalState).getChildWithName ("YoiBoiPatch");
                const auto expectedEngine = oldPatch.hasProperty ("soundEngine")
                    ? static_cast<int> (oldPatch.getProperty ("soundEngine"))
                    : oldPatch.hasProperty ("subMode") ? 3 : oldPatch.hasProperty ("tempoSync") ? 2 : 1;
                const auto migratedPatch = processorState (savedMigratedState).getChildWithName ("YoiBoiPatch");
                const bool engineSaved = static_cast<int> (migratedPatch.getProperty ("soundEngine", -1)) == expectedEngine;
                if (! engineSaved)
                    std::cerr << "Saved compatibility engine missing/wrong: expected " << expectedEngine
                              << ", got " << migratedPatch.getProperty ("soundEngine", -1).toString() << '\n';
                reopened->setStateInformation (savedMigratedState.getData(), static_cast<int> (savedMigratedState.getSize()));
                const auto secondParameterCheck = verifyParameters (originalParameters, *reopened);
                juce::MemoryBlock resavedState;
                reopened->getStateInformation (resavedState);
                const bool engineReopened = static_cast<int> (processorState (resavedState)
                    .getChildWithName ("YoiBoiPatch").getProperty ("soundEngine", -1)) == expectedEngine;

                const bool automate = fixture == 2;
                // The slow case also exercises historical maximum-length
                // clamps, which changed between the native-rate engines.
                const auto tempo = fixture == 3 ? 20.0 : 142.0;
                const auto reference = render (*original, rate, block, automate, tempo);
                const auto updated = render (*migrated, rate, block, automate, tempo);
                const auto fromNewSave = render (*reopened, rate, block, automate, tempo);
                // Engine 3: compare up to the CC123 frame, then require a release.
                const auto cc123Frame = static_cast<std::size_t> (juce::roundToInt (allNotesOffSeconds * rate));
                const auto comparedFrames = expectedEngine >= 3 ? cc123Frame : reference.samples.size();
                const auto initialDifference = compare (reference, updated, comparedFrames);
                const auto reopenedDifference = compare (reference, fromNewSave, comparedFrames);
                const auto peakIn = [] (const RenderedAudio& audio, std::size_t begin, std::size_t end)
                {
                    double peak = 0.0;
                    for (auto i = begin; i < std::min (end, audio.samples.size()); ++i)
                        peak = std::max (peak, std::abs (static_cast<double> (audio.samples[i])));
                    return peak;
                };
                bool releaseOk = true;
                double releasePeak = 0.0, preReleasePeak = 0.0, referencePostPeak = 0.0;
                if (expectedEngine >= 3)
                {
                    const auto window = static_cast<std::size_t> (juce::roundToInt (0.05 * rate));
                    preReleasePeak = peakIn (updated, cc123Frame - window, cc123Frame);
                    releasePeak = std::max (peakIn (updated, cc123Frame, updated.samples.size()),
                                            peakIn (fromNewSave, cc123Frame, fromNewSave.samples.size()));
                    referencePostPeak = peakIn (reference, cc123Frame, reference.samples.size());
                    const auto lastTail = peakIn (updated, updated.samples.size() - window, updated.samples.size());
                    releaseOk = releasePeak <= preReleasePeak * 1.25 + 1.0e-6 && lastTail <= preReleasePeak + 1.0e-6;
                }
                const bool ok = firstParameterCheck && secondParameterCheck && engineSaved && engineReopened
                    && initialDifference.passed && reopenedDifference.passed && releaseOk;
                ok ? ++passed : ++failed;
                worst = std::max ({ worst, initialDifference.maximum, reopenedDifference.maximum });
                std::cout << (ok ? "PASS" : "FAIL") << " fixture=" << fixture << " rate=" << rate
                          << " block=" << block << " automated=" << automate
                          << " bpm=" << tempo
                          << " engine=" << expectedEngine
                          << " latency=" << reference.latency << '/' << updated.latency << '/' << fromNewSave.latency
                          << std::scientific << " max=" << initialDifference.maximum
                          << " rms=" << initialDifference.rms
                          << " reopen-max=" << reopenedDifference.maximum
                          << " peak=" << initialDifference.peak << std::defaultfloat;
                if (expectedEngine >= 3)
                    std::cout << " compared-frames=" << comparedFrames << "/" << reference.samples.size()
                              << std::scientific << " cc123-old-tail=" << referencePostPeak
                              << " cc123-new-tail=" << releasePeak << " pre-cc123=" << preReleasePeak
                              << std::defaultfloat << (releaseOk ? " release=ok" : " release=FAIL");
                if (! ok) std::cout << " worst-frame=" << initialDifference.maximumFrame;
                std::cout << std::endl;
            }
    std::cout << "Historical compatibility result: " << passed << " passed, " << failed
              << " failed; worst sample error=" << std::scientific << worst << std::defaultfloat << '\n';
    return passed == 36 && failed == 0;
}
} // namespace yoiboi
