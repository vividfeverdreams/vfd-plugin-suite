#include "MidiClip.h"
#include <cmath>
#include <vector>

namespace stutter {

static_assert (ghostVelocity == (int) (hitVelocity * ghostLevel + 0.5f), "ghost velocity follows the gate's ghost level");

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

    const double cycleBeats = numSteps * stepBeats;
    const int repeats = juce::jmax (1, (int) std::ceil (4.0 / cycleBeats - 1.0e-9));
    const auto tick = [] (double beats) { return (double) juce::roundToInt (beats * clipTicksPerQuarter); };

    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0); // track name
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / juce::jlimit (20.0, 999.0, bpm))), 0);
    // A note ends at or before the next one starts, and addEvent keeps that order at equal times.
    for (int r = 0; r < repeats; ++r)
        for (const auto& n : notes) {
            const double on = tick ((r * numSteps + n.s) * stepBeats);
            const double off = juce::jmax (on + 1, tick ((r * numSteps + n.e) * stepBeats));
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
