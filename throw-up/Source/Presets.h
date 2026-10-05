#pragma once
#include "ReverbEngine.h"
#include <array>

namespace throwup
{
struct FactoryPreset
{
    const char* name;
    const char* group;
    const char* description;
    Settings settings;
};

// The bank is stable: 10 Small, 10 Medium, 10 Large, 10 Huge, then 10 Creative.
// Preset recalls should retain the current mix, output, and bypass values.
const std::array<FactoryPreset, 50>& factoryPresets();
} // namespace throwup
