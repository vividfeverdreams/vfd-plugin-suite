#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include "VfdUpdate.h"
#include "VfdHostKeys.h"

// Shared visual identity, credits and suite updates. These components never read or change audio state.
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
    if (name.containsIgnoreCase("STUTTER"))
        return { c(0x071d1f), c(0xddf8f2), c(0x5fe8d8), c(0x2c5c5e), c(0xa6cdc8),
                 c(0xf3fffb), c(0xa9d6d0), c(0x164244), c(0x0f4446) };
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

class InfoButton final : public juce::Button, private juce::ChangeListener, private juce::Timer {
public:
    InfoButton(Theme colours, juce::Typeface::Ptr typeface)
        : Button("INFO"), theme(colours), face(std::move(typeface))
    {
        setButtonText("INFO");
        setTitle("About Vivid Fever Dreams");
        setDescription("Open the plugin credits, music and support links");
        setTooltip("About Vivid Fever Dreams");
        setWantsKeyboardFocus(false); // keys belong to the host (VfdHostKeys.h)
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        updates->addChangeListener(this);
        changeListenerCallback(nullptr);
        startTimer(1500); // look for a newer suite once the editor is on screen
    }

    ~InfoButton() override { updates->removeChangeListener(this); }

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
        if (updateWaiting) {
            // A newer suite is out: a small lit LED in the key's corner.
            const juce::Point<float> led { cap.getRight() - 9.5f, cap.getY() + 7.5f };
            g.setGradientFill(juce::ColourGradient(theme.accent.withAlpha(0.7f), led, theme.accent.withAlpha(0.0f),
                                                   led.translated(8.0f, 0), true));
            g.fillEllipse(juce::Rectangle<float>(16.0f, 16.0f).withCentre(led));
            g.setColour(theme.accent.brighter(0.3f));
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(led));
            g.setColour(theme.keyInk.withAlpha(0.4f));
            g.drawEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(led), 0.6f);
        }
        if (hasKeyboardFocus(true)) {
            g.setColour(theme.accent);
            g.drawRoundedRectangle(socket.reduced(0.5f), 10, 1.5f);
        }
    }

    bool showsUpdate() const noexcept { return updateWaiting; }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        const bool waiting = updates->hasUpdate();
        if (waiting == updateWaiting) return;
        updateWaiting = waiting;
        setTooltip(waiting ? "About Vivid Fever Dreams. Suite " + updates->getRelease().version + " is available."
                           : juce::String("About Vivid Fever Dreams"));
        repaint();
    }

    void timerCallback() override
    {
        // Only editors on screen check, so host scans and offscreen tests stay offline.
        if (! isShowing() || getPeer() == nullptr) return;
        stopTimer();
        updates->checkAutomatically();
    }

    Theme theme;
    juce::Typeface::Ptr face;
    juce::SharedResourcePointer<update::Service> updates;
    bool updateWaiting = false;
};

class InfoOverlay final : public juce::Component {
    class CloseButton final : public juce::Button {
    public:
        explicit CloseButton(Theme colours) : Button("Close info"), theme(colours)
        {
            setTitle("Close info");
            setTooltip("Close info (Escape)");
            setWantsKeyboardFocus(false);
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
            setWantsKeyboardFocus(false);
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

    /** Suite version and updates, at the foot of the page. */
    class UpdateRow final : public juce::Component, private juce::ChangeListener {
        class ActionButton final : public juce::Button {
        public:
            ActionButton(Theme colours, juce::Typeface::Ptr typeface)
                : Button("Suite update"), theme(colours), face(std::move(typeface))
            {
                setWantsKeyboardFocus(false);
                setMouseCursor(juce::MouseCursor::PointingHandCursor);
            }
            bool primary = false;
            float progress = -1.0f; // download progress, or negative
            void paintButton(juce::Graphics& g, bool over, bool down) override
            {
                const auto bounds = getLocalBounds().toFloat().reduced(1);
                if (primary) {
                    g.setColour(theme.accent.withAlpha(down ? 0.75f : over ? 0.95f : 0.85f));
                    g.fillRoundedRectangle(bounds, 8);
                } else {
                    g.setColour(theme.text.withAlpha(down ? 0.16f : over ? 0.11f : 0.05f));
                    g.fillRoundedRectangle(bounds, 8);
                    if (progress >= 0) {
                        juce::Graphics::ScopedSaveState clip(g);
                        juce::Path pill;
                        pill.addRoundedRectangle(bounds, 8);
                        g.reduceClipRegion(pill);
                        g.setColour(theme.accent.withAlpha(0.28f));
                        g.fillRect(bounds.withWidth(bounds.getWidth() * juce::jlimit(0.0f, 1.0f, progress)));
                    }
                    g.setColour(hasKeyboardFocus(true) || over ? theme.accent : theme.line);
                    g.drawRoundedRectangle(bounds, 8, 1);
                }
                if (primary && hasKeyboardFocus(true)) {
                    g.setColour(theme.text);
                    g.drawRoundedRectangle(bounds.expanded(1.5f), 9, 1.2f);
                }
                g.setFont(font(face, 13).withExtraKerningFactor(0.08f));
                g.setColour(primary ? theme.panel : theme.accent);
                g.drawFittedText(getButtonText(), getLocalBounds().reduced(8, 0), juce::Justification::centred, 1, 0.8f);
            }
        private:
            Theme theme;
            juce::Typeface::Ptr face;
        };

    public:
        UpdateRow(Theme colours, juce::Typeface::Ptr typeface)
            : theme(colours), face(typeface), action(colours, typeface)
        {
            setName("Suite updates");
            addAndMakeVisible(action);
            action.onClick = [this] { act(); };
            updates->addChangeListener(this);
            changeListenerCallback(nullptr);
        }
        ~UpdateRow() override { updates->removeChangeListener(this); }

        juce::Button& button() noexcept { return action; }
        void checkAutomatically() { updates->checkAutomatically(); }

        // Centred like the page's other sections: the version as the heading, the key, then one line of status.
        void resized() override { action.setBounds((getWidth() - 214) / 2, 26, 214, 32); }

        void paint(juce::Graphics& g) override
        {
            g.setFont(font(face, 11).withExtraKerningFactor(0.13f));
            g.setColour(theme.muted);
            g.drawText("SUITE " + update::Service::installedVersion(), 0, 0, getWidth(), 20,
                       juce::Justification::centred, false);
            g.setFont(font(face, 13));
            g.setColour(status.stage == update::Service::Stage::failed ? theme.accent : theme.text.withAlpha(0.8f));
            g.drawFittedText(status.message, 0, 66, getWidth(), 32, juce::Justification::centredTop, 2, 0.9f);
        }

    private:
        using Stage = update::Service::Stage;

        void act()
        {
            switch (status.stage) {
                case Stage::available:
                    if (updates->getRelease().canInstall()) updates->install(); else updates->openReleasePage();
                    break;
                case Stage::opened: updates->showInstaller(); break;
                case Stage::failed:
                    if (status.installFailed) updates->openReleasePage(); else updates->checkNow();
                    break;
                case Stage::checking: case Stage::downloading: case Stage::verifying: break;
                case Stage::idle: case Stage::upToDate: updates->checkNow(); break;
            }
        }

        void changeListenerCallback(juce::ChangeBroadcaster*) override
        {
            status = updates->getStatus();
            const auto version = updates->getRelease().version;
            juce::String label = "CHECK FOR UPDATES";
            switch (status.stage) {
                case Stage::idle: break;
                case Stage::checking: label = "CHECKING..."; break;
                case Stage::upToDate: label = "CHECK AGAIN"; break;
                case Stage::available: label = (updates->getRelease().canInstall() ? "INSTALL " : "GET ") + version; break;
                case Stage::downloading: label = "DOWNLOADING " + juce::String(juce::roundToInt(status.progress * 100)) + "%"; break;
                case Stage::verifying: label = "VERIFYING..."; break;
                case Stage::opened: label = "SHOW INSTALLER"; break;
                case Stage::failed: label = status.installFailed ? "OPEN RELEASES PAGE" : "TRY AGAIN"; break;
            }
            if (status.message.isEmpty())
                status.message = "Updates come from the suite's GitHub releases page.";
            action.setButtonText(label);
            action.setTitle(label);
            action.setTooltip(status.stage == Stage::available ? status.message
                              : status.stage == Stage::opened ? juce::String("Show the downloaded installer")
                              : juce::String("Updates come from github.com/") + update::repository);
            action.primary = status.stage == Stage::available;
            action.progress = status.stage == Stage::downloading ? status.progress : -1.0f;
            action.repaint();
            repaint();
        }

        Theme theme;
        juce::Typeface::Ptr face;
        ActionButton action;
        update::Service::Status status;
        juce::SharedResourcePointer<update::Service> updates;
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
                     "https://fundraising.fracturedatlas.org/dream-sequence", theme, face, 16),
              updateRow(theme, face)
        {
            setName("Credits and support");
            logo = juce::Drawable::createFromImageData(svg, svgSize);
            if (logo != nullptr) logo->replaceColour(juce::Colours::black, theme.text);
            addAndMakeVisible(close);
            addAndMakeVisible(updateRow);
            close.onClick = std::move(closeAction);
            int order = 1;
            for (auto* button : focusOrder()) {
                if (button != &updateRow.button()) addAndMakeVisible(button); // that one lives in its row
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

        std::array<juce::Button*, 6> focusOrder() { return { &close, &cxy, &clt, &music, &donate, &updateRow.button() }; }
        UpdateRow& updates() noexcept { return updateRow; }

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
            updateRow.setBounds(28, 598, 424, 98);
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
            g.drawHorizontalLine(584, 28, 452);
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
        UpdateRow updateRow;
        juce::Label about, title, credit, producers, support, musicCopy, donateCopy;
    };

public:
    InfoOverlay(juce::String plugin, Theme colours, const char* svg, size_t svgSize, juce::Typeface::Ptr typeface)
        : panel(std::move(plugin), colours, svg, svgSize, std::move(typeface), [this] { dismiss(); })
    {
        setName("VFD Info Page");
        setTitle("About Vivid Fever Dreams");
        setDescription("Plugin credits, executive producers, music and donations");
        // While open the page holds the arrow keys so presets can't change under it; every other key still
        // reaches the host (VfdHostKeys.h).
        setWantsKeyboardFocus(true);
        HostKeys::allowFocus(*this);
        setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);
        setInterceptsMouseClicks(true, true);
        addAndMakeVisible(panel);
        setVisible(false);
    }

    void open(juce::Component* /*source*/)
    {
        setVisible(true);
        toFront(false);
        if (getPeer() != nullptr) panel.updates().checkAutomatically();
        // The page covers the editor without going modal: JUCE's modal state is shared by every instance of
        // a plug-in, so a modal page here would block the other open instances. Editors can also be rendered
        // without a peer by host validation tools.
        if (getPeer() != nullptr) grabKeyboardFocus();
    }

    void dismiss()
    {
        setVisible(false);
        // Back to the editor, so its arrow keys browse presets again; never to the INFO key, which would
        // then hold Return and Tab.
        // The page can sit inside one of the editor's panels, so look for the editor itself.
        auto* editor = HostKeys::findEditor(getParentComponent());
        if (editor == nullptr) editor = getParentComponent(); // an editor without HostKeys
        if (editor != nullptr && editor->getWantsKeyboardFocus() && editor->isShowing() && editor->getPeer() != nullptr)
            editor->grabKeyboardFocus();
        else
            giveAwayKeyboardFocus();
    }

    void resized() override
    {
        constexpr float width = 480, height = 708;
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
        // Plain arrows would step presets in the editor underneath; hold them while the page is open.
        const int code = key.getKeyCode();
        const bool arrow = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
                           || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
        if (arrow && !key.getModifiers().isAnyModifierKeyDown()) return true;
        // Space, Return, Tab, letters and every shortcut belong to the host.
        return false;
    }

private:
    Panel panel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InfoOverlay)
};

} // namespace vfd
