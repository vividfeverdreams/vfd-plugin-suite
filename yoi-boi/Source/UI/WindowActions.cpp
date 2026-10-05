#include "WindowActions.h"

namespace yoiboi
{
bool canCloseEditorWindow (juce::Component& editor) { return deviceWindowFor (editor) != nullptr; }
void closeEditorWindow (juce::Component& editor)
{
    juce::Component::SafePointer<juce::Component> safe (&editor);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe != nullptr)
            if (auto* window = deviceWindowFor (*safe)) window->closeDeviceWindow();
    });
}
}
