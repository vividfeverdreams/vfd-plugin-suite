#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace yoiboi
{
struct PresetItem
{
    juce::String id, name, family;
    bool favorite = false;
    bool recent = false;
    bool user = false;
    juce::String folder;      // Library folder: a factory category, or "Saved" / "Saved/<subfolder>"
    juce::StringArray tags;
    int engine = 4;
};

struct BounceInfo
{
    bool running = false;
    int done = 0, total = 0;
    juce::File folder;
    std::vector<juce::File> files;
    std::vector<juce::String> names;
    juce::String message;
};

// All callbacks run on the message thread. Meter/activity readers should read atomics.
// Empty callbacks are supported; unavailable actions are disabled in the editor.
struct EditorCallbacks
{
    std::function<void()> newYoi, mutate, undo, redo, resetShape, togglePreview;
    std::function<void(const juce::String&)> beginEdit;
    std::function<void()> endEdit;
    std::function<void(int, bool)> setLock;
    std::function<bool(int)> isLocked;
    std::function<void(const juce::String&, bool)> setParameterLock;
    std::function<bool(const juce::String&)> isParameterLocked;
    std::function<void()> requestClose;
    std::function<bool()> canClose;
    std::function<void()> audioSettings;
    std::function<int()> soundEngine;
    std::function<void()> useLatestSoundEngine;
    std::function<juce::String()> currentPatchName, familyName, statusText;
    std::function<bool()> canUndo, canRedo, previewing, externalNotesHeld, midiActive;
    std::function<float()> outputLevelLeft, outputLevelRight, gesturePhase;
    std::function<std::array<float, 128>()> waveform;
    std::function<float()> fmRatio;
    std::function<bool()> isFavorite;
    std::function<void()> toggleFavorite;
    std::function<std::vector<PresetItem>()> listPresets;
    std::function<void()> rescanPresets; // re-read the shared preset folder
    std::function<void(const juce::String&)> loadPreset;
    std::function<bool(const juce::String&)> savePreset;
    std::function<juce::String()> generationFamily;
    std::function<void(const juce::String&)> setGenerationFamily;
    std::function<int()> editorZoom;            // saved device size in percent (90, 100, 125 or 150)
    std::function<void(int)> setEditorZoom;
    // 0.2.0
    std::function<bool(const juce::String&, const juce::String&)> savePresetInFolder; // name, subfolder ("" = top level)
    std::function<juce::StringArray()> userPresetFolders;
    std::function<int()> abSlot;                // 0 = A, 1 = B
    std::function<void(int)> selectAbSlot;
    std::function<void()> copyAbSlot;           // copies the active slot into the other one
    std::function<bool(int, bool, juce::File)> startBounce; // count (8/16), mutate?, folder (empty = default)
    std::function<BounceInfo()> bounceInfo;
    std::function<void(int)> loadBounceVariant;
    std::function<juce::File()> defaultBounceFolder;
    std::function<bool()> subOutputActive;      // the host enabled the separate Sub output
};

class YoiBoiEditor final : public juce::AudioProcessorEditor
{
public:
    YoiBoiEditor(juce::AudioProcessor&, juce::AudioProcessorValueTreeState&, EditorCallbacks);
    ~YoiBoiEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(YoiBoiEditor)
};
}
