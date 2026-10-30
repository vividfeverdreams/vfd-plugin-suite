# SHIFTY 0.5.0 validation

Validated on Apple Silicon, macOS 14.5, on 2026-10-05. Release build, arm64, JUCE 9.0.2.

## Summary

| Check | Result |
| --- | --- |
| DSP tests (`ShiftyDspTests`, 44.1/48/96 kHz) | Pass |
| Source-formant tests (`ShiftyImpactTests`, CLASSIC engine) | 22/22 pass |
| Feature tests (`--feature-tests`), 0.4 regression set plus 0.5.0 set | Pass |
| **0.4.2 compatibility** (`--compat`, against the shipped 0.4.2 VST3) | Pass. 26 states × 3 signals, worst difference 2.46 × 10⁻⁶, same latency |
| Factory preset audit (`--preset-audit`) | Pass. 114 presets, no clipping, no near-silent change on raw saws |
| Hosted VST3: latency per engine and parameter list (`--host-latency`) | Pass |
| pluginval, strictness 5, VST3 | SUCCESS |
| Audio Unit, hosted in process from the build folder (`--au`) | Pass |
| pluginval / auval on the AU | **Not run**: needs the AU installed (see below) |
| Editor snapshots (default, Motion on, Vowel on, INFO, 21-image gallery) | Pass |

## 0.5.4 additions (2026-10-21)

Release build, arm64, JUCE 9.0.2, from `build/`.

| Check | Result |
| --- | --- |
| `ShiftyDspTests`, `ShiftyImpactTests` | Pass; 22/22 source-formant checks |
| `--feature-tests`: 0.4, 0.5.0 and 0.5.4 sets (23 sections) and the host-keys run | Pass |
| `--host-keys`: shared check (53) and SHIFTY's own (14), off-screen window | Pass |
| `--host-latency`: 29 parameters, host bypass is SHIFTY's Bypass (index 14), latency kept while bypassed | Pass |
| `--au`: bypass property and Bypass parameter are one switch, latency kept while bypassed | Pass |
| `--bypass-state` against the 0.5.3 VST3 | Pass (see below) |
| `--menu-placement`, `--gallery` (26 images, no focus ring after RANDOM), default and INFO snapshots | Pass |
| pluginval | Not run (per user) |

The host bypass is now SHIFTY's own BYPASS. 0.5.3 projects (BYPASS on or off) open with their BYPASS, and SHIFTY's state bytes are identical in 0.5.3 and 0.5.4. The VST3 wrapper no longer adds its own "Bypass" (30 parameters become 29), and the JUCE private block in the VST3 state no longer carries a separate host bypass. A 0.5.3 VST3 project saved with the DAW's own bypass on (and SHIFTY's BYPASS off) therefore opens active.

```
PASS 0.5.3 project, BYPASS off, DAW bypass on : opens with BYPASS off; SHIFTY's state byte-identical (1379 bytes); JUCE private block <JUCEPrivateData Bypass="1"/> -> empty
PASS 0.5.3 project, BYPASS on , DAW bypass off: opens with BYPASS on; SHIFTY's state byte-identical (1379 bytes); JUCE private block <JUCEPrivateData Bypass="0"/> -> empty
```

## Existing projects (0.4.2 compatibility)

The test loads SHIFTY 0.4.2 from the released installer payload and SHIFTY 0.5.0 as real VST3 plug-ins in the same host. It saves a state in 0.4.2, opens it in 0.5.0, and renders three test sounds through both: a 55 Hz saw, an FM growl and a Reese. The states are the 24 factory presets, plus Vowel Color with Sub Safe off and −3 dB Output, plus internal bypass.

- Every state opens on the **original 0.4.2 sound**, at 0.4.2's latency (2,048 samples at 48 kHz).
- It renders within 2.46 × 10⁻⁶ of 0.4.2 (worst sample, all 78 renders). The tolerance is 1 × 10⁻⁵. The remaining difference comes from the faster FFT.
- The same states loaded straight into SHIFTY's processor give the same result.

```
PASS 0.4.2 Rubber Throat                          max |difference| 7.3e-07, relative RMS 6.62e-07, latency 2048 hosted / 2048 in-process (0.4.2: 2048), ORIGINAL SOUND
PASS internal bypass                              max |difference| 2.98e-08, relative RMS 6.84e-09, latency 2048 hosted / 2048 in-process (0.4.2: 2048), ORIGINAL SOUND
0.4.2 compatibility passed: 26 states x 3 signals, worst |difference| 2.46e-06
```

The feature tests also check the following:
- Old states without an engine load on the original sound, with their values and locks intact.
- The 13 new controls take their defaults and Vowel Color stays off.
- **USE LATEST SOUND** switches to CLASSIC without changing any control or the latency, and is one UNDO step.
- The 24 originals in the Classic folder use the original sound.

## Engines and latency

Hosted VST3, latency set through saved states, as a DAW restores them:

```
Hosted VST3 latency, TALK: 48 kHz 0, 96 kHz 0 samples.
Hosted VST3 latency, MOUTH: 48 kHz 1024, 96 kHz 2048 samples.
Hosted VST3 latency, CLASSIC: 48 kHz 2048, 96 kHz 4096 samples.
Hosted VST3 latency, CLASSIC low latency: 48 kHz 1024, 96 kHz 2048 samples.
Hosted VST3 latency, CLASSIC original 0.4.2 sound: 48 kHz 2048, 96 kHz 4096 samples.
Hosted VST3 latency of a new instance (TALK) before prepare: 0.
```

The engine is not a host parameter. The feature tests check that:
- an engine change sets the latency at once and notifies the host;
- UNDO and REDO restore engine and latency;
- 50 RANDOM presses with every lock open never change the engine;
- a session restores its engine.

The parameter list is append-only: 0.4.2's 16 controls in their original order with version hint 1, then the 13 new ones with version hint 2, then the VST3 wrapper's Bypass.

Host bypass passes the input at exactly the reported latency on every engine, with no leftover audio on return:

```
Host bypass, TALK: impulse 1 at the reported 0-sample latency, stray 0, after re-enable peak 4.47512e-22.
Host bypass, MOUTH: impulse 1 at the reported 1024-sample latency, stray 0, after re-enable peak 2.00119e-32.
Host bypass, CLASSIC: impulse 1 at the reported 2048-sample latency, stray 4.5307e-14, after re-enable peak 0.
Host bypass, CLASSIC low latency: impulse 1 at the reported 1024-sample latency, stray 1.31726e-13, after re-enable peak 0.
Host bypass, original 0.4.2 sound: impulse 1 at the reported 2048-sample latency, stray 1.21407e-16, after re-enable peak 0.
```

## How much each engine changes a raw saw

The hardest case for 0.4 was a plain 55 Hz sawtooth, which has no resonances to move. "Spread" is the standard deviation of the per-harmonic level change (dB); "centroid" is the shift in spectral centre of mass, in semitones. Default FOCUS 0.55, BITE 0.35, CHARACTER 0.

| Engine | Saw, SHIFT −6 | Saw, SHIFT +6 | FM growl, SHIFT −6 | FM growl, SHIFT +6 |
| --- | --- | --- | --- | --- |
| CLASSIC | 0.10 dB | 0.12 dB | 6.91 dB | 6.11 dB |
| **TALK** | **5.44 dB** (centroid −3.5 st) | **5.42 dB** (+7.0 st) | 5.14 dB | 7.18 dB |
| MOUTH | 1.03 dB | 0.72 dB | 6.58 dB | 8.01 dB |

- CLASSIC and MOUTH follow the source, so they change a raw saw little. That is expected and documented. MOUTH presets use CHARACTER, which adds a mouth shape.
- TALK changes any source.
- DSP test (48 kHz): TALK moves the spectral centre −4.7 / +5.0 st at SHIFT −6 / +6. Its five vowels differ by 5.1 dB (OO–EE, third-octave shape) and at least 2.6 dB for the closest pair.

```
TALK 48000 Hz: latency 0, saw centroid 411.026 Hz; SHIFT -6/0/+6 centroid 399.702 / 524.623 / 699.858 Hz (-4.70827, +4.98937 st); vowel shape distance OO-EE 5.10788 dB, closest pair 2.63563 dB; extremes peak 1.07004, silence 0; 40 Hz sub -0.0847852 dB; block 37 vs 1024 with vowel modulation max 0
MOUTH 48000 Hz: latency 1024, tracked 92.0023 Hz (confidence 0.993336); pitch in 92, out 92 / 92 / 92 Hz; centroid -3.85419 / +6.93405 st at SHIFT -6/+6; noise peak 0.736293, confidence 0.342387
```

TALK also passes these checks:
- extreme settings stay bounded;
- silence stays silent;
- Sub Safe passes 40 Hz within 0.1 dB;
- per-sample vowel modulation is identical at host buffers of 37 and 1,024.

MOUTH also passes these checks:
- it keeps the played pitch at ±6 st;
- it tracks a 92 Hz bass to 0.003 Hz;
- on noise it stays bounded and reports low confidence.

## CLASSIC fixes

```
CLASSIC 41.2 Hz resonant saw at +6 st, warble (median harmonic level std): original 0.4.2 1.0331 dB (latency 2048), latest 0.291336 dB (2048), low latency 0.227928 dB (1024); low latency SHIFT +6 moves the centroid +10.2115 st
Motion at 9.4 Hz: 94.6985% of 0.5 Hz modulation depth; 0.804829% windows >3 dB below the slow-sweep minimum, deepest -11.8402 dB.
```

- The low-note warble (notes below G1) drops from 1.03 dB to 0.29 dB (0.23 dB in LOW LATENCY).
- Fast Motion keeps 95% of its depth at 9.4 Hz (0.4.2: about 49%).
- Level compensation no longer bumps on note starts or SHIFT jumps. This was checked in 0.4.3 and re-run here.

## DRIVE

```
DRIVE (ADAA): aliasing at full drive, 4.1 kHz sine -22.8752 -> -33.0957 dB, 7.3 kHz sine -16.7035 -> -36.327 dB re fundamental; 55 Hz difference to the 0.4.2 curve 0.336496%; zero drive transparent yes
```

## Movement (LFO, ENV, YOI)

```
ENV follower: -6 dBFS -> 0.985336, -48 dBFS -> 0; YOI: 4 onsets in 4 notes, sweep at 100 ms 0.500095, at 202 ms 1
```

- The YOI test plays four notes after 100 ms gaps, alternating −8 and −14 dBFS, with a 6 Hz tremolo inside each note. It catches every note start and never re-triggers on the tremolo.
- Through the whole plug-in:
  - ENV opens or closes the TALK vowel and raises SHIFT on MOUTH and CLASSIC.
  - YOI starts each note near FROM and arrives at TO after its time.
  - The original sound ignores ENV and YOI.
- The LFO is sample-identical at host buffers of 64, 257 and 2,048 on all four engines.
- Synced Motion runs at the host tempo with the transport stopped: within 4 × 10⁻⁷ st, at 140 BPM and at the 120 BPM fallback.
- 1 Bar follows a 3/4 bar.

## Presets

All 114 factory presets were rendered on three sounds: a raw 55 Hz saw, a 110 Hz FM growl and a 55 Hz Reese. Each input has an RMS of about −15 dBFS and peaks at or below 0.6.

- **Pass rule:** no output peak reaches 0 dBFS, and on the raw saw every new preset makes an audible change (spread ≥ 1 dB or centroid ≥ 1 st).
- **Classic folder:** some of the 24 originals change a raw saw very little. That is how 0.4.2 sounded, so they are kept as they were and marked rather than failed.
- **Fixes made during the audit:**
  - Four presets peaked just over 0 dBFS on the FM growl: Talking Metal, Basement Growl, Plasma Throat and Tar Pit. They now have 2–3 dB less output.
  - Big Mouth and Giant Jaw changed a raw saw too little. They now use a stronger CHARACTER mouth shape.
- **Level change** (output RMS minus input RMS) stays within −2.7 to +2.6 dB across all presets and sounds.
- Every preset recalls the same values and engine whatever was loaded before. Folder and name pairs are unique.

## User presets and window size

- **User presets.** Tests run in a temporary folder and never touch the real preset folders.
  - Presets save into a chosen subfolder, creating it if needed. Folders are listed, and presets in every folder are found.
  - A folder preset reloads with its name, folder and engine.
  - Existing files keep their bytes and modification time.
  - Folder names such as `..`, `../../outside` and `.hidden` cannot leave the preset folder or hide in it.
- **Window size.** 100% is 650 × 849.
  - A 2560 × 1440 screen offers all four sizes, 1920 × 1080 offers 90% and 100%, and 1440 × 900 offers only 90%.
  - The size is saved with the project as non-parameter state, and the host is told when it changes.
  - Old projects open at 100%.

## Audio Unit

The AU has type `aufx`, subtype `Shf1`, manufacturer `Yoib`, bundle ID `com.yoiboi.shifty` and version 0.5.0 (JUCE defaults, no custom codes).

macOS only lists Audio Units that are installed, so pluginval and `auval` cannot open the AU from the build folder (pluginval: "No types found"). Instead, `--au` registers the component's factory for that one test process and hosts it through JUCE's AU host. Nothing is installed.

```
AU found in process: VIVID FEVER DREAMS / SHIFTY 0.5.0 (AudioUnit:Effects/aufx,Shf1,Yoib)
AU latency, TALK: 0 samples at 48 kHz.
AU latency, MOUTH: 1024 samples at 48 kHz.
AU latency, CLASSIC: 2048 samples at 48 kHz.
AU latency, CLASSIC low latency: 1024 samples at 48 kHz.
AU latency, original 0.4.2 sound: 2048 samples at 48 kHz.
AU audio vs in-process SHIFTY (TALK, 1 s saw): max |difference| 0, energy 4865.58, finite.
AU editor: 650 x 849.
AU in-process validation passed.
```

**To do after installing:** `auval -v aufx Shf1 Yoib` and pluginval on the installed component.

## CPU

Thread CPU time, stereo, 48 kHz, 64-sample buffers, 10 s, per engine:

| Engine | Average | 99th-percentile callback (budget 1,333 µs) |
| --- | --- | --- |
| TALK | 1.4% | 27 µs |
| MOUTH | 1.9% | 86 µs |
| CLASSIC | 3.4% | 376 µs |
| CLASSIC LOW LATENCY | 3.6% | 216 µs |
| Original 0.4.2 sound | 2.1% | 233 µs |

CLASSIC's zero-padded analysis costs more than the original mode but spreads its work over smaller frames. These figures are informational, not a worst-case guarantee.

## Not yet done

- A real session in Ableton Live and Logic Pro: scan, save and reopen, automation, freeze and export.
- `auval` and pluginval on an installed AU.
- Listening on the user's own material.

The plug-ins are signed ad hoc and are not notarized.
