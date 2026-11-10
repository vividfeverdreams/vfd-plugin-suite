#include "MidiClip.h"
#include <cmath>
#include <vector>

namespace stutter {

static_assert (ghostVelocity == (int) (hitVelocity * ghostLevel + 0.5f), "ghost velocity follows the gate's ghost level");

int clipRepeats (double stepBeats)
{
    // The fewest cycles that make whole 4/4 bars: 1/32 straight twice, dotted 1/16 twice, triplet 1/16 three times.
    const double cycleBeats = numSteps * stepBeats;
    for (int repeats = 1; repeats <= 8; ++repeats) {
        const double bars = repeats * cycleBeats / 4.0;
        if (bars >= 1.0 - 1.0e-9 && std::abs (bars - std::round (bars)) < 1.0e-6) return repeats;
    }
    return juce::jmax (1, (int) std::ceil (4.0 / cycleBeats - 1.0e-9));
}

juce::MidiFile makeMidiClip (const Pattern& p, double swing, double length, double stepBeats, double bpm,
                             const juce::String& name)
{
    // The gate's events, in steps (see Envelope::build).
    struct Note { double s, e; bool ghost; };
    std::vector<Note> notes;
    int cur = -1;
    for (int i = 0; i < numSteps; ++i) {
        const auto& c = p[(size_t) i];
        const double s = stepStart (i, swing), e = s + length * slotLength (i, swing);
        if (c.v == Step::hit || c.v == Step::ghost || (c.v == Step::tie && cur < 0)) {
            notes.push_back ({ s, e, c.v == Step::ghost });
            cur = (int) notes.size() - 1;
        } else if (c.v == Step::tie) {
            notes[(size_t) cur].e = e;
        } else {
            cur = -1;
        }
    }
    // A tie on the first step holds the last step's note across the loop (Envelope::build): from one cycle into the
    // next the note carries on without a new attack. The clip starts with that held part as a note of its own and
    // ends with the last note held to the end; only where the clip itself loops does it start again.
    const bool wraps = notes.size() > 1 && p[0].v == Step::tie && cur >= 0;

    const int repeats = clipRepeats (stepBeats);
    const double cycleBeats = numSteps * stepBeats;
    const auto tick = [] (double beats) { return (double) juce::roundToInt (beats * clipTicksPerQuarter); };

    std::vector<Note> all; // in steps from the clip start
    for (int r = 0; r < repeats; ++r)
        for (size_t k = 0; k < notes.size(); ++k) {
            Note n { notes[k].s + r * numSteps, notes[k].e + r * numSteps, notes[k].ghost };
            if (wraps && k == 0 && r > 0) { all.back().e = n.e; continue; } // the held part joins the note before
            if (wraps && k + 1 == notes.size()) n.e = (r + 1) * numSteps;  // held to the cycle end, then joined
            all.push_back (n);
        }

    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0); // track name
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / juce::jlimit (20.0, 999.0, bpm))), 0);
    // A note ends at or before the next one starts, and addEvent keeps that order at equal times.
    for (const auto& n : all) {
        const double on = tick (n.s * stepBeats);
        const double off = juce::jmax (on + 1, tick (n.e * stepBeats));
        track.addEvent (juce::MidiMessage::noteOn (1, clipNoteNumber, (juce::uint8) (n.ghost ? ghostVelocity : hitVelocity)), on);
        track.addEvent (juce::MidiMessage::noteOff (1, clipNoteNumber), off);
    }
    // The end of the track marks the clip's length, so an empty last bar still counts.
    track.addEvent (juce::MidiMessage::endOfTrack(), tick (repeats * cycleBeats));
    track.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (clipTicksPerQuarter);
    file.addTrack (track);
    return file;
}

} // namespace stutter
