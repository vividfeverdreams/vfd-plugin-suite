#pragma once
#include <array>
#include "StutterProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>
#include <vector>

namespace vfd { class InfoButton; class InfoOverlay; }

// The teal enclosure is artwork; every label, value, graph and light is drawn here over it, in artwork pixels.
class StutterEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit StutterEditor (StutterProcessor&);
    ~StutterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    // The crop sits on the enclosure's outer edges in Assets/DeviceShell.png, so the device meets the window.
    static constexpr int cropX = 50, cropY = 54, cropW = 1024, cropH = 1304, baseWidth = 660;

private:
    struct Knob;
    class Lane;
    class Dots;
    class ScreenKey;
    class PlateKey;
    class SlapSwitch;
    class LockButton;
    class DiceButton;

    void timerCallback() override;
    float scale() const noexcept { return (float) getWidth() / (float) cropW; }
    juce::Rectangle<int> toEditor (juce::Rectangle<float> art) const;
    juce::Font font (float height, bool italic = false) const;
    void text (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float size, juce::Colour,
               juce::Justification = juce::Justification::centredLeft, bool italic = false, float kerning = 0.0f) const;

    void addKnob (const char* id, const char* label, float cx, float cy, float rFace, float rArc, float arcW,
                  float labelY, float valueY, float labelSize, float valueSize, bool slapGroup, const char* tooltip);
    void drawKnob (juce::Graphics&, const Knob&) const;
    juce::String knobValueText (const Knob&) const;
    float knobNormalised (const Knob&) const;
    void drawScreen (juce::Graphics&) const;
    void drawLanes (juce::Graphics&, const StutterProcessor::Snapshot&) const; // the screen below the name row
    void drawPlates (juce::Graphics&) const;
    void drawHeader (juce::Graphics&) const;
    void refreshShell();
    void applyScale (float);
    void setParam (const juce::String& id, float value);
    float param (const juce::String& id) const;
    std::uint64_t displayKey() const;

    StutterProcessor& proc;
    juce::Image shell, scaledShell;
    juce::Typeface::Ptr semiBold, boldItalic;
    std::unique_ptr<juce::LookAndFeel_V4> skin;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::unique_ptr<Lane> lane;
    std::unique_ptr<Dots> dots;
    std::unique_ptr<ScreenKey> rollFrom, prev, next, randomKey, subSafe, presetMenu, saveKey;
    std::array<std::unique_ptr<ScreenKey>, 4> rateKeys; // 1/4, 1/8, 1/16, 1/32
    void showPresetMenu();
    // SAVE: the groove name becomes a text field; Return (or OK) saves the groove to USER, Escape cancels.
    void beginNaming();
    void finishNaming (bool save);
    juce::TextEditor nameEditor;
    bool naming = false;
    double savedUntil = 0;   // the counter reads SAVED TO USER until then (ms)
    juce::String saveProblem;
    std::unique_ptr<PlateKey> lockKey, gapsKey, wideKey;
    std::unique_ptr<SlapSwitch> slapSwitch;
    std::unique_ptr<LockButton> swingLock;
    std::unique_ptr<DiceButton> dice;
    std::unique_ptr<juce::Slider> outputDrag;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::unique_ptr<vfd::InfoButton> infoButton;
    std::unique_ptr<vfd::InfoOverlay> infoOverlay;
    juce::TooltipWindow tooltips { this, 700 };

    float meterLeft = 0, meterRight = 0, ledLevel = 0;
    std::uint64_t lastKey = 0;
    bool sizeReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StutterEditor)
};
