#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <optional>
#include <vector>

// Factory and user presets. A preset is a complete set of control values (everything except HARVEST).
// User presets are XML files in <root>/<Folder>/<Name>.ehpreset; saving never overwrites an existing file.
namespace energy::presets {
struct Preset {
    juce::String folder, name, note;
    std::map<juce::String,float> values;
    bool factory = true;
    juce::File file;
};
const std::vector<Preset>& factory();
juce::StringArray factoryFolders();
// ENERGY_HARVESTER_PRESET_ROOT overrides the location (tests); otherwise $HOME/Library/Application Support/
// ENERGY HARVESTER/Presets on macOS, using $HOME so a redirected home (validators, tests) never reaches the real
// folder, %APPDATA%\ENERGY HARVESTER\Presets on Windows and ~/.config/ENERGY HARVESTER/Presets on Linux.
juce::File userRoot();
juce::StringArray userFolders(const juce::File& root);
std::vector<Preset> userPresets(const juce::File& root);
std::optional<Preset> read(const juce::File& file);
// Writes a new file and never replaces one: a taken name gets a numbered suffix. Creates the folder if needed.
juce::Result save(const juce::File& root, const juce::String& folder, const juce::String& name,
                  const std::map<juce::String,float>& values, juce::File& written);
juce::String cleanName(const juce::String& text);
inline constexpr const char* extension = ".ehpreset";
}
