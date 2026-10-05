#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <optional>
#include "BackScratchProcessor.h"
#include "PatternEditing.h"

namespace vfd { class InfoButton; class InfoOverlay; }

namespace backScratch {

class BackScratchEditor final : public juce::AudioProcessorEditor, public juce::TooltipClient, private juce::Timer {
public:
    explicit BackScratchEditor(BackScratchProcessor&);
    ~BackScratchEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    juce::String getTooltip() override;

    enum class Control { none, randomize, keepSub, phraseDuration,
                         phraseLock, dotted, triplet, shiftLeft, shiftRight,
                         previous, next, presets, save, load, pencil, eraser, undo, redo, drawOptions, shapes };
    enum class DrawMode { free, line, curve };
    DrawMode currentDrawMode() const noexcept { return drawMode; }
    static juce::Rectangle<float> controlBounds(Control) noexcept;
    static juce::Rectangle<float> patternBounds() noexcept;
    static juce::Rectangle<float> designViewport() noexcept;
    juce::AffineTransform designToEditor() const noexcept;

private:
    void timerCallback() override;
    void activate(Control);
    void chooseFactoryPreset();
    void chooseDrawMode();
    void chooseScratchShape();
    void closeEditMenu();
    void showEditMenu(juce::PopupMenu&, Control, std::function<void(int)>);
    void choosePresetFile(bool save);
    void stepPreset(int direction);
    void updateHover(juce::Point<float>);
    Control controlAt(juce::Point<float>) const noexcept;
    static bool patternHitTest(juce::Point<float>) noexcept;
    juce::Point<float> designPoint(juce::Point<float>) const noexcept;
    void drawText(juce::Graphics&, const juce::String&, juce::Rectangle<float>, float,
                  juce::Colour, juce::Justification = juce::Justification::centred,
                  bool italic = false) const;
    void drawPresetBrowser(juce::Graphics&, const BackScratchProcessor::DisplayState&);
    void drawEditTools(juce::Graphics&);
    void drawPattern(juce::Graphics&, const BackScratchProcessor::DisplayState&);
    void drawPhraseControls(juce::Graphics&);
    void drawRandomize(juce::Graphics&);
    void drawSubChoice(juce::Graphics&);
    void drawFooter(juce::Graphics&, const BackScratchProcessor::DisplayState&);
    void beginGridGesture(juce::Point<float>);
    void updateGridGesture(juce::Point<float>);
    void finishGridGesture(bool commit);
    double graphDelayRange(const ScratchPattern&) const noexcept;

    class DurationKnob final : public juce::Slider {
    public:
        void mouseDown(const juce::MouseEvent& e) override {
            if (!e.mods.isLeftButtonDown()) return;
            dragStart.emplace(e);
            setMouseDragSensitivity(e.mods.isShiftDown() ? 672 : 168);
            juce::Slider::mouseDown(e);
        }
        void mouseUp(const juce::MouseEvent& e) override {
            juce::Slider::mouseUp(e);
            dragStart.reset();
        }
        void finishDrag() {
            if (!dragStart) return;
            const auto event = *dragStart;
            dragStart.reset();
            juce::Slider::mouseUp(event);
        }
    private:
        std::optional<juce::MouseEvent> dragStart;
    };

    BackScratchProcessor& scratchProcessor;
    juce::Image shell, originalShell;
    juce::Typeface::Ptr regularTypeface, italicTypeface;
    std::unique_ptr<juce::LookAndFeel> knobLookAndFeel;
    DurationKnob durationKnob;
    juce::TooltipWindow tooltipWindow {this, 700};
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> durationAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<vfd::InfoButton> infoButton;
    std::unique_ptr<vfd::InfoOverlay> infoOverlay;
    juce::String statusLine;
    bool statusIsError = false;
    Control pressed = Control::none, hovered = Control::none;
    Control editTool = Control::none;
    std::optional<ScratchPattern> editPreview, gestureOriginal;
    PatternFreeDrawStroke freeDrawStroke;
    DrawMode drawMode = DrawMode::free;
    juce::Component::SafePointer<juce::Component> editMenu;
    unsigned menuGeneration = 0;
    juce::Point<float> graphHover;
    int eraseCell = 0;
    double strokePhase = 0.0, anchorPhase = 0.0;
    double strokeDelay = 0.0, anchorDelay = 0.0;
    double editDelayRange = 1.0;
    bool graphHovered = false;
    float flash = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackScratchEditor)
};

} // namespace backScratch
