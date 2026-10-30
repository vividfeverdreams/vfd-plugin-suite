#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "ReverbEngine.h"
#include <array>
#include <atomic>

class ThrowUpProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    ThrowUpProcessor();
    ~ThrowUpProcessor() override;
    const juce::String getName() const override { return "THROW-UP"; }
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void reset() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram(int i) override { loadFactoryPreset(i); }
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    // Room presets change only the room (sound) controls, never THROW, Output,
    // Bypass or the performance controls, and send no program-change refresh.
    void loadFactoryPreset(int index);
    static juce::StringArray roomParameterIds();

    // Sound generation: 0.1.5 (kept for projects saved with 0.1.5) or latest.
    throwup::SoundVersion getSoundVersion() const;
    void setSoundVersion(throwup::SoundVersion); // an undoable host edit

    // User presets: <root>/<Folder>/<Name>.throwup. Saving never replaces an
    // existing file. Tests and validators redirect the root.
    static juce::File userPresetRoot();
    static void setUserPresetRootForTesting(const juce::File& root);
    juce::File saveUserPreset(const juce::String& folder, const juce::String& name);
    bool loadUserPreset(const juce::File& file);
    juce::String getUserPresetName() const;

    // A/B compare of every setting except THROW, Output, Bypass and the THROW! key.
    int getActiveSlot() const;
    void selectSlot(int slot);

    // Editor size (1.0 = 660 x 855), saved with the project; 0 = not chosen yet.
    float getEditorScale() const;
    void setEditorScale(float scale);

    float getTriggerLevel() const { return triggerLevel.load(std::memory_order_relaxed); }
    // Applies a room picked by a MIDI note now (normally the 30 Hz message-thread poll does it).
    void flushPendingRoomForTesting() { applyPendingRoom(); }
    double getHostTempo() const { return hostTempo.load(std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inputLevel { 0.0f }, outputLevel { 0.0f };

    // Programs a MIDI note selects when "Note picks room" is on: C1 (36) upward.
    static constexpr int firstRoomNote = 36;
    static constexpr int parameterCount = 34;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout();
    throwup::Settings readSettings(int numSamples);
    void process(juce::AudioBuffer<float>&, juce::MidiBuffer&, bool forceBypass);
    void handleMidi(const juce::MidiBuffer&, int numSamples);
    // Real-time-safe hand-off: the audio thread only stores the requested room in
    // pendingRoom; this message-thread timer (30 Hz) takes it and loads the room.
    void timerCallback() override { applyPendingRoom(); }
    void applyPendingRoom();
    void applyParameter(const juce::String& id, float value);
    juce::ValueTree captureSlot() const;
    void applySlot(const juce::ValueTree&);

    throwup::ReverbEngine engine;
    std::array<std::atomic<float>*, parameterCount> parameterValues {};
    std::atomic<int> currentProgram { 0 };
    double sampleRate = 48000.0;
    // Audio-thread state.
    std::array<throwup::TriggerEvent, 64> triggerEvents {};
    int triggerEventCount = 0;
    std::array<bool, 128> heldNotes {};
    int heldNoteCount = 0;
    float midiLevel = 0.0f;
    bool midiWasActive = false;
    int roomOverride = -1;
    double freeSwellPhase = 0.0;
    std::atomic<int> pendingRoom { -1 }, appliedRoom { -1 };
    std::atomic<float> triggerLevel { 0.0f };
    std::atomic<double> hostTempo { 120.0 };
    // Metadata is written by UI/state operations and snapshotted by host save
    // threads. Never acquired by processBlock/readSettings/handleMidi. Do not
    // hold it across APVTS calls, file I/O, serialization or host callbacks.
    mutable juce::CriticalSection metadataLock;
    juce::String userPresetName;
    std::array<juce::ValueTree, 2> slots;
    int activeSlot = 0;
    float editorScale = 0.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThrowUpProcessor)
};
