# ENERGY HARVESTER 0.2 implementation handoff

This revision supersedes the original three-parameter restrictions and fixed secondary recipe. Keep the approved plasma-pink enclosure and the revision-3 six-second absorption animation.

## User controls and audio

Keep Harvest, Spin-Up Time (80–8000 ms, default 800), and FX Amount (0–100%, default 45) at their original indices and IDs. Add five boolean effect controls in this order: fxPan, fxChorus, fxPhaser, fxFlanger, fxWashout. Only Phaser defaults ON. Add syncMode, default OFF, and syncDivision, default quarter note, with six choices: eighth, quarter, half, one bar, two bars, four bars. All are native host-automatable parameters with state recall. Attach the five buttons inside the glass display and a TIME/SYNC pill under the SPIN-UP label in the existing left label area. Both timing sliders occupy the same track; show the one belonging to the chosen mode. Preserve the two remembered values independently.

The audio chain is tape pitch/volume/brightness start → three-voice chorus → six-stage phaser → negative-feedback flanger → eight-line damped stereo Householder hall → constant-power stereo panning. Each effect has a 10 ms enable smoothing envelope. FX Amount controls all depths and wet amounts continuously, with 10 ms intensity smoothing. Lower frequencies are protected inside the phaser and hall input. Mono panning is intentionally neutral. History buffers allocate only in prepare; invalidating a voice resets validity counters instead of clearing delay vectors on the audio thread.

Harvest ON launches once. Holding ON lands at exact clean audio; OFF cancels smoothly and rearms. The user explicitly chose all selected effects to fade out at the spin-up endpoint. Washout therefore has no residual tail after the scheduled endpoint. FX=0 and all buttons OFF retain the basic tape start. The independent light animation remains active while ON, including at clean AT SPEED.

## Timing and automation

SYNC uses host quarter-note BPM. Note lengths are 0.5, 1 and 2 quarter notes; bars are numerator × 4 / denominator quarter notes. Multiply by 60000 / BPM for milliseconds. Valid host range: 10–999 BPM; invalid/missing tempo falls back to 120. Invalid signature falls back to 4/4. The launch duration captures tempo, signature, mode and division at the exact gate edge. A new timing setting changes the next launch, while effect enable/intensity changes affect the active launch. Do not quantize a launch to beat boundaries; its trigger remains exactly where the DAW sends the gate automation.

Stream all raw VST3 queue points with fixed cursors for the ten parameters and bypass. Apply timing/effect controls before bypass and the gate at the same sample. Preserve the original lifecycle handling, OFF/ON events, 5001 same-offset gate points, zero-length flushes and thread-local bridge. The automatic tail report reflects the planned/latched duration, with the original 8.01-second minimum. Sync durations can exceed eight seconds at slow tempos or long divisions; use the engine's extended safe duration range.

## Compatibility and release

Retain bundle com.yoiboi.energyharvester and Enh1/Yoib VST3 identities. Write schema 2 state. Read schema 1 by keeping its three values and adding the new defaults; reject malformed or unsupported states transactionally. The new phaser sound replaces the first-build secondary recipe.

Use the new pink EH robot icon as plugin and individual-installer icon. Add ENERGY HARVESTER 0.2.0 as the fifth payload of suite 1.1.0. Preserve all four existing sibling payload versions and bytes. Grow the suite native window to accommodate five rows and show the existing payload icon in each row. Update the single action to Install / Update All Five. Verify hashes and signatures and exercise rollback after every possible partial commit for fresh, update and mixed installs, without touching live plugin folders.

Ship Apple Silicon VST3/macOS 12+, native individual and suite installers, source, screenshots, animation preview, checks and dry/basic/individual/combined sound comparisons. The local builds are ad hoc signed. Report measured tests and remaining DAW listening limits accurately.

## 0.2.1 refinement

Draw the SPIN-UP time/division and FX Amount readouts without idle or editing borders or backgrounds. Keep the native editable labels and slider attachments. Use dark text and caret on the existing fascia, with a pink text-selection highlight. Included in suite 1.1.1.

## 0.3.0 output-level revision

Append outputDb after the original ten parameters: −18 to +18 dB, 0.1 dB steps, default 0 dB. Add a compact native ivory rotary dial in the lower-left footer beside INFO. Use the existing pearl/rose materials and Chakra Petch typography, with a recessed plum mount and borderless editable dB readout. Provide fine Shift-drag, double-click unity, keyboard focus and a descriptive tooltip. Keep the 560 × 720 layout and protect the dial from decorative illumination.

The user explicitly selected spin-up-only trim. Smooth linear gain over 10 ms and multiply the effected voice before its final clean-audio blend: output = input + alpha × (effected × outputGain − input). OFF, completed spin-up and host bypass must return exact original input. Do not alter the established tape-start volume ramp or add hidden automatic gain. At outputDb = 0, preserve previous audio exactly.

Stream the new output parameter before the gate at equal sample offsets. Keep all original parameter IDs, indices and plugin class IDs. Write schema 3; read schema 1 and 2 transactionally, defaulting missing outputDb to 0. Reject invalid or nonfinite output values. Verify manual text entry and state recall, 40 rate/channel/gain DSP cases, actual VST3 gain automation across five block sizes, no callback allocation, exact clean landing and bypass. Package individual 0.3.0 and suite 1.2.0 installers; preserve all four sibling payload bytes.

## 0.3.1 output appearance refinement

Remove the entire rectangular output mount. Draw a circular molded resin socket directly on the lower-left footer PCB, with layered pink lips, an inset plum collar and a larger pearl rotary cap. Match the lighting direction and concentric details of the existing hardware sockets; align the rotary center with INFO. Put OUTPUT and the editable value directly on the board. Keep the existing range and all audio/state/automation behavior. Package plugin 0.3.1 and suite 1.2.1.
