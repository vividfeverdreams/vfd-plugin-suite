#pragma once

#include "YoiVoice.h"
#include "Legacy/YoiVoice010.h"
#include "Legacy/YoiVoice012.h"

namespace yoiboi::dsp
{
// Engine identifiers are permanent file-format values, not release numbers.
// Earlier algorithms remain compiled into one plug-in so a saved sound keeps
// its rendering behavior without asking the producer to manage old binaries.
class CompatibleVoice final
{
    template <typename Fn> decltype(auto) withVoice (Fn&& fn)
    {
        if (engine == 1) return fn (classic010);
        if (engine == 2) return fn (classic012);
        return fn (current);
    }
    template <typename Fn> decltype(auto) withVoice (Fn&& fn) const
    {
        if (engine == 1) return fn (classic010);
        if (engine == 2) return fn (classic012);
        return fn (current);
    }
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
public:
    void prepare (double sampleRate) noexcept
    {
        classic010.prepare (sampleRate); classic012.prepare (sampleRate); current.prepare (sampleRate);
        held = false; bend = 0.0f;
    }
    void reset() noexcept
    {
        // Keep historical reset behavior, including classic010's bend handling.
        withVoice ([] (auto& voice) { voice.reset(); });
        held = false;
        if (engine != 1) bend = 0.0f;
    }
    void setControls (const VoiceControls& c, int requestedEngine) noexcept
    {
        const bool switched = requestedEngine != engine;
        if (switched)
        {
            engine = requestedEngine;
            withVoice ([] (auto& voice) { voice.reset(); });
        }
        if (engine == 1) classic010.setControls (historicalControls<legacy010::VoiceControls> (c));
        else if (engine == 2) classic012.setControls (historicalControls<legacy012::VoiceControls> (c));
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
    float renderSample() noexcept { return withVoice ([] (auto& voice) { return voice.renderSample(); }); }
    bool isAudible() const noexcept { return withVoice ([] (const auto& voice) { return voice.isAudible(); }); }
    float gesturePhase() const noexcept { return withVoice ([] (const auto& voice) { return voice.gesturePhase(); }); }
    static constexpr int latencyFor (int selectedEngine) noexcept { return selectedEngine < 3 ? 0 : YoiVoice::latencySamples(); }

private:
    legacy010::YoiVoice classic010;
    legacy012::YoiVoice classic012;
    YoiVoice current;
    int engine = 3, note = 36;
    float velocity = 0.8f, glide = 0.0f, bend = 0.0f;
    bool held = false;
};
}
