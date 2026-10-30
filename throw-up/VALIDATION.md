# THROW-UP 0.2.3 verification

Native Apple Silicon VST3 and Audio Unit for macOS 12 or later, built with JUCE 9.0.2. Locally ad hoc signed, not Apple-notarized. Nothing was installed into `~/Library` or `/Library` while testing, and the tests never touch the real user-presets folder.

## 0.2.3: host keys, momentary release and project saves (2026-10-21)

- **TU-F1:** the shared `vfd::HostKeys` manages focus. Plain Left/Up load the previous preset; Right/Down load the next. The keyboard and on-screen arrows share `stepPreset`, the same room-loading functions, wrap-around and host gestures. The current bank is 90 factory rooms followed by user files in menu order (sorted folders/files, then loose files). Unreadable files are skipped. A factory 0.1.5 room stays within its 50-room bank. Same-named user files remain distinct while browsing; after project recall the existing saved name locates the first matching user file. Modified arrows and all other host keys pass through. Output text entry releases on an outside click, and a pending edit is cancelled by a click elsewhere before its double-click delay expires. The preset display is restored to the loaded room if an unsolicited ComboBox ID changes it.
- **TU-F2:** closing or hiding a held THROW! ends the gesture and sets its parameter to zero. A movement/visibility watcher also covers a hidden or detached host parent. A late mouse-up cannot release the gesture twice. Existing project values are not reset when an idle editor closes.
- **TU-F3:** a metadata lock protects the preset name, A/B trees, active slot and saved size. Saves take independent copies, then use APVTS and serialize outside that lock. Parameter callbacks and host notifications also run outside it. The audio path never takes the lock. Parameter IDs/order/count, DSP, schema and serialized field names/order are unchanged.

New tests are part of the normal host run (THROW! lifetime, user-arrow order/display and concurrent saves). The required shared GUI test and extra keyboard checks run with `--host-keys`; `--state-saves` runs just the concurrent-save regression without creating an editor. The GUI target includes `../shared` and defines `JUCE_MODAL_LOOPS_PERMITTED=1`.

```sh
build/ThrowUpHostTests_artefacts/Release/ThrowUpHostTests --host-keys
build/ThrowUpHostTests_artefacts/Release/ThrowUpHostTests --state-saves
build/ThrowUpHostTests_artefacts/Release/ThrowUpHostTests "$PWD/build/validation-0.2.3/screenshots" "$PWD/build/ThrowUp_artefacts/Release/VST3/THROW-UP.vst3"
build/ThrowUpVstPresetAutomation_artefacts/Release/ThrowUpVstPresetAutomation "$PWD/build/ThrowUp_artefacts/Release/VST3/THROW-UP.vst3"
build/ThrowUpHostTests_artefacts/Release/ThrowUpHostTests --au "$PWD/build/ThrowUp_artefacts/Release/AU/THROW-UP.component"
```

The shared GUI check covers keyboard focus, host transport/shortcuts, INFO and Output entry. Extra checks cover every arrow and modifier, keyboard/on-screen state and gesture equality, popup dismissal, Output-to-knob focus release, deferred text-entry cancellation and hiding a host parent. The save stress uses two background readers (1,000 saves each) while 300 UI iterations change names, A/B, size and restored state; every XML payload is checked and a settled state must round-trip byte-for-byte.

Current run logs are in `build/validation-0.2.3/`:

- Incremental Release build: **all CMake targets passed**, including VST3, AU and every test target, using the existing build directory and cached JUCE checkout. Both built bundles report **0.2.3** and pass `codesign --verify --deep --strict`.
- CTest: **2/2 passed** (`ThrowUpDsp`, 30.34 s; `ThrowUpWetLevels`, 71.77 s). The audit passes **150/150 legacy + 270/270 latest = 420/420** preset/source pairs. DSP verifies 140 presets and zero real-time allocations.
- Before the GUI restriction: packaged VST3 preset automation **passed 140 menu + 140 previous + 140 next + 140 random + 140 host Program selections**, and **2,800 automated blocks**; maximum audio error **0**. Installer self-test **75/75 passed**, with isolated temporary writes.
- `python3 shared/tools/check_text_literals.py` from the repository root: **passed**. `git diff --check -- throw-up`: **passed**.
- **GUI verification pending with Claude outside the sandbox:** normal `ThrowUpHostTests` (including the new lifetime/order/save regressions), `--host-keys`, `--state-saves`, in-process `--au`, and the released-0.1.5 compatibility run. The VST3 automation command above also initializes JUCE GUI and can be repeated outside the sandbox. No 0.2.3 host-keys or concurrent-save runtime pass is claimed here.
- **pluginval not run (sandbox).** Earlier launch attempts aborted in HIServices `_RegisterApplication` before validation; after the coordinator's instruction, no further GUI-initializing programs were launched. Claude must run strictness 5 outside the sandbox:

```sh
THROW_UP_PRESET_DIR="$PWD/build/validation-0.2.3/pluginval-presets" pluginval --strictness-level 5 --validate "$PWD/build/ThrowUp_artefacts/Release/VST3/THROW-UP.vst3"
```

The 0.1.5 compatibility command remains documented below; it needs the released 0.1.5 payload path. No plug-in was installed, no DAW or audio playback was started, and no system-wide scan was run. The latest folder-only scope also prevented vault sync or memory writes; this dated validation section is the local handoff.

The results below predate 0.2.3 and are retained as historical compatibility evidence.

## 0.2.2: the dice key (random preset)

The dice key, left of the preset arrows, loads a random preset through the same function as the preset menu (`selectFactoryPreset` / `selectUserPreset`), so a random room is scoped exactly like a chosen one. It draws from the bank the arrows browse: the 90 factory rooms plus every user preset the menu lists, or the 50 0.1.5 rooms while one of those is loaded. It never picks the preset already loaded (a user preset is matched by the name it loads under) and skips a user file that can't be read.

- **In-process (`ThrowUpHostTests`):** 400 random picks from the current bank and 400 from the 0.1.5 bank, each checked like a menu choice: THROW, Output and Bypass untouched, room edits only with balanced gestures, no program or parameter-info refresh, one metadata save, the room's values loaded, the display showing it, never the room already loaded, never the other bank. With user presets (two files that both load as "Wide Thing", a loose "Loose Room" and an unreadable file), 1,500 picks reach every listed user preset and factory rooms, never repeat the loaded preset, never pick a 0.1.5 room, and the display names each pick (including one user preset after another). From a 0.1.5 room, 200 picks stay in the 0.1.5 bank.
- **Packaged VST3 (`ThrowUpVstPresetAutomation`):** one random pick after each of the 140 menu selections, with the same host-gesture checks and audio compared against the reference processor.
- The display now refreshes when one user preset replaces another (dice, or A/B between two user presets); it used to keep the previous user preset's name.

## Old projects play identically

`ThrowUpHostTests --compat <0.1.5 payload VST3> <new VST3>` loads the released THROW-UP 0.1.5 (from the Vivid Fever Dreams Suite 1.2.2 payload, read-only copy) and this build side by side as VST3 plug-ins. For each case it sets a room and settings in 0.1.5, saves 0.1.5's own project state, renders audio, loads that state into this build, and renders the same audio.

There are 600 cases: all 50 rooms × three THROW/Output settings (100%/0 dB, 37.5%/+3 dB, 0%/−4 dB) × 44.1, 48, 96 and 192 kHz, using variable block sizes from 1 to 1,024 samples and mono for one room in seven.

Result: **600/600 renders bit-identical; maximum difference 0.** The VST3 identity is unchanged.

The same promise is checked inside the host suite:

- A 0.1.5 state restores all 13 settings, keeps the 0.1.5 sound, and recalls its room from the 0.1.5 bank by its old number.
- Every new control starts neutral.
- A 0.1.0–0.1.3 state (no schema) also keeps the 0.1.5 sound.
- New instances use the latest sound.
- **Use latest sound** is a single undoable host edit and can be switched back.

The VST3 test confirms that THROW keeps parameter ID 108124 and every 0.1.5 parameter keeps its ID.

**Why it can be identical.** Every change made since 0.1.5 leaves a steady sound bit-exact, including the earlier fixes for size crackle, pre-delay pitch bends, bypass clicks and block-stepped THROW automation. Each new feature is off by default and adds nothing to the signal path until it is used. A tiny parameter-snapping residue (a restored "0 dB" Output or Return can be 3.6e-7 dB) is treated as exact unity, so a restored 0 dB really is 0 dB.

## Results (verbatim)

`ctest --test-dir build --output-on-failure`

```
1/2 Test #1: ThrowUpDsp .......................   Passed
2/2 Test #2: ThrowUpWetLevels .................   Passed
100% tests passed, 0 tests failed out of 2
```

`ThrowUpDspTests` runs the core suites with both sound versions:

```
PASS 44100/48000/96000/192000 Hz, 0.1.5 sound: exact dry/bypass, parallel return, wet-only path, 4 ms automation, mono/stereo, output trim, timing, blocks, filtering, decay
0.1.5 sound: Long-decay wet peak 3.08931; output-trimmed parallel peak 0.434255
PASS 44100/48000/96000/192000 Hz, latest sound: (same checks)
latest sound: Long-decay wet peak 3.77331; output-trimmed parallel peak 0.525956
PASS latest Low cut 180 Hz: -3.00358 dB at the label
PASS latest Low cut 520 Hz: -3.01035 dB at the label
PASS latest Grit: quiet tail -0.00671921 dB, loud wet peak 4.74121 -> -2.88831 dBFS at Grit 100
PASS latest Duck: 50% -15 dB, 100% -30 dB; sidechain key -2.31059 -> -34.4109 dBFS
PASS freeze holds: -12.1845 dBFS at 1 s, -12.1621 dBFS at 23 s
PASS shimmer: bounded (peak 2.07112) and pitched (octave/fundamental -40.6105 -> -6.38102 dB)
PASS THROW! key: 0 -> 100% in 5 ms, half-way 250 ms into a 500 ms release
PASS MIDI trigger events: velocity 80% reached, release starts at its offset
PASS auto-catch: phrase ends at 0.5 s, throw opens at 0.602667 s
PASS Safe peak 1, Send mode silent at THROW 0, Return -6 dB -> -6 dB
PASS swell: -78.5156 / -37.3789 / -12.0607 dBFS, then -98.2314 after the cut
PASS synced pre-delay: 2000 ms delays the first wet sample to 2.03906 s
PASS Size change mid-tail (Back Room -> Event Horizon): >6 kHz share -58.8352 dB before, -57.4486 dB after
PASS pre-delay 24 -> 105 ms: 1 kHz energy share 0.999995 / 0.999999 (also 0 -> 250 and 7 -> 34 ms)
PASS bypass during tail: largest step 0.137508 vs natural 0.245053
PASS bypass/un-bypass with -12 dB output: largest step 0.00628316 vs natural 0.00628319
PASS block-rate THROW automation: largest deviation from host curve 0.000191675% THROW
140 presets verified; extreme-automation peak 4.47637; realtime allocations 0
All THROW-UP DSP tests passed.
```

The DSP suite also passes under AddressSanitizer and UndefinedBehaviorSanitizer.

- **Extreme automation** (peak 4.48): Size, pre-delay (up to 2.5 s), filters, THROW, Freeze, the THROW! key and the sound version jump every 31 blocks with shimmer, swell and Auto-catch on.
- **Shimmer stress** (separate run): decay 18 s, sizes 0/40/100, every pitch, sines from 45 Hz to 1 kHz, 50% and 100% shimmer. Every tail decays (worst case −82 dB over 27 s) and stays finite.

`ThrowUpWetLevelAudit build/wet-level-audit.csv factory --verify`

```
Audited 140 presets x 3 mid-bass sources at 100% THROW; mode=factory, dry active RMS=-14.8945 dBFS
0.1.5 bank (Legacy 0.1.5 folder): Maximum wet peak 6.1806 dBFS; maximum parallel-output peak 6.4423 dBFS (floating-point headroom).
Latest bank: Maximum wet peak 8.15442 dBFS; maximum parallel-output peak 8.15442 dBFS.
Latest bank on hot riddim sources (peak -0.5 dBFS): combined peak 17.0859 dBFS (Glass Pebble); with Safe clip 0 dBFS.
PASS: 150/150 0.1.5 preset/source pairs meet active, strongest 100 ms, first-arrival, and post-note audibility floors.
PASS: 270/270 latest preset/source pairs meet their folder's audibility floors.
```

The 0.1.5 bank numbers are identical to 0.1.5's own audit, and its 150 CSV rows are byte-identical (apart from the new bank column). Per-folder levels are in PRESETS.md.

`ThrowUpHostTests <screenshots folder> <THROW-UP.vst3>`: every check passes, ending `THROW-UP host checks passed`. It covers:

- all 140 menu/arrow/host-program selections, and 800 random-preset picks, keep THROW, Output and Bypass, with scoped, balanced gestures;
- the 0.1.5-state cases above;
- user presets in a scratch folder: saved in the chosen folder; saving a taken name makes "Name 2" and the original file is byte- and date-unchanged; loading restores the room and keeps THROW; the project remembers the name; the menu lists them;
- A/B (B starts as A, both recall, both are saved with the project);
- the size menu rule (a size fits if its width ≤ the screen width and its height + 60 ≤ the screen height), the chosen size applied and saved;
- MIDI note-on throw, note-off release, and note-picks-room;
- the sidechain bus (off by default; mono/stereo accepted, surround rejected; External ducking works);
- no overlapping controls on any display page;
- all the 0.1.5-era checks;
- the built VST3 hosted in-process: 34 controls, state round trip, zero latency, maximum difference 0 against the tested processor.

`ThrowUpVstPresetAutomation <THROW-UP.vst3>`

```
Packaged/reference maximum audio error 0
PASS actual VST3: 140 menu, 140 previous, 140 next, 140 random, 140 host Program selections; 2800 automated blocks; stable THROW ID 108124; maximum audio error 0
PASS no THROW/Program edits or global parameter-cache invalidation during preset selection
PASS manual THROW drag still records normal VST3 automation gestures
```

It also checks that the controller exposes exactly 34 controls plus Program, and none of JUCE's 2,080 "MIDI CC" proxy parameters, which are turned off because THROW-UP only needs MIDI notes.

`ThrowUpHostTests --au <THROW-UP.component>` (Audio Unit, registered for the test process only, from the build folder):

```
AU identity: type aumf, subtype Thr1, manufacturer Yoib
AU buses as hosted: inputs 2 (4 channels), outputs 1 (2 channels)
PASS AU bundle identifier com.yoiboi.throwup / AU version matches this build (0.2.2) / AU registered for this process only / AU scans as one plugin
PASS (x9) AU instantiates, exposes every program and parameter, reports zero latency
PASS (x9) AU audio is finite, matches the tested processor and leaves a tail
PASS (x9) AU state recalls every parameter into a new instance
AU validated in-process at 44.1/48/96 kHz with 64/512/1024-sample blocks
```

After a state recall the AU host reports "no factory preset" rather than the saved program number. THROW-UP's own display still shows the saved room.

`pluginval --strictness-level 5` on the built VST3, with `HOME` pointed at a scratch folder: **SUCCESS**. All sections ran: scan, open (cold/warm), info, programs, editor, editor while processing, audio processing, state, automation, editor automation, automatable parameters, auval, VST3 validator, and the bus tests.

## Screenshots

The host suite renders: every display page (ROOM, THROW, SPACE, MIX), the 0.1.5-sound badge, the keys lit (THROW! held with its cream arc, FREEZE, SAFE, SEND), 90%, 100%, 125% and 150% sizes, the compact host size, the INFO page, the meters, and the Save dialog. These were inspected for overlaps, clipping and small text.

## Earlier fixes included in 0.2.0

These changes since 0.1.5 leave a steady sound identical:

- **Size and preset changes:** no more crackle (delay lengths are smooth per sample) or pitch dive. Small moves glide gently and big jumps crossfade over 40 ms.
- **Pre-delay changes:** crossfade instead of bending the pitch.
- **Bypass:** fades over 15 ms both ways, and the reverb stops taking input while bypassed.
- **THROW automation:** follows the host curve inside each audio block (never faster than 4 ms full range).
- **Meters:** hold every block's peak.
- **Preset menu:** choosing the current room again reverts your edits.
- **Output:** double-click resets it to 0 dB.
- **Projects from newer versions:** these load safely.

## Limits

- **Real DAW sessions:** Ableton Live, Logic and other hosts were not driven in this check. The VST3 and AU were hosted in-process by JUCE and by pluginval.
- **Audio Unit type:** the AU is a MIDI-controlled effect (`aumf`) so that Logic can send it MIDI for throws.
- **Freeze:** a frozen tail holds exactly. Shimmer pauses while frozen.
- **Shimmer:** keeps the loop's decay time, but octave-up content is darkened by High cut on each pass.
- **Loud sources at full THROW:** these still produce peaks well above 0 dBFS, as 0.1.5 did. Use Return, Output or SAFE.
- **0.1.5 rooms:** keep 0.1.5's low cut, which is about 2.5 times above its label.
