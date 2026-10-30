#pragma once
#include "EnergyHarvesterProcessor.h"
#include "VfdHostKeys.h"
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
    std::unique_ptr<juce::ComponentTraverser> createKeyboardFocusTraverser() override
    { return vfd::HostKeys::createTraverser(); }
private:
    void stepPreset(int direction);
    std::unique_ptr<HarvestCanvas> canvas;
    bool restoringSize=true;
    // Last: the controls must outlive their keyboard policy.
    vfd::HostKeys hostKeys{*this,[this](int direction){stepPreset(direction);}};
};
