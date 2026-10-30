#include "YoiBoiProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace yoiboi
{
namespace
{
using APVTS = juce::AudioProcessorValueTreeState;
using Float = juce::AudioParameterFloat;
using Choice = juce::AudioParameterChoice;
using Bool = juce::AudioParameterBool;

constexpr std::array<const char*, 5> poseNames { "EE", "EH", "AH", "OH", "OO" };
constexpr std::array<const char*, 12> legacyFactoryNames {
    "Rubber Duck", "Bubble Trouble", "Elastic Yoi", "Chrome Chew", "Tin Goblin", "Laser Teeth",
    "Low Talker", "Throat Coat", "Bog Creature", "Pocket Gremlin", "Squeaky Clean", "Tiny Terror"
};
struct ParameterProperty { const char* id; const char* property; int group; };
constexpr std::array<ParameterProperty, 45> parameterProperties {{
    { "mouth", "mouth", 0 }, { "size", "size", 1 }, { "bite", "bite", 2 }, { "dirt", "dirt", 3 },
    { "sub", "sub", 5 }, { "length_ms", "freeLengthMilliseconds", 4 },
    { "length_division", "lengthDivision", 4 }, { "sync", "tempoSync", -1 },
    { "loop", "loop", -1 }, { "glide_ms", "glideMilliseconds", -1 }, { "output_db", "outputDecibels", -1 },
    { "start_pose", "startPose", 0 }, { "body_pose", "bodyPose", 0 }, { "end_pose", "endPose", 0 },
    { "body_time", "bodyTime", 4 }, { "curve", "curve", 4 }, { "sub_mode", "subMode", 5 },
    { "low_cut", "lowCut", 5 }, { "length_feel", "lengthFeel", 4 },
    { "fmRatio", "fmRatio", 2 }, { "fmIndex", "fmIndex", 2 }, { "resonance", "resonance", 2 },
    // 0.2.0. Performance settings (-1) are never randomized; the rest follow
    // the padlock of the control they shape.
    { "talk_mode", "talkMode", -1 }, { "talk_rate", "talkDivision", -1 }, { "talk_feel", "talkFeel", -1 },
    { "bend_range", "bendRange", -1 }, { "glide_mode", "glideMode", -1 },
    { "pitch_env", "pitchEnvSemitones", 2 }, { "pitch_env_ms", "pitchEnvMilliseconds", 2 },
    { "width", "width", -1 }, { "sub_reset", "subPhaseReset", -1 },
    { "shaper", "shaper", 3 }, { "squash", "squash", 3 },
    { "vowel_start", "vowelStart", 0 }, { "vowel_body", "vowelBody", 0 }, { "vowel_end", "vowelEnd", 0 },
    { "onset_glide", "onsetGlide", 0 }, { "consonant", "consonant", 0 }, { "consonant_level", "consonantLevel", 0 },
    { "vel_mouth", "velocityToMouth", -1 }, { "vel_bite", "velocityToBite", -1 }, { "vel_dirt", "velocityToDirt", -1 },
    { "attack_ms", "attackMilliseconds", -1 }, { "release_ms", "releaseMilliseconds", -1 },
    { "meter_bars", "meterBars", -1 }
}};

juce::StringArray vowelChoices()
{
    juce::StringArray result;
    for (int i = 0; i < vowelCount; ++i) result.add (vowelName (i));
    return result;
}

juce::StringArray poseChoices()
{
    juce::StringArray result;
    for (const auto* name : poseNames)
        result.add (name);
    return result;
}

float patchLengthSeconds (float sync, float division, float freeMilliseconds, float feel) noexcept
{
    if (sync > 0.5f)
    {
        const auto index = static_cast<size_t> (std::clamp (static_cast<int> (division), 0, 4));
        return syncedBeats[index] * 0.5f * lengthFeelMultiplier (feel); // 120 BPM reference.
    }
    return freeMilliseconds * 0.001f;
}

juce::String familyLabel (const Patch& patch) { return familyName (patch.family); }

juce::String favoriteKey (const Patch& patch)
{
    return patch.presetId.isNotEmpty() ? patch.presetId : "generated:" + patch.name;
}

bool sameSound (const Patch& a, const Patch& b) noexcept
{
    const auto close = [] (float x, float y) { return std::abs (x - y) < 0.00001f; };
    return a.soundEngine == b.soundEngine && close (a.mouth, b.mouth) && close (a.size, b.size) && close (a.bite, b.bite)
        && close (a.dirt, b.dirt) && close (a.lengthSeconds, b.lengthSeconds) && close (a.sub, b.sub)
        && a.startPose == b.startPose && a.bodyPose == b.bodyPose && a.endPose == b.endPose
        && close (a.bodyTime, b.bodyTime) && close (a.curve, b.curve) && close (a.fmRatio, b.fmRatio)
        && close (a.fmIndex, b.fmIndex) && close (a.resonance, b.resonance)
        && a.tempoSync == b.tempoSync && a.loop == b.loop && a.lengthDivision == b.lengthDivision
        && a.lengthFeel == b.lengthFeel && a.subMode == b.subMode && a.lowCut == b.lowCut
        && close (a.freeLengthMilliseconds, b.freeLengthMilliseconds)
        && close (a.glideMilliseconds, b.glideMilliseconds) && close (a.outputDecibels, b.outputDecibels)
        && a.talkMode == b.talkMode && a.talkDivision == b.talkDivision && a.talkFeel == b.talkFeel
        && a.bendRange == b.bendRange && a.glideMode == b.glideMode
        && close (a.pitchEnvSemitones, b.pitchEnvSemitones) && close (a.pitchEnvMilliseconds, b.pitchEnvMilliseconds)
        && close (a.width, b.width) && a.subPhaseReset == b.subPhaseReset && a.shaper == b.shaper
        && close (a.squash, b.squash) && a.vowelStart == b.vowelStart && a.vowelBody == b.vowelBody
        && a.vowelEnd == b.vowelEnd && a.onsetGlide == b.onsetGlide && a.consonant == b.consonant
        && close (a.consonantLevel, b.consonantLevel) && close (a.velocityToMouth, b.velocityToMouth)
        && close (a.velocityToBite, b.velocityToBite) && close (a.velocityToDirt, b.velocityToDirt)
        && close (a.attackMilliseconds, b.attackMilliseconds) && close (a.releaseMilliseconds, b.releaseMilliseconds)
        && a.meterBars == b.meterBars;
}

const Patch* factoryByName (const juce::String& name)
{
    for (const auto& preset : allFactoryPresets())
        if (preset.name == name) return &preset;
    return nullptr;
}

// Undo entries differ when the sound or the recalled identity differs. A
// repeated preset load or reset that changes neither must not add a step.
bool sameHistoryEntry (const Patch& a, const Patch& b) noexcept
{
    return sameSound (a, b) && a.name == b.name && a.presetId == b.presetId;
}

// The editor passes its upper-case combo text ("RUBBER"). Store and restore
// one canonical spelling so sessions compare and recall it reliably.
juce::String canonicalFamilyName (const juce::String& requested)
{
    const auto text = requested.trim();
    for (int i = 0; i < 4; ++i)
        if (text.equalsIgnoreCase (familyName (static_cast<Family> (i))))
            return familyName (static_cast<Family> (i));
    return "All Families";
}

void holdPeak (std::atomic<float>& meter, float value) noexcept
{
    auto held = meter.load (std::memory_order_relaxed);
    while (value > held
           && ! meter.compare_exchange_weak (held, value, std::memory_order_release, std::memory_order_relaxed))
    {
    }
}

float safeParameterValue (const juce::var& value, float fallback)
{
    if (value.isDouble() || value.isInt() || value.isInt64() || value.isBool())
    {
        const auto number = static_cast<float> (value);
        return std::isfinite (number) ? number : fallback;
    }
    if (! value.isString()) return fallback;
    const auto text = value.toString().trim();
    int offset = 0;
    if (text[offset] == '-' || text[offset] == '+') ++offset;
    const auto digits = [&]
    {
        const auto begin = offset;
        while (text[offset] >= '0' && text[offset] <= '9') ++offset;
        return offset - begin;
    };
    auto count = digits();
    if (text[offset] == '.') { ++offset; count += digits(); }
    if (count == 0) return fallback;
    if (text[offset] == 'e' || text[offset] == 'E')
    {
        ++offset;
        if (text[offset] == '-' || text[offset] == '+') ++offset;
        if (digits() == 0) return fallback;
    }
    const auto number = text.getFloatValue();
    return offset == text.length() && std::isfinite (number) ? number : fallback;
}
} // namespace

APVTS::ParameterLayout YoiBoiProcessor::createParameterLayout()
{
    APVTS::ParameterLayout p;
    const auto initial = makeDefaultPatch();
    const auto normal = juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f);
    p.add (std::make_unique<Float> (juce::ParameterID { "mouth", 1 }, "Mouth", normal, initial.mouth));
    p.add (std::make_unique<Float> (juce::ParameterID { "size", 1 }, "Size", normal, initial.size));
    p.add (std::make_unique<Float> (juce::ParameterID { "bite", 1 }, "Bite", normal, initial.bite));
    p.add (std::make_unique<Float> (juce::ParameterID { "dirt", 1 }, "Dirt", normal, initial.dirt));
    p.add (std::make_unique<Float> (juce::ParameterID { "sub", 1 }, "Sub", normal, initial.sub));
    p.add (std::make_unique<Float> (juce::ParameterID { "length_ms", 1 }, "Free Length", juce::NormalisableRange<float> (80.0f, 2000.0f, 1.0f, 0.5f), 280.0f));
    p.add (std::make_unique<Choice> (juce::ParameterID { "length_division", 1 }, "Synced Length", juce::StringArray { "1/16", "1/8", "1/4", "1/2", "1 Bar" }, 1));
    p.add (std::make_unique<Bool> (juce::ParameterID { "sync", 1 }, "Sync", true));
    p.add (std::make_unique<Bool> (juce::ParameterID { "loop", 1 }, "Loop", false));
    p.add (std::make_unique<Float> (juce::ParameterID { "glide_ms", 1 }, "Glide", juce::NormalisableRange<float> (0.0f, 500.0f, 1.0f, 0.5f), 24.0f));
    p.add (std::make_unique<Float> (juce::ParameterID { "output_db", 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), -6.0f));
    p.add (std::make_unique<Choice> (juce::ParameterID { "start_pose", 1 }, "Start Vowel", poseChoices(), initial.startPose));
    p.add (std::make_unique<Choice> (juce::ParameterID { "body_pose", 1 }, "Body Vowel", poseChoices(), initial.bodyPose));
    p.add (std::make_unique<Choice> (juce::ParameterID { "end_pose", 1 }, "End Vowel", poseChoices(), 0));
    p.add (std::make_unique<Float> (juce::ParameterID { "body_time", 1 }, "Body Time", juce::NormalisableRange<float> (0.2f, 0.8f, 0.001f), initial.bodyTime));
    p.add (std::make_unique<Float> (juce::ParameterID { "curve", 1 }, "Curve", normal, initial.curve));
    // Append parameters so existing sessions retain every original index/ID.
    p.add (std::make_unique<Choice> (juce::ParameterID { "sub_mode", 1 }, "Sub Routing", juce::StringArray { "Off", "Direct", "Driven" }, 1));
    p.add (std::make_unique<Bool> (juce::ParameterID { "low_cut", 1 }, "Body Low Cut", true));
    p.add (std::make_unique<Choice> (juce::ParameterID { "length_feel", 1 }, "Length Feel", juce::StringArray { "Straight", "Triplet", "Dotted" }, 0));
    // 0.2.0 (sound engine 4). Appended after every original parameter; older
    // engines ignore them, so 0.1.x sessions render exactly as before.
    const auto ms = [] (float low, float high, float skew, float step) { return juce::NormalisableRange<float> (low, high, step, skew); };
    p.add (std::make_unique<Choice> (juce::ParameterID { "talk_mode", 1 }, "Talk Mode", juce::StringArray { "Per Note", "Synced Talk", "Mod Wheel", "Aftertouch" }, initial.talkMode));
    p.add (std::make_unique<Choice> (juce::ParameterID { "talk_rate", 1 }, "Talk Rate", juce::StringArray { "1/16", "1/8", "1/4", "1/2", "1 Bar" }, initial.talkDivision));
    p.add (std::make_unique<Choice> (juce::ParameterID { "talk_feel", 1 }, "Talk Feel", juce::StringArray { "Straight", "Triplet", "Dotted" }, initial.talkFeel));
    p.add (std::make_unique<Float> (juce::ParameterID { "bend_range", 1 }, "Bend Range", juce::NormalisableRange<float> (1.0f, 24.0f, 1.0f), static_cast<float> (initial.bendRange)));
    p.add (std::make_unique<Choice> (juce::ParameterID { "glide_mode", 1 }, "Glide Mode", juce::StringArray { "Always", "Legato" }, initial.glideMode));
    p.add (std::make_unique<Float> (juce::ParameterID { "pitch_env", 1 }, "Pitch Drop", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), initial.pitchEnvSemitones));
    p.add (std::make_unique<Float> (juce::ParameterID { "pitch_env_ms", 1 }, "Pitch Drop Time", ms (5.0f, 2000.0f, 0.4f, 1.0f), initial.pitchEnvMilliseconds));
    p.add (std::make_unique<Float> (juce::ParameterID { "width", 1 }, "Body Width", normal, initial.width));
    p.add (std::make_unique<Bool> (juce::ParameterID { "sub_reset", 1 }, "Sub Phase Reset", initial.subPhaseReset));
    p.add (std::make_unique<Choice> (juce::ParameterID { "shaper", 1 }, "Shaper", juce::StringArray { "Classic", "Diode", "Fold", "Crush" }, initial.shaper));
    p.add (std::make_unique<Float> (juce::ParameterID { "squash", 1 }, "Squash", normal, initial.squash));
    p.add (std::make_unique<Choice> (juce::ParameterID { "vowel_start", 1 }, "Vowel Start", vowelChoices(), initial.vowelStart));
    p.add (std::make_unique<Choice> (juce::ParameterID { "vowel_body", 1 }, "Vowel Body", vowelChoices(), initial.vowelBody));
    p.add (std::make_unique<Choice> (juce::ParameterID { "vowel_end", 1 }, "Vowel End", vowelChoices(), initial.vowelEnd));
    p.add (std::make_unique<Choice> (juce::ParameterID { "onset_glide", 1 }, "Onset Glide", juce::StringArray { "None", "W", "Y" }, initial.onsetGlide));
    p.add (std::make_unique<Choice> (juce::ParameterID { "consonant", 1 }, "Consonant", juce::StringArray { "None", "T", "K", "CH" }, initial.consonant));
    p.add (std::make_unique<Float> (juce::ParameterID { "consonant_level", 1 }, "Consonant Level", normal, initial.consonantLevel));
    p.add (std::make_unique<Float> (juce::ParameterID { "vel_mouth", 1 }, "Velocity to Mouth", normal, initial.velocityToMouth));
    p.add (std::make_unique<Float> (juce::ParameterID { "vel_bite", 1 }, "Velocity to Bite", normal, initial.velocityToBite));
    p.add (std::make_unique<Float> (juce::ParameterID { "vel_dirt", 1 }, "Velocity to Dirt", normal, initial.velocityToDirt));
    p.add (std::make_unique<Float> (juce::ParameterID { "attack_ms", 1 }, "Attack", ms (0.5f, 200.0f, 0.35f, 0.1f), initial.attackMilliseconds));
    p.add (std::make_unique<Float> (juce::ParameterID { "release_ms", 1 }, "Release", ms (5.0f, 2000.0f, 0.35f, 1.0f), initial.releaseMilliseconds));
    p.add (std::make_unique<Bool> (juce::ParameterID { "meter_bars", 1 }, "Bars Follow Meter", initial.meterBars));
    return p;
}

YoiBoiProcessor::YoiBoiProcessor (juce::File presetDirectory)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       // Optional: hosts that enable it get the dedicated sub separately.
                                       .withOutput ("Sub", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout()),
      presetRoot (std::move (presetDirectory)),
      currentPatch (makeDefaultPatch())
{
    for (std::size_t i = 0; i < parameterIds.size(); ++i)
        rawParameters[i] = apvts.getRawParameterValue (parameterIds[i]);
    setLatencySamples (dsp::YoiVoice::latencySamples());
    // A new instance starts on the reworked signature sound (engine 4). A
    // session or preset recalled afterwards replaces it completely.
    if (const auto* initial = factoryByName ("Tape Bounce II")) applyPatch (*initial, false);
    currentPatch = livePatch();
    publishHiddenControls (currentPatch);
    history.clear();
    history.push_back (currentPatch);
    historyIndex = 0;
    rescanUserPresets();
}

YoiBoiProcessor::~YoiBoiProcessor()
{
    bounceRenderer.cancel();
    bounceRenderer.waitUntilFinished (15000);
}

bool YoiBoiProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto output = layout.getMainOutputChannelSet();
    if (! layout.getMainInputChannelSet().isDisabled()
        || ! (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo()))
        return false;
    for (int bus = 1; bus < layout.outputBuses.size(); ++bus)
    {
        const auto sub = layout.getChannelSet (false, bus);
        if (! (sub.isDisabled() || sub == juce::AudioChannelSet::mono() || sub == juce::AudioChannelSet::stereo()))
            return false;
    }
    return true;
}

void YoiBoiProcessor::processorLayoutsChanged()
{
    const auto* sub = getBus (false, 1);
    subBusEnabled.store (sub != nullptr && sub->isEnabled() && sub->getNumberOfChannels() > 0, std::memory_order_release);
}

void YoiBoiProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = std::isfinite (sampleRate) && sampleRate >= 8000.0
                      ? std::min (sampleRate, 768000.0) : 48000.0;
    bounceSampleRate.store (currentSampleRate, std::memory_order_release);
    waveformStride = std::max (1, static_cast<int> (currentSampleRate / 375.0));
    waveformCountdown = 0;
    waveformWriteIndex = 0;
    publishedWaveformIndex.store (0, std::memory_order_relaxed);
    for (auto& sample : waveformSamples) sample.store (0.0f, std::memory_order_relaxed);
    voice.prepare (currentSampleRate);
    allNotesOff();
    bypassActive = false;
    processorLayoutsChanged();
    meterLeft.store (0.0f); meterRight.store (0.0f);
}

void YoiBoiProcessor::releaseResources() { allNotesOff(); }

float YoiBoiProcessor::raw (const char* id) const noexcept
{
    // APVTS raw pointers stay valid across state replacement. Rendering reads
    // parameter atomics without taking the patch/history lock.
    for (std::size_t i = 0; i < parameterIds.size(); ++i)
        if (std::strcmp (id, parameterIds[i]) == 0)
            return rawParameters[i]->load (std::memory_order_relaxed);
    return 0.0f;
}

std::array<float, 128> YoiBoiProcessor::recentWaveform() const noexcept
{
    std::array<float, 128> result {};
    const auto oldest = publishedWaveformIndex.load (std::memory_order_acquire);
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = waveformSamples[(oldest + i) % result.size()].load (std::memory_order_relaxed);
    return result;
}

void YoiBoiProcessor::publishHiddenControls (const Patch& patch) noexcept
{
    soundEngine.store (patch.soundEngine, std::memory_order_release);
    setLatencySamples (dsp::CompatibleVoice::latencyFor (patch.soundEngine));
    fmRatio.store (patch.fmRatio, std::memory_order_release);
    fmIndex.store (patch.fmIndex, std::memory_order_release);
    resonance.store (patch.resonance, std::memory_order_release);
}

void YoiBoiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();
    buffer.clear();
    bypassActive = false;
    if (numSamples <= 0 || buffer.getNumChannels() == 0)
    {
        // Nothing renders, but note and controller state must still follow
        // the host: a dropped note-off here would leave a stuck note.
        for (const auto metadata : midi)
            if (metadata.numBytes <= 3) handleMidi (metadata.getMessage());
        midi.clear();
        return;
    }

    ParameterValues values {};
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = rawParameters[i]->load (std::memory_order_relaxed);
    HostTiming timing;
    if (auto* playhead = getPlayHead())
        if (auto position = playhead->getPosition())
        {
            if (auto hostBpm = position->getBpm())
                if (std::isfinite (*hostBpm) && *hostBpm > 0.0) { timing.bpm = *hostBpm; timing.hasBpm = true; }
            if (auto ppq = position->getPpqPosition()) { timing.ppq = *ppq; timing.hasPpq = std::isfinite (*ppq); }
            if (auto signature = position->getTimeSignature())
            { timing.numerator = signature->numerator; timing.denominator = signature->denominator; }
            timing.playing = position->getIsPlaying();
        }
    lastHostBpm.store (timing.hasBpm ? timing.bpm : 120.0, std::memory_order_relaxed);
    const int engine = soundEngine.load (std::memory_order_acquire);
    const bool latest = engine >= Patch::firstExtendedEngine;
    auto c = controlsFor (values, timing, engine, currentSampleRate,
                          fmRatio.load (std::memory_order_acquire), fmIndex.load (std::memory_order_acquire),
                          resonance.load (std::memory_order_acquire));
    auto subBuffer = latest && subBusEnabled.load (std::memory_order_acquire) && getBusCount (false) > 1
                   ? getBusBuffer (buffer, false, 1) : juce::AudioBuffer<float>();
    c.separateSub = subBuffer.getNumChannels() > 0;
    voice.setControls (c, engine);
    if (latest)
    {
        // Engine 4: Bend Range rescales the held wheel, and the scrub source
        // follows the selected talk mode.
        const int range = static_cast<int> (std::lround (values[pBendRange]));
        if (range != appliedBendRange || engine != lastEngine)
        {
            appliedBendRange = range;
            voice.setPitchBend (wheelPosition * static_cast<float> (range));
        }
        if (c.talkMode == 2) voice.setTalkScrub (modWheel);
        else if (c.talkMode == 3) voice.setTalkScrub (pressure);
    }
    else if (lastEngine >= Patch::firstExtendedEngine)
        voice.setPitchBend (wheelPosition * 2.0f); // back on a fixed +/-2 engine
    lastEngine = engine;

    const auto preview = previewCommand.exchange (0, std::memory_order_acq_rel);
    if (preview == 2 && previewVoiceActive) stopPreview();
    else if (preview == 1 && activeNote < 0 && ! previewVoiceActive) startPreview();

    auto mainBuffer = getBusBuffer (buffer, false, 0);
    const int channels = mainBuffer.getNumChannels();
    auto* left = channels > 0 ? mainBuffer.getWritePointer (0) : buffer.getWritePointer (0);
    auto* right = channels > 1 ? mainBuffer.getWritePointer (1) : nullptr;
    auto* subLeft = subBuffer.getNumChannels() > 0 ? subBuffer.getWritePointer (0) : nullptr;
    auto* subRight = subBuffer.getNumChannels() > 1 ? subBuffer.getWritePointer (1) : nullptr;
    float peakLeft = 0.0f, peakRight = 0.0f;
    auto render = [&] (int begin, int end)
    {
        for (int sample = begin; sample < end; ++sample)
        {
            if (previewVoiceActive) advancePreview();
            const auto frame = voice.renderFrame();
            // Engines 1-3 are mono: both sides carry the identical sample.
            left[sample] = right != nullptr || ! latest ? frame.left : 0.5f * (frame.left + frame.right);
            if (right != nullptr) right[sample] = frame.right;
            if (subLeft != nullptr) subLeft[sample] = frame.sub;
            if (subRight != nullptr) subRight[sample] = frame.sub;
            peakLeft = std::max (peakLeft, std::abs (left[sample]));
            peakRight = std::max (peakRight, std::abs (right != nullptr ? frame.right : left[sample]));
            if (--waveformCountdown <= 0)
            {
                waveformSamples[waveformWriteIndex].store (frame.left, std::memory_order_relaxed);
                waveformWriteIndex = (waveformWriteIndex + 1) % waveformSamples.size();
                publishedWaveformIndex.store (waveformWriteIndex, std::memory_order_release);
                waveformCountdown = waveformStride;
            }
        }
    };

    int cursor = 0;
    for (const auto metadata : midi)
    {
        const int eventSample = std::clamp (metadata.samplePosition, cursor, numSamples);
        render (cursor, eventSample);
        if (metadata.numBytes <= 3) handleMidi (metadata.getMessage());
        cursor = eventSample;
    }
    render (cursor, numSamples);
    midi.clear();
    holdPeak (meterLeft, peakLeft);
    holdPeak (meterRight, peakRight);
    displayedGesturePhase.store (voice.gesturePhase(), std::memory_order_release);
    midiActivityFramesLeft = std::max (0, midiActivityFramesLeft - numSamples);
    midiIndicator.store (midiActivityFramesLeft > 0, std::memory_order_release);
}

void YoiBoiProcessor::startPreview() noexcept
{
    // One bar of the preview phrase at the host tempo (120 BPM without one).
    const double bpm = std::clamp (lastHostBpm.load (std::memory_order_relaxed), 40.0, 300.0);
    previewSixteenth = 60.0 / bpm / 4.0 * currentSampleRate;
    previewEnd = static_cast<juce::int64> (std::llround (phraseSixteenths * previewSixteenth));
    previewPosition = 0;
    previewStep = 0;
    previewNoteOffAt = -1;
    previewVoiceActive = true;
    previewIndicator.store (true, std::memory_order_release);
}

void YoiBoiProcessor::stopPreview() noexcept
{
    voice.noteOff();
    previewVoiceActive = false;
    previewNoteOffAt = -1;
    previewIndicator.store (false, std::memory_order_release);
}

void YoiBoiProcessor::advancePreview() noexcept
{
    const auto position = previewPosition++;
    if (position == previewNoteOffAt) voice.noteOff();
    if (previewStep < phraseSteps.size()
        && position == static_cast<juce::int64> (std::llround (phraseSteps[previewStep].start * previewSixteenth)))
    {
        const auto& step = phraseSteps[previewStep];
        voice.noteOn (step.note, step.velocity, rawAt (pGlideMs), true);
        previewNoteOffAt = position + std::max<juce::int64> (1, std::llround (step.length * previewSixteenth * 0.85));
        ++previewStep;
    }
    if (position >= previewEnd) stopPreview();
}

void YoiBoiProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (! bypassActive)
    {
        // Nothing renders while bypassed, so a held note could never see its
        // note-off. Release and silence everything on entry; the pedal keeps
        // its physical position.
        const bool pedal = sustainDown;
        allNotesOff();
        sustainDown = pedal;
        bypassActive = true;
    }
    // Keep controller state current without starting new notes. Note-offs
    // for keys pressed before bypass are harmless: those keys are released.
    for (const auto metadata : midi)
        if (metadata.numBytes <= 3)
        {
            const auto message = metadata.getMessage();
            if (! message.isNoteOn()) handleMidi (message);
        }
    midi.clear();
}

void YoiBoiProcessor::handleMidi (const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        noteOn (message.getNoteNumber(), message.getFloatVelocity());
        midiActivityFramesLeft = static_cast<int> (currentSampleRate * 0.12);
    }
    else if (message.isNoteOff()) noteOff (message.getNoteNumber());
    else if (message.isPitchWheel())
    {
        const float value = static_cast<float> (message.getPitchWheelValue()) - 8192.0f;
        wheelPosition = value / 8192.0f;
        if (voice.activeEngine() >= Patch::firstExtendedEngine)
            voice.setPitchBend (value * (static_cast<float> (appliedBendRange) / 8192.0f));
        else
            voice.setPitchBend (value * (2.0f / 8192.0f)); // engines 1-3: fixed +/-2, exactly as before
    }
    else if (message.isController() && message.getControllerNumber() == 1)
    {
        modWheel = static_cast<float> (message.getControllerValue()) / 127.0f;
        if (voice.activeEngine() >= Patch::firstExtendedEngine && static_cast<int> (rawAt (pTalkMode)) == 2)
            voice.setTalkScrub (modWheel);
    }
    else if (message.isChannelPressure() || message.isAftertouch())
    {
        const int amount = message.isChannelPressure() ? message.getChannelPressureValue() : message.getAfterTouchValue();
        if (message.isAftertouch() && message.getNoteNumber() != activeNote) return;
        pressure = static_cast<float> (amount) / 127.0f;
        if (voice.activeEngine() >= Patch::firstExtendedEngine && static_cast<int> (rawAt (pTalkMode)) == 3)
            voice.setTalkScrub (pressure);
    }
    else if (message.isController() && message.getControllerNumber() == 64)
    {
        const bool down = message.getControllerValue() >= 64;
        if (sustainDown && ! down)
        {
            sustained.fill (false);
            updateActiveNote (false);
        }
        sustainDown = down;
    }
    else if (message.isAllSoundOff()) allNotesOff(); // CC120: immediate silence.
    else if (message.isAllNotesOff())
    {
        // CC123 releases notes so their tails finish, as hosts send it on
        // transport stop. The preserved 0.1.0-0.1.2 engines keep the hard cut
        // they always had, so those sounds render exactly as before.
        if (voice.activeEngine() < 3) allNotesOff();
        else releaseAllNotes();
    }
}

void YoiBoiProcessor::noteOn (int note, float velocity)
{
    if (note < 0 || note > 127) return;
    if (previewVoiceActive)
    {
        // External MIDI takes over: the phrase stops, the played note sounds.
        previewVoiceActive = false; previewNoteOffAt = -1;
        previewIndicator.store (false, std::memory_order_release);
    }
    const bool retrigger = activeNote < 0 || activeNote == note;
    for (int i = 0; i < noteOrderSize; ++i)
        if (noteOrder[static_cast<size_t> (i)] == note)
        {
            for (int j = i; j + 1 < noteOrderSize; ++j)
                noteOrder[static_cast<size_t> (j)] = noteOrder[static_cast<size_t> (j + 1)];
            --noteOrderSize; break;
        }
    if (noteOrderSize >= static_cast<int> (noteOrder.size()))
    {
        std::move (noteOrder.begin() + 1, noteOrder.end(), noteOrder.begin());
        --noteOrderSize;
    }
    noteOrder[static_cast<size_t> (noteOrderSize++)] = note;
    pressed[static_cast<size_t> (note)] = true;
    sustained[static_cast<size_t> (note)] = false;
    noteVelocities[static_cast<size_t> (note)] = velocity;
    activeNote = note;
    externalNotesIndicator.store (true, std::memory_order_release);
    voice.noteOn (note, velocity, raw ("glide_ms"), retrigger);
}

void YoiBoiProcessor::noteOff (int note)
{
    if (note < 0 || note > 127) return;
    if (! pressed[static_cast<size_t> (note)]) return;
    pressed[static_cast<size_t> (note)] = false;
    sustained[static_cast<size_t> (note)] = sustainDown;
    updateActiveNote (false);
}

void YoiBoiProcessor::updateActiveNote (bool retrigger)
{
    int next = -1;
    for (int i = noteOrderSize - 1; i >= 0; --i)
    {
        const int note = noteOrder[static_cast<size_t> (i)];
        if (pressed[static_cast<size_t> (note)] || sustained[static_cast<size_t> (note)])
        { next = note; break; }
    }
    if (next != activeNote)
    {
        activeNote = next;
        if (next >= 0)
            voice.noteOn (next, noteVelocities[static_cast<size_t> (next)], raw ("glide_ms"), retrigger);
        else voice.noteOff();
    }
    externalNotesIndicator.store (next >= 0, std::memory_order_release);
}

void YoiBoiProcessor::allNotesOff()
{
    pressed.fill (false); sustained.fill (false); noteOrderSize = 0;
    activeNote = -1; sustainDown = false; previewVoiceActive = false;
    previewNoteOffAt = -1; previewCommand.store (0); voice.reset();
    midiActivityFramesLeft = 0; midiIndicator.store (false);
    displayedGesturePhase.store (0.0f);
    externalNotesIndicator.store (false); previewIndicator.store (false);
}

void YoiBoiProcessor::releaseAllNotes()
{
    // Like a key-up on every note, including pedal-held ones: the voice runs
    // its normal release instead of being reset. The pedal position is kept.
    pressed.fill (false); sustained.fill (false); noteOrderSize = 0;
    activeNote = -1;
    previewVoiceActive = false; previewNoteOffAt = -1;
    voice.noteOff();
    externalNotesIndicator.store (false, std::memory_order_release);
    previewIndicator.store (false, std::memory_order_release);
}

void YoiBoiProcessor::setParameterValue (const char* id, float value)
{
    if (auto* parameter = apvts.getParameter (id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
}

Patch YoiBoiProcessor::livePatch() const
{
    const juce::ScopedLock lock (stateMutex);
    Patch patch = currentPatch;
    patch.mouth = raw ("mouth"); patch.size = raw ("size");
    patch.bite = raw ("bite"); patch.dirt = raw ("dirt");
    patch.sub = raw ("sub");
    patch.subMode = static_cast<int> (raw ("sub_mode")); patch.lowCut = raw ("low_cut") > 0.5f;
    patch.tempoSync = raw ("sync") > 0.5f;
    patch.loop = raw ("loop") > 0.5f;
    patch.freeLengthMilliseconds = raw ("length_ms");
    patch.lengthDivision = static_cast<int> (raw ("length_division"));
    patch.lengthFeel = static_cast<int> (raw ("length_feel"));
    patch.glideMilliseconds = raw ("glide_ms");
    patch.outputDecibels = raw ("output_db");
    patch.lengthSeconds = patchLengthSeconds (raw ("sync"), raw ("length_division"), raw ("length_ms"), raw ("length_feel"));
    patch.startPose = static_cast<int> (raw ("start_pose"));
    patch.bodyPose = static_cast<int> (raw ("body_pose"));
    patch.endPose = static_cast<int> (raw ("end_pose"));
    patch.bodyTime = raw ("body_time"); patch.curve = raw ("curve");
    patch.talkMode = static_cast<int> (rawAt (pTalkMode));
    patch.talkDivision = static_cast<int> (rawAt (pTalkRate));
    patch.talkFeel = static_cast<int> (rawAt (pTalkFeel));
    patch.bendRange = static_cast<int> (std::lround (rawAt (pBendRange)));
    patch.glideMode = static_cast<int> (rawAt (pGlideMode));
    patch.pitchEnvSemitones = rawAt (pPitchEnv);
    patch.pitchEnvMilliseconds = rawAt (pPitchEnvMs);
    patch.width = rawAt (pWidth);
    patch.subPhaseReset = rawAt (pSubReset) > 0.5f;
    patch.shaper = static_cast<int> (rawAt (pShaper));
    patch.squash = rawAt (pSquash);
    patch.vowelStart = static_cast<int> (rawAt (pVowelStart));
    patch.vowelBody = static_cast<int> (rawAt (pVowelBody));
    patch.vowelEnd = static_cast<int> (rawAt (pVowelEnd));
    patch.onsetGlide = static_cast<int> (rawAt (pOnsetGlide));
    patch.consonant = static_cast<int> (rawAt (pConsonant));
    patch.consonantLevel = rawAt (pConsonantLevel);
    patch.velocityToMouth = rawAt (pVelocityMouth);
    patch.velocityToBite = rawAt (pVelocityBite);
    patch.velocityToDirt = rawAt (pVelocityDirt);
    patch.attackMilliseconds = rawAt (pAttackMs);
    patch.releaseMilliseconds = rawAt (pReleaseMs);
    patch.meterBars = rawAt (pMeterBars) > 0.5f;
    return sanitizedPatch (std::move (patch));
}

void YoiBoiProcessor::pushHistory (const Patch& patch)
{
    if (historyIndex + 1 < static_cast<int> (history.size()))
        history.erase (history.begin() + historyIndex + 1, history.end());
    history.push_back (patch);
    if (history.size() > 50) history.erase (history.begin());
    historyIndex = static_cast<int> (history.size()) - 1;
}

void YoiBoiProcessor::preserveExcludedParameters (Patch& next, const Patch& previous) const
{
    const juce::ScopedLock lock (stateMutex);
    auto nextState = toValueTree (next);
    const auto oldState = toValueTree (previous);
    for (const auto& parameter : parameterProperties)
        if (parameterExclusions.contains (parameter.id))
            nextState.setProperty (parameter.property, oldState.getProperty (parameter.property), nullptr);
    if (const auto restored = fromValueTree (nextState)) next = *restored;
    next.lengthSeconds = patchLengthSeconds (next.tempoSync ? 1.f : 0.f,
        static_cast<float> (next.lengthDivision), next.freeLengthMilliseconds, static_cast<float> (next.lengthFeel));
}

// A locked or excluded control keeps its value through every preset load (user,
// 2026-10-28). A locked LENGTH also keeps SYNC, so the heard length can't change.
// Undo, redo, A/B and a restored project still set the sound exactly.
void YoiBoiProcessor::keepLockedSettings (Patch& next, const Patch& previous) const
{
    const juce::ScopedLock lock (stateMutex);
    auto nextState = toValueTree (next);
    const auto oldState = toValueTree (previous);
    const auto keep = [&] (const char* property) { nextState.setProperty (property, oldState.getProperty (property), nullptr); };
    for (const auto& parameter : parameterProperties)
        if (parameterExclusions.contains (parameter.id)
            || (parameter.group >= 0 && locks[static_cast<std::size_t> (parameter.group)]))
            keep (parameter.property);
    if (locks[4]) keep ("tempoSync");
    if (const auto restored = fromValueTree (nextState)) next = *restored;
    next.lengthSeconds = patchLengthSeconds (next.tempoSync ? 1.f : 0.f,
        static_cast<float> (next.lengthDivision), next.freeLengthMilliseconds, static_cast<float> (next.lengthFeel));
}

bool YoiBoiProcessor::isParameterLocked (const juce::String& id) const
{
    const juce::ScopedLock lock (stateMutex);
    for (const auto& parameter : parameterProperties)
        if (id == parameter.id)
            return parameterExclusions.contains (id)
                || (parameter.group >= 0 && locks[static_cast<std::size_t> (parameter.group)]);
    return false;
}

void YoiBoiProcessor::setParameterLock (const juce::String& id, bool excluded)
{
    const juce::ScopedLock lock (stateMutex);
    for (const auto& parameter : parameterProperties)
    {
        if (id != parameter.id) continue;
        if (excluded) parameterExclusions.addIfNotAlreadyThere (id);
        else
        {
            // Including one parameter under a group lock keeps its siblings
            // protected by converting that group into individual exclusions.
            if (parameter.group >= 0 && locks[static_cast<std::size_t> (parameter.group)])
            {
                for (const auto& sibling : parameterProperties)
                    if (sibling.group == parameter.group) parameterExclusions.addIfNotAlreadyThere (sibling.id);
                locks[static_cast<std::size_t> (parameter.group)] = false;
            }
            parameterExclusions.removeString (id);
        }
        return;
    }
}

void YoiBoiProcessor::applyPatch (Patch patch, bool addToHistory, bool preserveLengthParameters)
{
    patch = sanitizedPatch (std::move (patch));
    const juce::ScopedLock lock (stateMutex);
    if (addToHistory)
    {
        const auto before = livePatch();
        if (historyIndex >= 0 && ! sameSound (history[static_cast<size_t> (historyIndex)], before))
            pushHistory (before);
    }
    currentPatch = patch;
    const auto values = parameterValuesFor (patch);
    for (int i = 0; i < parameterCount; ++i)
    {
        if (preserveLengthParameters && (i == pLengthMs || i == pLengthDivision || i == pLengthFeel)) continue;
        setParameterValue (parameterIds[static_cast<std::size_t> (i)], values[static_cast<std::size_t> (i)]);
    }
    publishHiddenControls (patch);
    currentPatch = livePatch();
    // Loading the same preset twice (or resetting an unchanged shape) would
    // otherwise add an Undo step that changes nothing.
    if (addToHistory && (historyIndex < 0
                         || ! sameHistoryEntry (history[static_cast<size_t> (historyIndex)], currentPatch)))
        pushHistory (currentPatch);
}

void YoiBoiProcessor::newYoi()
{
    const auto old = livePatch();
    PatchLocks currentLocks; juce::String selectedFamily;
    std::uint64_t seed;
    {
        const juce::ScopedLock lock (stateMutex);
        currentLocks = locks; selectedFamily = generationFamily; seed = ++seedCounter;
    }
    if (std::all_of (currentLocks.begin(), currentLocks.end(), [] (bool value) { return value; }))
    { const juce::ScopedLock lock (stateMutex); status = "Unlock a control to generate"; return; }
    std::optional<Family> family;
    for (int i = 0; i < 4; ++i)
        if (selectedFamily.equalsIgnoreCase (familyName (static_cast<Family> (i))))
            family = static_cast<Family> (i);
    auto next = yoiboi::newYoi (old, currentLocks, seed, family);
    preserveExcludedParameters (next, old);
    if (sameSound (next, old))
    { const juce::ScopedLock lock (stateMutex); status = "Unlock a control to generate"; return; }
    applyPatch (next, true, currentLocks[4]);
    const juce::ScopedLock lock (stateMutex); status = "New sound ready";
}

void YoiBoiProcessor::mutate()
{
    const auto old = livePatch();
    PatchLocks currentLocks; std::uint64_t seed;
    { const juce::ScopedLock lock (stateMutex); currentLocks = locks; seed = ++seedCounter; }
    if (std::all_of (currentLocks.begin(), currentLocks.end(), [] (bool value) { return value; }))
    { const juce::ScopedLock lock (stateMutex); status = "Unlock a control to mutate"; return; }
    auto next = mutatePatch (old, currentLocks, seed);
    preserveExcludedParameters (next, old);
    if (sameSound (next, old))
    { const juce::ScopedLock lock (stateMutex); status = "Unlock a control to mutate"; return; }
    applyPatch (next, true, currentLocks[4]);
    const juce::ScopedLock lock (stateMutex); status = "Variation applied";
}

void YoiBoiProcessor::undo()
{
    Patch patch;
    { const juce::ScopedLock lock (stateMutex);
      if (historyIndex <= 0) return;
      patch = history[static_cast<size_t> (--historyIndex)]; }
    applyPatch (patch, false);
}

void YoiBoiProcessor::redo()
{
    Patch patch;
    { const juce::ScopedLock lock (stateMutex);
      if (historyIndex + 1 >= static_cast<int> (history.size())) return;
      patch = history[static_cast<size_t> (++historyIndex)]; }
    applyPatch (patch, false);
}

void YoiBoiProcessor::resetShape()
{
    auto patch = livePatch();
    const auto original = factoryPresets()[static_cast<size_t> (static_cast<int> (patch.family) * 3)];
    patch.startPose = original.startPose; patch.bodyPose = original.bodyPose;
    patch.endPose = original.endPose; patch.bodyTime = original.bodyTime;
    patch.curve = original.curve;
    patch.vowelStart = original.startPose; patch.vowelBody = original.bodyPose; patch.vowelEnd = original.endPose;
    applyPatch (std::move (patch), true);
}

void YoiBoiProcessor::beginManualEdit (const juce::String& description)
{
    const auto before = livePatch();
    const juce::ScopedLock lock (stateMutex);
    if (manualEditInProgress) return;
    editStart = before;
    manualEditInProgress = true;
    status = description;
}

void YoiBoiProcessor::endManualEdit()
{
    const auto after = livePatch();
    const juce::ScopedLock lock (stateMutex);
    if (! manualEditInProgress) return;
    manualEditInProgress = false;
    if (sameSound (editStart, after)) return;
    if (historyIndex >= 0 && ! sameSound (history[static_cast<size_t> (historyIndex)], editStart))
        pushHistory (editStart);
    pushHistory (after);
    currentPatch = after;
    status = "Sound edited";
}

juce::File YoiBoiProcessor::userPresetDirectory() const
{
    if (presetRoot != juce::File {}) return presetRoot;
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("YOI-BOI").getChildFile ("Presets");
}

void YoiBoiProcessor::rescanUserPresets()
{
    // Message thread. File I/O happens outside the state lock; the merge is
    // applied under it. Presets saved by another instance appear here, and
    // existing entries keep their index so open Library selections stay valid.
    juce::Array<juce::File> files;
    const auto directory = userPresetDirectory();
    // Subfolders of the preset folder become Library folders. Files are only
    // ever read here; nothing is moved or rewritten.
    if (directory.isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator (directory, true, "*.xml", juce::File::findFiles))
            if (entry.getFile().getRelativePathFrom (directory).retainCharacters ("/").length() <= 3)
                files.add (entry.getFile());
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFullPathName() < b.getFullPathName(); });

    struct Known { juce::File file; juce::Time modified; juce::int64 size; juce::String presetId; bool available; };
    std::vector<Known> known;
    {
        const juce::ScopedLock lock (stateMutex);
        for (const auto& entry : userPresets)
            known.push_back ({ entry.file, entry.modified, entry.size, entry.patch.presetId, entry.available });
    }

    const auto read = [&directory] (const juce::File& file, juce::Time modified, juce::int64 size) -> std::optional<UserPreset>
    {
        if (auto xml = juce::XmlDocument::parse (file))
            if (auto patch = fromValueTree (juce::ValueTree::fromXml (*xml)))
            {
                if (! patch->presetId.startsWith ("user:")) patch->presetId = "user:" + file.getFileNameWithoutExtension();
                const auto parent = file.getParentDirectory();
                const auto folder = parent == directory ? juce::String() : parent.getRelativePathFrom (directory);
                return UserPreset { *patch, file, modified, size, true, folder };
            }
        return std::nullopt;
    };

    std::vector<bool> seen (known.size(), false);
    std::vector<std::pair<std::size_t, UserPreset>> changed;
    std::vector<UserPreset> added;
    for (const auto& file : files)
    {
        const auto size = file.getSize();
        const auto modified = file.getLastModificationTime();
        std::optional<std::size_t> match;
        for (std::size_t i = 0; i < known.size() && ! match; ++i)
            if (known[i].file == file) match = i;
        if (match && known[*match].available && known[*match].modified == modified && known[*match].size == size)
        {
            seen[*match] = true;
            continue;
        }
        if (size > 1024 * 1024) continue;
        auto parsed = read (file, modified, size);
        if (! parsed) continue;
        if (! match)
        {
            // A preset re-created under another file name keeps its entry.
            for (std::size_t i = 0; i < known.size() && ! match; ++i)
                if (! seen[i] && known[i].presetId == parsed->patch.presetId && ! known[i].file.existsAsFile())
                    match = i;
        }
        if (match)
        {
            seen[*match] = true;
            changed.emplace_back (*match, std::move (*parsed));
        }
        else added.push_back (std::move (*parsed));
    }

    const juce::ScopedLock lock (stateMutex);
    for (std::size_t i = 0; i < known.size() && i < userPresets.size(); ++i)
        if (! seen[i]) userPresets[i].available = false;
    for (auto& [index, entry] : changed)
        if (index < userPresets.size()) userPresets[index] = std::move (entry);
    for (auto& entry : added) userPresets.push_back (std::move (entry));
}

juce::StringArray YoiBoiProcessor::userPresetFolders() const
{
    juce::StringArray folders;
    const auto directory = userPresetDirectory();
    if (directory.isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "*", juce::File::findDirectories))
            if (! entry.isHidden()) folders.addIfNotAlreadyThere (entry.getFile().getFileName());
    const juce::ScopedLock lock (stateMutex);
    for (const auto& preset : userPresets)
        if (preset.available && preset.folder.isNotEmpty()) folders.addIfNotAlreadyThere (preset.folder);
    folders.sortNatural();
    return folders;
}

bool YoiBoiProcessor::savePreset (const juce::String& name, const juce::String& folder)
{
    auto patch = livePatch();
    const auto previousFavoriteKey = favoriteKey (patch);
    patch.name = name.trim().substring (0, 64);
    if (patch.name.isEmpty())
    { const juce::ScopedLock lock (stateMutex); status = "Enter a preset name"; return false; }
    // One folder level below the preset folder; the name is made filename-safe.
    const auto folderName = juce::File::createLegalFileName (folder.trim().substring (0, 48)).trim();
    const auto root = userPresetDirectory();
    if (folderName.isNotEmpty() && ! root.createDirectory())
    { const juce::ScopedLock lock (stateMutex); status = "Could not create preset folder"; return false; }
    auto directory = folderName.isEmpty() ? root : root.getChildFile (folderName);
    if (! directory.createDirectory())
    { const juce::ScopedLock lock (stateMutex); status = "Could not create preset folder"; return false; }
    // Include presets saved by other instances, so saving a name that already
    // exists on disk replaces that file instead of adding a "(2)" copy.
    rescanUserPresets();
    // A suffix distinguishes names such as "My/Yoi" and "My:Yoi", which
    // otherwise collapse to the same legal filename on macOS.
    auto file = directory.getChildFile (juce::File::createLegalFileName (patch.name)
        + "-" + juce::String::toHexString (patch.name.hashCode64()) + ".xml");
    patch.presetId = "user:" + juce::Uuid().toString();
    bool replacingKnownPreset = false;
    {
        const juce::ScopedLock lock (stateMutex);
        for (const auto& entry : userPresets)
            if (entry.available && entry.patch.name == patch.name && entry.folder == (folderName.isEmpty() ? juce::String() : folderName))
            { file = entry.file; patch.presetId = entry.patch.presetId; replacingKnownPreset = true; break; }
    }
    if (! replacingKnownPreset && file.exists())
        file = directory.getNonexistentChildFile (file.getFileNameWithoutExtension(), ".xml");
    if (auto xml = toValueTree (patch).createXml())
    {
        juce::TemporaryFile replacement (file);
        if (xml->writeTo (replacement.getFile()) && replacement.overwriteTargetFileWithTemporary())
        {
            UserPreset saved { patch, file, file.getLastModificationTime(), file.getSize(), true, folderName };
            const juce::ScopedLock lock (stateMutex);
            auto existing = std::find_if (userPresets.begin(), userPresets.end(),
                                          [&] (const UserPreset& entry) { return entry.file == file; });
            if (existing != userPresets.end()) *existing = std::move (saved);
            else userPresets.push_back (std::move (saved));
            currentPatch.name = patch.name;
            currentPatch.presetId = patch.presetId;
            if (std::find (favorites.begin(), favorites.end(), previousFavoriteKey) != favorites.end()
                && std::find (favorites.begin(), favorites.end(), favoriteKey (patch)) == favorites.end())
                favorites.push_back (favoriteKey (patch));
            if (historyIndex >= 0 && sameSound (history[static_cast<std::size_t> (historyIndex)], patch))
            {
                history[static_cast<std::size_t> (historyIndex)].name = patch.name;
                history[static_cast<std::size_t> (historyIndex)].presetId = patch.presetId;
            }
            status = "Preset saved";
            return true;
        }
    }
    const juce::ScopedLock lock (stateMutex); status = "Could not save preset";
    return false;
}

void YoiBoiProcessor::loadPreset (const juce::String& id)
{
    const auto prefix = id.upToFirstOccurrenceOf (":", false, false);
    const auto number = id.fromFirstOccurrenceOf (":", false, false);
    if (number.isEmpty() || number.length() > 9 || ! number.containsOnly ("0123456789")) return;
    const int index = number.getIntValue();
    Patch patch;
    if (prefix == "factory" && index >= 0 && index < static_cast<int> (allFactoryPresets().size()))
        patch = allFactoryPresets()[static_cast<size_t> (index)];
    else if (prefix == "user")
    { const juce::ScopedLock lock (stateMutex);
      if (index < 0 || index >= static_cast<int> (userPresets.size())
          || ! userPresets[static_cast<size_t> (index)].available) return;
      patch = userPresets[static_cast<size_t> (index)].patch; }
    else return;
    keepLockedSettings (patch, livePatch());
    applyPatch (std::move (patch), true);
}

std::vector<PresetItem> YoiBoiProcessor::listPresets() const
{
    std::vector<PresetItem> result;
    const juce::ScopedLock lock (stateMutex);
    const auto isFavorite = [this] (const Patch& patch)
    { return favorites.end() != std::find (favorites.begin(), favorites.end(), favoriteKey (patch)); };
    // New Yoi and Mutate clear the preset id, so a generated sound marks nothing.
    const auto isCurrent = [this] (const Patch& patch)
    { return currentPatch.presetId.isNotEmpty() && patch.presetId == currentPatch.presetId; };
    const auto& factory = allFactoryPresets();
    const auto addFactory = [&] (int i)
    {
        const auto& patch = factory[static_cast<size_t> (i)];
        result.push_back ({ "factory:" + juce::String (i), patch.name, familyLabel (patch), isFavorite (patch), false, false,
                            patch.folder, patch.tags, patch.soundEngine, isCurrent (patch) });
    };
    // The 0.2.0 bank is listed first and the original twelve (Classic) after
    // it. Ids stay the bank positions, so saved references are unaffected.
    const auto classicCount = static_cast<int> (factoryPresets().size());
    for (int i = classicCount; i < static_cast<int> (factory.size()); ++i) addFactory (i);
    for (int i = 0; i < classicCount; ++i) addFactory (i);
    for (int i = 0; i < static_cast<int> (userPresets.size()); ++i)
    {
        const auto& entry = userPresets[static_cast<size_t> (i)];
        if (! entry.available) continue;
        result.push_back ({ "user:" + juce::String (i), entry.patch.name, familyLabel (entry.patch), isFavorite (entry.patch), false, true,
                            entry.folder.isEmpty() ? juce::String ("Saved") : "Saved/" + entry.folder,
                            juce::StringArray { "Saved", familyLabel (entry.patch) }, entry.patch.soundEngine,
                            isCurrent (entry.patch) });
    }
    return result;
}

void YoiBoiProcessor::toggleFavorite()
{
    const auto patch = livePatch();
    const juce::ScopedLock lock (stateMutex);
    const auto key = favoriteKey (patch);
    const auto it = std::find (favorites.begin(), favorites.end(), key);
    if (it == favorites.end()) favorites.push_back (key);
    else favorites.erase (it);
}

void YoiBoiProcessor::selectAbSlot (int slot)
{
    slot = std::clamp (slot, 0, 1);
    const auto live = livePatch();
    std::optional<Patch> target;
    {
        const juce::ScopedLock lock (stateMutex);
        if (slot == abActive) return;
        abSlots[static_cast<std::size_t> (abActive)] = live;
        // An empty slot starts as a copy of the sound being compared.
        if (! abSlots[static_cast<std::size_t> (slot)]) abSlots[static_cast<std::size_t> (slot)] = live;
        target = abSlots[static_cast<std::size_t> (slot)];
        abActive = slot;
    }
    applyPatch (*target, true);
    const juce::ScopedLock lock (stateMutex);
    status = slot == 0 ? "Comparing slot A" : "Comparing slot B";
}

void YoiBoiProcessor::copyAbSlot()
{
    const auto live = livePatch();
    const juce::ScopedLock lock (stateMutex);
    abSlots[static_cast<std::size_t> (abActive)] = live;
    abSlots[static_cast<std::size_t> (1 - abActive)] = live;
    status = abActive == 0 ? "Copied A to B" : "Copied B to A";
}

bool YoiBoiProcessor::startBounce (int count, bool useMutate, juce::File folder)
{
    count = count >= 16 ? 16 : 8;
    const auto base = livePatch();
    PatchLocks currentLocks; std::optional<Family> family; std::uint64_t baseSeed;
    {
        const juce::ScopedLock lock (stateMutex);
        currentLocks = locks; baseSeed = seedCounter;
        for (int i = 0; i < 4; ++i)
            if (generationFamily.equalsIgnoreCase (familyName (static_cast<Family> (i)))) family = static_cast<Family> (i);
    }
    if (std::all_of (currentLocks.begin(), currentLocks.end(), [] (bool value) { return value; })) currentLocks.fill (false);
    // Variant seeds derive from the instrument's sequence without consuming
    // it, so the next New Yoi is the same whether or not you bounce.
    std::vector<Patch> variants;
    for (int i = 0; i < count; ++i)
    {
        const auto seed = (baseSeed ^ UINT64_C (0xb0b0b0b0b0b0b0b0)) + UINT64_C (0x9e3779b97f4a7c15) * static_cast<std::uint64_t> (i + 1);
        auto next = useMutate ? mutatePatch (base, currentLocks, seed) : yoiboi::newYoi (base, currentLocks, seed, family);
        preserveExcludedParameters (next, base);
        variants.push_back (next);
    }
    BounceRenderer::Request request;
    request.variants = variants;
    request.bpm = std::clamp (lastHostBpm.load(), 40.0, 300.0);
    // The project's sample rate (48 kHz before the host has prepared the plug-in). Lengths stay in beats.
    request.sampleRate = bounceSampleRate.load (std::memory_order_acquire);
    request.folder = folder == juce::File {} ? BounceRenderer::defaultFolder() : folder;
    if (! bounceRenderer.start (std::move (request)))
    {
        const juce::ScopedLock lock (stateMutex);
        status = "Bounce is busy or the folder can't be created";
        return false;
    }
    const juce::ScopedLock lock (stateMutex);
    bounceVariants = std::move (variants);
    status = "Bouncing " + juce::String (count) + " variations";
    return true;
}

void YoiBoiProcessor::loadBounceVariant (int index)
{
    Patch patch;
    {
        const juce::ScopedLock lock (stateMutex);
        if (index < 0 || index >= static_cast<int> (bounceVariants.size())) return;
        patch = bounceVariants[static_cast<std::size_t> (index)];
    }
    applyPatch (std::move (patch), true);
}

void YoiBoiProcessor::togglePreview() noexcept
{
    previewCommand.store (previewIndicator.load() ? 2 : 1, std::memory_order_release);
}

EditorCallbacks YoiBoiProcessor::makeEditorCallbacks()
{
    EditorCallbacks c;
    c.newYoi = [this] { newYoi(); }; c.mutate = [this] { mutate(); };
    c.undo = [this] { undo(); }; c.redo = [this] { redo(); };
    c.resetShape = [this] { resetShape(); };
    c.togglePreview = [this] { togglePreview(); };
    c.beginEdit = [this] (const juce::String& description) { beginManualEdit (description); };
    c.endEdit = [this] { endManualEdit(); };
    c.setLock = [this] (int index, bool value) { if (index >= 0 && index < 6) { const juce::ScopedLock lock (stateMutex); locks[static_cast<size_t> (index)] = value; } };
    c.isLocked = [this] (int index) { const juce::ScopedLock lock (stateMutex); return index >= 0 && index < 6 ? locks[static_cast<size_t> (index)] : false; };
    c.setParameterLock = [this] (const juce::String& id, bool value) { setParameterLock (id, value); };
    c.isParameterLocked = [this] (const juce::String& id) { return isParameterLocked (id); };
    c.currentPatchName = [this] { const juce::ScopedLock lock (stateMutex); return currentPatch.name; };
    c.familyName = [this] { const juce::ScopedLock lock (stateMutex); return familyLabel (currentPatch); };
    c.statusText = [this] { const juce::ScopedLock lock (stateMutex); return status; };
    c.canUndo = [this] { const juce::ScopedLock lock (stateMutex); return historyIndex > 0; };
    c.canRedo = [this] { const juce::ScopedLock lock (stateMutex); return historyIndex + 1 < static_cast<int> (history.size()); };
    c.previewing = [this] { return previewIndicator.load(); };
    c.externalNotesHeld = [this] { return externalNotesIndicator.load(); };
    c.midiActive = [this] { return midiIndicator.load(); };
    c.outputLevelLeft = [this] { return meterLeft.exchange (0.0f, std::memory_order_acq_rel); };
    c.outputLevelRight = [this] { return meterRight.exchange (0.0f, std::memory_order_acq_rel); };
    c.gesturePhase = [this] { return displayedGesturePhase.load(); };
    c.waveform = [this] { return recentWaveform(); };
    c.fmRatio = [this] { return fmRatio.load (std::memory_order_acquire); };
    c.isFavorite = [this] { const juce::ScopedLock lock (stateMutex); return favorites.end() != std::find (favorites.begin(), favorites.end(), favoriteKey (currentPatch)); };
    c.toggleFavorite = [this] { toggleFavorite(); };
    c.listPresets = [this] { return listPresets(); };
    c.rescanPresets = [this] { rescanUserPresets(); };
    c.editorZoom = [this] { return editorZoom.load(); };
    c.setEditorZoom = [this] (int percent)
    {
        if (percent == editorZoom.load() || (percent != 90 && percent != 100 && percent != 125 && percent != 150)) return;
        editorZoom.store (percent);
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    };
    c.loadPreset = [this] (const juce::String& id) { loadPreset (id); };
    c.savePreset = [this] (const juce::String& name) { return savePreset (name); };
    c.savePresetInFolder = [this] (const juce::String& name, const juce::String& folder) { return savePreset (name, folder); };
    c.userPresetFolders = [this] { return userPresetFolders(); };
    c.abSlot = [this] { const juce::ScopedLock lock (stateMutex); return abActive; };
    c.selectAbSlot = [this] (int slot) { selectAbSlot (slot); };
    c.copyAbSlot = [this] { copyAbSlot(); };
    c.startBounce = [this] (int count, bool useMutate, juce::File folder) { return startBounce (count, useMutate, folder); };
    c.bounceInfo = [this]
    {
        const auto state = bounceRenderer.status();
        BounceInfo info;
        info.running = state.running; info.done = state.done; info.total = state.total;
        info.folder = state.folder; info.files = state.files; info.names = state.names; info.message = state.error;
        return info;
    };
    c.loadBounceVariant = [this] (int index) { loadBounceVariant (index); };
    c.defaultBounceFolder = [] { return BounceRenderer::defaultFolder(); };
    c.subOutputActive = [this] { return subBusEnabled.load() && soundEngine.load() >= Patch::firstExtendedEngine; };
    c.generationFamily = [this] { const juce::ScopedLock lock (stateMutex); return generationFamily; };
    c.setGenerationFamily = [this] (const juce::String& family) { const juce::ScopedLock lock (stateMutex); generationFamily = canonicalFamilyName (family); };
    c.soundEngine = [this] { return soundEngine.load (std::memory_order_acquire); };
    c.useLatestSoundEngine = [this]
    {
        auto patch = livePatch();
        if (patch.soundEngine == Patch::currentSoundEngine) return;
        applyPatch (upgradedToLatestEngine (std::move (patch)), true);
        const juce::ScopedLock lock (stateMutex);
        status = "Latest sound engine enabled; Undo restores the original";
    };
    return c;
}

juce::AudioProcessorEditor* YoiBoiProcessor::createEditor()
{
    return new YoiBoiEditor (*this, apvts, makeEditorCallbacks());
}

void YoiBoiProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const juce::ScopedLock lock (stateMutex);
    juce::ValueTree root ("YoiBoiState");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("seedCounter", juce::String::toHexString (static_cast<juce::int64> (seedCounter)), nullptr);
    root.setProperty ("generationFamily", generationFamily, nullptr);
    root.setProperty ("editorZoom", editorZoom.load(), nullptr);
    for (int i = 0; i < 6; ++i) root.setProperty ("lock" + juce::String (i), locks[static_cast<size_t> (i)], nullptr);
    root.addChild (apvts.copyState(), -1, nullptr);
    root.addChild (toValueTree (livePatch()), -1, nullptr);
    // The Live Set restores its current sound. Unchosen variations stay only
    // in this instance's Undo memory, never in saved state or the library.
    juce::ValueTree exclusions ("ParameterExclusions");
    for (const auto& id : parameterExclusions)
    {
        juce::ValueTree item ("Parameter"); item.setProperty ("id", id, nullptr);
        exclusions.addChild (item, -1, nullptr);
    }
    root.addChild (exclusions, -1, nullptr);
    juce::ValueTree liked ("Favorites");
    for (const auto& id : favorites) { juce::ValueTree item ("Item"); item.setProperty ("id", id, nullptr); liked.addChild (item, -1, nullptr); }
    root.addChild (liked, -1, nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary (*xml, destData);
}

void YoiBoiProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0 || size > 4 * 1024 * 1024) return;
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid() || ! root.hasType ("YoiBoiState") || static_cast<int> (root.getProperty ("version", 0)) != 1) return;
    const auto parsed = fromValueTree (root.getChildWithName ("YoiBoiPatch"));
    const auto params = root.getChildWithName ("PARAMETERS");
    if (! parsed || ! params.isValid()) return;

    // Validate the complete payload before changing the running instrument.
    // APVTS does not sanitize arbitrary ValueTree numbers for us.
    juce::ValueTree safeParameters ("PARAMETERS");
    // Sessions from 0.1.x have no 0.2.0 parameters: take them from the saved
    // patch (which carries the original poses into the vowel list).
    const auto patchValues = parameterValuesFor (*parsed);
    for (std::size_t index = 0; index < parameterIds.size(); ++index)
    {
        const auto* id = parameterIds[index];
        const auto* parameter = apvts.getParameter (id);
        auto fallback = parameter->convertFrom0to1 (parameter->getDefaultValue());
        if (static_cast<int> (index) >= originalParameterCount) fallback = patchValues[index];
        if (std::strcmp (id, "sub_mode") == 0) fallback = static_cast<float> (parsed->subMode);
        if (std::strcmp (id, "low_cut") == 0) fallback = parsed->lowCut ? 1.f : 0.f;
        if (std::strcmp (id, "length_feel") == 0) fallback = static_cast<float> (parsed->lengthFeel);
        const auto& range = parameter->getNormalisableRange();
        auto value = safeParameterValue (params.getChildWithProperty ("id", id).getProperty ("value"), fallback);
        value = range.snapToLegalValue (std::clamp (value, range.start, range.end));
        juce::ValueTree item ("PARAM");
        item.setProperty ("id", id, nullptr);
        item.setProperty ("value", value, nullptr);
        safeParameters.addChild (item, -1, nullptr);
    }
    const juce::ScopedLock lock (stateMutex);
    const auto restoreIdentity = [this] (Patch& patch)
    {
        if (patch.presetId.isNotEmpty()) return;
        for (const auto& preset : allFactoryPresets())
            if (patch.name == preset.name) { patch.presetId = preset.presetId; return; }
        for (const auto& preset : userPresets)
            if (preset.available && patch.name == preset.patch.name) { patch.presetId = preset.patch.presetId; return; }
        for (std::size_t index = 0; index < legacyFactoryNames.size(); ++index)
            if (patch.name == legacyFactoryNames[index])
            { patch.presetId = "factory:" + juce::String (static_cast<int> (index)); return; }
    };
    apvts.replaceState (safeParameters);
    currentPatch = *parsed;
    restoreIdentity (currentPatch);
    publishHiddenControls (currentPatch);
    currentPatch = livePatch();
    for (int i = 0; i < 6; ++i) locks[static_cast<size_t> (i)] = static_cast<bool> (root.getProperty ("lock" + juce::String (i), defaultPatchLocks()[static_cast<size_t> (i)]));
    parameterExclusions.clear();
    for (const auto& item : root.getChildWithName ("ParameterExclusions"))
    {
        if (parameterExclusions.size() >= static_cast<int> (parameterProperties.size())) break;
        const auto id = item.getProperty ("id").toString();
        for (const auto& parameter : parameterProperties)
            if (id == parameter.id) parameterExclusions.addIfNotAlreadyThere (id);
    }
    // Case-insensitive: earlier builds stored the editor's "RUBBER" spelling.
    generationFamily = canonicalFamilyName (root.getProperty ("generationFamily", "All Families").toString());
    const int zoom = static_cast<int> (root.getProperty ("editorZoom", 100));
    editorZoom.store ((zoom == 90 || zoom == 125 || zoom == 150) ? zoom : 100);
    history.clear();
    history.push_back (currentPatch);
    historyIndex = 0;
    favorites.clear();
    for (const auto& item : root.getChildWithName ("Favorites"))
    {
        if (favorites.size() >= 1024) break;
        const auto add = [&] (const juce::String& key)
        {
            if (key.isNotEmpty() && std::find (favorites.begin(), favorites.end(), key) == favorites.end())
                favorites.push_back (key);
        };
        if (item.hasProperty ("id")) add (item.getProperty ("id").toString().substring (0, 256));
        else
        {
            const auto name = item.getProperty ("name").toString().substring (0, 128);
            const auto migrate = [&] (const Patch& patch) { if (patch.name == name) add (favoriteKey (patch)); };
            for (const auto& patch : allFactoryPresets()) migrate (patch);
            for (const auto& preset : userPresets) if (preset.available) migrate (preset.patch);
            for (std::size_t index = 0; index < legacyFactoryNames.size(); ++index)
                if (name == legacyFactoryNames[index]) add ("factory:" + juce::String (static_cast<int> (index)));
            migrate (currentPatch);
        }
    }
    const auto counterText = root.getProperty ("seedCounter").toString();
    seedCounter = counterText.isNotEmpty() && counterText.length() <= 16
                  && counterText.containsOnly ("0123456789abcdefABCDEF")
                ? static_cast<std::uint64_t> (counterText.getHexValue64()) : currentPatch.seed;
    manualEditInProgress = false;
    status = "Session restored";
}
} // namespace yoiboi

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new yoiboi::YoiBoiProcessor();
}
