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
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    enum class Page { sound, motion, presets };
    enum class PresetFilter { factory, user, favorites };

    struct Control;
    struct Switch;
    struct PresetItem;

    void timerCallback() override;
    void addControl(const char* id, juce::Rectangle<float> hit,
                    juce::Rectangle<float> value, bool rotary, bool darkText);
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
    void showLocksMenu();
    void showParameterLockMenu(const juce::String& id, juce::Component* target);
    void drawHeader(juce::Graphics&);
    void drawSoundScreen(juce::Graphics&);
    void drawMotionScreen(juce::Graphics&);
    void drawPresetsScreen(juce::Graphics&);
    void drawControlPanel(juce::Graphics&);
    void drawUtilityPanel(juce::Graphics&);
    void drawKnob(juce::Graphics&, const char* id, juce::Point<float> centre,
                  float arcRadius, float pointerRadius, bool large);
    juce::Rectangle<int> scaled(juce::Rectangle<float>) const;
    juce::Point<float> designPoint(juce::Point<float>) const;
    void drawText(juce::Graphics&, const juce::String&, juce::Rectangle<float>,
                  float, juce::Colour, juce::Justification = juce::Justification::centred) const;

    ShiftyProcessor& plugin;
    juce::Image shell;
    juce::Image scaledShell;
    juce::Typeface::Ptr normalTypeface, displayTypeface;
    std::unique_ptr<vfd::InfoButton> infoButton;
    std::unique_ptr<vfd::InfoOverlay> infoOverlay;
    Page page = Page::sound;
    PresetFilter presetFilter = PresetFilter::factory;
    std::vector<Control> controls;
    std::vector<Switch> switches;
    std::unique_ptr<juce::Button> randomButton, locksButton, undoButton, redoButton;
    std::vector<PresetItem> presetItems;
    juce::TextEditor presetSearch;
    juce::TextEditor presetNameEditor;
    bool savingPreset = false;
    int selectedPreset = -1;
    int firstPreset = 0;
    juce::StringArray favorites;
    std::unique_ptr<juce::PropertiesFile> favoritesFile;
    std::array<float, 2> meterPeaks {0.0f, 0.0f};
    std::array<float, 64> inputSpectrum {}, outputSpectrum {};
    juce::TooltipWindow tooltips { this, 500 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShiftyEditor)
};

} // namespace shifty
