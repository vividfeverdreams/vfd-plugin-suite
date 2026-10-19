#include "ShiftyEditor.h"
#include "BinaryData.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace shifty {
namespace {
// The editor shows exactly the device: this crop sits on the shell's own straight edges in
// Assets/DeviceShell-editor.png (corners tightened to match), so it meets the window on every side.
// That art is baked from DeviceShell-fullbleed.png by Tools/make_editor_shell.py, which adds a
// 70 px band of board under the footer plate for the INFO key.
constexpr int shellX = 57, shellY = 73, shellWidth = 1008, shellHeight = 1247;
constexpr int infoBandHeight = 70;
constexpr int editorHeight = shellHeight + infoBandHeight;
constexpr int baseWidth = 650;
constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
constexpr float endAngle = juce::MathConstants<float>::pi * 2.75f;
const juce::Colour ink { 0xff073d2b }, screenText { 0xffd7ffee }, lime { 0xffc3ff62 };
const juce::Colour muted { 0xff9bc2b2 }, mint { 0xffd7f4cd }, neon { 0xff69ff9a };
const juce::Colour screenPanel { 0xff0b3a29 }, screenLine { 0xff398c5b };
// The footer plate is part of the shell art: four bays between engraved dividers at x 341, 524
// and 701, the last ending in the slant beside the speaker grill. Each group sits in its own bay.
const juce::Rectangle<float> motionSwitchBounds {175, 1168, 150, 50};
const juce::Rectangle<float> randomBounds {356, 1139, 154, 58};
const juce::Rectangle<float> locksBounds {543, 1139, 140, 58};
const juce::Rectangle<float> undoBounds {719, 1139, 64, 38};
const juce::Rectangle<float> redoBounds {791, 1139, 64, 38};
const juce::Rectangle<float> bypassBounds {719, 1185, 136, 36};
// OUTPUT is a knob on the board left of INFO (as on ENERGY HARVESTER), in a recessed socket like INFO's,
// with its name and value beside it.
const juce::Point<float> outputKnobCentre {421.0f, 1295.0f};
constexpr float outputSocketRadius = 39.0f, outputArcRadius = 33.0f, outputCapRadius = 26.5f;
const juce::Rectangle<float> outputNameBounds {226, 1262, 144, 26};
const juce::Rectangle<float> outputValueBounds {226, 1288, 144, 34};
// Screen (dark LCD) layout, design coordinates. The screen interior spans x 141..981, y 171..507.
const juce::Rectangle<float> pageTabs[] { {592, 116, 82, 42}, {674, 116, 82, 42}, {756, 116, 82, 42}, {838, 116, 80, 42} };
const juce::Rectangle<float> engineSelectorBounds {540, 202, 400, 40};
const juce::Rectangle<float> engineLockBounds {505, 200, 30, 44}; // centred between the star and the selector
const juce::Rectangle<float> motionTabBounds {180, 202, 330, 40};
const juce::Rectangle<float> folderChipBounds {640, 204, 290, 38};
const juce::Rectangle<float> randomPresetBounds {318, 203, 42, 42};
const juce::Rectangle<float> lowLatencyBounds {300, 254, 180, 40};
const juce::Rectangle<float> vowelSwitchBounds {300, 316, 180, 40};
const juce::Rectangle<float> voiceSetBounds {300, 330, 300, 42};
const juce::Rectangle<float> mouthVoiceSetBounds {300, 396, 300, 40};
const juce::Rectangle<float> latestButtonBounds {540, 398, 380, 56};
const juce::Rectangle<float> originalVowelBounds {300, 406, 170, 40};
const juce::Rectangle<float> saveFolderPickBounds {728, 357, 112, 40};
const juce::Rectangle<float> lfoCells[] { {170, 416, 160, 55}, {330, 416, 190, 55}, {520, 416, 180, 55},
                                          {700, 416, 130, 55}, {830, 416, 121, 55} };
const juce::Rectangle<float> yoiCells[] { {170, 416, 180, 55}, {350, 416, 140, 55}, {490, 416, 140, 55},
                                          {630, 416, 170, 55}, {800, 416, 151, 55} };
const juce::Rectangle<float> envCells[] { {170, 416, 270, 55}, {440, 416, 260, 55}, {700, 416, 251, 55} };

/** The design-space control each screen dropdown hangs from. */
juce::Rectangle<float> menuAnchor(const juce::String& menu) {
    if (menu == "shape") return lfoCells[0];
    if (menu == "rate") return lfoCells[1];
    if (menu == "target") return lfoCells[4];
    if (menu == "yoiFrom") return yoiCells[1];
    if (menu == "yoiTo") return yoiCells[2];
    if (menu == "folder") return folderChipBounds;
    if (menu == "saveFolder") return saveFolderPickBounds;
    jassertfalse;
    return {};
}

/** The editor draws every control; this slider only handles the mouse. Knobs drag up, value
    strips drag up or right, and both are relative: a click never moves the value, and Shift
    drags 5x finer (it can be pressed or released mid-drag). The vowel bar follows the mouse
    along its drawn track instead. */
class InvisibleSlider final : public juce::Slider {
public:
    enum class Drag { knob, strip, track };
    static constexpr double fineFactor = 5.0;

    void setDrag(Drag mode, int pixelsForFullRange, juce::Range<double> trackFraction = {0.0, 1.0}) {
        drag = mode;
        track = trackFraction;
        // Rotary drag styles never jump to the click inside Slider::mouseDown; the drag itself is ours.
        setSliderStyle(mode == Drag::knob ? juce::Slider::RotaryVerticalDrag
                                          : juce::Slider::RotaryHorizontalVerticalDrag);
        setMouseDragSensitivity(pixelsForFullRange);
    }
    void paint(juce::Graphics&) override {}
    void mouseDown(const juce::MouseEvent& e) override {
        contextClick = e.mods.isPopupMenu() && static_cast<bool>(onContextMenu);
        if (contextClick) { onContextMenu(); return; }
        // Alt-click resets to the default (Slider's single-click modifier); no drag follows it.
        dragging = isEnabled() && !e.mods.isAltDown();
        lastDrag = e.position;
        dragProportion = valueToProportionOfLength(getValue());
        juce::Slider::mouseDown(e); // begins the host gesture
        if (dragging && drag == Drag::track) followTrack(e.position.x);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (contextClick || !dragging) return;
        if (drag == Drag::track) { followTrack(e.position.x); return; }
        const auto moved = e.position - lastDrag;
        lastDrag = e.position;
        const double pixels = getMouseDragSensitivity() * (e.mods.isShiftDown() ? fineFactor : 1.0);
        const double distance = drag == Drag::knob ? -moved.y : moved.x - moved.y;
        dragProportion = juce::jlimit(0.0, 1.0, dragProportion + distance / pixels);
        setValue(proportionOfLengthToValue(dragProportion), juce::sendNotificationSync);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!contextClick) juce::Slider::mouseUp(e);
        contextClick = false;
        dragging = false;
    }
    std::function<void()> onContextMenu;
private:
    void followTrack(float x) {
        const double fraction = getWidth() > 0 ? x / static_cast<double>(getWidth()) : 0.0;
        const double proportion = (fraction - track.getStart()) / juce::jmax(0.01, track.getLength());
        setValue(proportionOfLengthToValue(juce::jlimit(0.0, 1.0, proportion)), juce::sendNotificationSync);
    }
    Drag drag = Drag::knob;
    juce::Range<double> track {0.0, 1.0};
    juce::Point<float> lastDrag;
    double dragProportion = 0.0;
    bool contextClick = false;
    bool dragging = false;
};

class InvisibleToggle final : public juce::ToggleButton {
public:
    void paintButton(juce::Graphics&, bool, bool) override {}
    void mouseDown(const juce::MouseEvent& e) override {
        contextClick = e.mods.isPopupMenu() && static_cast<bool>(onContextMenu);
        if (contextClick) onContextMenu();
        else juce::ToggleButton::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!contextClick) juce::ToggleButton::mouseDrag(e);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!contextClick) juce::ToggleButton::mouseUp(e);
        contextClick = false;
    }
    std::function<void()> onContextMenu;
private:
    bool contextClick = false;
};

class InvisibleAction final : public juce::TextButton {
public:
    void paintButton(juce::Graphics&, bool, bool) override {}
};

class ValueLabel final : public juce::Label {
public:
    ValueLabel(juce::Slider& s, bool shouldForwardDrag)
        : slider(s), dragThrough(shouldForwardDrag) {}
    /** Values printed straight on the board get a dark drop shadow so they read over the circuitry. */
    bool shadowed = false;
    /** A drag on the value moves its control (as it did before OUTPUT had a knob). */
    void setDragThrough(bool forward) noexcept { dragThrough = forward; }
    void paint(juce::Graphics& g) override {
        if (!shadowed || isBeingEdited()) { juce::Label::paint(g); return; }
        const auto area = getBorderSize().subtractedFrom(getLocalBounds()).toFloat();
        const float offset = juce::jmax(1.0f, getFont().getHeight() * 0.06f);
        g.setFont(getFont());
        g.setColour(juce::Colour(0xff031a11).withAlpha(0.75f));
        g.drawText(getText(), area.translated(0.0f, offset), getJustificationType(), true);
        g.setColour(findColour(textColourId));
        g.drawText(getText(), area, getJustificationType(), true);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) {
            slider.mouseDown(e.getEventRelativeTo(&slider));
        } else if (dragThrough && e.getNumberOfClicks() == 1) {
            forwarding = true;
            slider.mouseDown(e.getEventRelativeTo(&slider));
        } else juce::Label::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (forwarding) slider.mouseDrag(e.getEventRelativeTo(&slider));
        else juce::Label::mouseDrag(e);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (forwarding) slider.mouseUp(e.getEventRelativeTo(&slider));
        else juce::Label::mouseUp(e);
        forwarding = false;
    }
private:
    juce::Slider& slider;
    bool dragThrough = false;
    bool forwarding = false;
};

juce::Rectangle<float> knobLockBounds(const juce::String& id) {
    if (id == "shift") return {451, 547, 40, 44};
    if (id == "focus") return {711, 538, 34, 40};
    if (id == "bite") return {898, 538, 34, 40};
    if (id == "drive") return {711, 761, 34, 40};
    if (id == "mix") return {898, 761, 34, 40};
    return {};
}

void drawPadlock(juce::Graphics& g, juce::Rectangle<float> bounds,
                 bool locked, juce::Colour colour, juce::Colour keyhole = mint) {
    const auto box = bounds.withSizeKeepingCentre(20.0f, 17.0f).translated(0, 5.0f);
    g.setColour(colour);
    if (locked) g.fillRoundedRectangle(box, 3.0f);
    else g.drawRoundedRectangle(box, 3.0f, 2.3f);
    juce::Path shackle;
    const auto x = box.getX() + 4.0f;
    const auto y = box.getY();
    shackle.startNewSubPath(x, y + 1.0f);
    shackle.lineTo(x, y - 5.0f);
    shackle.cubicTo(x, y - 13.0f, x + 12.0f, y - 13.0f, x + 12.0f, y - 5.0f);
    if (locked) shackle.lineTo(x + 12.0f, y + 1.0f);
    g.strokePath(shackle, juce::PathStrokeType(2.5f,
                   juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (locked) {
        g.setColour(keyhole);
        g.fillEllipse(box.getCentreX() - 1.8f, box.getY() + 5.0f, 3.6f, 3.6f);
        g.fillRect(box.getCentreX() - 0.8f, box.getY() + 7.0f, 1.6f, 4.0f);
    }
}

juce::String vowelLabel(float position) {
    const float p = juce::jlimit(0.0f, 4.0f, position);
    const int nearest = juce::roundToInt(p);
    if (std::abs(p - static_cast<float>(nearest)) < 0.15f) return vowelName(nearest);
    const int lower = static_cast<int>(std::floor(p));
    return juce::String(vowelName(lower)) + "-" + vowelName(juce::jmin(4, lower + 1));
}

bool isPercentage(const juce::String& id) {
    return id == "focus" || id == "bite" || id == "drive" || id == "mix" || id == "character"
        || id == "talk" || id == "yoiAmount" || id == "envAmount";
}

/** The folder a typed name saves into: one level, no separators, no leading dots (as the processor does). */
juce::String cleanFolderName(const juce::String& typed) {
    auto folder = juce::File::createLegalFileName(typed.trim());
    while (folder.startsWithChar('.')) folder = folder.substring(1).trimStart();
    return folder;
}

juce::String formatValue(const juce::String& id, float value) {
    if (std::abs(value) < 0.0001f) value = 0.0f;
    if (id == "shift") return juce::String(value, 1) + " st";
    if (id == "subCut") return juce::String(juce::roundToInt(value)) + " Hz";
    if (id == "output") return juce::String(value, 1) + " dB";
    if (id == "motionDepth") return juce::String(value, 1) + " st";
    if (id == "motionFreeHz") return juce::String(value, 2) + " Hz";
    if (id == "vowelMorph") return vowelLabel(value);
    if (id == "envAmount") return (value > 0.0f ? "+" : "") + juce::String(juce::roundToInt(value * 100.0f)) + "%";
    if (id == "envAttack") return (value < 10.0f ? juce::String(value, 1) : juce::String(juce::roundToInt(value))) + " ms";
    if (id == "envRelease" || id == "yoiTime") return juce::String(juce::roundToInt(value)) + " ms";
    if (id == "yoiShift") return (value > 0.0f ? "+" : "") + juce::String(value, 1) + " st";
    return juce::String(juce::roundToInt(value * 100.0f)) + "%";
}

float waveform(double cycles, int shape) noexcept {
    const auto phase = cycles - std::floor(cycles);
    switch (shape) {
        case 1: return (float) (1.0 - 4.0 * std::abs(phase - 0.5));
        case 2: return (float) (2.0 * phase - 1.0);
        case 3: return (float) (1.0 - 2.0 * phase);
        default: return (float) std::sin(juce::MathConstants<double>::twoPi * phase);
    }
}

void drawFavoriteStar(juce::Graphics& g, juce::Point<float> centre,
                      float radius, bool filled, juce::Colour colour) {
    juce::Path star;
    for (int point = 0; point < 10; ++point) {
        const float angle = -juce::MathConstants<float>::halfPi
                          + point * juce::MathConstants<float>::pi / 5.0f;
        const float r = point % 2 == 0 ? radius : radius * 0.46f;
        const float x = centre.x + std::cos(angle) * r;
        const float y = centre.y + std::sin(angle) * r;
        if (point == 0) star.startNewSubPath(x, y);
        else star.lineTo(x, y);
    }
    star.closeSubPath();
    g.setColour(colour);
    if (filled) g.fillPath(star);
    else g.strokePath(star, juce::PathStrokeType(2.4f,
                                                juce::PathStrokeType::mitered,
                                                juce::PathStrokeType::rounded));
}

const char* engineName(EngineMode mode) {
    switch (mode) {
        case EngineMode::talk: return "TALK";
        case EngineMode::mouth: return "MOUTH";
        case EngineMode::classic: return "CLASSIC";
        case EngineMode::original: break;
    }
    return "CLASSIC 0.4";
}

juce::String latencyText(EngineMode mode, bool lowLatency, double sampleRate) {
    const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const int samples = ShiftyProcessor::latencyFor(mode, lowLatency, rate);
    return "LATENCY " + juce::String(juce::roundToInt(1000.0 * samples / rate)) + " MS";
}
} // namespace

struct ShiftyEditor::Control {
    juce::String id;
    juce::Rectangle<float> hit, value;
    bool darkText = true;
    std::unique_ptr<InvisibleSlider> slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::unique_ptr<ValueLabel> label;
    std::unique_ptr<InvisibleAction> lockButton;
};

struct ShiftyEditor::Switch {
    juce::String id;
    juce::Rectangle<float> hit;
    std::unique_ptr<InvisibleToggle> button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

struct ShiftyEditor::PresetItem {
    juce::String name;
    int factoryIndex = -1;
    juce::File file;
    juce::String folder; // factory folder, or "User" / "User/<folder>"
};

juce::Point<int> ShiftyEditor::sizeFor(double scale) {
    const int width = juce::roundToInt(baseWidth * scale);
    return { width, juce::roundToInt(baseWidth * scale * editorHeight / shellWidth) };
}

std::vector<double> ShiftyEditor::sizesFitting(juce::Rectangle<int> userArea) {
    std::vector<double> result;
    for (const double scale : sizeChoices) {
        const auto size = sizeFor(scale);
        if (userArea.isEmpty() || (size.x <= userArea.getWidth() && size.y + 60 <= userArea.getHeight()))
            result.push_back(scale);
    }
    return result;
}

ShiftyEditor::ShiftyEditor(ShiftyProcessor& p)
    : juce::AudioProcessorEditor(p), plugin(p),
      shell(juce::ImageCache::getFromMemory(BinaryData::DeviceShelleditor_png,
                                            BinaryData::DeviceShelleditor_pngSize)),
      glowMask(juce::ImageCache::getFromMemory(BinaryData::ShellGlow_png, BinaryData::ShellGlow_pngSize)),
      normalTypeface(juce::Typeface::createSystemTypefaceFor(
          BinaryData::ChakraPetchSemiBold_ttf, BinaryData::ChakraPetchSemiBold_ttfSize)),
      displayTypeface(juce::Typeface::createSystemTypefaceFor(
          BinaryData::ChakraPetchBoldItalic_ttf, BinaryData::ChakraPetchBoldItalic_ttfSize)) {
    setOpaque(true);
    setWantsKeyboardFocus(true);
    live = plugin.display();

    addControl("shift", {196, 606, 310, 285}, {245, 896, 220, 70}, true, true);
    addControl("focus", {576, 575, 144, 142}, {585, 706, 125, 45}, true, true);
    addControl("bite", {778, 575, 144, 142}, {790, 706, 125, 45}, true, true);
    addControl("drive", {576, 798, 144, 142}, {585, 927, 125, 45}, true, true);
    addControl("mix", {778, 798, 144, 142}, {790, 927, 125, 45}, true, true);
    addControl("subCut", {296, 1042, 120, 60}, {296, 1043, 125, 62}, false, false);
    addControl("output", juce::Rectangle<float>(2.0f * outputSocketRadius, 2.0f * outputSocketRadius).withCentre(outputKnobCentre),
               outputValueBounds, true, false, false);
    control("output")->label->setJustificationType(juce::Justification::centredRight);
    control("output")->label->shadowed = true;
    control("output")->label->setDragThrough(true);
    // MOTION page, LFO strip
    addControl("motionDepth", {525, 418, 170, 52}, {592, 421, 103, 48}, false, false);
    addControl("motionFreeHz", {335, 418, 180, 52}, {398, 421, 117, 48}, false, false);
    // MOTION page, ENV and YOI strips
    addControl("envAmount", {175, 418, 260, 52}, {300, 421, 130, 48}, false, false, false);
    addControl("envAttack", {445, 418, 250, 52}, {560, 421, 130, 48}, false, false, false);
    addControl("envRelease", {705, 418, 240, 52}, {820, 421, 125, 48}, false, false, false);
    addControl("yoiAmount", {172, 418, 176, 52}, {258, 421, 88, 48}, false, false, false);
    addControl("yoiTime", {632, 418, 166, 52}, {694, 421, 102, 48}, false, false, false);
    addControl("yoiShift", {802, 418, 147, 52}, {866, 421, 83, 48}, false, false, false);
    // VOICE page
    addControl("vowelMorph", {300, 248, 300, 50}, {180, 280, 110, 26}, false, false, false);
    addControl("character", {682, 280, 76, 76}, {670, 384, 100, 26}, true, false, false);
    addControl("talk", {832, 280, 76, 76}, {820, 384, 100, 26}, true, false, false);
    control("vowelMorph")->label->setJustificationType(juce::Justification::centredLeft);

    addSwitch("subSafe", {181, 1045, 103, 55});
    addSwitch("motionEnable", motionSwitchBounds);
    addSwitch("bypass", bypassBounds);
    addSwitch("motionSync", lfoCells[3]);
    addSwitch("vowel", vowelSwitchBounds);

    auto addAction = [this](std::unique_ptr<juce::Button>& button, const char* name,
                           const char* tooltip, std::function<void()> action) {
        button = std::make_unique<InvisibleAction>();
        button->setName(name);
        button->setButtonText(name);
        button->setTooltip(tooltip);
        button->setMouseCursor(juce::MouseCursor::PointingHandCursor);
        button->onClick = std::move(action);
        addAndMakeVisible(*button);
    };
    addAction(randomButton, "Randomize", "Create a new variation using every unlocked control, the engine included. Undo restores the previous sound.",
              [this] { plugin.randomizeParameters(); updateValueLabels(); updatePage(); repaint(); });
    addAction(locksButton, "Randomization locks", "Choose which parameters RANDOM keeps unchanged. Locks do not affect manual changes or preset loading.",
              [this] { showLocksMenu(); });
    addAction(undoButton, "Undo", "Undo the last change, preset, engine change or random variation", [this] {
        plugin.undo(); updateValueLabels(); updatePage(); repaint();
    });
    addAction(redoButton, "Redo", "Redo the last undone change", [this] {
        plugin.redo(); updateValueLabels(); updatePage(); repaint();
    });
    addAction(latestButton, "Use latest sound",
              "Switch this sound from the original 0.4 engine to the current CLASSIC engine. Undo returns to the original.",
              [this] { plugin.useLatestSound(); updatePage(); repaint(); });
    addAction(randomPresetButton, "Random preset",
              "Load a random preset from the list as it is filtered now (tab, folder and search). Undo returns to the previous sound.",
              [this] { loadRandomPreset(); });

    for (auto& c : controls) {
        c.slider->onValueChange = [this] { updateValueLabels(); repaint(); };
        c.label->onTextChange = [this, id = c.id, slider = c.slider.get(), label = c.label.get()] {
            float value = static_cast<float>(label->getText().getDoubleValue());
            if (isPercentage(id))
                value *= 0.01f;
            // A typed value is one complete edit, so it becomes one Undo step.
            auto* parameter = plugin.parameters().getParameter(id);
            if (parameter != nullptr) parameter->beginChangeGesture();
            slider->setValue(value, juce::sendNotificationSync);
            if (parameter != nullptr) parameter->endChangeGesture();
        };
    }
    for (auto& s : switches)
        s.button->onClick = [this] { updatePage(); repaint(); };

    addAndMakeVisible(presetSearch);
    presetSearch.setMultiLine(false);
    presetSearch.setFont(juce::Font(juce::FontOptions(normalTypeface).withHeight(22.0f)));
    presetSearch.setTextToShowWhenEmpty("Search presets", muted);
    presetSearch.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff073828));
    presetSearch.setColour(juce::TextEditor::textColourId, screenText);
    presetSearch.setColour(juce::TextEditor::outlineColourId, screenLine);
    presetSearch.setColour(juce::TextEditor::focusedOutlineColourId, lime);
    presetSearch.onTextChange = [this] { refreshPresetList(); repaint(); };
    presetSearch.onReturnKey = [this] { loadSelectedPreset(); };

    for (auto* editor : { &presetNameEditor, &presetFolderEditor }) {
        addAndMakeVisible(*editor);
        editor->setMultiLine(false);
        editor->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff073828));
        editor->setColour(juce::TextEditor::textColourId, screenText);
        editor->setColour(juce::TextEditor::outlineColourId, lime);
        editor->setColour(juce::TextEditor::focusedOutlineColourId, lime);
        editor->onReturnKey = [this] { finishPresetSave(); };
        editor->onEscapeKey = [this] { savingPreset = false; updatePage(); repaint(); };
        editor->onTextChange = [this] { repaint(); };
    }
    presetNameEditor.setTextToShowWhenEmpty("Preset name", muted);
    presetFolderEditor.setTextToShowWhenEmpty("Folder (optional)", muted);

    juce::PropertiesFile::Options favoriteOptions;
    favoriteOptions.applicationName = "SHIFTY";
    favoriteOptions.filenameSuffix = "settings";
    favoriteOptions.folderName = "YOI-BOI/SHIFTY";
    favoriteOptions.osxLibrarySubFolder = "Application Support";
    favoriteOptions.storageFormat = juce::PropertiesFile::storeAsXML;
    favoritesFile = std::make_unique<juce::PropertiesFile>(favoriteOptions);
    favorites.addLines(favoritesFile->getValue("favorites"));
    favorites.removeEmptyStrings();

    infoButton = std::make_unique<vfd::InfoButton>(vfd::themeFor("SHIFTY"), normalTypeface);
    infoOverlay = std::make_unique<vfd::InfoOverlay>("SHIFTY", vfd::themeFor("SHIFTY"),
        BinaryData::VfdLogo_svg, static_cast<size_t>(BinaryData::VfdLogo_svgSize), normalTypeface);
    infoButton->onClick = [this] { infoOverlay->open(infoButton.get()); };
    addAndMakeVisible(*infoButton);
    addChildComponent(*infoOverlay);

    refreshPresetList();
    updateValueLabels();
    updatePage();
    setResizable(true, false);
    const auto smallest = sizeFor(0.75), largest = sizeFor(sizeChoices.back());
    setResizeLimits(smallest.x, smallest.y, largest.x, largest.y);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(shellWidth) / editorHeight);
    // Open at the size saved with the project, or the largest standard size that fits the display.
    juce::Rectangle<int> userArea;
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        userArea = display->userBounds.toNearestInt();
    double scale = juce::jlimit(0.75, sizeChoices.back(), plugin.editorScale());
    const auto wanted = sizeFor(scale);
    if (!userArea.isEmpty() && (wanted.x > userArea.getWidth() || wanted.y + 60 > userArea.getHeight())) {
        const auto fitting = sizesFitting(userArea);
        scale = fitting.empty() ? sizeChoices.front() : fitting.back();
    }
    const auto size = sizeFor(scale);
    setSize(size.x, size.y);
    sizeReady = true;
    startTimerHz(30);
}

ShiftyEditor::~ShiftyEditor() { stopTimer(); }

void ShiftyEditor::addControl(const char* id, juce::Rectangle<float> hit,
                              juce::Rectangle<float> value, bool rotary, bool darkText, bool lockable) {
    controls.emplace_back();
    auto& c = controls.back();
    c.id = id;
    c.hit = hit;
    c.value = value;
    c.darkText = darkText;
    c.slider = std::make_unique<InvisibleSlider>();
    if (c.id == "vowelMorph") {
        // The drawn track's OO..EE marks span x 324..576 of the 300..600 hit area (see drawVowelBar).
        c.slider->setDrag(InvisibleSlider::Drag::track, 250, {24.0 / 300.0, 276.0 / 300.0});
    } else if (rotary) {
        c.slider->setDrag(InvisibleSlider::Drag::knob, 250);
    } else {
        // Bipolar strips get more travel so 1% is still about two pixels.
        const bool bipolar = c.id == "envAmount" || c.id == "yoiShift";
        c.slider->setDrag(InvisibleSlider::Drag::strip, bipolar ? 400 : 250);
    }
    c.slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    c.slider->setScrollWheelEnabled(false);
    c.slider->setPopupMenuEnabled(false);
    juce::String description = c.id;
    if (c.id == "shift") description = "Shift: move the formants lower (bigger, darker) or higher (smaller, brighter) while keeping the notes";
    else if (c.id == "focus") description = "Focus: TALK formant width, MOUTH grain length, CLASSIC envelope detail";
    else if (c.id == "bite") description = "Bite: TALK resonance and drive into the vowels, MOUTH pitch-synchronous buzz, CLASSIC contrast";
    else if (c.id == "drive") description = "Drive: anti-aliased saturation before the formant engine";
    else if (c.id == "mix") description = "Mix: blend in the formant effect; 100% gives the clearest change";
    else if (c.id == "output") description = "Output: SHIFTY's final level, from -24 to +6 dB";
    else if (c.id == "vowelMorph") description = "Vowel: morph OO, OH, AH, EH, EE (TALK) or the mouth shape (MOUTH)";
    else if (c.id == "character") description = "Character macro: more resonance, bite and drive (MOUTH: adds the vowel's mouth shape)";
    else if (c.id == "talk") description = "Talk macro: more envelope, YOI and Motion movement";
    else if (c.id == "envAmount") description = "Envelope amount: louder notes open the vowel (TALK) or raise SHIFT (MOUTH, CLASSIC); negative reverses";
    else if (c.id == "envAttack") description = "Envelope attack";
    else if (c.id == "envRelease") description = "Envelope release";
    else if (c.id == "yoiAmount") description = "YOI amount: each new note sweeps the vowel from FROM to TO";
    else if (c.id == "yoiTime") description = "YOI sweep time";
    else if (c.id == "yoiShift") description = "YOI shift: formant offset at the start of each sweep, gliding back to zero";
    const juce::String how = c.id == "vowelMorph" ? "Click or drag along the bar" :
                             rotary ? "Drag up or down" : "Drag up or right to raise";
    c.slider->setTooltip(description + ". " + how + ", hold Shift for fine steps, double click to reset, right click for randomization lock.");
    c.slider->onContextMenu = [this, parameterId = c.id, slider = c.slider.get()] {
        showParameterLockMenu(parameterId, slider);
    };
    if (rotary) c.slider->setRotaryParameters(startAngle, endAngle, true);
    if (auto* parameter = plugin.parameters().getParameter(id)) {
        c.slider->setName(parameter->getName(80));
        c.slider->setDoubleClickReturnValue(true,
            parameter->convertFrom0to1(parameter->getDefaultValue()));
    }
    addAndMakeVisible(*c.slider);
    c.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        plugin.parameters(), id, *c.slider);

    c.label = std::make_unique<ValueLabel>(*c.slider, !rotary);
    c.label->setJustificationType(juce::Justification::centred);
    c.label->setColour(juce::Label::textColourId, darkText ? ink : screenText);
    c.label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    c.label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    c.label->setColour(juce::Label::textWhenEditingColourId, darkText ? ink : screenText);
    c.label->setEditable(false, true, false);
    c.label->setTooltip(c.id + ": double click to type a value");
    addAndMakeVisible(*c.label);
    if (rotary && lockable) {
        c.lockButton = std::make_unique<InvisibleAction>();
        c.lockButton->setName(c.id + " randomization lock");
        c.lockButton->setMouseCursor(juce::MouseCursor::PointingHandCursor);
        c.lockButton->onClick = [this, parameterId = c.id] {
            plugin.setParameterLocked(parameterId, !plugin.isParameterLocked(parameterId));
            repaint();
        };
        addAndMakeVisible(*c.lockButton);
    }
}

void ShiftyEditor::addSwitch(const char* id, juce::Rectangle<float> hit) {
    switches.emplace_back();
    auto& s = switches.back();
    s.id = id;
    s.hit = hit;
    s.button = std::make_unique<InvisibleToggle>();
    if (auto* parameter = plugin.parameters().getParameter(id))
        s.button->setName(parameter->getName(80));
    s.button->onContextMenu = [this, parameterId = s.id, button = s.button.get()] {
        showParameterLockMenu(parameterId, button);
    };
    s.button->setTooltip(s.id == "motionEnable" ? "Turn motion on or off" :
                         s.id == "bypass" ? "Bypass SHIFTY" :
                         s.id == "motionSync" ? "Sync movement to the host tempo (it keeps running at the host tempo while the transport is stopped)" :
                         s.id == "vowel" ? "CLASSIC: add the optional vowel color contour. Locked out of RANDOM by default; right click to unlock." :
                         "Protect the original low end");
    addAndMakeVisible(*s.button);
    s.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        plugin.parameters(), id, *s.button);
}

ShiftyEditor::Control* ShiftyEditor::control(const char* id) noexcept {
    for (auto& c : controls) if (c.id == id) return &c;
    return nullptr;
}

ShiftyEditor::Switch* ShiftyEditor::controlSwitch(const char* id) noexcept {
    for (auto& s : switches) if (s.id == id) return &s;
    return nullptr;
}

float ShiftyEditor::valueOf(const char* id) const noexcept {
    if (auto* value = plugin.parameters().getRawParameterValue(id)) return value->load();
    return 0.0f;
}

bool ShiftyEditor::isOn(const char* id) const noexcept { return valueOf(id) > 0.5f; }

void ShiftyEditor::setParameter(const char* id, float value) {
    if (auto* parameter = plugin.parameters().getParameter(id)) {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        parameter->endChangeGesture();
    }
}

void ShiftyEditor::updateValueLabels() {
    for (auto& c : controls)
        if (!c.label->isBeingEdited()) {
            c.label->setText(formatValue(c.id, static_cast<float>(c.slider->getValue())),
                             juce::dontSendNotification);
            if (c.lockButton) {
                const bool locked = plugin.isParameterLocked(c.id);
                c.lockButton->setTooltip((locked ? "Unlock " : "Lock ") + c.slider->getName()
                    + (locked ? " for randomization" : " to keep its value when using RANDOM"));
                c.lockButton->setToggleState(locked, juce::dontSendNotification);
            }
        }
}

void ShiftyEditor::showPageForTesting(Page newPage, MotionTab tab) {
    page = newPage;
    motionTab = tab;
    live = plugin.display();
    updatePage();
    repaint();
}

void ShiftyEditor::selectPresetFilterForTesting(int filter, const juce::String& folder) {
    presetFilter = static_cast<PresetFilter>(juce::jlimit(0, 2, filter));
    presetFolderFilter = folder;
    refreshPresetList();
    repaint();
}

int ShiftyEditor::presetCountForTesting() const noexcept { return static_cast<int>(presetItems.size()); }

juce::Slider* ShiftyEditor::sliderForTesting(const juce::String& id) noexcept {
    for (auto& c : controls) if (c.id == id) return c.slider.get();
    return nullptr;
}

void ShiftyEditor::updatePage() {
    const auto engine = plugin.engineMode();
    const bool original = engine == EngineMode::original;
    const bool onMotion = page == Page::motion;
    const bool onVoice = page == Page::voice;
    const bool onPresets = page == Page::presets;
    // The vowel bar sits higher on TALK than on MOUTH (where pitch comes first).
    if (auto* vowel = control("vowelMorph")) {
        vowel->hit = engine == EngineMode::mouth ? juce::Rectangle<float>{300, 318, 300, 50}
                                                  : juce::Rectangle<float>{300, 248, 300, 50};
        vowel->value = engine == EngineMode::mouth ? juce::Rectangle<float>{180, 346, 110, 26}
                                                    : juce::Rectangle<float>{180, 280, 110, 26};
    }
    for (auto& c : controls) {
        bool visible = true;
        if (c.id == "motionDepth") visible = onMotion && motionTab == MotionTab::lfo;
        else if (c.id == "motionFreeHz") visible = onMotion && motionTab == MotionTab::lfo && !isOn("motionSync");
        else if (c.id.startsWith("env")) visible = onMotion && motionTab == MotionTab::envelope && !original;
        else if (c.id.startsWith("yoi")) visible = onMotion && motionTab == MotionTab::yoi && !original;
        else if (c.id == "vowelMorph") visible = onVoice && (engine == EngineMode::talk || engine == EngineMode::mouth);
        else if (c.id == "character" || c.id == "talk") visible = onVoice && !original;
        c.slider->setVisible(visible);
        c.label->setVisible(visible);
    }
    if (auto* sync = controlSwitch("motionSync")) sync->button->setVisible(onMotion && motionTab == MotionTab::lfo);
    if (auto* vowel = controlSwitch("vowel")) {
        vowel->hit = original ? originalVowelBounds : vowelSwitchBounds;
        vowel->button->setVisible(onVoice && (engine == EngineMode::classic || original));
    }
    latestButton->setVisible(onVoice && original);
    presetSearch.setVisible(onPresets && !savingPreset);
    randomPresetButton->setVisible(onPresets && !savingPreset);
    presetNameEditor.setVisible(onPresets && savingPreset);
    presetFolderEditor.setVisible(onPresets && savingPreset);
    resized();
}

juce::Rectangle<int> ShiftyEditor::scaled(juce::Rectangle<float> r) const {
    const float sx = static_cast<float>(getWidth()) / shellWidth;
    const float sy = static_cast<float>(getHeight()) / editorHeight;
    return juce::Rectangle<int>(juce::roundToInt((r.getX() - shellX) * sx),
                                juce::roundToInt((r.getY() - shellY) * sy),
                                juce::roundToInt(r.getWidth() * sx),
                                juce::roundToInt(r.getHeight() * sy));
}

juce::Point<float> ShiftyEditor::designPoint(juce::Point<float> p) const {
    return { p.x * shellWidth / static_cast<float>(getWidth()) + shellX,
             p.y * editorHeight / static_cast<float>(getHeight()) + shellY };
}

void ShiftyEditor::paintShell(juce::Graphics& g) {
    // Scale the shell (and its glow mask) once per size and screen density, then blit it 1:1.
    // A logical-size cache was upsampled, and soft, on Retina.
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int width = juce::jmax(1, juce::roundToInt(getWidth() * scale));
    const int height = juce::jmax(1, juce::roundToInt(getHeight() * scale));
    if (shell.isValid() && (scaledShell.getWidth() != width || scaledShell.getHeight() != height)) {
        scaledShell = juce::Image(juce::Image::RGB, width, height, false);
        juce::Graphics shellGraphics(scaledShell);
        shellGraphics.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        shellGraphics.drawImage(shell, 0, 0, width, height, shellX, shellY, shellWidth, editorHeight);
        if (glowMask.isValid()) {
            scaledGlow = juce::Image(juce::Image::ARGB, width, height, true);
            juce::Graphics glowGraphics(scaledGlow);
            glowGraphics.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            glowGraphics.drawImage(glowMask, 0, 0, width, height, shellX, shellY, shellWidth, editorHeight);
        }
    }
    if (scaledShell.isValid()) g.drawImage(scaledShell, getLocalBounds().toFloat());
}

void ShiftyEditor::paintGlow(juce::Graphics& g) {
    // The translucent shell lights up from inside with what SHIFTY is doing: brightness follows the
    // output, the light rides up the shell as the formants move up (SHIFT, plus any Motion, ENV or
    // YOI movement) and down as they move down, and its tint follows the vowel from OO to EE.
    if (glowLevel < 0.004f || !scaledGlow.isValid()) return;
    const auto bounds = getLocalBounds().toFloat();
    const float height = bounds.getHeight();
    const float position = 0.5f - 0.42f * juce::jlimit(-1.0f, 1.0f, glowShift / 12.0f);
    const juce::Point<float> centre { bounds.getCentreX(), bounds.getY() + height * position };
    const auto engine = plugin.engineMode();
    const float vowel = engine == EngineMode::talk || engine == EngineMode::mouth
                            ? juce::jlimit(0.0f, 1.0f, glowVowel / 4.0f) : 0.35f;
    const auto tint = juce::Colour(0xffbfffc8).interpolatedWith(juce::Colour(0xffe2ff9e), vowel);
    juce::ColourGradient light(tint.withAlpha(0.56f * glowLevel), centre,
                               tint.withAlpha(0.08f * glowLevel), centre.translated(0.0f, height * 0.62f), true);
    light.addColour(0.45, tint.withAlpha(0.3f * glowLevel));
    g.setGradientFill(light);
    g.drawImageTransformed(scaledGlow, juce::AffineTransform::scale(bounds.getWidth() / scaledGlow.getWidth(),
                                                                    height / scaledGlow.getHeight()), true);
}

void ShiftyEditor::resized() {
    const float scale = static_cast<float>(getWidth()) / shellWidth;
    for (auto& c : controls) {
        c.slider->setBounds(scaled(c.hit));
        c.label->setBounds(scaled(c.value));
        float fontSize = c.id == "shift" ? 46.0f : c.id == "output" ? 25.0f : 31.0f;
        if (c.id.startsWith("motion") || c.id.startsWith("env") || c.id.startsWith("yoi")) fontSize = 22.0f;
        if (c.id == "vowelMorph" || c.id == "character" || c.id == "talk") fontSize = 20.0f;
        c.label->setFont(juce::Font(juce::FontOptions(normalTypeface)
                                         .withHeight(fontSize * scale)));
        if (c.lockButton) c.lockButton->setBounds(scaled(knobLockBounds(c.id)));
    }
    for (auto& s : switches) s.button->setBounds(scaled(s.hit));
    randomButton->setBounds(scaled(randomBounds));
    locksButton->setBounds(scaled(locksBounds));
    undoButton->setBounds(scaled(undoBounds));
    redoButton->setBounds(scaled(redoBounds));
    latestButton->setBounds(scaled(latestButtonBounds));
    randomPresetButton->setBounds(scaled(randomPresetBounds));
    presetSearch.setBounds(scaled({645, 253, 275, 40}));
    presetSearch.setFont(juce::Font(juce::FontOptions(normalTypeface).withHeight(22.0f * scale)));
    presetNameEditor.setBounds(scaled({300, 305, 540, 44}));
    presetNameEditor.setFont(juce::Font(juce::FontOptions(normalTypeface).withHeight(26.0f * scale)));
    presetFolderEditor.setBounds(scaled({300, 357, 420, 40}));
    presetFolderEditor.setFont(juce::Font(juce::FontOptions(normalTypeface).withHeight(22.0f * scale)));
    if (infoButton) infoButton->setBounds(scaled({486, 1265, 152, 60}));
    if (infoOverlay) infoOverlay->setBounds(getLocalBounds());
    if (sizeReady) plugin.setEditorScale(static_cast<double>(getWidth()) / baseWidth);
}

void ShiftyEditor::drawText(juce::Graphics& g, const juce::String& str,
                            juce::Rectangle<float> bounds, float size, juce::Colour colour,
                            juce::Justification alignment) const {
    g.setFont(juce::Font(juce::FontOptions(normalTypeface).withHeight(size)));
    g.setColour(colour);
    g.drawFittedText(str, bounds.toNearestInt(), alignment, 1);
}

void ShiftyEditor::drawSegments(juce::Graphics& g, juce::Rectangle<float> area, const juce::StringArray& labels,
                                int selected, float textSize) const {
    g.setColour(juce::Colour(0xff123f31));
    g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
    g.setColour(screenLine);
    g.drawRoundedRectangle(area, area.getHeight() * 0.5f, 1.4f);
    const float width = area.getWidth() / static_cast<float>(labels.size());
    for (int i = 0; i < labels.size(); ++i) {
        const auto cell = juce::Rectangle<float>(area.getX() + width * static_cast<float>(i), area.getY(), width, area.getHeight());
        if (i == selected) {
            g.setColour(lime.withAlpha(0.88f));
            g.fillRoundedRectangle(cell.reduced(3.0f), (area.getHeight() - 6.0f) * 0.5f);
        }
        drawText(g, labels[i], cell, textSize, i == selected ? ink : screenText);
    }
}

void ShiftyEditor::drawVowelBar(juce::Graphics& g, juce::Rectangle<float> track, float value,
                                float liveValue, bool showLive) const {
    g.setColour(juce::Colour(0xff123f31));
    g.fillRoundedRectangle(track, track.getHeight() * 0.5f);
    g.setColour(screenLine);
    g.drawRoundedRectangle(track, track.getHeight() * 0.5f, 1.4f);
    const auto xAt = [&track](float v) { return track.getX() + 14.0f + (track.getWidth() - 28.0f) * juce::jlimit(0.0f, 4.0f, v) / 4.0f; };
    for (int v = 0; v <= 4; ++v) {
        const float x = xAt(static_cast<float>(v));
        g.setColour(muted.withAlpha(0.6f));
        g.drawVerticalLine(juce::roundToInt(x), track.getY() + 6.0f, track.getBottom() - 6.0f);
        drawText(g, vowelName(v), {x - 24.0f, track.getBottom() + 2.0f, 48.0f, 22.0f}, 17.0f,
                 juce::roundToInt(value) == v ? lime : muted);
    }
    g.setColour(lime.withAlpha(0.35f));
    g.fillRoundedRectangle(juce::Rectangle<float>(track.getX() + 3.0f, track.getY() + 3.0f,
                                                  xAt(value) - track.getX() - 3.0f, track.getHeight() - 6.0f),
                           (track.getHeight() - 6.0f) * 0.5f);
    if (showLive) {
        g.setColour(neon);
        g.fillEllipse(xAt(liveValue) - 5.0f, track.getCentreY() - 5.0f, 10.0f, 10.0f);
    }
    g.setColour(lime);
    g.fillEllipse(xAt(value) - 12.0f, track.getCentreY() - 12.0f, 24.0f, 24.0f);
    g.setColour(ink);
    g.drawEllipse(xAt(value) - 12.0f, track.getCentreY() - 12.0f, 24.0f, 24.0f, 2.0f);
}

void ShiftyEditor::drawCell(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& label,
                            const juce::String& value, bool active) const {
    if (label.isNotEmpty())
        drawText(g, label, area.withTrimmedLeft(8.0f).withWidth(area.getWidth() * 0.45f), 19.0f,
                 active ? screenText : muted, juce::Justification::centredLeft);
    if (value.isNotEmpty())
        drawText(g, value, area.withTrimmedLeft(area.getWidth() * 0.42f).withTrimmedRight(6.0f), 21.0f,
                 active ? lime : muted, juce::Justification::centred);
}

void ShiftyEditor::drawEngineNote(juce::Graphics& g, const juce::String& text) const {
    g.setFont(juce::Font(juce::FontOptions(normalTypeface).withHeight(20.0f)));
    g.setColour(muted);
    g.drawFittedText(text, juce::Rectangle<int>(186, 262, 750, 120), juce::Justification::centred, 4);
}

void ShiftyEditor::paint(juce::Graphics& g) {
    paintShell(g);
    paintGlow(g);

    juce::Graphics::ScopedSaveState state(g);
    const float sx = static_cast<float>(getWidth()) / shellWidth;
    const float sy = static_cast<float>(getHeight()) / editorHeight;
    g.addTransform(juce::AffineTransform(sx, 0.0f, -shellX * sx,
                                          0.0f, sy, -shellY * sy));
    drawHeader(g);
    if (page == Page::sound) drawSoundScreen(g);
    else if (page == Page::voice) drawVoiceScreen(g);
    else if (page == Page::motion) drawMotionScreen(g);
    else drawPresetsScreen(g);
    drawControlPanel(g);
    drawUtilityPanel(g);
}

void ShiftyEditor::drawHeader(juce::Graphics& g) {
    const char* labels[] = {"SOUND", "VOICE", "MOTION", "PRESETS"};
    for (int i = 0; i < 4; ++i) {
        const bool selected = static_cast<int>(page) == i;
        if (selected) {
            g.setColour(lime.withAlpha(0.86f));
            g.fillRoundedRectangle(pageTabs[i].reduced(2), 9.0f);
        }
        drawText(g, labels[i], pageTabs[i], 16.5f, selected ? ink : screenText);
    }
    if (glowLevel > 0.004f) {
        // The power LED blooms with the same light as the shell.
        const juce::Point<float> led {943.0f, 137.0f};
        g.setGradientFill(juce::ColourGradient(lime.withAlpha(0.55f * glowLevel), led,
                                               lime.withAlpha(0.0f), led.translated(30.0f, 0.0f), true));
        g.fillEllipse(juce::Rectangle<float>(60.0f, 60.0f).withCentre(led));
    }
    g.setColour(lime.withAlpha(0.9f));
    g.fillEllipse(932.0f, 126.0f, 22.0f, 22.0f);
}

void ShiftyEditor::drawSoundScreen(juce::Graphics& g) {
    const auto name = plugin.currentPresetName();
    const auto engine = plugin.engineMode();
    g.setFont(juce::Font(juce::FontOptions(displayTypeface).withHeight(32.0f)));
    g.setColour(screenText);
    g.drawFittedText(name, juce::Rectangle<int>(184, 202, 272, 42), juce::Justification::centredLeft, 1);
    drawFavoriteStar(g, {482.0f, 223.0f}, 18.0f, isFavorite(plugin.currentPresetFolder(), plugin.currentPresetName()), screenText);

    if (engine == EngineMode::original) {
        // The ORIGINAL SOUND badge, with its upgrade key.
        const auto badge = engineSelectorBounds;
        g.setColour(juce::Colour(0xff215d32));
        g.fillRoundedRectangle(badge, 20.0f);
        g.setColour(lime);
        g.drawRoundedRectangle(badge, 20.0f, 1.6f);
        drawText(g, "ORIGINAL SOUND", badge.withWidth(214.0f), 19.0f, lime);
        const juce::Rectangle<float> key {badge.getX() + 220.0f, badge.getY() + 4.0f, badge.getWidth() - 224.0f, badge.getHeight() - 8.0f};
        g.setGradientFill(juce::ColourGradient(mint.brighter(0.1f), key.getX(), key.getY(), mint.darker(0.12f),
                                               key.getX(), key.getBottom(), false));
        g.fillRoundedRectangle(key, 16.0f);
        drawText(g, "USE LATEST", key, 17.0f, ink);
    } else {
        drawSegments(g, engineSelectorBounds, {"TALK", "MOUTH", "CLASSIC"},
                     engine == EngineMode::talk ? 0 : engine == EngineMode::mouth ? 1 : 2, 19.0f);
    }
    if (plugin.isParameterLocked(ShiftyProcessor::engineLockId))
        drawPadlock(g, engineLockBounds, true, screenText, juce::Colour(0xff073828));

    const juce::Rectangle<float> graph {230.0f, 257.0f, 700.0f, 184.0f};
    g.setColour(juce::Colour(0xff236944).withAlpha(0.48f));
    for (int i = 0; i <= 16; ++i) {
        const float x = graph.getX() + graph.getWidth() * i / 16.0f;
        g.drawVerticalLine(juce::roundToInt(x), graph.getY(), graph.getBottom());
    }
    for (int i = 0; i <= 4; ++i) {
        const float y = graph.getY() + graph.getHeight() * i / 4.0f;
        g.drawHorizontalLine(juce::roundToInt(y), graph.getX(), graph.getRight());
    }

    // The analyzer's x axis: point i sits at 20 Hz * (nyquist / 20 Hz)^(i / 63).
    const double rate = plugin.getSampleRate() > 0.0 ? plugin.getSampleRate() : 48000.0;
    const auto xForHz = [&graph, rate](float hz) {
        return graph.getX() + graph.getWidth() * std::log(juce::jmax(20.0f, hz) / 20.0f)
                              / std::log(static_cast<float>(rate * 0.5) / 20.0f);
    };
    const float cut = juce::jlimit(60.0f, 200.0f, valueOf("subCut"));
    const float cutX = xForHz(cut);
    if (isOn("subSafe")) {
        g.setColour(lime.withAlpha(0.07f));
        g.fillRect(juce::Rectangle<float>(graph.getX(), graph.getY(), cutX - graph.getX(), graph.getHeight()));
        g.setColour(lime.withAlpha(0.65f));
        g.drawVerticalLine(juce::roundToInt(cutX), graph.getY(), graph.getBottom());
    }
    const auto yForDb = [&graph](float db) {
        return graph.getBottom() - (juce::jlimit(-72.0f, 0.0f, db) + 72.0f) / 72.0f * graph.getHeight();
    };
    auto drawSpectrum = [&](const std::array<float, 64>& spectrum, juce::Colour colour, float thickness) {
        juce::Path path;
        for (size_t i = 0; i < spectrum.size(); ++i) {
            const float x = graph.getX() + graph.getWidth() * static_cast<float>(i) / 63.0f;
            const float magnitude = juce::jlimit(0.0f, 1.0f, spectrum[i]);
            const float y = yForDb(magnitude > 0.0f ? 20.0f * std::log10(magnitude) : -72.0f);
            if (i == 0) path.startNewSubPath(x, y);
            else path.lineTo(x, y);
        }
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    };

    // Engine curves under the analyzer: TALK's resonance curve; CLASSIC's
    // detected (dotted) and relocated (solid) source envelope.
    juce::String status;
    if (engine == EngineMode::talk) {
        juce::Path fill, line;
        float peak = 1.0e-6f;
        std::array<float, displayPoints> response {};
        for (int i = 0; i < displayPoints; ++i) {
            response[static_cast<size_t>(i)] = TalkEngine::responseMagnitude(live.talk, displayFrequency(i, rate));
            peak = juce::jmax(peak, response[static_cast<size_t>(i)]);
        }
        fill.startNewSubPath(graph.getX(), graph.getBottom());
        for (int i = 0; i < displayPoints; ++i) {
            const float x = graph.getX() + graph.getWidth() * static_cast<float>(i) / 63.0f;
            const float y = yForDb(20.0f * std::log10(response[static_cast<size_t>(i)] / peak + 1.0e-6f) * 1.2f);
            fill.lineTo(x, y);
            if (i == 0) line.startNewSubPath(x, y);
            else line.lineTo(x, y);
        }
        fill.lineTo(graph.getRight(), graph.getBottom());
        fill.closeSubPath();
        g.setColour(lime.withAlpha(0.16f));
        g.fillPath(fill);
        g.setColour(lime.withAlpha(0.75f));
        g.strokePath(line, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (size_t k = 0; k < 2; ++k) {
            const float x = xForHz(live.talk.frequency[k]);
            g.setColour(lime.withAlpha(0.6f));
            g.drawVerticalLine(juce::roundToInt(x), graph.getY() + 4.0f, graph.getBottom());
            drawText(g, "F" + juce::String(static_cast<int>(k) + 1), {x - 18.0f, graph.getY() + 2.0f, 36.0f, 18.0f}, 14.0f, lime);
        }
        status = "TALK  " + vowelLabel(live.morph) + "  " + juce::String(live.shift, 1) + " ST";
    } else if (engine == EngineMode::mouth) {
        status = live.pitch > 0.0f
            ? "MOUTH  " + juce::String(live.pitch, 1) + " HZ  LOCK " + juce::String(juce::roundToInt(live.confidence * 100.0f)) + "%  "
                + juce::String(live.shift, 1) + " ST"
            : "MOUTH  NO PITCH  " + juce::String(live.shift, 1) + " ST";
    } else {
        if (live.envelopeValid) {
            juce::Path detected, moved;
            for (int i = 0; i < displayPoints; ++i) {
                const float x = graph.getX() + graph.getWidth() * static_cast<float>(i) / 63.0f;
                const float yd = yForDb(live.detected[static_cast<size_t>(i)] - 6.0f);
                const float ym = yForDb(live.moved[static_cast<size_t>(i)] - 6.0f);
                if (i == 0) { detected.startNewSubPath(x, yd); moved.startNewSubPath(x, ym); }
                else { detected.lineTo(x, yd); moved.lineTo(x, ym); }
            }
            juce::Path dashed;
            const float dashes[] = {6.0f, 6.0f};
            juce::PathStrokeType(2.0f).createDashedStroke(dashed, detected, dashes, 2);
            g.setColour(muted.withAlpha(0.9f));
            g.fillPath(dashed);
            g.setColour(lime);
            g.strokePath(moved, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        status = juce::String(engine == EngineMode::original ? "ORIGINAL 0.4" : "CLASSIC") + "  ENVELOPE  "
               + juce::String(live.shift, 1) + " ST";
    }
    drawSpectrum(inputSpectrum, muted, 2.0f);
    drawSpectrum(outputSpectrum, neon, 2.6f);
    g.setColour(screenText.withAlpha(0.85f));
    g.drawLine(graph.getX(), graph.getBottom(), graph.getRight(), graph.getBottom(), 1.8f);
    drawText(g, status, {graph.getRight() - 420.0f, graph.getY() + 4.0f, 414.0f, 22.0f}, 16.0f,
             screenText.withAlpha(0.9f), juce::Justification::centredRight);
    drawText(g, "0", {175, 246, 43, 28}, 22.0f, screenText);
    drawText(g, "-24", {166, 300, 57, 28}, 22.0f, screenText);
    drawText(g, "-48", {166, 351, 57, 28}, 22.0f, screenText);
    drawText(g, "-72", {166, 404, 57, 28}, 22.0f, screenText);
    drawText(g, "dB", {170, 431, 53, 27}, 21.0f, screenText);
    drawText(g, "100", {xForHz(100.0f) - 38.0f, 449, 77, 30}, 21.0f, screenText);
    drawText(g, "1k", {xForHz(1000.0f) - 38.0f, 449, 77, 30}, 21.0f, screenText);
    drawText(g, "10k", {xForHz(10000.0f) - 38.0f, 449, 77, 30}, 21.0f, screenText);
}

void ShiftyEditor::drawScreenKnob(juce::Graphics& g, const char* id, const char* label,
                                  juce::Point<float> centre, float radius) {
    const auto* c = control(id);
    if (!c) return;
    const float lo = static_cast<float>(c->slider->getMinimum());
    const float hi = static_cast<float>(c->slider->getMaximum());
    const float fraction = hi > lo ? juce::jlimit(0.0f, 1.0f, (static_cast<float>(c->slider->getValue()) - lo) / (hi - lo)) : 0.0f;
    juce::Path track, arc;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(juce::Colour(0xff1d5a3c));
    g.strokePath(track, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float angle = startAngle + fraction * (endAngle - startAngle);
    if (fraction > 0.001f) {
        arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour(lime);
        g.strokePath(arc, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour(juce::Colour(0xff123f31));
    g.fillEllipse(centre.x - radius + 9.0f, centre.y - radius + 9.0f, 2.0f * (radius - 9.0f), 2.0f * (radius - 9.0f));
    g.setColour(screenLine);
    g.drawEllipse(centre.x - radius + 9.0f, centre.y - radius + 9.0f, 2.0f * (radius - 9.0f), 2.0f * (radius - 9.0f), 1.4f);
    g.setColour(screenText);
    g.drawLine(centre.x + std::sin(angle) * radius * 0.2f, centre.y - std::cos(angle) * radius * 0.2f,
               centre.x + std::sin(angle) * (radius - 13.0f), centre.y - std::cos(angle) * (radius - 13.0f), 4.0f);
    drawText(g, label, {centre.x - 70.0f, centre.y + radius + 4.0f, 140.0f, 24.0f}, 18.0f, screenText);
    if (c->slider->hasKeyboardFocus(true)) {
        g.setColour(lime);
        g.drawEllipse(centre.x - radius - 6.0f, centre.y - radius - 6.0f, 2.0f * (radius + 6.0f), 2.0f * (radius + 6.0f), 1.6f);
    }
}

void ShiftyEditor::drawVoiceScreen(juce::Graphics& g) {
    const auto engine = plugin.engineMode();
    const bool lowLatency = plugin.classicLowLatency();
    g.setFont(juce::Font(juce::FontOptions(displayTypeface).withHeight(32.0f)));
    g.setColour(screenText);
    g.drawFittedText("VOICE", juce::Rectangle<int>(184, 202, 200, 42), juce::Justification::centredLeft, 1);
    drawText(g, juce::String(engineName(engine)) + "   " + latencyText(engine, lowLatency, plugin.getSampleRate()),
             {500, 204, 440, 38}, 19.0f, lime, juce::Justification::centredRight);

    const auto drawToggle = [&](juce::Rectangle<float> track, bool on) {
        g.setColour(on ? juce::Colour(0xff215d32) : juce::Colour(0xff123f31));
        g.fillRoundedRectangle(track, track.getHeight() * 0.5f);
        g.setColour(on ? lime : muted);
        g.drawRoundedRectangle(track, track.getHeight() * 0.5f, 1.5f);
        const float thumb = track.getHeight() - 8.0f;
        g.fillEllipse(on ? track.getRight() - thumb - 4.0f : track.getX() + 4.0f, track.getY() + 4.0f, thumb, thumb);
        drawText(g, on ? "ON" : "OFF", on ? track.withTrimmedRight(thumb + 6.0f) : track.withTrimmedLeft(thumb + 6.0f),
                 19.0f, on ? lime : screenText);
    };
    if (engine == EngineMode::original) {
        drawText(g, "ORIGINAL SOUND PRESERVED", {186, 258, 750, 34}, 26.0f, lime);
        g.setFont(juce::Font(juce::FontOptions(normalTypeface).withHeight(19.0f)));
        g.setColour(screenText);
        g.drawFittedText("This sound was made with SHIFTY 0.4 and plays exactly as it did, latency included. "
                         "CHARACTER, TALK, ENV, YOI and the vowel morph need the latest engine. UNDO switches back.",
                         juce::Rectangle<int>(196, 300, 730, 70), juce::Justification::centred, 3);
        drawText(g, "VOWEL COLOR", {184, originalVowelBounds.getY(), 116, originalVowelBounds.getHeight()}, 18.0f,
                 screenText, juce::Justification::centredLeft);
        drawToggle(originalVowelBounds, isOn("vowel"));
        const auto key = latestButtonBounds;
        g.setColour(ink.withAlpha(0.7f));
        g.fillRoundedRectangle(key.translated(0, 3.0f), 14.0f);
        g.setGradientFill(juce::ColourGradient(lime.brighter(latestButton->isMouseOver() ? 0.2f : 0.05f), key.getX(), key.getY(),
                                               lime.darker(0.1f), key.getX(), key.getBottom(), false));
        g.fillRoundedRectangle(key, 14.0f);
        g.setColour(ink.withAlpha(0.85f));
        g.drawRoundedRectangle(key, 14.0f, 2.2f);
        drawText(g, "USE LATEST SOUND", key, 22.0f, ink);
        if (latestButton->hasKeyboardFocus(true)) {
            g.setColour(lime);
            g.drawRoundedRectangle(key.expanded(4), 16.0f, 1.8f);
        }
        return;
    }

    // Left column: engine controls. Right column: CHARACTER and TALK macros.
    if (engine == EngineMode::talk || engine == EngineMode::mouth) {
        const bool talk = engine == EngineMode::talk;
        const float barY = talk ? 262.0f : 332.0f;
        drawText(g, talk ? "VOWEL" : "SHAPE", {184, barY - 12.0f, 110, 30}, 21.0f, screenText, juce::Justification::centredLeft);
        drawVowelBar(g, {310, barY, 280, 22}, valueOf("vowelMorph"), live.morph, talk);
        const auto voiceArea = talk ? voiceSetBounds : mouthVoiceSetBounds;
        drawText(g, "VOICE", {184, voiceArea.getY(), 110, voiceArea.getHeight()}, 21.0f, screenText, juce::Justification::centredLeft);
        drawSegments(g, voiceArea, {"HUMAN", "GROWL", "WOW"}, juce::roundToInt(valueOf("voiceSet")), 18.0f);
        if (talk) {
            g.setFont(juce::Font(juce::FontOptions(normalTypeface).withHeight(17.0f)));
            g.setColour(muted);
            g.drawFittedText("SHIFT scales the vowel. FOCUS sets its width, BITE its resonance.",
                             juce::Rectangle<int>(186, 394, 420, 56), juce::Justification::centredLeft, 2);
        } else {
            drawText(g, "PITCH", {184, 250, 110, 30}, 21.0f, screenText, juce::Justification::centredLeft);
            g.setFont(juce::Font(juce::FontOptions(displayTypeface).withHeight(36.0f)));
            g.setColour(live.pitch > 0.0f ? lime : muted);
            g.drawFittedText(live.pitch > 0.0f ? juce::String(live.pitch, 1) + " Hz" : juce::String("--"),
                             juce::Rectangle<int>(300, 244, 170, 44), juce::Justification::centredLeft, 1);
            drawText(g, "LOCK " + juce::String(juce::roundToInt(live.confidence * 100.0f)) + "%",
                     {470, 252, 130, 30}, 19.0f, muted, juce::Justification::centredRight);
            g.setFont(juce::Font(juce::FontOptions(normalTypeface).withHeight(17.0f)));
            g.setColour(muted);
            g.drawFittedText("CHARACTER adds this mouth shape; SHIFT resizes it.",
                             juce::Rectangle<int>(186, 448, 420, 30), juce::Justification::centredLeft, 1);
        }
    } else {
        // CLASSIC
        drawText(g, "LOW LATENCY", {184, lowLatencyBounds.getY(), 116, lowLatencyBounds.getHeight()}, 18.0f,
                 screenText, juce::Justification::centredLeft);
        drawToggle(lowLatencyBounds, lowLatency);
        drawText(g, lowLatency ? "21 ms" : "43 ms", {490, lowLatencyBounds.getY(), 110, lowLatencyBounds.getHeight()},
                 19.0f, muted, juce::Justification::centredLeft);
        drawText(g, "VOWEL COLOR", {184, vowelSwitchBounds.getY(), 116, vowelSwitchBounds.getHeight()}, 18.0f,
                 screenText, juce::Justification::centredLeft);
        drawToggle(vowelSwitchBounds, isOn("vowel"));
        g.setFont(juce::Font(juce::FontOptions(normalTypeface).withHeight(17.0f)));
        g.setColour(muted);
        g.drawFittedText("CLASSIC moves the source's own envelope. Plain saws have little to move: "
                         "use DRIVE, VOWEL COLOR, or TALK.",
                         juce::Rectangle<int>(186, 376, 420, 80), juce::Justification::centredLeft, 3);
    }
    drawScreenKnob(g, "character", "CHARACTER", {720.0f, 318.0f}, 38.0f);
    drawScreenKnob(g, "talk", "TALK", {870.0f, 318.0f}, 38.0f);
    g.setColour(screenLine.withAlpha(0.7f));
    g.drawVerticalLine(625, 262.0f, 470.0f);
}

void ShiftyEditor::drawMotionScreen(juce::Graphics& g) {
    const auto engine = plugin.engineMode();
    drawSegments(g, motionTabBounds, {"LFO", "ENV", "YOI"}, static_cast<int>(motionTab), 19.0f);
    juce::String headline;
    if (motionTab == MotionTab::lfo) {
        const int target = engine == EngineMode::talk ? juce::roundToInt(valueOf("motionTarget")) : 0;
        headline = isOn("motionEnable") ? (target == 0 ? "LFO MOVES SHIFT" : target == 1 ? "LFO MOVES THE VOWEL" : "LFO MOVES SHIFT + VOWEL")
                                        : "MOTION IS OFF";
    } else if (motionTab == MotionTab::envelope) {
        headline = engine == EngineMode::talk ? "LEVEL MOVES THE VOWEL" : "LEVEL MOVES SHIFT";
    } else {
        headline = "EACH NOTE STARTS A SWEEP";
    }
    drawText(g, headline, {520, 204, 420, 38}, 19.0f, isOn("motionEnable") || motionTab != MotionTab::lfo ? lime : muted,
             juce::Justification::centredRight);

    if (motionTab == MotionTab::lfo) drawLfoTab(g);
    else if (engine == EngineMode::original)
        drawEngineNote(g, "ENV and YOI need the latest engine.\nVOICE > USE LATEST SOUND switches this original 0.4 sound; UNDO switches back.");
    else if (motionTab == MotionTab::envelope) drawEnvelopeTab(g);
    else drawYoiTab(g);
}

void ShiftyEditor::drawLfoTab(juce::Graphics& g) {
    const auto engine = plugin.engineMode();
    const juce::Rectangle<float> graph {237.0f, 255.0f, 691.0f, 117.0f};
    g.setColour(juce::Colour(0xff246d47).withAlpha(0.5f));
    for (int i = 0; i <= 20; ++i)
        g.drawVerticalLine(juce::roundToInt(graph.getX() + graph.getWidth() * i / 20.0f),
                           graph.getY(), graph.getBottom());
    for (int i = 0; i <= 4; ++i)
        g.drawHorizontalLine(juce::roundToInt(graph.getY() + graph.getHeight() * i / 4.0f),
                             graph.getX(), graph.getRight());

    const int shape = juce::jlimit(0, 3, juce::roundToInt(valueOf("motionShape")));
    const float depth = juce::jlimit(0.0f, 12.0f, valueOf("motionDepth"));
    juce::Path curve;
    for (int i = 0; i <= 256; ++i) {
        const float x = graph.getX() + graph.getWidth() * i / 256.0f;
        const float v = waveform(i * 2.0 / 256.0, shape) * depth / 12.0f;
        const float y = graph.getCentreY() - v * graph.getHeight() * 0.48f;
        if (i == 0) curve.startNewSubPath(x, y);
        else curve.lineTo(x, y);
    }
    const bool on = isOn("motionEnable");
    g.setColour(on ? neon : muted);
    g.strokePath(curve, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    if (on && live.motionRunning) {
        // Live position on both drawn cycles.
        for (int cycle = 0; cycle < 2; ++cycle) {
            const float x = graph.getX() + graph.getWidth() * (live.lfoPhase + static_cast<float>(cycle)) / 2.0f;
            const float y = graph.getCentreY() - live.lfoValue * depth / 12.0f * graph.getHeight() * 0.48f;
            g.setColour(lime.withAlpha(0.35f));
            g.drawVerticalLine(juce::roundToInt(x), graph.getY(), graph.getBottom());
            g.setColour(lime);
            g.fillEllipse(x - 7.0f, y - 7.0f, 14.0f, 14.0f);
        }
    }
    drawText(g, "+12", {166, 244, 64, 28}, 19.0f, screenText);
    drawText(g, "0", {195, 302, 35, 28}, 19.0f, screenText);
    drawText(g, "-12", {166, 360, 64, 28}, 19.0f, screenText);
    drawText(g, "0", {232, 379, 35, 28}, 19.0f, screenText);
    drawText(g, "2 CYCLES", {825, 379, 107, 28}, 19.0f, screenText);

    g.setColour(juce::Colour(0xff39a65b).withAlpha(0.9f));
    g.drawRoundedRectangle(juce::Rectangle<float>(170, 416, 781, 55), 6.0f, 1.0f);
    for (int i = 1; i < 5; ++i) g.drawVerticalLine(juce::roundToInt(lfoCells[i].getX()), 424.0f, 465.0f);
    static const char* shapeNames[] = {"SINE", "TRIANGLE", "RAMP UP", "RAMP DOWN"};
    drawText(g, shapeNames[shape], lfoCells[0].reduced(4.0f, 4.0f), 21.0f, screenText);
    if (isOn("motionSync")) {
        static const char* rateNames[] = {"1/16", "1/8", "1/4", "1/2", "1 BAR"};
        const int rate = juce::jlimit(0, 4, juce::roundToInt(valueOf("motionRate")));
        drawText(g, juce::String("RATE ") + rateNames[rate], lfoCells[1].reduced(4.0f, 4.0f), 21.0f, screenText);
    } else {
        drawText(g, "RATE", {336, 420, 60, 46}, 20.0f, screenText);
    }
    drawText(g, "DEPTH", {524, 420, 68, 46}, 20.0f, screenText);
    drawText(g, isOn("motionSync") ? "SYNC ON" : "SYNC OFF", lfoCells[3].reduced(4.0f, 4.0f), 20.0f,
             isOn("motionSync") ? lime : screenText);
    if (engine == EngineMode::talk) {
        static const char* targets[] = {"SHIFT", "VOWEL", "BOTH"};
        drawText(g, targets[juce::jlimit(0, 2, juce::roundToInt(valueOf("motionTarget")))],
                 lfoCells[4].reduced(4.0f, 4.0f), 20.0f, lime);
    } else {
        drawText(g, "SHIFT", lfoCells[4].reduced(4.0f, 4.0f), 20.0f, muted);
    }
}

void ShiftyEditor::drawEnvelopeTab(juce::Graphics& g) {
    const auto engine = plugin.engineMode();
    const float amount = valueOf("envAmount");
    // Meters: input level (follower) and what it moves.
    const juce::Rectangle<float> levelTrack {300, 270, 600, 26}, moveTrack {300, 330, 600, 26};
    drawText(g, "LEVEL", {184, 266, 110, 34}, 20.0f, screenText, juce::Justification::centredLeft);
    drawText(g, engine == EngineMode::talk ? "VOWEL" : "SHIFT", {184, 326, 110, 34}, 20.0f, screenText,
             juce::Justification::centredLeft);
    for (const auto& track : { levelTrack, moveTrack }) {
        g.setColour(juce::Colour(0xff123f31));
        g.fillRoundedRectangle(track, 13.0f);
        g.setColour(screenLine);
        g.drawRoundedRectangle(track, 13.0f, 1.4f);
    }
    g.setColour(neon);
    g.fillRoundedRectangle(levelTrack.withWidth(juce::jmax(26.0f, levelTrack.getWidth() * live.envelope)).reduced(3.0f), 10.0f);
    if (engine == EngineMode::talk) {
        const float x = moveTrack.getX() + 13.0f + (moveTrack.getWidth() - 26.0f) * juce::jlimit(0.0f, 4.0f, live.morph) / 4.0f;
        g.setColour(lime);
        g.fillEllipse(x - 11.0f, moveTrack.getCentreY() - 11.0f, 22.0f, 22.0f);
        for (int v = 0; v <= 4; ++v)
            drawText(g, vowelName(v), {moveTrack.getX() + 13.0f + (moveTrack.getWidth() - 26.0f) * v / 4.0f - 24.0f,
                                       moveTrack.getBottom() + 2.0f, 48.0f, 22.0f}, 16.0f, muted);
    } else {
        const float x = moveTrack.getCentreX() + moveTrack.getWidth() * 0.5f * juce::jlimit(-12.0f, 12.0f, live.shift) / 12.0f;
        g.setColour(lime);
        g.fillEllipse(x - 11.0f, moveTrack.getCentreY() - 11.0f, 22.0f, 22.0f);
        drawText(g, "-12", {moveTrack.getX() - 6.0f, moveTrack.getBottom() + 2.0f, 50.0f, 22.0f}, 16.0f, muted);
        drawText(g, "+12", {moveTrack.getRight() - 44.0f, moveTrack.getBottom() + 2.0f, 50.0f, 22.0f}, 16.0f, muted);
    }
    g.setColour(juce::Colour(0xff39a65b).withAlpha(0.9f));
    g.drawRoundedRectangle(juce::Rectangle<float>(170, 416, 781, 55), 6.0f, 1.0f);
    for (int i = 1; i < 3; ++i) g.drawVerticalLine(juce::roundToInt(envCells[i].getX()), 424.0f, 465.0f);
    drawText(g, "AMOUNT", {180, 420, 115, 46}, 20.0f, std::abs(amount) > 0.001f ? screenText : muted);
    drawText(g, "ATTACK", {450, 420, 105, 46}, 20.0f, screenText);
    drawText(g, "RELEASE", {710, 420, 108, 46}, 20.0f, screenText);
}

void ShiftyEditor::drawYoiTab(juce::Graphics& g) {
    const float from = valueOf("yoiFrom"), to = valueOf("yoiTo"), amount = valueOf("yoiAmount");
    // The sweep: FROM vowel -> TO vowel, eased, with the live position.
    const juce::Rectangle<float> area {300, 258, 560, 120};
    const auto boxFor = [](float x) { return juce::Rectangle<float>(x - 40.0f, 288.0f, 80.0f, 60.0f); };
    for (const auto& [x, value, label] : { std::tuple<float, float, const char*>{area.getX(), from, "FROM"},
                                           std::tuple<float, float, const char*>{area.getRight(), to, "TO"} }) {
        const auto box = boxFor(x);
        g.setColour(juce::Colour(0xff123f31));
        g.fillRoundedRectangle(box, 12.0f);
        g.setColour(amount > 0.001f ? lime : muted);
        g.drawRoundedRectangle(box, 12.0f, 1.8f);
        drawText(g, vowelLabel(value), box, 26.0f, amount > 0.001f ? lime : muted);
        drawText(g, label, {box.getX(), box.getBottom() + 2.0f, box.getWidth(), 22.0f}, 16.0f, muted);
    }
    juce::Path sweep;
    const float x0 = area.getX() + 44.0f, x1 = area.getRight() - 44.0f;
    for (int i = 0; i <= 64; ++i) {
        const float t = static_cast<float>(i) / 64.0f;
        const float eased = 1.0f - (1.0f - t) * (1.0f - t);
        const float x = x0 + (x1 - x0) * t;
        const float y = 348.0f - 60.0f * eased;
        if (i == 0) sweep.startNewSubPath(x, y);
        else sweep.lineTo(x, y);
    }
    g.setColour(amount > 0.001f ? neon : muted);
    g.strokePath(sweep, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (amount > 0.001f && live.yoi < 0.999f) {
        // live.yoi is the eased position; invert the ease for the dot's x.
        const float t = 1.0f - std::sqrt(juce::jmax(0.0f, 1.0f - live.yoi));
        g.setColour(lime);
        g.fillEllipse(x0 + (x1 - x0) * t - 8.0f, 348.0f - 60.0f * live.yoi - 8.0f, 16.0f, 16.0f);
    }
    drawText(g, "ON EVERY NEW NOTE", {area.getX() + 60.0f, 350.0f, area.getWidth() - 120.0f, 26.0f}, 16.0f, muted);

    g.setColour(juce::Colour(0xff39a65b).withAlpha(0.9f));
    g.drawRoundedRectangle(juce::Rectangle<float>(170, 416, 781, 55), 6.0f, 1.0f);
    for (int i = 1; i < 5; ++i) g.drawVerticalLine(juce::roundToInt(yoiCells[i].getX()), 424.0f, 465.0f);
    drawText(g, "AMOUNT", {176, 420, 84, 46}, 18.0f, screenText);
    drawCell(g, yoiCells[1], "FROM", vowelLabel(from), true);
    drawCell(g, yoiCells[2], "TO", vowelLabel(to), true);
    drawText(g, "TIME", {636, 420, 58, 46}, 18.0f, screenText);
    drawText(g, "SHIFT", {806, 420, 60, 46}, 18.0f, screenText);
}

bool ShiftyEditor::isFavorite(const juce::String& folder, const juce::String& name) const {
    const bool legacyName = folder.isEmpty() || folder == "Classic" || folder == "User";
    return favorites.contains(folder + "/" + name) || (legacyName && favorites.contains(name));
}

void ShiftyEditor::toggleFavorite(const juce::String& folder, const juce::String& name) {
    if (isFavorite(folder, name)) {
        favorites.removeString(folder + "/" + name);
        if (folder.isEmpty() || folder == "Classic" || folder == "User") favorites.removeString(name);
    } else {
        favorites.add(folder.isEmpty() ? name : folder + "/" + name);
    }
    if (favoritesFile != nullptr) {
        favoritesFile->setValue("favorites", favorites.joinIntoString("\n"));
        favoritesFile->saveIfNeeded();
    }
}

void ShiftyEditor::drawPresetsScreen(juce::Graphics& g) {
    drawText(g, "PRESETS", {192, 201, 215, 45}, 35.0f, screenText,
             juce::Justification::centredLeft);
    if (savingPreset) {
        const juce::Rectangle<float> panel {242, 262, 637, 196};
        g.setColour(juce::Colour(0xff032a1e).withAlpha(0.96f));
        g.fillRoundedRectangle(panel, 12.0f);
        g.setColour(lime);
        g.drawRoundedRectangle(panel, 12.0f, 1.5f);
        drawText(g, "SAVE USER PRESET", {277, 268, 554, 32}, 23.0f, screenText);
        g.setColour(juce::Colour(0xff123f31));
        g.fillRoundedRectangle(saveFolderPickBounds, 8.0f);
        g.setColour(screenLine);
        g.drawRoundedRectangle(saveFolderPickBounds, 8.0f, 1.4f);
        drawText(g, "FOLDERS", saveFolderPickBounds, 17.0f, screenText);
        const auto name = juce::File::createLegalFileName(presetNameEditor.getText().trim());
        const auto folder = cleanFolderName(presetFolderEditor.getText());
        const auto target = (folder.isEmpty() ? plugin.userPresetDirectory() : plugin.userPresetDirectory().getChildFile(folder))
                                .getChildFile(name + ".shifty");
        const bool replaces = name.isNotEmpty() && target.existsAsFile();
        drawText(g, juce::String::fromUTF8(replaces ? "Return replaces your preset of this name  \xc2\xb7  Esc cancels"
                                                    : "Return saves  \xc2\xb7  Esc cancels  \xc2\xb7  folder is optional"),
                 {262, 404, 597, 30}, 18.0f, replaces ? juce::Colour(0xffff8f67) : muted);
        return;
    }
    {
        // Random preset: a die showing five.
        const bool hot = randomPresetButton->isMouseOver();
        const auto die = randomPresetBounds.reduced(6.0f);
        g.setColour((hot ? lime : screenText).withAlpha(hot ? 0.16f : 0.08f));
        g.fillRoundedRectangle(die, 6.0f);
        g.setColour(hot ? lime : screenText);
        g.drawRoundedRectangle(die, 6.0f, 2.0f);
        const float r = 2.7f, inset = die.getWidth() * 0.27f;
        for (const auto pip : { die.getTopLeft() + juce::Point<float>(inset, inset), die.getTopRight() + juce::Point<float>(-inset, inset),
                                die.getCentre(),
                                die.getBottomLeft() + juce::Point<float>(inset, -inset), die.getBottomRight() + juce::Point<float>(-inset, -inset) })
            g.fillEllipse(pip.x - r, pip.y - r, 2.0f * r, 2.0f * r);
        if (randomPresetButton->hasKeyboardFocus(true))
            g.drawRoundedRectangle(randomPresetBounds, 8.0f, 1.5f);
    }
    const char* filters[] = {"FACTORY", "USER", "FAV"};
    const juce::Rectangle<float> areas[] = {{186, 251, 146, 42}, {333, 251, 126, 42},
                                           {460, 251, 117, 42}};
    for (int i = 0; i < 3; ++i) {
        if (static_cast<int>(presetFilter) == i) {
            g.setColour(lime.withAlpha(0.17f));
            g.fillRoundedRectangle(areas[i], 6.0f);
            g.setColour(lime);
            g.drawRoundedRectangle(areas[i], 6.0f, 1.3f);
        }
        drawText(g, filters[i], areas[i], 21.0f, screenText);
    }
    // Folder chip
    g.setColour(juce::Colour(0xff123f31));
    g.fillRoundedRectangle(folderChipBounds, 19.0f);
    g.setColour(presetFolderFilter.isEmpty() ? screenLine : lime);
    g.drawRoundedRectangle(folderChipBounds, 19.0f, 1.4f);
    drawText(g, (presetFolderFilter.isEmpty() ? juce::String("ALL FOLDERS") : presetFolderFilter.toUpperCase()) + juce::String::fromUTF8("  \xe2\x96\xbe"),
             folderChipBounds, 19.0f, presetFolderFilter.isEmpty() ? screenText : lime);

    for (int row = 0; row < 4; ++row) {
        const int index = firstPreset + row;
        if (index >= static_cast<int>(presetItems.size())) break;
        const auto& item = presetItems[static_cast<size_t>(index)];
        const juce::Rectangle<float> area {183.0f, 302.0f + row * 35.0f, 749.0f, 34.0f};
        if (selectedPreset == index) {
            g.setColour(lime.withAlpha(0.18f));
            g.fillRoundedRectangle(area, 5.0f);
        }
        drawText(g, item.name, area.reduced(15.0f, 0.0f).withTrimmedRight(220.0f), 22.0f,
                 selectedPreset == index ? lime : screenText, juce::Justification::centredLeft);
        drawText(g, item.folder.toUpperCase(), {area.getRight() - 250.0f, area.getY(), 210.0f, area.getHeight()}, 16.0f,
                 muted, juce::Justification::centredRight);
        if (isFavorite(item.folder, item.name))
            drawFavoriteStar(g, {914.0f, area.getCentreY()}, 10.0f, true, lime);
    }
    g.setColour(lime.withAlpha(0.7f));
    g.drawHorizontalLine(442, 185.0f, 932.0f);
    const int total = static_cast<int>(presetItems.size());
    juce::String count = juce::String(total) + " presets";
    if (total > 4)
        count << "  " << (firstPreset + 1) << "-" << juce::jmin(total, firstPreset + 4);
    drawText(g, count, {193, 446, 300, 34}, 18.0f, muted, juce::Justification::centredLeft);
    drawText(g, "SAVE AS", {797, 443, 129, 35}, 20.0f, lime);
}

void ShiftyEditor::drawKnob(juce::Graphics& g, const char* id,
                            juce::Point<float> centre, float arcRadius,
                            float pointerRadius, bool large) {
    const auto* c = control(id);
    if (!c) return;
    const float lo = static_cast<float>(c->slider->getMinimum());
    const float hi = static_cast<float>(c->slider->getMaximum());
    const float fraction = hi > lo ? juce::jlimit(0.0f, 1.0f,
        (static_cast<float>(c->slider->getValue()) - lo) / (hi - lo)) : 0.0f;
    const float angle = startAngle + fraction * (endAngle - startAngle);
    if (fraction > 0.001f) {
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                          startAngle, angle, true);
        g.setColour(lime.withAlpha(0.9f));
        g.strokePath(arc, juce::PathStrokeType(large ? 7.0f : 4.0f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    }
    const float ux = std::sin(angle), uy = -std::cos(angle);
    const float x1 = centre.x + ux * pointerRadius * 0.27f;
    const float y1 = centre.y + uy * pointerRadius * 0.27f;
    const float x2 = centre.x + ux * pointerRadius;
    const float y2 = centre.y + uy * pointerRadius;
    const float thickness = large ? 11.0f : 6.5f;
    g.setColour(ink);
    g.drawLine(x1, y1, x2, y2, thickness);
    g.fillEllipse(x1 - thickness * 0.5f, y1 - thickness * 0.5f, thickness, thickness);
    g.fillEllipse(x2 - thickness * 0.5f, y2 - thickness * 0.5f, thickness, thickness);
    if (c->slider->hasKeyboardFocus(true)) {
        g.setColour(lime);
        g.drawEllipse(centre.x - arcRadius - 10.0f, centre.y - arcRadius - 10.0f,
                      2.0f * (arcRadius + 10.0f), 2.0f * (arcRadius + 10.0f), 2.0f);
    }
}

void ShiftyEditor::drawOutputKnob(juce::Graphics& g) {
    const auto* c = control("output");
    if (!c) return;
    const auto centre = outputKnobCentre;
    const auto circle = [centre](float radius) { return juce::Rectangle<float>(2.0f * radius, 2.0f * radius).withCentre(centre); };
    const auto socketColour = vfd::themeFor("SHIFTY").socket;
    // The recessed socket: the same moulding as the INFO key's.
    const auto socket = circle(outputSocketRadius);
    g.setColour(juce::Colours::black.withAlpha(0.44f));
    g.fillEllipse(socket.translated(0.0f, 2.0f));
    g.setGradientFill(juce::ColourGradient(socketColour.darker(0.5f), centre.x, socket.getY(),
                                          socketColour.brighter(0.32f), centre.x, socket.getBottom(), false));
    g.fillEllipse(socket);
    g.setColour(lime.withAlpha(0.25f));
    g.drawEllipse(socket.reduced(0.6f), 1.2f);
    // Level arc in the socket, measured from 0 dB (a small tick) to the setting.
    const float lo = static_cast<float>(c->slider->getMinimum()), hi = static_cast<float>(c->slider->getMaximum());
    const auto angleOf = [&](float value) { return startAngle + juce::jlimit(0.0f, 1.0f, (value - lo) / (hi - lo)) * (endAngle - startAngle); };
    const float zero = angleOf(0.0f), angle = angleOf(static_cast<float>(c->slider->getValue()));
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, outputArcRadius, outputArcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour(juce::Colour(0xff06291c).withAlpha(0.85f));
    g.strokePath(track, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (std::abs(angle - zero) > 0.01f) {
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, outputArcRadius, outputArcRadius, 0.0f,
                          juce::jmin(zero, angle), juce::jmax(zero, angle), true);
        g.setColour(lime.withAlpha(0.9f));
        g.strokePath(arc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const auto onRim = [&](float a, float radius) { return centre + juce::Point<float>(std::sin(a), -std::cos(a)) * radius; };
    g.setColour(mint.withAlpha(0.8f));
    g.drawLine({ onRim(zero, outputArcRadius - 3.5f), onRim(zero, outputSocketRadius - 0.5f) }, 1.6f);
    // The cap: the same pale resin as the plate knobs.
    const auto cap = circle(outputCapRadius);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillEllipse(cap.translated(0.0f, 2.0f));
    juce::ColourGradient resin(juce::Colour(0xfff4fbef), centre.x - outputCapRadius * 0.45f, cap.getY(),
                               juce::Colour(0xff9fbfa6), centre.x + outputCapRadius * 0.45f, cap.getBottom(), false);
    resin.addColour(0.45, juce::Colour(0xffdcebd8));
    g.setGradientFill(resin);
    g.fillEllipse(cap);
    g.setColour(ink.withAlpha(0.55f));
    g.drawEllipse(cap, 1.2f);
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.drawEllipse(cap.reduced(2.0f), 1.0f);
    g.setColour(ink);
    g.drawLine({ onRim(angle, outputCapRadius * 0.3f), onRim(angle, outputCapRadius * 0.8f) }, 4.5f);
    for (const float radius : { outputCapRadius * 0.3f, outputCapRadius * 0.8f })
        g.fillEllipse(juce::Rectangle<float>(4.5f, 4.5f).withCentre(onRim(angle, radius)));
    // The name sits on the board with a shadow, like the value below it.
    g.setFont(juce::Font(juce::FontOptions(normalTypeface).withHeight(20.0f)).withExtraKerningFactor(0.06f));
    g.setColour(juce::Colour(0xff031a11).withAlpha(0.75f));
    g.drawText("OUTPUT", outputNameBounds.translated(0.0f, 1.5f), juce::Justification::centredRight, false);
    g.setColour(screenText);
    g.drawText("OUTPUT", outputNameBounds, juce::Justification::centredRight, false);
    if (c->slider->hasKeyboardFocus(true)) {
        g.setColour(lime);
        g.drawEllipse(socket.expanded(3.0f), 1.6f);
    }
}

void ShiftyEditor::drawControlPanel(juce::Graphics& g) {
    drawText(g, "SHIFT", {255, 542, 194, 51}, 43.0f, ink);
    drawText(g, "FOCUS", {566, 540, 158, 39}, 31.0f, ink);
    drawText(g, "BITE", {773, 540, 155, 39}, 31.0f, ink);
    drawText(g, "DRIVE", {565, 763, 159, 39}, 31.0f, ink);
    drawText(g, "MIX", {773, 763, 157, 39}, 31.0f, ink);
    drawKnob(g, "shift", {351.0f, 755.0f}, 159.0f, 74.0f, true);
    drawKnob(g, "focus", {646.0f, 647.0f}, 68.0f, 33.0f, false);
    drawKnob(g, "bite", {851.0f, 647.0f}, 68.0f, 33.0f, false);
    drawKnob(g, "drive", {646.0f, 870.0f}, 68.0f, 33.0f, false);
    drawKnob(g, "mix", {851.0f, 870.0f}, 68.0f, 33.0f, false);
    drawText(g, "-12", {181, 884, 76, 38}, 27.0f, ink);
    drawText(g, "+12", {445, 884, 78, 38}, 27.0f, ink);
    for (const auto& c : controls) {
        if (!c.lockButton) continue;
        const bool locked = plugin.isParameterLocked(c.id);
        drawPadlock(g, knobLockBounds(c.id), locked, ink.withAlpha(locked ? 1.0f : 0.65f));
        if (c.lockButton->hasKeyboardFocus(true)) {
            g.setColour(ink);
            g.drawRoundedRectangle(knobLockBounds(c.id), 5.0f, 1.7f);
        }
    }
}

void ShiftyEditor::drawUtilityPanel(juce::Graphics& g) {
    drawText(g, "SUB SAFE", {169, 1013, 147, 34}, 25.0f, screenText);
    if (isOn("subSafe")) {
        g.setColour(lime.withAlpha(0.62f));
        g.fillEllipse(191.0f, 1054.0f, 37.0f, 37.0f);
    } else {
        g.setColour(juce::Colour(0xff061e18).withAlpha(0.55f));
        g.fillEllipse(188.0f, 1052.0f, 43.0f, 43.0f);
    }

    // OUTPUT is the knob beside INFO, so the meters take the rest of the strip.
    constexpr int segments = 24;
    for (int channel = 0; channel < 2; ++channel) {
        const float y = channel == 0 ? 1025.0f : 1065.0f;
        drawText(g, channel == 0 ? "L" : "R", {478.0f, y - 3.0f, 45.0f, 31.0f},
                 25.0f, screenText);
        const float peak = meterPeaks[static_cast<size_t>(channel)];
        for (int segment = 0; segment < segments; ++segment) {
            const bool lit = peak >= (segment + 1) / static_cast<float>(segments);
            juce::Colour colour = segment < segments / 2 ? lime :
                                  segment < segments * 6 / 7 ? juce::Colour(0xff79f1e7) :
                                                               juce::Colour(0xffff8f67);
            g.setColour(lit ? colour : juce::Colour(0xff104d37).withAlpha(0.7f));
            g.fillRect(juce::Rectangle<float>(529.0f + segment * 18.0f, y,
                                               14.0f, 22.0f));
        }
    }
    drawOutputKnob(g);

    // The footer plate, its bays and dividers are moulded into the shell art.
    const bool motionOn = isOn("motionEnable");
    const auto track = motionSwitchBounds;
    const float radius = track.getHeight() * 0.5f, thumb = track.getHeight() - 10.0f;
    drawText(g, "MOTION", {track.getX() - 5.0f, 1136.0f, track.getWidth() + 10.0f, 28.0f}, 22.0f, ink);
    g.setColour(motionOn ? juce::Colour(0xff134c29) : juce::Colour(0xff235442));
    g.fillRoundedRectangle(track, radius);
    g.setColour(motionOn ? lime : mint.withAlpha(0.8f));
    g.drawRoundedRectangle(track.reduced(1.0f), radius - 1.0f, 2.0f);
    const float thumbX = motionOn ? track.getRight() - 5.0f - thumb : track.getX() + 5.0f;
    g.setGradientFill(juce::ColourGradient(motionOn ? lime.brighter(0.2f) : mint, thumbX, track.getY() + 5.0f,
                                          motionOn ? lime : juce::Colour(0xff97c5a2), thumbX, track.getBottom() - 5.0f, false));
    g.fillEllipse(thumbX, track.getY() + 5.0f, thumb, thumb);
    const auto stateArea = motionOn ? track.withTrimmedRight(thumb + 10.0f) : track.withTrimmedLeft(thumb + 10.0f);
    drawText(g, motionOn ? "ON" : "OFF", stateArea, 24.0f, motionOn ? lime : mint);
    if (auto* s = controlSwitch("motionEnable")) {
        if (s->button->hasKeyboardFocus(true)) {
            g.setColour(ink);
            g.drawRoundedRectangle(track.expanded(4), radius + 4.0f, 2.0f);
        }
    }

    auto drawAction = [&](juce::Button& button, juce::Rectangle<float> area,
                          const char* label, float textSize, bool primary, bool enabled) {
        const auto face = primary ? lime : mint;
        g.setColour(ink.withAlpha(0.7f));
        g.fillRoundedRectangle(area.translated(0, 3.0f), 12.0f);
        g.setGradientFill(juce::ColourGradient(face.brighter(button.isMouseOver() ? 0.22f : 0.05f),
                                              area.getX(), area.getY(), face.darker(0.11f),
                                              area.getX(), area.getBottom(), false));
        g.fillRoundedRectangle(area, 12.0f);
        g.setColour(ink.withAlpha(0.85f));
        g.drawRoundedRectangle(area, 12.0f, 2.2f);
        drawText(g, label, area.reduced(7.0f, 1.0f), textSize,
                 ink.withAlpha(enabled ? 1.0f : 0.4f));
        if (button.hasKeyboardFocus(true)) {
            g.setColour(ink);
            g.drawRoundedRectangle(area.expanded(3), 14.0f, 1.8f);
        }
    };
    drawAction(*randomButton, randomBounds, "RANDOM", 29.0f, true, true);
    drawAction(*locksButton, locksBounds, "LOCKS", 27.0f, false, true);
    int lockCount = plugin.isParameterLocked(ShiftyProcessor::engineLockId) ? 1 : 0;
    for (const auto& id : ShiftyProcessor::randomizableParameterIds())
        if (plugin.isParameterLocked(id)) ++lockCount;
    drawText(g, "NEW VARIATION", randomBounds.withY(1203.0f).withHeight(20.0f).expanded(14.0f, 0.0f), 15.5f, ink.withAlpha(0.82f));
    drawText(g, juce::String(lockCount) + " LOCKED", locksBounds.withY(1203.0f).withHeight(20.0f).expanded(14.0f, 0.0f),
             16.0f, ink.withAlpha(0.82f));
    drawAction(*undoButton, undoBounds, "UNDO", 19.0f, false, plugin.canUndo());
    drawAction(*redoButton, redoBounds, "REDO", 19.0f, false, plugin.canRedo());

    const bool bypassOn = isOn("bypass");
    g.setColour(bypassOn ? ink : ink.withAlpha(0.12f));
    g.fillRoundedRectangle(bypassBounds, 17.0f);
    g.setColour(ink.withAlpha(0.55f));
    g.drawRoundedRectangle(bypassBounds, 17.0f, 1.5f);
    drawText(g, bypassOn ? "BYPASS ON" : "BYPASS OFF", bypassBounds, 18.0f,
             bypassOn ? lime : ink);
    if (auto* s = controlSwitch("bypass")) {
        if (s->button->hasKeyboardFocus(true)) {
            g.setColour(ink);
            g.drawRoundedRectangle(bypassBounds.expanded(3), 19.0f, 1.8f);
        }
    }
}

void ShiftyEditor::timerCallback() {
    undoButton->setEnabled(plugin.canUndo());
    redoButton->setEnabled(plugin.canRedo());
    plugin.copySpectrum(inputSpectrum, outputSpectrum);
    for (int channel = 0; channel < 2; ++channel)
        meterPeaks[static_cast<size_t>(channel)] = std::max(
            plugin.outputPeak(channel), meterPeaks[static_cast<size_t>(channel)] * 0.86f);
    const auto previousEngine = live.engine;
    live = plugin.display();
    live.engine = plugin.engineMode();
    // Glow: about -42 dBFS out is dark, -6 dBFS is full. Quick to light, slower to fade, like a
    // lamp behind the plastic; the light's position and tint glide rather than jump.
    const float outDb = juce::Decibels::gainToDecibels(juce::jmax(meterPeaks[0], meterPeaks[1]), -100.0f);
    const float glowTarget = isOn("bypass") ? 0.0f : juce::jlimit(0.0f, 1.0f, (outDb + 42.0f) / 36.0f);
    glowLevel += (glowTarget - glowLevel) * (glowTarget > glowLevel ? 0.5f : 0.12f);
    if (glowLevel < 0.004f) glowLevel = 0.0f;
    glowShift += (juce::jlimit(-12.0f, 12.0f, live.shift) - glowShift) * 0.3f;
    glowVowel += (live.morph - glowVowel) * 0.25f;
    updateValueLabels();
    bool needsPageUpdate = live.engine != previousEngine;
    if (page == Page::motion)
        if (auto* freeRate = control("motionFreeHz"))
            if (motionTab == MotionTab::lfo && freeRate->slider->isVisible() == isOn("motionSync")) needsPageUpdate = true;
    if (needsPageUpdate) updatePage();
    repaint();
}

void ShiftyEditor::refreshPresetList() {
    presetItems.clear();
    const auto query = presetSearch.getText().trim();
    const auto matches = [&query](const juce::String& name) { return query.isEmpty() || name.containsIgnoreCase(query); };
    const auto favoriteItem = [this](const PresetItem& item) { return isFavorite(item.folder, item.name); };
    if (presetFilter != PresetFilter::user) {
        const auto& bank = ShiftyProcessor::factoryPresets();
        for (size_t i = 0; i < bank.size(); ++i) {
            PresetItem item { bank[i].name, static_cast<int>(i), {}, bank[i].folder };
            if (!matches(item.name)) continue;
            if (presetFolderFilter.isNotEmpty() && item.folder != presetFolderFilter) continue;
            if (presetFilter == PresetFilter::favorites && !favoriteItem(item)) continue;
            presetItems.push_back(item);
        }
    }
    if (presetFilter != PresetFilter::factory) {
        const auto root = plugin.userPresetDirectory();
        for (const auto& file : plugin.userPresetFiles()) {
            const auto parent = file.getParentDirectory();
            PresetItem item { file.getFileNameWithoutExtension(), -1, file,
                              parent == root ? juce::String("User") : "User/" + parent.getFileName() };
            if (!matches(item.name)) continue;
            if (presetFolderFilter.isNotEmpty() && item.folder != presetFolderFilter
                && item.folder != "User/" + presetFolderFilter) continue;
            if (presetFilter == PresetFilter::favorites && !favoriteItem(item)) continue;
            presetItems.push_back(item);
        }
    }
    selectedPreset = presetItems.empty() ? -1 : 0;
    firstPreset = 0;
    // Start on the sound that is loaded, when it is in the list.
    const auto currentFolder = plugin.currentPresetFolder(), currentName = plugin.currentPresetName();
    for (int i = 0; i < static_cast<int>(presetItems.size()); ++i) {
        const auto& item = presetItems[static_cast<size_t>(i)];
        if (item.folder == currentFolder && item.name == currentName) {
            selectedPreset = i;
            firstPreset = juce::jlimit(0, std::max(0, static_cast<int>(presetItems.size()) - 4), i - 1);
            break;
        }
    }
}

void ShiftyEditor::loadSelectedPreset() {
    if (selectedPreset < 0 || selectedPreset >= static_cast<int>(presetItems.size())) return;
    const auto& item = presetItems[static_cast<size_t>(selectedPreset)];
    if (item.factoryIndex >= 0) plugin.loadFactoryPreset(item.factoryIndex);
    else plugin.loadUserPreset(item.file);
    updateValueLabels();
    updatePage();
    repaint();
}

void ShiftyEditor::loadRandomPreset() {
    // From the list as filtered now, never the sound already loaded. The 24
    // original 0.4.2 sounds barely change a plain synth, so the dice skips them
    // unless the list holds nothing else (e.g. the Classic folder).
    std::vector<int> picks, originals;
    const auto currentFolder = plugin.currentPresetFolder(), currentName = plugin.currentPresetName();
    for (int i = 0; i < static_cast<int>(presetItems.size()); ++i) {
        const auto& item = presetItems[static_cast<size_t>(i)];
        if (item.folder == currentFolder && item.name == currentName) continue;
        const bool original = item.factoryIndex >= 0
            && ShiftyProcessor::factoryPresets()[static_cast<size_t>(item.factoryIndex)].engine == EngineMode::original;
        (original ? originals : picks).push_back(i);
    }
    if (picks.empty()) picks = originals;
    if (picks.empty()) return;
    selectedPreset = picks[static_cast<size_t>(presetDice.nextInt(static_cast<int>(picks.size())))];
    firstPreset = juce::jlimit(0, std::max(0, static_cast<int>(presetItems.size()) - 4), selectedPreset - 1);
    loadSelectedPreset();
}

void ShiftyEditor::beginPresetSave() {
    savingPreset = true;
    presetNameEditor.setText(plugin.currentPresetName(), juce::dontSendNotification);
    const auto folder = plugin.currentPresetFolder();
    presetFolderEditor.setText(folder.startsWith("User/") ? folder.fromFirstOccurrenceOf("User/", false, false) : juce::String(),
                               juce::dontSendNotification);
    updatePage();
    presetNameEditor.grabKeyboardFocus();
    presetNameEditor.selectAll();
    repaint();
}

void ShiftyEditor::finishPresetSave() {
    const auto name = presetNameEditor.getText().trim();
    if (name.isEmpty()) return;
    const auto folder = cleanFolderName(presetFolderEditor.getText());
    plugin.saveUserPreset(name, folder);
    savingPreset = false;
    presetFilter = PresetFilter::user;
    presetFolderFilter = folder;
    presetSearch.setText({}, juce::dontSendNotification);
    refreshPresetList();
    const auto legal = juce::File::createLegalFileName(name);
    for (int i = 0; i < static_cast<int>(presetItems.size()); ++i)
        if (presetItems[static_cast<size_t>(i)].file.getFileNameWithoutExtension() == legal) {
            selectedPreset = i;
            firstPreset = std::max(0, i - 3);
            break;
        }
    updatePage();
    repaint();
}

juce::PopupMenu::Options ShiftyEditor::menuOptions(const juce::String& anchor) {
    // Drop down from the clicked control. Targeting only the editor hangs the menu below the
    // whole window, under the plugin's bottom-left corner.
    const auto area = scaled(menuAnchor(anchor));
    return juce::PopupMenu::Options().withTargetComponent(this)
                                     .withTargetScreenArea(localAreaToGlobal(area))
                                     .withMinimumWidth(area.getWidth());
}

void ShiftyEditor::showShapeMenu() {
    juce::PopupMenu menu;
    const char* names[] = {"Sine", "Triangle", "Ramp Up", "Ramp Down"};
    for (int i = 0; i < 4; ++i)
        menu.addItem(i + 1, names[i], true, juce::roundToInt(valueOf("motionShape")) == i);
    menu.addSeparator();
    menu.addItem(100, "Lock for randomization", true, plugin.isParameterLocked("motionShape"));
    menu.showMenuAsync(menuOptions("shape"),
        [safe = juce::Component::SafePointer<ShiftyEditor>(this)](int result) {
            if (safe == nullptr) return;
            if (result == 100) safe->plugin.setParameterLocked("motionShape",
                !safe->plugin.isParameterLocked("motionShape"));
            else if (result > 0) safe->setParameter("motionShape", result - 1.0f);
            safe->repaint();
        });
}

void ShiftyEditor::showRateMenu() {
    juce::PopupMenu menu;
    const char* names[] = {"1/16", "1/8", "1/4", "1/2", "1 Bar"};
    for (int i = 0; i < 5; ++i)
        menu.addItem(i + 1, names[i], true, juce::roundToInt(valueOf("motionRate")) == i);
    menu.addSeparator();
    menu.addItem(100, "Lock for randomization", true, plugin.isParameterLocked("motionRate"));
    menu.showMenuAsync(menuOptions("rate"),
        [safe = juce::Component::SafePointer<ShiftyEditor>(this)](int result) {
            if (safe == nullptr) return;
            if (result == 100) safe->plugin.setParameterLocked("motionRate",
                !safe->plugin.isParameterLocked("motionRate"));
            else if (result > 0) safe->setParameter("motionRate", result - 1.0f);
            safe->repaint();
        });
}

void ShiftyEditor::showTargetMenu() {
    juce::PopupMenu menu;
    const char* names[] = {"Shift (formant size)", "Vowel", "Shift and vowel"};
    for (int i = 0; i < 3; ++i)
        menu.addItem(i + 1, names[i], true, juce::roundToInt(valueOf("motionTarget")) == i);
    menu.addSeparator();
    menu.addItem(100, "Lock for randomization", true, plugin.isParameterLocked("motionTarget"));
    menu.showMenuAsync(menuOptions("target"),
        [safe = juce::Component::SafePointer<ShiftyEditor>(this)](int result) {
            if (safe == nullptr) return;
            if (result == 100) safe->plugin.setParameterLocked("motionTarget", !safe->plugin.isParameterLocked("motionTarget"));
            else if (result > 0) safe->setParameter("motionTarget", result - 1.0f);
            safe->repaint();
        });
}

void ShiftyEditor::showVowelMenu(const char* id) {
    juce::PopupMenu menu;
    for (int i = 0; i < 5; ++i)
        menu.addItem(i + 1, vowelName(i), true, juce::roundToInt(valueOf(id)) == i);
    menu.addSeparator();
    menu.addItem(100, "Lock for randomization", true, plugin.isParameterLocked(id));
    menu.showMenuAsync(menuOptions(id),
        [safe = juce::Component::SafePointer<ShiftyEditor>(this), parameterId = juce::String(id)](int result) {
            if (safe == nullptr) return;
            if (result == 100) safe->plugin.setParameterLocked(parameterId, !safe->plugin.isParameterLocked(parameterId));
            else if (result > 0) safe->setParameter(parameterId.toRawUTF8(), result - 1.0f);
            safe->repaint();
        });
}

void ShiftyEditor::showFolderMenu() {
    juce::PopupMenu menu;
    juce::StringArray folders;
    if (presetFilter != PresetFilter::user)
        folders.addArray(factoryFolders());
    if (presetFilter != PresetFilter::factory)
        for (const auto& folder : plugin.userPresetFolders()) folders.add(folder);
    menu.addItem(1, "All folders", true, presetFolderFilter.isEmpty());
    for (int i = 0; i < folders.size(); ++i)
        menu.addItem(i + 2, folders[i], true, presetFolderFilter == folders[i]);
    menu.showMenuAsync(menuOptions("folder"),
        [safe = juce::Component::SafePointer<ShiftyEditor>(this), folders](int result) {
            if (safe == nullptr || result <= 0) return;
            safe->presetFolderFilter = result == 1 ? juce::String() : folders[result - 2];
            safe->refreshPresetList();
            safe->repaint();
        });
}

void ShiftyEditor::showSaveFolderMenu() {
    juce::PopupMenu menu;
    const auto folders = plugin.userPresetFolders();
    menu.addItem(1, "No folder (top level)");
    for (int i = 0; i < folders.size(); ++i) menu.addItem(i + 2, folders[i]);
    menu.addSeparator();
    menu.addItem(1000, "New folder: type its name in the folder field", false, false);
    menu.showMenuAsync(menuOptions("saveFolder"),
        [safe = juce::Component::SafePointer<ShiftyEditor>(this), folders](int result) {
            if (safe == nullptr || result <= 0 || result >= 1000) return;
            safe->presetFolderEditor.setText(result == 1 ? juce::String() : folders[result - 2], juce::dontSendNotification);
            safe->repaint();
        });
}

juce::PopupMenu ShiftyEditor::createSizeMenu() const {
    juce::PopupMenu menu;
    juce::Rectangle<int> userArea;
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    if (const auto* display = isShowing() ? displays.getDisplayForRect(getScreenBounds()) : displays.getPrimaryDisplay())
        userArea = display->userBounds.toNearestInt();
    const auto fitting = sizesFitting(userArea);
    menu.addSectionHeader("Size");
    for (size_t i = 0; i < sizeChoices.size(); ++i) {
        const double scale = sizeChoices[i];
        const bool fits = std::find(fitting.begin(), fitting.end(), scale) != fitting.end();
        const bool current = std::abs(getWidth() - sizeFor(scale).x) <= 1;
        menu.addItem(static_cast<int>(i) + 1, juce::String(juce::roundToInt(scale * 100.0)) + "%", fits, current);
    }
    return menu;
}

void ShiftyEditor::applyScale(double scale) {
    const auto size = sizeFor(scale);
    setSize(size.x, size.y);
    plugin.setEditorScale(scale);
}

void ShiftyEditor::showSizeMenu() {
    createSizeMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
        [safe = juce::Component::SafePointer<ShiftyEditor>(this)](int result) {
            if (safe == nullptr || result <= 0) return;
            safe->applyScale(sizeChoices[static_cast<size_t>(result - 1)]);
        });
}

void ShiftyEditor::showParameterLockMenu(const juce::String& id, juce::Component* target) {
    juce::PopupMenu menu;
    if (auto* parameter = plugin.parameters().getParameter(id))
        menu.addSectionHeader(parameter->getName(80));
    menu.addItem(1, "Lock for randomization", true, plugin.isParameterLocked(id));
    menu.addItem(2, "All randomization locks...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target).withMousePosition(),
        [safe = juce::Component::SafePointer<ShiftyEditor>(this), id](int result) {
            if (safe == nullptr) return;
            if (result == 1) safe->plugin.setParameterLocked(id, !safe->plugin.isParameterLocked(id));
            else if (result == 2) safe->showLocksMenu();
            safe->repaint();
        });
}

void ShiftyEditor::showEngineLockMenu() {
    juce::PopupMenu menu;
    menu.addSectionHeader("Engine");
    menu.addItem(1, "Lock for randomization", true, plugin.isParameterLocked(ShiftyProcessor::engineLockId));
    menu.addItem(2, "All randomization locks...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
        [safe = juce::Component::SafePointer<ShiftyEditor>(this)](int result) {
            if (safe == nullptr) return;
            const auto* id = ShiftyProcessor::engineLockId;
            if (result == 1) safe->plugin.setParameterLocked(id, !safe->plugin.isParameterLocked(id));
            else if (result == 2) safe->showLocksMenu();
            safe->repaint();
        });
}

void ShiftyEditor::showLocksMenu() {
    juce::PopupMenu menu;
    menu.addSectionHeader("Keep these values when using RANDOM");
    auto ids = ShiftyProcessor::randomizableParameterIds();
    ids.insert(0, ShiftyProcessor::engineLockId);
    for (int i = 0; i < ids.size(); ++i)
        if (ids[i] == ShiftyProcessor::engineLockId)
            menu.addItem(i + 1, "Engine (TALK / MOUTH / CLASSIC)", true, plugin.isParameterLocked(ids[i]));
        else if (auto* parameter = plugin.parameters().getParameter(ids[i]))
            menu.addItem(i + 1, parameter->getName(80), true, plugin.isParameterLocked(ids[i]));
    menu.addSeparator();
    menu.addItem(1001, "Lock all");
    menu.addItem(1002, "Unlock all");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(locksButton.get())
                          .withMinimumWidth(260),
        [safe = juce::Component::SafePointer<ShiftyEditor>(this), ids](int result) {
            if (safe == nullptr) return;
            if (result > 0 && result <= ids.size()) {
                const auto& id = ids[result - 1];
                safe->plugin.setParameterLocked(id, !safe->plugin.isParameterLocked(id));
            } else if (result == 1001 || result == 1002) {
                for (const auto& id : ids) safe->plugin.setParameterLocked(id, result == 1001);
            }
            safe->updateValueLabels();
            safe->repaint();
        });
}

void ShiftyEditor::mouseDown(const juce::MouseEvent& e) {
    const auto p = designPoint(e.position);
    // Right click on the engine selector: its randomization lock. Anywhere
    // else on the shell a control does not claim: editor size.
    if (e.mods.isPopupMenu()) {
        if (page == Page::sound && engineSelectorBounds.contains(p)) showEngineLockMenu();
        else showSizeMenu();
        return;
    }
    if (p.y >= 115.0f && p.y <= 161.0f && p.x >= pageTabs[0].getX() && p.x < pageTabs[3].getRight()) {
        for (int i = 0; i < 4; ++i)
            if (pageTabs[i].contains(p.x, juce::jlimit(pageTabs[i].getY(), pageTabs[i].getBottom() - 1.0f, p.y)))
                page = static_cast<Page>(i);
        updatePage();
        repaint();
        return;
    }
    const auto engine = plugin.engineMode();
    if (page == Page::sound) {
        if (p.x >= 462.0f && p.x <= 504.0f && p.y >= 200.0f && p.y <= 248.0f) {
            toggleFavorite(plugin.currentPresetFolder(), plugin.currentPresetName());
            repaint();
            return;
        }
        if (engineSelectorBounds.contains(p)) {
            if (engine == EngineMode::original) {
                if (p.x >= engineSelectorBounds.getX() + 220.0f) plugin.useLatestSound();
            } else {
                const int segment = juce::jlimit(0, 2, static_cast<int>((p.x - engineSelectorBounds.getX()) / (engineSelectorBounds.getWidth() / 3.0f)));
                plugin.setEngine(segment == 0 ? EngineMode::talk : segment == 1 ? EngineMode::mouth : EngineMode::classic,
                                 plugin.classicLowLatency());
            }
            updatePage();
            repaint();
            return;
        }
    }
    if (page == Page::voice) {
        const auto voiceArea = engine == EngineMode::mouth ? mouthVoiceSetBounds : voiceSetBounds;
        if ((engine == EngineMode::talk || engine == EngineMode::mouth) && voiceArea.contains(p)) {
            setParameter("voiceSet", static_cast<float>(juce::jlimit(0, 2, static_cast<int>((p.x - voiceArea.getX()) / (voiceArea.getWidth() / 3.0f)))));
            repaint();
            return;
        }
        if (engine == EngineMode::classic && lowLatencyBounds.contains(p)) {
            plugin.setEngine(EngineMode::classic, !plugin.classicLowLatency());
            repaint();
            return;
        }
    }
    if (page == Page::motion) {
        if (motionTabBounds.contains(p)) {
            motionTab = static_cast<MotionTab>(juce::jlimit(0, 2, static_cast<int>((p.x - motionTabBounds.getX()) / (motionTabBounds.getWidth() / 3.0f))));
            updatePage();
            repaint();
            return;
        }
        if (motionTab == MotionTab::lfo) {
            if (lfoCells[0].contains(p)) showShapeMenu();
            else if (lfoCells[1].contains(p) && isOn("motionSync")) showRateMenu();
            else if (lfoCells[4].contains(p) && engine == EngineMode::talk) showTargetMenu();
            return;
        }
        if (motionTab == MotionTab::yoi && engine != EngineMode::original) {
            if (yoiCells[1].contains(p)) showVowelMenu("yoiFrom");
            else if (yoiCells[2].contains(p)) showVowelMenu("yoiTo");
            return;
        }
    }
    if (page == Page::presets) {
        if (savingPreset) {
            if (saveFolderPickBounds.contains(p)) showSaveFolderMenu();
            return;
        }
        if (folderChipBounds.contains(p)) {
            showFolderMenu();
        } else if (p.y >= 251.0f && p.y < 294.0f && p.x >= 185.0f && p.x < 578.0f) {
            presetFilter = p.x < 333.0f ? PresetFilter::factory :
                           p.x < 460.0f ? PresetFilter::user : PresetFilter::favorites;
            presetFolderFilter = {};
            refreshPresetList();
            repaint();
        } else if (p.y >= 302.0f && p.y < 442.0f && p.x >= 183.0f && p.x < 933.0f) {
            const int index = firstPreset + static_cast<int>((p.y - 302.0f) / 35.0f);
            if (index >= 0 && index < static_cast<int>(presetItems.size())) {
                if (p.x >= 895.0f) {
                    // The star column toggles a favorite.
                    const auto& item = presetItems[static_cast<size_t>(index)];
                    toggleFavorite(item.folder, item.name);
                }
                selectedPreset = index;
                repaint();
            }
        } else if (p.y >= 443.0f && p.y < 479.0f && p.x >= 792.0f && p.x < 932.0f) {
            beginPresetSave();
        }
    }
}

void ShiftyEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    if (page != Page::presets || savingPreset) return;
    const auto p = designPoint(e.position);
    if (p.y >= 302.0f && p.y < 442.0f && p.x >= 183.0f && p.x < 895.0f) {
        const int index = firstPreset + static_cast<int>((p.y - 302.0f) / 35.0f);
        if (index >= 0 && index < static_cast<int>(presetItems.size())) {
            selectedPreset = index;
            loadSelectedPreset();
        }
    }
}

void ShiftyEditor::mouseWheelMove(const juce::MouseEvent& e,
                                   const juce::MouseWheelDetails& wheel) {
    const auto p = designPoint(e.position);
    if (page == Page::presets && p.y >= 300.0f && p.y <= 442.0f) {
        const int maxFirst = std::max(0, static_cast<int>(presetItems.size()) - 4);
        firstPreset = juce::jlimit(0, maxFirst, firstPreset + (wheel.deltaY < 0 ? 1 : -1));
        repaint();
    } else {
        juce::AudioProcessorEditor::mouseWheelMove(e, wheel);
    }
}

bool ShiftyEditor::keyPressed(const juce::KeyPress& key) {
    if (page == Page::presets && !savingPreset) {
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
            if (!presetItems.empty()) {
                selectedPreset = juce::jlimit(0, static_cast<int>(presetItems.size()) - 1,
                    selectedPreset + (key == juce::KeyPress::downKey ? 1 : -1));
                firstPreset = juce::jlimit(0, std::max(0, static_cast<int>(presetItems.size()) - 4),
                                          std::min(firstPreset, selectedPreset));
                if (selectedPreset >= firstPreset + 4) firstPreset = selectedPreset - 3;
                repaint();
            }
            return true;
        }
        if (key == juce::KeyPress::returnKey) { loadSelectedPreset(); return true; }
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z') {
        if (key.getModifiers().isShiftDown()) plugin.redo();
        else plugin.undo();
        return true;
    }
    return false;
}

} // namespace shifty
