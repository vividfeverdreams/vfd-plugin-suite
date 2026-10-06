# ENERGY HARVESTER 0.4.0 validation

5 October 2026, Apple M1 Pro / macOS 14.5, Release arm64 VST3 and AU, built locally and ad hoc signed. Nothing was installed into the real plug-in folders or the real preset folder. Tests use a temporary preset folder, and pluginval ran with HOME pointed at a scratch folder.

## Results

- **CTest, all five targets passed:** EnergyDSP, EnergyHostState, EnergyVst3Automation, EnergyAuValidation and EnergyCompat032.
- **Compatibility with the shipped 0.3.2 plug-in.**
  - Test bundle: the suite 1.2.2 payload, ENERGY HARVESTER.vst3 0.3.2, binary SHA-256 `6178a6c4…7e607d`.
  - Method: a 0.3.2 instance is configured, saves its state, and that state is loaded into both a fresh 0.3.2 instance and 0.4.0. Both render the same three-second session with a launch.
  - Scenarios: defaults; 2.5 s with all five effects at 100 % and +6 dB; one-bar SYNC at 140 BPM with a cancel and relaunch; 80 ms with FX 0 at −12 dB; 1.2 s flanger and panning at 85 %.
  - Result: all five rendered **bit-identical** (288,000 of 288,000 samples each). The restored projects report Latest Sound off.
  - During development, the legacy path was also compared with the original 0.3.2 engine source in 756 rate, channel, effect, length, trim and retrigger scenarios. All were bit-identical.
- **DSP suite.** All original checks pass, plus the 0.3.2 regression suite and a 0.4.0 feature suite:
  - every mode finishes as specified (exact dry landing, or silence held until OFF and then exact dry);
  - True Tape has no grain flutter (envelope jitter 0.086 against 1.10 for Classic);
  - the curve and depth shape the climb;
  - Resonance 0 is bit-identical to the original sweep;
  - Sub Keep 90 Hz holds 50 Hz at −0.76 dB through a stop;
  - Tail rings and then returns exactly dry, Hold holds until OFF, and Mix 0 % is exactly dry;
  - 30 random sessions across all modes are click-free (largest step within 10 ms of an event 0.038, against 0.040 elsewhere);
  - no allocation in the audio callback;
  - an 8 s Rewind with every effect, the resonant filter, Sub Keep and Tail runs at about 1.3 % of one core.
- **VST3 automation through the shipped bundle.** All checks pass, alone and with BACK-SCRATCH, SHIFTY, THROW-UP and YOI-BOI loaded in the same process. The 23 controls keep the original eleven IDs and order.
- **Audio Unit, in-process from the build folder (nothing installed).** Covered:
  - aufx Enh1 Yoib, bundle ID com.yoiboi.energyharvester, version 0.4.0;
  - five open/close cycles;
  - mono and stereo configurations;
  - 23 parameters with valid ranges and defaults;
  - 48 kHz stereo and 44.1 kHz mono;
  - 1- to 4096-frame slices;
  - exact pass-through idle and bypassed;
  - a spin that lands exactly dry;
  - Rewind through the parameter interface;
  - ClassInfo state round trip;
  - reset, latency 0, tail 8.01 s and the Cocoa view.
- **pluginval 1.0.4, strictness 5, on the VST3:** SUCCESS. Covered editor, editor while processing, audio processing at 44.1/48/96 kHz with 64–1024 blocks, state, automation, editor automation and buses.
- **Host checks.** All pass:
  - schema 1, 2 and 3 projects keep every setting and load with Latest Sound off;
  - the upgrade is an undoable, non-automatable parameter saved as schema 4;
  - sizes 90/100/125/150 % are restored, and unknown sizes fall back to 100 %;
  - Land On Grid ends within 0.0001 ms of the next beat, bar or two-bar line, and falls back to TIME without a transport;
  - the light-show ratio falls to 0 for a stop and goes negative (−3) for a rewind;
  - all 56 factory presets apply exactly without touching HARVEST;
  - user presets never overwrite existing files and follow $HOME;
  - the earlier recall-race, transport, flush and tail fixes still hold.
- **Factory preset renders.** 56 of 56 pass on the 140 BPM test loop; per-preset numbers are in PRESETS.md.
  - Peaks: every preset peaks at the loop's own −2.5 dBFS except one at −1.4 dBFS.
  - Clean landings: 32 presets land exactly dry at the deadline.
  - Tail: 8 presets let the hall ring 2.9–6.4 s and then return exactly dry.
  - Stop and brake: 15 presets reach standstill and return exactly dry 10 ms after OFF. While held, 10 are silent (−150 dB). The 2 with Sub Keep keep the sub going (−1.3 and −3.9 dB re dry), and the 3 with Washout ring their hall (−36 to −51 dB).
  - Hold: 1 preset holds until OFF.
  - Band-pass presets read −21 to −33 dB against this loop. Most of the loop's energy is its 49 Hz sub, which a band-pass removes by design, so these presets carry +4 to +6 dB of output trim.
- **Light show.**
  - Frames rendered at 30 fps through each mode. Measured rotor motion: clockwise during a start (+9 to +18° per frame), anticlockwise during a rewind (−27° per frame), and still while a stop rests.
  - Reduced Motion and the landing shockwave are unchanged; a stop that comes to rest fades the board down instead.
  - Native renderer, 560 × 720 at 1×: full snapshot p95 5.9 ms, animation paint p95 3.2 ms. These are harness measurements, not DAW measurements.
- **Screenshots inspected.** Covered: ready, sync, output boost, spinning, at speed, the MODE page, the setup screen in latest and legacy sound, the legacy badge, tape stop, stopped, rewinding, braking, a preset on the title plate, INFO, and 90/125/150 % sizes. The setup-screen rows were re-laid out after inspection so every key has room for its label.

## Not yet done

- Sessions in real DAWs (Ableton Live for VST3, Logic Pro and Ableton for AU) and listening on real material.
- Apple's own auval against an installed copy. The AU was validated in-process instead, so nothing had to be installed.
- Retina profiling with several editors open.
- Packaging the AU in the installers (the existing installers carry only the VST3).
