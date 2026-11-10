# STUTTER-STEP 0.5.0

Swing gate and short stereo slap delay. Built with JUCE 9.0.2 as VST3 and AU.

## What's new in 0.5.0

A new row under the step lane, edge-to-edge window art, and 32 new grooves.

- **GATE / SLAP** (left of the row): picks what clicks in the lane edit. **GATE** works as before: click a step for hit, ghost, off; Shift-click ties it to the step before; right-click still toggles its slap send. **SLAP** brings the slap sends to the front and dims the gate to an outline: every note gets a lime column on the step that starts it. Click a column to send that note to the slap, click again to stop; drag up or down for how much it sends (5-100%, shown above the column); drag across to draw several. Steps that don't sound can't send (the counter says so), so SLAP never changes the gate. In GATE, a lime dot under a hit shows it sends. The old SLAP dot row is gone; its job is now the SLAP layer.
  - Why it was hard before: the only send controls were the small dots under the lane (one per step, 12 px, and a click on a silent step also added a hit) and an unlabelled right-click. The lime bars you could see in the lane were the echo energy, drawn behind the gate shape, and clicking there edited the gate.
  - Send levels are new: until 0.5.0 a send was on or off at full level. Every older send stays full.
- **NUDGE ‹ ›** (centre): moves the whole groove one step earlier or later, wrapping around. Each step keeps its hit or ghost, its tie and its send. A held note that ends up across the loop's end stays held across it (it no longer restarts on the first step), so a nudged groove sounds exactly like the original moved in time. Nudging marks the groove edited (*), like any edit. STUTTER-STEP has no undo; nudge back the other way.
- **DOTTED / TRIPLET** (right): every step becomes 1.5 times (dotted) or two thirds (triplet) of the RATE. Only one can be lit; click the lit one for straight. The 16 steps then span a different length, shown under the lane ("1½ BARS", "⅔ BAR") with the beat or bar numbers moved to match. It is a host parameter, **Timing** (Straight, Dotted, Triplet), appended after Bypass, so it automates and saves like RATE. Gate, swing, LOCK's echo time and the playhead all run on the timed step. The groove's cycle follows the song position, so a dotted 1/16 groove (1½ bars) lines up again every 3 bars.
- **New grooves:** **SIMPLE** (15: quarter chug, straight 8ths, offbeats, trance gate, 3-3-2, gallops, half notes, a dotted-8th pulse, 8th and quarter triplets and more) and **HATS** (17: 8th and 16th hats with accents and ghosts, shuffled and two-step hats, offbeat opens, skips, trap 1/32 rolls, triplet rolls, dotted hats) for carving hi-hat patterns out of a held hat or noise loop. They come after the 43 older grooves; see [PRESETS.md](PRESETS.md). ROLL FROM has a **HATS** family for NEW GROOVE; ALL still never rolls hats.
- **Window:** the window now shows only the device (`Assets/DeviceShell-fullbleed.png`, corners tightened; see ARTWORK.md), so no dark corners. It opens at 660 × 839 (was 660 × 840) and the art is drawn 3% larger; every size option keeps its aspect ratio.
- **DRAG MIDI** writes what you hear at dotted and triplet timing too. The clip is whole 4/4 bars: dotted 1/16 is two cycles (3 bars), triplet 1/16 three cycles (2 bars), triplet 1/8 three cycles (4 bars), dotted 1/8 one cycle (3 bars). A held note that wraps the loop carries on inside the clip; the clip only restarts it where the clip itself loops.

**Compatibility.** Projects and `.ssgroove` files from 0.4.3 and older open straight, on the GATE layer, with full sends, and render byte-identical to 0.4.3 (51 saved states and 3 groove files, captured from the unmodified 0.4.3 build, are checked in `Tests/Compat043.h`). Factory grooves are still saved by name and the 43 older ones keep their program numbers; hosts now list 75 programs. Saved states gain the Timing parameter and two GROOVE properties (`editLayer`; `sendLevels`, only when a send is below full); `.ssgroove` files gain `timing` and `sendLevels` (schema 1 unchanged). Older STUTTER-STEP versions ignore the new fields: they open a 0.5.0 project straight with full sends.

## 0.4.3

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
