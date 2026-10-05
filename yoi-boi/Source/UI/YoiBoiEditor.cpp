#include "YoiBoiEditor.h"
#include "WindowActions.h"
#include "BinaryData.h"
#include "VfdInfo.h"
#include <array>
#include <cmath>

namespace yoiboi
{
namespace
{
constexpr int designWidth = 1080, artworkHeight = 720, designHeight = 764;
// The artwork uses the original mockup coordinates; the actual editor ends at
// the device shell so the old navy margin and resize grip are gone.
constexpr int shellX = 5, shellY = 18, shellWidth = 1070, shellHeight = 724;
constexpr int infoBandTop = 665, infoBandHeight = 44;
const juce::Colour lavender { 0xffb7a2e5 }, cream { 0xffeee8ff }, ink { 0xff10122e };
const juce::Colour cobalt { 0xff202b88 }, cyan { 0xff65e8f2 }, lime { 0xffd7f76c };
const juce::Colour midnight { 0xff071343 }, electricBlue { 0xff073fc5 }, shellPurple { 0xff7768c4 };
const juce::StringArray vowels { "EE", "EH", "AH", "OH", "OO" };

void drawDice(juce::Graphics& g, juce::Point<float> centre, float size, juce::Colour colour)
{
    const juce::Rectangle<float> face(centre.x - size * 0.5f, centre.y - size * 0.5f, size, size);
    g.setColour(midnight.withAlpha(0.16f));
    g.fillRoundedRectangle(face.translated(1.0f, 3.0f), size * 0.16f);
    g.setGradientFill(juce::ColourGradient(lime.brighter(0.5f), face.getTopLeft(),
                                          lime, face.getBottomRight(), false));
    g.fillRoundedRectangle(face, size * 0.16f);
    g.setColour(colour);
    g.drawRoundedRectangle(face.reduced(0.8f), size * 0.16f, 1.8f);
    g.setColour(midnight);
    const float offset = size * 0.235f, radius = size * 0.065f;
    for (const auto dot : std::array<juce::Point<float>, 5> {{{-offset, -offset}, {offset, -offset},
                                                            {0, 0}, {-offset, offset}, {offset, offset}}})
        g.fillEllipse(centre.x + dot.x - radius, centre.y + dot.y - radius, radius * 2.0f, radius * 2.0f);
}

juce::Font font(float size, bool bold = false, bool italic = false)
{
    juce::ignoreUnused(bold);
    static auto typeface = juce::Typeface::createSystemTypefaceFor(BinaryData::ChakraPetchSemiBold_ttf,
                                                                    BinaryData::ChakraPetchSemiBold_ttfSize);
    auto result = juce::Font(juce::FontOptions(typeface).withHeight(size));
    result.setItalic(italic);
    return result;
}

juce::Font displayFont(float size)
{
    static auto typeface = juce::Typeface::createSystemTypefaceFor(BinaryData::ChakraPetchBoldItalic_ttf,
                                                                    BinaryData::ChakraPetchBoldItalic_ttfSize);
    return juce::Font(juce::FontOptions(typeface).withHeight(size));
}

void text(juce::Graphics& g, const juce::String& value, juce::Rectangle<int> bounds,
          float size, juce::Colour colour, bool bold = false,
          juce::Justification alignment = juce::Justification::centredLeft)
{
    g.setFont(font(size, bold));
    g.setColour(colour);
    g.drawFittedText(value, bounds, alignment, 1);
}

class ParameterValueLabel final : public juce::Label
{
public:
    explicit ParameterValueLabel(juce::Slider& control) : slider(control) {}
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu() && needsForwarding()) slider.mouseDown(event.getEventRelativeTo(&slider));
        else juce::Label::mouseDown(event);
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu() && needsForwarding()) slider.mouseUp(event.getEventRelativeTo(&slider));
        else juce::Label::mouseUp(event);
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return createIgnoredAccessibilityHandler(*this);
    }
private:
    bool needsForwarding() const
    {
        return slider.getSliderStyle() != juce::Slider::LinearBar && slider.getSliderStyle() != juce::Slider::LinearBarVertical;
    }
    juce::Slider& slider;
};

class Theme final : public juce::LookAndFeel_V4
{
public:
    Theme()
    {
        setColour(juce::Slider::textBoxTextColourId, ink);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, cyan.withAlpha(0.45f));
        setColour(juce::ComboBox::backgroundColourId, cobalt.darker(0.45f));
        setColour(juce::ComboBox::textColourId, cream);
        setColour(juce::ComboBox::outlineColourId, cream.withAlpha(0.25f));
        setColour(juce::ComboBox::arrowColourId, cyan);
        setColour(juce::PopupMenu::backgroundColourId, ink);
        setColour(juce::PopupMenu::textColourId, cream);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, cobalt);
        setColour(juce::PopupMenu::highlightedTextColourId, cyan);
        setColour(juce::TextEditor::backgroundColourId, ink.withAlpha(0.7f));
        setColour(juce::TextEditor::textColourId, cream);
        setColour(juce::TextEditor::outlineColourId, cream.withAlpha(0.28f));
        setColour(juce::TextEditor::focusedOutlineColourId, cyan);
        setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::TooltipWindow::backgroundColourId, ink);
        setColour(juce::TooltipWindow::textColourId, cream);
        setColour(juce::TooltipWindow::outlineColourId, cyan.withAlpha(0.45f));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                              bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(2.0f);
        const auto dark = static_cast<bool>(b.getProperties().getWithDefault("dark", false));
        const auto role = b.getProperties().getWithDefault("role", juce::String()).toString();
        const auto active = b.getToggleState();
        if (role == "favorite" || role == "sync")
        {
            if (highlighted || b.hasKeyboardFocus(true))
            {
                g.setColour(cyan.withAlpha(0.22f));
                g.fillRoundedRectangle(r, 7);
            }
            return;
        }
        if (role == "utility")
        {
            g.setColour(midnight.withAlpha(down ? 0.30f : 0.17f));
            g.fillRoundedRectangle(r, 7.0f);
            g.setColour((highlighted || b.hasKeyboardFocus(true) ? cyan : cream).withAlpha(0.7f));
            g.drawRoundedRectangle(r, 7.0f, 0.9f);
            return;
        }
        if (role == "primary" || role == "mutate" || role == "history" || role == "preview")
        {
            const auto corner = role == "primary" ? 25.0f : 16.0f;
            if (down || active || (highlighted && b.isEnabled()))
            {
                g.setColour((down ? midnight : role == "preview" ? cyan : juce::Colours::white)
                                .withAlpha(down ? 0.12f : 0.17f));
                g.fillRoundedRectangle(r.reduced(5), corner);
            }
            if (b.hasKeyboardFocus(true))
            {
                g.setColour(role == "preview" ? cyan : midnight);
                g.drawRoundedRectangle(r.reduced(5), corner, 1.7f);
            }
            return; // The physical cap and its lighting are part of the shell artwork.
        }
        if (role == "tab" && ! active)
        {
            if (highlighted || b.hasKeyboardFocus(true))
            {
                g.setColour(cream.withAlpha(0.12f));
                g.fillRoundedRectangle(r, 9);
            }
            return;
        }
        auto top = dark ? cobalt.brighter(0.20f) : cream.brighter(role == "primary" ? 0.34f : 0.15f);
        auto bottom = dark ? midnight.brighter(0.25f) : role == "primary" ? cream.brighter(0.04f) : lavender.brighter(0.18f);
        if (active) { top = cream.brighter(0.12f); bottom = cyan.brighter(0.27f); }
        if (highlighted && b.isEnabled()) { top = top.brighter(0.12f); bottom = bottom.brighter(0.08f); }
        const float opacity = b.isEnabled() ? 1.0f : 0.45f;
        if (down) r.translate(0, 1);
        const float corner = role == "primary" ? 27.0f : 12.0f;
        g.setColour(midnight.withAlpha((role == "primary" ? 0.35f : 0.26f) * opacity));
        g.fillRoundedRectangle(r.translated(0, role == "primary" ? 6.0f : 4.0f), corner);
        g.setColour((dark ? cyan : cream).withAlpha((role == "primary" ? 0.52f : 0.23f) * opacity));
        g.fillRoundedRectangle(r.expanded(role == "primary" ? 3.0f : 1.0f), corner + 3.0f);
        g.setGradientFill(juce::ColourGradient(top.withMultipliedAlpha(opacity), r.getTopLeft(),
                                              bottom.withMultipliedAlpha(opacity), r.getBottomLeft(), false));
        g.fillRoundedRectangle(r, corner);
        if (role == "primary")
        {
            g.setGradientFill(juce::ColourGradient(cream.withAlpha(0.73f * opacity), r.getTopLeft(),
                                                  cream.withAlpha(0.0f), { r.getCentreX(), r.getCentreY() }, false));
            g.fillRoundedRectangle(r.reduced(4), corner - 4);
        }
        g.setColour(cream.withAlpha((dark ? 0.23f : 0.95f) * opacity));
        g.drawRoundedRectangle(r.reduced(1.0f), corner - 1.0f, role == "primary" ? 2.5f : 1.5f);
        g.setColour((dark ? cyan : cream).withAlpha(0.42f * opacity));
        g.drawRoundedRectangle(r.reduced(5), juce::jmax(5.0f, corner - 5.0f), 1.0f);
        if (b.hasKeyboardFocus(true))
        {
            g.setColour(dark ? cyan : ink);
            g.drawRoundedRectangle(r.reduced(3), corner - 3, 2);
        }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        const auto dark = static_cast<bool>(b.getProperties().getWithDefault("dark", false));
        const auto size = static_cast<float>(b.getProperties().getWithDefault("fontSize", 18.0f));
        const auto role = b.getProperties().getWithDefault("role", juce::String()).toString();
        g.setFont(static_cast<bool>(b.getProperties().getWithDefault("italic", false))
                      ? displayFont(size) : font(size, true));
        auto colour = dark && ! b.getToggleState() ? cream : midnight;
        g.setColour(colour.withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.5f));
        auto label = b.getLocalBounds().reduced(6).translated(0, down ? 1 : 0);
        if (role == "utility" && b.getButtonText() == "CLOSE")
        {
            const auto c = b.getLocalBounds().toFloat().getCentre();
            g.setColour(cream.withAlpha(b.isEnabled() ? 1.0f : 0.4f));
            g.drawLine(c.x - 3.5f, c.y - 3.5f, c.x + 3.5f, c.y + 3.5f, 1.6f);
            g.drawLine(c.x + 3.5f, c.y - 3.5f, c.x - 3.5f, c.y + 3.5f, 1.6f);
            return;
        }
        if (role == "favorite")
        {
            juce::Path star;
            star.addStar(b.getLocalBounds().toFloat().getCentre(), 5, 6.0f, 13.0f, 0.0f);
            g.setColour(b.getToggleState() ? lime : cyan);
            if (b.getToggleState()) g.fillPath(star);
            g.strokePath(star, juce::PathStrokeType(1.6f));
            return;
        }
        if (role == "sync")
        {
            const juce::Point<float> c { 12.0f, b.getHeight() * 0.5f };
            if (b.getToggleState())
            {
                for (int radius = 16; radius >= 7; --radius)
                {
                    g.setColour(lime.withAlpha(0.025f));
                    g.fillEllipse(c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
                }
                g.setGradientFill(juce::ColourGradient(juce::Colours::white, c.x - 3, c.y - 4,
                                                       lime, c.x + 5, c.y + 7, false));
                g.fillEllipse(c.x - 7.5f, c.y - 7.5f, 15, 15);
            }
            label.removeFromLeft(25);
            g.setColour(midnight);
            g.drawFittedText("SYNC", label, juce::Justification::centred, 1);
            return;
        }
        if (role == "primary")
        {
            drawDice(g, { b.getWidth() * 0.5f, 48.0f }, 43.0f, juce::Colour(0xff70bd13));
            label = { 8, b.getHeight() - 55, b.getWidth() - 16, 39 };
        }
        else if (role == "mutate")
        {
            juce::Path arrows;
            arrows.startNewSubPath(25, 22); arrows.lineTo(32, 22);
            arrows.cubicTo(41, 22, 41, 40, 50, 40); arrows.lineTo(57, 40);
            arrows.startNewSubPath(25, 40); arrows.lineTo(32, 40);
            arrows.cubicTo(41, 40, 41, 22, 50, 22); arrows.lineTo(57, 22);
            arrows.startNewSubPath(51, 16); arrows.lineTo(57, 22); arrows.lineTo(51, 28);
            arrows.startNewSubPath(51, 34); arrows.lineTo(57, 40); arrows.lineTo(51, 46);
            g.strokePath(arrows, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            label.removeFromLeft(47);
        }
        else if (role == "history")
        {
            const bool reverse = b.getButtonText() == "REDO";
            const float cx = b.getWidth() * 0.5f, cy = 18.0f;
            juce::Path arrow;
            arrow.startNewSubPath(cx + 12, cy + 12); arrow.lineTo(cx + 12, cy + 7);
            arrow.quadraticTo(cx + 12, cy, cx + 3, cy); arrow.lineTo(cx - 12, cy);
            arrow.lineTo(cx - 5, cy - 7);
            arrow.startNewSubPath(cx - 12, cy); arrow.lineTo(cx - 5, cy + 7);
            if (reverse) arrow.applyTransform(juce::AffineTransform(-1, 0, cx * 2, 0, 1, 0));
            g.strokePath(arrow, juce::PathStrokeType(2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            label = { 4, b.getHeight() - 28, b.getWidth() - 8, 23 };
        }
        else if (role == "preview")
        {
            const float x = 18.0f, y = b.getHeight() * 0.5f;
            juce::Path icon;
            if (b.getToggleState()) icon.addRoundedRectangle(x - 4, y - 5, 10, 10, 1.5f);
            else { icon.startNewSubPath(x - 4, y - 6); icon.lineTo(x + 6, y); icon.lineTo(x - 4, y + 6); icon.closeSubPath(); }
            g.fillPath(icon);
            label.removeFromLeft(19);
        }
        else if (role == "filter")
        {
            label = { 24, 0, b.getWidth() - 29, b.getHeight() };
            g.setColour(b.getToggleState() ? cobalt : cyan.withAlpha(0.85f));
            g.fillEllipse(9.0f, b.getHeight() * 0.5f - 2.5f, 5.0f, 5.0f);
        }
        else if (role == "recent") label = { 10, 0, b.getWidth() - 19, b.getHeight() };
        g.setColour(colour.withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText(b.getButtonText(), label,
                         role == "recent" ? juce::Justification::centredLeft : juce::Justification::centred, 1);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float value,
                          float start, float end, juce::Slider& slider) override
    {
        const juce::Point<float> centre { x + width * 0.5f, y + height * 0.5f };
        constexpr float radius = 54.5f;
        // The cap is in the photographic shell. Only its live ring and pointer are drawn here.
        for (int segment = 0; segment < 3; ++segment)
        {
            const float from = start + (end - start) * segment / 3.0f + 0.035f;
            const float to = start + (end - start) * (segment + 1) / 3.0f - 0.035f;
            juce::Path arc;
            arc.addCentredArc(centre.x, centre.y, radius, radius, 0, from, to, true);
            g.setColour(cobalt.withAlpha(0.58f));
            g.strokePath(arc, juce::PathStrokeType(5.5f));
            const float progressEnd = juce::jlimit(from, to, start + (end - start) * value);
            if (progressEnd > from)
            {
                juce::Path progress;
                progress.addCentredArc(centre.x, centre.y, radius, radius, 0, from, progressEnd, true);
                g.setColour(cobalt.withAlpha(0.55f));
                g.strokePath(progress, juce::PathStrokeType(5.5f));
            }
        }
        const auto angle = start + (end - start) * value;
        juce::Path pointer;
        pointer.startNewSubPath(centre.x + std::sin(angle) * 20, centre.y - std::cos(angle) * 20);
        pointer.lineTo(centre.x + std::sin(angle) * 36, centre.y - std::cos(angle) * 36);
        g.setColour(cream.withAlpha(0.7f));
        g.strokePath(pointer, juce::PathStrokeType(6.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                     juce::AffineTransform::translation(0, 1.2f));
        g.setColour(midnight);
        g.strokePath(pointer, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (slider.hasKeyboardFocus(true))
        {
            g.setColour(cyan);
            g.drawEllipse(centre.x - 48, centre.y - 48, 96, 96, 1.8f);
        }
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                          juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        if (static_cast<bool>(slider.getProperties().getWithDefault("valueOnly", false))) return;
        auto mid = static_cast<float>(y) + h * 0.5f;
        g.setColour(cream.withAlpha(0.22f));
        g.fillRoundedRectangle(static_cast<float>(x), mid - 2, static_cast<float>(w), 4, 2);
        g.setColour(cyan);
        g.fillRoundedRectangle(static_cast<float>(x), mid - 2, juce::jmax(0.0f, pos - x), 4, 2);
        g.setColour(cream);
        g.fillEllipse(pos - 6, mid - 6, 12, 12);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return font(juce::jmin(15.0f, h * 0.6f), true); }
    juce::Font getComboBoxFont(juce::ComboBox& box) override { return font(box.getHeight() <= 25 ? 13.0f : 16.0f, true); }
    juce::Font getLabelFont(juce::Label& l) override { return l.getFont(); }
    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        std::unique_ptr<juce::Label> original(juce::LookAndFeel_V4::createSliderTextBox(slider));
        auto* label = new ParameterValueLabel(slider);
        original->copyAllExplicitColoursTo(*label);
        label->setJustificationType(juce::Justification::centred);
        label->setKeyboardType(juce::TextInputTarget::decimalKeyboard);
        label->setFont(font(static_cast<bool>(slider.getProperties().getWithDefault("macro", false)) ? 23.0f : 17.0f));
        return label;
    }
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override
    {
        if (! static_cast<bool>(slider.getProperties().getWithDefault("macro", false)))
            return juce::LookAndFeel_V4::getSliderLayout(slider);
        juce::Slider::SliderLayout layout;
        layout.sliderBounds = { 0, 0, slider.getWidth(), 119 };
        layout.textBoxBounds = { 0, 132, slider.getWidth(), 27 };
        return layout;
    }
};

class LockButton final : public juce::Button
{
public:
    explicit LockButton(const juce::String& name) : Button(name) { setTitle(name); setClickingTogglesState(true); }
    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced(3);
        if (getToggleState() || over)
        {
            g.setColour(getToggleState() ? cyan : cream.withAlpha(0.45f));
            g.fillRoundedRectangle(r, 8);
        }
        auto c = r.getCentre();
        g.setColour(ink);
        g.fillRoundedRectangle(c.x - 6, c.y - 1, 12, 10, 2);
        juce::Path shackle;
        shackle.startNewSubPath(c.x - 4, c.y);
        shackle.lineTo(c.x - 4, c.y - 5);
        shackle.cubicTo(c.x - 4, c.y - 12, c.x + 4, c.y - 12, c.x + 4, c.y - 5);
        shackle.lineTo(c.x + 4, c.y + (getToggleState() ? 0 : -4));
        g.strokePath(shackle, juce::PathStrokeType(2.2f));
        g.setColour(getToggleState() ? cyan : lavender);
        g.fillEllipse(c.x - 1.2f, c.y + 2, 2.4f, 2.4f);
        if (hasKeyboardFocus(true))
        {
            g.setColour(ink);
            g.drawRoundedRectangle(r, 8, 2);
        }
    }
};

template <typename Base>
class ParameterControl : public Base
{
public:
    std::function<void()> onContextMenu;
    void paintOverChildren(juce::Graphics& g) override
    {
        Base::paintOverChildren(g);
        if (static_cast<bool>(this->getProperties().getWithDefault("randomExcluded", false)))
        {
            g.setColour(cyan);
            g.fillRoundedRectangle(static_cast<float>(this->getWidth() - 7), 2.0f, 4.0f, 7.0f, 1.5f);
        }
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        contextClick = event.mods.isPopupMenu();
        if (contextClick) { if (onContextMenu) onContextMenu(); return; }
        Base::mouseDown(event);
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (! contextClick) Base::mouseDrag(event);
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (! contextClick) Base::mouseUp(event);
        contextClick = false;
    }
private:
    bool contextClick = false;
};

using ParameterButton = ParameterControl<juce::TextButton>;
using ParameterCombo = ParameterControl<juce::ComboBox>;

class FocusedSlider final : public ParameterControl<juce::Slider>
{
public:
    FocusedSlider() { setPopupMenuEnabled(false); }
    bool keyPressed(const juce::KeyPress& key) override
    {
        const int code = key.getKeyCode();
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey
            || code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
        {
            juce::Slider::ScopedDragNotification gesture(*this);
            return juce::Slider::keyPressed(key);
        }
        return juce::Slider::keyPressed(key);
    }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& details) override
    {
        if (hasKeyboardFocus(true)) juce::Slider::mouseWheelMove(event, details);
    }
};

class Macro final : public juce::Component
{
public:
    Macro(const juce::String& name, const juce::String& tip) : lock("Lock " + name)
    {
        title.setText(name, juce::dontSendNotification);
        title.setFont(font(21, true));
        title.setColour(juce::Label::textColourId, ink);
        title.setJustificationType(juce::Justification::centred);
        slider.setName(name);
        slider.setTitle(name);
        slider.setTooltip(tip);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 110, 26);
        slider.setColour(juce::Slider::textBoxTextColourId, midnight);
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setWantsKeyboardFocus(true);
        slider.getProperties().set("macro", true);
        slider.setScrollWheelEnabled(true);
        slider.setVelocityModeParameters(0.22, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        lock.setTooltip("Protect " + name.toLowerCase() + " from New Yoi and Mutate. Manual edits still work.");
        addAndMakeVisible(slider);
        addAndMakeVisible(title);
        addAndMakeVisible(lock);
    }
    void resized() override
    {
        title.setBounds(0, 111, getWidth(), 26);
        slider.setBounds(0, 0, getWidth(), 160);
        lock.setBounds(getWidth() - 31, -5, 32, 32);
    }
    FocusedSlider slider;
    juce::Label title;
    LockButton lock;
};

float readNormalised(juce::AudioProcessorValueTreeState& state, const juce::String& id, float fallback)
{
    if (auto* p = state.getParameter(id)) return p->getValue();
    return fallback;
}

float readActual(juce::AudioProcessorValueTreeState& state, const juce::String& id, float fallback)
{
    if (auto* p = state.getRawParameterValue(id)) return p->load();
    return fallback;
}

class GestureCanvas final : public juce::Component
{
public:
    explicit GestureCanvas(juce::AudioProcessorValueTreeState& s) : state(s)
    {
        setTitle("Mouth motion: Start, Body, End");
        setDescription("Three vowel poses across the length of a note. Use the Shape controls to edit numerically.");
        setWantsKeyboardFocus(true);
    }
    std::function<void(int)> selectPoint;
    std::function<void()> openShape, beginEdit, endEdit;
    std::function<void(const juce::String&)> contextMenu;
    bool editing = false, snap = true;
    int selected = 1;
    float phase = -1;

    static float poseToVisual(float pose)
    {
        return pose <= 0.75f ? 0.18f + pose * 0.6f : 0.63f + (pose - 0.75f) * 1.2f;
    }

    static float visualToPose(float visual)
    {
        return visual <= 0.63f ? (visual - 0.18f) / 0.6f : 0.75f + (visual - 0.63f) / 1.2f;
    }

    juce::Rectangle<float> graph() const { return getLocalBounds().toFloat().reduced(8, 3).withTrimmedLeft(68).withTrimmedRight(64).withTrimmedBottom(19); }
    std::array<juce::Point<float>, 3> points() const
    {
        auto r = graph();
        const float t = juce::jlimit(0.2f, 0.8f, readActual(state, "body_time", 0.5f));
        return {{{ r.getX(), r.getY() + poseToVisual(readNormalised(state, "start_pose", 0)) * r.getHeight() },
                 { r.getX() + t * r.getWidth(), r.getY() + poseToVisual(readNormalised(state, "body_pose", 0.75f)) * r.getHeight() },
                 { r.getRight(), r.getY() + poseToVisual(readNormalised(state, "end_pose", 0)) * r.getHeight() }}};
    }
    void paint(juce::Graphics& g) override
    {
        auto r = graph();
        const juce::Rectangle<float> grid(34, r.getY(), getWidth() - 48.0f, r.getHeight());
        for (int i = 0; i <= 22; ++i)
        {
            auto x = grid.getX() + grid.getWidth() * i / 22.0f;
            g.setColour(cyan.withAlpha(i % 4 == 0 ? 0.17f : 0.065f));
            g.drawVerticalLine(juce::roundToInt(x), r.getY(), r.getBottom());
        }
        for (int i = 0; i <= 6; ++i)
        {
            const auto y = r.getY() + r.getHeight() * i / 6.0f;
            g.setColour(cyan.withAlpha(i % 2 == 0 ? 0.17f : 0.07f));
            g.drawHorizontalLine(juce::roundToInt(y), grid.getX(), grid.getRight());
        }
        const std::array<juce::String, 3> vowelLabels { "EE", "OH", "OO" };
        const std::array<float, 3> vowelPositions { 0.18f, 0.63f, 0.93f };
        for (int i = 0; i < 3; ++i)
            text(g, vowelLabels[static_cast<size_t>(i)],
                 { 0, juce::roundToInt(r.getY() + vowelPositions[static_cast<size_t>(i)] * r.getHeight()) - 9, 31, 18 },
                 17, cyan, true);
        auto pts = points();
        const auto mouth = editing ? 1.0f : readNormalised(state, "mouth", 0.65f);
        for (int i = 1; i < 3; ++i) pts[static_cast<size_t>(i)].y = pts[0].y + (pts[static_cast<size_t>(i)].y - pts[0].y) * mouth;
        juce::Path path;
        path.startNewSubPath(pts[0]);
        const auto curve = readNormalised(state, "curve", 0.5f);
        const auto exponent = std::exp2((0.5f - curve) * 2.4f);
        const auto looping = ! editing && readActual(state, "loop", 0) >= 0.5f;
        for (std::size_t segment = 0; segment < 2; ++segment)
        {
            for (int i = 1; i <= 64; ++i)
            {
                const auto t = i / 64.0f;
                const auto shaped = std::pow(t, exponent);
                const auto eased = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * shaped);
                const auto x = pts[segment].x + (pts[segment + 1].x - pts[segment].x) * t;
                auto y = pts[segment].y + (pts[segment + 1].y - pts[segment].y) * eased;
                const auto position = (x - r.getX()) / r.getWidth();
                if (looping && position > 0.94f) y += (pts[0].y - y) * ((position - 0.94f) / 0.06f);
                path.lineTo(x, y);
            }
        }
        auto fill = path;
        fill.lineTo(pts[2].x, r.getBottom());
        fill.lineTo(pts[0].x, r.getBottom());
        fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(cyan.withAlpha(0.46f), r.getTopLeft(), electricBlue.withAlpha(0.03f), r.getBottomLeft(), false));
        g.fillPath(fill);
        g.setColour(cyan.withAlpha(0.12f));
        g.strokePath(path, juce::PathStrokeType(22));
        g.setColour(cyan.withAlpha(0.27f));
        g.strokePath(path, juce::PathStrokeType(10));
        g.setColour(lime.brighter(0.16f));
        g.strokePath(path, juce::PathStrokeType(2.8f));
        if (phase >= 0)
        {
            g.setColour(cream.withAlpha(0.65f));
            const auto x = r.getX() + juce::jlimit(0.0f, 1.0f, phase) * r.getWidth();
            g.drawLine(x, r.getY(), x, r.getBottom(), 1.2f);
        }
        const std::array<juce::String, 3> labels { "START", "BODY", "END" };
        for (int i = 0; i < 3; ++i)
        {
            auto p = pts[static_cast<size_t>(i)];
            if (looping && i == 2) p.y = pts[0].y;
            g.setColour(cyan.withAlpha(0.12f));
            g.fillEllipse(p.x - 23, p.y - 23, 46, 46);
            g.setColour(lime.withAlpha(0.22f));
            g.fillEllipse(p.x - 16, p.y - 16, 32, 32);
            g.setColour(lime);
            g.fillEllipse(p.x - 14, p.y - 14, 28, 28);
            g.setGradientFill(juce::ColourGradient(juce::Colours::white, p.x - 4, p.y - 7,
                                                  lime, p.x + 6, p.y + 9, false));
            g.fillEllipse(p.x - 9, p.y - 9, 18, 18);
            g.setColour(cream);
            g.drawEllipse(p.x - 14, p.y - 14, 28, 28, 1.5f);
            if (editing && selected == i)
            {
                g.setColour(cyan);
                g.drawEllipse(p.x - 18, p.y - 18, 36, 36, 1.4f);
            }
            text(g, labels[static_cast<size_t>(i)], { juce::roundToInt(p.x) - 34, juce::roundToInt(r.getBottom()) + 10, 68, 16 },
                 12, cream.withAlpha(0.75f), true, juce::Justification::centred);
        }
        if (hasKeyboardFocus(true))
        {
            g.setColour(cyan);
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 5, 1);
        }
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto pts = points();
        float distance = 100000;
        for (int i = 0; i < 3; ++i)
        {
            const auto d = pts[static_cast<size_t>(i)].getDistanceFrom(event.position);
            if (d < distance) { selected = i; distance = d; }
        }
        if (event.mods.isPopupMenu())
        {
            if (selectPoint) selectPoint(selected);
            if (contextMenu) contextMenu(poseId());
            return;
        }
        if (! editing) { if (openShape) openShape(); return; }
        if (selectPoint) selectPoint(selected);
        if (beginEdit) beginEdit();
        if (auto* p = state.getParameter(poseId())) p->beginChangeGesture();
        if (selected == 1) if (auto* p = state.getParameter("body_time")) p->beginChangeGesture();
        dragging = true;
        mouseDrag(event);
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (! editing || ! dragging) return;
        const auto r = graph();
        auto v = juce::jlimit(0.0f, 1.0f, visualToPose((event.position.y - r.getY()) / r.getHeight()));
        v = std::round(v * 4) / 4; // Version one exposes five discrete, calibrated vowel poses.
        if (auto* p = state.getParameter(poseId())) p->setValueNotifyingHost(v);
        if (selected == 1)
            if (auto* p = state.getParameter("body_time"))
            {
                auto position = juce::jlimit(0.2f, 0.8f, (event.position.x - r.getX()) / r.getWidth());
                if (snap) position = std::round(position * 20) / 20;
                p->setValueNotifyingHost(p->convertTo0to1(position));
            }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (! dragging) return;
        if (auto* p = state.getParameter(poseId())) p->endChangeGesture();
        if (selected == 1) if (auto* p = state.getParameter("body_time")) p->endChangeGesture();
        dragging = false;
        if (endEdit) endEdit();
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (! editing && (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey))
        { if (openShape) openShape(); return true; }
        return false;
    }
private:
    juce::String poseId() const { return selected == 0 ? "start_pose" : selected == 1 ? "body_pose" : "end_pose"; }
    juce::AudioProcessorValueTreeState& state;
    bool dragging = false;
};
}

class YoiBoiEditor::Impl final : public juce::Component, private juce::Timer, private juce::ListBoxModel
{
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    Impl(juce::AudioProcessorValueTreeState& s, EditorCallbacks cb) : state(s), callbacks(std::move(cb)), shellArtwork(juce::ImageCache::getFromMemory(BinaryData::HandheldShell_png, BinaryData::HandheldShell_pngSize)), gesture(s), tooltip(this, 650)
    {
        setLookAndFeel(&theme);
        const std::array<juce::String, 3> tabNames { "PLAY", "SHAPE", "LIBRARY" };
        for (int i = 0; i < 3; ++i)
        {
            auto& tab = tabs[static_cast<size_t>(i)];
            setupButton(tab, tabNames[static_cast<size_t>(i)], true);
            tab.getProperties().set("italic", true);
            tab.getProperties().set("role", "tab");
            tab.getProperties().set("fontSize", 21.0f);
            tab.onClick = [this, i] { showPage(i); };
        }
        setupButton(zoom, "100%", true);
        zoom.setTitle("Interface zoom and settings");
        zoom.getProperties().set("role", "utility");
        zoom.getProperties().set("fontSize", 12.0f);
        zoom.setTooltip("Choose the device size.");
        zoom.onClick = [this] { showZoomMenu(); };
        setupButton(close, "CLOSE", true);
        close.setTitle("Close plug-in window");
        close.getProperties().set("role", "utility");
        close.setTooltip("Close this device window. Your sound continues playing.");
        close.onClick = [this] { if (callbacks.requestClose) callbacks.requestClose(); };
        setupButton(newYoi, "NEW YOI");
        newYoi.getProperties().set("fontSize", 34.0f);
        newYoi.getProperties().set("italic", true);
        newYoi.getProperties().set("role", "primary");
        newYoi.setTooltip("Generate a fresh talking bass. Excluded parameters stay put. Use Save Preset to keep a sound.");
        newYoi.onClick = [this] { if (callbacks.newYoi) callbacks.newYoi(); toast("New sound ready — not saved"); refreshLibrary(); };
        setupButton(mutate, "MUTATE");
        mutate.getProperties().set("fontSize", 27.0f);
        mutate.getProperties().set("italic", true);
        mutate.getProperties().set("role", "mutate");
        mutate.setTooltip("Make a smaller variation of this sound, keeping every locked group.");
        mutate.onClick = [this] { if (callbacks.mutate) callbacks.mutate(); toast("Variation applied"); refreshLibrary(); };
        setupButton(undo, "UNDO"); setupButton(redo, "REDO");
        undo.getProperties().set("role", "history");
        redo.getProperties().set("role", "history");
        undo.onClick = [this] { if (callbacks.undo) callbacks.undo(); toast("Undone"); refreshLibrary(); };
        redo.onClick = [this] { if (callbacks.redo) callbacks.redo(); toast("Redone"); refreshLibrary(); };
        setupButton(favorite, "FAVORITE", true);
        favorite.getProperties().set("fontSize", 11.0f);
        favorite.getProperties().set("role", "favorite");
        favorite.setTooltip("Favorite a factory or saved preset. Save a new sound first to keep it.");
        favorite.onClick = [this] { if (callbacks.toggleFavorite) callbacks.toggleFavorite(); refreshLibrary(); };

        family.addItemList({ "ALL FAMILIES", "RUBBER", "METAL", "THROAT", "SQUEAK" }, 1);
        family.setSelectedId(1, juce::dontSendNotification);
        family.setTitle("Generation family");
        family.setTooltip("New Yoi stays in the selected family. All Families permits a new family.");
        family.onChange = [this] { if (callbacks.setGenerationFamily) callbacks.setGenerationFamily(family.getText()); };
        addAndMakeVisible(family);

        const std::array<juce::String, 6> names { "MOUTH", "SIZE", "BITE", "DIRT", "LENGTH", "SUB" };
        const std::array<juce::String, 6> descriptions {
            "How far the sound moves through the vowel shape. Zero holds its starting vowel.",
            "Make the mouth larger and deeper without changing the played note.",
            "Add harmonics and a sharper FM articulation.",
            "Add saturation with calibrated level compensation.",
            "Set one vowel gesture's duration, in beats with Sync or milliseconds without it.",
            "Blend in a clean sine sub, separate from the mouth distortion." };
        const std::array<juce::String, 6> ids { "mouth", "size", "bite", "dirt", "length_division", "sub" };
        for (int i = 0; i < 6; ++i)
        {
            auto& m = macros[static_cast<size_t>(i)];
            m = std::make_unique<Macro>(names[static_cast<size_t>(i)], descriptions[static_cast<size_t>(i)]);
            addAndMakeVisible(*m);
            m->slider.onDragStart = [this, name = names[static_cast<size_t>(i)]] { beginEdit("Adjust " + name); };
            m->slider.onDragEnd = [this] { endEdit(); };
            m->lock.setToggleState(i >= 4, juce::dontSendNotification);
            m->lock.onClick = [this, i] {
                if (callbacks.setLock) callbacks.setLock(i, macros[static_cast<size_t>(i)]->lock.getToggleState());
                else fallbackLocks[static_cast<size_t>(i)] = macros[static_cast<size_t>(i)]->lock.getToggleState();
            };
            if (i != 4) attachMacro(i, ids[static_cast<size_t>(i)]);
        }

        setupButton(oneShot, "ONE SHOT", true); setupButton(loop, "LOOP", true);
        oneShot.onClick = [this] { setParam("loop", 0); };
        loop.onClick = [this] { setParam("loop", 1); };
        bindContext(oneShot, "loop"); bindContext(loop, "loop");
        setupButton(sync, "SYNC", true);
        sync.getProperties().set("role", "sync");
        sync.setClickingTogglesState(true);
        sync.setTooltip("Use musical divisions for Length. Switching keeps both free and synced values.");
        // Use the same transaction path as the other buttons so switching
        // timing mode is included in the instrument's sound Undo history.
        sync.onClick = [this] { setParam("sync", sync.getToggleState() ? 1.0f : 0.0f); };
        sync.setEnabled(state.getParameter("sync") != nullptr);
        bindContext(sync, "sync");
        subMode.addItemList({ "OFF", "DIRECT", "DRIVEN" }, 1);
        subMode.setTitle("Sub routing");
        subMode.setTooltip("Off mutes the sine sub. Direct adds it after the body distortion. Driven saturates the sub.");
        subMode.onChange = [this] { if (! updatingInspector) setParam("sub_mode", (subMode.getSelectedId() - 1) / 2.0f); };
        addAndMakeVisible(subMode); bindContext(subMode, "sub_mode");
        setupButton(lowCut, "BODY LOW-CUT", true);
        lowCut.getProperties().set("role", "filter");
        lowCut.getProperties().set("fontSize", 12.0f);
        lowCut.setTooltip("Remove sub frequencies from the talking body so the direct sine stays clean.");
        lowCut.onClick = [this] { setParam("low_cut", readActual(state, "low_cut", 1) < 0.5f ? 1.0f : 0.0f); };
        bindContext(lowCut, "low_cut");
        lengthFeel.addItemList({ "STRAIGHT", "TRIPLET · 2/3", "DOTTED · 3/2" }, 1);
        lengthFeel.setTitle("Length rhythm");
        lengthFeel.setTooltip("Apply straight, triplet or dotted timing to the Length division. Available when Sync is on.");
        lengthFeel.onChange = [this] { if (! updatingInspector) setParam("length_feel", (lengthFeel.getSelectedId() - 1) / 2.0f); };
        addAndMakeVisible(lengthFeel); bindContext(lengthFeel, "length_feel");
        setupButton(preview, "PREVIEW", true);
        preview.getProperties().set("role", "preview");
        preview.getProperties().set("italic", true);
        preview.setTooltip("Audition a short bass note. External MIDI takes priority.");
        preview.onClick = [this] { if (callbacks.togglePreview) callbacks.togglePreview(); };
        prepareSmallSlider(glide, "Glide", "glide_ms", " ms", glideAttachment);
        prepareSmallSlider(output, "Output", "output_db", " dB", outputAttachment);
        for (auto* numeric : { &glide, &output })
        {
            numeric->getProperties().set("valueOnly", true);
            numeric->setSliderStyle(juce::Slider::LinearBar);
            numeric->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 31);
            numeric->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            numeric->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            numeric->setTooltip(numeric->getName() + ": drag to adjust, or click the value to type.");
        }

        gesture.openShape = [this] { showPage(1); };
        gesture.selectPoint = [this](int p) { selectedPoint.setSelectedId(p + 1, juce::dontSendNotification); syncShapeInspector(); };
        gesture.beginEdit = [this] { beginEdit("Edit mouth motion"); };
        gesture.endEdit = [this] { endEdit(); };
        gesture.contextMenu = [this](const juce::String& id) { showParameterMenu(id, gesture); };
        addAndMakeVisible(gesture);
        selectedPoint.addItemList({ "START", "BODY", "END" }, 1);
        selectedPoint.setSelectedId(2, juce::dontSendNotification);
        selectedPoint.setTitle("Selected gesture point");
        selectedPoint.onChange = [this] { gesture.selected = selectedPoint.getSelectedId() - 1; syncShapeInspector(); gesture.repaint(); };
        pose.addItemList(vowels, 1); pose.setSelectedId(4, juce::dontSendNotification); pose.setTitle("Vowel pose");
        pose.onChange = [this] { if (! updatingInspector) setParam(poseId(), (pose.getSelectedId() - 1) / 4.0f); };
        pose.onContextMenu = [this] { showParameterMenu(poseId(), pose); };
        setupButton(snap, "TIME SNAP", true);
        snap.setClickingTogglesState(true); snap.setToggleState(true, juce::dontSendNotification);
        snap.onClick = [this] { gesture.snap = snap.getToggleState(); };
        snap.setTooltip("Snap dragged Body timing to 5% steps. Vowels always use five curated poses.");
        setupButton(resetShape, "RESET SHAPE", true);
        resetShape.setTooltip("Restore this family's mouth motion. This action can be undone.");
        resetShape.onClick = [this] {
            if (callbacks.resetShape) callbacks.resetShape();
            else { beginEdit("Reset shape"); setParam("start_pose", 0, false); setParam("body_pose", 0.75f, false); setParam("end_pose", 0, false); setParam("body_time", 0.5f, false); setParam("curve", 0.5f, false); endEdit(); }
        };
        setupButton(motionLock, "LOCK", true);
        motionLock.getProperties().set("fontSize", 14.0f);
        motionLock.getProperties().set("role", "filter");
        motionLock.setClickingTogglesState(true);
        motionLock.setTooltip("Lock or unlock Mouth and Length together for New Yoi and Mutate.");
        motionLock.onClick = [this] {
            const bool next = motionLock.getToggleState();
            if (callbacks.setLock) { callbacks.setLock(0, next); callbacks.setLock(4, next); }
            else { fallbackLocks[0] = next; fallbackLocks[4] = next; }
        };
        prepareSmallSlider(bodyTime, "Body timing", "body_time", "", bodyAttachment);
        bodyTime.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; };
        bodyTime.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue() / 100.0; };
        bodyTime.updateText();
        prepareSmallSlider(curve, "Curve", "curve", "", curveAttachment);
        curve.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; };
        curve.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue() / 100.0; };
        curve.updateText();
        addAndMakeVisible(selectedPoint); addAndMakeVisible(pose);

        const std::array<juce::String, 7> filters { "ALL SOUNDS", "FAVORITES", "SAVED", "RUBBER", "METAL", "THROAT", "SQUEAK" };
        for (int i = 0; i < 7; ++i)
        {
            auto& button = libraryFilters[static_cast<size_t>(i)];
            setupButton(button, filters[static_cast<size_t>(i)], true);
            button.getProperties().set("fontSize", 16.0f);
            button.getProperties().set("role", "filter");
            button.onClick = [this, i] { activeFilter = i; refreshLibrary(); };
        }
        search.setTextToShowWhenEmpty("Search sounds", cream.withAlpha(0.55f));
        search.setTitle("Search sounds");
        search.setFont(font(14));
        search.onTextChange = [this] { refreshLibrary(); };
        addAndMakeVisible(search);
        library.setModel(this);
        library.setRowHeight(27);
        library.setOutlineThickness(0);
        library.setTitle("Presets. Select a row, then double-click or press Enter to load.");
        library.setMultipleSelectionEnabled(false);
        addAndMakeVisible(library);
        setupButton(load, "LOAD", true);
        load.onClick = [this] { loadSelected(); };
        setupButton(save, "SAVE PRESET", true);
        save.onClick = [this] { showSaveDialog(); };

        auto infoTypeface = juce::Typeface::createSystemTypefaceFor(
            BinaryData::ChakraPetchSemiBold_ttf, BinaryData::ChakraPetchSemiBold_ttfSize);
        infoButton = std::make_unique<vfd::InfoButton>(vfd::themeFor("YOI-BOI"), infoTypeface);
        infoOverlay = std::make_unique<vfd::InfoOverlay>("YOI-BOI", vfd::themeFor("YOI-BOI"),
            BinaryData::VfdLogo_svg, static_cast<size_t>(BinaryData::VfdLogo_svgSize), infoTypeface);
        infoButton->onClick = [this] { infoOverlay->open(infoButton.get()); };
        addAndMakeVisible(*infoButton);
        addChildComponent(*infoOverlay);
        refreshLibrary();
        updateValues();
        setSize(designWidth, designHeight);
        showPage(0);
        startTimerHz(30);
    }

    ~Impl() override
    {
        stopTimer();
        library.setModel(nullptr);
        for (auto& attachment : macroAttachments) attachment.reset();
        glideAttachment.reset(); outputAttachment.reset(); bodyAttachment.reset(); curveAttachment.reset();
        setLookAndFeel(nullptr);
    }

    void setupButton(juce::TextButton& button, const juce::String& name, bool dark = false)
    {
        button.setButtonText(name); button.setTitle(name); button.setWantsKeyboardFocus(true);
        button.getProperties().set("dark", dark); addAndMakeVisible(button);
    }
    template <typename Control>
    void bindContext(Control& control, const juce::String& id)
    {
        control.getProperties().set("parameterId", id);
        control.onContextMenu = [this, &control, id] { showParameterMenu(id, control); };
    }
    void showParameterMenu(const juce::String& id, juce::Component& target)
    {
        if (! callbacks.setParameterLock || ! callbacks.isParameterLocked) return;
        const auto* parameter = state.getParameter(id);
        juce::PopupMenu menu;
        menu.addSectionHeader(parameter != nullptr ? parameter->getName(50) : id);
        const bool excluded = callbacks.isParameterLocked(id);
        menu.addItem(1, "Exclude from randomization", true, excluded);
        if (id == "length_division")
        {
            if (const auto* division = dynamic_cast<const juce::AudioParameterChoice*>(parameter))
            {
                juce::PopupMenu rhythm;
                for (int i = 0; i < division->choices.size(); ++i)
                {
                    juce::PopupMenu feel;
                    feel.addItem(100 + i * 3, division->choices[i] + " straight");
                    feel.addItem(101 + i * 3, division->choices[i] + " triplet");
                    feel.addItem(102 + i * 3, division->choices[i] + " dotted");
                    rhythm.addSubMenu(division->choices[i], feel);
                }
                menu.addSubMenu("Choose musical length", rhythm);
            }
        }
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target),
            [safe = juce::Component::SafePointer<Impl>(this), id, excluded](int result) {
                if (safe == nullptr) return;
                if (result == 1)
                {
                    safe->callbacks.setParameterLock(id, ! excluded);
                    safe->toast(excluded ? "Included in randomization" : "Excluded from randomization");
                    safe->updateValues();
                }
                else if (result >= 100 && id == "length_division")
                {
                    const int division = (result - 100) / 3, feel = (result - 100) % 3;
                    if (const auto* p = safe->state.getParameter(id))
                    {
                        safe->beginEdit("Choose musical length");
                        safe->setParam(id, p->convertTo0to1(static_cast<float>(division)), false);
                        safe->setParam("length_feel", feel / 2.0f, false);
                        safe->endEdit();
                    }
                }
            });
    }
    void showZoomMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader("DEVICE SIZE");
        auto* editor = findParentComponentOfClass<YoiBoiEditor>();
        const int current = editor != nullptr ? juce::roundToInt(editor->getWidth() * 100.0 / shellWidth) : 100;
        for (const int percent : { 100, 125, 150 })
            menu.addItem(percent, juce::String(percent) + "%", true, current == percent);
        menu.addSeparator();
        menu.addSectionHeader("YOI-BOI 0.1.4");
        const int engine = callbacks.soundEngine ? callbacks.soundEngine() : 3;
        if (engine < 3)
        {
            menu.addItem(10, engine == 1 ? "Original 0.1.0 / 0.1.1 sound preserved"
                                         : "Original 0.1.2 sound preserved", false, true);
            menu.addItem(11, "Use latest sound (changes tone)", static_cast<bool>(callbacks.useLatestSoundEngine));
        }
        else menu.addItem(10, "Latest sound engine", false, true);
        if (callbacks.audioSettings) { menu.addSeparator(); menu.addItem(2, "Audio / MIDI settings"); }
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&zoom),
            [safe = juce::Component::SafePointer<Impl>(this)](int result) {
                if (safe == nullptr) return;
                if (result == 2 && safe->callbacks.audioSettings) safe->callbacks.audioSettings();
                else if (result == 11 && safe->callbacks.useLatestSoundEngine)
                {
                    safe->callbacks.useLatestSoundEngine();
                    safe->toast("Latest sound enabled. Undo restores the original.");
                    safe->updateValues();
                }
                else if (result == 100 || result == 125 || result == 150)
                    if (auto* parent = safe->findParentComponentOfClass<YoiBoiEditor>())
                        parent->setSize(shellWidth * result / 100, shellHeight * result / 100);
            });
    }
    void beginEdit(const juce::String& name) { if (callbacks.beginEdit) callbacks.beginEdit(name); }
    void endEdit() { if (callbacks.endEdit) callbacks.endEdit(); }
    void setParam(const juce::String& id, float normalised, bool transaction = true)
    {
        if (auto* parameter = state.getParameter(id))
        {
            if (transaction) beginEdit("Adjust " + parameter->getName(40));
            parameter->beginChangeGesture(); parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalised)); parameter->endChangeGesture();
            if (transaction) endEdit();
        }
    }
    void prepareSmallSlider(FocusedSlider& slider, const juce::String& name, const juce::String& id,
                            const juce::String& suffix, std::unique_ptr<SliderAttachment>& attachment)
    {
        slider.setTitle(name); slider.setName(name);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 68, 28);
        slider.setColour(juce::Slider::textBoxTextColourId, cream);
        slider.setTextValueSuffix(suffix);
        slider.setScrollWheelEnabled(false);
        slider.setWantsKeyboardFocus(true);
        slider.onDragStart = [this, name] { beginEdit("Adjust " + name); };
        slider.onDragEnd = [this] { endEdit(); };
        bindContext(slider, id);
        addAndMakeVisible(slider);
        if (state.getParameter(id)) attachment = std::make_unique<SliderAttachment>(state, id, slider);
        else slider.setEnabled(false);
    }
    void attachMacro(int index, const juce::String& id)
    {
        auto& slider = macros[static_cast<size_t>(index)]->slider;
        bindContext(slider, id);
        macroAttachments[static_cast<size_t>(index)].reset();
        if (auto* parameter = state.getParameter(id))
        {
            macroAttachments[static_cast<size_t>(index)] = std::make_unique<SliderAttachment>(state, id, slider);
            if (index == 4)
            {
                slider.textFromValueFunction = [this, parameter, id](double v) {
                    if (id == "length_ms") return juce::String(juce::roundToInt(v)) + " ms";
                    const auto feel = juce::roundToInt(readActual(state, "length_feel", 0));
                    return parameter->getText(parameter->convertTo0to1(static_cast<float>(v)), 32)
                        + (feel == 1 ? " T" : feel == 2 ? " D" : "");
                };
                slider.valueFromTextFunction = [parameter, id](const juce::String& t) {
                    const auto value = t.trim().trimCharactersAtEnd(" TtDd.").trim();
                    return id == "length_ms" ? t.getDoubleValue() : static_cast<double>(parameter->convertFrom0to1(parameter->getValueForText(value)));
                };
            }
            else
            {
                slider.textFromValueFunction = [parameter](double v) { return juce::String(juce::roundToInt(parameter->convertTo0to1(static_cast<float>(v)) * 100)) + "%"; };
                slider.valueFromTextFunction = [parameter](const juce::String& t) { return static_cast<double>(parameter->convertFrom0to1(juce::jlimit(0.0f, 1.0f, t.getFloatValue() / 100))); };
            }
            slider.updateText();
        }
        else slider.setEnabled(false);
    }

    void paint(juce::Graphics& g) override
    {
        juce::Path silhouette;
        silhouette.addRoundedRectangle(static_cast<float>(shellX), static_cast<float>(shellY),
                                       static_cast<float>(shellWidth), static_cast<float>(shellHeight), 65.0f);
        g.reduceClipRegion(silhouette);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        // Retain the original control artwork at its exact coordinates. The new
        // board strip is inserted below the utility panel; the bottom rim moves.
        const auto drawShellSlice = [&](float destinationY, float destinationHeight,
                                       float sourceY, float sourceHeight)
        {
            const auto sourceScale = static_cast<float>(shellArtwork.getHeight()) / artworkHeight;
            g.drawImage(shellArtwork, 0, juce::roundToInt(destinationY), designWidth,
                        juce::roundToInt(destinationHeight), 0, juce::roundToInt(sourceY * sourceScale),
                        shellArtwork.getWidth(), juce::roundToInt(sourceHeight * sourceScale));
        };
        drawShellSlice(0, infoBandTop, 0, infoBandTop);
        drawShellSlice(infoBandTop, infoBandHeight, infoBandTop, 1);
        drawShellSlice(infoBandTop + infoBandHeight, artworkHeight - infoBandTop,
                       infoBandTop, artworkHeight - infoBandTop);
        {
            // Keep the added band in the same photographed lavender resin as
            // the original chassis, with its existing reflections and details.
            juce::Graphics::ScopedSaveState surfaceState(g);
            g.reduceClipRegion(65, infoBandTop, 948, infoBandHeight);
            drawShellSlice(infoBandTop, infoBandHeight, infoBandTop, 20);
        }

        for (int i = 0; i < 3; ++i)
        {
            const float x = 94.0f + i * 13.5f;
            juce::Path stripe;
            stripe.startNewSubPath(x, 45); stripe.lineTo(x + 11, 45);
            stripe.lineTo(x + 1, 70); stripe.quadraticTo(x, 74, x - 4, 74);
            stripe.lineTo(x - 12, 74); stripe.closeSubPath();
            g.setColour(i == 0 ? lavender.brighter(0.35f) : i == 1 ? cyan : lime);
            g.fillPath(stripe);
        }
        g.setFont(displayFont(66)); g.setColour(cream.brighter(0.12f));
        g.drawFittedText("YOI-BOI", { 145, 37, 193, 42 }, juce::Justification::centredLeft, 1);
        g.setFont(font(11, true, true).withExtraKerningFactor(0.19f));
        const bool originalSound = callbacks.soundEngine && callbacks.soundEngine() < 3;
        g.drawText(originalSound ? "ORIGINAL SOUND PRESERVED" : "TALKING BASS SYNTH",
                   juce::Rectangle<int>(347, 43, 180, 18), juce::Justification::centredLeft);
        if (midiLit)
        {
            for (int radius = 19; radius >= 9; --radius)
            {
                g.setColour(lime.withAlpha(0.025f));
                g.fillEllipse(950.0f - radius, 66.5f - radius, radius * 2.0f, radius * 2.0f);
            }
            g.setGradientFill(juce::ColourGradient(juce::Colours::white, 947, 62, lime, 955, 75, false));
            g.fillEllipse(940.5f, 57, 19, 19);
        }
        text(g, "MIDI", { 931, 78, 39, 15 }, 10, midnight, true, juce::Justification::centred);

        if (page != 2)
        {
            text(g, familyText.toUpperCase() + (page == 1 ? "  /  SHAPE" : "  /  MOUTH MOTION"),
                 { 104, 116, 390, 20 }, 17, cyan, true);
            g.setFont(displayFont(42)); g.setColour(cyan.brighter(0.15f));
            g.drawFittedText(patchName, { 103, 132, 467, 38 }, juce::Justification::centredLeft, 1);
            if (page == 0)
            {
                if (callbacks.waveform)
                {
                    const auto samples = callbacks.waveform();
                    float peak = 0.02f;
                    for (const auto sample : samples) peak = juce::jmax(peak, std::abs(sample));
                    for (int i = 0; i < static_cast<int>(samples.size()); ++i)
                    {
                        const float magnitude = std::abs(samples[static_cast<size_t>(i)]);
                        const float x = 104.0f + i * 5.2f;
                        const float half = 1.0f + juce::jmin(1.0f, magnitude / peak) * 16.0f;
                        g.setColour(cyan.withAlpha(0.12f));
                        g.drawLine(x, 352.0f - half, x, 352.0f + half, 4.5f);
                        g.setColour(cyan.withAlpha(magnitude > 0.004f ? 0.85f : 0.25f));
                        g.drawLine(x, 352.0f - half, x, 352.0f + half, 1.5f);
                    }
                }
                const auto prompt = allLocked ? juce::String("UNLOCK A CONTROL TO GENERATE")
                                  : statusUntil > juce::Time::getMillisecondCounterHiRes() ? status
                                  : juce::String("CLICK THE MOTION TO SHAPE IT");
                if (allLocked || statusUntil > juce::Time::getMillisecondCounterHiRes())
                    text(g, prompt.toUpperCase(), { 338, 116, 284, 20 }, 11, lime, true, juce::Justification::centredRight);
            }
            else
            {
                text(g, "SOURCE: FM  /  " + juce::String(callbacks.fmRatio ? callbacks.fmRatio() : 2.0f, 1) + ":1",
                     { 611, 155, 154, 18 }, 12, cyan.withAlpha(0.85f), true, juce::Justification::centredRight);
                g.setColour(cyan.withAlpha(0.27f)); g.drawLine(102, 310, 767, 310, 1);
                text(g, "POINT", { 103, 312, 70, 19 }, 14, cyan, true);
                text(g, "VOWEL", { 223, 312, 75, 19 }, 14, cyan, true);
                text(g, "BODY TIME", { 326, 312, 103, 19 }, 14, cyan, true);
                text(g, "CURVE", { 461, 312, 80, 19 }, 14, cyan, true);
            }
        }
        else
        {
            text(g, "YOUR SOUNDS", { 104, 117, 265, 21 }, 15, cyan, true);
            g.setColour(cyan.withAlpha(0.28f)); g.drawVerticalLine(569, 121, 372);
            text(g, "NAME", { 262, 169, 185, 17 }, 10, cyan.withAlpha(0.85f), true);
            text(g, "FAMILY", { 458, 169, 82, 17 }, 10, cyan.withAlpha(0.85f), true);
            text(g, "CURRENT SOUND", { 587, 165, 171, 20 }, 13, cyan, true);
            g.setFont(displayFont(21)); g.setColour(cream);
            g.drawFittedText(patchName, { 587, 187, 171, 28 }, juce::Justification::centredLeft, 1);
            text(g, familyText.toUpperCase(), { 587, 214, 171, 19 }, 13, cyan, true);
            g.setColour(cyan.withAlpha(0.28f)); g.drawLine(588, 238, 761, 238, 1);
            text(g, "SAVE WHAT YOU LOVE", { 587, 249, 180, 20 }, 13, cyan, true);
            g.setFont(font(13)); g.setColour(cream.withAlpha(0.8f));
            g.drawFittedText("New Yoi and Mutate never add presets.\n\nUse SAVE PRESET to keep this sound. Undo brings back a previous variation.",
                             { 587, 276, 175, 78 }, juce::Justification::topLeft, 6);
            if (filtered.empty()) text(g, "No sounds match this view.", { 252, 238, 302, 48 }, 13, cream.withAlpha(0.76f));
            text(g, juce::String(filtered.size()) + " SOUNDS", { 346, 347, 82, 20 }, 10, cyan, true);
        }
        if (page != 0 && statusUntil > juce::Time::getMillisecondCounterHiRes())
            text(g, status, { 325, 116, 230, 21 }, 13, lime, true, juce::Justification::centredRight);
        g.setColour(cyan.withAlpha(0.2f)); g.drawLine(103, 367, 767, 367, 1);
        text(g, "SUB", { 104, 372, 31, 23 }, 12, cyan, true);
        text(g, "LENGTH", { 424, 372, 61, 23 }, 12, cyan, true);
        text(g, "RIGHT-CLICK TO LOCK", { 634, 372, 135, 23 }, 10, cream.withAlpha(0.65f), false, juce::Justification::centredRight);
        text(g, "GLIDE", { 408, 620, 55, 24 }, 15, midnight, true);
        text(g, "OUTPUT", { 709, 620, 62, 24 }, 15, midnight, true);
        drawMeters(g);
    }

    void resized() override
    {
        zoom.setBounds(347, 65, 82, 24); close.setBounds(433, 65, 26, 24);
        for (int i = 0; i < 3; ++i) tabs[static_cast<size_t>(i)].setBounds(552 + i * 119, 44, 117, 37);
        newYoi.setBounds(820, 104, 202, 133);
        mutate.setBounds(820, 251, 202, 62);
        undo.setBounds(820, 331, 96, 60); redo.setBounds(925, 331, 97, 60);
        family.setBounds(page == 2 ? 585 : 631, 120, page == 2 ? 143 : 135, 28);
        favorite.setBounds(page == 2 ? 730 : 582, page == 2 ? 118 : 136, 34, 34);
        const std::array<int, 6> centres { 132, 287, 442, 599, 757, 928 };
        for (int i = 0; i < 6; ++i) macros[static_cast<size_t>(i)]->setBounds(centres[static_cast<size_t>(i)] - 74, 426, 148, 160);
        oneShot.setBounds(72, 613, 111, 35); loop.setBounds(184, 613, 88, 35); sync.setBounds(306, 614, 82, 36);
        glide.setBounds(460, 616, 73, 31); preview.setBounds(576, 615, 98, 34); output.setBounds(776, 616, 67, 31);
        gesture.setBounds(page == 1 ? juce::Rectangle<int>(103, 177, 667, 132) : juce::Rectangle<int>(103, 178, 667, 155));
        selectedPoint.setBounds(102, 331, 110, 27); pose.setBounds(222, 331, 94, 27);
        bodyTime.setBounds(326, 331, 123, 27); curve.setBounds(460, 331, 114, 27);
        snap.setBounds(584, 329, 83, 31); resetShape.setBounds(674, 329, 95, 31);
        motionLock.setBounds(638, 119, 130, 30);
        search.setBounds(104, 141, 449, 29);
        save.setBounds(431, 341, 122, 27); load.setBounds(254, 341, 80, 27);
        for (int i = 0; i < 7; ++i) libraryFilters[static_cast<size_t>(i)].setBounds(102, 180 + i * 26, 134, 25);
        library.setBounds(253, 187, 300, 150);
        subMode.setBounds(137, 372, 108, 23); lowCut.setBounds(257, 371, 145, 25);
        lengthFeel.setBounds(486, 372, 135, 23);
        if (infoButton) infoButton->setBounds(492, 668, 96, 38);
        if (infoOverlay) infoOverlay->setBounds(shellX, shellY, shellWidth, shellHeight);
    }

    void showPage(int newPage)
    {
        page = newPage;
        for (int i = 0; i < 3; ++i) tabs[static_cast<size_t>(i)].setToggleState(i == page, juce::dontSendNotification);
        gesture.setVisible(page != 2); gesture.editing = page == 1;
        favorite.setVisible(true);
        family.setVisible(page != 1);
        for (auto* c : std::array<juce::Component*, 7> { &selectedPoint, &pose, &bodyTime, &curve, &snap, &resetShape, &motionLock }) c->setVisible(page == 1);
        for (auto* c : std::array<juce::Component*, 4> { &search, &library, &load, &save }) c->setVisible(page == 2);
        for (auto& b : libraryFilters) b.setVisible(page == 2);
        if (page == 2) refreshLibrary();
        if (page == 1) syncShapeInspector();
        resized(); repaint();
    }

    void drawMeters(juce::Graphics& g)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const float level = ch == 0 ? meterL : meterR;
            const auto db = juce::Decibels::gainToDecibels(juce::jmax(level, 0.00001f), -60.0f);
            const auto count = juce::jlimit(0, 16, juce::roundToInt((db + 60) / 60 * 16));
            text(g, ch == 0 ? "L" : "R", { 857, 615 + ch * 17, 17, 17 }, 11, midnight, true);
            for (int i = 0; i < 16; ++i)
            {
                g.setColour(i < count ? (level >= 0.99f && i == 15 ? juce::Colours::salmon : i > 10 ? cyan : lime)
                                     : cobalt.withAlpha(0.44f));
                g.fillRect(882.0f + i * 7.65f, 620.0f + ch * 17.0f, 6.4f, 8.0f);
            }
        }
    }

    void toast(const juce::String& message) { status = message; statusUntil = juce::Time::getMillisecondCounterHiRes() + 2600; repaint(); }
    void timerCallback() override { updateValues(); }
    void updateValues()
    {
        patchName = callbacks.currentPatchName ? callbacks.currentPatchName() : "Bubble Trouble";
        familyText = callbacks.familyName ? callbacks.familyName() : "Rubber";
        const auto isSync = readActual(state, "sync", 1) >= 0.5f;
        sync.setToggleState(isSync, juce::dontSendNotification);
        if (! lengthAttached || isSync != lastSync)
        {
            lastSync = isSync; lengthAttached = true;
            attachMacro(4, isSync ? "length_division" : "length_ms");
        }
        const int feel = juce::jlimit(0, 2, juce::roundToInt(readActual(state, "length_feel", 0)));
        if (feel != lastFeel) { lastFeel = feel; macros[4]->slider.updateText(); }
        {
            const juce::ScopedValueSetter<bool> updating(updatingInspector, true);
            if (! subMode.isPopupActive()) subMode.setSelectedId(1 + juce::jlimit(0, 2, juce::roundToInt(readActual(state, "sub_mode", 1))), juce::dontSendNotification);
            if (! lengthFeel.isPopupActive()) lengthFeel.setSelectedId(feel + 1, juce::dontSendNotification);
            lowCut.setToggleState(readActual(state, "low_cut", 1) >= 0.5f, juce::dontSendNotification);
        }
        lengthFeel.setEnabled(isSync);
        const bool originalSound = callbacks.soundEngine && callbacks.soundEngine() < 3;
        subMode.setEnabled(! originalSound);
        lowCut.setEnabled(! originalSound);
        subMode.setTooltip(originalSound ? "Original sound preserved. Set SUB to zero to mute it, or choose Zoom > Use latest sound for new routing."
                                         : "Off mutes the sub. Direct bypasses distortion. Driven adds saturation.");
        lowCut.setTooltip(originalSound ? "Original sound preserved. Zoom > Use latest sound enables the new body filter."
                                        : "Remove low frequencies from the vowel body to leave room for the clean sub.");
        close.setEnabled(callbacks.requestClose && (! callbacks.canClose || callbacks.canClose()));
        if (auto* editor = findParentComponentOfClass<YoiBoiEditor>())
            zoom.setButtonText("ZOOM " + juce::String(juce::roundToInt(editor->getWidth() * 100.0 / shellWidth)) + "%");
        updateExclusionIndicators(*this);
        oneShot.setToggleState(readActual(state, "loop", 0) < 0.5f, juce::dontSendNotification);
        loop.setToggleState(! oneShot.getToggleState(), juce::dontSendNotification);
        allLocked = true;
        for (int i = 0; i < 6; ++i)
        {
            const auto locked = callbacks.isLocked ? callbacks.isLocked(i) : fallbackLocks[static_cast<size_t>(i)];
            macros[static_cast<size_t>(i)]->lock.setToggleState(locked, juce::dontSendNotification);
            allLocked = allLocked && locked;
        }
        if (callbacks.isParameterLocked)
        {
            allLocked = true;
            for (const auto* id : { "mouth", "size", "bite", "dirt", "sub", "length_ms", "length_division",
                                   "start_pose", "body_pose", "end_pose", "body_time", "curve", "fmRatio", "fmIndex", "resonance" })
                allLocked = allLocked && callbacks.isParameterLocked(id);
        }
        newYoi.setEnabled(! allLocked && static_cast<bool>(callbacks.newYoi));
        newYoi.setButtonText(allLocked ? "UNLOCK FIRST" : "NEW YOI");
        mutate.setButtonText(allLocked ? "UNLOCK FIRST" : "MUTATE");
        const auto mouthLocked = callbacks.isLocked ? callbacks.isLocked(0) : fallbackLocks[0];
        const auto lengthLocked = callbacks.isLocked ? callbacks.isLocked(4) : fallbackLocks[4];
        motionLock.setEnabled(static_cast<bool>(callbacks.setLock));
        motionLock.setToggleState(mouthLocked && lengthLocked, juce::dontSendNotification);
        motionLock.setButtonText(mouthLocked != lengthLocked ? "MIXED" : mouthLocked ? "LOCKED" : "LOCK");
        mutate.setEnabled(! allLocked && static_cast<bool>(callbacks.mutate));
        undo.setEnabled(callbacks.undo && (! callbacks.canUndo || callbacks.canUndo()));
        redo.setEnabled(callbacks.redo && (! callbacks.canRedo || callbacks.canRedo()));
        favorite.setEnabled(static_cast<bool>(callbacks.toggleFavorite));
        const auto fav = callbacks.isFavorite && callbacks.isFavorite();
        favorite.setButtonText(fav ? "FAVORITED" : "FAVORITE");
        favorite.setToggleState(fav, juce::dontSendNotification);
        preview.setEnabled(callbacks.togglePreview && (! callbacks.externalNotesHeld || ! callbacks.externalNotesHeld()));
        const auto playing = callbacks.previewing && callbacks.previewing();
        preview.setButtonText(playing ? "STOP" : "PREVIEW");
        preview.setToggleState(playing, juce::dontSendNotification);
        save.setEnabled(static_cast<bool>(callbacks.savePreset));
        load.setEnabled(callbacks.loadPreset && library.getSelectedRow() >= 0 && library.getSelectedRow() < static_cast<int>(filtered.size()));
        family.setEnabled(static_cast<bool>(callbacks.setGenerationFamily));
        if (callbacks.generationFamily && ! family.isPopupActive())
        {
            const auto name = callbacks.generationFamily();
            for (int i = 0; i < family.getNumItems(); ++i)
                if (family.getItemText(i).equalsIgnoreCase(name)) family.setSelectedItemIndex(i, juce::dontSendNotification);
        }
        midiLit = callbacks.midiActive && callbacks.midiActive();
        meterL = callbacks.outputLevelLeft ? juce::jmax(callbacks.outputLevelLeft(), meterL * 0.84f) : 0;
        meterR = callbacks.outputLevelRight ? juce::jmax(callbacks.outputLevelRight(), meterR * 0.84f) : meterL;
        gesture.phase = callbacks.gesturePhase ? callbacks.gesturePhase() : -1;
        if (juce::Time::getMillisecondCounterHiRes() > statusUntil)
            status = callbacks.statusText ? callbacks.statusText() : "LOCKS KEEP WHAT YOU LIKE";
        if (page == 1) syncShapeInspector();
        gesture.repaint(); repaint();
    }
    juce::String poseId() const { return gesture.selected == 0 ? "start_pose" : gesture.selected == 1 ? "body_pose" : "end_pose"; }
    void updateExclusionIndicators(juce::Component& component)
    {
        auto id = component.getProperties().getWithDefault("parameterId", juce::String()).toString();
        if (&component == &pose) id = poseId();
        if (id.isNotEmpty())
        {
            const bool excluded = callbacks.isParameterLocked && callbacks.isParameterLocked(id);
            if (static_cast<bool>(component.getProperties().getWithDefault("randomExcluded", false)) != excluded)
            {
                component.getProperties().set("randomExcluded", excluded);
                component.repaint();
            }
        }
        for (auto* child : component.getChildren()) updateExclusionIndicators(*child);
    }
    void syncShapeInspector()
    {
        if (pose.isPopupActive()) return;
        const juce::ScopedValueSetter<bool> scoped(updatingInspector, true);
        pose.setSelectedId(1 + juce::jlimit(0, 4, juce::roundToInt(readNormalised(state, poseId(), 0) * 4)), juce::dontSendNotification);
    }

    void refreshLibrary()
    {
        const auto oldIndex = library.getSelectedRow();
        const auto oldId = oldIndex >= 0 && oldIndex < static_cast<int>(filtered.size()) ? filtered[static_cast<size_t>(oldIndex)].id : juce::String();
        auto items = callbacks.listPresets ? callbacks.listPresets() : std::vector<PresetItem>();
        filtered.clear();
        auto query = search.getText().trim();
        for (auto& p : items)
        {
            if (activeFilter == 1 && ! p.favorite) continue;
            if (activeFilter == 2 && ! p.user) continue;
            if (activeFilter >= 3 && ! p.family.equalsIgnoreCase(libraryFilters[static_cast<size_t>(activeFilter)].getButtonText())) continue;
            if (query.isNotEmpty() && ! (p.name + " " + p.family).containsIgnoreCase(query)) continue;
            filtered.push_back(std::move(p));
        }
        library.updateContent();
        library.deselectAllRows();
        for (int i = 0; i < static_cast<int>(filtered.size()); ++i)
            if (filtered[static_cast<size_t>(i)].id == oldId) library.selectRow(i);
        for (int i = 0; i < 7; ++i) libraryFilters[static_cast<size_t>(i)].setToggleState(i == activeFilter, juce::dontSendNotification);
        library.repaint(); repaint();
    }
    int getNumRows() override { return static_cast<int>(filtered.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= static_cast<int>(filtered.size())) return;
        const auto& p = filtered[static_cast<size_t>(row)];
        if (selected) { g.setColour(cyan.withAlpha(0.18f)); g.fillRoundedRectangle(0, 1, static_cast<float>(width), height - 2.0f, 5); }
        g.setColour(cream.withAlpha(0.08f)); g.drawHorizontalLine(height - 1, 0, static_cast<float>(width));
        text(g, p.name, { 10, 0, width - 177, height }, 18, selected ? cyan : cream, selected);
        if (p.favorite) { g.setColour(lime); g.fillEllipse(width - 165.0f, height * 0.5f - 3, 6, 6); }
        text(g, p.family.toUpperCase(), { width - 146, 0, 83, height }, 13, cream.withAlpha(0.82f), true);
        text(g, p.user ? "SAVED" : "FACTORY", { width - 65, 0, 63, height }, 10, cream.withAlpha(0.52f));
    }
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override { loadSelected(); }
    void returnKeyPressed(int) override { loadSelected(); }
    void loadSelected()
    {
        const auto selected = library.getSelectedRow();
        if (callbacks.loadPreset && selected >= 0 && selected < static_cast<int>(filtered.size()))
        {
            const auto item = filtered[static_cast<size_t>(selected)];
            callbacks.loadPreset(item.id); toast("Loaded " + item.name); refreshLibrary();
        }
    }
    void showSaveDialog()
    {
        if (! callbacks.savePreset) return;
        auto* dialog = new juce::AlertWindow("Save preset", "Name this YOI so you can find it again.", juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor("name", patchName, "Name");
        dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        dialog->enterModalState(true, juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<Impl>(this), dialog](int result) {
            if (safe != nullptr && result == 1)
            {
                auto name = dialog->getTextEditorContents("name").trim().substring(0, 64);
                if (name.isNotEmpty() && safe->callbacks.savePreset)
                {
                    const bool saved = safe->callbacks.savePreset(name);
                    safe->toast(saved ? juce::String("Preset saved")
                                      : safe->callbacks.statusText ? safe->callbacks.statusText() : juce::String("Could not save preset"));
                    safe->refreshLibrary();
                }
                else if (name.isEmpty()) safe->toast("Enter a preset name");
            }
        }), true);
    }

    juce::AudioProcessorValueTreeState& state;
    EditorCallbacks callbacks;
    juce::Image shellArtwork;
    std::unique_ptr<vfd::InfoButton> infoButton;
    std::unique_ptr<vfd::InfoOverlay> infoOverlay;
    Theme theme;
    std::array<juce::TextButton, 3> tabs;
    juce::TextButton newYoi, mutate, undo, redo, favorite, preview, zoom, close;
    ParameterButton oneShot, loop, sync, lowCut;
    ParameterCombo subMode, lengthFeel;
    juce::ComboBox family;
    std::array<std::unique_ptr<Macro>, 6> macros;
    std::array<std::unique_ptr<SliderAttachment>, 6> macroAttachments;
    std::unique_ptr<SliderAttachment> glideAttachment, outputAttachment, bodyAttachment, curveAttachment;
    FocusedSlider glide, output, bodyTime, curve;
    GestureCanvas gesture;
    juce::ComboBox selectedPoint;
    ParameterCombo pose;
    juce::TextButton snap, resetShape;
    juce::TextButton motionLock;
    std::array<juce::TextButton, 7> libraryFilters;
    juce::TextEditor search;
    juce::ListBox library;
    juce::TextButton load, save;
    juce::TooltipWindow tooltip;
    std::vector<PresetItem> filtered;
    std::array<bool, 6> fallbackLocks { false, false, false, false, true, true };
    juce::String patchName, familyText, status;
    double statusUntil = 0;
    float meterL = 0, meterR = 0;
    int page = 0, activeFilter = 0, lastFeel = -1;
    bool lengthAttached = false, lastSync = true, allLocked = false, updatingInspector = false, midiLit = false;
};

YoiBoiEditor::YoiBoiEditor(juce::AudioProcessor& ownerProcessor, juce::AudioProcessorValueTreeState& state, EditorCallbacks callbacks)
    : juce::AudioProcessorEditor(ownerProcessor)
{
    const juce::Component::SafePointer<YoiBoiEditor> safe(this);
    if (! callbacks.requestClose) callbacks.requestClose = [safe] { if (safe != nullptr) closeEditorWindow(*safe); };
    if (! callbacks.canClose) callbacks.canClose = [safe] { return safe != nullptr && canCloseEditorWindow(*safe); };
    if (! callbacks.audioSettings && juce::JUCEApplicationBase::isStandaloneApp())
        callbacks.audioSettings = [safe] {
            if (safe != nullptr)
                if (auto* window = deviceWindowFor(*safe)) window->showDeviceAudioSettings();
        };
    impl = std::make_unique<Impl>(state, std::move(callbacks));
    addAndMakeVisible(*impl);
    setOpaque(false);
    setResizable(true, false);
    setResizeLimits(shellWidth, shellHeight, shellWidth * 3 / 2, shellHeight * 3 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(static_cast<double>(shellWidth) / shellHeight);
    setSize(shellWidth, shellHeight);
}

YoiBoiEditor::~YoiBoiEditor() = default;
void YoiBoiEditor::paint(juce::Graphics&) {}
void YoiBoiEditor::resized()
{
    if (impl == nullptr) return;
    const auto scale = juce::jmin(getWidth() / static_cast<float>(shellWidth), getHeight() / static_cast<float>(shellHeight));
    impl->setBounds(0, 0, designWidth, designHeight);
    impl->setTransform(juce::AffineTransform::scale(scale).translated((getWidth() - shellWidth * scale) * 0.5f - shellX * scale,
                                                                    (getHeight() - shellHeight * scale) * 0.5f - shellY * scale));
}
}
