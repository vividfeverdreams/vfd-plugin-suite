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
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    juce::String getTooltip() override;

    enum class Control { none, randomize, keepSub, phraseDuration,
                         phraseLock, dotted, triplet, shiftLeft, shiftRight,
                         previous, next, presets, save, load, pencil, eraser, undo, redo, drawOptions, shapes,
                         // 0.11: screen pages and the PERFORM page.
                         pagePattern, pagePerform, engage, quantize, retrigger, every, lookback,
                         mix, depth, output, slotA, slotB, slotC, slotD, slotE, slotF, slotG, slotH,
                         chain, split, history,
                         // 0.11.2: random preset key beside the browser arrows.
                         randomPreset };
    enum class DrawMode { free, line, curve, roll };
    DrawMode currentDrawMode() const noexcept { return drawMode; }
    bool showsPerformPage() const noexcept { return performPage; }
    static juce::Rectangle<float> controlBounds(Control) noexcept;
    static juce::Rectangle<float> patternBounds() noexcept;
    static juce::Rectangle<float> designViewport() noexcept;
    juce::AffineTransform designToEditor() const noexcept;
    // Size menu (right-click on the shell). Scales relative to 700 x 942.
    static constexpr std::array<float, 4> sizeChoices {0.9f, 1.0f, 1.25f, 1.5f};
    static bool sizeFits(float scale, juce::Rectangle<float> userArea) noexcept;
    void applyScale(float scale);
    // Test hooks: open the menus that a right-click on the shell or slot shows.
    void showSizeMenu();
    void showSlotMenu(int slot);
    // What the random preset key can load next: a factory bank index, or a user
    // preset file (bank -1). Public for tests.
    struct PresetChoice { int bank = -1; juce::File file; };
    std::vector<PresetChoice> randomPresetChoices() const;

private:
    void timerCallback() override;
    void activate(Control);
    bool isControlVisible(Control) const noexcept;
    void chooseFactoryPreset();
    void chooseDrawMode();
    void chooseScratchShape();
    void chooseChoice(Control);
    void closeEditMenu();
    void showEditMenu(juce::PopupMenu&, Control, std::function<void(int)>);
    void choosePresetFile(bool save);
    void saveToFolder(const juce::File& folder);
    void addUserPresets(juce::PopupMenu&, std::vector<juce::File>&);
    // Every way of choosing a preset (menus, arrows, Load, the random key) ends here.
    void loadBankPreset(int index);
    void loadUserPreset(const juce::File&);
    void loadRandomPreset();
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
    void drawPerformPage(juce::Graphics&, const BackScratchProcessor::DisplayState&);
    void drawPageTabs(juce::Graphics&, const BackScratchProcessor::DisplayState&);
    void drawPhraseControls(juce::Graphics&);
    void drawRandomize(juce::Graphics&);
    void drawSubChoice(juce::Graphics&);
    void drawFooter(juce::Graphics&, const BackScratchProcessor::DisplayState&);
    void beginGridGesture(juce::Point<float>);
    void updateGridGesture(juce::Point<float>);
    void finishGridGesture(bool commit);
    double graphDelayRange(const ScratchPattern&) const noexcept;
    double rollGrainPhase() const noexcept;
    // Horizontal bar controls on the PERFORM page.
    struct Bar { const char* id; float low, high, reset; };
    static std::optional<Bar> barFor(Control) noexcept;
    void dragBar(Control, juce::Point<float>);

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

    void paintEnclosure(juce::Graphics&);

    BackScratchProcessor& scratchProcessor;
    juce::Image shell, originalShell;
    // The enclosure composited once at the screen's physical resolution; repainted frames just blit it.
    juce::Image enclosureCache;
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
    // Roll tool: note value (1/8, 1/16, 1/32, 1/16T) and style.
    int rollRate = 1;
    RollStyle rollStyle = RollStyle::repeat;
    bool performPage = false;
    Control draggingBar = Control::none;
    float barStartValue = 0.0f;
    juce::Point<float> barStartPoint;
    std::vector<juce::File> menuFiles;
    // The user preset this editor loaded last and the name it showed then, so
    // the random key skips it even when the file was renamed after saving.
    juce::File userPresetFile;
    juce::String userPresetShownAs;
    juce::Random presetRandom;
    juce::Component::SafePointer<juce::Component> editMenu;
    unsigned menuGeneration = 0;
    juce::Point<float> graphHover;
    int eraseCell = 0;
    double strokePhase = 0.0, anchorPhase = 0.0;
    double strokeDelay = 0.0, anchorDelay = 0.0;
    double editDelayRange = 1.0;
    bool graphHovered = false;
    float flash = 0.0f;
    int blink = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackScratchEditor)
};

} // namespace backScratch
