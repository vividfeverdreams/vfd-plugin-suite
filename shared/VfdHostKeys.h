#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Host keys, the suite rule for every plug-in window.
//
// The plug-in keeps only:
//   - the plain arrow keys, which step to the previous / next preset while its window is focused, and
//   - the whole keyboard while one of its text fields is being typed in, or while a menu or page that
//     asked for the keys (HostKeys::allowFocus) is open.
// Everything else reaches the host: space for play/stop, Return, Tab (Live's view switch), letters
// (Live's computer MIDI keyboard) and every shortcut, Cmd-Z included.
//
// How: the editor itself takes keyboard focus when it is clicked, so its arrow keys can browse presets.
// No other control takes focus, so no button can answer Return and nothing else consumes keys. Clicking
// outside a text field releases it. The focus traverser has no default component, so closing a menu or
// field never hands focus to a button.
//
//   class MyEditor : public juce::AudioProcessorEditor {
//       vfd::HostKeys hostKeys { *this, [this] (int step) { loadAdjacentPreset (step); } };
//       std::unique_ptr<juce::ComponentTraverser> createKeyboardFocusTraverser() override
//       { return vfd::HostKeys::createTraverser(); }
//       // at the end of the constructor, and after adding controls later: hostKeys.apply();
//   };
//
// A control that opens a text field from its own mouseDown must open it under the pointer (or open it
// asynchronously); the click that opened it would otherwise count as a click elsewhere.
namespace vfd {

class HostKeys final : private juce::KeyListener,
                       private juce::MouseListener,
                       private juce::FocusChangeListener
{
public:
    // Plugs into the editor; stepPreset gets -1 (left / up) or +1 (right / down). Pass nullptr for none.
    HostKeys (juce::Component& editorToManage, std::function<void (int)> stepPreset)
        : editor (editorToManage), step (std::move (stepPreset))
    {
        editor.getProperties().set (editorProperty, true);
        editor.setWantsKeyboardFocus (true);
        editor.setMouseClickGrabsKeyboardFocus (false); // focus is taken in mouseDown below instead
        editor.setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
        editor.addKeyListener (this);
        editor.addMouseListener (this, true);
        juce::Desktop::getInstance().addFocusChangeListener (this);
    }

    ~HostKeys() override
    {
        juce::Desktop::getInstance().removeFocusChangeListener (this);
        editor.removeMouseListener (this);
        editor.removeKeyListener (this);
    }

    // Marks a component (a menu overlay, the INFO page) that may hold the keyboard while it is open.
    static void allowFocus (juce::Component& c) { c.getProperties().set (allowFocusProperty, true); }

    // Takes keyboard focus away from every control except text fields and allowed components.
    // Call it after the editor has built its controls, and again after adding controls later.
    void apply() { applyTo (editor); }

    static std::unique_ptr<juce::ComponentTraverser> createTraverser() { return std::make_unique<Traverser>(); }

    static bool isTextField (const juce::Component* c) { return dynamic_cast<const juce::TextEditor*> (c) != nullptr; }

    // The editor a HostKeys manages that contains c (or is c), or nullptr.
    static juce::Component* findEditor (juce::Component* c)
    {
        for (; c != nullptr; c = c->getParentComponent())
            if (c->getProperties().getWithDefault (editorProperty, false))
                return c;
        return nullptr;
    }

    // True for c and anything inside a component marked with allowFocus, below the editor.
    bool isWithinAllowed (const juce::Component* c) const
    {
        for (; c != nullptr && c != &editor; c = c->getParentComponent())
            if (c->getProperties().getWithDefault (allowFocusProperty, false))
                return true;
        return false;
    }

private:
    static constexpr const char* allowFocusProperty = "vfdHostKeysAllowFocus";
    static constexpr const char* editorProperty = "vfdHostKeysEditor";

    // JUCE normally hands focus to the first focusable child when a focused field or menu closes, which
    // left a button holding the keyboard. With no default component, nothing is picked.
    struct Traverser final : juce::KeyboardFocusTraverser
    {
        juce::Component* getDefaultComponent (juce::Component*) override { return nullptr; }
    };

    void applyTo (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (! isTextField (child) && ! child->getProperties().getWithDefault (allowFocusProperty, false))
            {
                child->setWantsKeyboardFocus (false);
                child->setMouseClickGrabsKeyboardFocus (false);
            }
            applyTo (*child);
        }
    }

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        // Only plain arrows: Shift/Cmd/Alt/Ctrl + arrow belong to the host.
        if (key.getModifiers().isAnyModifierKeyDown())
            return false;
        const int code = key.getKeyCode();
        const int direction = (code == juce::KeyPress::leftKey || code == juce::KeyPress::upKey)      ? -1
                              : (code == juce::KeyPress::rightKey || code == juce::KeyPress::downKey) ? 1
                                                                                                       : 0;
        if (direction != 0)
        {
            if (step != nullptr)
                step (direction);
            return step != nullptr;
        }
        // JUCE moves focus on any Tab that nothing used while something is focused, which would keep Tab
        // from the host. Letting go of the keyboard first passes it on.
        if (code == juce::KeyPress::tabKey)
            editor.giveAwayKeyboardFocus();
        return false;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* target = e.eventComponent;
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        const bool focusInside = focused != nullptr && focused != &editor && editor.isParentOf (focused);
        if (focusInside && ! isWithinAllowed (focused))
        {
            // A click on the field (or the control under it that opened it) keeps it; any other click
            // releases it, committing what was typed, so the field doesn't keep the space bar.
            const bool onTheField = focused->getScreenBounds().expanded (2).contains (e.getScreenPosition());
            if (! onTheField)
                focused->giveAwayKeyboardFocus();
        }
        if (isWithinAllowed (target))
            return; // menus and the INFO page manage their own focus
        auto* now = juce::Component::getCurrentlyFocusedComponent();
        const bool fieldKept = now != nullptr && now != &editor && editor.isParentOf (now);
        if (! fieldKept && editor.isShowing())
            editor.grabKeyboardFocus();
    }

    // Safety net for controls added after apply(): if one of them takes focus anyway, hand it to the editor.
    void globalFocusChanged (juce::Component* focused) override
    {
        if (focused == nullptr || focused == &editor || ! editor.isParentOf (focused))
            return;
        if (isTextField (focused) || isWithinAllowed (focused))
            return;
        if (editor.isShowing())
            editor.grabKeyboardFocus();
    }

    juce::Component& editor;
    std::function<void (int)> step;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostKeys)
};

} // namespace vfd
