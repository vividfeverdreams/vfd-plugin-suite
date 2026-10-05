#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace yoiboi
{
// Implemented by our own standalone window. Hosted editors never change their
// host's window styles, ownership, delegates, or visibility bookkeeping.
class DeviceWindowActions
{
public:
    virtual ~DeviceWindowActions() = default;
    virtual void closeDeviceWindow() = 0;
    virtual void showDeviceAudioSettings() = 0;
};

inline DeviceWindowActions* deviceWindowFor (juce::Component& component)
{
    for (auto* parent = &component; parent != nullptr; parent = parent->getParentComponent())
        if (auto* window = dynamic_cast<DeviceWindowActions*> (parent)) return window;
    return nullptr;
}

bool canCloseEditorWindow (juce::Component&);
void closeEditorWindow (juce::Component&);
}
