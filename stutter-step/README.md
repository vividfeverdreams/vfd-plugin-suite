# STUTTER-STEP 0.4.3

Swing gate and short stereo slap delay. Built with JUCE 9.0.2 as VST3 and AU.

0.4.3 adds **DRAG MIDI**: the DRAG key at the right of OUTPUT drops the groove onto a MIDI track as a MIDI clip. Drag it out of the window onto a track or clip slot; a click alone shows the hint in the counter.

- One C1 note (MIDI 36, the first pad of a Drum Rack) wherever the gate opens, timed as the gate is: your SWING, LENGTH and RATE, ties held through, a tie after silence starting a full hit. Hits are velocity 112 and ghosts 43, the gate's −8.4 dB on a linear-velocity sampler. Slap sends, DEPTH and SMOOTH are not part of the clip.
- The clip is whole bars: 4 at 1/4, 2 at 1/8, 1 at 1/16, and 1/32 repeats to fill one bar. It is named after the groove and carries the host tempo and 4/4.
- The file is `<groove name>.mid` in a `STUTTER-STEP MIDI` folder in the temp directory, rewritten on every drag. Hosts copy dropped MIDI into the project.
- Code: `Source/MidiClip.*` (the clip), `StutterProcessor::writeMidiClip`, `StutterEditor::MidiKey`. Sound, parameters and saved state are unchanged.

0.4.2 implements suite 1.6.2 items SS-F1–SS-F5:

- Plain Left/Up browse the previous groove; Right/Down browse the next. The list is the 43 factory grooves, then USER in name order, wrapping and skipping unreadable files. Modified arrows and all other keys reach the host, except while typing a name or using an open menu/INFO page.
- Clicking controls leaves focus on the editor. Return cannot trigger NEW GROOVE or another key. SAVE still accepts Return, Escape and OK; drafts survive switching windows. Clicking another key cancels naming.
- Locked TIME has a lime 10–950 ms arc. Dragging unlocks at the displayed value when it fits the permanent 10–250 ms free-time parameter. Longer times stay locked when dragged, scrolled or double-clicked; use LOCK explicitly to choose free time. This avoids shortening a long echo on the first drag without changing automation ranges.
- Hosts querying the current program name see a USER name or `NEW · FAMILY / groove name`. JUCE has no independent current-program title: custom grooves retain their historical program index 0, whose queried title temporarily describes the custom groove. Selecting program 0 still loads Lean Back. The 43 program indices and their load order are unchanged. Hosts that cache titles may keep their cached name until they requery.
- State serialization safely snapshots groove metadata while audio reads only atomic pattern cells/parameters. SUB SAFE switches use a 10 ms crossfade; fixed settings retain the original sample output.

Parameter IDs, count, order and ranges, GROOVE schema 2, and `.ssgroove` schema 1 are unchanged. See [PRESETS.md](PRESETS.md) and [VALIDATION.md](VALIDATION.md).
