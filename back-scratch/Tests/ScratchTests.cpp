#include "PatternTests.h"
#include "PatternEditingTests.h"
#include "DrawToolsTests.h"
#include "MotionProfileTests.h"
#include "LiveEngineTests.h"
#include "EngineContinuityTests.h"
#include "FeatureEngineTests.h"
#include <iostream>
#include <string>

namespace {
// PRESETS.md tables, generated from the factory bank itself.
void printPresetMarkdown()
{
    using namespace backScratch;
    const char* lengths[] {"1/2 beat", "1 beat", "2 beats", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars"};
    const char* plays[] {"every phrase", "last of 2", "last of 4", "last of 8"};
    const char* chains[] {"", "A-B", "A-D", "A-H"};
    const char* splits[] {"120 Hz", "160 Hz", "200 Hz", "300 Hz"};
    const char* looks[] {"", "1 beat", "1 bar", "2 bars", "4 bars"};
    const char* snaps[] {"1/16", "1/8", "beat", "half bar", "bar"};
    for (const auto& folder : bankFolders()) {
        // Bass, Drums, Vocals and Classic hold the 75 original recipes, which
        // PRESETS.md lists in one table.
        if (folder.rfind("Classic/", 0) == 0 || folder == "Bass" || folder == "Drums" || folder == "Vocals") continue;
        std::cout << "\n### " << folder << "\n\n| Preset | Length | Plays | Settings | What it does |\n| --- | --- | --- | --- | --- |\n";
        for (const auto& preset : bankPresets()) {
            if (preset.folder != folder) continue;
            const auto& s = preset.settings;
            std::string settings;
            const auto add = [&settings](const std::string& text) { settings += (settings.empty() ? "" : ", ") + text; };
            if (preset.classic >= 0) add("original recipe #" + std::to_string(preset.classic + 1));
            add("Keep Sub split " + std::string(splits[s.split]));
            if (s.depth != 100.0f) add("Depth " + std::to_string(static_cast<int>(s.depth)) + "%");
            if (s.mix != 100.0f) add("Mix " + std::to_string(static_cast<int>(s.mix)) + "%");
            if (s.lookback > 0) add("Look Back " + std::string(looks[s.lookback]));
            if (s.chain > 0) add("chain " + std::string(chains[s.chain]));
            if (s.engageQuantize > 0) add("Engage snaps to the " + std::string(snaps[s.engageQuantize]));
            const std::string length = preset.classic >= 0 ? "recipe length (unless locked)" : lengths[s.phraseDuration];
            std::cout << "| " << preset.name << " | " << length << " | " << plays[s.every] << " | " << settings
                      << " | " << preset.description << " |\n";
        }
    }
}
}

int main(int argc, char* argv[])
{
    if (argc == 2 && std::string(argv[1]) == "--presets-markdown") {
        printPresetMarkdown();
        return 0;
    }
    try {
        backScratch::runPatternTests();
        backScratch::runPatternEditingTests();
        backScratch::runDrawToolsTests();
        backScratch::runMotionProfileTests();
        runLiveEngineTests();
        runEngineContinuityTests();
        runFeatureEngineTests();
        std::cout << "BACK-SCRATCH live engine and pattern tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
