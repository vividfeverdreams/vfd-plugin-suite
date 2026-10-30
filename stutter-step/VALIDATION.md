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
