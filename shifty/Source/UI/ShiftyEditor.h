#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <memory>
#include <vector>
#include "../Plugin/ShiftyProcessor.h"
#include "VfdInfo.h"

namespace shifty {

class ShiftyEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer {
public:
    explicit ShiftyEditor(ShiftyProcessor&);
    ~ShiftyEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void paintShell(juce::Graphics&);
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

    enum class Page { sound, voice, motion, presets };
    enum class MotionTab { lfo, envelope, yoi };

    /** Editor sizes offered by the right-click menu, as a fraction of 650 px wide. */
    static constexpr std::array<double, 4> sizeChoices { 0.9, 1.0, 1.25, 1.5 };
    static juce::Point<int> sizeFor(double scale);
    /** Sizes that fit a display's user area (width <= area width, height + 60 <= area height). */
    static std::vector<double> sizesFitting(juce::Rectangle<int> userArea);
    juce::PopupMenu createSizeMenu() const;

    // Test hooks (snapshots and feature tests).
    void showPageForTesting(Page, MotionTab = MotionTab::lfo);
    void beginPresetSaveForTesting() { beginPresetSave(); }
    void refreshForTesting() { timerCallback(); }
    /** filter: 0 factory, 1 user, 2 favorites; folder empty = all folders. */
    void selectPresetFilterForTesting(int filter, const juce::String& folder);
    int presetCountForTesting() const noexcept;

private:
    enum class PresetFilter { factory, user, favorites };

    struct Control;
    struct Switch;
    struct PresetItem;

    void timerCallback() override;
    void addControl(const char* id, juce::Rectangle<float> hit,
                    juce::Rectangle<float> value, bool rotary, bool darkText, bool lockable = true);
    void addSwitch(const char* id, juce::Rectangle<float> hit);
    Control* control(const char* id) noexcept;
    Switch* controlSwitch(const char* id) noexcept;
    float valueOf(const char* id) const noexcept;
    bool isOn(const char* id) const noexcept;
    void setParameter(const char* id, float value);
    void updateValueLabels();
    void updatePage();
    void refreshPresetList();
    void loadSelectedPreset();
    void beginPresetSave();
    void finishPresetSave();
    void showShapeMenu();
    void showRateMenu();
    void showTargetMenu();
    void showVowelMenu(const char* id);
    void showFolderMenu();
    void showSaveFolderMenu();
    void showSizeMenu();
    void applyScale(double scale);
    void showLocksMenu();
    void showParameterLockMenu(const juce::String& id, juce::Component* target);
    void drawHeader(juce::Graphics&);
    void drawSoundScreen(juce::Graphics&);
    void drawVoiceScreen(juce::Graphics&);
    void drawMotionScreen(juce::Graphics&);
    void drawLfoTab(juce::Graphics&);
    void drawEnvelopeTab(juce::Graphics&);
    void drawYoiTab(juce::Graphics&);
    void drawPresetsScreen(juce::Graphics&);
    void drawControlPanel(juce::Graphics&);
    void drawUtilityPanel(juce::Graphics&);
    void drawKnob(juce::Graphics&, const char* id, juce::Point<float> centre,
                  float arcRadius, float pointerRadius, bool large);
    void drawScreenKnob(juce::Graphics&, const char* id, const char* label, juce::Point<float> centre, float radius);
    void drawSegments(juce::Graphics&, juce::Rectangle<float> area, const juce::StringArray& labels,
                      int selected, float textSize) const;
    void drawVowelBar(juce::Graphics&, juce::Rectangle<float> track, float value, float live, bool showLive) const;
    void drawCell(juce::Graphics&, juce::Rectangle<float> area, const juce::String& label,
                  const juce::String& value, bool active) const;
    void drawEngineNote(juce::Graphics&, const juce::String& text) const;
    juce::Rectangle<int> scaled(juce::Rectangle<float>) const;
    juce::Point<float> designPoint(juce::Point<float>) const;
    void drawText(juce::Graphics&, const juce::String&, juce::Rectangle<float>,
                  float, juce::Colour, juce::Justification = juce::Justification::centred) const;
    /** Favorites are stored as "Folder/Name"; bare names from 0.4 still count for Classic and User sounds. */
    bool isFavorite(const juce::String& folder, const juce::String& name) const;
    void toggleFavorite(const juce::String& folder, const juce::String& name);

    ShiftyProcessor& plugin;
    juce::Image shell;
    juce::Image scaledShell; // shell at physical resolution for the current size (see paintShell)
    juce::Typeface::Ptr normalTypeface, displayTypeface;
    std::unique_ptr<vfd::InfoButton> infoButton;
    std::unique_ptr<vfd::InfoOverlay> infoOverlay;
    Page page = Page::sound;
    MotionTab motionTab = MotionTab::lfo;
    PresetFilter presetFilter = PresetFilter::factory;
    juce::String presetFolderFilter; // empty = all folders
    std::vector<Control> controls;
    std::vector<Switch> switches;
    std::unique_ptr<juce::Button> randomButton, locksButton, undoButton, redoButton, latestButton;
    std::vector<PresetItem> presetItems;
    juce::TextEditor presetSearch;
    juce::TextEditor presetNameEditor;
    juce::TextEditor presetFolderEditor;
    bool savingPreset = false;
    int selectedPreset = -1;
    int firstPreset = 0;
    juce::StringArray favorites;
    std::unique_ptr<juce::PropertiesFile> favoritesFile;
    std::array<float, 2> meterPeaks {0.0f, 0.0f};
    std::array<float, 64> inputSpectrum {}, outputSpectrum {};
    ShiftyProcessor::Display live;
    bool sizeReady = false;
    juce::TooltipWindow tooltips { this, 500 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShiftyEditor)
};

} // namespace shifty
