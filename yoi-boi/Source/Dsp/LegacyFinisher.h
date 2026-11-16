#pragma once
#include "Finisher.h"
#include <juce_dsp/juce_dsp.h>
namespace yoiboi::dsp
{
// Engines 1/2 stay archived. A synchronous sub-free shadow supplies the OTT
// body; the residual (including the archive's master-shaper interaction) goes
// around the detectors. Only the opt-in output uses this 2x half-band stage.
class LegacyFinisher
{
public:
    void prepare (double rate)
    {
        oversampling.initProcessing (1); input.setSize (2, 1);
        finish.prepare (rate * 2); reset();
    }
    void reset() noexcept { oversampling.reset(); finish.reset(); }
    void set (float amount, float trim) noexcept { finish.set (amount, trim); }
    float process (float body, float residual) noexcept
    {
        input.setSample (0, 0, body); input.setSample (1, 0, residual);
        juce::dsp::AudioBlock<float> block (input);
        auto high = oversampling.processSamplesUp (block);
        for (int i = 0; i < 2; ++i)
        {
            const float x = (finish.body (high.getSample (0, i)) + high.getSample (1, i)) * finish.gain();
            high.setSample (0, i, finish.dc (finish.glue (x, 0), 0));
            high.setSample (1, i, 0);
        }
        oversampling.processSamplesDown (block);
        return Finisher::guard (input.getSample (0, 0));
    }
private:
    juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    juce::AudioBuffer<float> input;
    Finisher finish;
};
}
