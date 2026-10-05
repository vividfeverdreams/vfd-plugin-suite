# 0.3.1 — integrated output dial

- Remove the rectangular dark output panel. Mount a larger ivory dial directly on the footer PCB inside a concentric pink resin socket matching the existing hardware.
- Align the dial with INFO, refine the inset collar and highlights, and retain a borderless editable value.
- Preserve output range, spin-up-only gain, automation and project compatibility. Included in suite 1.2.1.

# 0.3.0 — spin-up output level

- Add an ivory OUTPUT dial beside INFO, with a borderless editable dB readout. Range −18 to +18 dB, 0.1 dB steps, Shift-drag for finer control and double-click for unity.
- Apply output trim only to the harvested voice, with 10 ms smoothing and the existing clean handoff. OFF, AT SPEED and bypass remain unchanged; 0 dB preserves the previous sound.
- Append an automatable parameter without changing existing parameter or plugin identities. Schema 3 reads schema 1/2 projects with output at 0 dB.
- Add output-level DSP, real VST3 automation, state migration and native text-entry verification, plus a +6 dB combined-effects demo.
- Included in the one-button suite installer 1.2.0.

# 0.2.1 — borderless editable values

- Remove the value-box outlines and backgrounds in TIME, SYNC and FX Amount, including while editing.
- Retain click-to-type, selection, keyboard editing and native automation attachments.
- Included in the one-button suite installer 1.1.1.

# 0.2.0 — selectable effects and host tempo sync

- Five independently selectable display buttons: Panning, Chorus, Phaser, Flanger and Washout. FX Amount controls every selected effect, with smooth switching and intensity changes.
- Three-voice stereo chorus, six-stage phase sweep, negative-feedback flanger, diffuse damped stereo hall and constant-power panning. Mono panning stays neutral.
- TIME/SYNC pill inside the spin-up label area. Six host-tempo divisions from 1/8 to four bars, host time-signature support, remembered manual and sync settings, and launch-latched timing.
- New transparent plasma-pink EH robot icon in the established resin/PCB icon family.
- Original parameter IDs, VST3 identities and project recall retained. Schema-1 projects migrate to the new controls; updated secondary sound replaces the first-build recipe.
- ENERGY HARVESTER joins one-button Vivid Fever Dreams Suite 1.1.0 as the fifth payload; the other four payloads are byte-identical to suite 1.0.0. Native suite rows now show each plugin icon.
- Expanded effect, sync, migration and installer rollback tests, native UI previews and individual/combined sound comparisons.

# 0.1.0 — first build

- One rising-edge HARVEST launch; held ON lands clean, OFF cancels/rearms.
- Logarithmic 80–8000 ms duration and live smoothed secondary FX amount.
- Bounded two-head pitch ascent, brightness/volume recovery, six-stage magnetic phasing and accelerating pulse.
- Mono/stereo processing, zero fixed latency, finite tail, safe recall/transport/bypass behavior.
- Host queue streaming preserves short pulses and same-sample controls, including empty control flushes and explicit gate edges at bypass exit.
- Plasma-pink native hardware UI, six-second irregular circuit lights, progress, shared INFO, Reduced Motion and resizing.
- Native local installer, audio previews, source package and verification evidence.
