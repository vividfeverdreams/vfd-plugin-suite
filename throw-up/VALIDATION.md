# THROW-UP 0.2.0 verification

Native Apple Silicon VST3 and Audio Unit for macOS 12 or later, built with JUCE 9.0.2. Locally ad hoc signed, not Apple-notarized. Nothing was installed into `~/Library` or `/Library` while testing, and the tests never touch the real user-presets folder.

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

- all 140 menu/arrow/host-program selections keep THROW, Output and Bypass, with scoped, balanced gestures;
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
PASS actual VST3: 140 menu, 140 previous, 140 next, 140 host Program selections; 2660 automated blocks; stable THROW ID 108124; maximum audio error 0
PASS no THROW/Program edits or global parameter-cache invalidation during preset selection
PASS manual THROW drag still records normal VST3 automation gestures
```

It also checks that the controller exposes exactly 34 controls plus Program, and none of JUCE's 2,080 "MIDI CC" proxy parameters, which are turned off because THROW-UP only needs MIDI notes.

`ThrowUpHostTests --au <THROW-UP.component>` (Audio Unit, registered for the test process only, from the build folder):

```
AU identity: type aumf, subtype Thr1, manufacturer Yoib
AU buses as hosted: inputs 2 (4 channels), outputs 1 (2 channels)
PASS AU bundle identifier com.yoiboi.throwup / AU version 0.2.0 / AU registered for this process only / AU scans as one plugin
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
