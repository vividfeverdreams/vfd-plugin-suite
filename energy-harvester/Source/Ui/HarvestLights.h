#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <vector>

namespace energy::ui {

// Everything the light show reacts to. Read from the processor's atomics once per frame.
struct LightInputs {
    bool gate = false;          // HARVEST as processed by the engine
    int state = 0;              // 0 off, 1 spinning, 2 at speed, 3 releasing
    float progress = 0;         // spin-up progress 0..1
    float level = 0;            // output block peak, linear
    bool bypassed = false;
    bool reducedMotion = false;
    bool dimmed = false;        // INFO page covers the device
};

// The HARVEST reactor: a jog-wheel rotor that turns at the actual tape speed, energy running through the
// board's real copper traces, a lip/rim glow that breathes with the audio, and a shockwave when the tape
// reaches speed. Works in the editor's 560 x 720 design coordinates. Message thread only.
class HarvestLights {
public:
    HarvestLights();
    void reset() noexcept;
    void advance(double seconds, const LightInputs&) noexcept;
    // Board layer: drawn over the shell and under the panels (caller clips away the display and plates).
    void paintBoard(juce::Graphics&) const;
    // Ring layer: the 40 LED segments with the rotor, lip glow and cap rim. Always drawn; static when idle.
    void paintRing(juce::Graphics&, float progress) const;
    bool needsFrames() const noexcept;
    float charge() const noexcept { return charge_; }

    static constexpr float centreX = 280, centreY = 330, segmentRadius = 102, lipRadius = 121, capRadius = 90;
    static float tapeRatio(int state, float progress) noexcept;

private:
    struct Trace { juce::Path path; float length = 0, phase = 0; };
    float intensity() const noexcept;
    void paintTrace(juce::Graphics&, const Trace&, float lit) const;
    void paintPackets(juce::Graphics&, const Trace&, float lit) const;

    std::vector<Trace> traces_;
    juce::Image halo_;
    // Motion state.
    float charge_ = 0, fadeFrom_ = 0, fadeTarget_ = 0, speed_ = 0, level_ = 0, fill_ = 0;
    double fadeElapsed_ = 0, rotor_ = 0, flow_ = 0, shockAge_ = 10, igniteAge_ = 10;
    int lastState_ = 0;
    bool lastGate_ = false, still_ = false, dim_ = false, bypassed_ = false;
};

}
