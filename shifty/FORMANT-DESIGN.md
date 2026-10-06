# SHIFTY 0.5: how the engines work

## Product focus

SHIFTY changes the resonances ("formants") of a bass while the notes stay put. Version 0.4 did this one way: it measured the incoming sound's spectral envelope and moved it. That is faithful to the source, but a raw saw or a smooth synth has almost no envelope to move. On such sources 0.4 changed very little: about 0.1–0.2 dB of tonal change on a plain 55 Hz saw at ±6 semitones.

0.5 keeps that engine (as CLASSIC, and bit-for-bit as the "original 0.4.2 sound") and adds two engines that work on any bass:

- **TALK** gives the sound a mouth.
- **MOUTH** resizes the mouth the sound already has, pitch by pitch.

## TALK: a morphing formant filter bank (no latency)

- **The vowel bank.** Five band-pass resonators (F1–F5) run on every sample. Their frequencies, bandwidths and levels come from vowel tables: OO, OH, AH, EH and EE in three sets:
  - **HUMAN**: classic adult vowel formants.
  - **GROWL**: lower, wider, with a darker top.
  - **WOW**: exaggerated, for wobbles.
- **Morphing.** VOWEL morphs continuously between neighbouring vowels. Frequencies and bandwidths are interpolated in log space and levels in dB. Switching voice sets crossfades the tables over 20 ms.
- **SHIFT** multiplies every formant frequency by 2^(st/12), keeping each formant's Q. The bank is the same vowel, scaled bigger or smaller.
- **FOCUS** sets the resonance width. Natural vowel bandwidths (40–130 Hz) are too narrow for bass: they would pick out single harmonics of a 55 Hz note and whistle. So FOCUS scales them from 7× (broad) to 1.26× (narrow), and Q is clamped to 0.7–30.
- **BITE** and **CHARACTER** narrow the resonances further. They also drive the bank's input through a soft clipper, which gives a vocal "rasp" that tracks the vowel.
- **Body.** A small amount of the unfiltered signal (the "body", −20 dB at defaults) runs under the bank so the low mids never vanish. BITE and CHARACTER reduce it. At default settings the formant peaks sit about 12–15 dB above the floor between them.
- **Level.** Each vowel is normalised analytically to the same output power for a sawtooth-like input, so morphing does not pump. A slow, stereo-linked auto-gain (250 ms power, 100 ms gain, at most +12 dB) then matches the bank to its input.
- **SUB SAFE** is a Linkwitz-Riley 4th-order split. Everything below the frequency is the untouched input. The dry path goes through the matching all-pass, so MIX never combs at the crossover.
- **Modulation.** Motion, ENV and YOI arrive as per-sample offsets, not smoothed, so fast movement is exact and independent of the host's buffer size. The knob values are smoothed over 20 ms. Smoothed values land exactly on their target, so a finished bypass or MIX ramp is bit-exact.
- **The display** draws the bank's actual response (the same formula as the filters), with F1 and F2 marked.

## MOUTH: pitch-synchronous formant shifting (21 ms)

- **Pitch tracking.** A YIN tracker runs on a decimated mono copy (about 12 kHz):
  - 24 ms window, 32–650 Hz, threshold 0.2, a new estimate every 5 ms.
  - Estimates below 50% confidence are rejected. The last good period is held for 60 ms.
  - After that, the engine falls back to a fixed grain size, so noise and chords still pass smoothly.
- **Grain rebuild (PSOLA style).** The output is rebuilt from grains placed one pitch period apart. Grain length is a whole fraction of the period, and FOCUS sets how long. Each grain is read from the input at a rate of 2^(st/12) (Catmull-Rom interpolation), which scales its spectral envelope while the grain spacing keeps the pitch.
  - Measured: the output pitch stays within 0.1 Hz of the input at ±6 st.
  - The tracked pitch of a 92 Hz test bass reads 92.00 Hz at 99% confidence.
- **BITE** makes the grains shorter than the period, adding a pitch-synchronous buzz.
- **CHARACTER** adds the chosen vowel's three lowest formants as a gentle resonance before the grains. This gives raw saws a shape that SHIFT then resizes. Without it, MOUTH on a plain saw is close to neutral, which is physically right: a saw has no mouth to resize.
- **Latency** is fixed at 1,024 samples at 48 kHz (21.3 ms; 941 at 44.1 kHz). It is reserved for the longest grain reach at +12 st. Dry, MIX and BYPASS use the same delay.

## CLASSIC: the source envelope, moved (43 ms, or 21 ms)

- **Method.** A streaming STFT with a 40 ms frame (2,048 samples at 48 kHz) and a quarter-frame hop.
  - Each frame estimates the spectral envelope with a bounded true-envelope method: a peak-based start, then three cepstral passes. This adapts [Röbel and Rodet, DAFx 2005](https://www.dafx.de/paper-archive/2005/P_030.pdf).
  - It then moves the envelope by 2^(st/12) and applies the difference as a zero-phase EQ.
  - FOCUS sets envelope detail; BITE scales the difference.
- **0.5 changes to the latest mode:**
  - **Zero-padded analysis (×2) and inter-frame envelope smoothing (45 ms).** Notes below G1 have harmonics fewer than two FFT bins apart, so the envelope estimate used to flicker frame to frame. That was the low-note warble. It is now a third as large: a 41.2 Hz resonant saw at +6 st gave 1.03 dB of harmonic level wobble on 0.4.2, 0.29 dB now.
  - **Frame-matched level compensation** (5 ms smoothing, ±12 dB). This replaces 0.4's slow 35/120 ms follower, which bumped on note starts and SHIFT jumps.
  - **Motion read per sample at each frame's centre,** with a gain-curve smoothing that only acts while the shift is moving fast. Fast Motion keeps its depth (95% at 9.4 Hz, against 49% in 0.4.2) with far fewer dropouts.
  - **ADAA DRIVE.**
  - The FFT is Apple's vDSP (via JUCE) instead of a scalar FFT.
- **LOW LATENCY** halves the frame (1,024 samples, 21 ms at 48 kHz) and zero-pads the analysis back to 0.4's frequency resolution. The envelope keeps that detail and the warble stays low (0.23 dB). Time resolution is shorter, which suits plucks.
- **The display** shows the detected envelope (dotted) and where it was moved (solid).

## The original 0.4.2 sound

Projects and presets saved before 0.5 have no engine stored, so they run the **original** mode. It uses:

- 0.4.2's level compensation and its tanh drive;
- no envelope smoothing and no padding;
- 0.4.2's Motion path: a 64-sample sample-and-hold through the SHIFT smoother, with 1 Bar as four quarter notes.

The compatibility test renders the 24 original factory states, plus two edge cases, through the actual 0.4.2 VST3 and through 0.5.0. The largest difference is 2.5 × 10⁻⁶, which comes from the faster FFT. Latency is unchanged.

The one behaviour fix that also runs here: with the transport stopped, synced Motion runs at the host tempo instead of freezing. Host bypass alignment, preset location and undo are processor fixes and apply to every engine.

**USE LATEST SOUND** switches an instance from original to CLASSIC (latest mode) as one undoable step. The latency stays the same (2,048 at 48 kHz).

## Engines, latency and safety

- **The engine is not a parameter.** It is saved state, changed only from the editor (or a preset) on the message thread. The processor calls `setLatencySamples` immediately and tells the host. Hosts cannot automate it and RANDOM never touches it, so plug-in delay compensation never changes mid-playback.
- **Switching** fades the old engine out over one block, resets the new one, and fades it in over 20 ms.
- **No allocation on the audio thread.** All five engine instances (original, CLASSIC, CLASSIC low latency, TALK, MOUTH) are prepared up front; only the selected one runs.

## Movement

- **ENV** is a peak follower with ATTACK and RELEASE. It reads 0–1 over −48 to −6 dBFS, so one setting behaves alike on quiet and loud parts. On TALK it moves the vowel (±4 vowel steps at full amount); on MOUTH and CLASSIC it moves SHIFT (±12 st).
- **YOI** watches a fast envelope (0.5/15 ms) against a slow one (30/60 ms). A jump of 6 dB above the slow envelope, and above −46 dBFS, starts a sweep. A 50 ms hold-off ignores the ripple of one attack. The sweep eases out from FROM to TO over TIME. YOI SHIFT adds a formant offset that glides back to zero over the same sweep.
- **CHARACTER** pushes BITE and DRIVE up (and FOCUS in CLASSIC) inside the DSP, and adds TALK's narrowing and MOUTH's mouth shape.
- **TALK (macro)** pushes ENV and YOI amounts up and deepens the LFO.
- **Macros never rewrite other parameters,** so automation and presets stay exact.

## DRIVE

0.5 keeps the 0.4 drive curve but band-limits its distortion with first-order antiderivative anti-aliasing. It is applied to the curve's non-linear part only, so the clean part of the signal stays sample-aligned.

At full drive, aliasing falls:

| Test tone | Before | After |
| --- | --- | --- |
| 4.1 kHz sine | −22.9 dB | −33.1 dB |
| 7.3 kHz sine | −16.7 dB | −36.3 dB |

Values are relative to the fundamental. On a 55 Hz bass the result differs from the old curve by 0.34%. Zero drive is bit-transparent.
