#include "StutterEditor.h"
#include "BinaryData.h"
#include "VfdInfo.h"
#include <cmath>

using namespace stutter;

namespace {
const juce::Colour bench { 0xff050d0e }, cream { 0xfff4ead2 }, glassText { 0xffddf8f2 }, glassMuted { 0xff6fa39d },
    aqua { 0xff5fe8d8 }, gateStroke { 0xff7ff5e6 }, lime { 0xffd3f76e }, ink { 0xff164244 }, pointerInk { 0xff183235 },
    keyFill { 0xff0a2a2c }, navy { 0xff1b2c5e }, badgeText { 0xff8fe9de }, panel { 0xff071d1f };

// Artwork coordinates, measured on Assets/DeviceShell.png (1120 x 1408).
const juce::Rectangle<float> laneArt { 250, 286, 684, 128 }, dotsArt { 250, 440, 684, 30 };
const juce::Rectangle<float> outputArt { 540, 514, 110, 32 }, meterArt { 724, 494, 210, 52 };
const juce::Point<float> ledCentre { 948, 127 };
constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f, sweep = juce::MathConstants<float>::pi * 1.5f;
constexpr std::array<float, 4> menuScales { 0.9f, 1.0f, 1.25f, 1.5f };

juce::String swingWord (double s)
{
    return s < 0.515 ? "STRAIGHT" : s < 0.56 ? "LIGHT" : s < 0.62 ? "MEDIUM" : s < 0.655 ? "HEAVY"
         : s < 0.68 ? "TRIPLET" : s < 0.72 ? "LAZY" : "DRUNK";
}
juce::String percent (double v01) { return juce::String (juce::roundToInt (v01 * 100.0)) + "%"; }
juce::String hertz (float hz) { return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz"; }
const char* rateNames[] { "1/8", "1/16", "1/32" };

juce::Image loadShell()
{
    return juce::ImageCache::getFromMemory (BinaryData::DeviceShell_png, BinaryData::DeviceShell_pngSize);
}

// Envelopes for the screen, built exactly as the processor builds them.
struct LaneModel
{
    Envelope gate, send, gaps;
    float closed = 0;
    LaneModel (const StutterProcessor::Snapshot& s)
    {
        closed = StutterProcessor::closedGain (s.depth);
        const double ramp = juce::jlimit (0.002, 0.45, (s.smoothMs / 1000.0) / s.stepSeconds());
        if (s.bypassed) gate.setConstant (1.0f); else gate.build (s.pattern, s.swing, s.length, ramp, closed, false);
        if (s.slapOn && ! s.bypassed) send.build (s.pattern, s.swing, s.length, ramp, 0.0f, true); else send.setConstant (0.0f);
        if (s.gaps && ! s.bypassed) gaps.buildInverseOf (gate, closed); else gaps.setConstant (1.0f);
    }
};

class Skin final : public juce::LookAndFeel_V4
{
public:
    explicit Skin (juce::Typeface::Ptr f) : face (f)
    {
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, glassText);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, aqua);
        setColour (juce::PopupMenu::highlightedTextColourId, panel);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xfff3fffb));
        setColour (juce::TooltipWindow::textColourId, ink);
        setColour (juce::TooltipWindow::outlineColourId, aqua);
    }
    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (face).withHeight (16.0f)); }
    juce::Font getTooltipFont() { return juce::Font (juce::FontOptions (face).withHeight (14.0f)); }
    juce::TextLayout layoutTooltipText (const juce::String& t, juce::Colour colour)
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (t, getTooltipFont(), colour);
        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (s, 320.0f);
        return layout;
    }
    juce::Rectangle<int> getTooltipBounds (const juce::String& t, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        const auto layout = layoutTooltipText (t, juce::Colours::black);
        const int w = (int) (layout.getWidth() + 18.0f), h = (int) (layout.getHeight() + 12.0f);
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 24,
                                     pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin (parent);
    }
    void drawTooltip (juce::Graphics& g, const juce::String& t, int w, int h) override
    {
        const juce::Rectangle<float> r (0, 0, (float) w, (float) h);
        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        layoutTooltipText (t, findColour (juce::TooltipWindow::textColourId)).draw (g, r.reduced (9.0f, 6.0f));
    }
private:
    juce::Typeface::Ptr face;
};
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Controls

struct StutterEditor::Knob
{
    juce::String id, label;
    float cx, cy, rFace, rArc, arcW, labelY, valueY, labelSize, valueSize;
    bool slapGroup;
    struct Fine final : juce::Slider
    {
        std::function<void()> beforeDrag;
        void paint (juce::Graphics&) override {}
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (beforeDrag) beforeDrag();
            setMouseDragSensitivity (sensitivity * (e.mods.isShiftDown() ? 5 : 1));
            juce::Slider::mouseDown (e);
        }
        void focusGained (FocusChangeType) override { if (auto* p = getParentComponent()) p->repaint(); }
        void focusLost (FocusChangeType) override { if (auto* p = getParentComponent()) p->repaint(); }
        int sensitivity = 220;
    } slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::Rectangle<float> hit() const { const float r = rArc + arcW; return { cx - r, cy - r, 2 * r, 2 * r }; }
};

// Dark keys on the screen glass: RATE, SUB SAFE, preset arrows and ROLL FROM.
class StutterEditor::ScreenKey final : public juce::Button
{
public:
    enum class Style { key, text, readout, left, right, overlay };
    ScreenKey (const juce::String& name, Style st, juce::Typeface::Ptr f) : juce::Button (name), style (st), face (f)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
        setTitle (name);
    }
    std::function<juce::String()> label;
    std::function<bool()> lit;
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto s = (float) getHeight() / 32.0f;
        auto r = getLocalBounds().toFloat().reduced (1.0f * s);
        const bool on = lit && lit();
        const bool focus = hasKeyboardFocus (true);
        if (style == Style::overlay) {
            // An invisible hit area over drawn content: a faint wash on hover, a ring for keyboard focus.
            if (over || down) { g.setColour (aqua.withAlpha (down ? 0.10f : 0.05f)); g.fillRoundedRectangle (r, 6.0f * s); }
            if (focus) { g.setColour (aqua); g.drawRoundedRectangle (r, 6.0f * s, 2.0f * s); }
            return;
        }
        if (style == Style::text) {
            const auto txt = label ? label() : getName();
            const auto split = txt.indexOf ("|");
            g.setFont (juce::Font (juce::FontOptions (face).withHeight (15.0f * s)).withExtraKerningFactor (0.14f));
            g.setColour (over || focus ? glassText : glassMuted);
            const auto first = txt.substring (0, split), second = txt.substring (split + 1);
            g.drawText (first, r, juce::Justification::centredLeft, false);
            const auto w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), first);
            g.setColour (aqua);
            g.drawText (second, r.withTrimmedLeft (w), juce::Justification::centredLeft, false);
            return;
        }
        const float radius = 7.0f * s;
        g.setColour (on ? aqua : keyFill.withAlpha (down ? 1.0f : 0.8f));
        g.fillRoundedRectangle (r, radius);
        g.setColour (on ? aqua : aqua.withAlpha (over || focus ? 0.6f : 0.26f));
        g.drawRoundedRectangle (r, radius, (focus ? 2.0f : 1.5f) * s);
        if (style == Style::left || style == Style::right) {
            juce::Path chevron;
            const auto c = r.getCentre();
            const float d = 6.0f * s, side = style == Style::left ? 1.0f : -1.0f;
            chevron.startNewSubPath (c.x + d * 0.6f * side, c.y - d);
            chevron.lineTo (c.x - d * 0.6f * side, c.y);
            chevron.lineTo (c.x + d * 0.6f * side, c.y + d);
            g.setColour (glassText);
            g.strokePath (chevron, juce::PathStrokeType (2.6f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }
        g.setColour (on ? panel : style == Style::readout ? glassText : glassMuted);
        g.setFont (juce::Font (juce::FontOptions (face).withHeight ((style == Style::readout ? 16.0f : 15.0f) * s)));
        g.drawText (label ? label() : getName(), r, juce::Justification::centred, false);
    }
private:
    Style style;
    juce::Typeface::Ptr face;
};

// Pale molded keys on the slap plate, with a status LED.
class StutterEditor::PlateKey final : public juce::Button
{
public:
    PlateKey (const juce::String& name, juce::Typeface::Ptr f) : juce::Button (name), face (f)
    {
        setClickingTogglesState (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
        setTitle (name);
    }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const float s = (float) getHeight() / 52.0f;
        auto r = getLocalBounds().toFloat().reduced (2.0f * s);
        r.removeFromBottom (3.0f * s);
        const float radius = 10.0f * s, travel = down ? 2.0f * s : 0.0f;
        g.setColour (juce::Colour (0xff9cc4c0));
        g.fillRoundedRectangle (r.translated (0, 3.0f * s), radius);
        r = r.translated (0, travel);
        g.setGradientFill ({ juce::Colour (0xfff6fbf8), 0, r.getY(), juce::Colour (0xffd6ece8), 0, r.getBottom(), false });
        g.fillRoundedRectangle (r, radius);
        g.setColour (ink.withAlpha (over || hasKeyboardFocus (true) ? 0.75f : 0.28f));
        g.drawRoundedRectangle (r, radius, (hasKeyboardFocus (true) ? 2.0f : 1.5f) * s);
        const auto led = juce::Rectangle<float> (8.0f * s, 8.0f * s).withCentre ({ r.getCentreX(), r.getY() + 13.0f * s });
        if (getToggleState()) {
            g.setColour (lime.withAlpha (0.5f));
            g.fillEllipse (led.expanded (4.0f * s));
            g.setColour (juce::Colour (0xffc9f56a));
        } else {
            g.setColour (juce::Colour (0xff4a6f6e));
        }
        g.fillEllipse (led);
        g.setColour (ink);
        g.setFont (juce::Font (juce::FontOptions (face).withHeight (14.0f * s)).withExtraKerningFactor (0.08f));
        g.drawText (getName(), r.withTrimmedTop (22.0f * s), juce::Justification::centredTop, false);
    }
private:
    juce::Typeface::Ptr face;
};

class StutterEditor::SlapSwitch final : public juce::Button
{
public:
    explicit SlapSwitch (juce::Typeface::Ptr f) : juce::Button ("Slap"), face (f)
    {
        setClickingTogglesState (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
        setTitle ("Slap delay on or off");
    }
    void paintButton (juce::Graphics& g, bool, bool) override
    {
        const float s = (float) getHeight() / 40.0f;
        const auto r = getLocalBounds().toFloat().reduced (s);
        const bool on = getToggleState();
        g.setGradientFill ({ juce::Colour (0xff0c2c2e), 0, r.getY(), juce::Colour (0xff164446), 0, r.getBottom(), false });
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (hasKeyboardFocus (true) ? aqua : juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, s * (hasKeyboardFocus (true) ? 2.0f : 1.0f));
        const float d = r.getHeight() - 8.0f * s;
        const float x = on ? r.getRight() - d - 4.0f * s : r.getX() + 4.0f * s;
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (x, r.getY() + 6.0f * s, d, d);
        g.setGradientFill ({ juce::Colour (0xfffffaf0), x, r.getY(), juce::Colour (0xffe7dcc0), x, r.getBottom(), false });
        g.fillEllipse (x, r.getY() + 4.0f * s, d, d);
        g.setFont (juce::Font (juce::FontOptions (face).withHeight (15.0f * s)).withExtraKerningFactor (0.1f));
        g.setColour (on ? lime : glassMuted);
        g.drawText (on ? "ON" : "OFF", on ? r.withTrimmedRight (d + 6.0f * s) : r.withTrimmedLeft (d + 6.0f * s),
                    juce::Justification::centred, false);
    }
private:
    juce::Typeface::Ptr face;
};

class StutterEditor::LockButton final : public juce::Button
{
public:
    LockButton() : juce::Button ("Lock swing")
    {
        setClickingTogglesState (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
        setTitle ("Lock swing");
        setTooltip ("Keep this swing when you roll a new groove or load a factory groove.");
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const float s = (float) getHeight() / 36.0f;
        const auto r = getLocalBounds().toFloat().reduced (1.0f * s);
        const bool on = getToggleState();
        g.setColour (on ? ink : juce::Colours::white.withAlpha (over ? 0.42f : 0.28f));
        g.fillRoundedRectangle (r, 9.0f * s);
        g.setColour (hasKeyboardFocus (true) ? aqua : ink.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 9.0f * s, 1.5f * s);
        const auto c = r.getCentre();
        g.setColour (on ? lime : ink);
        g.drawRoundedRectangle (juce::Rectangle<float> (14.0f * s, 10.0f * s).withCentre ({ c.x, c.y + 4.0f * s }), 2.0f * s, 2.4f * s);
        juce::Path shackle;
        const float sx = c.x - 4.0f * s, top = c.y - 7.0f * s;
        shackle.startNewSubPath (sx, c.y - 1.0f * s);
        shackle.lineTo (sx, top + 3.0f * s);
        shackle.addCentredArc (c.x, top + 3.0f * s, 4.0f * s, 4.0f * s, 0, -juce::MathConstants<float>::halfPi,
                               on ? juce::MathConstants<float>::halfPi : juce::MathConstants<float>::pi * 0.28f);
        if (on) shackle.lineTo (c.x + 4.0f * s, c.y - 1.0f * s);
        g.strokePath (shackle, juce::PathStrokeType (2.4f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

class StutterEditor::DiceButton final : public juce::Button
{
public:
    explicit DiceButton (juce::Typeface::Ptr f) : juce::Button ("New groove"), face (f)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
        setTitle ("New groove");
        setTooltip ("Roll a new groove. Most rolls swing; ROLL FROM on the screen picks the family, and the padlock keeps your swing.");
    }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const float s = (float) getHeight() / 108.0f;
        const auto r = getLocalBounds().toFloat();
        if (down) { g.setColour (ink.withAlpha (0.16f)); g.fillRoundedRectangle (r.reduced (4.0f * s), 24.0f * s); }
        else if (over) { g.setColour (juce::Colours::white.withAlpha (0.12f)); g.fillRoundedRectangle (r.reduced (4.0f * s), 24.0f * s); }
        if (hasKeyboardFocus (true)) { g.setColour (aqua); g.drawRoundedRectangle (r.reduced (2.0f * s), 24.0f * s, 3.0f * s); }
        g.setFont (juce::Font (juce::FontOptions (face).withHeight (30.0f * s)).withExtraKerningFactor (0.02f));
        const juce::String label ("NEW GROOVE");
        const float textW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label);
        const float icon = 52.0f * s, gap = 18.0f * s, total = icon + gap + textW;
        const float x0 = r.getCentreX() - total * 0.5f, travel = down ? 3.0f * s : 0.0f;
        const auto die = juce::Rectangle<float> (x0, r.getCentreY() - icon * 0.5f + travel, icon, icon).reduced (3.0f * s);
        g.setColour (juce::Colour (0xffd6f56f));
        g.fillRoundedRectangle (die, 11.0f * s);
        g.setColour (juce::Colour (0xffa7c94a));
        g.drawRoundedRectangle (die, 11.0f * s, 2.0f * s);
        g.setColour (juce::Colour (0xff29421a));
        const float pip = 4.2f * s;
        for (const auto& [px, py] : { std::pair { 0.27f, 0.27f }, { 0.73f, 0.27f }, { 0.5f, 0.5f }, { 0.27f, 0.73f }, { 0.73f, 0.73f } })
            g.fillEllipse (juce::Rectangle<float> (2 * pip, 2 * pip).withCentre ({ die.getX() + die.getWidth() * px, die.getY() + die.getHeight() * py }));
        g.setColour (navy);
        g.drawText (label, juce::Rectangle<float> (x0 + icon + gap, r.getY() + travel, textW + 4.0f, r.getHeight()), juce::Justification::centredLeft, false);
    }
private:
    juce::Typeface::Ptr face;
};

// The step lane: gate shape, swing, echo energy and playhead. Click to edit.
class StutterEditor::Lane final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Lane (StutterEditor& e) : editor (e)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle ("Gate pattern");
        setDescription ("Click a step: hit, ghost, off. Shift-click ties it to the step before. Right-click toggles its slap send.");
        setTooltip ("Click a step: hit, ghost, off. Shift-click: tie to the step before. Right-click: slap send.");
    }
    void paint (juce::Graphics& g) override
    {
        const auto snap = editor.proc.snapshot();
        const LaneModel m (snap);
        const float s = (float) getWidth() / laneArt.getWidth();
        g.addTransform (juce::AffineTransform::scale (s));
        const float W = laneArt.getWidth(), H = laneArt.getHeight();
        const auto xOf = [W] (double t) { return (float) (t / numSteps * W); };
        const auto yOf = [H] (float v) { return H - 10.0f - v * (H - 22.0f); };

        for (int i = 0; i <= numSteps; ++i) {
            g.setColour (aqua.withAlpha (i % 4 == 0 ? 0.20f : 0.07f));
            g.fillRect (xOf (i) - 0.5f, 4.0f, 1.0f, H - 8.0f);
        }
        if (snap.swing > 0.505) {
            g.setColour (juce::Colour (0xffa0dcd6).withAlpha (0.55f));
            for (int i = 1; i < numSteps; i += 2) {
                const float x0 = xOf (i), x1 = xOf (stepStart (i, snap.swing));
                g.drawLine (x0, 6.0f, x1 - 3.0f, 6.0f, 1.4f);
                juce::Path head;
                head.addTriangle (x1, 6.0f, x1 - 5.0f, 3.0f, x1 - 5.0f, 9.0f);
                g.fillPath (head);
            }
        }
        // Echo energy, sampled the way the slap hears the send.
        if (snap.slapOn && ! snap.bypassed) {
            const double d = snap.slapSeconds() / snap.stepSeconds();
            const float visMix = 0.3f + 0.7f * (float) snap.mix;
            for (float x = 0; x < W; x += 2.0f) {
                const double t = x / W * numSteps;
                float e = 0, amp = 1;
                for (int k = 1; k <= 7 && amp > 0.04f; ++k) { e += amp * m.send.eval (t - k * d); amp *= (float) snap.feedback; }
                e *= m.gaps.eval (t) * visMix;
                if (e < 0.01f) continue;
                const float h = juce::jmin (1.0f, e) * (H - 22.0f);
                g.setColour (lime.withAlpha (0.18f + 0.4f * juce::jmin (1.0f, e)));
                g.fillRect (x, H - 10.0f - h, 2.0f, h);
            }
        }
        if (m.closed > 0.001f && ! snap.bypassed) {
            juce::Path floor;
            floor.startNewSubPath (0, yOf (m.closed));
            floor.lineTo (W, yOf (m.closed));
            const float dash[] { 4.0f, 5.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (floor, floor, dash, 2);
            g.setColour (aqua.withAlpha (0.35f));
            g.fillPath (floor);
        }
        juce::Path shape, line;
        shape.startNewSubPath (0, H - 10.0f);
        for (float x = 0; x <= W; x += 1.0f) {
            const float y = yOf (m.gate.eval (x / W * numSteps));
            shape.lineTo (x, y);
            if (x == 0.0f) line.startNewSubPath (x, y); else line.lineTo (x, y);
        }
        shape.lineTo (W, H - 10.0f);
        shape.closeSubPath();
        g.setGradientFill ({ aqua.withAlpha (0.55f), 0, 8, aqua.withAlpha (0.06f), 0, H, false });
        g.fillPath (shape);
        g.setColour (aqua.withAlpha (0.22f));
        g.strokePath (line, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (gateStroke);
        g.strokePath (line, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        for (int i = 0; i < numSteps; ++i) {
            const auto v = snap.pattern[(size_t) i].v;
            g.setColour (gateStroke.withAlpha (v == Step::hit ? 0.95f : v == Step::ghost ? 0.5f : v == Step::tie ? 0.28f : 0.1f));
            g.fillRect (xOf (stepStart (i, snap.swing)) + 2.0f, H - 5.0f, xOf (slotLength (i, snap.swing)) - 4.0f, 4.0f);
        }
        // Current step and playhead.
        const double pos = editor.proc.uiPosition.load();
        const int cur = stepAt (pos, snap.swing);
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        g.fillRect (xOf (stepStart (cur, snap.swing)), 4.0f, xOf (slotLength (cur, snap.swing)), H - 8.0f);
        const float px = xOf (pos);
        g.setColour (cream.withAlpha (0.25f));
        g.fillRect (px - 2.5f, 2.0f, 5.0f, H - 4.0f);
        g.setColour (cream.withAlpha (0.95f));
        g.fillRect (px - 0.8f, 2.0f, 1.6f, H - 4.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto snap = editor.proc.snapshot();
        const double t = juce::jlimit (0.0, 15.9999, (double) e.position.x / getWidth() * numSteps);
        const int i = stepAt (t, snap.swing);
        auto cell = snap.pattern[(size_t) i];
        if (e.mods.isPopupMenu()) cell.slap = ! cell.slap;
        else if (e.mods.isShiftDown()) cell.v = (cell.v == Step::tie || i == 0 || snap.pattern[(size_t) i - 1].v == Step::off) ? Step::hit : Step::tie;
        else cell.v = cell.v == Step::hit ? Step::ghost : cell.v == Step::ghost ? Step::off : Step::hit;
        editor.proc.setCell (i, cell);
        editor.repaint();
    }
private:
    StutterEditor& editor;
};

// Slap-send dots under the lane, one per step at its swung centre.
class StutterEditor::Dots final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Dots (StutterEditor& e) : editor (e)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle ("Slap sends");
        setTooltip ("Lime dots send that hit to the slap. Click to toggle.");
    }
    void paint (juce::Graphics& g) override
    {
        const auto snap = editor.proc.snapshot();
        const float s = (float) getWidth() / dotsArt.getWidth();
        g.addTransform (juce::AffineTransform::scale (s));
        for (int i = 0; i < numSteps; ++i) {
            const auto& c = snap.pattern[(size_t) i];
            const float x = (float) ((stepStart (i, snap.swing) + slotLength (i, snap.swing) * 0.5) / numSteps * dotsArt.getWidth());
            const juce::Point<float> centre { x, 15.0f };
            if (c.v == Step::off) {
                g.setColour (lime.withAlpha (0.14f));
                g.drawEllipse (juce::Rectangle<float> (6, 6).withCentre (centre), 1.5f);
            } else if (c.slap) {
                g.setColour (lime.withAlpha (0.35f));
                g.fillEllipse (juce::Rectangle<float> (20, 20).withCentre (centre));
                g.setColour (lime);
                g.fillEllipse (juce::Rectangle<float> (12, 12).withCentre (centre));
            } else {
                g.setColour (lime.withAlpha (0.35f));
                g.drawEllipse (juce::Rectangle<float> (12, 12).withCentre (centre), 2.0f);
            }
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto snap = editor.proc.snapshot();
        const int i = stepAt (juce::jlimit (0.0, 15.9999, (double) e.position.x / getWidth() * numSteps), snap.swing);
        auto cell = snap.pattern[(size_t) i];
        cell.slap = ! cell.slap;
        if (cell.slap && cell.v == Step::off) cell.v = Step::hit;
        editor.proc.setCell (i, cell);
        editor.repaint();
    }
private:
    StutterEditor& editor;
};

// ---------------------------------------------------------------------------------------------------------------------

StutterEditor::StutterEditor (StutterProcessor& p)
    : AudioProcessorEditor (p), proc (p), shell (loadShell()),
      semiBold (juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchSemiBold_ttf, BinaryData::ChakraPetchSemiBold_ttfSize)),
      boldItalic (juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchBoldItalic_ttf, BinaryData::ChakraPetchBoldItalic_ttfSize)),
      skin (std::make_unique<Skin> (semiBold))
{
    setLookAndFeel (skin.get());
    setOpaque (true);

    addKnob ("swing", "SWING", 350, 818, 112, 124, 9, 0, 0, 18, 30, false,
             "Swing: where the second step of each pair lands. 50% is straight, 66.7% is triplet, 75% is fully dotted.");
    addKnob ("length", "LENGTH", 584, 905, 42, 51, 6, 818, 968, 18, 19, false, "How long each step stays open, as a share of its slot.");
    addKnob ("smooth", "SMOOTH", 724, 905, 42, 51, 6, 818, 968, 18, 19, false,
             "Gate edge time. Short is choppy; longer is rounder and click-free on subs. The attack is centred on the beat.");
    addKnob ("depth", "DEPTH", 864, 905, 42, 51, 6, 818, 968, 18, 19, false, "How far the gate closes between hits. FULL is silence.");
    addKnob ("slapTime", "TIME", 460, 1118, 37, 45, 5, 1041, 1172, 16, 17, true,
             "Slap time in ms. With LOCK on it follows the swing, so a downbeat's echo lands on the swung off-step. Dragging it unlocks.");
    addKnob ("slapFeedback", "FEEDBACK", 583, 1118, 37, 45, 5, 1041, 1172, 16, 17, true, "How many repeats each slap leaves.");
    addKnob ("slapTone", "TONE", 707, 1118, 37, 45, 5, 1041, 1172, 16, 17, true, "Darkens or brightens the echoes. The tone sits inside the feedback, so repeats get darker.");
    addKnob ("slapMix", "MIX", 833, 1118, 37, 45, 5, 1041, 1172, 16, 17, true, "Echo level over the gated sound. The dry level is unchanged.");
    for (auto& k : knobs)
        if (k->id == "slapTime")
            k->slider.beforeDrag = [this] {
                if (param ("slapLock") < 0.5f) return;
                const auto snap = proc.snapshot();
                setParam ("slapTime", (float) juce::jlimit (10.0, 250.0, snap.slapSeconds() * 1000.0));
                setParam ("slapLock", 0.0f);
            };

    lane = std::make_unique<Lane> (*this);
    dots = std::make_unique<Dots> (*this);
    addAndMakeVisible (*lane);
    addAndMakeVisible (*dots);

    rollFrom = std::make_unique<ScreenKey> ("Roll from", ScreenKey::Style::text, semiBold);
    rollFrom->label = [this] { return juce::String ("ROLL FROM ") + juce::String::charToString (0x25BE) + " |"
                                      + (proc.rollFrom < 0 ? juce::String ("ALL GROOVES") : juce::String (familyName ((Family) proc.rollFrom))); };
    rollFrom->setTooltip ("Which groove family NEW GROOVE rolls from. Click to cycle.");
    rollFrom->onClick = [this] { proc.rollFrom = proc.rollFrom + 1 >= (int) Family::count ? -1 : proc.rollFrom + 1; repaint(); };
    presetMenu = std::make_unique<ScreenKey> ("Factory grooves", ScreenKey::Style::overlay, semiBold);
    presetMenu->setTooltip ("Browse the factory grooves by folder.");
    presetMenu->onClick = [this] { showPresetMenu(); };
    prev = std::make_unique<ScreenKey> ("Previous factory groove", ScreenKey::Style::left, semiBold);
    next = std::make_unique<ScreenKey> ("Next factory groove", ScreenKey::Style::right, semiBold);
    prev->onClick = [this] { proc.loadFactory ((proc.groove.presetIndex < 0 ? 0 : proc.groove.presetIndex) - 1); repaint(); };
    next->onClick = [this] { proc.loadFactory ((proc.groove.presetIndex < 0 ? -1 : proc.groove.presetIndex) + 1); repaint(); };
    prev->setTooltip ("Previous factory groove");
    next->setTooltip ("Next factory groove");
    subSafe = std::make_unique<ScreenKey> ("Sub safe", ScreenKey::Style::readout, semiBold);
    subSafe->label = [this] { const int i = juce::roundToInt (param ("subSafe")); return juce::String (i == 0 ? "OFF" : i == 1 ? "80 Hz" : i == 2 ? "120 Hz" : "200 Hz"); };
    subSafe->setTooltip ("Lows below this never reach the slap, so the sub stays clean and mono. Click to cycle.");
    subSafe->onClick = [this] { setParam ("subSafe", (float) ((juce::roundToInt (param ("subSafe")) + 1) % 4)); repaint(); };
    int rateIndex = 0;
    for (auto* key : { &rate8, &rate16, &rate32 }) {
        const int i = rateIndex++;
        *key = std::make_unique<ScreenKey> (juce::String ("Rate ") + rateNames[i], ScreenKey::Style::key, semiBold);
        (*key)->label = [i] { return juce::String (rateNames[i]); };
        (*key)->lit = [this, i] { return juce::roundToInt (param ("rate")) == i; };
        (*key)->setTooltip ("Step size. The 16 steps span 2 bars at 1/8, 1 bar at 1/16 and half a bar at 1/32.");
        (*key)->onClick = [this, i] { setParam ("rate", (float) i); repaint(); };
    }
    for (auto* key : { presetMenu.get(), rollFrom.get(), prev.get(), next.get(), subSafe.get(), rate8.get(), rate16.get(), rate32.get() })
        addAndMakeVisible (*key);

    outputDrag = std::make_unique<Knob::Fine>();
    outputDrag->setSliderStyle (juce::Slider::RotaryVerticalDrag);
    outputDrag->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    outputDrag->setMouseDragSensitivity (240);
    outputDrag->setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    outputDrag->setTitle ("Output");
    outputDrag->setTooltip ("Output level. Drag up or down; double-click for 0 dB.");
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "output", *outputDrag);
    outputDrag->setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (*outputDrag);

    swingLock = std::make_unique<LockButton>();
    swingLock->setToggleState (proc.lockSwing, juce::dontSendNotification);
    swingLock->onClick = [this] { proc.lockSwing = swingLock->getToggleState(); };
    addAndMakeVisible (*swingLock);
    dice = std::make_unique<DiceButton> (boldItalic);
    dice->onClick = [this] { proc.rollNewGroove(); repaint(); };
    addAndMakeVisible (*dice);

    slapSwitch = std::make_unique<SlapSwitch> (semiBold);
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "slapOn", *slapSwitch));
    addAndMakeVisible (*slapSwitch);
    lockKey = std::make_unique<PlateKey> ("LOCK", semiBold);
    gapsKey = std::make_unique<PlateKey> ("GAPS", semiBold);
    wideKey = std::make_unique<PlateKey> ("WIDE", semiBold);
    lockKey->setTooltip ("Echo time follows the swing: a downbeat's slap lands exactly on the swung off-step.");
    gapsKey->setTooltip ("Echoes only sound while the gate is closed: call and response.");
    wideKey->setTooltip ("Offsets the right echo by 14 ms for width. The lows stay mono.");
    for (const auto& [key, id] : { std::pair { lockKey.get(), "slapLock" }, { gapsKey.get(), "slapGaps" }, { wideKey.get(), "slapWide" } }) {
        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, *key));
        addAndMakeVisible (*key);
    }

    const auto theme = vfd::themeFor ("STUTTER-STEP");
    infoButton = std::make_unique<vfd::InfoButton> (theme, semiBold);
    infoButton->setName ("INFO");
    infoOverlay = std::make_unique<vfd::InfoOverlay> ("STUTTER-STEP", theme, BinaryData::VfdLogo_svg, BinaryData::VfdLogo_svgSize, semiBold);
    infoButton->onClick = [this] { infoOverlay->open (infoButton.get()); };
    addAndMakeVisible (*infoButton);
    addChildComponent (*infoOverlay);

    setResizable (true, false);
    setResizeLimits (juce::roundToInt (baseWidth * 0.8f), juce::roundToInt (baseWidth * 0.8f * cropH / cropW),
                     juce::roundToInt (baseWidth * 1.5f), juce::roundToInt (baseWidth * 1.5f * cropH / cropW));
    if (auto* constrainer = getConstrainer()) constrainer->setFixedAspectRatio ((double) cropW / cropH);
    juce::Rectangle<int> userArea { 0, 0, 1440, 900 };
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        userArea = display->userBounds.toNearestInt();
    const auto fits = [userArea] (float sc) { return baseWidth * sc <= userArea.getWidth() && baseWidth * sc * cropH / cropW + 60 <= userArea.getHeight(); };
    float initial = proc.editorScale > 0 && fits (proc.editorScale) ? proc.editorScale : 1.0f;
    if (! fits (initial)) { initial = 0.8f; for (const float sc : menuScales) if (sc <= 1.0f && fits (sc)) initial = sc; }
    setSize (juce::roundToInt (baseWidth * initial), juce::roundToInt (baseWidth * initial * cropH / cropW));
    sizeReady = true;
    lastKey = displayKey();
    startTimerHz (30);
}

StutterEditor::~StutterEditor()
{
    stopTimer();
    buttonAttachments.clear();
    outputAttachment.reset();
    for (auto& k : knobs) k->attachment.reset();
    setLookAndFeel (nullptr);
}

void StutterEditor::addKnob (const char* id, const char* label, float cx, float cy, float rFace, float rArc, float arcW,
                             float labelY, float valueY, float labelSize, float valueSize, bool slapGroup, const char* tooltip)
{
    auto k = std::make_unique<Knob>();
    k->id = id; k->label = label; k->cx = cx; k->cy = cy; k->rFace = rFace; k->rArc = rArc; k->arcW = arcW;
    k->labelY = labelY; k->valueY = valueY; k->labelSize = labelSize; k->valueSize = valueSize; k->slapGroup = slapGroup;
    auto& s = k->slider;
    s.setName (label);
    s.setTitle (juce::String (label).substring (0, 1) + juce::String (label).substring (1).toLowerCase());
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setRotaryParameters (startAngle, startAngle + sweep, true);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.sensitivity = rFace > 100 ? 360 : 220;
    s.setMouseDragSensitivity (s.sensitivity);
    s.setScrollWheelEnabled (true);
    s.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    s.setTooltip (juce::String (tooltip) + " Shift-drag for fine moves; double-click to reset.");
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, s);
    if (auto* parameter = proc.apvts.getParameter (id))
        s.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    addAndMakeVisible (s);
    knobs.push_back (std::move (k));
}

// ---------------------------------------------------------------------------------------------------------------------

juce::Rectangle<int> StutterEditor::toEditor (juce::Rectangle<float> art) const
{
    const float s = scale();
    return juce::Rectangle<float> ((art.getX() - cropX) * s, (art.getY() - cropY) * s, art.getWidth() * s, art.getHeight() * s).toNearestInt();
}

juce::Font StutterEditor::font (float height, bool italic) const
{
    return juce::Font (juce::FontOptions (italic ? boldItalic : semiBold).withHeight (height));
}

void StutterEditor::text (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float size, juce::Colour c,
                          juce::Justification j, bool italic, float kerning) const
{
    g.setFont (font (size, italic).withExtraKerningFactor (kerning));
    g.setColour (c);
    g.drawText (t, r, j, false);
}

float StutterEditor::param (const juce::String& id) const
{
    if (auto* v = proc.apvts.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

void StutterEditor::setParam (const juce::String& id, float value)
{
    if (auto* parameter = proc.apvts.getParameter (id)) {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
}

void StutterEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    juce::String folder;
    juce::PopupMenu sub;
    const auto flush = [&] { if (folder.isNotEmpty()) menu.addSubMenu (folder, sub, true, nullptr, proc.groove.presetIndex >= 0 && folder == factoryGroove (proc.groove.presetIndex).folder); };
    for (int i = 0; i < numFactoryGrooves; ++i) {
        const auto& f = factoryGroove (i);
        if (folder != f.folder) { flush(); folder = f.folder; sub = {}; }
        sub.addItem (i + 1, f.name, true, i == proc.groove.presetIndex);
    }
    flush();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetMenu.get()),
                        [safe = juce::Component::SafePointer<StutterEditor> (this)] (int result) {
                            if (safe != nullptr && result > 0) { safe->proc.loadFactory (result - 1); safe->repaint(); }
                        });
}

void StutterEditor::resized()
{
    refreshShell();
    for (auto& k : knobs) k->slider.setBounds (toEditor (k->hit()));
    lane->setBounds (toEditor (laneArt));
    dots->setBounds (toEditor (dotsArt));
    rollFrom->setBounds (toEditor ({ 196, 194, 360, 30 }));
    presetMenu->setBounds (toEditor ({ 188, 226, 646, 46 }));
    prev->setBounds (toEditor ({ 846, 230, 40, 38 }));
    next->setBounds (toEditor ({ 892, 230, 40, 38 }));
    rate8->setBounds (toEditor ({ 196, 514, 58, 32 }));
    rate16->setBounds (toEditor ({ 260, 514, 58, 32 }));
    rate32->setBounds (toEditor ({ 324, 514, 58, 32 }));
    subSafe->setBounds (toEditor ({ 410, 514, 110, 32 }));
    outputDrag->setBounds (toEditor (outputArt));
    swingLock->setBounds (toEditor ({ 446, 620, 36, 36 }));
    dice->setBounds (toEditor ({ 585, 662, 310, 108 }));
    slapSwitch->setBounds (toEditor ({ 290, 1046, 96, 40 }));
    lockKey->setBounds (toEditor ({ 182, 1104, 68, 52 }));
    gapsKey->setBounds (toEditor ({ 256, 1104, 68, 52 }));
    wideKey->setBounds (toEditor ({ 330, 1104, 68, 52 }));
    infoButton->setBounds (toEditor ({ 485, 1230, 150, 56 }));
    infoOverlay->setBounds (getLocalBounds());
    if (sizeReady) proc.editorScale = (float) getWidth() / baseWidth;
}

void StutterEditor::refreshShell()
{
    if (! shell.isValid() || getWidth() <= 0) return;
    const float physical = juce::jmax (1.0f, (float) juce::Component::getApproximateScaleFactorForComponent (this));
    const int w = juce::roundToInt (getWidth() * physical), h = juce::roundToInt (getHeight() * physical);
    if (scaledShell.isValid() && scaledShell.getWidth() == w && scaledShell.getHeight() == h) return;
    scaledShell = shell.getClippedImage ({ cropX, cropY, cropW, cropH }).rescaled (w, h, juce::Graphics::highResamplingQuality);
}

void StutterEditor::applyScale (float sc)
{
    setSize (juce::roundToInt (baseWidth * sc), juce::roundToInt (baseWidth * sc * cropH / cropW));
}

void StutterEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) return;
    juce::PopupMenu menu;
    menu.addSectionHeader ("SIZE");
    juce::Rectangle<int> userArea { 0, 0, 1440, 900 };
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        userArea = display->userBounds.toNearestInt();
    const float current = (float) getWidth() / baseWidth;
    for (size_t i = 0; i < menuScales.size(); ++i) {
        const float sc = menuScales[i];
        const bool fits = baseWidth * sc <= userArea.getWidth() && baseWidth * sc * cropH / cropW + 60 <= userArea.getHeight();
        menu.addItem ((int) i + 1, juce::String (juce::roundToInt (sc * 100)) + "%", fits, std::abs (current - sc) < 0.01f);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                        [safe = juce::Component::SafePointer<StutterEditor> (this)] (int result) {
                            if (safe != nullptr && result > 0) safe->applyScale (menuScales[(size_t) result - 1]);
                        });
}

// ---------------------------------------------------------------------------------------------------------------------
// Painting (artwork coordinates)

void StutterEditor::paint (juce::Graphics& g)
{
    g.fillAll (bench);
    if (scaledShell.isValid())
        g.drawImage (scaledShell, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    g.addTransform (juce::AffineTransform::translation ((float) -cropX, (float) -cropY).scaled (scale()));
    drawHeader (g);
    drawScreen (g);
    drawPlates (g);
    for (const auto& k : knobs) drawKnob (g, *k);
}

void StutterEditor::drawHeader (juce::Graphics& g) const
{
    const juce::Rectangle<float> mark { 240, 98, 420, 56 };
    text (g, "STUTTER-STEP", mark.translated (0, 2), 46, juce::Colour (0xff063436).withAlpha (0.6f), juce::Justification::centredLeft, true, 0.01f);
    text (g, "STUTTER-STEP", mark, 46, cream, juce::Justification::centredLeft, true, 0.01f);
    text (g, juce::String ("SWING GATE ") + juce::String::charToString (0x00B7) + " SLAP", { 590, 102, 330, 50 }, 17, badgeText,
          juce::Justification::centred, false, 0.2f);
    if (ledLevel > 0.01f) {
        const float r = 26.0f;
        g.setGradientFill ({ juce::Colour (0xfff2fffd).withAlpha (ledLevel), ledCentre.x, ledCentre.y,
                             aqua.withAlpha (0.0f), ledCentre.x + r, ledCentre.y, true });
        g.fillEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre (ledCentre));
    }
}

void StutterEditor::drawScreen (juce::Graphics& g) const
{
    const auto snap = proc.snapshot();
    const auto& gr = proc.groove;
    // Row A: ROLL FROM is a child key; counter on the right.
    const juce::String counter = gr.presetIndex >= 0 ? "FACTORY " + juce::String (gr.presetIndex + 1) + " / " + juce::String (numFactoryGrooves)
                                                     : "SEED " + juce::String ((int) gr.seed).paddedLeft ('0', 5);
    text (g, counter, { 600, 194, 334, 30 }, 15, glassMuted, juce::Justification::centredRight, false, 0.14f);
    // Row B: groove name, family chip.
    const juce::String name = gr.name + (gr.edited ? " *" : "");
    const auto nameFont = font (34, true);
    const float nameW = juce::jmin (430.0f, juce::GlyphArrangement::getStringWidth (nameFont, name) + 4.0f);
    text (g, name, { 196, 228, nameW, 42 }, 34, glassText, juce::Justification::centredLeft, true);
    const juce::String fam = gr.presetIndex >= 0 ? juce::String (factoryGroove (gr.presetIndex).folder) : juce::String (familyName (gr.family));
    const auto chipFont = font (13).withExtraKerningFactor (0.16f);
    const float chipW = juce::GlyphArrangement::getStringWidth (chipFont, fam) + 18.0f;
    const juce::Rectangle<float> chip { 196 + nameW + 14, 236, chipW, 26 };
    g.setColour (aqua.withAlpha (0.45f));
    g.drawRoundedRectangle (chip, 6.0f, 1.5f);
    text (g, fam, chip, 13, aqua, juce::Justification::centred, false, 0.16f);
    text (g, juce::String::charToString (0x25BE), { chip.getRight() + 8, 236, 20, 26 }, 16, glassMuted, juce::Justification::centred);

    text (g, "GATE", { 196, 340, 54, 20 }, 13, glassMuted, juce::Justification::centredLeft, false, 0.16f);
    text (g, "SLAP", { 196, 446, 54, 20 }, 13, juce::Colour (0xffa9c96a), juce::Justification::centredLeft, false, 0.16f);
    // Beat labels at the straight grid; the span label on the right.
    const int rate = snap.rate;
    const char* labels16[] { "1", "2", "3", "4" };
    const char* labelsBar[] { "1", "", "2", "" };
    for (int k = 0; k < 4; ++k) {
        const juce::String l = rate == 1 ? labels16[k] : labelsBar[k];
        if (l.isNotEmpty()) text (g, l, { laneArt.getX() + k * laneArt.getWidth() / 4 + 3, 418, 30, 18 }, 13, glassMuted);
    }
    text (g, rate == 0 ? "2 BARS" : rate == 1 ? "1 BAR" : juce::String::fromUTF8 ("\xc2\xbd BAR"), { 834, 418, 100, 18 }, 13, glassMuted,
          juce::Justification::centredRight, false, 0.14f);
    g.setColour (aqua.withAlpha (0.14f));
    g.fillRect (juce::Rectangle<float> (196, 482, 738, 1));
    // Bottom row labels, OUTPUT readout, meter.
    text (g, "RATE", { 196, 494, 100, 16 }, 12, glassMuted, juce::Justification::centredLeft, false, 0.16f);
    text (g, "SUB SAFE", { 410, 494, 110, 16 }, 12, glassMuted, juce::Justification::centredLeft, false, 0.16f);
    text (g, "OUTPUT", { 540, 494, 110, 16 }, 12, glassMuted, juce::Justification::centredLeft, false, 0.16f);
    const auto out = outputArt.reduced (1);
    g.setColour (keyFill.withAlpha (0.8f));
    g.fillRoundedRectangle (out, 7);
    g.setColour (aqua.withAlpha (outputDrag->hasKeyboardFocus (true) ? 0.9f : 0.26f));
    g.drawRoundedRectangle (out, 7, 1.5f);
    const float outDb = param ("output");
    text (g, (outDb > 0.04f ? "+" : outDb < -0.04f ? juce::String::fromUTF8 ("\xe2\x88\x92") : juce::String()) + juce::String (std::abs (outDb), 1) + " dB",
          out, 16, glassText, juce::Justification::centred);
    text (g, juce::String (juce::roundToInt (snap.bpm)) + " BPM", { meterArt.getX(), 494, 110, 16 }, 11, glassMuted, juce::Justification::centredLeft, false, 0.16f);
    text (g, proc.uiPlaying.load() ? "PLAYING" : "FREE RUN", { meterArt.getRight() - 110, 494, 110, 16 }, 11,
          proc.uiPlaying.load() ? aqua : glassMuted, juce::Justification::centredRight, false, 0.16f);
    for (int ch = 0; ch < 2; ++ch) {
        const float y = 516.0f + ch * 16.0f;
        text (g, ch == 0 ? "L" : "R", { meterArt.getX(), y - 2, 14, 15 }, 11, glassMuted);
        const juce::Rectangle<float> bar { meterArt.getX() + 18, y, meterArt.getWidth() - 18, 11 };
        const float level = ch == 0 ? meterLeft : meterRight;
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float fill = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
        for (float x = bar.getX(); x < bar.getRight() - 1; x += 12.0f) {
            const bool on = (x - bar.getX()) / bar.getWidth() < fill;
            g.setColour (on ? (db > -1.0f && x > bar.getRight() - 30 ? juce::Colour (0xffff8a6a) : aqua) : aqua.withAlpha (0.10f));
            g.fillRoundedRectangle ({ x, y, 9.0f, 11.0f }, 2.0f);
        }
    }
}

void StutterEditor::drawPlates (juce::Graphics& g) const
{
    const auto snap = proc.snapshot();
    const auto embossed = [&] (const juce::String& t, juce::Rectangle<float> r, float size, juce::Colour c, juce::Justification j, bool italic, float kern) {
        text (g, t, r.translated (0, 1), size, juce::Colours::white.withAlpha (0.45f * c.getFloatAlpha()), j, italic, kern);
        text (g, t, r, size, c, j, italic, kern);
    };
    embossed ("SWING", { 250, 630, 200, 24 }, 18, ink, juce::Justification::centred, false, 0.16f);
    embossed ("STRAIGHT", { 166, 936, 110, 16 }, 14, ink.withAlpha (0.72f), juce::Justification::centredRight, false, 0.12f);
    embossed ("DRUNK", { 426, 936, 110, 16 }, 14, ink.withAlpha (0.72f), juce::Justification::centredLeft, false, 0.12f);
    // Swing value: big number, small word.
    const juce::String number = juce::String (snap.swing * 100.0, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "%";
    const juce::String word = swingWord (snap.swing);
    const float nW = juce::GlyphArrangement::getStringWidth (font (30), number);
    const float wW = juce::GlyphArrangement::getStringWidth (font (16).withExtraKerningFactor (0.14f), word);
    const float x0 = 350.0f - (nW + 10.0f + wW) * 0.5f;
    embossed (number, { x0, 960, nW + 4, 34 }, 30, ink, juce::Justification::centredLeft, false, 0.0f);
    embossed (word, { x0 + nW + 10, 968, wW + 6, 24 }, 16, ink, juce::Justification::centredLeft, false, 0.14f);

    const float slapAlpha = snap.slapOn ? 1.0f : 0.45f;
    embossed ("SLAP", { 184, 1050, 100, 34 }, 28, ink, juce::Justification::centredLeft, true, 0.0f);
    juce::String hint = ! snap.slapOn ? "Slap is off"
                      : snap.slapLock ? "Locked to swing " + juce::String::charToString (0x00B7) + " " + juce::String (juce::roundToInt (snap.slapSeconds() * 1000.0)) + " ms"
                      : "Free time " + juce::String::charToString (0x00B7) + " drag TIME";
    text (g, hint, { 182, 1166, 230, 18 }, 13, ink.withAlpha (0.72f * slapAlpha));
}

float StutterEditor::knobNormalised (const Knob& k) const
{
    auto* parameter = proc.apvts.getParameter (k.id);
    if (parameter == nullptr) return 0;
    if (k.id == "slapTime" && param ("slapLock") > 0.5f)
        return parameter->convertTo0to1 ((float) juce::jlimit (10.0, 250.0, proc.snapshot().slapSeconds() * 1000.0));
    return parameter->getValue();
}

juce::String StutterEditor::knobValueText (const Knob& k) const
{
    const float v = param (k.id);
    if (k.id == "length" || k.id == "slapFeedback" || k.id == "slapMix") return percent (v / 100.0);
    if (k.id == "smooth") return juce::String (v, v < 10 ? 1 : 0) + " ms";
    if (k.id == "depth") {
        if (v >= 99.5f) return "FULL";
        if (v < 0.5f) return "NONE";
        return juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (juce::roundToInt (-juce::Decibels::gainToDecibels (StutterProcessor::closedGain (v / 100.0)))) + " dB";
    }
    if (k.id == "slapTime") return juce::String (juce::roundToInt (param ("slapLock") > 0.5f ? proc.snapshot().slapSeconds() * 1000.0 : v)) + " ms";
    if (k.id == "slapTone") return hertz (StutterProcessor::toneHz (v / 100.0));
    return {};
}

void StutterEditor::drawKnob (juce::Graphics& g, const Knob& k) const
{
    const bool dim = k.slapGroup && param ("slapOn") < 0.5f;
    const float n = juce::jlimit (0.0f, 1.0f, knobNormalised (k));
    const float angle = startAngle + sweep * n;
    juce::Path arc;
    arc.addCentredArc (k.cx, k.cy, k.rArc, k.rArc, 0, startAngle, juce::jmax (startAngle + 0.01f, angle), true);
    const float arcAlpha = dim ? 0.25f : 1.0f;
    g.setColour (aqua.withAlpha (0.25f * arcAlpha));
    g.strokePath (arc, juce::PathStrokeType (k.arcW * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (aqua.withAlpha (arcAlpha));
    g.strokePath (arc, juce::PathStrokeType (k.arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto at = [&] (float r) { return juce::Point<float> (k.cx + r * std::sin (angle), k.cy - r * std::cos (angle)); };
    g.setColour (pointerInk.withAlpha (dim ? 0.5f : 1.0f));
    g.drawLine ({ at (k.rFace * 0.3f), at (k.rFace * 0.84f) }, juce::jmax (4.0f, k.rFace * 0.085f));
    if (k.slider.hasKeyboardFocus (true)) {
        g.setColour (aqua);
        g.drawEllipse (k.hit().reduced (2.0f), 3.0f);
    }
    if (k.labelY > 0) {
        const float a = dim ? 0.45f : 1.0f;
        text (g, k.label, { k.cx - 90, k.labelY + 1, 180, k.labelSize + 4 }, k.labelSize, juce::Colours::white.withAlpha (0.45f * a), juce::Justification::centred, false, 0.16f);
        text (g, k.label, { k.cx - 90, k.labelY, 180, k.labelSize + 4 }, k.labelSize, ink.withAlpha (a), juce::Justification::centred, false, 0.16f);
        text (g, knobValueText (k), { k.cx - 90, k.valueY, 180, k.valueSize + 4 }, k.valueSize, ink.withAlpha (a), juce::Justification::centred);
    }
}

// ---------------------------------------------------------------------------------------------------------------------

std::uint64_t StutterEditor::displayKey() const
{
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h] (std::uint64_t v) { h ^= v; h *= 1099511628211ull; };
    for (auto* parameter : proc.getParameters())
        mix ((std::uint64_t) juce::roundToInt (parameter->getValue() * 100000.0f));
    mix (proc.grooveRevision.load());
    mix ((std::uint64_t) (proc.rollFrom + 2));
    mix (proc.uiPlaying.load() ? 1 : 0);
    mix ((std::uint64_t) juce::roundToInt (proc.uiBpm.load() * 10.0));
    return h;
}

void StutterEditor::timerCallback()
{
    const float peakL = proc.uiPeakLeft.exchange (0.0f), peakR = proc.uiPeakRight.exchange (0.0f);
    meterLeft = juce::jmax (peakL, meterLeft * 0.82f);
    meterRight = juce::jmax (peakR, meterRight * 0.82f);
    const float closed = StutterProcessor::closedGain (param ("depth") / 100.0f);
    ledLevel = closed >= 0.999f ? 0.35f : juce::jlimit (0.0f, 1.0f, (proc.uiGate.load() - closed) / (1.0f - closed));
    if (param ("bypass") > 0.5f) ledLevel = 0.0f;
    if (swingLock->getToggleState() != proc.lockSwing) swingLock->setToggleState (proc.lockSwing, juce::dontSendNotification);

    const auto key = displayKey();
    if (key != lastKey) {
        lastKey = key;
        repaint();
        return;
    }
    lane->repaint();
    repaint (toEditor ({ ledCentre.x - 30, ledCentre.y - 30, 60, 60 }));
    repaint (toEditor (meterArt));
}
