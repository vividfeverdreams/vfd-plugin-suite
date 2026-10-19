#include "ThrowUpEditor.h"
#include "Presets.h"
#include "BinaryData.h"
#include "VfdInfo.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
// Artwork coordinates are kept separate from the editor size so controls remain
// registered with the physical caps at every host-supported zoom level.
// The crop sits on the enclosure's own straight edges in Assets/DeviceShell-fullbleed.png (corners
// tightened to match), so the device meets the host window on every side.
constexpr int shellX = 46, shellY = 28, shellWidth = 1030, shellHeight = 1335;
// The physical INFO key on the lower board strip, in artwork coordinates.
const juce::Rectangle<float> infoKeyBounds { 486.1f, 1222.4f, 153.9f, 60.9f };
// Molded keys on the same strip, either side of INFO.
const juce::Rectangle<float> throwKeyBounds { 190.0f, 1222.4f, 140.0f, 60.9f };
const juce::Rectangle<float> freezeKeyBounds { 340.0f, 1222.4f, 136.0f, 60.9f };
const juce::Rectangle<float> safeKeyBounds { 650.0f, 1222.4f, 96.0f, 60.9f };
const juce::Rectangle<float> sendKeyBounds { 754.0f, 1222.4f, 96.0f, 60.9f };
constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
constexpr float endAngle = juce::MathConstants<float>::pi * 2.75f;
const juce::Colour cream { 0xffffeed3 }, amber { 0xffffca70 }, orange { 0xffff9d43 };
const juce::Colour muted { 0xffd6b28f }, panel { 0xff291209 }, ink { 0xff55240d };
const juce::Rectangle<float> bypassBounds { 186, 1113, 178, 56 };
// OUTPUT: a small knob in the footer plate's middle bay, under its name, with the value beside it.
const juce::Point<float> outputKnobCentre { 428.0f, 1140.0f };
constexpr float outputSocketRadius = 35.0f, outputArcRadius = 29.5f, outputCapRadius = 23.5f;
const juce::Rectangle<float> outputValueBounds { 471.0f, 1119.0f, 100.0f, 42.0f };
constexpr int baseWidth = 660;
constexpr int userItemBase = 1000;
const std::array<const char*, 4> pageNames { "ROOM", "THROW", "SPACE", "MIX" };

juce::Image loadShell()
{
    int size = 0;
    if (const auto* data = BinaryData::getNamedResource ("DeviceShellfullbleed_png", size))
        return juce::ImageCache::getFromMemory (data, size);
    return {};
}

juce::String milliseconds (double value)
{
    return value >= 1000.0 ? juce::String (value / 1000.0, 2) + " s" : juce::String (juce::roundToInt (value)) + " ms";
}

// The user presets the menu lists, in menu order: <root>/<Folder>/<Name>.throwup by
// folder (empty folders left out), then loose files in the root (folder name empty).
struct UserFolder { juce::String name; juce::Array<juce::File> files; };
std::vector<UserFolder> findUserPresets()
{
    std::vector<UserFolder> found;
    const auto presetRoot = ThrowUpProcessor::userPresetRoot();
    auto folders = presetRoot.findChildFiles (juce::File::findDirectories, false);
    folders.sort();
    for (const auto& folder : folders) {
        auto files = folder.findChildFiles (juce::File::findFiles, false, "*.throwup");
        files.sort();
        if (! files.isEmpty()) found.push_back ({ folder.getFileName(), files });
    }
    auto loose = presetRoot.findChildFiles (juce::File::findFiles, false, "*.throwup");
    loose.sort();
    if (! loose.isEmpty()) found.push_back ({ {}, loose });
    return found;
}

// The name a user preset file loads under (as loadUserPreset reads it).
juce::String userPresetNameOf (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
        return xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    return file.getFileNameWithoutExtension();
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
        setColour (juce::TextEditor::outlineColourId, amber.withAlpha (.5f));
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, cream);
        setColour (juce::AlertWindow::outlineColourId, amber);
        setColour (juce::ComboBox::backgroundColourId, panel);
        setColour (juce::ComboBox::textColourId, cream);
        setColour (juce::ComboBox::outlineColourId, amber.withAlpha (.5f));
        setColour (juce::ComboBox::arrowColourId, amber);
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
    juce::Font getAlertWindowMessageFont() override { return font (16.0f); }
    juce::Font getAlertWindowTitleFont() override { return font (20.0f, true); }
    juce::Font getTextButtonFont (juce::TextButton&, int height) override { return font ((float) height * .6f); }
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
        if (button.getProperties()["tab"]) {
            // Display page tabs: a lit underline marks the open page.
            if (button.getToggleState()) {
                g.setColour (amber.withAlpha (.16f));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (amber);
                g.fillRoundedRectangle (r.getX() + 4.0f, r.getBottom() - 3.0f, r.getWidth() - 8.0f, 2.5f, 1.2f);
            } else if (over) {
                g.setColour (amber.withAlpha (.08f));
                g.fillRoundedRectangle (r, 3.0f);
            }
            return;
        }
        const bool lit = button.getToggleState();
        g.setColour (down ? orange.withAlpha (.28f) : lit ? amber.withAlpha (.24f) : panel.withAlpha (over ? .8f : .45f));
        g.fillRoundedRectangle (r, r.getHeight() * .2f);
        g.setColour (amber.withAlpha (lit || over || button.hasKeyboardFocus (true) ? .85f : .25f));
        g.drawRoundedRectangle (r, r.getHeight() * .2f, lit ? 1.5f : 1.0f);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        auto r = button.getLocalBounds().toFloat();
        if (button.getProperties()["dice"]) {
            // The random-preset key: a die showing five, in the arrows' cream and at their glyph
            // height. It is laid out on a 12-unit grid (1 outline, 1 gap, 2 pip, ...) from a
            // whole-pixel corner, so at 100% and below it lands exactly on the pixel grid. Pips are
            // drawn a little over 2 units so a 2-pixel pip is as bright as the outline.
            const float side = std::round (juce::jmax (12.0f, r.getHeight() * .44f)), unit = side / 12.0f;
            const auto die = juce::Rectangle<float> (std::round (r.getCentreX() - side * .5f),
                                                     std::round (r.getCentreY() - side * .5f), side, side);
            const float stroke = unit, corner = 3.0f * unit;
            g.setColour (cream.withAlpha (.14f));
            g.fillRoundedRectangle (die, corner);
            g.setColour (cream);
            g.drawRoundedRectangle (die.reduced (stroke * .5f), corner - stroke * .5f, stroke);
            const float pip = 2.4f * unit, offset = 3.0f * unit;
            for (const auto& [dx, dy] : { std::pair (-1, -1), std::pair (1, -1), std::pair (0, 0), std::pair (-1, 1), std::pair (1, 1) })
                g.fillEllipse (juce::Rectangle<float> (pip, pip).withCentre (die.getCentre() + juce::Point<float> ((float) dx, (float) dy) * offset));
            return;
        }
        const bool isBypass = button.getName() == "Bypass";
        if (isBypass) {
            const float thumbWidth = r.getHeight();
            if (button.getToggleState()) r.removeFromRight (thumbWidth);
            else r.removeFromLeft (thumbWidth);
        }
        const bool tab = button.getProperties()["tab"];
        const float scale = isBypass ? .43f : tab ? .7f : button.getProperties()["small"] ? .56f : .78f;
        g.setFont (font ((float) button.getHeight() * scale));
        g.setColour (tab ? (button.getToggleState() ? amber : muted) : button.getToggleState() ? amber : cream);
        g.drawText (button.getButtonText(), r.reduced (2.0f, 0.0f), juce::Justification::centred);
    }
private:
    juce::Typeface::Ptr typeface, displayTypeface;
};

// The preset display: rebuilt each time it opens so new user presets appear.
class ThrowUpEditor::PresetBox final : public juce::ComboBox
{
public:
    std::function<void()> beforePopup;
    void showPopup() override
    {
        if (beforePopup) beforePopup();
        juce::ComboBox::showPopup();
    }
};

// A cream molded key on the lower board strip, matching the INFO key, with a status LED.
class ThrowUpEditor::KeyButton final : public juce::Button
{
public:
    KeyButton (const juce::String& name, juce::Typeface::Ptr face) : juce::Button (name), typeface (face)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
    }
    std::function<bool()> ledLit;          // overrides the toggle state for the LED
    std::function<void (bool)> onPress;     // momentary keys: called on press and release
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const float radius = r.getHeight() * .32f;
        g.setColour (juce::Colours::black.withAlpha (.35f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f), radius);
        const bool pressed = down || pressedNow;
        g.setGradientFill ({ pressed ? juce::Colour (0xffe9d2b2) : cream, r.getX(), r.getY(),
                             pressed ? juce::Colour (0xffc9a77c) : juce::Colour (0xffd8b98e), r.getX(), r.getBottom(), false });
        g.fillRoundedRectangle (r, radius);
        g.setColour (ink.withAlpha (over || hasKeyboardFocus (true) ? .95f : .6f));
        g.drawRoundedRectangle (r, radius, 1.6f);
        const bool lit = ledLit ? ledLit() : getToggleState();
        const float led = r.getHeight() * .2f;
        const juce::Rectangle<float> ledBounds { r.getX() + r.getHeight() * .32f, r.getCentreY() - led * .5f, led, led };
        if (lit) {
            g.setColour (orange.withAlpha (.45f));
            g.fillEllipse (ledBounds.expanded (led * .45f));
        }
        g.setColour (lit ? juce::Colour (0xffff6a2a) : ink.withAlpha (.35f));
        g.fillEllipse (ledBounds);
        g.setColour (ink);
        g.setFont (juce::Font (juce::FontOptions (typeface).withHeight (r.getHeight() * .36f)));
        g.drawText (getButtonText(), r.withTrimmedLeft (ledBounds.getRight() - r.getX() + 4.0f).withTrimmedRight (6.0f),
                    juce::Justification::centred, false);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        juce::Button::mouseDown (e);
        if (onPress && ! pressedNow) { pressedNow = true; onPress (true); repaint(); }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::Button::mouseUp (e);
        if (onPress && pressedNow) { pressedNow = false; onPress (false); repaint(); }
    }
private:
    juce::Typeface::Ptr typeface;
    bool pressedNow = false;
};

struct ThrowUpEditor::Control
{
    juce::String id, title;
    juce::Rectangle<float> hit, readout;
    bool physical = false, large = false;
    int page = -1; // -1: always visible (plate knobs, Output)
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
    struct ValueLabel final : juce::Label, private juce::Timer {
        explicit ValueLabel (juce::Slider& s) : slider (s) {}
        void mouseDown (const juce::MouseEvent& event) override {
            stopTimer();
            forwarding = event.getNumberOfClicks() == 1 && ! event.mods.isPopupMenu();
            if (forwarding) slider.mouseDown (event.getEventRelativeTo (&slider));
            else if (! resetOnDoubleClick) juce::Label::mouseDown (event);
        }
        void mouseDrag (const juce::MouseEvent& event) override {
            if (forwarding) slider.mouseDrag (event.getEventRelativeTo (&slider));
            else if (! resetOnDoubleClick) juce::Label::mouseDrag (event);
        }
        void mouseUp (const juce::MouseEvent& event) override {
            if (forwarding) {
                slider.mouseUp (event.getEventRelativeTo (&slider));
                // A box-style value (Output) opens text entry on a plain click,
                // once it is clear the click is not the start of a double-click.
                if (resetOnDoubleClick && ! event.mouseWasDraggedSinceMouseDown())
                    startTimer (juce::MouseEvent::getDoubleClickTimeout());
            }
            else if (! resetOnDoubleClick) juce::Label::mouseUp (event);
            forwarding = false;
        }
        void mouseDoubleClick (const juce::MouseEvent& event) override {
            if (! resetOnDoubleClick) { juce::Label::mouseDoubleClick (event); return; }
            stopTimer();
            slider.mouseDoubleClick (event.getEventRelativeTo (&slider));
        }
        void timerCallback() override {
            stopTimer();
            showEditor();
        }
        juce::Slider& slider;
        bool forwarding = false;
        // When the value label covers the whole control, double-click resets
        // like a knob and a single click types a value.
        bool resetOnDoubleClick = false;
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
    setOpaque (true);
    addControl ("mix", "THROW", "Adds pure reverb over your original bass. Dry stays at full level. 0% adds no reverb; 100% adds the full wet return. Automate THROW in your DAW, or use the THROW! key, MIDI or Auto-catch.",
                { 400, 616, 320, 320 }, { 445, 947, 230, 51 }, true, true);
    addControl ("size", "SIZE", "Changes the dimensions and spacing of the reverb room.",
                { 214, 567, 140, 140 }, { 194, 731, 180, 42 }, true);
    addControl ("decay", "DECAY", "Sets how long the reverb tail decays, in seconds.",
                { 770, 567, 140, 140 }, { 750, 731, 180, 42 }, true);
    addControl ("predelay", "PRE-DELAY", "Adds space between the dry hit and the reverb, in milliseconds. SPACE page: lock it to the host tempo.",
                { 214, 846, 140, 140 }, { 194, 993, 180, 33 }, true);
    addControl ("diffusion", "DIFFUSION", "From scattered reflections to a dense, smooth tail.",
                { 769, 846, 140, 140 }, { 749, 993, 180, 33 }, true);
    // ROOM page.
    addPageControl ("lowCut", "LOW CUT", "High-pass filter for the reverb only. With the latest sound the tail is 3 dB down at this frequency.", Page::room, 0);
    addPageControl ("highCut", "HIGH CUT", "Low-pass filter for the reverb only. Lower it to darken the tail.", Page::room, 1);
    addPageControl ("width", "WIDTH", "Stereo spread of the wet tail. 0% is mono, 100% is natural, 150% is extra wide.", Page::room, 2);
    addPageControl ("motion", "MOTION", "Adds gentle pitch movement inside the reverb tail.", Page::room, 3);
    addPageControl ("grit", "GRIT", "Saturates the reverb. With the latest sound, Grit sets a soft ceiling on the wet return: it gets dirtier, not louder.", Page::room, 4);
    addPageControl ("duck", "DUCK", "Pulls the wet tail down while the input (or the sidechain) plays, then lets it bloom into the gaps.", Page::room, 5);
    // THROW page.
    addPageControl ("trigger", "MODE", "Manual: THROW knob, automation and the THROW! key. MIDI: notes open the throw (velocity sets the level). Auto-catch: a throw opens when an input phrase ends.", Page::trigger, 0);
    addPageControl ("throwAmount", "AMOUNT", "THROW level reached by the THROW! key, MIDI velocity 127 and Auto-catch.", Page::trigger, 1);
    addPageControl ("throwAttack", "ATTACK", "Time for a triggered throw to rise to full.", Page::trigger, 2);
    addPageControl ("throwRelease", "RELEASE", "Time for a triggered throw to fall back to the THROW knob setting.", Page::trigger, 3);
    addPageControl ("releaseSync", "SYNC", "Locks the release to the host tempo instead of milliseconds.", Page::trigger, 4);
    addPageControl ("catchLevel", "CATCH", "Auto-catch threshold: a phrase louder than this that then stops for 60 ms opens a throw.", Page::trigger, 5);
    // SPACE page.
    addPageControl ("predelaySync", "PRE SYNC", "Locks the pre-delay to the host tempo (ms uses the PRE-DELAY knob).", Page::space, 0);
    addPageControl ("predelayFeel", "FEEL", "Straight, dotted or triplet synced pre-delay.", Page::space, 1);
    addPageControl ("swell", "SWELL", "A reverse-style swell: the reverb rises through each cycle and cuts on the grid line, into the drop.", Page::space, 2);
    addPageControl ("shimmer", "SHIMMER", "Feeds part of the tail through a pitch shifter, so the reverb climbs or falls in pitch.", Page::space, 3);
    addPageControl ("shimmerPitch", "PITCH", "Shimmer interval: an octave or a fourth down, a fifth or an octave up.", Page::space, 4);
    // MIX page.
    addPageControl ("returnTrim", "RETURN", "Trims the wet return only (the dry is untouched). Use it to tame loud throws.", Page::mix, 0);
    addPageControl ("duckSource", "DUCK SRC", "What Duck listens to: the dry input, or the external sidechain input (route a kick or snare to it in your DAW).", Page::mix, 1);
    addPageControl ("duckRelease", "DUCK REL", "How fast the tail returns after the ducking source stops.", Page::mix, 2);
    addPageControl ("duckThreshold", "DUCK THR", "Latest sound: the level above which Duck reaches its full depth.", Page::mix, 3);
    addControl ("output", "OUTPUT", "Final output level, from -18 to +6 dB.",
                juce::Rectangle<float> (2.0f * outputSocketRadius, 2.0f * outputSocketRadius).withCentre (outputKnobCentre),
                outputValueBounds, true);

    // Display page tabs.
    for (size_t i = 0; i < pageTabs.size(); ++i) {
        auto& tab = pageTabs[i];
        tab.setButtonText (pageNames[i]);
        tab.setName (juce::String ("Page ") + pageNames[i]);
        tab.getProperties().set ("tab", true);
        tab.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        tab.onClick = [this, i] { showPage (static_cast<Page> (i)); };
        addAndMakeVisible (tab);
    }
    pageTabs[0].setTooltip ("Room tone: filters, width, motion, grit and duck.");
    pageTabs[1].setTooltip ("THROW trigger: MIDI notes, Auto-catch, attack and release.");
    pageTabs[2].setTooltip ("Space effects: synced pre-delay, swell and shimmer.");
    pageTabs[3].setTooltip ("Mix: return trim, duck source and the sound version.");

    presets = std::make_unique<PresetBox>();
    presets->setName ("Factory presets");
    presets->setColour (juce::ComboBox::textColourId, cream);
    presets->setTooltip ("Factory rooms by folder, the original 0.1.5 bank, and your saved presets. Loading a preset preserves THROW, Output, Bypass and the performance keys.");
    presets->beforePopup = [this] { rebuildPresetMenu(); };
    presets->onChange = [this] {
        const int id = presets->getSelectedId();
        if (id > 0 && id <= pluginProcessor.getNumPrograms()) pluginProcessor.loadFactoryPreset (id - 1);
        updatePresetDisplay(); repaint();
    };
    addAndMakeVisible (*presets);
    rebuildPresetMenu();
    previous.setName ("Previous preset"); next.setName ("Next preset"); bypass.setName ("Bypass");
    previous.setTooltip ("Previous factory preset. THROW and Output stay where you set them.");
    next.setTooltip ("Next factory preset. THROW and Output stay where you set them.");
    previous.onClick = [this] { stepPreset (-1); };
    next.onClick = [this] { stepPreset (1); };
    // The dice key, first in the arrows' cluster. Its face is drawn as a die, so the
    // accessible name comes from the title.
    randomKey.setName ("Random preset");
    randomKey.setTitle ("Random preset");
    randomKey.getProperties().set ("dice", true);
    randomKey.setTooltip ("Random preset: loads a different room from the factory folders or your user presets "
                          "(on a 0.1.5 room, another 0.1.5 room). THROW and Output stay where you set them.");
    randomKey.onClick = [this] { loadRandomPreset(); };
    bypass.setClickingTogglesState (true);
    bypass.setTooltip ("Bypass the effect. The dry input passes through unchanged.");
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (pluginProcessor.apvts, "bypass", bypass);
    saveKey.setName ("Save preset");
    saveKey.setTooltip ("Save the current room as a user preset, in a folder you choose or create.");
    saveKey.onClick = [this] { openSaveDialog(); };
    slotA.setName ("Compare A"); slotB.setName ("Compare B");
    slotA.setTooltip ("A/B compare: slot A. Switching keeps THROW, Output and Bypass.");
    slotB.setTooltip ("A/B compare: slot B. The first switch copies A, so you can tweak B and compare.");
    slotA.onClick = [this] { pluginProcessor.selectSlot (0); updateKeys(); updatePresetDisplay(); repaint(); };
    slotB.onClick = [this] { pluginProcessor.selectSlot (1); updateKeys(); updatePresetDisplay(); repaint(); };
    noteRoomsKey.setName ("Note rooms");
    noteRoomsKey.setClickingTogglesState (true);
    noteRoomsKey.setTooltip ("MIDI mode: each note also picks a factory room (C1 = the first room).");
    keyAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (pluginProcessor.apvts, "noteRooms", noteRoomsKey));
    soundKey.setName ("Sound version");
    soundKey.setTooltip ("Switch between the 0.1.5 sound (kept for older projects) and the latest sound. Undoable.");
    soundKey.onClick = [this] {
        const bool toLatest = pluginProcessor.getSoundVersion() == throwup::SoundVersion::v015;
        auto* window = new juce::AlertWindow (toLatest ? "USE THE LATEST SOUND?" : "GO BACK TO THE 0.1.5 SOUND?",
            toLatest ? "This project plays with the 0.1.5 sound. The latest sound has an accurate low cut, real Grit saturation, "
                       "Duck in decibels and a recalibrated return, so it will sound different. You can switch back at any time, "
                       "and your DAW's undo reverses it."
                     : "The 0.1.5 tone, Grit, Duck and return level will be used again, exactly as 0.1.5 played them.",
            juce::MessageBoxIconType::NoIcon, this);
        window->addButton (toLatest ? "Use latest" : "Use 0.1.5", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        showDialog (window, [this, toLatest] (int result) {
            if (result != 1) return;
            pluginProcessor.setSoundVersion (toLatest ? throwup::SoundVersion::latest : throwup::SoundVersion::v015);
            updateKeys(); repaint();
        });
    };
    for (auto* button : { &previous, &next, &randomKey, &bypass, &saveKey, &slotA, &slotB, &noteRoomsKey, &soundKey }) {
        button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible (*button);
    }
    for (auto* button : { &saveKey, &slotA, &slotB, &noteRoomsKey, &soundKey })
        button->getProperties().set ("small", true);

    // Molded keys on the board strip.
    throwKey = std::make_unique<KeyButton> ("THROW!", normalTypeface);
    throwKey->setButtonText ("THROW!");
    throwKey->setTooltip ("Hold to throw: opens THROW to Amount with the THROW page's attack and release. Records as automation.");
    throwKey->ledLit = [this] { return pluginProcessor.getTriggerLevel() > 1.0f; };
    throwKey->onPress = [this] (bool down) {
        auto* parameter = pluginProcessor.apvts.getParameter ("throwKey");
        if (down) parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (down ? 1.0f : 0.0f);
        if (! down) parameter->endChangeGesture();
    };
    freezeKey = std::make_unique<KeyButton> ("FREEZE", normalTypeface);
    freezeKey->setButtonText ("FREEZE");
    freezeKey->setClickingTogglesState (true);
    freezeKey->setTooltip ("Holds the current tail forever and stops feeding it. Turn off to let it decay.");
    keyAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (pluginProcessor.apvts, "freeze", *freezeKey));
    safeKey = std::make_unique<KeyButton> ("SAFE", normalTypeface);
    safeKey->setButtonText ("SAFE");
    safeKey->setClickingTogglesState (true);
    safeKey->setTooltip ("Safe clip: a soft clip on the whole output (dry and wet) that never exceeds 0 dBFS. Off by default.");
    keyAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (pluginProcessor.apvts, "safeClip", *safeKey));
    sendKey = std::make_unique<KeyButton> ("SEND", normalTypeface);
    sendKey->setButtonText ("SEND");
    sendKey->setTooltip ("Send mode: removes the dry signal, for a return track. THROW then sets the reverb level.");
    sendKey->ledLit = [this] { return pluginProcessor.apvts.getRawParameterValue ("dry")->load() < .5f; };
    sendKey->onClick = [this] {
        auto* parameter = pluginProcessor.apvts.getParameter ("dry");
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getValue() >= .5f ? 0.0f : 1.0f);
        parameter->endChangeGesture();
        sendKey->repaint();
    };
    for (auto* key : { throwKey.get(), freezeKey.get(), safeKey.get(), sendKey.get() })
        addAndMakeVisible (*key);

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
    setResizeLimits (528, 528 * shellHeight / shellWidth, 990, 990 * shellHeight / shellWidth);
    if (auto* constrainer = getConstrainer()) constrainer->setFixedAspectRatio ((double) shellWidth / shellHeight);
    juce::Rectangle<int> userArea { 0, 0, 1920, 1080 };
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        userArea = display->userBounds.toNearestInt();
    const float scale = initialScale (pluginProcessor.getEditorScale(), userArea);
    setSize (juce::roundToInt (baseWidth * scale), juce::roundToInt (baseWidth * scale * shellHeight / shellWidth));
    sizeReady = true;
    showPage (Page::room);
    updateKeys();
    startTimerHz (24);
}

ThrowUpEditor::~ThrowUpEditor()
{
    stopTimer();
    if (dialog != nullptr) {
        // The dialog uses this editor's look-and-feel: detach and close it first.
        dialog->setLookAndFeel (nullptr);
        dialog->exitModalState (0);
    }
    bypassAttachment.reset();
    keyAttachments.clear();
    controls.clear();
    setLookAndFeel (nullptr);
}

juce::String ThrowUpEditor::valueText (const juce::String& id, double value) const
{
    if (auto* choiceParameter = dynamic_cast<juce::AudioParameterChoice*> (pluginProcessor.apvts.getParameter (id)))
        return choiceParameter->choices[juce::jlimit (0, choiceParameter->choices.size() - 1, juce::roundToInt (value))];
    if (id == "decay") return juce::String (value, 2) + " s";
    if (id == "predelay") return juce::String (value, 1) + " ms";
    if (id == "lowCut" || id == "highCut")
        return value >= 1000.0 ? juce::String (value / 1000.0, 2) + " kHz"
                               : juce::String (juce::roundToInt (value)) + " Hz";
    if (id == "output") return juce::String (value, 1) + " dB";
    if (id == "returnTrim") return (value > 0.05 ? "+" : "") + juce::String (value, 1) + " dB";
    if (id == "catchLevel" || id == "duckThreshold") return juce::String (value, 1) + " dB";
    if (id == "throwAttack" || id == "throwRelease" || id == "duckRelease") return milliseconds (value);
    return juce::String (juce::roundToInt (value)) + "%";
}

void ThrowUpEditor::addPageControl (const char* id, const char* title, const char* tooltip, Page controlPage, int slot)
{
    const float x = slot % 3 == 0 ? 188.0f : slot % 3 == 1 ? 448.0f : 708.0f;
    const float y = slot < 3 ? 274.0f : 362.0f;
    // Hit areas end just below each bar, leaving the display's bottom row free for page keys.
    addControl (id, title, tooltip, { x, y, 222, 58 }, { x + 90.0f, y, 132, 34 }, false, false, static_cast<int> (controlPage));
}

void ThrowUpEditor::addControl (const char* id, const char* title, const char* tooltip,
                                juce::Rectangle<float> hit, juce::Rectangle<float> readout,
                                bool physical, bool large, int controlPage)
{
    auto c = std::make_unique<Control>();
    c->id = id; c->title = title; c->physical = physical; c->large = large; c->page = controlPage;
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
    c->value.setJustificationType (c->id == "output" ? juce::Justification::centredLeft
                                   : physical ? juce::Justification::centred : juce::Justification::centredRight);
    c->value.setBorderSize ({ 0, 0, 0, 0 });
    c->value.setColour (juce::Label::textColourId, physical || c->id == "output" ? ink : cream);
    c->value.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    c->value.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    c->value.setColour (juce::Label::textWhenEditingColourId, cream);
    c->value.setColour (juce::Label::backgroundWhenEditingColourId, panel);
    c->value.setColour (juce::Label::outlineWhenEditingColourId, amber);
    c->value.setEditable (false, true, false);
    c->value.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    // Output's value also works as a box: drag it, double-click to reset like the knob, click to type.
    c->value.resetOnDoubleClick = c->id == "output";
    c->value.setTooltip (c->value.resetOnDoubleClick
        ? "Drag to adjust. Hold Shift for fine adjustment. Double-click to reset to 0 dB; click to enter a value."
        : "Drag to adjust. Hold Shift for fine adjustment. Double-click to enter a value.");
    c->value.setText (valueText (id, c->slider.getValue()), juce::dontSendNotification);
    auto* control = c.get();
    c->slider.onValueChange = [this, control] {
        if (! control->value.isBeingEdited())
            control->value.setText (valueText (control->id, control->slider.getValue()), juce::dontSendNotification);
        repaint();
    };
    c->value.onEditorShow = [control] {
        if (auto* editor = control->value.getCurrentTextEditor()) {
            editor->setText (control->value.getText(), false);
            editor->selectAll();
        }
    };
    c->value.onTextChange = [this, control] {
        const auto typed = control->value.getText().trim();
        auto* parameter = pluginProcessor.apvts.getParameter (control->id);
        double v = typed.getDoubleValue();
        bool valid = std::isfinite (v) && typed.containsAnyOf ("0123456789");
        if (auto* choiceParameter = dynamic_cast<juce::AudioParameterChoice*> (parameter)) {
            // Choices accept their name ("1/8", "Dotted", "+12") or their number.
            const int index = choiceParameter->choices.indexOf (typed, true);
            valid = index >= 0;
            if (! valid)
                for (int i = 0; i < choiceParameter->choices.size() && ! valid; ++i)
                    if (choiceParameter->choices[i].startsWithIgnoreCase (typed) && typed.isNotEmpty()) { v = i; valid = true; }
            if (index >= 0) v = index;
        }
        if ((control->id == "lowCut" || control->id == "highCut") && typed.containsIgnoreCase ("k")) v *= 1000.0;
        if (valid) {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) juce::jlimit (control->slider.getMinimum(), control->slider.getMaximum(), v)));
            parameter->endChangeGesture();
        }
        control->value.setText (valueText (control->id, control->slider.getValue()), juce::dontSendNotification);
    };
    addAndMakeVisible (c->slider); addAndMakeVisible (c->value);
    controls.push_back (std::move (c));
}

void ThrowUpEditor::showPage (Page newPage)
{
    page = newPage;
    for (size_t i = 0; i < pageTabs.size(); ++i)
        pageTabs[i].setToggleState (static_cast<int> (i) == static_cast<int> (page), juce::dontSendNotification);
    for (const auto& c : controls) {
        const bool visible = c->page < 0 || c->page == static_cast<int> (page);
        c->slider.setVisible (visible);
        c->value.setVisible (visible);
    }
    noteRoomsKey.setVisible (page == Page::trigger);
    soundKey.setVisible (page == Page::mix);
    repaint();
}

void ThrowUpEditor::rebuildPresetMenu()
{
    auto* root = presets->getRootMenu();
    root->clear();
    const auto& bank = throwup::factoryPresets();
    const auto addPreset = [this] (juce::PopupMenu& menu, int index) {
        juce::PopupMenu::Item entry (throwup::factoryPresets()[(size_t) index].name);
        entry.setID (index + 1);
        // ComboBox only reports a changed ID. The item action also runs
        // when the current room is chosen again, which reverts its edits.
        entry.setAction ([safe = juce::Component::SafePointer<ThrowUpEditor> (this), index] {
            if (safe != nullptr) safe->selectFactoryPreset (index);
        });
        menu.addItem (std::move (entry));
    };
    for (const auto* folder : throwup::presetFolders()) {
        juce::PopupMenu submenu;
        for (int i = 0; i < throwup::legacyPresetStart(); ++i)
            if (juce::String (bank[(size_t) i].group) == folder) addPreset (submenu, i);
        root->addSubMenu (folder, submenu);
    }
    juce::PopupMenu legacy;
    for (const char* group : { "Small", "Medium", "Large", "Huge", "Creative" }) {
        juce::PopupMenu submenu;
        for (int i = throwup::legacyPresetStart(); i < (int) bank.size(); ++i)
            if (juce::String (bank[(size_t) i].group) == group) addPreset (submenu, i);
        legacy.addSubMenu (group, submenu);
    }
    root->addSeparator();
    root->addSubMenu ("0.1.5 rooms (original sound)", legacy);
    // User presets: <root>/<Folder>/<Name>.throwup, read fresh each time.
    userPresetFiles.clear();
    juce::PopupMenu user;
    const auto addUser = [this] (juce::PopupMenu& menu, const juce::File& file) {
        const int id = userItemBase + (int) userPresetFiles.size();
        userPresetFiles.push_back (file);
        juce::PopupMenu::Item entry (file.getFileNameWithoutExtension());
        entry.setID (id);
        entry.setAction ([safe = juce::Component::SafePointer<ThrowUpEditor> (this), file] {
            if (safe != nullptr) safe->selectUserPreset (file);
        });
        menu.addItem (std::move (entry));
    };
    for (const auto& folder : findUserPresets()) {
        if (folder.name.isEmpty()) {
            for (const auto& file : folder.files) addUser (user, file);
            continue;
        }
        juce::PopupMenu submenu;
        for (const auto& file : folder.files) addUser (submenu, file);
        user.addSubMenu (folder.name, submenu);
    }
    if (user.getNumItems() == 0) user.addSectionHeader ("No user presets yet - use SAVE");
    root->addSubMenu ("User", user);
    visiblePreset = -1;
    updatePresetDisplay();
}

void ThrowUpEditor::openSaveDialog()
{
   #if JUCE_MAC
    const juce::String presetFolder = "Application Support/THROW-UP/Presets";
   #else
    const auto presetFolder = ThrowUpProcessor::userPresetRoot().getFullPathName();
   #endif
    auto* window = new juce::AlertWindow ("SAVE PRESET", "Saved to " + presetFolder + ". Existing presets are never replaced.",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->setLookAndFeel (skin.get());
    const auto current = pluginProcessor.getUserPresetName().isNotEmpty() ? pluginProcessor.getUserPresetName()
        : juce::String (throwup::factoryPresets()[(size_t) juce::jlimit (0, pluginProcessor.getNumPrograms() - 1, pluginProcessor.getCurrentProgram())].name);
    window->addTextEditor ("name", current + (modified ? " edit" : ""), "Name");
    juce::StringArray folders;
    for (const auto& folder : ThrowUpProcessor::userPresetRoot().findChildFiles (juce::File::findDirectories, false))
        folders.add (folder.getFileName());
    folders.sort (true);
    folders.add ("New folder...");
    window->addComboBox ("folder", folders, "Folder");
    window->addTextEditor ("newFolder", "My Throws", "New folder name");
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    showDialog (window, [this, window] (int result) {
        if (result != 1) return;
        auto* box = window->getComboBoxComponent ("folder");
        auto folder = box->getText();
        if (box->getSelectedItemIndex() == box->getNumItems() - 1 || folder.isEmpty())
            folder = window->getTextEditorContents ("newFolder");
        const auto saved = pluginProcessor.saveUserPreset (folder, window->getTextEditorContents ("name"));
        rebuildPresetMenu();
        repaint();
        if (saved == juce::File()) {
            auto* failed = new juce::AlertWindow ("PRESET NOT SAVED", "THROW-UP could not write to its presets folder.",
                                                  juce::MessageBoxIconType::NoIcon, this);
            failed->addButton ("OK", 0, juce::KeyPress (juce::KeyPress::returnKey));
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ThrowUpEditor> (this), failed] {
                if (safe != nullptr) safe->showDialog (failed, nullptr); else delete failed;
            });
        }
    });
}

void ThrowUpEditor::showDialog (juce::AlertWindow* window, std::function<void (int)> onResult)
{
    if (dialog != nullptr) { delete window; return; }
    window->setLookAndFeel (skin.get());
    for (int i = 0; i < window->getNumButtons(); ++i)
        if (auto* button = window->getButton (i)) button->getProperties().set ("small", true);
    dialog = window;
    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<ThrowUpEditor> (this), onResult = std::move (onResult)] (int result) {
            if (safe == nullptr) return;
            if (onResult) onResult (result);
        }), true);
}

bool ThrowUpEditor::scaleFits (float scale, juce::Rectangle<int> userArea)
{
    const float width = baseWidth * scale;
    const float height = width * shellHeight / shellWidth;
    return width <= (float) userArea.getWidth() && height + 60.0f <= (float) userArea.getHeight();
}

float ThrowUpEditor::initialScale (float saved, juce::Rectangle<int> userArea)
{
    if (saved > 0.0f && scaleFits (saved, userArea)) return saved;
    if (saved <= 0.0f && scaleFits (1.0f, userArea)) return 1.0f;
    for (auto it = menuScales.rbegin(); it != menuScales.rend(); ++it)
        if (*it <= (saved > 0.0f ? saved : 1.0f) && scaleFits (*it, userArea)) return *it;
    return 0.8f;
}

juce::PopupMenu ThrowUpEditor::createSizeMenu (juce::Rectangle<int> userArea)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("SIZE");
    const float current = (float) getWidth() / (float) baseWidth;
    for (size_t i = 0; i < menuScales.size(); ++i) {
        const float scale = menuScales[i];
        juce::PopupMenu::Item item (juce::String (juce::roundToInt (scale * 100.0f)) + "%");
        item.setID ((int) i + 1);
        item.setEnabled (scaleFits (scale, userArea));
        item.setTicked (std::abs (current - scale) < 0.01f);
        item.setAction ([safe = juce::Component::SafePointer<ThrowUpEditor> (this), scale] {
            if (safe != nullptr) safe->applyScale (scale);
        });
        menu.addItem (std::move (item));
    }
    return menu;
}

void ThrowUpEditor::applyScale (float scale)
{
    setSize (juce::roundToInt (baseWidth * scale), juce::roundToInt (baseWidth * scale * shellHeight / shellWidth));
    pluginProcessor.setEditorScale (scale);
}

void ThrowUpEditor::mouseDown (const juce::MouseEvent& event)
{
    // Right-click anywhere on the enclosure (not on a control) opens the size menu.
    if (! event.mods.isPopupMenu()) return;
    juce::Rectangle<int> userArea { 0, 0, 1920, 1080 };
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        userArea = display->userBounds.toNearestInt();
    createSizeMenu (userArea).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
        .withMousePosition());
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
    const int program = juce::jlimit (0, pluginProcessor.getNumPrograms() - 1, pluginProcessor.getCurrentProgram());
    const auto& preset = throwup::factoryPresets()[(size_t) program];
    juce::String header;
    if (pluginProcessor.getUserPresetName().isNotEmpty())
        header = "USER";
    else if (preset.legacy)
        header = "0.1.5 " + juce::String (preset.group).toUpperCase() + "  " + juce::String (program - throwup::legacyPresetStart() + 1).paddedLeft ('0', 2) + "/50";
    else
        header = juce::String (preset.group).toUpperCase() + "  " + juce::String (program + 1).paddedLeft ('0', 2) + "/" + juce::String (throwup::legacyPresetStart());
    if (modified) header = "EDITED  " + header;
    text (g, header, { 560, 175, 370, 25 }, 17, modified ? amber : muted, juce::Justification::centredRight);
    g.setColour (amber.withAlpha (.22f));
    g.drawHorizontalLine (256, 188.0f, 930.0f);
    for (const auto& c : controls) {
        if (c->physical || c->id == "output" || c->page != static_cast<int> (page)) continue;
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
    // Bottom line of the display: what this page is for.
    const auto bpm = juce::String (juce::roundToInt (pluginProcessor.getHostTempo())) + " BPM";
    switch (page) {
        case Page::room:
            text (g, pluginProcessor.getUserPresetName().isNotEmpty() ? "User preset: " + pluginProcessor.getUserPresetName()
                                                                      : juce::String (preset.description),
                  { 190, 422, 740, 21 }, 16, muted.withAlpha (.85f));
            break;
        case Page::trigger:
            text (g, "THROW! key, MIDI notes or Auto-catch open the throw. C1 = first room.", { 190, 420, 590, 24 }, 15, muted.withAlpha (.85f));
            break;
        case Page::space:
            text (g, "Synced pre-delay and swell follow the host tempo: " + bpm + ". FREEZE holds the tail.", { 190, 422, 740, 21 }, 16, muted.withAlpha (.85f));
            break;
        case Page::mix:
            text (g, pluginProcessor.getSoundVersion() == throwup::SoundVersion::v015 ? "SOUND: 0.1.5 (this project's original sound)"
                                                                                      : "SOUND: LATEST (accurate low cut, real Grit, Duck in dB)",
                  { 190, 420, 520, 24 }, 15, pluginProcessor.getSoundVersion() == throwup::SoundVersion::v015 ? amber : muted.withAlpha (.85f));
            break;
    }
}

void ThrowUpEditor::drawKnob (juce::Graphics& g, Control& c)
{
    if (c.id == "output") { drawOutputKnob (g, c); return; }
    const auto centre = c.hit.getCentre();
    const float arcRadius = c.large ? 164.0f : 89.0f;
    const float pointerRadius = c.large ? 94.0f : 41.0f;
    const float fraction = (float) c.slider.valueToProportionOfLength (c.slider.getValue());
    const float angle = startAngle + fraction * (endAngle - startAngle);
    if (c.large) {
        // A triggered throw (THROW! key, MIDI, Auto-catch) shows as a cream arc beyond the knob.
        const float triggered = pluginProcessor.getTriggerLevel() * 0.01f;
        if (triggered > fraction + .005f) {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0, startAngle,
                               startAngle + triggered * (endAngle - startAngle), true);
            g.setColour (cream.withAlpha (.9f));
            g.strokePath (arc, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
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

void ThrowUpEditor::drawOutputKnob (juce::Graphics& g, Control& c)
{
    // The plate knobs' caps are in the artwork; this one is drawn whole: a recessed socket moulded like
    // the INFO key's, an amber level arc measured from 0 dB (the tick), and the same cream cap.
    const auto centre = outputKnobCentre;
    const auto circle = [centre] (float radius) { return juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (centre); };
    const auto socketColour = vfd::themeFor ("THROW-UP").socket;
    const auto socket = circle (outputSocketRadius);
    g.setColour (juce::Colours::black.withAlpha (.4f));
    g.fillEllipse (socket.translated (0, 2));
    g.setGradientFill (juce::ColourGradient (socketColour.darker (.5f), centre.x, socket.getY(),
                                            socketColour.brighter (.32f), centre.x, socket.getBottom(), false));
    g.fillEllipse (socket);
    g.setColour (amber.withAlpha (.3f));
    g.drawEllipse (socket.reduced (.6f), 1.2f);
    const auto angleOf = [&c] (double value) {
        return startAngle + (float) c.slider.valueToProportionOfLength (juce::jlimit (c.slider.getMinimum(), c.slider.getMaximum(), value)) * (endAngle - startAngle);
    };
    const float zero = angleOf (0.0), angle = angleOf (c.slider.getValue());
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, outputArcRadius, outputArcRadius, 0, startAngle, endAngle, true);
    g.setColour (panel.withAlpha (.8f));
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (std::abs (angle - zero) > .01f) {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, outputArcRadius, outputArcRadius, 0, juce::jmin (zero, angle), juce::jmax (zero, angle), true);
        g.setColour (amber);
        g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const auto onRim = [centre] (float a, float radius) { return centre + juce::Point<float> (std::sin (a), -std::cos (a)) * radius; };
    g.setColour (cream.withAlpha (.85f));
    g.drawLine ({ onRim (zero, outputArcRadius - 3.5f), onRim (zero, outputSocketRadius - .5f) }, 1.6f);
    const auto cap = circle (outputCapRadius);
    g.setColour (juce::Colours::black.withAlpha (.45f));
    g.fillEllipse (cap.translated (0, 2));
    juce::ColourGradient resin (juce::Colour (0xfffffaee), centre.x - outputCapRadius * .45f, cap.getY(),
                                juce::Colour (0xffd8bf9c), centre.x + outputCapRadius * .45f, cap.getBottom(), false);
    resin.addColour (.45, juce::Colour (0xfff7ead3));
    g.setGradientFill (resin);
    g.fillEllipse (cap);
    g.setColour (ink.withAlpha (.5f));
    g.drawEllipse (cap, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (.7f));
    g.drawEllipse (cap.reduced (2.0f), 1.0f);
    g.setColour (ink);
    g.drawLine ({ onRim (angle, outputCapRadius * .3f), onRim (angle, outputCapRadius * .8f) }, 4.5f);
    for (const float radius : { outputCapRadius * .3f, outputCapRadius * .8f })
        g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (onRim (angle, radius)));
    if (c.slider.hasKeyboardFocus (true)) {
        g.setColour (ink.withAlpha (.75f));
        g.drawEllipse (socket.expanded (3.0f), 2.0f);
    }
}

void ThrowUpEditor::drawMeters (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // 14 segments: ten 6 dB steps from -60 to 0 dBFS, then four 4.5 dB steps up
    // to +18 dBFS, because the parallel wet return can run well above 0 dBFS.
    constexpr int segments = 14, belowZero = 10;
    const auto threshold = [] (int segment) {
        return segment < belowZero ? -60.0f + 6.0f * (float) segment : 4.5f * (float) (segment - belowZero);
    };
    const float segmentX = bounds.getX() + 48.0f;
    const float zeroX = segmentX + (float) belowZero * 13.8f - 1.9f;
    for (int row = 0; row < 2; ++row) {
        const float y = bounds.getY() + (float) row * 30.0f;
        text (g, row == 0 ? "IN" : "OUT", { bounds.getX(), y - 3.0f, 43, 25 }, 18, cream);
        const float decibels = juce::Decibels::gainToDecibels (row == 0 ? inputMeter : outputMeter, -60.0f);
        for (int i = 0; i < segments; ++i) {
            const bool lit = decibels > threshold (i);
            g.setColour (lit ? (i >= belowZero ? juce::Colour (0xffff7850) : amber) : cream.withAlpha (.10f));
            g.fillRect (segmentX + (float) i * 13.8f, y, 10.0f, 18.0f);
        }
        // Clip indicator: lit (and held for 1.5 s) after any peak above 0 dBFS.
        const bool clipped = (row == 0 ? inputClipHold : outputClipHold) > 0;
        const juce::Rectangle<float> led { segmentX + (float) segments * 13.8f + 6.0f, y, 30.0f, 18.0f };
        g.setColour (clipped ? juce::Colour (0xffff4a3a) : cream.withAlpha (.10f));
        g.fillRoundedRectangle (led, 3.0f);
    }
    // 0 dBFS mark between the last amber and first over-zero segment.
    g.setColour (cream.withAlpha (.7f));
    g.fillRect (zeroX - 0.75f, bounds.getY() - 4.0f, 1.5f, 52.0f);
    text (g, "0", { zeroX - 10.0f, bounds.getY() - 19.0f, 20.0f, 14.0f }, 13, muted, juce::Justification::centred);
    text (g, "CLIP", { segmentX + (float) segments * 13.8f + 1.0f, bounds.getY() - 19.0f, 40.0f, 14.0f },
          13, muted, juce::Justification::centred);
}

void ThrowUpEditor::drawFooter (juce::Graphics& g)
{
    text (g, "BYPASS", { 186, 1075, 178, 31 }, 22, ink, juce::Justification::centred);
    text (g, "OUTPUT", { 395, 1075, 150, 31 }, 22, ink, juce::Justification::centred);
    g.setColour (ink.withAlpha (.27f));
    g.drawVerticalLine (380, 1079.0f, 1170.0f);
    drawMeters (g, { 618, 1101, 285, 55 });
}

void ThrowUpEditor::paintShell (juce::Graphics& g)
{
    // Resample the enclosure once per size and screen density, then blit it 1:1. A logical-size cache was
    // upsampled, and soft, on Retina.
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int width = juce::jmax (1, juce::roundToInt (getWidth() * scale));
    const int height = juce::jmax (1, juce::roundToInt (getHeight() * scale));
    if (shell.isValid() && (scaledShell.getWidth() != width || scaledShell.getHeight() != height)) {
        scaledShell = juce::Image (juce::Image::RGB, width, height, false);
        juce::Graphics shellGraphics (scaledShell);
        shellGraphics.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        shellGraphics.drawImage (shell, 0, 0, width, height, shellX, shellY, shellWidth, shellHeight);
    }
    if (scaledShell.isValid()) g.drawImage (scaledShell, getLocalBounds().toFloat());
}

void ThrowUpEditor::paint (juce::Graphics& g)
{
    paintShell (g);
    juce::Graphics::ScopedSaveState state (g);
    const float sx = (float) getWidth() / (float) shellWidth;
    const float sy = (float) getHeight() / (float) shellHeight;
    g.addTransform (juce::AffineTransform (sx, 0, -(float) shellX * sx, 0, sy, -(float) shellY * sy));
    drawScreen (g);
    // The small top display names the device, or warns that the 0.1.5 sound is in use.
    if (pluginProcessor.getSoundVersion() == throwup::SoundVersion::v015)
        text (g, "0.1.5 SOUND", { 602, 82, 302, 38 }, 23, amber, juce::Justification::centred);
    else
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
    for (const auto& c : controls) {
        c->slider.setBounds (scaled (c->hit));
        c->value.setBounds (scaled (c->readout));
        const float fontSize = c->large ? 43.0f : c->id == "output" ? 25.0f : c->physical ? 29.0f : 25.0f;
        c->value.setFont (skin->font (fontSize * uiScale));
    }
    // Preset row: the name, SAVE and A/B, then the browse keys (dice, previous, next)
    // as one cluster of equal keys, right-aligned with the header above.
    presets->setBounds (scaled ({ 186, 204, 370, 44 }));
    saveKey.setBounds (scaled ({ 566, 206, 74, 40 }));
    slotA.setBounds (scaled ({ 650, 206, 40, 40 }));
    slotB.setBounds (scaled ({ 696, 206, 40, 40 }));
    randomKey.setBounds (scaled ({ 762, 204, 47, 44 }));
    previous.setBounds (scaled ({ 822, 204, 47, 44 }));
    next.setBounds (scaled ({ 882, 204, 47, 44 }));
    for (size_t i = 0; i < pageTabs.size(); ++i)
        pageTabs[i].setBounds (scaled ({ 184.0f + (float) i * 88.0f, 172.0f, 84.0f, 28.0f }));
    noteRoomsKey.setBounds (scaled ({ 780, 423, 150, 26 }));
    soundKey.setBounds (scaled ({ 712, 423, 218, 26 }));
    bypass.setBounds (scaled (bypassBounds));
    throwKey->setBounds (scaled (throwKeyBounds));
    freezeKey->setBounds (scaled (freezeKeyBounds));
    safeKey->setBounds (scaled (safeKeyBounds));
    sendKey->setBounds (scaled (sendKeyBounds));
    if (infoButton) infoButton->setBounds (scaled (infoKeyBounds));
    if (infoOverlay) infoOverlay->setBounds (getLocalBounds());
    // Remember host or menu resizes with the project.
    if (sizeReady) pluginProcessor.setEditorScale ((float) getWidth() / (float) baseWidth);
}

void ThrowUpEditor::stepPreset (int direction)
{
    // Arrows step through the current bank; the 0.1.5 rooms step among themselves.
    const int current = pluginProcessor.getCurrentProgram();
    const int start = current >= throwup::legacyPresetStart() ? throwup::legacyPresetStart() : 0;
    const int count = current >= throwup::legacyPresetStart() ? throwup::legacyPresetCount : throwup::legacyPresetStart();
    selectFactoryPreset (start + (current - start + direction + count) % count);
}

void ThrowUpEditor::selectFactoryPreset (int index)
{
    pluginProcessor.loadFactoryPreset (index);
    updatePresetDisplay(); repaint();
}

bool ThrowUpEditor::selectUserPreset (const juce::File& file)
{
    const bool loaded = pluginProcessor.loadUserPreset (file);
    updatePresetDisplay(); repaint();
    return loaded;
}

void ThrowUpEditor::loadRandomPreset()
{
    // Draw from the bank the arrows browse: the 0.1.5 rooms among themselves while
    // one is loaded, otherwise the factory folders plus the user presets the menu
    // lists. The preset already loaded is left out (a user preset by its name).
    const auto userName = pluginProcessor.getUserPresetName();
    const int current = pluginProcessor.getCurrentProgram();
    const bool legacy = userName.isEmpty() && current >= throwup::legacyPresetStart();
    const int start = legacy ? throwup::legacyPresetStart() : 0;
    const int end = legacy ? pluginProcessor.getNumPrograms() : throwup::legacyPresetStart();
    std::vector<int> rooms;
    for (int i = start; i < end; ++i)
        if (userName.isNotEmpty() || i != current) rooms.push_back (i);
    std::vector<juce::File> files;
    if (! legacy)
        for (const auto& folder : findUserPresets())
            for (const auto& file : folder.files)
                if (userName.isEmpty() || userPresetNameOf (file) != userName) files.push_back (file);
    // Nothing else to choose: do nothing. An unreadable user file is skipped.
    while (! rooms.empty() || ! files.empty()) {
        const int pick = random.nextInt ((int) (rooms.size() + files.size()));
        if (pick < (int) rooms.size()) { selectFactoryPreset (rooms[(size_t) pick]); return; }
        const auto file = files.begin() + (pick - (int) rooms.size());
        if (selectUserPreset (*file)) return;
        files.erase (file);
    }
}

void ThrowUpEditor::updatePresetDisplay()
{
    const int current = juce::jlimit (0, pluginProcessor.getNumPrograms() - 1, pluginProcessor.getCurrentProgram());
    const auto userName = pluginProcessor.getUserPresetName();
    const int shown = userName.isNotEmpty() ? -2 : current;
    // User presets share one key, so compare their names too (dice or A/B from one user preset to another).
    if (visiblePreset != shown || (userName.isNotEmpty() && presets->getText() != userName)) {
        visiblePreset = shown;
        if (userName.isNotEmpty()) presets->setText (userName, juce::dontSendNotification);
        else presets->setSelectedId (current + 1, juce::dontSendNotification);
    }
    modified = false;
    if (userName.isNotEmpty()) return;
    const auto& r = throwup::factoryPresets()[(size_t) current].room;
    const std::array<float, 18> values { r.size, r.decay, r.predelay, r.lowCut, r.highCut, r.diffusion, r.motion,
        r.width, r.grit, r.duck, (float) r.predelaySync, (float) r.predelayFeel, r.shimmer, (float) r.shimmerPitch,
        (float) r.swell, r.duckRelease, r.duckThreshold, r.version == throwup::SoundVersion::v015 ? 0.0f : 1.0f };
    const auto ids = ThrowUpProcessor::roomParameterIds();
    for (int i = 0; i < ids.size(); ++i)
        if (std::abs (pluginProcessor.apvts.getRawParameterValue (ids[i])->load() - values[(size_t) i]) > .06f) modified = true;
}

void ThrowUpEditor::updateKeys()
{
    slotA.setToggleState (pluginProcessor.getActiveSlot() == 0, juce::dontSendNotification);
    slotB.setToggleState (pluginProcessor.getActiveSlot() == 1, juce::dontSendNotification);
    soundKey.setButtonText (pluginProcessor.getSoundVersion() == throwup::SoundVersion::v015 ? "USE LATEST SOUND" : "USE 0.1.5 SOUND");
    // The PRE-DELAY readout shows the synced note value when tempo sync is on.
    const int sync = juce::roundToInt (pluginProcessor.apvts.getRawParameterValue ("predelaySync")->load());
    for (const auto& c : controls)
        if (c->id == "predelay" && ! c->value.isBeingEdited()) {
            if (sync > 0) {
                const int feel = juce::roundToInt (pluginProcessor.apvts.getRawParameterValue ("predelayFeel")->load());
                const auto beats = throwup::predelayBeats (static_cast<throwup::PredelaySync> (sync), static_cast<throwup::PredelayFeel> (feel));
                const double ms = std::min (beats * 60000.0 / pluginProcessor.getHostTempo(), (double) throwup::ReverbEngine::maximumPredelayMs);
                c->value.setText (valueText ("predelaySync", sync) + (feel == 1 ? "." : feel == 2 ? "T" : "") + "  " + juce::String (juce::roundToInt (ms)) + " ms",
                                  juce::dontSendNotification);
            } else {
                c->value.setText (valueText ("predelay", c->slider.getValue()), juce::dontSendNotification);
            }
        }
    for (auto* key : { throwKey.get(), freezeKey.get(), safeKey.get(), sendKey.get() })
        key->repaint();
}

void ThrowUpEditor::timerCallback()
{
    // Collect the highest peak of every block since the previous frame.
    const float inputPeak = pluginProcessor.inputLevel.exchange (0.0f, std::memory_order_relaxed);
    const float outputPeak = pluginProcessor.outputLevel.exchange (0.0f, std::memory_order_relaxed);
    inputMeter = juce::jmax (inputPeak, inputMeter * .78f);
    outputMeter = juce::jmax (outputPeak, outputMeter * .78f);
    constexpr int clipHoldFrames = 36; // 1.5 s at 24 Hz
    inputClipHold = inputPeak > 1.0f ? clipHoldFrames : juce::jmax (0, inputClipHold - 1);
    outputClipHold = outputPeak > 1.0f ? clipHoldFrames : juce::jmax (0, outputClipHold - 1);
    updatePresetDisplay();
    updateKeys();
    bypass.setButtonText (bypass.getToggleState() ? "ON" : "OFF");
    repaint();
}
