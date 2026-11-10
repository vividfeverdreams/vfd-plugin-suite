# STUTTER-STEP 0.5.0 validation — 2026-11-03 (NUDGE, DOTTED/TRIPLET, GATE/SLAP, SIMPLE + HATS, full-bleed window)

Run from `stutter-step/` (Claude). Incremental Release build in `build/`: VST3, AU, StutterStepTests, StutterStepShots, StutterStepDemos; no warnings. Both bundles report **0.5.0** and pass `codesign --verify --deep --strict`. Not installed; pluginval left for the release pass.

```sh
build/StutterStepTests_artefacts/Release/StutterStepTests
build/StutterStepShots_artefacts/Release/StutterStepShots --host-keys
build/StutterStepShots_artefacts/Release/StutterStepShots --window-edges [folder for corner crops]
build/StutterStepShots_artefacts/Release/StutterStepShots <folder>
build/StutterStepDemos_artefacts/Release/StutterStepDemos <folder>
build/StutterStepDemos_artefacts/Release/StutterStepDemos --source <wav> --groove <name> --bars 4 --out <wav> [--timing dotted|triplet --from-bar 2]
python3 ../shared/tools/check_text_literals.py
```

- **StutterStepTests: 316 checks, 0 failures** (213 before). New:
  - **0.4.3 compatibility** (`Tests/Compat043.h`, written by `--write-compat` from the unmodified 0.4.3 Source before any change): 51 host states (all 43 factory grooves at 90/140/174 BPM, an edited groove at 1/32 with free slap time, GAPS, SUB SAFE 200 and OUTPUT −3, sends on silent and tied steps at 1/4, a rolled groove, slap off, bypass, stopped transport, a schema-1 project, a USER-groove project) render **byte-identical** (SHA-256 of three bars of a reese, both channels), also when loaded over an instance already on triplet timing, the SLAP layer and 35% sends; all open straight on GATE. Three `.ssgroove` files saved by 0.4.3 load straight and render byte-identical. Every 0.4.3 groove's DRAG MIDI clip at every RATE is byte-identical. The 43 older grooves keep their index and name.
  - **Send levels:** text round trip (full sends write nothing), junk clamped, the send envelope scales by level and never the gate, a 50% send gives half the echo (0.500 ± 0.01), projects keep levels, `eventOwner` (ties, ties across the loop, all-tied).
  - **Nudge:** back-and-forth and 16 nudges are identities; every factory groove nudged ±1 at straight swing and ±2 at 62/71% swing gives the same gate and send envelopes shifted in time (300 cases, ≤ 1e-4); the processor marks the groove edited, bumps the screen revision and notifies the host; projects and DRAG MIDI keep the nudged groove; at 125 BPM a groove nudged one step later renders the original one 5,760 samples later (≤ 1e-4).
  - **Dotted/triplet:** straight is RATE bit-for-bit; TIMING is a host parameter, last, default straight; onsets within 30 samples of the timed, swung grid and gate open/closed exactly where it should be at 90/140/174 BPM, rates 1/8–1/32, two cycles (across the loop); LOCK follows the timed step; the playhead runs on timed steps; with the host looping every two bars at straight, dotted and triplet (and a tie wrapping the loop) the gain never moves faster than its 0.5 ms smoother allows (no clicks); dotted and triplet DRAG MIDI note-ons fall where the rendered gate opens (≤ 30 samples); whole-bar clip lengths for all 12 RATE × timing pairs; factory grooves set timing, NEW GROOVE keeps timing and RATE (ALL and HATS), USER grooves save timing and levels.
  - **SIMPLE and HATS:** 15 then 17 after the 43; SIMPLE straight-ish with ≥ 3 ms edges; HATS short, crisp, full depth, ≤ 1 faint send; no duplicate rhythm+timing+swing+LENGTH; 75 host programs; 2,000 HATS rolls are 8–16 steps, short, no sends; ALL never rolls HATS; the never-near-a-factory-groove check (≥ 4 steps from all 75, the groove replaced too) holds over 2,000 rolls across every family including HATS; the swing padlock holds through all 75 grooves, a HATS roll and a nudge.
  - **MIDI clip:** every groove at its RATE × straight/dotted/triplet × nudges 0/−1/+3 (675 clips): whole 4/4 bars, notes exactly where the gate is open, held notes that wrap the loop carry on inside the clip.
  - Mutation checks: turning off the across-the-loop tie in `Envelope::build` fails 2 checks (1,357 clip mismatches, 30 nudge shifts). Without the explicit TIMING reset, old states still open straight (JUCE resets parameters missing from a state), so that line is a guard.
- **`--host-keys`: 96 checks, 0 failures** (55 shared host-keys checks + 41 STUTTER-STEP). New: GATE, SLAP, both NUDGE keys, DOTTED, TRIPLET exist, show, never take the keyboard; NUDGE later/earlier by real clicks; DOTTED → TRIPLET → straight; in GATE a click cycles the step; SLAP: a click sends a step at full and leaves the gate alone, a second click stops it, a silent step can't send, a vertical drag sets a smaller send, a horizontal drag draws sends across steps; Return and space reach the host after NUDGE and after editing sends.
- **`--window-edges`: 10 checks, 0 failures** (80, 90, 100, 125, 150% at 1x and 2x: all four corner pixels and four edge midpoints are enclosure, opaque, far from the background colour). With the 0.4.3 crop all 10 fail.
- **Demos: the 43 older grooves' WAVs are byte-identical** to 0.4.3 (`build/validation/current-demos`, itself identical to 0.4.2); `00 TOUR.wav` is identical for its first 43 grooves and then plays the 32 new ones. Peaks 0.45–0.77 at the demo mix.
- `check_text_literals.py`: all non-ASCII literals decode as UTF-8.
- Text contrast of the new labels on the screen (WCAG): NUDGE caption and beat numbers 7.0:1, unlit keys 5.7:1, lit GATE 11.6:1, lit SLAP 14.4:1, timed span label 13.3:1, send tag 14.4:1.

**Not verified:** the user's ears and hands in Live (dragging sends, the nudged and timed grooves on real material, DRAG MIDI at dotted/triplet dropped into Live); pluginval and auval (left for the release pass); Windows/Linux builds (the render fingerprints are macOS arm64 only).

# STUTTER-STEP 0.4.3 validation — 2026-10-26 (DRAG MIDI)

Run from `stutter-step/` (Claude, outside the sandbox). Incremental Release build in `build/`: VST3, AU, StutterStepTests, StutterStepShots, StutterStepDemos; no warnings. Both bundles report **0.4.3** and pass `codesign --verify --deep --strict`.

```sh
build/StutterStepTests_artefacts/Release/StutterStepTests
build/StutterStepShots_artefacts/Release/StutterStepShots --host-keys
build/StutterStepShots_artefacts/Release/StutterStepShots <folder>
build/StutterStepDemos_artefacts/Release/StutterStepDemos build/validation/demos-0.4.3
pluginval --strictness-level 5 --validate-in-process --validate "$PWD/build/StutterStep_artefacts/Release/VST3/STUTTER-STEP.vst3"
```

- **StutterStepTests: 213 checks, 0 failures.** New `testMidiClip`: a hand-worked clip (swing, LENGTH, ghost, held tie, tie after silence) to the tick; 960 PPQ, C1 on channel 1, track name, tempo, 4/4; the written file reads back unchanged; 1/4 spans four bars, 1/32 repeats to one bar, empty grooves and empty last bars keep the length (end-of-track); LENGTH 100% joins notes with each off before the next on. All 43 factory grooves × 4 RATEs × 3 swings × 3 LENGTHs: notes sound exactly where the processor's `Envelope` gate is open, hits where it is fully open and ghosts at ghost level (1,600 points per clip). `writeMidiClip`: named after the groove, byte-equal to the clip, replaced after an edit with no temporary files left, rolled names, an unwritable folder returns no file.
- Mutation check: ignoring swing, or letting every tie start a new note, each fail 6 checks.
- **`--host-keys`: 25 checks, 0 failures**, shared host-keys suite included. New: the DRAG MIDI key exists and never takes the keyboard; a click starts no drag; a mouse drag sent to the editor in an on-screen window hands `startExternalDrag` the file `Glass Stairs.mid`, byte-equal to the processor's clip; the groove is untouched; Return still reaches the host; pressing DRAG cancels SAVE naming like other keys. The OS drag itself (`performExternalDragDropOfFiles`) needs a real mouse and was not exercised.
- **Demos: 44/44 WAVs byte-identical** to the 0.4.2 renders in `build/validation/current-demos`.
- **pluginval strictness 5: SUCCESS** (19 sections).
- `python3 shared/tools/check_text_literals.py` passes.

# STUTTER-STEP 0.4.2 validation — 2026-10-21

Run commands from `stutter-step/`. Only this plug-in's source and build directory were changed. No installation, DAW, audio-device playback, system scan, commit or branch change.

## Completed

```sh
cmake --build build --parallel 4
build/StutterStepTests_artefacts/Release/StutterStepTests
build/StutterStepDemos_artefacts/Release/StutterStepDemos build/validation/current-demos
```

- Incremental build succeeded in the existing Release build with its cached `external/JUCE`: VST3, AU, StutterStepTests, StutterStepShots and StutterStepDemos. Both bundles report **0.4.2** and pass `codesign --verify --deep --strict`.
- **189 checks, 0 failures** (0.4.1 baseline: 163/163). Tests and Demos now have no GUI initialiser and never create an editor, NSApplication or audio device. The test runner verifies that no MessageManager is created.
- New coverage: 12 complete stereo SUB SAFE renders (Off/80/120/200 Hz at 44.1/48/96 kHz) match 0.4.1's SHA-256 fingerprints; all 12 switch directions and rapid retargeting stay stable; concurrent groove changes, background serialization/program queries and audio rendering; exact 0.4.1 state-byte fingerprint and byte-for-byte schema-2 round trip; factory/rolled/USER program-title behavior. Existing schema-1 migration, USER files, factory-bank and random-groove tests still pass.
- **44/44 WAVs byte-identical** to unmodified 0.4.1: 43 factory grooves plus the complete tour. The baseline was captured before implementation with:

```sh
build/StutterStepDemos_artefacts/Release/StutterStepDemos build/validation/baseline-demos
```

- `Tests/SubSafeReference.h` pins pre-change sample fingerprints (macOS arm64; other libm implementations may round differently). The state fingerprint was independently captured by compiling the original processor from git HEAD into the console-only `build/validation/original/StateMain` helper.
- From the repository root, `python3 shared/tools/check_text_literals.py` passes. `git diff --check -- stutter-step` also passes.
- Logs and comparison data: `build/validation/tests.log`, `compatibility.log`, `build-verified.log`, `baseline-demos.log`, `current-demos.log`. These are local ignored build artifacts.

## Claude: pending outside the sandbox

**Do not launch these inside the sandbox.** The macOS window server is inaccessible; GUI application registration can SIGABRT and show crash dialogs. No host-key/editor/snapshot tests were launched here. pluginval not run (sandbox).

```sh
build/StutterStepShots_artefacts/Release/StutterStepShots --host-keys
build/StutterStepShots_artefacts/Release/StutterStepShots build/validation/shots-0.4.2
pluginval --strictness-level 5 --validate-in-process --validate "$PWD/build/StutterStep_artefacts/Release/VST3/STUTTER-STEP.vst3"
```

`--host-keys` runs the shared `vfd::test::runHostKeys` on the real processor/editor, using the shown groove name, a blank shell spot, SAVE and four safe controls. It then checks all four arrow modifiers, Up/Down, host Escape, Return after NEW GROOVE, SAVE draft focus loss/Return/Escape/OK/another key, factory/USER wrap and 300/150 ms TIME drags. `JUCE_MODAL_LOOPS_PERMITTED=1` is enabled for this target. The flag creates only temporary test USER files and cleans them up.

The render command writes nine PNGs, including 300 ms and 950 ms locked arcs. Review those plus the normal, 80% and 150% layouts. Actual host/UI behavior remains unverified until these commands pass outside the sandbox.

## Compatibility decisions / handoff

- SS-F2: free TIME permanently tops out at 250 ms. A longer LOCK uses a separate lime 10–950 ms arc and refuses implicit unlocking on drag/wheel/double-click. Use LOCK explicitly for free time. Values up to 250 ms unlock at the shown value. No extended parameter range or unsaved timing override was introduced.
- SS-F4: JUCE exposes indexed program names rather than a separate current title. Rolled/USER grooves keep index 0 and temporarily supply its actual queried title; selecting 0 still loads Lean Back. Program count/order and saved state stay the same. A host that caches program titles may need to requery.
- SS-F5: only metadata/state/UI/program access takes the recursive metadata lock; `processBlock` never takes it. SUB SAFE uses a 10 ms old/new-filter crossfade only on changes; faster requests queue the latest target until that fade finishes.
- No `shared/` changes are needed. The shared helper, INFO and shared test were read and reused without edits.
- Obsidian refresh/save was not performed: the vault is outside the permitted write roots and this task allows changes only in `stutter-step/`. This dated file is the local handoff; no other model's vault notes were replaced.
