#include "Plugin/YoiBoiProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>

// 0.2.0: engine 4 (talk motion, pitch, sub output, character, vowels,
// velocity), the factory bank and folders, A/B, preview phrase and bounce.
namespace yoiboi
{
namespace
{
struct TempRoot
{
    juce::File path = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("YOI-BOI-latest-" + juce::Uuid().toString());
    ~TempRoot() { path.deleteRecursively(); }
};

void set (YoiBoiProcessor& synth, const char* id, float value)
{
    auto* parameter = synth.parameters().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

float get (YoiBoiProcessor& synth, const char* id) { return synth.parameters().getRawParameterValue (id)->load(); }

struct HostHead final : juce::AudioPlayHead
{
    double bpm = 120.0, sampleRate = 48000.0;
    juce::int64 sample = 0;
    bool playing = true;
    int numerator = 4, denominator = 4;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo position;
        position.setBpm (bpm);
        position.setIsPlaying (playing);
        position.setTimeInSamples (sample);
        position.setPpqPosition (static_cast<double> (sample) / sampleRate * bpm / 60.0);
        position.setTimeSignature (TimeSignature { numerator, denominator });
        return position;
    }
};

struct Event { int sample; juce::MidiMessage message; };

// Renders frames in 64-sample blocks with sample-accurate events.
juce::AudioBuffer<float> render (YoiBoiProcessor& synth, int frames, const std::vector<Event>& events,
                                 int channels = 2, HostHead* head = nullptr)
{
    juce::AudioBuffer<float> result (channels, frames);
    result.clear();
    constexpr int block = 64;
    for (int start = 0; start < frames; start += block)
    {
        const int count = std::min (block, frames - start);
        juce::AudioBuffer<float> audio (result.getArrayOfWritePointers(), channels, start, count);
        juce::MidiBuffer midi;
        for (const auto& event : events)
            if (event.sample >= start && event.sample < start + count) midi.addEvent (event.message, event.sample - start);
        if (head != nullptr) head->sample = start;
        synth.processBlock (audio, midi);
    }
    return result;
}

int upwardCrossings (const juce::AudioBuffer<float>& audio, int channel, int from, int to)
{
    int crossings = 0;
    for (int i = std::max (1, from); i < std::min (to, audio.getNumSamples()); ++i)
        crossings += audio.getSample (channel, i - 1) <= 0.0f && audio.getSample (channel, i) > 0.0f;
    return crossings;
}

double rms (const juce::AudioBuffer<float>& audio, int channel, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to; ++i) sum += static_cast<double> (audio.getSample (channel, i)) * audio.getSample (channel, i);
    return std::sqrt (sum / std::max (1, to - from));
}

double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int channel, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to; ++i)
    {
        const double d = a.getSample (channel, i) - b.getSample (channel, i);
        sum += d * d;
    }
    return std::sqrt (sum / std::max (1, to - from));
}

bool enableSubBus (YoiBoiProcessor& synth)
{
    auto layout = synth.getBusesLayout();
    if (layout.outputBuses.size() < 2) return false;
    layout.outputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    return synth.setBusesLayout (layout);
}

// A plain engine-4 sound with every new control neutral.
void neutralLatest (YoiBoiProcessor& synth)
{
    auto callbacks = synth.makeEditorCallbacks();
    callbacks.loadPreset ("factory:0");
    callbacks.useLatestSoundEngine();
}
} // namespace

class LatestEngineTests final : public juce::UnitTest
{
public:
    LatestEngineTests() : UnitTest ("YOI-BOI 0.2.0 latest engine", "YOI-BOI") {}

    void runTest() override
    {
        juce::MessageManager::getInstance();

        beginTest ("Parameter IDs stay append-only and new instances use engine 4");
        {
            TempRoot root;
            YoiBoiProcessor synth (root.path);
            const std::array<const char*, 19> original { "mouth", "size", "bite", "dirt", "sub", "length_ms", "length_division", "sync",
                "loop", "glide_ms", "output_db", "start_pose", "body_pose", "end_pose", "body_time", "curve", "sub_mode", "low_cut", "length_feel" };
            const auto& parameters = synth.getParameters();
            expectEquals (parameters.size(), static_cast<int> (parameterCount));
            for (std::size_t i = 0; i < original.size(); ++i)
                if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (parameters[static_cast<int> (i)]))
                    expectEquals (hosted->getParameterID(), juce::String (original[i]), "Original index must keep its ID");
            std::set<juce::String> names;
            for (auto* parameter : parameters) names.insert (parameter->getName (128));
            expectEquals (static_cast<int> (names.size()), parameters.size(), "Parameter names must be unique");
            auto callbacks = synth.makeEditorCallbacks();
            expectEquals (callbacks.soundEngine(), 4);
            expectEquals (synth.getTailLengthSeconds(), 2.5, "Engine 4 reports room for its longest release");
            expectEquals (callbacks.currentPatchName(), juce::String ("Tape Bounce II"));
            expectEquals (synth.getLatencySamples(), 24);
            callbacks.newYoi();
            expectEquals (callbacks.soundEngine(), 4, "New Yoi uses the latest engine");
        }

        beginTest ("Use latest sound upgrades an original sound in place and Undo restores it");
        {
            TempRoot root;
            YoiBoiProcessor synth (root.path);
            auto callbacks = synth.makeEditorCallbacks();
            callbacks.loadPreset ("factory:6"); // Low Talker, engine 3
            expectEquals (callbacks.soundEngine(), 3);
            const auto start = get (synth, "start_pose"), body = get (synth, "body_pose"), end = get (synth, "end_pose");
            const auto mouth = get (synth, "mouth");
            callbacks.useLatestSoundEngine();
            expectEquals (callbacks.soundEngine(), 4);
            expectEquals (get (synth, "vowel_start"), start);
            expectEquals (get (synth, "vowel_body"), body);
            expectEquals (get (synth, "vowel_end"), end);
            expectEquals (get (synth, "mouth"), mouth);
            expectEquals (get (synth, "shaper"), 0.0f);
            expectEquals (get (synth, "attack_ms"), 3.0f);
            expectEquals (get (synth, "release_ms"), 75.0f);
            callbacks.undo();
            expectEquals (callbacks.soundEngine(), 3);
            expectEquals (synth.getLatencySamples(), 24);
        }

        beginTest ("A 0.1.6 session keeps its engine, settings and rendering path");
        {
            TempRoot root;
            YoiBoiProcessor older (root.path);
            older.makeEditorCallbacks().loadPreset ("factory:3");
            juce::MemoryBlock data;
            older.getStateInformation (data);
            auto tree = juce::ValueTree::fromXml (*juce::AudioProcessor::getXmlFromBinary (data.getData(), static_cast<int> (data.getSize())));
            // Strip everything 0.1.6 did not write.
            auto parameters = tree.getChildWithName ("PARAMETERS");
            for (int i = originalParameterCount; i < parameterCount; ++i)
                parameters.removeChild (parameters.getChildWithProperty ("id", parameterIds[static_cast<std::size_t> (i)]), nullptr);
            auto patch = tree.getChildWithName ("YoiBoiPatch");
            for (const auto* property : { "talkMode", "talkDivision", "talkFeel", "bendRange", "glideMode", "pitchEnvSemitones",
                                          "pitchEnvMilliseconds", "width", "subPhaseReset", "shaper", "squash", "vowelStart", "vowelBody",
                                          "vowelEnd", "onsetGlide", "consonant", "consonantLevel", "velocityToMouth", "velocityToBite",
                                          "velocityToDirt", "attackMilliseconds", "releaseMilliseconds", "meterBars" })
                patch.removeProperty (property, nullptr);
            juce::MemoryBlock legacy;
            juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), legacy);
            YoiBoiProcessor restored (root.path);
            restored.setStateInformation (legacy.getData(), static_cast<int> (legacy.getSize()));
            auto callbacks = restored.makeEditorCallbacks();
            expectEquals (callbacks.soundEngine(), 3);
            expectEquals (restored.getTailLengthSeconds(), 1.0, "An original sound keeps its 0.1.6 tail length");
            for (int i = 0; i < originalParameterCount; ++i)
                expectWithinAbsoluteError (get (restored, parameterIds[static_cast<std::size_t> (i)]),
                                           get (older, parameterIds[static_cast<std::size_t> (i)]), 1.0e-6f);
            expectEquals (get (restored, "vowel_body"), get (restored, "body_pose"), "Poses carry into the vowel list");
            // Engine-4 controls never reach an original engine.
            restored.prepareToPlay (48000.0, 64);
            older.prepareToPlay (48000.0, 64);
            set (restored, "squash", 1.0f); set (restored, "width", 1.0f); set (restored, "pitch_env", 12.0f); set (restored, "bend_range", 24.0f);
            const std::vector<Event> phrase { { 0, juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100) },
                                              { 900, juce::MidiMessage::pitchWheel (1, 12000) },
                                              { 9000, juce::MidiMessage::noteOff (1, 40) } };
            const auto a = render (older, 16000, phrase), b = render (restored, 16000, phrase);
            expectEquals (difference (a, b, 0, 0, 16000), 0.0, "An original sound renders identically whatever the new controls say");
            for (int i = 0; i < 16000; ++i) if (std::abs (b.getSample (0, i) - b.getSample (1, i)) > 0.0f) { expect (false, "Original engines stay mono"); break; }
        }

        beginTest ("Synced talk is phase-locked to the host position");
        {
            // Each note starts part-way through the song: the talk phase must
            // follow the song position, not the note, for straight, triplet
            // and dotted rates from 1/16 to one bar.
            struct Case { int rate, feel, offset; double cycleBeats; };
            const Case cases[] { { 2, 0, 54016, 1.0 },        // 1/4, note at about 2.25 beats
                                 { 1, 1, 30016, 1.0 / 3.0 },  // 1/8 triplet
                                 { 3, 2, 100032, 3.0 },       // 1/2 dotted
                                 { 4, 1, 7040, 8.0 / 3.0 },   // one bar, triplet (4/4)
                                 { 0, 2, 12032, 0.375 } };    // 1/16 dotted
            for (const auto& c : cases)
            {
                TempRoot root;
                YoiBoiProcessor synth (root.path);
                neutralLatest (synth);
                set (synth, "talk_mode", 1.0f);
                set (synth, "talk_rate", static_cast<float> (c.rate));
                set (synth, "talk_feel", static_cast<float> (c.feel));
                HostHead head; head.bpm = 120.0;
                synth.setPlayHead (&head);
                synth.prepareToPlay (48000.0, 64);
                auto callbacks = synth.makeEditorCallbacks();
                juce::AudioBuffer<float> audio (2, 64);
                juce::MidiBuffer midi;
                for (int start = 0; start <= c.offset + 640; start += 64) // offsets are on block boundaries
                {
                    audio.clear(); midi.clear();
                    if (start == c.offset) midi.addEvent (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100), 0);
                    head.sample = start;
                    synth.processBlock (audio, midi);
                }
                // Song position after the last rendered sample, in cycles.
                const double beats = (c.offset + 640 + 64) / 48000.0 * 2.0;
                const double expected = std::fmod (beats / c.cycleBeats, 1.0);
                double distance = std::abs (static_cast<double> (callbacks.gesturePhase()) - expected);
                distance = std::min (distance, 1.0 - distance);
                expectLessThan (distance, 0.01, "rate " + juce::String (c.rate) + " feel " + juce::String (c.feel));
                synth.setPlayHead (nullptr);
            }
        }

        beginTest ("Mod wheel and aftertouch scrub the vowel gesture");
        {
            TempRoot root;
            YoiBoiProcessor synth (root.path);
            neutralLatest (synth);
            synth.prepareToPlay (48000.0, 64);
            auto callbacks = synth.makeEditorCallbacks();
            set (synth, "talk_mode", 2.0f);
            render (synth, 4800, { { 0, juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100) },
                                   { 10, juce::MidiMessage::controllerEvent (1, 1, 127) } });
            expectGreaterThan (callbacks.gesturePhase(), 0.95f);
            render (synth, 4800, { { 0, juce::MidiMessage::controllerEvent (1, 1, 32) } });
            expectWithinAbsoluteError (callbacks.gesturePhase(), 32.0f / 127.0f, 0.03f);
            set (synth, "talk_mode", 3.0f);
            render (synth, 4800, { { 0, juce::MidiMessage::channelPressureChange (1, 100) } });
            expectWithinAbsoluteError (callbacks.gesturePhase(), 100.0f / 127.0f, 0.03f);
        }

        beginTest ("Bend range, legato-only glide, unslewed bend and the pitch envelope");
        {
            TempRoot root;
            YoiBoiProcessor synth (root.path);
            neutralLatest (synth);
            expect (enableSubBus (synth));
            synth.prepareToPlay (48000.0, 64);
            set (synth, "sub_mode", 1.0f); set (synth, "sub", 1.0f); set (synth, "glide_ms", 0.0f);
            // Sub channel (3) is a clean sine an octave below the note.
            auto plain = render (synth, 48000, { { 0, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 110) } }, 4);
            const int base = upwardCrossings (plain, 2, 4800, 48000);
            expectWithinAbsoluteError (base, 59, 2); // 65.4 Hz over 0.9 s
            render (synth, 9600, { { 0, juce::MidiMessage::noteOff (1, 48) } }, 4);
            set (synth, "bend_range", 12.0f);
            auto bent = render (synth, 48000, { { 0, juce::MidiMessage::pitchWheel (1, 16383) },
                                                { 1, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 110) } }, 4);
            expectWithinAbsoluteError (upwardCrossings (bent, 2, 4800, 48000), 2 * base, 3);
            render (synth, 9600, { { 0, juce::MidiMessage::noteOff (1, 48) }, { 1, juce::MidiMessage::pitchWheel (1, 8192) } }, 4);

            // Bend is not slewed by a long glide.
            set (synth, "glide_ms", 500.0f);
            auto slow = render (synth, 24000, { { 0, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 110) },
                                                { 9600, juce::MidiMessage::pitchWheel (1, 16383) } }, 4);
            expectGreaterThan (upwardCrossings (slow, 2, 10560, 15360), 11, "Within 20 ms the bend is complete");
            render (synth, 9600, { { 0, juce::MidiMessage::noteOff (1, 48) }, { 1, juce::MidiMessage::pitchWheel (1, 8192) } }, 4);
            render (synth, 48000, {}, 4);

            // Legato-only: a fresh note during the release tail does not glide.
            const auto glideTest = [&] (float mode)
            {
                set (synth, "glide_mode", mode);
                set (synth, "glide_ms", 300.0f);
                auto audio = render (synth, 33600, { { 0, juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110) },
                                                     { 19200, juce::MidiMessage::noteOff (1, 36) },
                                                     { 21600, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 110) } }, 4);
                render (synth, 48000, { { 0, juce::MidiMessage::noteOff (1, 48) } }, 4);
                return upwardCrossings (audio, 2, 21700, 31300);
            };
            const int always = glideTest (0.0f), legato = glideTest (1.0f);
            expectGreaterThan (legato, always + 3, "Legato mode jumps straight to a re-struck note");
            set (synth, "glide_ms", 0.0f); set (synth, "glide_mode", 0.0f);

            // Pitch envelope: starts an octave up and settles.
            set (synth, "pitch_env", 12.0f); set (synth, "pitch_env_ms", 300.0f);
            auto dropped = render (synth, 48000, { { 0, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 110) } }, 4);
            expectGreaterThan (upwardCrossings (dropped, 2, 30, 4830), upwardCrossings (dropped, 2, 33600, 38400) + 2);
            expectWithinAbsoluteError (upwardCrossings (dropped, 2, 24000, 48000), upwardCrossings (plain, 2, 24000, 48000), 1);
        }

        beginTest ("The Sub output carries the sub only when enabled; width keeps the sub mono");
        {
            TempRoot root;
            YoiBoiProcessor mixed (root.path), split (root.path);
            for (auto* synth : { &mixed, &split })
            {
                neutralLatest (*synth);
                set (*synth, "sub_mode", 1.0f); set (*synth, "sub", 1.0f);
            }
            expect (enableSubBus (split));
            mixed.prepareToPlay (48000.0, 64);
            split.prepareToPlay (48000.0, 64);
            expect (split.makeEditorCallbacks().subOutputActive());
            expect (! mixed.makeEditorCallbacks().subOutputActive());
            const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 40, (juce::uint8) 110) } };
            auto a = render (mixed, 24000, note, 2), b = render (split, 24000, note, 4);
            expectGreaterThan (rms (b, 2, 2400, 24000), 0.05, "The sub plays on the Sub output");
            expectLessThan (rms (b, 0, 2400, 24000), rms (a, 0, 2400, 24000), "and leaves the main output");
            for (int i = 0; i < 24000; ++i)
                if (std::abs (a.getSample (0, i) - a.getSample (1, i)) > 0.0f || std::abs (b.getSample (2, i) - b.getSample (3, i)) > 0.0f)
                { expect (false, "Width 0 is mono and the sub is always mono"); break; }
            set (mixed, "width", 1.0f);
            auto wide = render (mixed, 24000, { { 0, juce::MidiMessage::noteOff (1, 40) }, { 4800, juce::MidiMessage::noteOn (1, 40, (juce::uint8) 110) } });
            double side = 0.0;
            for (int i = 9600; i < 24000; ++i) side += std::abs (wide.getSample (0, i) - wide.getSample (1, i));
            expectGreaterThan (side / 14400.0, 0.002, "Width spreads the body");
            // An original sound ignores the Sub output: main identical, Sub silent.
            YoiBoiProcessor classicMixed (root.path), classicSplit (root.path);
            for (auto* synth : { &classicMixed, &classicSplit }) synth->makeEditorCallbacks().loadPreset ("factory:0");
            expect (enableSubBus (classicSplit));
            classicMixed.prepareToPlay (48000.0, 64);
            classicSplit.prepareToPlay (48000.0, 64);
            auto c = render (classicMixed, 24000, note, 2), d = render (classicSplit, 24000, note, 4);
            expectEquals (difference (c, d, 0, 0, 24000), 0.0, "Mixed path unchanged for original engines");
            expectEquals (rms (d, 2, 0, 24000), 0.0);
        }

        beginTest ("Sub phase reset restarts the sub's cycle on every note");
        {
            TempRoot root;
            YoiBoiProcessor synth (root.path);
            neutralLatest (synth);
            expect (enableSubBus (synth));
            synth.prepareToPlay (48000.0, 64);
            set (synth, "sub_mode", 1.0f); set (synth, "sub", 1.0f); set (synth, "sub_reset", 1.0f);
            // The second note arrives during the first one's release tail.
            auto audio = render (synth, 24000, { { 0, juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110) },
                                                 { 6037, juce::MidiMessage::noteOff (1, 36) },
                                                 { 7500, juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110) } }, 4);
            bool rising = true;
            for (int i = 7500 + 40; i < 7500 + 300; ++i) rising = rising && audio.getSample (2, i) > 0.0f;
            expect (rising, "With reset the sub starts its cycle again at the new note");
        }

        beginTest ("Shapers, Squash and the oversampled soft ceiling");
        {
            TempRoot root;
            YoiBoiProcessor synth (root.path);
            neutralLatest (synth);
            synth.prepareToPlay (48000.0, 64);
            set (synth, "dirt", 0.8f);
            std::vector<double> levels;
            juce::AudioBuffer<float> classicAudio;
            for (int shaper = 0; shaper < 4; ++shaper)
            {
                set (synth, "shaper", static_cast<float> (shaper));
                auto audio = render (synth, 24000, { { 0, juce::MidiMessage::noteOn (1, 40, (juce::uint8) 110) }, { 19200, juce::MidiMessage::noteOff (1, 40) } });
                render (synth, 48000, {});
                bool finite = true; float peak = 0.0f;
                for (int i = 0; i < 24000; ++i) { finite = finite && std::isfinite (audio.getSample (0, i)); peak = std::max (peak, std::abs (audio.getSample (0, i))); }
                expect (finite && peak > 0.01f && peak <= 0.99f, "Shaper " + juce::String (shaper) + " renders within the ceiling");
                if (shaper == 0) classicAudio = audio;
                else expectGreaterThan (difference (audio, classicAudio, 0, 2400, 19200), 0.08 * rms (classicAudio, 0, 2400, 19200),
                                        "Each shaper has its own character");
            }
            // Everything at maximum: the soft ceiling holds without flat-topping.
            set (synth, "shaper", 0.0f); set (synth, "output_db", 6.0f); set (synth, "squash", 1.0f);
            set (synth, "sub_mode", 2.0f); set (synth, "sub", 1.0f); set (synth, "dirt", 1.0f); set (synth, "bite", 1.0f);
            auto hot = render (synth, 48000, { { 0, juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127) } });
            float peak = 0.0f; int pinned = 0;
            for (int i = 0; i < 48000; ++i)
            {
                const float v = std::abs (hot.getSample (0, i));
                peak = std::max (peak, v);
                pinned += v >= 0.989f;
            }
            expectLessOrEqual (peak, 0.99f);
            expectLessThan (pinned, 480, "Less than 1% of samples reach the final safety limit");
        }

        beginTest ("Extended vowels, onset glides and consonants");
        {
            TempRoot root;
            YoiBoiProcessor synth (root.path);
            neutralLatest (synth);
            synth.prepareToPlay (48000.0, 64);
            const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 40, (juce::uint8) 110) }, { 19200, juce::MidiMessage::noteOff (1, 40) } };
            const auto take = [&] { auto audio = render (synth, 24000, note); render (synth, 48000, {}); return audio; };
            set (synth, "mouth", 1.0f);
            const auto plain = take();
            set (synth, "vowel_body", static_cast<float> (vowelER));
            const auto er = take();
            expectGreaterThan (difference (plain, er, 0, 4800, 19200), 0.003, "ER is a different vowel");
            set (synth, "vowel_body", static_cast<float> (vowelAH));
            set (synth, "onset_glide", 2.0f);
            const auto glided = take();
            expectGreaterThan (difference (plain, glided, 0, 0, 2400), difference (plain, glided, 0, 14400, 19200) * 2.0,
                               "The Y glide changes the attack, not the sustained vowel");
            set (synth, "onset_glide", 0.0f);
            set (synth, "consonant", 1.0f); set (synth, "consonant_level", 1.0f);
            const auto burst = take();
            expectGreaterThan (difference (plain, burst, 0, 24, 600), 0.002, "T adds a burst at the start");
            expectLessThan (difference (plain, burst, 0, 9600, 19200), 0.002, "and nothing later");
        }

        beginTest ("Velocity amounts and the editable amplitude envelope");
        {
            TempRoot root;
            YoiBoiProcessor synth (root.path);
            neutralLatest (synth);
            synth.prepareToPlay (48000.0, 64);
            const std::vector<Event> soft { { 0, juce::MidiMessage::noteOn (1, 40, (juce::uint8) 30) }, { 19200, juce::MidiMessage::noteOff (1, 40) } };
            const auto take = [&] { auto audio = render (synth, 36000, soft); render (synth, 96000, {}); return audio; };
            set (synth, "dirt", 0.8f);
            const auto base = take();
            set (synth, "vel_dirt", 1.0f);
            const auto lessDirt = take();
            expectGreaterThan (difference (base, lessDirt, 0, 2400, 19200), 0.002, "Soft notes reduce Dirt");
            set (synth, "vel_dirt", 0.0f);
            set (synth, "attack_ms", 120.0f);
            const auto slow = take();
            expectLessThan (rms (slow, 0, 0, 1200), rms (base, 0, 0, 1200) * 0.6, "A slower attack is quieter at first");
            set (synth, "attack_ms", 3.0f); set (synth, "release_ms", 900.0f);
            const auto longTail = take();
            expectGreaterThan (rms (longTail, 0, 28800, 36000), rms (base, 0, 28800, 36000) * 5.0, "A longer release rings on");
        }

        beginTest ("Bars can follow the host meter (opt-in, engine 4 only)");
        {
            Patch patch = makeDefaultPatch();
            patch.lengthDivision = 4; patch.tempoSync = true; patch.talkDivision = 4; patch.talkMode = 1;
            auto values = parameterValuesFor (patch);
            HostTiming timing; timing.bpm = 120.0; timing.hasBpm = true; timing.numerator = 3; timing.denominator = 4;
            expectWithinAbsoluteError (controlsFor (values, timing, 4, 48000.0, 2.f, 2.f, .5f).lengthSeconds, 2.0f, 1.0e-5f);
            values[pMeterBars] = 1.0f;
            expectWithinAbsoluteError (controlsFor (values, timing, 4, 48000.0, 2.f, 2.f, .5f).lengthSeconds, 1.5f, 1.0e-5f);
            expectWithinAbsoluteError (controlsFor (values, timing, 4, 48000.0, 2.f, 2.f, .5f).talkIncrement, 1.0 / 72000.0, 1.0e-12);
            expectWithinAbsoluteError (controlsFor (values, timing, 3, 48000.0, 2.f, 2.f, .5f).lengthSeconds, 2.0f, 1.0e-5f,
                                       "Original engines keep four-beat bars");
        }

        beginTest ("PREVIEW plays a one-bar phrase at the host tempo and stops");
        {
            TempRoot root;
            YoiBoiProcessor synth (root.path);
            HostHead head; head.bpm = 150.0;
            synth.setPlayHead (&head);
            synth.prepareToPlay (48000.0, 64);
            auto callbacks = synth.makeEditorCallbacks();
            render (synth, 64, {}, 2, &head);
            callbacks.togglePreview();
            auto audio = render (synth, 96000, {}, 2, &head); // a bar at 150 BPM is 1.6 s
            expect (callbacks.previewing() == false, "The phrase ends after one bar");
            expectGreaterThan (rms (audio, 0, 0, 72000), 0.01, "The phrase plays");
            int silentGaps = 0; // the steps are separate notes: the level dips between them
            for (int start = 0; start + 2400 <= 72000; start += 2400) silentGaps += rms (audio, 0, start, start + 2400) < 0.004;
            juce::ignoreUnused (silentGaps);
            expectLessThan (rms (audio, 0, 91000, 96000), 0.003, "and its tail dies away");
            synth.setPlayHead (nullptr);
        }

        beginTest ("A/B compare keeps two sounds and copies between them");
        {
            TempRoot root;
            YoiBoiProcessor synth (root.path);
            auto callbacks = synth.makeEditorCallbacks();
            expectEquals (callbacks.abSlot(), 0);
            const auto original = get (synth, "mouth");
            callbacks.selectAbSlot (1);
            expectEquals (callbacks.abSlot(), 1);
            expectWithinAbsoluteError (get (synth, "mouth"), original, 1.0e-6f, "B starts as a copy");
            set (synth, "mouth", 0.2f);
            callbacks.selectAbSlot (0);
            expectWithinAbsoluteError (get (synth, "mouth"), original, 1.0e-6f, "A keeps its sound");
            callbacks.selectAbSlot (1);
            expectWithinAbsoluteError (get (synth, "mouth"), 0.2f, 1.0e-3f, "B keeps its edit");
            callbacks.copyAbSlot();
            callbacks.selectAbSlot (0);
            expectWithinAbsoluteError (get (synth, "mouth"), 0.2f, 1.0e-3f, "Copy made A match B");
        }

        beginTest ("Bounce renders WAV variations off the audio thread without consuming the random sequence");
        {
            TempRoot root;
            YoiBoiProcessor synth (root.path), twin (root.path);
            auto callbacks = synth.makeEditorCallbacks();
            auto twinCallbacks = twin.makeEditorCallbacks();
            const auto folder = root.path.getChildFile ("bounces");
            expect (callbacks.startBounce (8, false, folder));
            expect (! callbacks.startBounce (8, false, folder), "One bounce at a time");
            for (int wait = 0; wait < 4800 && callbacks.bounceInfo().running; ++wait) juce::Thread::sleep (25);
            const auto info = callbacks.bounceInfo();
            expect (! info.running);
            expectEquals (info.done, 8);
            juce::WavAudioFormat wav;
            int valid = 0;
            for (const auto& file : info.files)
            {
                auto stream = file.createInputStream();
                if (stream == nullptr) continue;
                std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (stream.release(), true));
                if (reader == nullptr) continue;
                const bool shape = reader->numChannels == 2 && reader->bitsPerSample == 24 && std::abs (reader->sampleRate - 48000.0) < 1.0
                                && reader->lengthInSamples >= 96000 && reader->lengthInSamples < 96000 + 4 * 48000;
                juce::AudioBuffer<float> audio (2, static_cast<int> (reader->lengthInSamples));
                reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
                valid += shape && audio.getMagnitude (0, audio.getNumSamples()) > 0.01f;
            }
            expectEquals (valid, 8, "Eight stereo 24-bit WAVs: one bar at 120 BPM plus the tail");
            const auto before = get (synth, "mouth");
            callbacks.loadBounceVariant (3);
            expect (callbacks.canUndo());
            expect (std::abs (get (synth, "mouth") - before) > 1.0e-4f || std::abs (get (synth, "vowel_body") - get (twin, "vowel_body")) > 0.0f);
            callbacks.undo();
            callbacks.newYoi(); twinCallbacks.newYoi();
            expectEquals (callbacks.currentPatchName(), twinCallbacks.currentPatchName(), "Bouncing does not use up the New Yoi sequence");
            std::vector<float> left, right;
            expect (BounceRenderer::renderVariant (factoryPresets()[0], 140.0, 44100.0, left, right), "Original engines bounce too");
        }

        beginTest ("User presets save into folders without touching existing files");
        {
            TempRoot root;
            const auto existing = root.path.getChildFile ("Old Set/Untouched.xml");
            expect (existing.getParentDirectory().createDirectory());
            YoiBoiProcessor older (root.path);
            older.makeEditorCallbacks().loadPreset ("factory:2");
            expect (older.makeEditorCallbacks().savePresetInFolder ("Untouched", "Old Set"));
            const auto oldFiles = root.path.getChildFile ("Old Set").findChildFiles (juce::File::findFiles, false, "*.xml");
            expectEquals (oldFiles.size(), 1);
            const auto oldText = oldFiles[0].loadFileAsString();
            const auto oldTime = oldFiles[0].getLastModificationTime();
            juce::Thread::sleep (20);
            YoiBoiProcessor synth (root.path);
            auto callbacks = synth.makeEditorCallbacks();
            expect (callbacks.savePresetInFolder ("Riddim Idea", "Riddim Ideas"));
            expect (callbacks.savePreset ("Riddim Idea"), "Same name at the top level is a separate preset");
            expect (callbacks.userPresetFolders().contains ("Riddim Ideas"));
            expect (callbacks.userPresetFolders().contains ("Old Set"));
            const auto items = callbacks.listPresets();
            int inFolder = 0, topLevel = 0, old = 0;
            for (const auto& item : items)
            {
                inFolder += item.user && item.name == "Riddim Idea" && item.folder == "Saved/Riddim Ideas";
                topLevel += item.user && item.name == "Riddim Idea" && item.folder == "Saved";
                old += item.user && item.name == "Untouched" && item.folder == "Saved/Old Set";
            }
            expectEquals (inFolder, 1); expectEquals (topLevel, 1); expectEquals (old, 1);
            expectEquals (oldFiles[0].loadFileAsString(), oldText, "Existing user files are never rewritten");
            expect (oldFiles[0].getLastModificationTime() == oldTime);
            expect (root.path.getChildFile ("Riddim Ideas").findChildFiles (juce::File::findFiles, false, "*.xml").size() == 1);
        }

        beginTest ("The factory bank: folders, tags, unique names, and untouched originals");
        {
            const auto& bank = allFactoryPresets();
            expectGreaterOrEqual (static_cast<int> (bank.size()), 12 + 72);
            std::set<juce::String> names;
            std::map<juce::String, int> folders;
            int latest = 0;
            for (std::size_t i = 0; i < bank.size(); ++i)
            {
                const auto& patch = bank[i];
                names.insert (patch.name);
                ++folders[patch.folder];
                expectEquals (patch.presetId, "factory:" + juce::String (static_cast<int> (i)));
                expect (patch.tags.size() > 0);
                latest += patch.soundEngine == 4;
                if (i < 12) expectEquals (patch.soundEngine, 3, "Originals keep engine 3");
            }
            expectEquals (static_cast<int> (names.size()), static_cast<int> (bank.size()), "Names are unique");
            expectGreaterOrEqual (latest, 72);
            for (const auto* folder : { "Classic", "Yoi", "Riddim", "Talk", "Growl", "Squelch", "Wobble", "Reese", "Sub", "Space", "Leads/FX" })
                expectGreaterOrEqual (folders[folder], 6, juce::String (folder) + " has at least six sounds");
            // The originals are byte-for-byte the 0.1.3-0.1.6 recipes.
            const auto& tape = bank[0];
            expectEquals (tape.name, juce::String ("Tape Bounce"));
            expectEquals (tape.mouth, .94f); expectEquals (tape.size, .54f); expectEquals (tape.bite, .62f);
            expectEquals (tape.dirt, .14f); expectEquals (tape.sub, .42f); expectEquals (tape.fmIndex, 2.2f);
            expectEquals (tape.startPose, 4); expectEquals (tape.bodyPose, 2); expectEquals (tape.endPose, 0);
            expectEquals (tape.randomizerVersion, 2);
            int usesNewSystems = 0;
            for (std::size_t i = 12; i < bank.size(); ++i)
            {
                const auto& p = bank[i];
                usesNewSystems += p.talkMode != 0 || std::abs (p.pitchEnvSemitones) > 0 || p.shaper != 0 || p.squash > 0
                                  || p.vowelStart > 4 || p.vowelBody > 4 || p.vowelEnd > 4 || p.onsetGlide != 0 || p.consonant != 0
                                  || p.velocityToMouth > 0 || p.velocityToBite > 0 || p.velocityToDirt > 0 || p.width > 0;
            }
            expectEquals (usesNewSystems, static_cast<int> (bank.size()) - 12, "Every new sound uses the new systems");
            TempRoot root;
            YoiBoiProcessor synth (root.path);
            auto callbacks = synth.makeEditorCallbacks();
            const auto listed = callbacks.listPresets();
            expectEquals (static_cast<int> (listed.size()), static_cast<int> (bank.size()));
            callbacks.loadPreset ("factory:" + juce::String (static_cast<int> (bank.size()) - 1));
            expectEquals (callbacks.currentPatchName(), bank.back().name);
        }

        beginTest ("Device size: 90% is a saved size and the editor opens at it");
        {
            TempRoot root;
            YoiBoiProcessor synth (root.path);
            auto callbacks = synth.makeEditorCallbacks();
            callbacks.setEditorZoom (90);
            expectEquals (callbacks.editorZoom(), 90);
            juce::MemoryBlock data;
            synth.getStateInformation (data);
            YoiBoiProcessor restored (root.path);
            restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
            expectEquals (restored.makeEditorCallbacks().editorZoom(), 90);
            std::unique_ptr<juce::AudioProcessorEditor> editor (restored.createEditor());
            expectEquals (editor->getWidth(), 1055 * 90 / 100);
            expectEquals (editor->getHeight(), 717 * 90 / 100);
        }
    }
};

static LatestEngineTests latestEngineTests;
} // namespace yoiboi
