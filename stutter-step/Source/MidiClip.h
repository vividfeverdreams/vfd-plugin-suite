#pragma once
#include "Groove.h"
#include <juce_audio_basics/juce_audio_basics.h>

// The groove as a MIDI clip, for the editor's DRAG key: one note wherever the gate opens, timed exactly as
// Envelope::build times the gate (swing, LENGTH, ties held through, a tie after silence starts a full hit).
namespace stutter {

constexpr int clipTicksPerQuarter = 960;
constexpr int clipNoteNumber = 36;   // C1 in Live: a bass note on a synth, the first pad of a Drum Rack
constexpr int hitVelocity = 112;
constexpr int ghostVelocity = 43;    // hitVelocity * ghostLevel: the gate's -8.4 dB on a linear-velocity sampler

// stepBeats is the step size in quarter notes (RATE). A cycle shorter than a 4/4 bar (1/32) repeats to fill one,
// so the clip is always whole bars. The track carries the groove's name, 4/4 and the tempo.
juce::MidiFile makeMidiClip (const Pattern&, double swing, double length, double stepBeats, double bpm,
                             const juce::String& name);

} // namespace stutter
