#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace yoiboi
{
// Standalone settings that persist only the audio/MIDI device choice. The
// JUCE holder also writes the sound ("filterState") and a file-chooser path to
// its PropertySet; both are dropped here, so the standalone app still never
// saves or restores a sound automatically.
class DeviceOnlySettings final : public juce::PropertySet
{
public:
    static constexpr std::array<const char*, 2> persistedKeys { "audioSetup", "shouldMuteInput" };

    static juce::PropertiesFile::Options fileOptions()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "YOI-BOI";
        options.filenameSuffix = ".settings";
        options.folderName = "YOI-BOI";
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.millisecondsBeforeSaving = -1; // saved explicitly below
        return options;
    }

    // ~/Library/Application Support/YOI-BOI/YOI-BOI.settings on macOS.
    static juce::File defaultFile() { return fileOptions().getDefaultFile(); }

    explicit DeviceOnlySettings (const juce::File& file) : stored (file, fileOptions())
    {
        const juce::ScopedValueSetter<bool> loading (updating, true);
        for (const auto* key : persistedKeys)
            if (stored.containsKey (key)) setValue (key, stored.getValue (key));
    }

    static bool isPersisted (const juce::String& key)
    {
        for (const auto* allowed : persistedKeys)
            if (key == allowed) return true;
        return false;
    }

    void propertyChanged() override
    {
        if (updating) return;
        const juce::ScopedValueSetter<bool> guard (updating, true);
        const auto keys = getAllProperties().getAllKeys();
        for (const auto& key : keys)
            if (! isPersisted (key)) removeValue (key);
        for (const auto* key : persistedKeys)
            if (containsKey (key)) stored.setValue (key, getValue (key));
        stored.saveIfNeeded();
    }

private:
    juce::PropertiesFile stored;
    bool updating = false;
};

// Only the bare shell moves the borderless window. A click on a control in
// the header (ZOOM, CLOSE) belongs to that control, not to a window drag.
inline bool startsDeviceWindowDrag (const juce::Component& editor, const juce::Component* clicked,
                                    juce::Point<float> positionInEditor, bool leftButton)
{
    if (! leftButton || clicked == nullptr || editor.getWidth() <= 0) return false;
    const bool onShell = clicked == &editor || clicked->getParentComponent() == &editor;
    const auto scale = static_cast<float> (editor.getWidth()) / 1055.0f;
    return onShell && positionInEditor.y < 68.0f * scale
        && positionInEditor.x > 38.0f * scale && positionInEditor.x < 458.0f * scale;
}
} // namespace yoiboi
