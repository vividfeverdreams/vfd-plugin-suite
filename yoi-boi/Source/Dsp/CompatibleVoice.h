#pragma once

#include <algorithm>
#include <cmath>
#include <utility>

#include "YoiVoice.h"
#include "YoiVoice4.h"
#include "Legacy/YoiVoice010.h"
#include "Legacy/YoiVoice012.h"

namespace yoiboi::dsp
{
// Engine identifiers are permanent file-format values, not release numbers.
// Earlier algorithms remain compiled into one plug-in so a saved sound keeps
// its rendering behavior without asking the producer to manage old binaries.
class CompatibleVoice final
{
    template <typename Fn> decltype(auto) withEngine (int selected, Fn&& fn)
    {
        if (selected == 1) return fn (classic010);
        if (selected == 2) return fn (classic012);
        if (selected == 4) return fn (latest);
        return fn (current);
    }
    template <typename Fn> decltype(auto) withEngine (int selected, Fn&& fn) const
    {
        if (selected == 1) return fn (classic010);
        if (selected == 2) return fn (classic012);
        if (selected == 4) return fn (latest);
        return fn (current);
    }
    template <typename Fn> decltype(auto) withVoice (Fn&& fn) { return withEngine (engine, std::forward<Fn> (fn)); }
    template <typename Fn> decltype(auto) withVoice (Fn&& fn) const { return withEngine (engine, std::forward<Fn> (fn)); }
    template <typename Controls> static Controls historicalControls (const VoiceControls& c)
    {
        Controls old;
        old.mouth = c.mouth; old.size = c.size; old.bite = c.bite; old.dirt = c.dirt;
        old.sub = c.subMode == 0 ? 0.0f : c.sub;
        old.output = c.output; old.lengthSeconds = c.lengthSeconds;
        old.bodyTime = c.bodyTime; old.curve = c.curve;
        old.fmRatio = c.fmRatio; old.fmIndex = c.fmIndex; old.resonance = c.resonance;
        old.startPose = c.startPose; old.bodyPose = c.bodyPose; old.endPose = c.endPose;
        old.loop = c.loop;
        return old;
    }
    void resetEngine (int selected) noexcept { withEngine (selected, [] (auto& voice) { voice.reset(); }); }
    void cancelFade() noexcept
    {
        if (fadeSamplesLeft > 0) resetEngine (fadingEngine);
        fadeSamplesLeft = 0;
    }
    YoiVoice4::Frame renderEngineFrame (int selected) noexcept
    {
        if (selected == 4) return latest.renderFrame();
        // Engines 1-3 are mono: the very same sample on both sides, no sub bus.
        const float sample = withEngine (selected, [] (auto& voice) { return voice.renderSample(); });
        return { sample, sample, 0.0f };
    }
public:
    using Frame = YoiVoice4::Frame;

    // Allocates engine 4's delay lines: call from prepareToPlay, never the audio thread.
    void prepare (double sampleRate)
    {
        classic010.prepare (sampleRate); classic012.prepare (sampleRate); current.prepare (sampleRate);
        latest.prepare (sampleRate);
        held = false; bend = 0.0f;
        const auto rate = std::isfinite (sampleRate) && sampleRate >= 8000.0 ? sampleRate : 48000.0;
        fadeLength = std::max (1, static_cast<int> (std::lround (rate * fadeSeconds)));
        fadeSamplesLeft = 0;
    }
    void reset() noexcept
    {
        // Keep historical reset behavior, including classic010's bend handling.
        withVoice ([] (auto& voice) { voice.reset(); });
        held = false;
        if (engine != 1) bend = 0.0f;
        cancelFade(); // A hard reset silences an outgoing engine's tail too.
    }
    // Original controls only: every engine-4 extra keeps its neutral default.
    void setControls (const VoiceControls& c, int requestedEngine) noexcept
    {
        LatestControls extended;
        static_cast<VoiceControls&> (extended) = c;
        setControls (extended, requestedEngine);
    }
    void setControls (const LatestControls& c, int requestedEngine) noexcept
    {
        requestedEngine = std::clamp (requestedEngine, 1, 4);
        const bool switched = requestedEngine != engine;
        if (switched)
        {
            // Let an audible outgoing engine fade over a few milliseconds
            // instead of cutting its output. Silent engines (for example right
            // after prepare) switch exactly as before, so recalled sessions
            // render bit-identically.
            const int outgoing = engine;
            if (fadeSamplesLeft > 0)
            {
                if (fadingEngine != requestedEngine) resetEngine (fadingEngine);
                fadeSamplesLeft = 0;
            }
            engine = requestedEngine;
            if (withEngine (outgoing, [] (const auto& voice) { return voice.isAudible(); }))
            {
                fadingEngine = outgoing;
                fadeSamplesLeft = fadeLength;
            }
            withVoice ([] (auto& voice) { voice.reset(); });
        }
        if (engine == 1) classic010.setControls (historicalControls<legacy010::VoiceControls> (c));
        else if (engine == 2) classic012.setControls (historicalControls<legacy012::VoiceControls> (c));
        else if (engine == 4) latest.setControls (c);
        else current.setControls (c);
        if (switched)
        {
            withVoice ([&] (auto& voice) { voice.setPitchBend (bend); });
            if (held) withVoice ([&] (auto& voice) { voice.noteOn (note, velocity, glide, true); });
        }
    }
    void noteOn (int pitch, float amount, float glideMs, bool retrigger = true) noexcept
    {
        note = pitch; velocity = amount; glide = glideMs; held = true;
        withVoice ([&] (auto& voice) { voice.noteOn (pitch, amount, glideMs, retrigger); });
    }
    void noteOff() noexcept { held = false; withVoice ([] (auto& voice) { voice.noteOff(); }); }
    void setPitchBend (float semitones) noexcept
    {
        bend = semitones;
        withVoice ([&] (auto& voice) { voice.setPitchBend (semitones); });
    }
    // Mono output (the left side). For engines 1-3 it is their own sample.
    float renderSample() noexcept { return renderFrame().left; }
    Frame renderFrame() noexcept
    {
        auto frame = renderEngineFrame (engine);
        if (fadeSamplesLeft <= 0) return frame; // Steady state: the engine's own output, untouched.
        const float gain = static_cast<float> (fadeSamplesLeft) / static_cast<float> (fadeLength);
        const auto outgoing = renderEngineFrame (fadingEngine);
        if (--fadeSamplesLeft == 0) resetEngine (fadingEngine);
        frame.left = std::clamp (frame.left + gain * outgoing.left, -0.98f, 0.98f);
        frame.right = std::clamp (frame.right + gain * outgoing.right, -0.98f, 0.98f);
        frame.sub = std::clamp (frame.sub + gain * outgoing.sub, -0.98f, 0.98f);
        return frame;
    }
    // Mod wheel / aftertouch position for engine 4's scrubbed talk modes.
    void setTalkScrub (float position) noexcept { latest.setTalkScrub (position); }
    bool isAudible() const noexcept
    {
        return fadeSamplesLeft > 0 || withVoice ([] (const auto& voice) { return voice.isAudible(); });
    }
    float gesturePhase() const noexcept { return withVoice ([] (const auto& voice) { return voice.gesturePhase(); }); }
    int activeEngine() const noexcept { return engine; }
    bool isFadingOutEngine() const noexcept { return fadeSamplesLeft > 0; }
    int engineFadeLength() const noexcept { return fadeLength; }
    static constexpr int latencyFor (int selectedEngine) noexcept { return selectedEngine < 3 ? 0 : YoiVoice::latencySamples(); }
    static constexpr double fadeSeconds = 0.005;

private:
    legacy010::YoiVoice classic010;
    legacy012::YoiVoice classic012;
    YoiVoice current;
    YoiVoice4 latest;
    int engine = 3, note = 36;
    float velocity = 0.8f, glide = 0.0f, bend = 0.0f;
    bool held = false;
    int fadingEngine = 3, fadeSamplesLeft = 0, fadeLength = 240;
};
}
