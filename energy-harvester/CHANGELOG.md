# 0.4.4 — host keys, window size and project sound

- EH-F1: plain Left/Up and Right/Down keys use the title plate's previous/next preset path, including user presets and wrap-around. Controls cannot take keyboard focus; other keys and modified arrows reach the host. Typing a value keeps the keyboard until a click elsewhere. OUTPUT's Shift-drag fine adjustment is unchanged.
- EH-F2: dragging the window corner saves the nearest existing scale (90/100/125/150 %) with the project, restored when reopening.
- EH-F3: loading any preset preserves the project's Latest Sound / original-sound choice, even if a preset stores a conflicting choice. All other preset values, including OUTPUT, still apply; HARVEST remains untouched. Saved project formats and parameter identities are unchanged.
- EH-F4: the mode key now reads STOP>START, matching STOP > START on the LCD.

# 0.4.3 — random preset

- A die on the title plate's preset strip, between ▶ and SAVE, loads a random preset: any factory or user preset the browser lists, never the one already loaded. It loads exactly like a pick from the browser (one undoable change for the host, HARVEST untouched), and ◀ ▶ carry on from it.
- The die is drawn in the strip's own key style (pearl on the dark plate, same hover, press and focus ring). The name field is 24 px narrower to make room.
- Fix: a user preset saved under a name that was already taken (stored as, say, "Keep(2)") is now listed under that numbered name, the one the strip shows after saving, instead of a second "Keep". The arrows and the die now recognise it as the loaded preset.

# 0.4.2 — tooltips stay inside the device

- Tooltips are kept inside the visible device (the window shows only part of the design canvas) and wrap between words, in Chakra Petch. INFO shows the suite version and offers updates.

# 0.4.1 — menus open at the pointer

- The right-click menu (Reduced Motion, size) opens at the pointer instead of under the whole device.

# 0.4.0 — tape stops, rewinds, presets and the latest sound

New gestures and controls (all saved with the project and host-automatable, except Latest Sound):
- MODE: Tape Start (the original), Tape Stop (power-down to silence, held while HARVEST stays on), Brake (turntable brake), Stop > Start (stop over the first half, restart over the second) and Rewind (brake, spin backwards up to three times speed, snap back at the end). The stop, brake and rewind modes use a new varispeed deck that reads up to 16 seconds of recorded history.
- True Tape engine for Tape Start: continuous varispeed instead of the two-head pitch climb, so no grain flutter. It lands with a jump back to the live signal. Classic stays the default.
- Land On Grid (Off, 1/4, 1 bar, 2 bars): with a playing transport, the gesture ends exactly on the next beat or bar line after HARVEST turns on. It falls back to TIME or SYNC when the transport is stopped.
- Speed curve (LOG through S-CURVE to SLAM LATE; 0 is the original) and start depth (−1 to −48 semitones; 24 is the original quarter speed).
- Resonant filter sweep: 12 dB or 24 dB low-pass or band-pass, with Resonance and Start Frequency. Resonance 0 is exactly the original one-pole sweep.
- Sub Keep (Off, 60, 90, 120, 150 Hz): the band below the split stays the dry input. Exact dry outside a spin.
- Landing: Clean (the original), Tail (the Washout hall rings on over the dry signal after the landing) or Hold (stay effected until HARVEST is off). Plus a parallel Mix.
- Preset browser on the title plate: 56 factory transition presets in 10 folders, and user presets in `~/Library/Application Support/ENERGY HARVESTER/Presets/<Folder>/`. Saving never overwrites an existing file. HARVEST is never changed by a preset.
- Display pages: FX, MODE and a flip-up SETUP screen. Size menu now offers 90 %, 100 %, 125 % and 150 %, with sizes that do not fit the display disabled.
- The light show follows the engine's real playback ratio: the rotor slows to a stop for a tape stop, runs backwards for a rewind, and a stop that comes to rest powers the board down softly instead of the landing shockwave.
- Audio Unit build alongside VST3 (aufx Enh1 Yoib), validated in-process from the build folder.

Sound refinements, behind Latest Sound (on for new instances, off for projects saved by 0.3.x until you choose USE LATEST SOUND in SETUP; the host can undo it):
- Effects recede over the last 40 % of a start and Washout's hall return reaches zero exactly at the landing, instead of being cut in the last 40 ms.
- Chorus, phaser, flanger and Washout leave the sub alone (50 Hz stays within ±0.5 dB); panning only attenuates and keeps the sub centred.
- The landing is a single short-delay tap instead of a two-head comb and flam.

Fixes for every project:
- Host bypass, transport start, seek, loop wrap and project or preset recall during a spin fade over 5 ms instead of clicking.
- A recall while audio is running can no longer fabricate a launch.
- A VST3 parameter flush without transport information no longer resets a running spin or loses the host tempo.
- Non-finite input no longer poisons the running voice. Automation points at the very end of a block are applied in that block.
- The reported tail is capped at 60 seconds.

Compatibility: plug-in identities are unchanged. Parameter IDs are append-only (eleven original, twelve new), and state schema 4 reads schemas 1 to 3. A state saved by the shipped 0.3.2 plug-in renders bit for bit the same in 0.4.0.

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
