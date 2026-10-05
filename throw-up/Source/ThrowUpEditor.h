#pragma once
#include "ThrowUpProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
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

private:
    struct Control;
    class Skin;
    void timerCallback() override;
    void addControl (const char* id, const char* title, const char* tooltip,
                     juce::Rectangle<float> hit, juce::Rectangle<float> value,
                     bool physical = false, bool large = false);
    void updatePresetDisplay();
    void stepPreset (int direction);
    void drawScreen (juce::Graphics&);
    void drawKnob (juce::Graphics&, Control&);
    void drawFooter (juce::Graphics&);
    void drawMeters (juce::Graphics&, juce::Rectangle<float>);
    juce::Rectangle<int> scaled (juce::Rectangle<float>) const;
    void text (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float,
               juce::Colour, juce::Justification = juce::Justification::centredLeft, bool italic = false);

    ThrowUpProcessor& pluginProcessor;
    juce::Image shell, scaledShell;
    juce::Typeface::Ptr normalTypeface, titleTypeface;
    std::unique_ptr<Skin> skin;
    std::vector<std::unique_ptr<Control>> controls;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" }, bypass { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<vfd::InfoButton> infoButton;
    std::unique_ptr<vfd::InfoOverlay> infoOverlay;
    juce::TooltipWindow tooltips { this, 600 };
    float uiScale = 1.0f, inputMeter = 0.0f, outputMeter = 0.0f;
    int inputClipHold = 0, outputClipHold = 0;
    int visiblePreset = -1;
    bool modified = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThrowUpEditor)
};
