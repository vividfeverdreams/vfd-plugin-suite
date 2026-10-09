# STUTTER-STEP — build plan

**Vivid Fever Dreams suite · teal colorway · 9 October 2026**

A rhythmic gate for sustained mid-basses and subs. One click rolls a groove that is swung most of the time; picked hits feed a slap delay that brings the gaps to life. The sub never enters the echoes. Interactive prototype: [STUTTER-STEP mock](https://claude.ai/artifact/9szMVXJBAQHWwz93bCRA9b) (source in `../mock/index.html`).

## Product

- **Insert effect**, stereo in/out, zero latency. VST3 + AU (`aufx`), Apple Silicon, macOS 12+, JUCE 9.0.2, the same stack as the other five plugins.
- **Gate:** a 16-step pattern of HIT, GHOST (−8.4 dB), TIE (continue the previous hit) and OFF. The pattern follows song position. RATE sets the step size, so the same 16 steps span 4 bars (1/4), 2 bars (1/8), 1 bar (1/16) or ½ bar (1/32).
- **Swing:** classic pair swing. In each pair of steps, the second starts at `2·swing` of the pair (50% straight, 66.7% triplet, 75% dotted). SWING is the hero knob.
- **NEW GROOVE:** composes a brand-new rhythm in one of seven groove families. ALL is weighted so about 95% of rolls are swung: Shuffle 24, Bounce 20, Skank 14, Drag 14, Gallop 12, Wonk 11, Straight 5. The padlock keeps the current swing; ROLL FROM narrows the family. Since 0.3 a roll is built from the family's one-beat cells set in a phrase form (A A A B, A B A C …, lightly varied) or from an accent grouping across the beats (3+3+2, 3-5-3-5 …), with an occasional fill. It is always at least four steps away from every factory groove and from the groove it replaces, its name is never a factory name, and the screen marks it **NEW · FAMILY** in lime. Rate, depth and the slap settings stay as they are.
- **Slap:** each step can send to the slap (the lime dots). The echo goes through a 24 dB/oct high-pass at SUB SAFE, so no sub ever repeats, then two short delay lines with TONE in the feedback loop.
  - **LOCK:** echo time = `2·swing·step`, so a downbeat's echo lands exactly on the swung off-step. This is the signature move: the slap plays the ghost note the gate left out.
  - **GAPS:** echoes sound only while the gate is closed (call and response).
  - **WIDE:** right echo +14 ms. The lows stay mono because the slap is high-passed.
- **Out of scope for v0.1:** pattern slots and chains, user preset files, undo, MIDI triggering, sidechain. Projects keep everything through the DAW state.

## Parameters (stable IDs)

| ID | Label | Range | Default |
|---|---|---|---|
| `swing` | SWING | 50–75 % | 60 % |
| `rate` | RATE | 1/4, 1/8, 1/16, 1/32 (projects from 0.1/0.2 are migrated: schema 1 → 2) | 1/16 |
| `length` | LENGTH | 5–100 % of the step slot | 60 % |
| `smooth` | SMOOTH | 1–40 ms, log | 6 ms |
| `depth` | DEPTH | 0–100 % (closed gain = (1−d)²) | 100 % |
| `slapOn` | SLAP | on/off | on |
| `slapLock` | LOCK | on/off | on |
| `slapTime` | TIME | 10–250 ms, log (used when LOCK is off) | 90 ms |
| `slapFeedback` | FEEDBACK | 0–75 % | 22 % |
| `slapTone` | TONE | 500 Hz–12 kHz (exp) | 45 % ≈ 2.1 kHz |
| `slapMix` | MIX | 0–100 % wet level | 40 % |
| `slapGaps` | GAPS | on/off | off |
| `slapWide` | WIDE | on/off | on |
| `subSafe` | SUB SAFE | Off, 80, 120, 200 Hz | 120 Hz |
| `output` | OUTPUT | −12 to +12 dB | 0 dB |
| `bypass` | host bypass | on/off | off |

Pattern cells, preset name, family, seed, swing lock, ROLL FROM and editor size are saved state, not automation.

## DSP

**Clock.** Each block reads BPM, PPQ and play state. Playing: step position = PPQ ÷ rate. Stopped: a free-running clock at the last tempo, so live input still gates while you tweak. Loops and seeks need no special handling, because the gate is a pure function of position.

**Envelope (shared by audio and screen).** Events are built from the cells: a HIT or GHOST opens at its swung start, TIEs extend it, and it closes after LENGTH × its slot. Each event becomes breakpoints in step units, with ramps of SMOOTH (converted to steps at the current tempo):

- The attack ramp is centred on the grid line, so the hit lands on time without lookahead.
- If the gap to the next event is shorter than one ramp, the gate holds open and moves to the next level instead of chattering.
- Ramps are raised-cosine; a 0.5 ms one-pole on the final gain catches parameter jumps.
- The screen draws the same envelope function, so what you see is what you hear.

Three envelopes come from one build: **gate**, **send** (slap-flagged events only, closed = 0) and **gaps** (1 − gate openness).

**Slap.** Mono send (L+R)/2 × send envelope → LR4 high-pass at SUB SAFE → two 1 s delay lines (Hermite read, 60 ms time slew, so large changes glide like tape) → TONE one-pole low-pass, 60 Hz DC/low guard and tanh soft clip inside the feedback loop → × MIX × gaps envelope → added to the gated dry.

**Bypass** crossfades over 5 ms. With no latency there is nothing to align.

## UI

The shell art is the generated teal sibling (`Assets/DeviceShell.png`, 1120 × 1408, made from the SHIFTY shell in edit mode). The editor crops `(50, 54, 1024, 1304)` (the enclosure's outer edges) and opens at **660 × 840**. Right-click the shell for 90/100/125/150 %. Every label, value, graph and light is drawn by JUCE over the art. Chakra Petch, as across the suite.

Measured control map (artwork pixels):

| Element | Position |
|---|---|
| Wordmark | x 240, y 98–154, Bold Italic 46 px, cream |
| Badge | 590,102 – 920,152: "SWING GATE · SLAP" |
| LED | centre 948,127: lit while the gate is open |
| Screen glass | 168,182 – 962,568 |
| SWING knob | centre 350,818; face r 112; arc r 124 |
| NEW GROOVE cap | 585,662 – 895,770 |
| LENGTH / SMOOTH / DEPTH | centres 584 / 724 / 864, y 905; face r 42 |
| Slap knobs TIME / FEEDBACK / TONE / MIX | centres 460 / 583 / 707 / 833, y 1118; face r 37 |
| Slap switch, LOCK / GAPS / WIDE keys | 290,1046 (96×40); keys at x 182 / 256 / 330, y 1104 (68×52) |
| INFO key | 485,1230 (150×56) |

**Screen:** ROLL FROM and the preset counter, the groove name and family, prev/next, the 16-step lane (swing arrows, gate shape, lime echo energy, dashed DEPTH floor, playhead), the slap-send dots, then RATE, SUB SAFE, OUTPUT, BPM and a stereo meter.

**Lane gestures:** click cycles HIT → GHOST → OFF; Shift-click toggles TIE; right-click or a dot toggles the slap send.

**INFO** reuses `shared/VfdInfo.h` with a new teal `STUTTER` theme.

## Build and repo layout

```
yoi boi claude/
  shared/            copy of the suite's shared/ (VfdInfo.h gains the STUTTER theme)
  stutter-step/      this plugin; ../shared resolves exactly as in the suite repo
    external/JUCE    pristine JUCE 9.0.2 (copied from YOI-BOI, without the BACK-SCRATCH patch)
    Source/          Groove (model, generator, envelope), SlapDelay, Processor, Editor
    Tests/           console test app
```

Identity: `COMPANY_NAME "VIVID FEVER DREAMS"`, `BUNDLE_ID com.yoiboi.stutterstep`, manufacturer `Yoib`, plugin code `Stp1`, AU `aufx/Stp1/Yoib`. Bundles are ad-hoc signed after build, like the siblings.

To join the suite later, move `stutter-step/` next to the others in the suite repo's `work/`, merge the `STUTTER` theme into `work/shared/VfdInfo.h`, and add it to the Suite installer (1.4.0) with a teal robot icon ("SS" on the chest).

## Validation

1. **Unit tests:** swing timing, envelope shape and merge rules, determinism and constraints of the generator over 5,000 seeds (density 5–12, at most 4 empty steps in a row, ≥ 80 % swung under ALL), the gain-slope click bound at SMOOTH = 1 ms, slap stability at maximum feedback, LOCK time, state round-trip, and a null test (DEPTH 0 + slap off = input).
2. **Render test:** 55 Hz sine at 140 BPM through the processor. Open steps pass at full level, closed steps are silent, and onsets land within ±½ ramp of the grid.
3. **Hosts:** pluginval (strictness 5) on the VST3, then Ableton Live by hand: play/stop/loop, tempo changes, freeze/export twice for identical renders. Never run `auval -a` or restart `AudioComponentRegistrar`: they scan every installed plug-in and set off iLok prompts.

## Milestones

1. Scaffold, parameters, state. 2. Groove model + generator + envelope, with tests. 3. Gate in the processor. 4. Slap. 5. Editor over the shell art. 6. Tests, pluginval/auval, install for an Ableton check. 7. Icon and the suite installer.

## Status (v0.3.0, 9 October 2026)

- **RATE 1/4** added (16 steps = 4 bars). The choice list is now 1/4, 1/8, 1/16, 1/32; saved projects carry GROOVE schema 2, and schema-1 projects have their RATE index moved up one on load, so they keep their rate. LOCK's echo time is capped at 0.95 s (it was 0.5 s) so 1/4 grooves still land on the swung off-step at normal tempos.
- **NEW GROOVE rewritten** (see Product). It used to fill per-family probability templates, which made rolls resemble the factory grooves, and its name lists contained factory names (Rubber Shuffle, Velvet Strut, Jelly Bounce, Pony Gallop), so a roll could look exactly like a preset. A reopened project also mapped a rolled groove with a factory name back to that preset; rolled grooves now always reopen as rolled.
- Tests: 116 checks, 0 failures, including 2,000 rolls (1,958 distinct rhythms; none within four steps of a factory groove or of the groove it replaced; no factory names), 1/4 timing, LOCK at 1/4 and the schema-1 RATE migration.
- `StutterStepShots` also renders `05-quarter-rate.png`.

## Status (v0.2.0, 9 October 2026)


- **Presets:** 43 factory grooves in eight folders (HALFTIME, RIDDIM, TRIPLET, LEFT-FIELD, DUB, SHUFFLE, SUB, STUTTER), browsable from the groove-name menu; see `PRESETS.md`. Projects find their preset by name, so the bank can grow. A fresh instance opens on Lean Back.
- **Icon:** the teal "SS" robot in `Assets/AppIcon.png`, built into both bundles; see `ARTWORK.md`.
- Built: `build/StutterStep_artefacts/Release/VST3/STUTTER-STEP.vst3` and `AU/STUTTER-STEP.component`, ad-hoc signed, installed to `~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`.
- Tests: `build/StutterStepTests_artefacts/Release/StutterStepTests`, 99 checks, 0 failures. They include every factory groove on a -6 dBFS reese: no clipping, never below 20 % of the input level.
- pluginval 1.0.4, strictness 5, in-process, on the VST3: **SUCCESS** (`../review/pluginval-vst3-0.2.0.txt`). The AU registers with macOS as `aufx Stp1 Yoib`; Apple's `auval` was not run, by choice (see Validation).
- Audition renders: `build/StutterStepDemos_artefacts/Release/StutterStepDemos <folder>` writes one WAV per groove plus `00 TOUR.wav` (`../review/preset-demos/`).
- Editor renders: `build/StutterStepShots_artefacts/Release/StutterStepShots <folder>` (`../review/*.png`).
- **Suite:** ships in Vivid Fever Dreams Suite 1.4.0 (`outputs/Vivid-Fever-Dreams-Suite-v1.4.0-Beta-Install.zip` in the suite's ChatGPT project). The suite copy at `work/stutter-step` in the suite repo is now the main source; it builds against `work/shared`, which has the STUTTER theme merged in.
- Not yet done: the Ableton pass.

Rebuild: `cmake --build build --target StutterStep_VST3 StutterStep_AU StutterStepTests -j10` (CMake 4.1 from YOI-BOI's `tools/cmake-py/cmake/data/bin/cmake`).
