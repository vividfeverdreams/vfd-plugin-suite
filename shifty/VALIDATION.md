# SHIFTY 0.4.0 validation

Validated on Apple Silicon, macOS 14.5, on 2026-09-26.

## Source-formant behavior

All **22 source-formant checks pass**. The suite measures whether the input's own resonance envelope moves by the requested frequency ratio. It does not reward adding a stock vowel contour or require a large change on a featureless input.

Twelve cases cover three independently defined resonance pairs on 80, 110 and 165 Hz carriers, each shifted −6/+6 semitones at 48 and 96 kHz. Vowel is OFF, Bite and Drive are zero, Mix is 100%, and Sub Safe is off to isolate envelope accuracy. Separate tests cover sub protection.

| Input resonances (Hz) | Carrier (Hz) | Shift requested (st) | Fitted shift (st) | Target correlation | Measured output peaks (Hz) |
| --- | ---: | ---: | ---: | ---: | --- |
| 730 / 2070 | 80 | -6 | -5.75 | 0.992 | 535 / 1470 |
| 730 / 2070 | 80 | +6 | +6.00 | 0.992 | 1031 / 2954 |
| 970 / 2890 | 110 | -6 | -5.75 | 0.996 | 701 / 2016 |
| 970 / 2890 | 110 | +6 | +6.00 | 0.996 | 1378 / 4094 |
| 1270 / 3670 | 165 | -6 | -6.00 | 0.983 | 900 / 2586 |
| 1270 / 3670 | 165 | +6 | +6.25 | 0.987 | 1803 / 5302 |

The fitted displacement is searched in quarter-semitone increments; every tested case is within one such increment of the request. Target correlations range from 0.983 to 0.996. Harmonic pitch remains on the input grid. These are controlled test results, not a guarantee of exact tracking for every recording.

Four flat-carrier checks produce 0.23–0.25 dB of centered harmonic-gain spread, showing that the default mode does not impose the previous fixed vowel pattern. Two checks confirm that switching Vowel on adds a distinct optional color. Four checks verify neutral Shift=0/Drive=0 with Vowel both off and on.

The first run exposed a real misplaced upper resonance on the 165 Hz carrier. An adaptive peak-based initialization corrected the envelope estimate. All original thresholds were retained.

## Regression checks

- DSP checks pass at 44.1, 48 and 96 kHz: neutral and dry nulls, silence, pitch, pure and mixed sub preservation, stereo isolation, linked stereo ratios, and finite automation at extreme settings.
- All 24 factory presets explicitly recall Vowel off and produce finite audible audio. All 16 randomizable parameters respect locks; Vowel stays off through Random with default locks. Undo/Redo and state persistence pass.
- Legacy fifteen-parameter host states retain their original settings and lock bits, insert Vowel off and lock it. Loading old state into an already-colored instance does not inherit Vowel on. User presets continue to preserve local lock choices.
- Motion at 20 Hz remains sample-identical across host buffers of 64, 257 and 2048 samples. Output changes retain their smoothing ramp.
- The final VST3 passes pluginval strictness level 5, including audio, automation, state, editor and mono/stereo bus tests. Prepared latency is unchanged: 2,048 samples at 48 kHz and 4,096 at 96 kHz.
- Native OFF/ON editor snapshots confirm a legible secondary Vowel switch in the SOUND header; device corners remain transparent.
- Callback timing during the source tests stayed below 9% of one realtime thread on this machine, including the 96 kHz stereo cases. This includes measurement overhead and is informational; it is not a worst-case audio deadline guarantee.
- Installer build and extracted-archive integrity/signature checks pass. All 40 installer checks pass, including the installer self-detection regression. SHIFTY's bundle and VST3 class identities remain unchanged and separate from YOI-BOI.

## Scope and listening examples

Audio comparisons are synthetic fixtures, latency aligned and RMS matched. The short preview plays dry → −6 semitones → +6 semitones, with Vowel off throughout. Generic saw, square, Reese and FM examples are also included without a forced large-change threshold.

The update is packaged for installation; this task did not replace the installed plug-in. Earlier parameter values load, but revised processing changes their audio. Retain the earlier release when exact old audio recall is needed.

An actual Ableton session and listening review on the user's own material remain to be done. The app and plug-in are signed locally with an ad hoc identity and are not Apple notarized.
