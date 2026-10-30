#pragma once

// Host-keys check shared by every plug-in's test app (rule and design: shared/VfdHostKeys.h).
// Opens the real editor in an off-screen window and sends keys and clicks through its peer, the same
// path a host's key events take.
//
//   vfd::test::HostKeysProbe probe;
//   probe.currentPreset = [&] { return processor.currentPresetName(); };
//   probe.blankSpot = { 30, 600 };                       // editor-local point on plain shell art
//   probe.openTextField = [&] { return editor.beginTypingSomething(); };   // optional
//   failures += vfd::test::runHostKeys (*editor, probe, std::cout);
//
// Needs a display; prints "skip" and passes without one.

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <ostream>
#include <typeinfo>
#include <vector>

#include "VfdHostKeys.h"
#include "VfdInfo.h"

#if ! JUCE_MODAL_LOOPS_PERMITTED
 #error "HostKeysTest.h pumps the message loop: build the test target with JUCE_MODAL_LOOPS_PERMITTED=1"
#endif

namespace vfd::test {

struct HostKeysProbe
{
    std::function<juce::String()> currentPreset;     // the loaded preset's name as the editor shows it
    juce::Point<int> blankSpot;                       // editor-local point where a click does nothing
    std::function<juce::TextEditor*()> openTextField; // optional: opens a field the user can type in
    std::vector<juce::Point<int>> controlSpots;       // optional: knobs, keys and switches safe to click once
    int settleMs = 60;                                // time for an async preset load to show
};

inline int runHostKeys (juce::Component& editor, const HostKeysProbe& probe, std::ostream& out)
{
    int failures = 0;
    const auto expect = [&] (bool ok, const juce::String& what)
    {
        out << (ok ? "  ok   " : "  FAIL ") << what << "\n";
        if (! ok)
            ++failures;
    };
    const auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
    const auto describe = [] (juce::Component* c) -> juce::String
    {
        if (c == nullptr)
            return "nothing";
        return "'" + c->getName() + "' (" + juce::String (typeid (*c).name()) + ")";
    };

    out << "host keys\n";
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
    {
        out << "  skip (no display)\n";
        return 0;
    }

    juce::Component holder; // stands in for the plug-in wrapper's component, as in a host
    holder.setSize (editor.getWidth(), editor.getHeight());
    holder.addAndMakeVisible (editor);
    holder.setTopLeftPosition (-6000, -6000);
    holder.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    holder.setVisible (true);
    pump (600); // let the window finish opening and activating
    auto* peer = holder.getPeer();
    expect (peer != nullptr && editor.isShowing(), "editor window opens");
    if (peer == nullptr || ! editor.isShowing())
    {
        holder.removeFromDesktop();
        holder.removeChildComponent (&editor);
        return failures;
    }

    // A real key carries its text character, which a focused text field would type.
    const auto press = [peer] (int key, juce::ModifierKeys mods = {})
    {
        const auto text = key == juce::KeyPress::spaceKey ? juce::juce_wchar (' ')
                          : (key >= 'a' && key <= 'z' && ! mods.isCommandDown()) ? juce::juce_wchar (key)
                                                                                  : juce::juce_wchar (0);
        return peer->handleKeyPress (juce::KeyPress (key, mods, text));
    };
    const auto reachesHost = [&] (int key, juce::ModifierKeys mods = {}) { return ! press (key, mods); };
    const auto focused = [] { return juce::Component::getCurrentlyFocusedComponent(); };
    const auto click = [&] (juce::Point<int> editorPoint)
    {
        const auto at = holder.getLocalPoint (&editor, editorPoint).toFloat();
        const auto t = juce::Time::currentTimeMillis();
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, at,
                                juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), juce::MouseInputSource::defaultPressure,
                                juce::MouseInputSource::defaultOrientation, t);
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, at, juce::ModifierKeys(),
                                juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, t + 30);
        pump (30);
    };
    const auto hostKeysReachHost = [&] (const juce::String& when)
    {
        expect (reachesHost (juce::KeyPress::spaceKey), "space reaches the host " + when);
        expect (reachesHost (juce::KeyPress::returnKey), "Return reaches the host " + when);
        expect (reachesHost ('a'), "letter keys (Live's computer keyboard) reach the host " + when);
        expect (reachesHost ('z', juce::ModifierKeys::commandModifier), "Cmd-Z reaches the host " + when);
        expect (reachesHost ('z', juce::ModifierKeys (juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier)),
                "Shift-Cmd-Z reaches the host " + when);
        expect (reachesHost (juce::KeyPress::leftKey, juce::ModifierKeys::commandModifier),
                "Cmd-arrow reaches the host " + when);
    };
    const auto arrowsBrowse = [&] (const juce::String& when)
    {
        const auto before = probe.currentPreset != nullptr ? probe.currentPreset() : juce::String();
        const bool used = press (juce::KeyPress::rightKey);
        pump (probe.settleMs);
        const auto after = probe.currentPreset != nullptr ? probe.currentPreset() : juce::String();
        expect (used, "the right arrow is kept for presets " + when);
        if (probe.currentPreset != nullptr)
            expect (after != before, "the right arrow loads the next preset " + when + " ('" + before + "' -> '" + after + "')");
        press (juce::KeyPress::leftKey);
        pump (probe.settleMs);
        if (probe.currentPreset != nullptr)
            expect (probe.currentPreset() == before, "the left arrow goes back to '" + before + "' " + when);
    };

    // Nothing has been clicked yet: every key belongs to the host.
    expect (reachesHost (juce::KeyPress::spaceKey), "space reaches the host with the editor open");

    // Policy: only text fields and allowed components (menus, the INFO page) may take the keyboard.
    {
        juce::StringArray grabbers;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& parent)
        {
            for (auto* child : parent.getChildren())
            {
                const bool allowed = HostKeys::isTextField (child)
                                     || child->getProperties().getWithDefault ("vfdHostKeysAllowFocus", false);
                if (! allowed && child->getWantsKeyboardFocus())
                    grabbers.add (describe (child));
                walk (*child);
            }
        };
        walk (editor);
        expect (grabbers.isEmpty(), "no control can take the keyboard"
                                        + (grabbers.isEmpty() ? juce::String() : ": " + grabbers.joinIntoString (", ")));
    }

    // A click in the plug-in gives the editor the arrow keys and nothing else.
    click (probe.blankSpot);
    expect (focused() == &editor, "clicking the plug-in focuses the editor (focus on " + describe (focused()) + ")");
    hostKeysReachHost ("after a click");
    arrowsBrowse ("after a click");
    expect (reachesHost (juce::KeyPress::tabKey), "Tab reaches the host");
    expect (focused() == nullptr, "Tab leaves nothing in the plug-in focused");

    // Clicking a control must not leave it holding Return or space (a focused button answers Return).
    for (const auto& spot : probe.controlSpots)
    {
        click (spot);
        auto* hit = editor.getComponentAt (spot);
        const auto where = describe (hit) + " at " + spot.toString();
        expect (focused() == &editor, "clicking " + where + " focuses only the editor (focus on " + describe (focused()) + ")");
        expect (reachesHost (juce::KeyPress::returnKey) && reachesHost (juce::KeyPress::spaceKey),
                "Return and space reach the host after clicking " + where);
    }
    click (probe.blankSpot);

    // The INFO page: arrows can't change the preset under it, everything else reaches the host.
    {
        InfoOverlay* info = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& parent)
        {
            for (auto* child : parent.getChildren())
            {
                if (info == nullptr)
                    info = dynamic_cast<InfoOverlay*> (child);
                find (*child);
            }
        };
        find (editor);
        expect (info != nullptr, "editor has the INFO page");
        if (info != nullptr)
        {
            info->open (nullptr);
            pump (30);
            const auto before = probe.currentPreset != nullptr ? probe.currentPreset() : juce::String();
            hostKeysReachHost ("with INFO open");
            press (juce::KeyPress::rightKey);
            pump (probe.settleMs);
            if (probe.currentPreset != nullptr)
                expect (probe.currentPreset() == before, "arrows don't change the preset under INFO");
            expect (info->isVisible(), "keys don't close INFO");
            press (juce::KeyPress::escapeKey);
            expect (! info->isVisible(), "Escape closes INFO");
            expect (focused() == &editor, "closing INFO gives the editor its arrow keys back (focus on " + describe (focused()) + ")");
            hostKeysReachHost ("after INFO closes");
            arrowsBrowse ("after INFO closes");
        }
    }

    // A text field being typed in keeps the keyboard; clicking elsewhere releases it.
    if (probe.openTextField != nullptr)
    {
        auto* field = probe.openTextField();
        pump (30);
        expect (field != nullptr && focused() == field, "typing in a field takes the keys (focus on " + describe (focused()) + ")");
        expect (! reachesHost (juce::KeyPress::spaceKey), "a field being typed in keeps the space bar");
        click (probe.blankSpot);
        pump (50);
        expect (focused() == &editor, "clicking elsewhere releases the field (focus on " + describe (focused()) + ")");
        hostKeysReachHost ("after leaving a field");
        arrowsBrowse ("after leaving a field");
    }

    holder.removeFromDesktop();
    holder.removeChildComponent (&editor);
    out << "HOST KEYS: " << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << "\n";
    return failures;
}

} // namespace vfd::test
