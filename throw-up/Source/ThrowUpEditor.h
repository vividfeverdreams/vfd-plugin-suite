#pragma once
#include "ThrowUpProcessor.h"
#include "VfdHostKeys.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <memory>
#include <vector>

namespace vfd { class InfoButton; class InfoOverlay; }

class ThrowUpEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ThrowUpEditor (ThrowUpProcessor&);
    ~ThrowUpEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void visibilityChanged() override;
    std::unique_ptr<juce::ComponentTraverser> createKeyboardFocusTraverser() override
    { return vfd::HostKeys::createTraverser(); }

    // Pages of the top display: room tone, THROW trigger, space effects, mix.
    enum class Page { room = 0, trigger, space, mix };
    void showPage (Page);
    Page getPage() const noexcept { return page; }

    // Right-click on the enclosure opens the size menu. Sizes that do not fit
    // the display (width <= user area width, height + 60 <= user area height)
    // are disabled. The chosen size is saved with the project.
    static constexpr std::array<float, 4> menuScales { 0.9f, 1.0f, 1.25f, 1.5f };
    static bool scaleFits (float scale, juce::Rectangle<int> userArea);
    static float initialScale (float saved, juce::Rectangle<int> userArea);
    juce::PopupMenu createSizeMenu (juce::Rectangle<int> userArea);
    void applyScale (float scale);

    // The preset menu is rebuilt each time it opens, so new user presets appear.
    void rebuildPresetMenu();
    void openSaveDialog();
    // The dice key: loads a random preset the way the menu does. It draws from the
    // bank the arrows browse (the factory folders plus the user presets, or the 0.1.5
    // rooms while one of those is loaded) and never picks the preset already loaded.
    void loadRandomPreset();

private:
    struct Control;
    class Skin;
    class PresetBox;
    class KeyButton;
    class VisibilityWatcher;
    void timerCallback() override;
    void addControl (const char* id, const char* title, const char* tooltip,
                     juce::Rectangle<float> hit, juce::Rectangle<float> value,
                     bool physical = false, bool large = false, int controlPage = -1);
    void addPageControl (const char* id, const char* title, const char* tooltip, Page controlPage, int slot);
    juce::String valueText (const juce::String& id, double value) const;
    void updatePresetDisplay();
    void updateKeys();
    void stepPreset (int direction);
    // Every preset selection (menu, arrows, dice) loads through these.
    void selectFactoryPreset (int index);
    bool selectUserPreset (const juce::File&);
    void drawScreen (juce::Graphics&);
    void drawKnob (juce::Graphics&, Control&);
    void drawOutputKnob (juce::Graphics&, Control&);
    void drawFooter (juce::Graphics&);
    void drawMeters (juce::Graphics&, juce::Rectangle<float>);
    juce::Rectangle<int> scaled (juce::Rectangle<float>) const;
    void text (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float,
               juce::Colour, juce::Justification = juce::Justification::centredLeft, bool italic = false);

    ThrowUpProcessor& pluginProcessor;
    void paintShell (juce::Graphics&);
    juce::Image shell, scaledShell; // scaledShell: the enclosure at physical resolution for the current size
    juce::Typeface::Ptr normalTypeface, titleTypeface;
    std::unique_ptr<Skin> skin;
    std::vector<std::unique_ptr<Control>> controls;
    std::unique_ptr<PresetBox> presets;
    juce::TextButton previous { "<" }, next { ">" }, randomKey, bypass { "BYPASS" };
    juce::Random random;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<vfd::InfoButton> infoButton;
    std::unique_ptr<vfd::InfoOverlay> infoOverlay;
    juce::TooltipWindow tooltips { this, 600 };
    float uiScale = 1.0f, inputMeter = 0.0f, outputMeter = 0.0f;
    int inputClipHold = 0, outputClipHold = 0;
    int visiblePreset = -1;
    bool modified = false;
    // 0.2.0 controls.
    Page page = Page::room;
    std::array<juce::TextButton, 4> pageTabs;
    juce::TextButton saveKey { "SAVE" }, slotA { "A" }, slotB { "B" }, noteRoomsKey { "NOTE ROOMS" }, soundKey { "USE LATEST SOUND" };
    std::unique_ptr<KeyButton> throwKey, freezeKey, safeKey, sendKey;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> keyAttachments;
    std::vector<juce::File> userPresetFiles;
    juce::Component::SafePointer<juce::AlertWindow> dialog; // closed with the editor
    void showDialog (juce::AlertWindow*, std::function<void (int)> onResult);
    bool sizeReady = false;
    juce::File browsedUserPreset;
    std::unique_ptr<VisibilityWatcher> visibilityWatcher;
    // Constructed after the controls, destroyed before them.
    vfd::HostKeys hostKeys { *this, [this] (int step) { stepPreset (step); } };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThrowUpEditor)
};
