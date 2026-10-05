#include "ThrowUpEditor.h"
#include "Presets.h"
#include "BinaryData.h"
#include "VfdInfo.h"
#include <algorithm>
#include <cmath>

namespace {
// Artwork coordinates are kept separate from the editor size so controls remain
// registered with the physical caps at every host-supported zoom level.
constexpr int shellX = 34, shellY = 18, shellWidth = 1058, shellHeight = 1360;
constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
constexpr float endAngle = juce::MathConstants<float>::pi * 2.75f;
const juce::Colour cream { 0xffffeed3 }, amber { 0xffffca70 }, orange { 0xffff9d43 };
const juce::Colour muted { 0xffd6b28f }, panel { 0xff291209 }, ink { 0xff55240d };
const juce::Rectangle<float> bypassBounds { 186, 1113, 178, 56 };

juce::String valueText (const juce::String& id, double value)
{
    if (id == "decay") return juce::String (value, 2) + " s";
    if (id == "predelay") return juce::String (value, 1) + " ms";
    if (id == "lowCut" || id == "highCut")
        return value >= 1000.0 ? juce::String (value / 1000.0, 2) + " kHz"
                               : juce::String (juce::roundToInt (value)) + " Hz";
    if (id == "output") return juce::String (value, 1) + " dB";
    return juce::String (juce::roundToInt (value)) + "%";
}

juce::Image loadShell()
{
    int size = 0;
    if (const auto* data = BinaryData::getNamedResource ("DeviceShellv2_png", size))
        return juce::ImageCache::getFromMemory (data, size);
    return {};
}
}

class ThrowUpEditor::Skin final : public juce::LookAndFeel_V4
{
public:
    Skin (juce::Typeface::Ptr face, juce::Typeface::Ptr displayFace)
        : typeface (face), displayTypeface (displayFace)
    {
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, cream);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, orange);
        setColour (juce::PopupMenu::highlightedTextColourId, ink);
        setColour (juce::TooltipWindow::backgroundColourId, cream);
        setColour (juce::TooltipWindow::textColourId, ink);
        setColour (juce::TooltipWindow::outlineColourId, amber);
        setColour (juce::TextEditor::backgroundColourId, panel);
        setColour (juce::TextEditor::textColourId, cream);
        setColour (juce::TextEditor::highlightColourId, amber.withAlpha (0.4f));
        setColour (juce::TextEditor::focusedOutlineColourId, amber);
    }
    juce::Font font (float height, bool display = false) const
    {
        return juce::Font (juce::FontOptions (display ? displayTypeface : typeface).withHeight (height));
    }
    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return font ((float) box.getHeight() * .68f, true);
    }
    juce::Font getPopupMenuFont() override { return font (17.0f); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        const int margin = juce::roundToInt ((float) box.getHeight() * .1f);
        label.setBounds (margin, 0, box.getWidth() - box.getHeight(), box.getHeight());
        label.setFont (getComboBoxFont (box));
    }
    void drawComboBox (juce::Graphics& g, int width, int height, bool down,
                       int, int, int, int, juce::ComboBox& box) override
    {
        const float h = (float) height;
        if (down || box.hasKeyboardFocus (true)) {
            g.setColour (amber.withAlpha (.2f));
            g.fillRoundedRectangle (box.getLocalBounds().toFloat(), h * .15f);
        }
        const float x = (float) width - h * .46f;
        juce::Path p;
        p.startNewSubPath (x - h * .13f, h * .44f);
        p.lineTo (x, h * .59f);
        p.lineTo (x + h * .13f, h * .44f);
        g.setColour (amber);
        g.strokePath (p, juce::PathStrokeType (juce::jmax (1.0f, h * .035f),
                                             juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool over, bool down) override
    {
        const auto r = button.getLocalBounds().toFloat().reduced (.7f);
        if (button.getName() == "Bypass") {
            const bool on = button.getToggleState();
            g.setColour (on ? juce::Colour (0xff783914) : juce::Colour (0xff582b17));
            g.fillRoundedRectangle (r, r.getHeight() * .5f);
            g.setColour (cream.withAlpha (over || down || button.hasKeyboardFocus (true) ? 1.0f : .55f));
            g.drawRoundedRectangle (r, r.getHeight() * .5f, 1.0f);
            const float diameter = r.getHeight() - 7.0f;
            const float x = on ? r.getRight() - diameter - 3.5f : r.getX() + 3.5f;
            g.setGradientFill ({ cream, x, r.getY(), juce::Colour (0xffd8b98e), x, r.getBottom(), false });
            g.fillEllipse (x, r.getY() + 3.5f, diameter, diameter);
            return;
        }
        g.setColour (down ? orange.withAlpha (.28f) : panel.withAlpha (over ? .8f : .45f));
        g.fillRoundedRectangle (r, r.getHeight() * .2f);
        g.setColour (amber.withAlpha (over || button.hasKeyboardFocus (true) ? .8f : .2f));
        g.drawRoundedRectangle (r, r.getHeight() * .2f, 1.0f);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        auto r = button.getLocalBounds().toFloat();
        const bool isBypass = button.getName() == "Bypass";
        if (isBypass) {
            const float thumbWidth = r.getHeight();
            if (button.getToggleState()) r.removeFromRight (thumbWidth);
            else r.removeFromLeft (thumbWidth);
        }
        g.setFont (font ((float) button.getHeight() * (isBypass ? .43f : .78f)));
        g.setColour (cream);
        g.drawText (button.getButtonText(), r, juce::Justification::centred);
    }
private:
    juce::Typeface::Ptr typeface, displayTypeface;
};

struct ThrowUpEditor::Control
{
    juce::String id, title;
    juce::Rectangle<float> hit, readout;
    bool physical = false, large = false;
    struct FineSlider final : juce::Slider {
        int normalSensitivity = 180;
        void paint (juce::Graphics&) override {}
        void mouseDown (const juce::MouseEvent& event) override {
            setMouseDragSensitivity (normalSensitivity * (event.mods.isShiftDown() ? 5 : 1));
            juce::Slider::mouseDown (event);
        }
        void focusGained (juce::Component::FocusChangeType) override {
            if (auto* parent = getParentComponent()) parent->repaint();
        }
        void focusLost (juce::Component::FocusChangeType) override {
            if (auto* parent = getParentComponent()) parent->repaint();
        }
    } slider;
    struct ValueLabel final : juce::Label {
        explicit ValueLabel (juce::Slider& s) : slider (s) {}
        void mouseDown (const juce::MouseEvent& event) override {
            forwarding = event.getNumberOfClicks() == 1 && ! event.mods.isPopupMenu();
            if (forwarding) slider.mouseDown (event.getEventRelativeTo (&slider));
            else juce::Label::mouseDown (event);
        }
        void mouseDrag (const juce::MouseEvent& event) override {
            if (forwarding) slider.mouseDrag (event.getEventRelativeTo (&slider));
            else juce::Label::mouseDrag (event);
        }
        void mouseUp (const juce::MouseEvent& event) override {
            if (forwarding) slider.mouseUp (event.getEventRelativeTo (&slider));
            else juce::Label::mouseUp (event);
            forwarding = false;
        }
        juce::Slider& slider;
        bool forwarding = false;
    } value { slider };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

ThrowUpEditor::ThrowUpEditor (ThrowUpProcessor& p)
    : AudioProcessorEditor (p), pluginProcessor (p), shell (loadShell()),
      normalTypeface (juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchSemiBold_ttf, BinaryData::ChakraPetchSemiBold_ttfSize)),
      titleTypeface (juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchBoldItalic_ttf, BinaryData::ChakraPetchBoldItalic_ttfSize)),
      skin (std::make_unique<Skin> (normalTypeface, titleTypeface))
{
    setLookAndFeel (skin.get());
    setOpaque (false);
    addControl ("mix", "THROW", "Adds pure reverb over your original bass. Dry stays at full level. 0% adds no reverb; 100% adds the full wet return. Automate THROW in your DAW.",
                { 400, 616, 320, 320 }, { 445, 947, 230, 51 }, true, true);
    addControl ("size", "SIZE", "Changes the dimensions and spacing of the reverb room.",
                { 214, 567, 140, 140 }, { 194, 731, 180, 42 }, true);
    addControl ("decay", "DECAY", "Sets how long the reverb tail decays, in seconds.",
                { 770, 567, 140, 140 }, { 750, 731, 180, 42 }, true);
    addControl ("predelay", "PRE-DELAY", "Adds space between the dry hit and the reverb, in milliseconds.",
                { 214, 846, 140, 140 }, { 194, 993, 180, 33 }, true);
    addControl ("diffusion", "DIFFUSION", "From scattered reflections to a dense, smooth tail.",
                { 769, 846, 140, 140 }, { 749, 993, 180, 33 }, true);
    addControl ("lowCut", "LOW CUT", "High-pass filter for the reverb only. Raise it to keep the sub range clean.",
                { 188, 274, 222, 69 }, { 278, 274, 132, 34 });
    addControl ("highCut", "HIGH CUT", "Low-pass filter for the reverb only. Lower it to darken the tail.",
                { 448, 274, 222, 69 }, { 538, 274, 132, 34 });
    addControl ("width", "WIDTH", "Stereo spread of the wet tail. 0% is mono, 100% is natural, 150% is extra wide.",
                { 708, 274, 222, 69 }, { 798, 274, 132, 34 });
    addControl ("grit", "GRIT", "Adds saturation and roughness to the reverb path.",
                { 448, 362, 222, 69 }, { 538, 362, 132, 34 });
    addControl ("motion", "MOTION", "Adds gentle pitch movement inside the reverb tail. Throw timing stays under your DAW automation.",
                { 188, 362, 222, 69 }, { 278, 362, 132, 34 });
    addControl ("duck", "DUCK", "Pulls the wet tail down while the input plays, then lets it bloom into the gaps.",
                { 708, 362, 222, 69 }, { 798, 362, 132, 34 });
    addControl ("output", "OUTPUT", "Sets final output level in dB.",
                { 395, 1112, 150, 58 }, { 395, 1112, 150, 58 });

    const auto& bank = throwup::factoryPresets();
    for (int group = 0; group < 5; ++group) {
        juce::PopupMenu submenu;
        for (int item = 0; item < 10; ++item) {
            const int index = group * 10 + item;
            submenu.addItem (index + 1, bank[(size_t) index].name);
        }
        presets.getRootMenu()->addSubMenu (bank[(size_t) group * 10].group, submenu);
    }
    presets.setName ("Factory presets");
    presets.setColour (juce::ComboBox::textColourId, cream);
    presets.setTooltip ("50 factory presets: 10 each in Small, Medium, Large, Huge, and Creative. Loading a preset preserves THROW, Output, and Bypass.");
    presets.onChange = [this] {
        if (presets.getSelectedId() > 0) pluginProcessor.loadFactoryPreset (presets.getSelectedId() - 1);
        updatePresetDisplay(); repaint();
    };
    addAndMakeVisible (presets);
    previous.setName ("Previous preset"); next.setName ("Next preset"); bypass.setName ("Bypass");
    previous.setTooltip ("Previous factory preset. THROW and Output stay where you set them.");
    next.setTooltip ("Next factory preset. THROW and Output stay where you set them.");
    previous.onClick = [this] { stepPreset (-1); };
    next.onClick = [this] { stepPreset (1); };
    bypass.setClickingTogglesState (true);
    bypass.setTooltip ("Bypass the effect. The dry input passes through unchanged.");
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (pluginProcessor.apvts, "bypass", bypass);
    for (auto* button : { &previous, &next, &bypass }) {
        button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible (*button);
    }
    updatePresetDisplay();
    bypass.setButtonText (bypass.getToggleState() ? "ON" : "OFF");
    const auto infoTheme = vfd::themeFor ("THROW-UP");
    infoButton = std::make_unique<vfd::InfoButton> (infoTheme, normalTypeface);
    infoButton->setName ("INFO");
    infoOverlay = std::make_unique<vfd::InfoOverlay> ("THROW-UP", infoTheme,
        BinaryData::VfdLogo_svg, BinaryData::VfdLogo_svgSize, normalTypeface);
    infoButton->onClick = [this] { infoOverlay->open (infoButton.get()); };
    addAndMakeVisible (*infoButton);
    addChildComponent (*infoOverlay);
    setResizable (true, false);
    setResizeLimits (528, 679, 990, 1273);
    if (auto* constrainer = getConstrainer()) constrainer->setFixedAspectRatio ((double) shellWidth / shellHeight);
    setSize (660, juce::roundToInt (660.0 * shellHeight / shellWidth));
    startTimerHz (24);
}

ThrowUpEditor::~ThrowUpEditor()
{
    stopTimer();
    bypassAttachment.reset();
    controls.clear();
    setLookAndFeel (nullptr);
}

void ThrowUpEditor::addControl (const char* id, const char* title, const char* tooltip,
                                juce::Rectangle<float> hit, juce::Rectangle<float> readout,
                                bool physical, bool large)
{
    auto c = std::make_unique<Control>();
    c->id = id; c->title = title; c->physical = physical; c->large = large;
    c->hit = hit; c->readout = readout;
    c->slider.setName (title);
    c->slider.setSliderStyle (physical ? juce::Slider::RotaryVerticalDrag : juce::Slider::RotaryHorizontalVerticalDrag);
    c->slider.setRotaryParameters (startAngle, endAngle, true);
    c->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    c->slider.normalSensitivity = large ? 360 : 180;
    c->slider.setMouseDragSensitivity (c->slider.normalSensitivity);
    c->slider.setVelocityModeParameters (1.0, 1, 0.0, false);
    c->slider.setScrollWheelEnabled (false);
    c->slider.setTooltip (juce::String (tooltip) + " Hold Shift for fine adjustment. Double-click the control to reset; double-click its value to type a number.");
    c->slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    c->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (pluginProcessor.apvts, id, c->slider);
    if (auto* parameter = pluginProcessor.apvts.getParameter (id))
        c->slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    c->value.setName (juce::String (title) + " value");
    c->value.setJustificationType (physical || c->id == "output" ? juce::Justification::centred : juce::Justification::centredRight);
    c->value.setBorderSize ({ 0, 0, 0, 0 });
    c->value.setColour (juce::Label::textColourId, physical || c->id == "output" ? ink : cream);
    c->value.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    c->value.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    c->value.setColour (juce::Label::textWhenEditingColourId, cream);
    c->value.setColour (juce::Label::backgroundWhenEditingColourId, panel);
    c->value.setColour (juce::Label::outlineWhenEditingColourId, amber);
    c->value.setEditable (false, true, false);
    c->value.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    c->value.setTooltip ("Drag to adjust. Hold Shift for fine adjustment. Double-click to enter a value.");
    c->value.setText (valueText (id, c->slider.getValue()), juce::dontSendNotification);
    auto* control = c.get();
    c->slider.onValueChange = [this, control] {
        if (! control->value.isBeingEdited())
            control->value.setText (valueText (control->id, control->slider.getValue()), juce::dontSendNotification);
        repaint();
    };
    c->value.onEditorShow = [control] {
        if (auto* editor = control->value.getCurrentTextEditor()) {
            editor->setText (juce::String (control->slider.getValue(), control->id == "decay" ? 2 : 1), false);
            editor->selectAll();
        }
    };
    c->value.onTextChange = [this, control] {
        const auto typed = control->value.getText().trim();
        double v = typed.getDoubleValue();
        if ((control->id == "lowCut" || control->id == "highCut") && typed.containsIgnoreCase ("k")) v *= 1000.0;
        if (std::isfinite (v) && typed.containsAnyOf ("0123456789")) {
            auto* parameter = pluginProcessor.apvts.getParameter (control->id);
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) juce::jlimit (control->slider.getMinimum(), control->slider.getMaximum(), v)));
            parameter->endChangeGesture();
        }
        control->value.setText (valueText (control->id, control->slider.getValue()), juce::dontSendNotification);
    };
    addAndMakeVisible (c->slider); addAndMakeVisible (c->value);
    controls.push_back (std::move (c));
}

juce::Rectangle<int> ThrowUpEditor::scaled (juce::Rectangle<float> r) const
{
    const float sx = (float) getWidth() / (float) shellWidth;
    const float sy = (float) getHeight() / (float) shellHeight;
    return { juce::roundToInt ((r.getX() - (float) shellX) * sx),
             juce::roundToInt ((r.getY() - (float) shellY) * sy),
             juce::roundToInt (r.getWidth() * sx), juce::roundToInt (r.getHeight() * sy) };
}

void ThrowUpEditor::text (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r,
                          float height, juce::Colour colour, juce::Justification align, bool italic)
{
    g.setColour (colour);
    g.setFont (juce::Font (juce::FontOptions (italic ? titleTypeface : normalTypeface).withHeight (height)));
    g.drawText (s, r, align, false);
}

void ThrowUpEditor::drawScreen (juce::Graphics& g)
{
    const auto& preset = throwup::factoryPresets()[(size_t) juce::jlimit (0, 49, pluginProcessor.getCurrentProgram())];
    text (g, juce::String (preset.group).toUpperCase() + " / FACTORY", { 190, 175, 540, 25 }, 18, muted);
    text (g, modified ? "EDITED" : juce::String (pluginProcessor.getCurrentProgram() + 1).paddedLeft ('0', 2) + " / 50",
          { 765, 175, 165, 25 }, 18, amber, juce::Justification::centredRight);
    g.setColour (amber.withAlpha (.22f));
    g.drawHorizontalLine (256, 188.0f, 930.0f);
    for (const auto& c : controls) {
        if (c->physical || c->id == "output") continue;
        const auto r = c->hit;
        text (g, c->title, { r.getX(), r.getY(), 116, 34 }, 20.0f, muted);
        const float fraction = (float) c->slider.valueToProportionOfLength (c->slider.getValue());
        const float x = r.getX(), y = r.getY() + 50.0f, width = r.getWidth();
        g.setColour (cream.withAlpha (.13f));
        g.fillRoundedRectangle (x, y, width, 5.0f, 2.5f);
        g.setColour (amber.withAlpha (.87f));
        g.fillRoundedRectangle (x, y, juce::jmax (3.0f, width * fraction), 5.0f, 2.5f);
        g.setColour (cream);
        g.fillEllipse (x + width * fraction - 4.5f, y - 2.0f, 9.0f, 9.0f);
        if (c->slider.hasKeyboardFocus (true)) {
            g.setColour (amber.withAlpha (.85f));
            g.drawRoundedRectangle (r.expanded (5.0f), 5.0f, 1.5f);
        }
    }
    text (g, preset.description, { 190, 422, 740, 21 }, 16, muted.withAlpha (.85f));
}

void ThrowUpEditor::drawKnob (juce::Graphics& g, Control& c)
{
    const auto centre = c.hit.getCentre();
    const float arcRadius = c.large ? 164.0f : 89.0f;
    const float pointerRadius = c.large ? 94.0f : 41.0f;
    const float fraction = (float) c.slider.valueToProportionOfLength (c.slider.getValue());
    const float angle = startAngle + fraction * (endAngle - startAngle);
    if (fraction > .001f) {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0, startAngle, angle, true);
        g.setColour (amber);
        g.strokePath (arc, juce::PathStrokeType (c.large ? 7.0f : 4.0f,
                                               juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const float ux = std::sin (angle), uy = -std::cos (angle);
    const float x1 = centre.x + ux * pointerRadius * .25f;
    const float y1 = centre.y + uy * pointerRadius * .25f;
    const float x2 = centre.x + ux * pointerRadius;
    const float y2 = centre.y + uy * pointerRadius;
    const float thickness = c.large ? 10.5f : 6.0f;
    g.setColour (ink);
    g.drawLine (x1, y1, x2, y2, thickness);
    g.fillEllipse (x1 - thickness * .5f, y1 - thickness * .5f, thickness, thickness);
    g.fillEllipse (x2 - thickness * .5f, y2 - thickness * .5f, thickness, thickness);
    if (c.slider.hasKeyboardFocus (true)) {
        g.setColour (ink.withAlpha (.75f));
        g.drawEllipse (centre.x - arcRadius - 8.0f, centre.y - arcRadius - 8.0f,
                       (arcRadius + 8.0f) * 2.0f, (arcRadius + 8.0f) * 2.0f, 2.0f);
    }
}

void ThrowUpEditor::drawMeters (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const auto level = [] (float value) {
        return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (value, -60.0f) + 60.0f) / 60.0f);
    };
    for (int row = 0; row < 2; ++row) {
        const float y = bounds.getY() + (float) row * 30.0f;
        text (g, row == 0 ? "IN" : "OUT", { bounds.getX(), y - 3.0f, 43, 25 }, 18, cream);
        const float magnitude = row == 0 ? inputMeter : outputMeter;
        const int lit = juce::roundToInt (level (magnitude) * 14.0f);
        for (int i = 0; i < 14; ++i) {
            g.setColour (i < lit ? (i >= 12 ? juce::Colour (0xffff7850) : amber) : cream.withAlpha (.10f));
            g.fillRect (bounds.getX() + 48.0f + (float) i * 13.8f, y, 10.0f, 18.0f);
        }
    }
}

void ThrowUpEditor::drawFooter (juce::Graphics& g)
{
    text (g, "BYPASS", { 186, 1075, 178, 31 }, 22, ink, juce::Justification::centred);
    text (g, "OUTPUT", { 395, 1075, 150, 31 }, 22, ink, juce::Justification::centred);
    g.setColour (ink.withAlpha (.27f));
    g.drawVerticalLine (380, 1079.0f, 1170.0f);
    drawMeters (g, { 618, 1101, 285, 55 });
}

void ThrowUpEditor::paint (juce::Graphics& g)
{
    if (scaledShell.isValid()) g.drawImageAt (scaledShell, 0, 0);
    juce::Graphics::ScopedSaveState state (g);
    const float sx = (float) getWidth() / (float) shellWidth;
    const float sy = (float) getHeight() / (float) shellHeight;
    g.addTransform (juce::AffineTransform (sx, 0, -(float) shellX * sx, 0, sy, -(float) shellY * sy));
    drawScreen (g);
    text (g, "MID-BASS REVERB", { 602, 82, 302, 38 }, 23, cream, juce::Justification::centred);
    text (g, "THROW", { 440, 554, 240, 45 }, 38, ink, juce::Justification::centred);
    text (g, "SIZE", { 194, 508, 180, 37 }, 27, ink, juce::Justification::centred);
    text (g, "DECAY", { 750, 508, 180, 37 }, 27, ink, juce::Justification::centred);
    text (g, "PRE-DELAY", { 187, 785, 194, 36 }, 27, ink, juce::Justification::centred);
    text (g, "DIFFUSION", { 742, 785, 194, 36 }, 27, ink, juce::Justification::centred);
    for (const auto& c : controls)
        if (c->physical) drawKnob (g, *c);
    text (g, "OFF", { 389, 909, 100, 30 }, 21, ink.withAlpha (.85f), juce::Justification::centred);
    text (g, "FULL", { 631, 909, 100, 30 }, 21, ink.withAlpha (.85f), juce::Justification::centred);
    text (g, "ADDED REVERB", { 445, 995, 230, 27 }, 20, ink.withAlpha (.8f), juce::Justification::centred);
    drawFooter (g);
}

void ThrowUpEditor::resized()
{
    uiScale = (float) getWidth() / (float) shellWidth;
    if (shell.isValid() && getWidth() > 0 && getHeight() > 0 &&
        (scaledShell.getWidth() != getWidth() || scaledShell.getHeight() != getHeight())) {
        scaledShell = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
        juce::Graphics shellGraphics (scaledShell);
        shellGraphics.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        shellGraphics.drawImage (shell, 0, 0, getWidth(), getHeight(), shellX, shellY, shellWidth, shellHeight);
    }
    for (const auto& c : controls) {
        c->slider.setBounds (scaled (c->hit));
        c->value.setBounds (scaled (c->readout));
        const float fontSize = c->large ? 43.0f : c->physical || c->id == "output" ? 29.0f : 25.0f;
        c->value.setFont (skin->font (fontSize * uiScale));
    }
    presets.setBounds (scaled ({ 186, 204, 612, 44 }));
    previous.setBounds (scaled ({ 822, 204, 47, 44 }));
    next.setBounds (scaled ({ 882, 204, 47, 44 }));
    bypass.setBounds (scaled (bypassBounds));
    if (infoButton) infoButton->setBounds (juce::Rectangle<float> (282, 751, 96, 38)
        .transformedBy (juce::AffineTransform::scale (getWidth() / 660.0f, getHeight() / 848.0f)).toNearestInt());
    if (infoOverlay) infoOverlay->setBounds (getLocalBounds());
}

void ThrowUpEditor::stepPreset (int direction)
{
    pluginProcessor.loadFactoryPreset ((pluginProcessor.getCurrentProgram() + direction + 50) % 50);
    updatePresetDisplay(); repaint();
}

void ThrowUpEditor::updatePresetDisplay()
{
    const int current = juce::jlimit (0, 49, pluginProcessor.getCurrentProgram());
    if (visiblePreset != current) {
        visiblePreset = current;
        presets.setSelectedId (current + 1, juce::dontSendNotification);
    }
    const auto& s = throwup::factoryPresets()[(size_t) current].settings;
    const std::array<const char*, 10> ids { "size", "decay", "predelay", "diffusion", "lowCut", "highCut", "width", "grit", "motion", "duck" };
    const std::array<float, 10> values { s.size, s.decay, s.predelay, s.diffusion, s.lowCut, s.highCut, s.width, s.grit, s.motion, s.duck };
    modified = false;
    for (size_t i = 0; i < ids.size(); ++i)
        if (std::abs (pluginProcessor.apvts.getRawParameterValue (ids[i])->load() - values[i]) > .06f) modified = true;
}

void ThrowUpEditor::timerCallback()
{
    inputMeter = juce::jmax (pluginProcessor.inputLevel.load (std::memory_order_relaxed), inputMeter * .78f);
    outputMeter = juce::jmax (pluginProcessor.outputLevel.load (std::memory_order_relaxed), outputMeter * .78f);
    updatePresetDisplay();
    bypass.setButtonText (bypass.getToggleState() ? "ON" : "OFF");
    repaint();
}
