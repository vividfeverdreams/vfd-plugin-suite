#include "ShiftyEditor.h"
#include "BinaryData.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace shifty {
namespace {
constexpr int shellX = 50, shellY = 66, shellWidth = 1024, shellHeight = 1260;
constexpr int infoBandTop = 1244, infoBandHeight = 70;
constexpr int editorHeight = shellHeight + infoBandHeight;
constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
constexpr float endAngle = juce::MathConstants<float>::pi * 2.75f;
const juce::Colour ink { 0xff073d2b }, screenText { 0xffd7ffee }, lime { 0xffc3ff62 };
const juce::Colour muted { 0xff9bc2b2 }, mint { 0xffd7f4cd }, neon { 0xff69ff9a };
const juce::Rectangle<float> motionSwitchBounds {177, 1160, 168, 58};
const juce::Rectangle<float> randomBounds {367, 1146, 168, 65};
const juce::Rectangle<float> locksBounds {550, 1146, 150, 65};
const juce::Rectangle<float> undoBounds {721, 1126, 86, 42};
const juce::Rectangle<float> redoBounds {816, 1126, 86, 42};
const juce::Rectangle<float> bypassBounds {721, 1180, 181, 40};
const juce::Rectangle<float> vowelSwitchBounds {532, 207, 241, 43};

class InvisibleSlider final : public juce::Slider {
public:
    void paint(juce::Graphics&) override {}
    void mouseDown(const juce::MouseEvent& e) override {
        contextClick = e.mods.isPopupMenu() && static_cast<bool>(onContextMenu);
        if (contextClick) onContextMenu();
        else juce::Slider::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!contextClick) juce::Slider::mouseDrag(e);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!contextClick) juce::Slider::mouseUp(e);
        contextClick = false;
    }
    std::function<void()> onContextMenu;
private:
    bool contextClick = false;
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
                 bool locked, juce::Colour colour) {
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
        g.setColour(mint);
        g.fillEllipse(box.getCentreX() - 1.8f, box.getY() + 5.0f, 3.6f, 3.6f);
        g.fillRect(box.getCentreX() - 0.8f, box.getY() + 7.0f, 1.6f, 4.0f);
    }
}

juce::String formatValue(const juce::String& id, float value) {
    if (std::abs(value) < 0.0001f) value = 0.0f;
    if (id == "shift") return juce::String(value, 1) + " st";
    if (id == "subCut") return juce::String(juce::roundToInt(value)) + " Hz";
    if (id == "output") return juce::String(value, 1) + " dB";
    if (id == "motionDepth") return juce::String(value, 1) + " st";
    if (id == "motionFreeHz") return juce::String(value, 2) + " Hz";
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
};

ShiftyEditor::ShiftyEditor(ShiftyProcessor& p)
    : juce::AudioProcessorEditor(p), plugin(p),
      shell(juce::ImageCache::getFromMemory(BinaryData::DeviceShell_png,
                                            BinaryData::DeviceShell_pngSize)),
      normalTypeface(juce::Typeface::createSystemTypefaceFor(
          BinaryData::ChakraPetchSemiBold_ttf, BinaryData::ChakraPetchSemiBold_ttfSize)),
      displayTypeface(juce::Typeface::createSystemTypefaceFor(
          BinaryData::ChakraPetchBoldItalic_ttf, BinaryData::ChakraPetchBoldItalic_ttfSize)) {
    setOpaque(false);
    setWantsKeyboardFocus(true);

    addControl("shift", {196, 606, 310, 285}, {245, 896, 220, 70}, true, true);
    addControl("focus", {576, 575, 144, 142}, {585, 706, 125, 45}, true, true);
    addControl("bite", {778, 575, 144, 142}, {790, 706, 125, 45}, true, true);
    addControl("drive", {576, 798, 144, 142}, {585, 927, 125, 45}, true, true);
    addControl("mix", {778, 798, 144, 142}, {790, 927, 125, 45}, true, true);
    addControl("subCut", {296, 1042, 120, 60}, {296, 1043, 125, 62}, false, false);
    addControl("output", {458, 1042, 155, 60}, {462, 1043, 150, 62}, false, false);
    addControl("motionDepth", {585, 418, 181, 52}, {653, 421, 111, 48}, false, false);
    addControl("motionFreeHz", {363, 418, 189, 52}, {432, 421, 115, 48}, false, false);

    addSwitch("subSafe", {181, 1045, 103, 55});
    addSwitch("motionEnable", motionSwitchBounds);
    addSwitch("bypass", bypassBounds);
    addSwitch("motionSync", {771, 418, 176, 52});
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
    addAction(randomButton, "Randomize", "Create a new variation using every unlocked parameter. Undo restores the previous sound.",
              [this] { plugin.randomizeParameters(); updateValueLabels(); updatePage(); repaint(); });
    addAction(locksButton, "Randomization locks", "Choose which parameters RANDOM keeps unchanged. Locks do not affect manual changes or preset loading.",
              [this] { showLocksMenu(); });
    addAction(undoButton, "Undo", "Undo the last change or random variation", [this] {
        plugin.undo(); updateValueLabels(); updatePage(); repaint();
    });
    addAction(redoButton, "Redo", "Redo the last undone change", [this] {
        plugin.redo(); updateValueLabels(); updatePage(); repaint();
    });

    for (auto& c : controls) {
        c.slider->onValueChange = [this] { updateValueLabels(); repaint(); };
        c.label->onTextChange = [id = c.id, slider = c.slider.get(), label = c.label.get()] {
            float value = static_cast<float>(label->getText().getDoubleValue());
            if (id == "focus" || id == "bite" || id == "drive" || id == "mix")
                value *= 0.01f;
            slider->setValue(value, juce::sendNotificationSync);
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
    presetSearch.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff398c5b));
    presetSearch.setColour(juce::TextEditor::focusedOutlineColourId, lime);
    presetSearch.onTextChange = [this] { refreshPresetList(); repaint(); };
    presetSearch.onReturnKey = [this] { loadSelectedPreset(); };

    addAndMakeVisible(presetNameEditor);
    presetNameEditor.setMultiLine(false);
    presetNameEditor.setTextToShowWhenEmpty("Preset name", muted);
    presetNameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff073828));
    presetNameEditor.setColour(juce::TextEditor::textColourId, screenText);
    presetNameEditor.setColour(juce::TextEditor::outlineColourId, lime);
    presetNameEditor.setColour(juce::TextEditor::focusedOutlineColourId, lime);
    presetNameEditor.onReturnKey = [this] { finishPresetSave(); };
    presetNameEditor.onEscapeKey = [this] { savingPreset = false; updatePage(); repaint(); };

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
    setResizeLimits(500, 649, 850, 1104);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(shellWidth) / editorHeight);
    setSize(650, 844);
    startTimerHz(30);
}

ShiftyEditor::~ShiftyEditor() { stopTimer(); }

void ShiftyEditor::addControl(const char* id, juce::Rectangle<float> hit,
                              juce::Rectangle<float> value, bool rotary, bool darkText) {
    controls.emplace_back();
    auto& c = controls.back();
    c.id = id;
    c.hit = hit;
    c.value = value;
    c.darkText = darkText;
    c.slider = std::make_unique<InvisibleSlider>();
    c.slider->setSliderStyle(rotary ? juce::Slider::RotaryVerticalDrag
                                    : juce::Slider::LinearHorizontal);
    c.slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    c.slider->setScrollWheelEnabled(false);
    c.slider->setPopupMenuEnabled(false);
    juce::String description = c.id;
    if (c.id == "shift") description = "Shift: move the sound's own spectral envelope lower or higher while preserving its notes";
    else if (c.id == "focus") description = "Focus: follow broad spectral shapes or finer formant detail in the source sound";
    else if (c.id == "bite") description = "Bite: strengthen the contrast of the source formant shift";
    else if (c.id == "drive") description = "Drive: add harmonics for fuller formant movement on sparse bass sounds";
    else if (c.id == "mix") description = "Mix: blend in the formant effect; 100% gives the clearest change";
    c.slider->setTooltip(description + ". Drag to adjust, double click to reset, right click for randomization lock.");
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
    if (rotary) {
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
                         s.id == "motionSync" ? "Sync movement to the host tempo" :
                         s.id == "vowel" ? "Add optional vowel color to the source formant shift. Off by default and locked out of RANDOM; right click to unlock." :
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

void ShiftyEditor::updatePage() {
    const bool onMotion = page == Page::motion;
    const bool onPresets = page == Page::presets;
    for (auto& c : controls) {
        const bool motionControl = c.id == "motionDepth" || c.id == "motionFreeHz";
        bool visible = !motionControl || onMotion;
        if (c.id == "motionFreeHz") visible = onMotion && !isOn("motionSync");
        c.slider->setVisible(visible);
        c.label->setVisible(visible);
    }
    if (auto* sync = controlSwitch("motionSync")) sync->button->setVisible(onMotion);
    if (auto* vowel = controlSwitch("vowel")) vowel->button->setVisible(page == Page::sound);
    presetSearch.setVisible(onPresets && !savingPreset);
    presetNameEditor.setVisible(onPresets && savingPreset);
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

void ShiftyEditor::resized() {
    if (shell.isValid() && getWidth() > 0 && getHeight() > 0 &&
        (scaledShell.getWidth() != getWidth() || scaledShell.getHeight() != getHeight())) {
        scaledShell = juce::Image(juce::Image::ARGB, getWidth(), getHeight(), true);
        juce::Graphics shellGraphics(scaledShell);
        const float sx = static_cast<float>(getWidth()) / shellWidth;
        const float sy = static_cast<float>(getHeight()) / editorHeight;
        shellGraphics.addTransform(juce::AffineTransform(sx, 0, -shellX * sx, 0, sy, -shellY * sy));
        // Keep all original control geometry and the lower molded rim intact.
        // Only the exposed board strip gains room for the physical INFO key.
        shellGraphics.drawImage(shell, shellX, shellY, shellWidth, infoBandTop - shellY,
                                shellX, shellY, shellWidth, infoBandTop - shellY);
        shellGraphics.drawImage(shell, shellX, infoBandTop, shellWidth, infoBandHeight,
                                shellX, infoBandTop, shellWidth, 1);
        // Reuse the photographed board surface, including its components and
        // resin illumination, rather than introducing a separately drawn plate.
        shellGraphics.drawImage(shell, 135, infoBandTop, 850, infoBandHeight,
                                135, infoBandTop, 850, 40);
        shellGraphics.drawImage(shell, shellX, infoBandTop + infoBandHeight,
                                shellWidth, shellY + shellHeight - infoBandTop,
                                shellX, infoBandTop, shellWidth, shellY + shellHeight - infoBandTop);
    }
    const float scale = static_cast<float>(getWidth()) / shellWidth;
    for (auto& c : controls) {
        c.slider->setBounds(scaled(c.hit));
        c.label->setBounds(scaled(c.value));
        float fontSize = c.id == "shift" ? 46.0f :
                         (c.id == "subCut" || c.id == "output") ? 31.0f : 31.0f;
        if (c.id == "motionDepth" || c.id == "motionFreeHz") fontSize = 22.0f;
        c.label->setFont(juce::Font(juce::FontOptions(normalTypeface)
                                         .withHeight(fontSize * scale)));
        if (c.lockButton) c.lockButton->setBounds(scaled(knobLockBounds(c.id)));
    }
    for (auto& s : switches) s.button->setBounds(scaled(s.hit));
    randomButton->setBounds(scaled(randomBounds));
    locksButton->setBounds(scaled(locksBounds));
    undoButton->setBounds(scaled(undoBounds));
    redoButton->setBounds(scaled(redoBounds));
    presetSearch.setBounds(scaled({645, 253, 275, 40}));
    presetSearch.setFont(juce::Font(juce::FontOptions(normalTypeface).withHeight(22.0f * scale)));
    presetNameEditor.setBounds(scaled({300, 333, 540, 49}));
    presetNameEditor.setFont(juce::Font(juce::FontOptions(normalTypeface).withHeight(28.0f * scale)));
    if (infoButton) infoButton->setBounds(scaled({486, 1249, 152, 60}));
    if (infoOverlay) infoOverlay->setBounds(getLocalBounds());
}

void ShiftyEditor::drawText(juce::Graphics& g, const juce::String& str,
                            juce::Rectangle<float> bounds, float size, juce::Colour colour,
                            juce::Justification alignment) const {
    g.setFont(juce::Font(juce::FontOptions(normalTypeface).withHeight(size)));
    g.setColour(colour);
    g.drawFittedText(str, bounds.toNearestInt(), alignment, 1);
}

void ShiftyEditor::paint(juce::Graphics& g) {
    if (scaledShell.isValid()) g.drawImageAt(scaledShell, 0, 0);

    juce::Graphics::ScopedSaveState state(g);
    const float sx = static_cast<float>(getWidth()) / shellWidth;
    const float sy = static_cast<float>(getHeight()) / editorHeight;
    g.addTransform(juce::AffineTransform(sx, 0.0f, -shellX * sx,
                                          0.0f, sy, -shellY * sy));
    drawHeader(g);
    if (page == Page::sound) drawSoundScreen(g);
    else if (page == Page::motion) drawMotionScreen(g);
    else drawPresetsScreen(g);
    drawControlPanel(g);
    drawUtilityPanel(g);
}

void ShiftyEditor::drawHeader(juce::Graphics& g) {
    const char* labels[] = {"SOUND", "MOTION", "PRESETS"};
    const juce::Rectangle<float> tabs[] = {{594, 116, 107, 42},
                                          {701, 116, 107, 42},
                                          {808, 116, 107, 42}};
    for (int i = 0; i < 3; ++i) {
        const bool selected = static_cast<int>(page) == i;
        if (selected) {
            g.setColour(lime.withAlpha(0.86f));
            g.fillRoundedRectangle(tabs[i].reduced(2), 9.0f);
        }
        drawText(g, labels[i], tabs[i], 18.0f, selected ? ink : screenText);
    }
    g.setColour(lime.withAlpha(0.9f));
    g.fillEllipse(932.0f, 126.0f, 22.0f, 22.0f);
}

void ShiftyEditor::drawSoundScreen(juce::Graphics& g) {
    const auto name = plugin.currentPresetName();
    g.setFont(juce::Font(juce::FontOptions(displayTypeface).withHeight(34.0f)));
    g.setColour(screenText);
    g.drawFittedText(name, juce::Rectangle<int>(195, 204, 260, 42), juce::Justification::centredLeft, 1);
    drawFavoriteStar(g, {474.0f, 225.0f}, 20.0f, favorites.contains(name), screenText);
    // Keep the optional color beside the spectrum legend, below the main tabs.
    const bool vowelOn = isOn("vowel");
    drawText(g, "VOWEL", {532, 211, 91, 34}, 20.0f, muted);
    const juce::Rectangle<float> track {631, 209, 140, 39};
    g.setColour(vowelOn ? juce::Colour(0xff215d32) : juce::Colour(0xff123f31));
    g.fillRoundedRectangle(track, 19.5f);
    g.setColour(vowelOn ? lime : muted);
    g.drawRoundedRectangle(track, 19.5f, 1.5f);
    const float thumbX = vowelOn ? track.getRight() - 36.0f : track.getX() + 4.0f;
    g.setColour(vowelOn ? lime : muted);
    g.fillEllipse(thumbX, 213.0f, 31.0f, 31.0f);
    drawText(g, vowelOn ? "ON" : "OFF",
             vowelOn ? juce::Rectangle<float>(643, 211, 87, 34)
                     : juce::Rectangle<float>(673, 211, 85, 34),
             21.0f, vowelOn ? lime : screenText);
    if (auto* vowel = controlSwitch("vowel")) {
        if (vowel->button->hasKeyboardFocus(true)) {
            g.setColour(lime);
            g.drawRoundedRectangle(vowelSwitchBounds.expanded(2), 8.0f, 1.8f);
        }
    }
    g.setColour(neon);
    g.fillRoundedRectangle(797.0f, 217.0f, 43.0f, 4.0f, 2.0f);
    g.setColour(muted);
    g.fillRoundedRectangle(797.0f, 240.0f, 43.0f, 4.0f, 2.0f);
    drawText(g, "SHIFTED", {846, 207, 104, 25}, 18.0f, screenText);
    drawText(g, "INPUT", {846, 231, 104, 25}, 18.0f, screenText);

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

    const float cut = juce::jlimit(60.0f, 200.0f, valueOf("subCut"));
    const float cutX = graph.getX() + graph.getWidth()
        * std::log(cut / 40.0f) / std::log(20000.0f / 40.0f);
    if (isOn("subSafe")) {
        g.setColour(lime.withAlpha(0.07f));
        g.fillRect(juce::Rectangle<float>(graph.getX(), graph.getY(), cutX - graph.getX(), graph.getHeight()));
        g.setColour(lime.withAlpha(0.65f));
        g.drawVerticalLine(juce::roundToInt(cutX), graph.getY(), graph.getBottom());
    }

    auto drawSpectrum = [&](const std::array<float, 64>& spectrum, juce::Colour colour,
                            float thickness) {
        juce::Path path;
        for (size_t i = 0; i < spectrum.size(); ++i) {
            const float x = graph.getX() + graph.getWidth() * static_cast<float>(i) / 63.0f;
            const float magnitude = juce::jlimit(0.0f, 1.0f, spectrum[i]);
            const float relativeDb = magnitude > 0.0f
                ? juce::jlimit(-72.0f, 0.0f, 20.0f * std::log10(magnitude))
                : -72.0f;
            const float y = graph.getBottom()
                - (relativeDb + 72.0f) / 72.0f * graph.getHeight();
            if (i == 0) path.startNewSubPath(x, y);
            else path.lineTo(x, y);
        }
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    };
    drawSpectrum(inputSpectrum, muted, 2.4f);
    drawSpectrum(outputSpectrum, neon, 3.1f);
    g.setColour(screenText.withAlpha(0.85f));
    g.drawLine(graph.getX(), graph.getBottom(), graph.getRight(), graph.getBottom(), 1.8f);
    drawText(g, "0", {175, 246, 43, 28}, 22.0f, screenText);
    drawText(g, "-24", {166, 300, 57, 28}, 22.0f, screenText);
    drawText(g, "-48", {166, 351, 57, 28}, 22.0f, screenText);
    drawText(g, "-72", {166, 404, 57, 28}, 22.0f, screenText);
    drawText(g, "dB", {170, 431, 53, 27}, 21.0f, screenText);
    drawText(g, "100", {266, 449, 77, 30}, 21.0f, screenText);
    drawText(g, "1k", {537, 449, 77, 30}, 21.0f, screenText);
    drawText(g, "10k", {805, 449, 77, 30}, 21.0f, screenText);
}

void ShiftyEditor::drawMotionScreen(juce::Graphics& g) {
    const auto name = plugin.currentPresetName();
    g.setFont(juce::Font(juce::FontOptions(displayTypeface).withHeight(34.0f)));
    g.setColour(screenText);
    g.drawFittedText(name, juce::Rectangle<int>(195, 204, 260, 42), juce::Justification::centredLeft, 1);
    drawText(g, "SHIFT MOVEMENT", {756, 209, 183, 31}, 19.0f, screenText,
             juce::Justification::centredRight);

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
    g.setColour(isOn("motionEnable") ? neon : muted);
    g.strokePath(curve, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    drawText(g, "+12", {166, 244, 64, 28}, 19.0f, screenText);
    drawText(g, "0", {195, 302, 35, 28}, 19.0f, screenText);
    drawText(g, "-12", {166, 360, 64, 28}, 19.0f, screenText);
    drawText(g, "0", {232, 379, 35, 28}, 19.0f, screenText);
    drawText(g, "2 CYCLES", {825, 379, 107, 28}, 19.0f, screenText);

    g.setColour(juce::Colour(0xff39a65b).withAlpha(0.9f));
    g.drawRoundedRectangle(juce::Rectangle<float>(170, 416, 781, 55), 6.0f, 1.0f);
    for (int x : {360, 552, 772}) g.drawVerticalLine(x, 424.0f, 465.0f);
    static const char* shapeNames[] = {"SINE", "TRIANGLE", "RAMP UP", "RAMP DOWN"};
    drawText(g, shapeNames[shape], {178, 420, 178, 46}, 23.0f, screenText);
    if (isOn("motionSync")) {
        static const char* rateNames[] = {"1/16", "1/8", "1/4", "1/2", "1 BAR"};
        const int rate = juce::jlimit(0, 4, juce::roundToInt(valueOf("motionRate")));
        drawText(g, juce::String("RATE ") + rateNames[rate], {364, 420, 186, 46},
                 22.0f, screenText);
    } else {
        drawText(g, "RATE", {365, 420, 69, 46}, 21.0f, screenText);
    }
    drawText(g, "DEPTH", {557, 420, 91, 46}, 21.0f, screenText);
    drawText(g, isOn("motionSync") ? "SYNC ON" : "SYNC OFF", {776, 420, 173, 46},
             22.0f, isOn("motionSync") ? lime : screenText);
}

void ShiftyEditor::drawPresetsScreen(juce::Graphics& g) {
    drawText(g, "PRESETS", {192, 201, 215, 45}, 35.0f, screenText,
             juce::Justification::centredLeft);
    if (savingPreset) {
        g.setColour(juce::Colour(0xff032a1e).withAlpha(0.96f));
        g.fillRoundedRectangle(juce::Rectangle<float>(242, 273, 637, 167), 12.0f);
        g.setColour(lime);
        g.drawRoundedRectangle(juce::Rectangle<float>(242, 273, 637, 167), 12.0f, 1.5f);
        drawText(g, "SAVE USER PRESET", {277, 288, 554, 36}, 25.0f, screenText);
        drawText(g, "Press Return to save  ·  Esc to cancel", {282, 388, 548, 34},
                 19.0f, muted);
        return;
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
    for (int row = 0; row < 4; ++row) {
        const int index = firstPreset + row;
        if (index >= static_cast<int>(presetItems.size())) break;
        const juce::Rectangle<float> area {183.0f, 302.0f + row * 35.0f, 749.0f, 34.0f};
        if (selectedPreset == index) {
            g.setColour(lime.withAlpha(0.18f));
            g.fillRoundedRectangle(area, 5.0f);
        }
        drawText(g, presetItems[static_cast<size_t>(index)].name,
                 area.reduced(15.0f, 0.0f), 22.0f,
                 selectedPreset == index ? lime : screenText,
                 juce::Justification::centredLeft);
        if (favorites.contains(presetItems[static_cast<size_t>(index)].name))
            drawFavoriteStar(g, {899.0f, area.getCentreY()}, 11.0f, true, lime);
    }
    g.setColour(lime.withAlpha(0.7f));
    g.drawHorizontalLine(442, 185.0f, 932.0f);
    drawText(g, juce::String(presetItems.size()) + " presets", {193, 446, 230, 34},
             18.0f, muted, juce::Justification::centredLeft);
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
    drawText(g, "OUTPUT", {476, 1013, 132, 34}, 25.0f, screenText);
    if (isOn("subSafe")) {
        g.setColour(lime.withAlpha(0.62f));
        g.fillEllipse(191.0f, 1054.0f, 37.0f, 37.0f);
    } else {
        g.setColour(juce::Colour(0xff061e18).withAlpha(0.55f));
        g.fillEllipse(188.0f, 1052.0f, 43.0f, 43.0f);
    }

    for (int channel = 0; channel < 2; ++channel) {
        const float y = channel == 0 ? 1025.0f : 1065.0f;
        drawText(g, channel == 0 ? "L" : "R", {640.0f, y - 3.0f, 45.0f, 31.0f},
                 25.0f, screenText);
        const float peak = meterPeaks[static_cast<size_t>(channel)];
        for (int segment = 0; segment < 14; ++segment) {
            const bool lit = peak >= (segment + 1) / 14.0f;
            juce::Colour colour = segment < 7 ? lime :
                                  segment < 12 ? juce::Colour(0xff79f1e7) :
                                                 juce::Colour(0xffff8f67);
            g.setColour(lit ? colour : juce::Colour(0xff104d37).withAlpha(0.7f));
            g.fillRect(juce::Rectangle<float>(691.0f + segment * 18.0f, y,
                                               14.0f, 22.0f));
        }
    }

    // The replacement surface stays inside the shell, preserving its transparent exterior.
    const juce::Rectangle<float> footer {151, 1119, 773, 123};
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff8dde80), 151, 1119,
                                          juce::Colour(0xff46ae60), 924, 1242, false));
    g.fillRoundedRectangle(footer, 22.0f);
    g.setColour(juce::Colour(0xffb9f69e).withAlpha(0.65f));
    g.drawRoundedRectangle(footer.reduced(2.0f), 20.0f, 1.5f);
    g.setColour(ink.withAlpha(0.26f));
    g.drawVerticalLine(354, 1136.0f, 1227.0f);
    g.drawVerticalLine(709, 1136.0f, 1227.0f);

    const bool motionOn = isOn("motionEnable");
    drawText(g, "MOTION", {177, 1126, 168, 30}, 23.0f, ink);
    g.setColour(motionOn ? juce::Colour(0xff134c29) : juce::Colour(0xff235442));
    g.fillRoundedRectangle(motionSwitchBounds, 29.0f);
    g.setColour(motionOn ? lime : mint.withAlpha(0.8f));
    g.drawRoundedRectangle(motionSwitchBounds.reduced(1.0f), 28.0f, 2.0f);
    const float thumbX = motionOn ? motionSwitchBounds.getRight() - 53.0f
                                  : motionSwitchBounds.getX() + 5.0f;
    g.setGradientFill(juce::ColourGradient(motionOn ? lime.brighter(0.2f) : mint,
                                          thumbX, 1165, motionOn ? lime : juce::Colour(0xff97c5a2),
                                          thumbX, 1211, false));
    g.fillEllipse(thumbX, 1165.0f, 48.0f, 48.0f);
    drawText(g, motionOn ? "ON" : "OFF",
             motionOn ? juce::Rectangle<float>(190, 1171, 88, 34)
                      : juce::Rectangle<float>(238, 1171, 92, 34),
             25.0f, motionOn ? lime : mint);
    if (auto* s = controlSwitch("motionEnable")) {
        if (s->button->hasKeyboardFocus(true)) {
            g.setColour(ink);
            g.drawRoundedRectangle(motionSwitchBounds.expanded(4), 32.0f, 2.0f);
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
    int lockCount = 0;
    for (const auto& id : ShiftyProcessor::randomizableParameterIds())
        if (plugin.isParameterLocked(id)) ++lockCount;
    drawText(g, "NEW VARIATION", {362, 1217, 178, 21}, 15.5f, ink.withAlpha(0.82f));
    drawText(g, juce::String(lockCount) + " LOCKED", {550, 1217, 150, 21},
             16.0f, ink.withAlpha(0.82f));
    drawAction(*undoButton, undoBounds, "UNDO", 21.0f, false, plugin.canUndo());
    drawAction(*redoButton, redoBounds, "REDO", 21.0f, false, plugin.canRedo());

    const bool bypassOn = isOn("bypass");
    g.setColour(bypassOn ? ink : ink.withAlpha(0.12f));
    g.fillRoundedRectangle(bypassBounds, 17.0f);
    g.setColour(ink.withAlpha(0.55f));
    g.drawRoundedRectangle(bypassBounds, 17.0f, 1.5f);
    drawText(g, bypassOn ? "BYPASS ON" : "BYPASS OFF", bypassBounds, 20.0f,
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
    updateValueLabels();
    if (page == Page::motion)
        if (auto* freeRate = control("motionFreeHz"))
            if (freeRate->slider->isVisible() == isOn("motionSync")) updatePage();
    repaint();
}

void ShiftyEditor::refreshPresetList() {
    presetItems.clear();
    const auto query = presetSearch.getText().trim();
    const auto factory = ShiftyProcessor::factoryPresetNames();
    if (presetFilter != PresetFilter::user) {
        for (int i = 0; i < factory.size(); ++i) {
            const auto name = factory[i];
            if ((presetFilter == PresetFilter::favorites && !favorites.contains(name)) ||
                (!query.isEmpty() && !name.containsIgnoreCase(query))) continue;
            presetItems.push_back({name, i, {}});
        }
    }
    if (presetFilter != PresetFilter::factory) {
        const auto files = plugin.userPresetFiles();
        for (const auto& file : files) {
            const auto name = file.getFileNameWithoutExtension();
            if ((presetFilter == PresetFilter::favorites && !favorites.contains(name)) ||
                (!query.isEmpty() && !name.containsIgnoreCase(query))) continue;
            presetItems.push_back({name, -1, file});
        }
    }
    selectedPreset = presetItems.empty() ? -1 : 0;
    firstPreset = 0;
}

void ShiftyEditor::loadSelectedPreset() {
    if (selectedPreset < 0 || selectedPreset >= static_cast<int>(presetItems.size())) return;
    const auto& item = presetItems[static_cast<size_t>(selectedPreset)];
    if (item.factoryIndex >= 0) plugin.loadFactoryPreset(item.factoryIndex);
    else plugin.loadUserPreset(item.file);
    updateValueLabels();
    repaint();
}

void ShiftyEditor::beginPresetSave() {
    savingPreset = true;
    presetNameEditor.setText(plugin.currentPresetName(), juce::dontSendNotification);
    updatePage();
    presetNameEditor.grabKeyboardFocus();
    presetNameEditor.selectAll();
    repaint();
}

void ShiftyEditor::finishPresetSave() {
    const auto name = presetNameEditor.getText().trim();
    if (name.isEmpty()) return;
    plugin.saveUserPreset(name);
    savingPreset = false;
    presetFilter = PresetFilter::user;
    refreshPresetList();
    for (int i = 0; i < static_cast<int>(presetItems.size()); ++i)
        if (presetItems[static_cast<size_t>(i)].name == name) {
            selectedPreset = i;
            firstPreset = std::max(0, i - 3);
            break;
        }
    updatePage();
    repaint();
}

void ShiftyEditor::showShapeMenu() {
    juce::PopupMenu menu;
    const char* names[] = {"Sine", "Triangle", "Ramp Up", "Ramp Down"};
    for (int i = 0; i < 4; ++i)
        menu.addItem(i + 1, names[i], true, juce::roundToInt(valueOf("motionShape")) == i);
    menu.addSeparator();
    menu.addItem(100, "Lock for randomization", true, plugin.isParameterLocked("motionShape"));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
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
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [safe = juce::Component::SafePointer<ShiftyEditor>(this)](int result) {
            if (safe == nullptr) return;
            if (result == 100) safe->plugin.setParameterLocked("motionRate",
                !safe->plugin.isParameterLocked("motionRate"));
            else if (result > 0) safe->setParameter("motionRate", result - 1.0f);
            safe->repaint();
        });
}

void ShiftyEditor::showParameterLockMenu(const juce::String& id, juce::Component* target) {
    juce::PopupMenu menu;
    if (auto* parameter = plugin.parameters().getParameter(id))
        menu.addSectionHeader(parameter->getName(80));
    menu.addItem(1, "Lock for randomization", true, plugin.isParameterLocked(id));
    menu.addItem(2, "All randomization locks...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
        [safe = juce::Component::SafePointer<ShiftyEditor>(this), id](int result) {
            if (safe == nullptr) return;
            if (result == 1) safe->plugin.setParameterLocked(id, !safe->plugin.isParameterLocked(id));
            else if (result == 2) safe->showLocksMenu();
            safe->repaint();
        });
}

void ShiftyEditor::showLocksMenu() {
    juce::PopupMenu menu;
    menu.addSectionHeader("Keep these values when using RANDOM");
    const auto ids = ShiftyProcessor::randomizableParameterIds();
    for (int i = 0; i < ids.size(); ++i)
        if (auto* parameter = plugin.parameters().getParameter(ids[i]))
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
    if (p.y >= 115.0f && p.y <= 161.0f) {
        if (p.x >= 594.0f && p.x < 701.0f) page = Page::sound;
        else if (p.x >= 701.0f && p.x < 808.0f) page = Page::motion;
        else if (p.x >= 808.0f && p.x < 916.0f) page = Page::presets;
        updatePage();
        repaint();
        return;
    }
    if (page == Page::sound && p.x >= 445.0f && p.x <= 500.0f &&
        p.y >= 201.0f && p.y <= 250.0f) {
        const auto name = plugin.currentPresetName();
        if (favorites.contains(name)) favorites.removeString(name);
        else favorites.add(name);
        if (favoritesFile != nullptr) {
            favoritesFile->setValue("favorites", favorites.joinIntoString("\n"));
            favoritesFile->saveIfNeeded();
        }
        repaint();
        return;
    }
    if (page == Page::motion && p.y >= 416.0f && p.y <= 471.0f) {
        if (p.x >= 170.0f && p.x < 360.0f) showShapeMenu();
        else if (p.x >= 360.0f && p.x < 552.0f && isOn("motionSync")) showRateMenu();
        return;
    }
    if (page == Page::presets && !savingPreset) {
        if (p.y >= 251.0f && p.y < 294.0f && p.x >= 185.0f && p.x < 578.0f) {
            presetFilter = p.x < 333.0f ? PresetFilter::factory :
                           p.x < 460.0f ? PresetFilter::user : PresetFilter::favorites;
            refreshPresetList();
            repaint();
        } else if (p.y >= 302.0f && p.y < 442.0f && p.x >= 183.0f && p.x < 933.0f) {
            const int index = firstPreset + static_cast<int>((p.y - 302.0f) / 35.0f);
            if (index >= 0 && index < static_cast<int>(presetItems.size())) {
                selectedPreset = index;
                repaint();
            }
        } else if (p.y >= 443.0f && p.y < 479.0f && p.x >= 792.0f && p.x < 932.0f) {
            beginPresetSave();
        }
    }
}

void ShiftyEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    if (page != Page::presets) return;
    const auto p = designPoint(e.position);
    if (p.y >= 302.0f && p.y < 442.0f && p.x >= 183.0f && p.x < 933.0f) {
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
