#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <array>

// Shared visual identity only. This component never reads or changes audio state.
namespace vfd {

struct Theme {
    juce::Colour panel, text, accent, line, muted, keyTop, keyBottom, keyInk, socket;
};

inline Theme themeFor(const juce::String& name)
{
    const auto c = [](juce::uint32 rgb) { return juce::Colour(0xff000000u | rgb); };
    if (name.containsIgnoreCase("BACK"))
        return { c(0x210d12), c(0xf6e8d7), c(0xb3edf2), c(0x714047), c(0xd8bdba),
                 c(0xfff5e8), c(0xc5aaca), c(0x542435), c(0x451321) };
    if (name.containsIgnoreCase("THROW"))
        return { c(0x211307), c(0xfff0d8), c(0xffcf83), c(0x674625), c(0xd6c3ac),
                 c(0xfff6cf), c(0xc6a793), c(0x694325), c(0x703514) };
    if (name.containsIgnoreCase("SHIFTY"))
        return { c(0x09281d), c(0xd7ffee), c(0xc3ff62), c(0x42765b), c(0xb7d3c5),
                 c(0xf1ffe4), c(0x9bc5c3), c(0x245744), c(0x16533a) };
    return { c(0x18152e), c(0xf1edff), c(0x8ff1ff), c(0x4e496a), c(0xccc5df),
             c(0xf9f2ff), c(0xb8accf), c(0x494064), c(0x403453) };
}

inline juce::Font font(juce::Typeface::Ptr typeface, float height)
{
    return juce::Font(typeface != nullptr ? juce::FontOptions(typeface).withHeight(height)
                                          : juce::FontOptions(height));
}

class InfoButton final : public juce::Button {
public:
    InfoButton(Theme colours, juce::Typeface::Ptr typeface)
        : Button("INFO"), theme(colours), face(std::move(typeface))
    {
        setButtonText("INFO");
        setTitle("About Vivid Fever Dreams");
        setDescription("Open the plugin credits, music and support links");
        setTooltip("About Vivid Fever Dreams");
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        // A recessed socket, a dark travel gap, and a raised resin cap make the
        // control read as part of the hardware even before hover feedback.
        const auto scale = juce::jmin(getWidth() / 96.0f, getHeight() / 38.0f);
        g.addTransform(juce::AffineTransform::scale(scale).translated(
            (getWidth() - 96.0f * scale) * 0.5f, (getHeight() - 38.0f * scale) * 0.5f));
        const juce::Rectangle<float> socket(1, 1, 94, 36);
        g.setColour(juce::Colours::black.withAlpha(0.44f));
        g.fillRoundedRectangle(socket.translated(0, 1), 11);
        g.setGradientFill(juce::ColourGradient(theme.socket.darker(0.5f), 0, 1,
                                              theme.socket.brighter(0.32f), 0, 37, false));
        g.fillRoundedRectangle(socket, 10);
        g.setColour(theme.accent.withAlpha(0.25f));
        g.drawRoundedRectangle(socket.reduced(0.5f), 10, 0.8f);
        g.setColour(juce::Colours::black.withAlpha(0.73f));
        g.fillRoundedRectangle({ 5, 4, 86, 29 }, 8);
        auto cap = juce::Rectangle<float>(7, down ? 7.0f : 5.0f, 82, 26);
        if (!down) {
            g.setColour(theme.keyBottom.darker(0.7f));
            g.fillRoundedRectangle(cap.translated(0, 3), 7);
        }
        const auto top = over ? theme.keyTop.brighter(0.12f) : theme.keyTop;
        g.setGradientFill(juce::ColourGradient(top, 0, cap.getY(), theme.keyBottom,
                                              0, cap.getBottom(), false));
        g.fillRoundedRectangle(cap, 7);
        g.setColour(juce::Colours::white.withAlpha(down ? 0.30f : 0.75f));
        g.drawRoundedRectangle(cap.reduced(0.65f), 6.2f, 0.8f);
        g.setColour(theme.accent.withAlpha(0.24f));
        g.drawHorizontalLine(static_cast<int>(cap.getBottom() - 2), 15, 81);
        g.setFont(font(face, 12.5f).withExtraKerningFactor(0.16f));
        g.setColour(theme.keyInk);
        g.drawText("INFO", cap.translated(0, -0.3f), juce::Justification::centred, false);
        if (hasKeyboardFocus(true)) {
            g.setColour(theme.accent);
            g.drawRoundedRectangle(socket.reduced(0.5f), 10, 1.5f);
        }
    }

private:
    Theme theme;
    juce::Typeface::Ptr face;
};

class InfoOverlay final : public juce::Component {
    class CloseButton final : public juce::Button {
    public:
        explicit CloseButton(Theme colours) : Button("Close info"), theme(colours)
        {
            setTitle("Close info");
            setTooltip("Close info (Escape)");
            setWantsKeyboardFocus(true);
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }
        void paintButton(juce::Graphics& g, bool over, bool down) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced(1);
            g.setColour(theme.text.withAlpha(down ? 0.16f : over ? 0.11f : 0.04f));
            g.fillRoundedRectangle(bounds, 8);
            g.setColour(hasKeyboardFocus(true) ? theme.accent : theme.line);
            g.drawRoundedRectangle(bounds, 8, 1);
            g.setColour(theme.text);
            g.drawLine(12, 12, 20, 20, 1.5f);
            g.drawLine(20, 12, 12, 20, 1.5f);
        }
    private:
        Theme theme;
    };

    class Link final : public juce::HyperlinkButton {
    public:
        Link(const juce::String& label, const juce::String& address, Theme colours,
             juce::Typeface::Ptr typeface, float size)
            : HyperlinkButton(label, juce::URL(address)), theme(colours), face(std::move(typeface)), height(size)
        {
            setTooltip(address);
            setDescription("Opens " + address + " in your browser");
            setWantsKeyboardFocus(true);
        }
        void paintButton(juce::Graphics& g, bool over, bool down) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced(1);
            if (over || down) {
                g.setColour(theme.accent.withAlpha(down ? 0.14f : 0.07f));
                g.fillRoundedRectangle(bounds, 5);
            }
            const auto textFont = vfd::font(face, height);
            g.setFont(textFont);
            g.setColour(over ? theme.accent.brighter(0.15f) : theme.accent);
            g.drawText(getButtonText(), bounds, juce::Justification::centred, false);
            const auto textWidth = juce::GlyphArrangement::getStringWidth(textFont, getButtonText());
            const float start = juce::jmax(4.0f, (getWidth() - textWidth) * 0.5f);
            const float y = (getHeight() + height) * 0.5f - 1.0f;
            g.setColour(theme.accent.withAlpha(over ? 0.9f : 0.50f));
            g.drawLine(start, y, getWidth() - start, y, 0.7f);
            if (hasKeyboardFocus(true)) {
                g.setColour(theme.accent);
                g.drawRoundedRectangle(bounds, 5, 1);
            }
        }
    private:
        Theme theme;
        juce::Typeface::Ptr face;
        float height;
    };

    class Panel final : public juce::Component {
    public:
        Panel(juce::String plugin, Theme colours, const char* svg, size_t svgSize,
              juce::Typeface::Ptr typeface, std::function<void()> closeAction)
            : theme(colours), face(std::move(typeface)), close(theme),
              cxy("@cxy", "https://x.com/cxy", theme, face, 19),
              clt("@clt", "https://x.com/clt", theme, face, 19),
              music("vividfeverdreams.xyz", "https://www.vividfeverdreams.xyz", theme, face, 17),
              donate("Dream Sequence on Fractured Atlas",
                     "https://fundraising.fracturedatlas.org/dream-sequence", theme, face, 16)
        {
            setName("Credits and support");
            logo = juce::Drawable::createFromImageData(svg, svgSize);
            if (logo != nullptr) logo->replaceColour(juce::Colours::black, theme.text);
            addAndMakeVisible(close);
            close.onClick = std::move(closeAction);
            int order = 1;
            for (auto* button : focusOrder()) {
                addAndMakeVisible(button);
                button->setExplicitFocusOrder(order++);
            }
            setup(about, "ABOUT " + plugin, 11, theme.muted, true);
            about.setJustificationType(juce::Justification::centredLeft);
            setup(title, "Vivid Fever Dreams", 26, theme.text);
            setup(credit, "This plugin was developed by Vivid Fever Dreams with use of artificial intelligence coding tools.",
                  14, theme.muted);
            setup(producers, "EXECUTIVE PRODUCERS", 11, theme.muted, true);
            setup(support, "SUPPORT THE WORK", 11, theme.muted, true);
            setup(musicCopy, "If you want to support me, check out my music.", 14, theme.text);
            setup(donateCopy, "You can also donate to my live interactive visual project.", 14, theme.text);
        }

        std::array<juce::Button*, 5> focusOrder() { return { &close, &cxy, &clt, &music, &donate }; }

        void resized() override
        {
            about.setBounds(28, 23, 370, 28);
            close.setBounds(420, 22, 32, 32);
            title.setBounds(28, 203, 424, 35);
            credit.setBounds(42, 247, 396, 47);
            producers.setBounds(28, 323, 424, 20);
            cxy.setBounds(151, 349, 80, 32);
            clt.setBounds(249, 349, 80, 32);
            support.setBounds(28, 410, 424, 20);
            musicCopy.setBounds(28, 439, 424, 26);
            music.setBounds(122, 467, 236, 30);
            donateCopy.setBounds(28, 509, 424, 28);
            donate.setBounds(60, 538, 360, 32);
        }

        void paint(juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced(0.7f);
            g.setColour(theme.panel);
            g.fillRoundedRectangle(bounds, 18);
            g.setColour(theme.line);
            g.drawRoundedRectangle(bounds, 18, 1);
            g.setColour(theme.text.withAlpha(0.06f));
            g.drawHorizontalLine(1, 26, 454);
            if (logo != nullptr)
                logo->drawWithin(g, { 176, 70, 128, 120 }, juce::RectanglePlacement::centred, 1);
            g.setColour(theme.line.withAlpha(0.7f));
            g.drawHorizontalLine(308, 28, 452);
            g.drawHorizontalLine(396, 28, 452);
            g.setColour(theme.muted);
            g.fillEllipse(238.5f, 363.5f, 3, 3);
        }

    private:
        void setup(juce::Label& label, const juce::String& text, float size, juce::Colour colour, bool tracked = false)
        {
            label.setName(text);
            label.setText(text, juce::dontSendNotification);
            label.setFont(font(face, size).withExtraKerningFactor(tracked ? 0.13f : 0));
            label.setColour(juce::Label::textColourId, colour);
            label.setJustificationType(juce::Justification::centred);
            label.setMinimumHorizontalScale(1.0f);
            label.setBorderSize(juce::BorderSize<int>(0));
            label.setInterceptsMouseClicks(false, false);
            addAndMakeVisible(label);
        }
        Theme theme;
        juce::Typeface::Ptr face;
        std::unique_ptr<juce::Drawable> logo;
        CloseButton close;
        Link cxy, clt, music, donate;
        juce::Label about, title, credit, producers, support, musicCopy, donateCopy;
    };

public:
    InfoOverlay(juce::String plugin, Theme colours, const char* svg, size_t svgSize, juce::Typeface::Ptr typeface)
        : panel(std::move(plugin), colours, svg, svgSize, std::move(typeface), [this] { dismiss(); })
    {
        setName("VFD Info Page");
        setTitle("About Vivid Fever Dreams");
        setDescription("Plugin credits, executive producers, music and donations");
        setWantsKeyboardFocus(true);
        setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);
        setInterceptsMouseClicks(true, true);
        addAndMakeVisible(panel);
        setVisible(false);
    }

    void open(juce::Component* source)
    {
        returnFocus = source;
        setVisible(true);
        toFront(false);
        // The page covers the editor and contains keyboard focus without going modal: JUCE's modal state is
        // shared by every instance of a plug-in, so a modal page here would block the other open instances.
        // Editors can also be rendered without a peer by host validation tools.
        if (getPeer() != nullptr) panel.focusOrder().front()->grabKeyboardFocus();
    }

    void dismiss()
    {
        setVisible(false);
        if (returnFocus != nullptr && returnFocus->isShowing() && returnFocus->getPeer() != nullptr)
            returnFocus->grabKeyboardFocus();
    }

    void resized() override
    {
        constexpr float width = 480, height = 595;
        const auto scale = juce::jmin(1.0f, juce::jmin((getWidth() - 28.0f) / width, (getHeight() - 28.0f) / height));
        panel.setBounds(0, 0, static_cast<int>(width), static_cast<int>(height));
        // Whole-pixel offsets keep the page's text and hairlines crisp on 1x displays.
        panel.setTransform(juce::AffineTransform::scale(juce::jmax(0.1f, scale))
            .translated(std::round((getWidth() - width * scale) * 0.5f), std::round((getHeight() - height * scale) * 0.5f)));
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black.withAlpha(0.68f)); }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.eventComponent == this) dismiss();
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override
    {
        // Wheel events otherwise bubble through the page to the editor beneath it.
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey) { dismiss(); return true; }
        if (key == juce::KeyPress::tabKey) {
            const auto buttons = panel.focusOrder();
            int current = -1;
            for (int i = 0; i < static_cast<int>(buttons.size()); ++i)
                if (buttons[static_cast<size_t>(i)]->hasKeyboardFocus(true)) current = i;
            const int count = static_cast<int>(buttons.size());
            const bool backwards = key.getModifiers().isShiftDown();
            const int next = current < 0 ? (backwards ? count - 1 : 0) : (current + (backwards ? count - 1 : 1)) % count;
            if (getPeer() != nullptr) buttons[static_cast<size_t>(next)]->grabKeyboardFocus();
            return true;
        }
        // Buttons handle their own activation first. Command shortcuts (save, quit, ...) go back to the host so
        // they still work while the page is open; Undo/Redo and every other key stay here so they can't change
        // the sound or presets under the page.
        const auto modifiers = key.getModifiers();
        const bool undoRedo = modifiers.isCommandDown() && (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z');
        return undoRedo || !modifiers.isCommandDown();
    }

private:
    Panel panel;
    juce::Component::SafePointer<juce::Component> returnFocus;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InfoOverlay)
};

} // namespace vfd
