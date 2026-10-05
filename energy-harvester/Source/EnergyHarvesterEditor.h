#pragma once
#include "EnergyHarvesterProcessor.h"
class HarvestCanvas;
class EnergyHarvesterEditor final : public juce::AudioProcessorEditor {
public:
    explicit EnergyHarvesterEditor(EnergyHarvesterProcessor&);
    ~EnergyHarvesterEditor() override;
    void resized() override;
    void previewTime(double seconds);
    double measureAnimationPaint();
private:
    std::unique_ptr<HarvestCanvas> canvas;
};
