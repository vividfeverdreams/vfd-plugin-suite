#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>

#include "Dsp/CompatibleVoice.h"
#include "Patch/Patch.h"
#include "UI/YoiBoiEditor.h"

namespace yoiboi
{
class YoiBoiProcessor final : public juce::AudioProcessor
{
public:
    explicit YoiBoiProcessor (juce::File presetDirectory = {});
    ~YoiBoiProcessor() override = default;

    const juce::String getName() const override { return "YOI-BOI"; }
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // Host bypass renders silence. Held notes are released on entry so a
    // note-off that arrives while bypassed cannot leave a stuck note.
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& parameters() noexcept { return apvts; }
    EditorCallbacks makeEditorCallbacks();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void applyPatch (Patch patch, bool addToHistory, bool preserveLengthParameters = false);
    Patch livePatch() const;
    void setParameterValue (const char* id, float value);
    void pushHistory (const Patch& patch);
    void preserveExcludedParameters (Patch& next, const Patch& previous) const;
    void setParameterLock (const juce::String& id, bool excluded);
    bool isParameterLocked (const juce::String& id) const;
    void newYoi();
    void mutate();
    void undo();
    void redo();
    void resetShape();
    void beginManualEdit (const juce::String& description);
    void endManualEdit();
    void loadPreset (const juce::String& id);
    bool savePreset (const juce::String& name);
    void toggleFavorite();
    std::vector<PresetItem> listPresets() const;
    // Merges the preset folder into the list: changed files are re-read, new
    // files are appended and removed files are hidden. Indices stay stable for
    // the life of the instance, so "user:<index>" Library ids remain valid.
    void rescanUserPresets();
    juce::File userPresetDirectory() const;
    void togglePreview() noexcept;
    void handleMidi (const juce::MidiMessage& message);
    void noteOn (int note, float velocity);
    void noteOff (int note);
    void allNotesOff();
    void releaseAllNotes();
    void updateActiveNote (bool retrigger);
    void publishHiddenControls (const Patch&) noexcept;
    float raw (const char* id) const noexcept;
    std::array<float, 128> recentWaveform() const noexcept;

    // No UndoManager: Undo/Redo is the patch history below. An APVTS undo
    // manager would record every automation flush in one never-closed
    // transaction and grow without bound.
    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<float>*, 19> rawParameters {};
    const juce::File presetRoot;
    dsp::CompatibleVoice voice;
    std::atomic<int> soundEngine { Patch::currentSoundEngine };
    double currentSampleRate = 48000.0;
    int previewFramesLeft = 0;
    int midiActivityFramesLeft = 0;
    int activeNote = -1;
    bool sustainDown = false;
    bool previewVoiceActive = false;
    bool bypassActive = false; // audio thread only

    std::array<bool, 128> pressed {};
    std::array<bool, 128> sustained {};
    std::array<float, 128> noteVelocities {};
    std::array<int, 128> noteOrder {};
    int noteOrderSize = 0;

    mutable juce::CriticalSection stateMutex;
    Patch currentPatch;
    PatchLocks locks = defaultPatchLocks();
    juce::StringArray parameterExclusions;
    std::vector<Patch> history;
    int historyIndex = -1;
    Patch editStart;
    bool manualEditInProgress = false;
    struct UserPreset
    {
        Patch patch;
        juce::File file;
        juce::Time modified;
        juce::int64 size = 0;
        bool available = true;
    };
    std::vector<UserPreset> userPresets;
    std::vector<juce::String> favorites;
    juce::String generationFamily = "All Families";
    std::atomic<int> editorZoom { 100 }; // device size, saved with the session (not an audio parameter)
    juce::String status;
    std::uint64_t seedCounter = 1;

    std::atomic<float> fmRatio { 2.0f }, fmIndex { 3.8f }, resonance { 0.52f };
    std::array<std::atomic<float>, 128> waveformSamples {};
    std::atomic<unsigned> publishedWaveformIndex { 0 };
    unsigned waveformWriteIndex = 0;
    int waveformCountdown = 0, waveformStride = 128;
    // Peak-hold since the editor's last read: the audio thread raises these
    // with a CAS max and the editor takes them with exchange(0).
    std::atomic<float> meterLeft { 0.0f }, meterRight { 0.0f }, displayedGesturePhase { 0.0f };
    std::atomic<bool> midiIndicator { false }, previewIndicator { false }, externalNotesIndicator { false };
    std::atomic<int> previewCommand { 0 }; // 1 start, 2 stop
};
} // namespace yoiboi
