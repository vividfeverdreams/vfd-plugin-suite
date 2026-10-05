#include "WindowActions.h"
#import <AppKit/AppKit.h>

namespace yoiboi
{
namespace
{
NSWindow* floatingEditorWindow (juce::Component& editor)
{
    auto* peer = editor.getPeer();
    if (peer == nullptr) return nil;
    auto* view = static_cast<NSView*> (peer->getNativeHandle());
    if (view == nil || ! [view isKindOfClass: [NSView class]]) return nil;
    auto* window = [view window];
    if (window == nil || ([window styleMask] & NSWindowStyleMaskClosable) == 0) return nil;
    // Only request a normal close for a dedicated plug-in window. An editor
    // embedded inside a DAW's main window must never close the DAW window.
    if ([[window title] rangeOfString: @"YOI-BOI" options: NSCaseInsensitiveSearch].location == NSNotFound)
        return nil;
    const auto contentSize = [[window contentView] bounds].size;
    const auto editorSize = [view bounds].size;
    if (contentSize.width > editorSize.width + 80.0 || contentSize.height > editorSize.height + 100.0)
        return nil;
    return window;
}
}

bool canCloseEditorWindow (juce::Component& editor)
{
    return deviceWindowFor (editor) != nullptr || floatingEditorWindow (editor) != nil;
}

void closeEditorWindow (juce::Component& editor)
{
    // Queue the request so closing cannot delete a button during its callback.
    juce::Component::SafePointer<juce::Component> safe (&editor);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe == nullptr) return;
        if (auto* ownWindow = deviceWindowFor (*safe)) ownWindow->closeDeviceWindow();
        else if (auto* hostWindow = floatingEditorWindow (*safe)) [hostWindow performClose: nil];
    });
}
}
