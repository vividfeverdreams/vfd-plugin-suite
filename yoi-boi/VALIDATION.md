# YOI-BOI 0.2.0 validation

Apple Silicon macOS Release build, JUCE 9.0.2. Checked 5 October 2026.

Nothing was installed during development or testing. Every check ran on the products in the build folder. The test program gives itself a private temporary home folder, so presets, settings and bounces never touch your real folders.

## Results

- **Integrated tests: 74 test groups, 23,090 checks, 0 failures.** They cover the processor, the editor, presets, sessions, migration, randomization, Undo/Redo and the 18 new 0.2.0 groups listed below. The run ends with a real VST3 scan, load and render: `VST3 host scan: YOI-BOI, peak 0.140201`.
- **Historical compatibility: 180 cases passed, 0 failed, worst sample error 0.** Every released YOI-BOI binary in the release archive was checked: five distinct builds (0.1.3 backup, 0.1.3 staged, 0.1.4, 0.1.5 and 0.1.6), 36 cases each. The other copies in the archive are byte-identical to these: 0.1.5 in suites 1.0.0 to 1.2.1, and 0.1.6 in suite 1.2.2 and its beta.
  - Each case loads the old and new plug-ins side by side. The old binary saves a project, then both binaries render it sample for sample. The project is also reopened from a fresh 0.2.0 save.
  - The cases cover 44.1, 48 and 96 kHz, blocks of 64, 257 and 1024 samples, four sound setups, MIDI offsets, glide, sustain, pitch bend, automation and a 20 BPM case.
  - VST3 component and controller IDs, the original parameter IDs, order and values, the saved engine and the reported latency (24 samples) all match.
- **Audio Unit, hosted in-process from the build folder (not installed):**
  - `AU component: VIVID FEVER DREAMS: YOI-BOI type=aumu subtype=YB01 manufacturer=YBoi version=512 factory=YOI_BOIAUFactory`
  - `AU in-process: YOI-BOI (in-process test) v0.2.0, 42 parameters, latency 24, peak 0.163527, state 3586 bytes`
  - Bundle ID `com.yoiboi.synth`. The code signature verifies (ad hoc).
- **pluginval 1.0.4, strictness 5, on the VST3: SUCCESS, exit 0.** 16 test groups ran with GUI tests skipped, random seed 20261005 and a 120-second timeout. pluginval's home folder was a scratch folder. Its info step prints "Reported latency: 0" because it asks before the plug-in is prepared; once prepared, YOI-BOI reports 24 samples.
- **Factory sounds: all 87 rendered and passed.** No clipping, silence, non-finite samples or DC. The new sounds sit between −25.2 and −24.8 LUFS-ish. Per-sound figures are in `FACTORY-PRESETS.md`.
- **Interface:** `--render-ui` captured every page at 100% (PLAY, SHAPE, MOD, LIBRARY, Library bounces, INFO), the original-engine MOD and SHAPE states, and the 90%, 125% and 150% sizes. Every screenshot was inspected. Truncated Library buttons and status text, a repeated column and a clipped header were fixed and re-checked.

### Why engine-3 comparisons stop at All Notes Off

The released 0.1.3–0.1.6 binaries stop a note dead on **CC123 (All Notes Off)**. 0.2.0 lets 0.1.3–0.1.6 sounds finish their release instead (see below). So for those sounds the comparison runs sample for sample up to the fixture's last CC123, at 1.79 s of a 2.0 s render, with zero difference. After that point the harness checks two things:

- the new tail never exceeds 1.25 times the peak of the 50 ms before CC123;
- the last 50 ms are no louder than that window.

All 180 cases pass this check.

## New 0.2.0 tests

| Test group | Checks |
| --- | ---: |
| Parameter IDs stay append-only and new instances use engine 4 | 26 |
| Use latest sound upgrades an original sound in place and Undo restores it | 11 |
| A 0.1.6 session keeps its engine, settings and rendering path | 23 |
| Synced talk is phase-locked to the host position (1/16 to one bar; straight, triplet, dotted) | 5 |
| Mod wheel and aftertouch scrub the vowel gesture | 3 |
| Bend range, legato-only glide, unslewed bend and the pitch envelope | 7 |
| The Sub output carries the sub only when enabled; width keeps the sub mono | 9 |
| Sub phase reset restarts the sub's cycle on every note | 2 |
| Shapers, Squash and the oversampled soft ceiling | 9 |
| Extended vowels, onset glides and consonants | 4 |
| Velocity amounts and the editable amplitude envelope | 3 |
| Bars can follow the host meter (opt-in, engine 4 only) | 4 |
| PREVIEW plays a one-bar phrase at the host tempo and stops | 3 |
| A/B compare keeps two sounds and copies between them | 6 |
| Bounce renders WAV variations off the audio thread without consuming the random sequence | 9 |
| User presets save into folders without touching existing files | 13 |
| The factory bank: folders, tags, unique names, and untouched originals | 214 |
| Device size: 90% is a saved size and the editor opens at it | 4 |

Each synced-talk check starts a note part-way through the song and requires the talk phase to match the song position within 1% of a cycle.

## What changes for existing projects

**Sound engines 1–3 are frozen.** The 0.1.0/0.1.1 and 0.1.2 engines (`Source/Dsp/Legacy/`) and the 0.1.3 engine (`Source/Dsp/YoiVoice.cpp`) are byte-identical to the audited baseline. The new controls only reach engine 4. With the Sub output off, which is the default, the main output is exactly as before. "1 Bar = meter" is off by default and affects engine 4 only.

These fixes went in after the released 0.1.6 build and ship for the first time in 0.2.0. They apply to every engine unless noted:

- **CC123 (All Notes Off)** releases 0.1.3–0.1.6 sounds with their normal tail instead of stopping them dead, so hosts that send it at transport stop no longer click. **CC120 (All Sound Off)** still silences at once. 0.1.0–0.1.2 sounds keep their original hard stop.
- **Host bypass** outputs silence and releases held notes when bypass starts, so a note-off sent while bypassed can no longer leave a stuck note. Pitch bend, sustain and All Notes/Sound Off are still followed during bypass. Process calls with zero samples or zero channels still apply their MIDI.
- **Changing engine while a note plays** fades the old engine out over 5 ms instead of cutting it. This covers Undo across engines, Use latest sound, and loading a preset or project. A silent engine switches exactly as before.
- The generation family is saved in a fixed spelling, so it comes back when you reopen a project. Projects that saved the editor's upper-case spelling are repaired.
- The Library rescans the shared preset folder when you open or refresh it, and before saving, so presets saved by other instances appear. Saving an existing name replaces that file instead of making a "(2)" copy. Deleted files leave the list.
- Loading the same preset twice, or pressing Reset Shape twice, no longer adds an empty Undo step.
- The output meter holds its peak between editor refreshes. Clicking a control no longer gives it keyboard focus, so arrow or Return keys can't edit the sound by accident. Tab still reaches every control.
- The standalone app remembers its audio and MIDI devices in `~/Library/Application Support/YOI-BOI/YOI-BOI.settings` (devices only; the sound is never saved automatically).

Also new in 0.2.0:

- The Library lists the new folders first and the twelve originals under **Classic**. The originals keep their ids (`factory:0`–`factory:11`), sound and Favorites.
- **PREVIEW** plays a one-bar phrase instead of one short note.
- The **Sub** output, **A / B** compare, bounces and preset folders do nothing until you use them.
- Engine-4 sounds report a 2.5-second tail to the host, room for their longest release. Engines 1–3 keep the 1-second tail of 0.1.6.
- Projects remember the device size. 90% is a new option, and the window opens at the largest size that fits when the saved one doesn't.

## Practical limits

- **Audio Unit:** checked only in-process from the build folder. pluginval cannot find an Audio Unit that macOS hasn't registered, and reported "Num plugins found: 0". Run `auval -v aumu YB01 YBoi` and pluginval on an installed copy before release.
- **Missing old binaries:** the release archive has no 0.1.0–0.1.2 binaries, so they could not be re-run this time. Their engines are byte-identical frozen sources. The integrated tests still import 0.1.0–0.1.2 projects and check that they keep their engines and zero latency. The 0.1.4 validation compared those engines against the real binaries with zero difference.
- **Not tested:** a full interactive DAW session (save and reopen, freeze or flatten, export, long sessions) and the native installer window. Bounces are rendered and shown as tiles by the automated tests, but dragging a tile into a DAW has not been tried by hand.
- **Saved-engine scope:** compatibility covers projects saved by the releases above. A project that was already moved to a newer engine and saved can't recover its older sound.

## Reproduce

See README.md for the build and check commands:

- `YoiBoiTests <VST3>` runs the integrated tests and the VST3 scan.
- `--check-compat <old VST3> <new VST3>` runs the historical comparison. It is implemented in `Tests/CompatibilityFixtures.h`.
- `--check-au <component>` hosts the AU in-process.
- `--preset-stats` and `--render-demos` render the factory sounds.
- `--render-ui` captures the screenshots.
