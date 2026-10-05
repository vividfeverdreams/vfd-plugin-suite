#include "BackScratchEditor.h"
#include "ScratchPattern.h"
#include "PatternEditing.h"
#include "BinaryData.h"
#include "VfdInfo.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace backScratch {

namespace {
constexpr float canvasWidth = 1122.0f, canvasHeight = 1402.0f;
const auto cream = juce::Colour::fromRGB(246, 232, 215);
const auto cyan = juce::Colour::fromRGB(179, 237, 242);
const auto deep = juce::Colour::fromRGB(22, 10, 13);
const auto burgundy = juce::Colour::fromRGB(76, 12, 22);
const auto red = juce::Colour::fromRGB(226, 41, 62);
const auto lime = juce::Colour(0xffd7f76c);
const auto midnight = juce::Colour(0xff071343);
juce::Rectangle<float> rectangle(float x, float y, float w, float h) { return {x, y, w, h}; }

// Shared visual language with YOI-BOI: rounded lime die, five dark pips,
// a cream physical cap and the same italic Chakra Petch label.
void drawDice(juce::Graphics& g, juce::Point<float> centre, float size)
{
    const auto face = rectangle(centre.x - size * 0.5f, centre.y - size * 0.5f, size, size);
    g.setColour(midnight.withAlpha(0.16f));
    g.fillRoundedRectangle(face.translated(1.0f, 3.0f), size * 0.16f);
    g.setGradientFill(juce::ColourGradient(lime.brighter(0.5f), face.getTopLeft(),
                                          lime, face.getBottomRight(), false));
    g.fillRoundedRectangle(face, size * 0.16f);
    g.setColour(juce::Colour(0xff70bd13));
    g.drawRoundedRectangle(face.reduced(0.8f), size * 0.16f, 1.8f);
    g.setColour(midnight);
    const float offset = size * 0.235f, radius = size * 0.065f;
    for (const auto dot : std::array<juce::Point<float>, 5> {{{-offset, -offset}, {offset, -offset},
                                                            {0, 0}, {-offset, offset}, {offset, offset}}})
        g.fillEllipse(centre.x + dot.x - radius, centre.y + dot.y - radius,
                      radius * 2.0f, radius * 2.0f);
}

void drawLock(juce::Graphics& g, juce::Point<float> centre, bool locked)
{
    juce::Path shackle;
    if (locked) {
        shackle.startNewSubPath(centre.x - 7, centre.y);
        shackle.lineTo(centre.x - 7, centre.y - 7);
        shackle.cubicTo(centre.x - 7, centre.y - 17, centre.x + 7, centre.y - 17,
                       centre.x + 7, centre.y - 7);
        shackle.lineTo(centre.x + 7, centre.y);
    } else {
        shackle.startNewSubPath(centre.x - 7, centre.y);
        shackle.lineTo(centre.x - 7, centre.y - 7);
        shackle.cubicTo(centre.x - 7, centre.y - 18, centre.x + 8, centre.y - 18,
                       centre.x + 8, centre.y - 8);
    }
    g.strokePath(shackle, juce::PathStrokeType(2.8f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    g.fillRoundedRectangle(rectangle(centre.x - 11, centre.y - 1, 22, 17), 4.0f);
}

// Menu previews are rendered from the same scratch trajectory as the main
// screen, so the little contour is a useful starting point, not decoration.
class EditMenuRow final : public juce::PopupMenu::CustomComponent {
public:
    EditMenuRow(juce::String titleIn, juce::String detailIn, int modeIn,
                std::optional<ScratchPattern> shapeIn, bool selectedIn, juce::Typeface::Ptr faceIn)
        : title(std::move(titleIn)), detail(std::move(detailIn)), shape(std::move(shapeIn)),
          face(std::move(faceIn)), mode(modeIn), selected(selectedIn)
    {
        setName(title);
    }
    void getIdealSize(int& width, int& height) override { width = 286; height = detail.isEmpty() ? 42 : 53; }
    void paint(juce::Graphics& g) override {
        const auto box = getLocalBounds().toFloat();
        if (isItemHighlighted()) {
            g.setColour(burgundy.brighter(0.25f));
            g.fillRoundedRectangle(box.reduced(3, 1), 5.0f);
        }
        const auto chart = juce::Rectangle<float>(12, box.getCentreY() - 12, 72, 24);
        g.setColour(cyan.withAlpha(0.13f));
        g.drawRoundedRectangle(chart.expanded(3, 4), 3.0f, 1.0f);
        juce::Path contour;
        if (shape) {
            const double range = patternEditingDelayRange(*shape);
            for (int i = 0; i <= 96; ++i) {
                const double phase = std::min(1.0 - 1.0e-9, i / 96.0);
                const auto value = evaluatePattern(*shape, phase * shape->beats);
                const auto point = juce::Point<float>(chart.getX() + static_cast<float>(phase) * chart.getWidth(),
                    chart.getBottom() - static_cast<float>(value.delayBeats / range) * chart.getHeight());
                if (i == 0) contour.startNewSubPath(point); else contour.lineTo(point);
            }
        } else {
            contour.startNewSubPath(chart.getX(), chart.getBottom() - 3);
            if (mode == 0) {
                contour.cubicTo(chart.getX() + 9, chart.getBottom() - 3, chart.getX() + 10, chart.getY(), chart.getX() + 23, chart.getY() + 2);
                contour.cubicTo(chart.getX() + 38, chart.getY() + 3, chart.getX() + 40, chart.getBottom(), chart.getX() + 49, chart.getBottom() - 3);
                contour.cubicTo(chart.getX() + 60, chart.getBottom() - 5, chart.getX() + 62, chart.getY() + 3, chart.getRight(), chart.getY() + 4);
            } else if (mode == 1) contour.lineTo(chart.getRight(), chart.getY() + 3);
            else contour.cubicTo(chart.getCentreX(), chart.getBottom() - 3, chart.getCentreX(), chart.getY() + 3, chart.getRight(), chart.getY() + 3);
        }
        g.setColour(cyan);
        g.strokePath(contour, juce::PathStrokeType(1.65f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont(juce::Font(juce::FontOptions(face).withHeight(15.0f)));
        g.setColour(isItemHighlighted() ? cyan : cream);
        g.drawFittedText(title, juce::Rectangle<int>(99, detail.isEmpty() ? 8 : 6, getWidth() - 120, 25), juce::Justification::centredLeft, 1);
        if (detail.isNotEmpty()) {
            g.setFont(juce::Font(juce::FontOptions(face).withHeight(11.5f)));
            g.setColour(cream.withAlpha(0.58f));
            g.drawFittedText(detail, juce::Rectangle<int>(99, 28, getWidth() - 108, 17), juce::Justification::centredLeft, 1);
        }
        if (selected) {
            g.setColour(cyan);
            g.fillEllipse(static_cast<float>(getWidth() - 15), box.getCentreY() - 2.5f, 5, 5);
        }
    }
private:
    juce::String title, detail;
    std::optional<ScratchPattern> shape;
    juce::Typeface::Ptr face;
    int mode;
    bool selected;
};

class DurationKnobLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    DurationKnobLookAndFeel() {
        setColour(juce::PopupMenu::backgroundColourId, deep);
        setColour(juce::PopupMenu::textColourId, cream);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, burgundy.brighter(0.35f));
        setColour(juce::PopupMenu::highlightedTextColourId, cyan);
        setColour(juce::TooltipWindow::backgroundColourId, deep);
        setColour(juce::TooltipWindow::textColourId, cream);
        setColour(juce::TooltipWindow::outlineColourId, cream.withAlpha(0.35f));
    }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float position, float startAngle, float endAngle, juce::Slider& slider) override
    {
        // The sculpted cap is photographed in the panel. Only live markings sit
        // on top, as in the other two devices in this family.
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                   static_cast<float>(width), static_cast<float>(height));
        const auto centre = bounds.getCentre();
        const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto pointAt = [centre](float angle, float distance) {
            return centre + juce::Point<float>(std::sin(angle) * distance, -std::cos(angle) * distance);
        };
        for (int tick = 0; tick < BackScratchProcessor::phraseDurationCount; ++tick) {
            const float angle = startAngle + (endAngle - startAngle) * tick
                                                 / (BackScratchProcessor::phraseDurationCount - 1);
            g.setColour(tick <= juce::roundToInt(position * (BackScratchProcessor::phraseDurationCount - 1))
                            ? cream : burgundy.withAlpha(0.45f));
            g.drawLine({pointAt(angle, radius * 0.915f), pointAt(angle, radius * 0.975f)}, 2.2f);
        }
        const float angle = startAngle + position * (endAngle - startAngle);
        const auto tip = pointAt(angle, radius * 0.63f);
        const auto tail = pointAt(angle, radius * 0.33f);
        g.setColour(burgundy.darker(0.25f));
        g.drawLine({tail, tip}, radius * 0.068f);
        g.fillEllipse(tip.x - radius * 0.034f, tip.y - radius * 0.034f,
                      radius * 0.068f, radius * 0.068f);
        if (slider.isMouseOverOrDragging()) {
            g.setColour(cream.withAlpha(0.5f));
            g.drawEllipse(bounds.reduced(radius * 0.15f), 1.3f);
        }
    }
};
}

BackScratchEditor::BackScratchEditor(BackScratchProcessor& owner)
    : AudioProcessorEditor(owner), scratchProcessor(owner)
{
    shell = juce::ImageFileFormat::loadFrom(BinaryData::Interiorv7_png, BinaryData::Interiorv7_pngSize);
    originalShell = juce::ImageFileFormat::loadFrom(BinaryData::Shell_png, BinaryData::Shell_pngSize);
    regularTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::ChakraPetchSemiBold_ttf,
                                                              BinaryData::ChakraPetchSemiBold_ttfSize);
    italicTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::ChakraPetchBoldItalic_ttf,
                                                             BinaryData::ChakraPetchBoldItalic_ttfSize);
    knobLookAndFeel = std::make_unique<DurationKnobLookAndFeel>();
    setLookAndFeel(knobLookAndFeel.get());
    durationKnob.setLookAndFeel(knobLookAndFeel.get());
    durationKnob.setName("Phrase length");
    durationKnob.setComponentID("phrase-duration");
    durationKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    durationKnob.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                     juce::MathConstants<float>::pi * 2.75f, true);
    durationKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    durationKnob.setRange(0, BackScratchProcessor::phraseDurationCount - 1, 1);
    durationKnob.setScrollWheelEnabled(true);
    durationKnob.setDoubleClickReturnValue(true, 3.0);
    durationKnob.setWantsKeyboardFocus(false);
    durationKnob.setMouseClickGrabsKeyboardFocus(false);
    durationKnob.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    durationKnob.setTooltip("Drag up or down to change length. Shift-drag for fine control. Double-click for 1 bar.");
    durationKnob.onValueChange = [this] { statusLine.clear(); repaint(); };
    addAndMakeVisible(durationKnob);
    durationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        owner.parameters(), "phraseDuration", durationKnob);
    const auto infoTheme = vfd::themeFor("BACK-SCRATCH");
    infoButton = std::make_unique<vfd::InfoButton>(infoTheme, regularTypeface);
    infoButton->setName("INFO");
    infoOverlay = std::make_unique<vfd::InfoOverlay>("BACK-SCRATCH", infoTheme,
        BinaryData::VfdLogo_svg, BinaryData::VfdLogo_svgSize, regularTypeface);
    infoButton->onClick = [this] { infoOverlay->open(infoButton.get()); };
    addAndMakeVisible(*infoButton);
    addChildComponent(*infoOverlay);
    setSize(700, 940);
    setWantsKeyboardFocus(true);
    setTitle("BACK-SCRATCH — live scratch variations");
    startTimerHz(30);
}

BackScratchEditor::~BackScratchEditor()
{
    stopTimer();
    closeEditMenu();
    finishGridGesture(false);
    durationKnob.finishDrag();
    durationAttachment.reset();
    durationKnob.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

juce::Rectangle<float> BackScratchEditor::controlBounds(Control control) noexcept
{
    switch (control) {
        case Control::randomize: return rectangle(619, 723, 264, 154);
        case Control::keepSub:   return rectangle(221, 1034, 326, 129);
        case Control::phraseDuration: return rectangle(253, 643, 310, 310);
        case Control::phraseLock: return rectangle(569, 651, 44, 44);
        case Control::dotted:   return rectangle(633, 651, 117, 44);
        case Control::triplet:  return rectangle(765, 651, 119, 44);
        case Control::shiftLeft: return rectangle(520, 520, 34, 28);
        case Control::shiftRight: return rectangle(570, 520, 34, 28);
        case Control::previous: return rectangle(792, 251, 47, 47);
        case Control::next:     return rectangle(848, 251, 47, 47);
        case Control::presets:  return rectangle(229, 236, 546, 68);
        case Control::save:     return rectangle(596, 99, 140, 37);
        case Control::load:     return rectangle(749, 99, 140, 37);
        case Control::pencil:   return rectangle(476, 195, 112, 38);
        case Control::drawOptions: return rectangle(588, 195, 26, 38);
        case Control::shapes:   return rectangle(622, 195, 106, 38);
        case Control::eraser:   return rectangle(736, 195, 42, 38);
        case Control::undo:     return rectangle(804, 195, 42, 38);
        case Control::redo:     return rectangle(852, 195, 42, 38);
        case Control::none:     break;
    }
    return {};
}

juce::Rectangle<float> BackScratchEditor::patternBounds() noexcept
{
    return rectangle(230, 314, 664, 180);
}

bool BackScratchEditor::patternHitTest(juce::Point<float> point) noexcept
{
    const auto bounds = patternBounds();
    // Both the baseline and the final grid boundary are drawing surfaces.
    // Allow only floating-point round-trip error from editor scaling; this
    // must not extend the editing area into the labels or nearby controls.
    constexpr float edgeTolerance = 0.001f;
    return point.x >= bounds.getX() - edgeTolerance && point.x <= bounds.getRight() + edgeTolerance
        && point.y >= bounds.getY() - edgeTolerance && point.y <= bounds.getBottom() + edgeTolerance;
}

juce::Rectangle<float> BackScratchEditor::designViewport() noexcept
{
    // Keep the original enclosure intact; remove the dark image padding around it.
    return rectangle(70, 47, 980, 1316);
}

juce::AffineTransform BackScratchEditor::designToEditor() const noexcept
{
    const auto view = designViewport();
    return juce::AffineTransform::translation(-view.getX(), -view.getY())
        .scaled(static_cast<float>(getWidth()) / view.getWidth(),
                static_cast<float>(getHeight()) / view.getHeight());
}

void BackScratchEditor::resized()
{
    durationKnob.setBounds(controlBounds(Control::phraseDuration).transformedBy(designToEditor()).toNearestInt());
    if (infoButton) infoButton->setBounds(juce::Rectangle<float>(302, 832, 96, 38)
        .transformedBy(juce::AffineTransform::scale(getWidth() / 700.0f, getHeight() / 940.0f)).toNearestInt());
    if (infoOverlay) infoOverlay->setBounds(getLocalBounds());
}

juce::Point<float> BackScratchEditor::designPoint(juce::Point<float> point) const noexcept
{
    return point.transformedBy(designToEditor().inverted());
}

BackScratchEditor::Control BackScratchEditor::controlAt(juce::Point<float> p) const noexcept
{
    for (const auto control : { Control::randomize, Control::keepSub, Control::phraseLock,
                               Control::dotted, Control::triplet, Control::shiftLeft, Control::shiftRight,
                               Control::previous, Control::next, Control::presets, Control::save, Control::load,
                               Control::pencil, Control::eraser, Control::undo, Control::redo, Control::drawOptions, Control::shapes })
        if (controlBounds(control).contains(p)) return control;
    return Control::none;
}

void BackScratchEditor::drawText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> box,
                                float size, juce::Colour colour, juce::Justification align, bool italic) const
{
    g.setColour(colour);
    g.setFont(juce::Font(juce::FontOptions(italic ? italicTypeface : regularTypeface).withHeight(size)));
    g.drawFittedText(text, box.toNearestInt(), align, 1, 0.72f);
}

void BackScratchEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(28, 4, 7));
    g.addTransform(designToEditor());
    if (shell.isValid()) g.drawImage(shell, rectangle(0, 0, canvasWidth, canvasHeight));
    // Keep the original hardware light completely intact. No banner or control
    // extends into this reserved physical part of the enclosure.
    if (originalShell.isValid()) {
        juce::Graphics::ScopedSaveState saved(g);
        juce::Path light; light.addEllipse(905, 92, 52, 55);
        g.reduceClipRegion(light);
        g.drawImage(originalShell, rectangle(0, 0, canvasWidth, canvasHeight));
    }
    const auto display = scratchProcessor.displayState();
    drawPresetBrowser(g, display);
    drawEditTools(g);
    drawPattern(g, display);
    drawPhraseControls(g);
    drawRandomize(g);
    drawSubChoice(g);
    drawFooter(g, display);
}

void BackScratchEditor::drawPresetBrowser(juce::Graphics& g, const BackScratchProcessor::DisplayState& display)
{
    for (const auto control : {Control::save, Control::load}) {
        const auto box = controlBounds(control);
        const bool active = hovered == control;
        if (active) {
            g.setColour(cream.withAlpha(pressed == control ? 0.20f : 0.10f));
            g.fillRoundedRectangle(box, 7.0f);
        }
        drawText(g, control == Control::save ? "SAVE" : "LOAD", box, 19, active ? cyan : cream);
    }
    g.setColour(cream.withAlpha(0.18f));
    g.drawVerticalLine(742, 105, 131);
    const bool generated = display.generated;
    const auto& preset = factoryPresets()[static_cast<size_t>(display.factoryIndex)];
    const bool usingLiveInput = display.playing && display.scratchActive && !display.historyReady;
    const auto category = display.edited || editPreview.has_value() ? juce::String("EDITED PATTERN")
        : generated ? juce::String("GENERATED PATTERN") : juce::String(preset.category) + " / FACTORY";
    const auto eyebrow = statusLine.isNotEmpty() ? statusLine
        : editTool == Control::pencil ? (drawMode == DrawMode::free ? "FREE DRAW / FOLLOW YOUR HAND"
            : drawMode == DrawMode::line ? "LINE / DRAG TWO POINTS" : "CURVE / DRAG A SWEEP")
        : editTool == Control::eraser ? "ERASE / DRAG TO RESTORE AUDIO"
        : category + (usingLiveInput ? " / LIVE INPUT" : "");
    drawText(g, eyebrow, rectangle(230, 207, 232, 26), 15,
             statusLine.isNotEmpty() && statusIsError ? juce::Colour::fromRGB(255, 164, 153) : cyan,
             juce::Justification::centredLeft);
    const bool sameIdentity = display.edited == scratchProcessor.hasCustomPattern()
        && generated == scratchProcessor.isGenerated()
        && (generated ? display.seed == scratchProcessor.generationSeed()
                      : display.factoryIndex == scratchProcessor.selectedFactoryPreset()
                            && display.seed == scratchProcessor.variationSeed())
        && display.durationChoice == scratchProcessor.phraseDurationChoice()
        && display.timingChoice == scratchProcessor.phraseTimingChoice()
        && display.pattern.gridOffset == scratchProcessor.patternShiftSteps();
    auto name = editPreview ? juce::String("Edited Scratch") : sameIdentity ? scratchProcessor.currentPresetName()
                            : display.edited ? juce::String("Edited Scratch")
                            : generated ? juce::String("New Scratch") : juce::String(preset.name);
    if (generated && name.startsWith("New Scratch /")) name = "New Scratch";
    drawText(g, name, rectangle(229, 241, 523, 58), 38, cream,
             juce::Justification::centredLeft, true);
    if (hovered == Control::presets) {
        g.setColour(cyan.withAlpha(0.5f));
        g.drawHorizontalLine(303, 230, 747);
    }
    juce::Path chevron;
    chevron.startNewSubPath(755, 268); chevron.lineTo(763, 276); chevron.lineTo(771, 268);
    g.setColour(cyan);
    g.strokePath(chevron, juce::PathStrokeType(2.0f));
    for (const auto control : {Control::previous, Control::next}) {
        const auto box = controlBounds(control);
        g.setColour(cyan.withAlpha(hovered == control ? 0.18f : 0.05f));
        g.fillRoundedRectangle(box, 9.0f);
        g.setColour(cream.withAlpha(0.22f));
        g.drawRoundedRectangle(box, 9.0f, 1.0f);
        const auto c = box.getCentre();
        const float direction = control == Control::previous ? -1.0f : 1.0f;
        juce::Path arrow;
        arrow.startNewSubPath(c.x - direction * 4, c.y - 8);
        arrow.lineTo(c.x + direction * 4, c.y);
        arrow.lineTo(c.x - direction * 4, c.y + 8);
        g.setColour(cream);
        g.strokePath(arrow, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void BackScratchEditor::drawEditTools(juce::Graphics& g)
{
    for (const auto control : {Control::pencil, Control::drawOptions, Control::shapes,
                               Control::eraser, Control::undo, Control::redo}) {
        const auto box = controlBounds(control);
        const bool selected = editTool == control || (control == Control::drawOptions && editTool == Control::pencil);
        const bool enabled = control == Control::undo ? scratchProcessor.canUndoPatternEdit()
            : control == Control::redo ? scratchProcessor.canRedoPatternEdit() : true;
        const bool down = pressed == control && hovered == control;
        const bool split = control == Control::pencil || control == Control::drawOptions;
        g.setColour(cyan.withAlpha(selected ? 0.20f : down ? 0.15f : hovered == control && enabled ? 0.10f : 0.025f));
        if (split) {
            juce::Graphics::ScopedSaveState clip(g);
            g.reduceClipRegion(box.toNearestInt());
            g.fillRoundedRectangle(controlBounds(Control::pencil).getUnion(controlBounds(Control::drawOptions)), 7.0f);
            g.setColour((selected ? cyan : cream).withAlpha(selected ? 0.65f : 0.16f));
            g.drawRoundedRectangle(controlBounds(Control::pencil).getUnion(controlBounds(Control::drawOptions)).reduced(0.5f), 6.5f, 1.0f);
        } else {
            g.fillRoundedRectangle(box, 7.0f);
            g.setColour((selected ? cyan : cream).withAlpha(selected ? 0.65f : enabled ? 0.16f : 0.06f));
            g.drawRoundedRectangle(box.reduced(0.5f), 6.5f, 1.0f);
        }
        const auto colour = (selected ? cyan : cream).withAlpha(enabled ? 0.95f : 0.22f);
        g.setColour(colour);
        const auto c = (control == Control::pencil ? juce::Point<float>(box.getX() + 20, box.getCentreY())
                                                    : box.getCentre()).translated(0, down ? 1.0f : 0.0f);
        juce::Path symbol;
        if (control == Control::drawOptions || control == Control::shapes) {
            const float x = control == Control::shapes ? box.getRight() - 13 : c.x;
            symbol.startNewSubPath(x - 4, c.y - 2);
            symbol.lineTo(x, c.y + 2);
            symbol.lineTo(x + 4, c.y - 2);
            if (control == Control::drawOptions) {
                g.setColour(cyan.withAlpha(0.20f));
                g.drawVerticalLine(juce::roundToInt(box.getX()), box.getY() + 8, box.getBottom() - 8);
            } else {
                drawText(g, "SHAPES", box.withTrimmedLeft(8).withTrimmedRight(24), 16, cream);
            }
        } else if (control == Control::pencil) {
            if (drawMode == DrawMode::free) {
                symbol.startNewSubPath(c.x - 9, c.y + 9);
                symbol.lineTo(c.x - 7, c.y + 2);
                symbol.lineTo(c.x + 5, c.y - 10);
                symbol.lineTo(c.x + 10, c.y - 5);
                symbol.lineTo(c.x - 2, c.y + 7);
                symbol.closeSubPath();
                symbol.startNewSubPath(c.x + 2, c.y - 7);
                symbol.lineTo(c.x + 7, c.y - 2);
            } else {
                symbol.startNewSubPath(c.x - 9, c.y + 6);
                if (drawMode == DrawMode::curve)
                    symbol.cubicTo(c.x, c.y + 6, c.x - 1, c.y - 6, c.x + 9, c.y - 6);
                else symbol.lineTo(c.x + 9, c.y - 6);
                g.fillEllipse(c.x - 11, c.y + 4, 4, 4);
                g.fillEllipse(c.x + 7, c.y - 8, 4, 4);
            }
            drawText(g, drawMode == DrawMode::free ? "FREE" : drawMode == DrawMode::line ? "LINE" : "CURVE",
                     box.withTrimmedLeft(38).withTrimmedRight(7), 16, colour);
        } else if (control == Control::eraser) {
            symbol.startNewSubPath(c.x - 10, c.y + 2);
            symbol.lineTo(c.x + 2, c.y - 10);
            symbol.lineTo(c.x + 11, c.y - 1);
            symbol.lineTo(c.x + 1, c.y + 9);
            symbol.lineTo(c.x - 3, c.y + 9);
            symbol.closeSubPath();
            symbol.startNewSubPath(c.x - 5, c.y - 3);
            symbol.lineTo(c.x + 4, c.y + 6);
            symbol.startNewSubPath(c.x - 1, c.y + 10);
            symbol.lineTo(c.x + 11, c.y + 10);
        } else {
            const float d = control == Control::undo ? 1.0f : -1.0f;
            symbol.startNewSubPath(c.x - d * 9, c.y - 3);
            symbol.cubicTo(c.x + d * 9, c.y - 13, c.x + d * 15, c.y + 10, c.x + d, c.y + 9);
            symbol.startNewSubPath(c.x - d * 3, c.y - 10);
            symbol.lineTo(c.x - d * 9, c.y - 3);
            symbol.lineTo(c.x - d * 1, c.y + 1);
        }
        g.setColour(colour);
        g.strokePath(symbol, juce::PathStrokeType(2.1f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }
}

void BackScratchEditor::drawPattern(juce::Graphics& g, const BackScratchProcessor::DisplayState& display)
{
    const auto& pattern = editPreview ? *editPreview : display.pattern;
    const double phraseBeats = pattern.beats;
    const auto plot = patternBounds();
    const auto graph = plot;
    const double barBeats = std::clamp(display.barBeats, 0.25, 16.0);
    // The nudge controls and the display share exactly sixteen cells at every
    // duration, including dotted and triplet phrases.
    for (int cell = 0; cell <= patternGridDivisions; ++cell) {
        g.setColour(cyan.withAlpha(cell % 4 == 0 ? 0.16f : 0.07f));
        g.drawVerticalLine(juce::roundToInt(graph.getX() + static_cast<float>(cell)
                                         / patternGridDivisions * graph.getWidth()),
                           graph.getY(), graph.getBottom());
    }
    for (int row = 0; row <= 4; ++row) {
        g.setColour(cyan.withAlpha(row == 0 || row == 4 ? 0.13f : 0.06f));
        g.drawHorizontalLine(juce::roundToInt(graph.getY() + row * graph.getHeight() / 4),
                             graph.getX(), graph.getRight());
    }
    const double labelStride = phraseBeats >= barBeats ? barBeats : 1.0;
    for (int index = 0; index * labelStride < phraseBeats; ++index)
        drawText(g, juce::String(index + 1),
                 rectangle(graph.getX() + static_cast<float>(index * labelStride / phraseBeats) * graph.getWidth() + 3,
                           graph.getBottom() + 6,
                           std::min(1.0f, static_cast<float>(labelStride / phraseBeats)) * graph.getWidth() - 5, 18),
                 13, cream.withAlpha(0.4f), juce::Justification::centredLeft);
    const double maxDelay = editPreview ? editDelayRange : graphDelayRange(pattern);
    if (editTool == Control::eraser && graphHovered) {
        const auto x = std::clamp(graphHover.x, graph.getX(), graph.getRight());
        const int cell = std::clamp(static_cast<int>((x - graph.getX()) / graph.getWidth()
                                                   * patternGridDivisions), 0, patternGridDivisions - 1);
        const float width = graph.getWidth() / patternGridDivisions;
        g.setColour((editTool == Control::eraser ? red : cyan).withAlpha(0.10f));
        g.fillRect(rectangle(graph.getX() + cell * width, graph.getY(), width, graph.getHeight()));
    }
    const double offset = patternOffsetBeats(pattern);
    // Split only the visible spans where they wrap across the display edge.
    // Their source coordinates remain intact so a curved gesture does not
    // change its shape when part of it moves onto the other side of the graph.
    for (int segment = 0; segment < pattern.count; ++segment) {
        const auto& part = pattern.segments[static_cast<size_t>(segment)];
        if (part.end <= part.start || pattern.beats <= 0) continue;
        for (int wrap = 0; wrap <= 1; ++wrap) {
            const double start = std::max(0.0, part.start + offset - wrap * pattern.beats);
            const double end = std::min(pattern.beats, part.end + offset - wrap * pattern.beats);
            if (end <= start) continue;
            std::array<juce::Point<float>, 33> points;
            std::array<float, 33> gates;
            juce::Path line;
            for (int sample = 0; sample <= 32; ++sample) {
                const double beat = start + (end - start) * sample / 32.0;
                const double source = beat + wrap * pattern.beats - offset;
                const auto value = evaluatePatternSegment(pattern, segment, source);
                const auto point = juce::Point<float>(
                    graph.getX() + static_cast<float>(beat / pattern.beats) * graph.getWidth(),
                    graph.getBottom() - static_cast<float>(value.delayBeats / maxDelay) * graph.getHeight());
                points[static_cast<size_t>(sample)] = point;
                gates[static_cast<size_t>(sample)] = value.gate;
                if (sample == 0) line.startNewSubPath(point); else line.lineTo(point);
            }
            // Faint strokes retain the shape of closed gates; brightness shows
            // the audible part of the gesture instead of lighting muted chops.
            g.setColour((part.wet > 0.001f ? cyan : cream).withAlpha(part.wet > 0.001f ? 0.13f : 0.30f));
            g.strokePath(line, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
            if (part.wet <= 0.001f) continue;
            const int chunks = std::abs(part.gateStart - part.gateEnd) < 0.001f ? 1 : 8;
            const int pointsPerChunk = 32 / chunks;
            for (int chunk = 0; chunk < chunks; ++chunk) {
                const int first = chunk * pointsPerChunk, last = (chunk + 1) * pointsPerChunk;
                juce::Path audible;
                audible.startNewSubPath(points[static_cast<size_t>(first)]);
                for (int point = first + 1; point <= last; ++point)
                    audible.lineTo(points[static_cast<size_t>(point)]);
                const float gate = std::clamp((gates[static_cast<size_t>(first)]
                                               + gates[static_cast<size_t>(last)]) * 0.5f, 0.0f, 1.0f);
                if (gate < 0.001f) continue;
                g.setColour(cyan.withAlpha(0.07f * gate));
                g.strokePath(audible, juce::PathStrokeType(13.0f));
                g.setColour(cyan.withAlpha(0.13f * gate));
                g.strokePath(audible, juce::PathStrokeType(7.0f));
                g.setColour(cyan.withAlpha(0.96f * gate));
                g.strokePath(audible, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
            }
        }
    }
    if (display.playing) {
        const float phase = std::clamp(display.phase, 0.0f, 1.0f);
        const float x = graph.getX() + phase * graph.getWidth();
        g.setColour(cream.withAlpha(0.08f));
        g.fillRect(rectangle(graph.getX(), graph.getY(), x - graph.getX(), graph.getHeight()));
        g.setColour(cream.withAlpha(0.8f));
        g.drawVerticalLine(juce::roundToInt(x), plot.getY() + 7, plot.getBottom() - 7);
        g.fillEllipse(x - 3, graph.getBottom() + 8, 6, 6);
        const auto active = editPreview ? evaluatePattern(pattern, phase * pattern.beats) : display.activeSample;
        const float y = graph.getBottom() - static_cast<float>(active.delayBeats / maxDelay) * graph.getHeight();
        g.setColour(cream.withAlpha(active.wet > 0.001f ? 0.2f + active.gate * 0.8f : 0.5f));
        g.fillEllipse(x - 4, y - 4, 8, 8);
    }
    if (editTool != Control::none) {
        g.setColour((editTool == Control::eraser ? red : cyan).withAlpha(graphHovered ? 0.55f : 0.25f));
        g.drawRect(graph, 1.0f);
        if (graphHovered && editTool == Control::pencil) {
            const double phase = editPreview ? strokePhase
                : std::clamp(static_cast<double>((graphHover.x - graph.getX()) / graph.getWidth()), 0.0, 1.0);
            const double depth = editPreview ? strokeDelay
                : std::clamp(static_cast<double>((graph.getBottom() - graphHover.y) / graph.getHeight()), 0.0, 1.0) * maxDelay;
            const float x = graph.getX() + static_cast<float>(phase) * graph.getWidth();
            const float y = graph.getBottom() - static_cast<float>(depth / maxDelay) * graph.getHeight();
            g.setColour(cyan.withAlpha(0.23f));
            g.drawHorizontalLine(juce::roundToInt(y), graph.getX(), graph.getRight());
            g.drawVerticalLine(juce::roundToInt(x), graph.getY(), graph.getBottom());
            g.setColour(cyan);
            g.drawEllipse(x - 4.0f, y - 4.0f, 8.0f, 8.0f, 1.3f);
        }
    }
    g.setColour(cyan.withAlpha(0.17f));
    g.drawHorizontalLine(519, 230, 894);
    drawText(g, "PHRASE", rectangle(230, 522, 89, 25), 15, cream.withAlpha(0.55f), juce::Justification::centredLeft);
    drawText(g, BackScratchProcessor::durationLabel(display.durationChoice, display.timingChoice).toUpperCase(),
             rectangle(327, 520, 183, 27), 18, cyan, juce::Justification::centredLeft);
    const juce::String identity = display.edited || editPreview ? "EDITED" : display.generated
        ? "SEED " + juce::String(display.seed)
        : juce::String(display.factoryIndex + 1).paddedLeft('0', 2) + " / 75";
    drawText(g, identity, rectangle(683, 522, 211, 25), 15, cream.withAlpha(0.55f), juce::Justification::centredRight);
    for (const auto control : {Control::shiftLeft, Control::shiftRight}) {
        const auto box = controlBounds(control);
        if (hovered == control || pressed == control) {
            g.setColour(cyan.withAlpha(pressed == control ? 0.18f : 0.08f));
            g.fillRoundedRectangle(box, 5.0f);
        }
        const auto centre = box.getCentre();
        const float direction = control == Control::shiftLeft ? -1.0f : 1.0f;
        juce::Path arrow;
        arrow.startNewSubPath(centre.x - direction * 4, centre.y - 5);
        arrow.lineTo(centre.x + direction * 2, centre.y);
        arrow.lineTo(centre.x - direction * 4, centre.y + 5);
        g.setColour(hovered == control ? cyan : cream.withAlpha(0.7f));
        g.strokePath(arrow, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}

void BackScratchEditor::drawPhraseControls(juce::Graphics& g)
{
    drawText(g, "LENGTH", rectangle(291, 620, 232, 28), 22, burgundy.darker(0.3f));
    drawText(g, scratchProcessor.phraseDurationLabel().toUpperCase(), rectangle(281, 953, 254, 32),
             26, burgundy.darker(0.3f));
    const auto box = controlBounds(Control::phraseLock);
    const bool locked = scratchProcessor.phraseLocked();
    if (locked || hovered == Control::phraseLock) {
        g.setColour(cream.withAlpha(locked ? 0.62f : 0.25f));
        g.fillRoundedRectangle(box.reduced(3), 9.0f);
    }
    g.setColour(burgundy.withAlpha(locked ? 1.0f : 0.65f));
    drawLock(g, box.getCentre(), locked);
    for (const auto control : {Control::dotted, Control::triplet}) {
        const auto button = controlBounds(control);
        const int timing = control == Control::dotted ? 1 : 2;
        const bool on = scratchProcessor.phraseTimingChoice() == timing;
        const bool down = pressed == control && hovered == control;
        g.setColour(burgundy.withAlpha(0.12f));
        g.fillRoundedRectangle(button.translated(0, 2), 9.0f);
        g.setGradientFill(juce::ColourGradient(cream.withAlpha(down ? 0.28f : on ? 0.68f : 0.44f),
                                              button.getTopLeft(), cream.withAlpha(down ? 0.38f : 0.25f),
                                              button.getBottomLeft(), false));
        g.fillRoundedRectangle(button, 9.0f);
        g.setColour(cream.withAlpha(hovered == control ? 0.85f : 0.42f));
        g.drawRoundedRectangle(button.reduced(0.5f), 8.5f, 1.0f);
        const auto lamp = rectangle(button.getX() + 11, button.getCentreY() - 4, 8, 8);
        g.setColour(burgundy.withAlpha(0.7f));
        g.fillEllipse(lamp.expanded(1.3f));
        if (on) {
            g.setColour(lime.withAlpha(0.23f));
            g.fillEllipse(lamp.expanded(4.0f));
        }
        g.setColour(on ? lime : burgundy.darker(0.15f));
        g.fillEllipse(lamp);
        drawText(g, control == Control::dotted ? "DOTTED" : "TRIPLET", button.withTrimmedLeft(27).withTrimmedRight(6),
                 17, burgundy.darker(0.35f));
    }
}

void BackScratchEditor::drawRandomize(juce::Graphics& g)
{
    const auto box = controlBounds(Control::randomize);
    const bool down = pressed == Control::randomize && hovered == Control::randomize;
    if (down || hovered == Control::randomize || flash > 0) {
        g.setColour((down ? midnight : juce::Colours::white).withAlpha(down ? 0.10f : 0.10f + flash * 0.10f));
        g.fillRoundedRectangle(box.reduced(6), 23.0f);
    }
    const float offset = down ? 2.0f : 0.0f;
    drawDice(g, {box.getCentreX(), box.getY() + 45 + offset}, 45.0f);
    drawText(g, "RANDOMIZE", rectangle(box.getX() + 12, box.getBottom() - 60 + offset, box.getWidth() - 24, 41),
             32, midnight, juce::Justification::centred, true);
}

void BackScratchEditor::drawSubChoice(juce::Graphics& g)
{
    const bool on = scratchProcessor.keepSub();
    drawText(g, "KEEP SUB", rectangle(228, 1040, 276, 36), 22, burgundy.darker(0.3f), juce::Justification::centredLeft);
    const auto track = rectangle(229, 1099, 125, 45);
    g.setColour(cream.withAlpha(0.6f));
    g.fillRoundedRectangle(track.expanded(2), 24.0f);
    g.setGradientFill(juce::ColourGradient(burgundy.darker(0.4f), track.getTopLeft(), burgundy, track.getBottomLeft(), false));
    g.fillRoundedRectangle(track, 22.5f);
    g.setColour(on ? lime.withAlpha(0.7f) : cream.withAlpha(0.12f));
    g.drawRoundedRectangle(track.reduced(1), 21.5f, 1.2f);
    const auto cap = rectangle(on ? track.getRight() - 40 : track.getX() + 4, track.getY() + 4, 37, 37);
    g.setGradientFill(juce::ColourGradient(cream.brighter(0.24f), cap.getTopLeft(), cream.darker(0.13f), cap.getBottomRight(), false));
    g.fillEllipse(cap);
    g.setColour(cream.brighter(0.3f)); g.drawEllipse(cap.reduced(2), 1.0f);
    drawText(g, on ? "ON" : "OFF", on ? track.withTrimmedRight(44) : track.withTrimmedLeft(44), 17, on ? lime : cream);
    drawText(g, on ? "Original lows" : "Full range", rectangle(373, 1098, 173, 46),
             21, burgundy.darker(0.3f), juce::Justification::centredLeft);
    if (hovered == Control::keepSub) {
        g.setColour(burgundy.withAlpha(0.15f));
        g.drawRoundedRectangle(controlBounds(Control::keepSub).reduced(3), 11.0f, 1.0f);
    }
}

void BackScratchEditor::drawFooter(juce::Graphics& g, const BackScratchProcessor::DisplayState& display)
{
    const bool playing = display.playing;
    const bool bypassed = scratchProcessor.parameters().getRawParameterValue("bypass")->load() > 0.5f;
    drawText(g, juce::String(display.tempoBpm, 0) + " BPM", rectangle(581, 1056, 145, 26),
             18, cream, juce::Justification::centredLeft);
    drawText(g, bypassed ? "BYPASS" : playing ? "SYNC" : "READY", rectangle(779, 1056, 112, 26),
             16, playing && !bypassed ? lime : cyan, juce::Justification::centredRight);
    for (int channel = 0; channel < 2; ++channel) {
        drawText(g, channel == 0 ? "L" : "R", rectangle(581, 1103 + channel * 24.0f, 23, 18),
                 14, cream.withAlpha(0.65f), juce::Justification::centredLeft);
        const float level = std::clamp(scratchProcessor.outputPeak(channel), 0.0f, 1.0f);
        for (int bar = 0; bar < 14; ++bar) {
            g.setColour(bar < level * 14.0f ? cyan : cream.withAlpha(0.10f));
            g.fillRect(rectangle(612 + bar * 19.5f, 1106 + channel * 24.0f, 14, 12));
        }
    }
}

void BackScratchEditor::timerCallback()
{
    if (editPreview) {
        const auto current = scratchProcessor.currentPattern();
        if (std::abs(current.beats - editPreview->beats) > 1.0e-8
            || current.gridOffset != editPreview->gridOffset)
            finishGridGesture(false);
    }
    flash = std::max(0.0f, flash - static_cast<float>(getTimerInterval()) * 0.0024f);
    // Very short triplet phrases need the faster display cadence. Keep using
    // the published audio frame; predicting a future playhead can run ahead
    // of playback or show an automation change before it reaches the output.
    const int frameInterval = scratchProcessor.isTransportPlaying() ? 1000 / 60 : 1000 / 30;
    if (getTimerInterval() != frameInterval) startTimer(frameInterval);
    repaint();
}

void BackScratchEditor::updateHover(juce::Point<float> p)
{
    graphHover = designPoint(p);
    graphHovered = patternHitTest(graphHover);
    hovered = controlAt(graphHover);
    const bool disabled = (hovered == Control::undo && !scratchProcessor.canUndoPatternEdit())
        || (hovered == Control::redo && !scratchProcessor.canRedoPatternEdit());
    setMouseCursor(editTool != Control::none && (graphHovered || editPreview)
        ? juce::MouseCursor::CrosshairCursor
        : hovered == Control::none || disabled ? juce::MouseCursor::NormalCursor
                                               : juce::MouseCursor::PointingHandCursor);
    repaint();
}

void BackScratchEditor::mouseMove(const juce::MouseEvent& e) { updateHover(e.position); }
void BackScratchEditor::mouseExit(const juce::MouseEvent&) { hovered = Control::none; graphHovered = false; repaint(); }
void BackScratchEditor::mouseDrag(const juce::MouseEvent& e)
{
    updateHover(e.position);
    if (editPreview) updateGridGesture(designPoint(e.position));
}

void BackScratchEditor::mouseDown(const juce::MouseEvent& e)
{
    if (!e.mods.isLeftButtonDown()) return;
    if (isShowing()) grabKeyboardFocus();
    updateHover(e.position);
    if (editTool != Control::none && graphHovered) {
        pressed = Control::none;
        beginGridGesture(designPoint(e.position));
        return;
    }
    pressed = controlAt(designPoint(e.position));
    repaint();
}

void BackScratchEditor::mouseUp(const juce::MouseEvent& e)
{
    if (editPreview) {
        if (e.mouseWasDraggedSinceMouseDown()) updateGridGesture(designPoint(e.position));
        finishGridGesture(true);
        updateHover(e.position);
        return;
    }
    const auto released = controlAt(designPoint(e.position));
    const auto action = pressed;
    pressed = Control::none;
    if (action != Control::none && action == released) activate(action);
    repaint();
}

double BackScratchEditor::graphDelayRange(const ScratchPattern& pattern) const noexcept
{
    if (editTool != Control::none || pattern.editingRange > 0.0)
        return patternEditingDelayRange(pattern);
    double range = 1.0e-6;
    for (int i = 0; i < pattern.count; ++i) {
        const auto& segment = pattern.segments[static_cast<size_t>(i)];
        range = std::max({range, segment.delayStart, segment.delayEnd});
    }
    return range;
}

void BackScratchEditor::beginGridGesture(juce::Point<float> point)
{
    if (!scratchProcessor.beginPatternEdit()) return;
    editPreview = scratchProcessor.currentPattern();
    gestureOriginal = editPreview;
    editDelayRange = patternEditingDelayRange(*editPreview);
    const auto graph = patternBounds();
    anchorPhase = strokePhase = std::clamp(static_cast<double>((point.x - graph.getX()) / graph.getWidth()), 0.0, 1.0);
    anchorDelay = strokeDelay = std::clamp(static_cast<double>((graph.getBottom() - point.y) / graph.getHeight()), 0.0, 1.0)
        * editDelayRange;
    const int cell = std::clamp(static_cast<int>(strokePhase * patternGridDivisions), 0, patternGridDivisions - 1);
    auto candidate = *editPreview;
    bool changed = false;
    if (editTool == Control::pencil) {
        if (drawMode == DrawMode::free) {
            changed = freeDrawStroke.begin(*gestureOriginal, strokePhase, strokeDelay)
                && freeDrawStroke.render(candidate, &strokeDelay);
        } else {
            // A click leaves a small mark at the actual pointer height. The
            // subsequent drag is always rebuilt from the unmodified phrase.
            constexpr double halfMark = 1.0 / 256.0;
            changed = drawPatternLine(candidate, std::max(0.0, strokePhase - halfMark), strokeDelay,
                std::min(1.0, strokePhase + halfMark), strokeDelay, drawMode == DrawMode::curve, &strokeDelay);
        }
    } else {
        eraseCell = cell;
        changed = erasePatternGridCells(candidate, cell, cell);
    }
    if (changed && scratchProcessor.previewPatternEdit(candidate)) {
        editPreview = candidate;
        statusLine.clear();
    } else {
        finishGridGesture(false);
    }
    repaint();
}

void BackScratchEditor::updateGridGesture(juce::Point<float> point)
{
    if (!editPreview || !gestureOriginal) return;
    const auto current = scratchProcessor.currentPattern();
    if (std::abs(current.beats - editPreview->beats) > 1.0e-8
        || current.gridOffset != editPreview->gridOffset) {
        finishGridGesture(false);
        return;
    }
    const auto graph = patternBounds();
    const double position = std::clamp(static_cast<double>((point.x - graph.getX()) / graph.getWidth()), 0.0, 1.0);
    const double depth = std::clamp(static_cast<double>((graph.getBottom() - point.y) / graph.getHeight()), 0.0, 1.0)
        * editDelayRange;
    auto candidate = *editPreview;
    if (editTool == Control::eraser) {
        const int cell = std::clamp(static_cast<int>(position * patternGridDivisions), 0, patternGridDivisions - 1);
        if (erasePatternGridCells(candidate, std::min(eraseCell, cell), std::max(eraseCell, cell))
            && scratchProcessor.previewPatternEdit(candidate)) {
            editPreview = candidate;
            eraseCell = cell;
        }
    } else {
        double applied = depth;
        bool changed = false;
        if (drawMode == DrawMode::free) {
            changed = freeDrawStroke.append(position, depth) && freeDrawStroke.render(candidate, &applied);
        } else {
            candidate = *gestureOriginal;
            if (std::abs(position - anchorPhase) > 1.0e-6)
                changed = drawPatternLine(candidate, anchorPhase, anchorDelay, position, depth,
                                          drawMode == DrawMode::curve, &applied);
            else {
                constexpr double halfMark = 1.0 / 256.0;
                changed = drawPatternLine(candidate, std::max(0.0, position - halfMark), depth,
                    std::min(1.0, position + halfMark), depth, drawMode == DrawMode::curve, &applied);
            }
        }
        if (changed && scratchProcessor.previewPatternEdit(candidate)) {
            editPreview = candidate;
            strokePhase = position;
            strokeDelay = applied;
        }
    }
    repaint();
}

void BackScratchEditor::finishGridGesture(bool commit)
{
    if (!editPreview) return;
    if (commit) scratchProcessor.commitPatternEdit();
    else scratchProcessor.cancelPatternEdit();
    editPreview.reset();
    gestureOriginal.reset();
    pressed = Control::none;
    repaint();
}

void BackScratchEditor::activate(Control control)
{
    if (control == Control::drawOptions) {
        chooseDrawMode();
    } else if (control == Control::shapes) {
        chooseScratchShape();
    } else if (control == Control::pencil || control == Control::eraser) {
        editTool = editTool == control ? Control::none : control;
        statusLine.clear();
    } else if (control == Control::undo || control == Control::redo) {
        if (control == Control::undo) scratchProcessor.undoPatternEdit();
        else scratchProcessor.redoPatternEdit();
        statusLine.clear();
    } else if (control == Control::randomize) {
        scratchProcessor.randomizePattern();
        flash = 1.0f;
        statusLine.clear();
    } else if (control == Control::keepSub) {
        scratchProcessor.setKeepSub(!scratchProcessor.keepSub());
        statusLine.clear();
    } else if (control == Control::phraseLock) {
        scratchProcessor.setPhraseLocked(!scratchProcessor.phraseLocked());
        statusLine.clear();
    } else if (control == Control::dotted || control == Control::triplet) {
        const int timing = control == Control::dotted ? 1 : 2;
        scratchProcessor.setPhraseTimingChoice(scratchProcessor.phraseTimingChoice() == timing ? 0 : timing);
        statusLine.clear();
    } else if (control == Control::shiftLeft || control == Control::shiftRight) {
        scratchProcessor.shiftPattern(control == Control::shiftLeft ? -1 : 1);
        statusLine.clear();
    } else if (control == Control::previous || control == Control::next) {
        stepPreset(control == Control::next ? 1 : -1);
    } else if (control == Control::presets) chooseFactoryPreset();
    else if (control == Control::save || control == Control::load) choosePresetFile(control == Control::save);
    repaint();
}

void BackScratchEditor::stepPreset(int direction)
{
    const int current = scratchProcessor.selectedFactoryPreset();
    const int next = current < 0 ? (direction > 0 ? 0 : factoryPresetCount - 1)
                                : (current + direction + factoryPresetCount) % factoryPresetCount;
    scratchProcessor.selectFactoryPreset(next);
    statusLine.clear();
}

void BackScratchEditor::closeEditMenu()
{
    ++menuGeneration;
    if (editMenu != nullptr) {
        // Menus complete asynchronously. Detach this editor's look-and-feel
        // before it can be destroyed, and dismiss only our own popup.
        editMenu->setLookAndFeel(nullptr);
        editMenu->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        editMenu = nullptr;
    }
}

void BackScratchEditor::showEditMenu(juce::PopupMenu& menu, Control control, std::function<void(int)> chosen)
{
    closeEditMenu();
    const auto generation = menuGeneration;
    const auto bounds = controlBounds(control).transformedBy(designToEditor());
    const auto target = localAreaToGlobal(bounds.toNearestInt());
    // JUCE's popup placement dereferences the display even for an embedded
    // menu. Plugin scanners may create an editor without a window server.
    if (juce::Desktop::getInstance().getDisplays().getDisplayForPoint(target.getCentre().toFloat()) == nullptr)
        return;
    menu.setLookAndFeel(knobLookAndFeel.get());
    const auto safe = juce::Component::SafePointer<BackScratchEditor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withParentComponent(this)
        .withTargetScreenArea(target).withMaximumNumColumns(1),
        [safe, generation, chosen = std::move(chosen)](int result) {
            if (safe == nullptr || generation != safe->menuGeneration) return;
            safe->editMenu = nullptr;
            if (result > 0) chosen(result);
        });
    auto* popup = juce::Component::getCurrentlyModalComponent();
    editMenu = popup != nullptr && isParentOf(popup) ? popup : nullptr;
    if (editMenu != nullptr)
        editMenu->setName(control == Control::drawOptions ? "Scratch drawing tools" : "Scratch starting shapes");
}

void BackScratchEditor::chooseDrawMode()
{
    juce::PopupMenu menu;
    const std::array<const char*, 3> titles {"Free Draw", "Line", "Curve"};
    const std::array<const char*, 3> details {"Follow your hand", "One straight ramp", "One smooth sweep"};
    for (int mode = 0; mode < 3; ++mode)
        menu.addCustomItem(mode + 1, std::make_unique<EditMenuRow>(titles[static_cast<size_t>(mode)],
            details[static_cast<size_t>(mode)], mode, std::nullopt, static_cast<int>(drawMode) == mode, regularTypeface),
            nullptr, titles[static_cast<size_t>(mode)]);
    const auto safe = juce::Component::SafePointer<BackScratchEditor>(this);
    showEditMenu(menu, Control::drawOptions, [safe](int result) {
        if (safe == nullptr || result < 1 || result > 3) return;
        safe->drawMode = static_cast<DrawMode>(result - 1);
        safe->editTool = Control::pencil;
        safe->statusLine.clear();
        safe->repaint();
    });
}

void BackScratchEditor::chooseScratchShape()
{
    juce::PopupMenu menu;
    menu.addSectionHeader("SCRATCH STARTERS");
    const auto pattern = scratchProcessor.currentPattern();
    for (int shape = 0; shape < scratchShapeCount; ++shape)
        menu.addCustomItem(shape + 1, std::make_unique<EditMenuRow>(scratchShapeName(shape), "", -1,
            makeScratchShape(pattern, shape), false, regularTypeface), nullptr, scratchShapeName(shape));
    const auto safe = juce::Component::SafePointer<BackScratchEditor>(this);
    showEditMenu(menu, Control::shapes, [safe](int result) {
        if (safe == nullptr || result < 1 || result > scratchShapeCount) return;
        const auto candidate = makeScratchShape(safe->scratchProcessor.currentPattern(), result - 1);
        if (!validateEditedPattern(candidate) || !safe->scratchProcessor.beginPatternEdit()) return;
        if (safe->scratchProcessor.previewPatternEdit(candidate)) {
            safe->scratchProcessor.commitPatternEdit();
            safe->editTool = Control::pencil;
            safe->drawMode = DrawMode::free;
            safe->statusIsError = false;
            safe->statusLine = juce::String(scratchShapeName(result - 1)).toUpperCase();
        } else safe->scratchProcessor.cancelPatternEdit();
        safe->repaint();
    });
}

void BackScratchEditor::chooseFactoryPreset()
{
    juce::PopupMenu menu;
    const auto& presets = factoryPresets();
    for (const auto* category : {"Bass", "Drums", "Vocals"}) {
        juce::PopupMenu group;
        bool containsSelection = false;
        for (int index = 0; index < factoryPresetCount; ++index) {
            const auto& preset = presets[static_cast<size_t>(index)];
            if (!juce::String(preset.category).equalsIgnoreCase(category)) continue;
            const bool selected = index == scratchProcessor.selectedFactoryPreset()
                && !scratchProcessor.isGenerated() && scratchProcessor.variationSeed() == 0;
            group.addItem(index + 1, preset.name, true, selected);
            containsSelection = containsSelection || selected;
        }
        menu.addSubMenu(category, std::move(group), true, juce::Image(), containsSelection);
    }
    auto safe = juce::Component::SafePointer<BackScratchEditor>(this);
    auto bounds = controlBounds(Control::presets);
    bounds = bounds.transformedBy(designToEditor());
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                       .withTargetScreenArea(localAreaToGlobal(bounds.toNearestInt())).withMaximumNumColumns(1),
        [safe](int result) {
            if (safe == nullptr || result <= 0) return;
            safe->scratchProcessor.selectFactoryPreset(result - 1);
            safe->statusLine.clear();
            safe->repaint();
        });
}

void BackScratchEditor::choosePresetFile(bool save)
{
    if (fileChooser != nullptr) return;
    const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    const auto suggested = save ? folder.getChildFile(juce::File::createLegalFileName(scratchProcessor.currentPresetName())
                                                      + ".backscratch") : folder;
    fileChooser = std::make_unique<juce::FileChooser>(save ? "Save BACK-SCRATCH preset" : "Load BACK-SCRATCH preset",
                                                     suggested, "*.backscratch", true);
    auto safe = juce::Component::SafePointer<BackScratchEditor>(this);
    const int flags = juce::FileBrowserComponent::canSelectFiles | (save
        ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
        : juce::FileBrowserComponent::openMode);
    fileChooser->launchAsync(flags, [safe, save](const juce::FileChooser& chooser) {
        if (safe == nullptr) return;
        auto file = chooser.getResult();
        if (file != juce::File()) {
            if (save) file = file.withFileExtension("backscratch");
            const auto result = save ? safe->scratchProcessor.savePreset(file) : safe->scratchProcessor.loadPreset(file);
            safe->statusIsError = result.failed();
            safe->statusLine = result.failed() ? result.getErrorMessage()
                                              : (save ? "SAVED: " : "LOADED: ") + file.getFileNameWithoutExtension();
            safe->repaint();
        }
        // Do not destroy the chooser from inside its own callback.
        juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->fileChooser.reset(); });
    });
}

bool BackScratchEditor::keyPressed(const juce::KeyPress& key)
{
    if (fileChooser != nullptr) return false;
    if (editTool != Control::none && !editPreview && key.getModifiers().isCommandDown()
        && !key.getModifiers().isAltDown() && (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z')) {
        const bool redo = key.getModifiers().isShiftDown();
        if (redo ? scratchProcessor.canRedoPatternEdit() : scratchProcessor.canUndoPatternEdit()) {
            activate(redo ? Control::redo : Control::undo);
            return true;
        }
        return false;
    }
    if (key.getModifiers().isAnyModifierKeyDown()) return false;
    if (key.getKeyCode() == juce::KeyPress::escapeKey) {
        closeEditMenu();
        finishGridGesture(false);
        pressed = Control::none;
        editTool = Control::none;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
        return true;
    }
    return false;
}

juce::String BackScratchEditor::getTooltip()
{
    switch (hovered) {
        case Control::pencil: return "Draw with the selected tool. The small arrow chooses Free Draw, Line or Curve. Click again to stop editing.";
        case Control::drawOptions: return "Choose Free Draw to follow your hand, Line for straight ramps, or Curve for smooth sweeps.";
        case Control::shapes: return "Start from a scratch shape, then draw over any part. Keeps your length and shift; one click to undo.";
        case Control::eraser: return "Erase: drag over cells to restore the original audio. Click again to stop editing.";
        case Control::undo: return scratchProcessor.canUndoPatternEdit() ? "Undo the last drawing or erasing gesture." : "Nothing to undo yet.";
        case Control::redo: return scratchProcessor.canRedoPatternEdit() ? "Redo the last undone gesture." : "Nothing to redo yet.";
        case Control::dotted: return "Dotted length: 1.5 times the knob value. Click again for straight timing.";
        case Control::triplet: return "Triplet length: two thirds of the knob value. Click again for straight timing.";
        case Control::shiftLeft: return "Shift the whole pattern left by one grid cell. It wraps across the edges.";
        case Control::shiftRight: return "Shift the whole pattern right by one grid cell. It wraps across the edges.";
        case Control::phraseLock: return "Keep this length and timing when creating a new pattern.";
        case Control::randomize: return "Create a new scratch pattern.";
        case Control::keepSub: return "Preserve the original sub when on. Scratch the full signal when off.";
        case Control::previous: return "Previous factory preset.";
        case Control::next: return "Next factory preset.";
        case Control::presets: return "Choose a factory preset.";
        case Control::save: return "Save this pattern and its settings.";
        case Control::load: return "Load a saved pattern.";
        case Control::none: {
            if (graphHovered && editTool == Control::pencil)
                return drawMode == DrawMode::free ? "Draw freely along the timeline. Small hand wobbles are smoothed. Escape cancels."
                    : drawMode == DrawMode::line ? "Drag between two points to draw a straight ramp. Escape cancels."
                    : "Drag between two points to draw a smooth sweep. Escape cancels.";
            if (graphHovered && editTool == Control::eraser)
                return "Drag across cells to restore the original audio. Escape cancels.";
            const auto display = scratchProcessor.displayState();
            if (display.playing && display.scratchActive && !display.historyReady)
                return "LIVE INPUT: this gesture needs earlier audio. The original input plays until that audio is available.";
            break;
        }
        case Control::phraseDuration: break;
    }
    return {};
}

} // namespace backScratch
