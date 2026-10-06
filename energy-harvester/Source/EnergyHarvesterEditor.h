#pragma once
#include "EnergyHarvesterProcessor.h"
class HarvestCanvas;
class EnergyHarvesterEditor final : public juce::AudioProcessorEditor {
public:
    explicit EnergyHarvesterEditor(EnergyHarvesterProcessor&);
    ~EnergyHarvesterEditor() override;
    void resized() override;
    void previewTime(double seconds);
    // Test hook: advance the live light show by `seconds`, as the timer would, reading the processor's state.
    void stepAnimationForTesting(double seconds);
    double measureAnimationPaint();
    // Test hook: 0 FX page, 1 MODE page, 2 setup screen.
    void showPageForTesting(int page);
private:
    std::unique_ptr<HarvestCanvas> canvas;
};
