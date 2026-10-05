# THROW-UP 0.1.4 verification

Native Apple Silicon VST3 for macOS 12 or later, built with JUCE 9.0.2. Installer and plugin are locally ad hoc signed, not Apple-notarized.

## Change and reason

The previous release preserved dry correctly but its filtered, ducked, energy-normalised reverb return was too quiet. Earlier tests proved that tails existed without requiring useful wet level. This release adds a fixed +18 dB (7.943282347×) gain to the wet return, after filtering, saturation, width and ducking, outside the feedback loop. It applies to both mono and stereo. Dry gain, linear THROW range and 4 ms ramp remain unchanged. Preset values, names, 13 parameter IDs, order, state format, VST3 identities and preset-automation behavior are retained.

This is return calibration, not a change to the overall Output setting or a limiter. The reverb decay and grit are not driven harder internally. THROW zero and Bypass remain exact dry paths. Nonzero THROW in saved sessions now produces a stronger effect.

## Musical level regression

The new WetLevelAudit measures all 50 presets against harmonic mid-bass, detuned reese and repeated bass stabs at 48 kHz. The sources have matched active RMS of -14.8945 dBFS. Sustained notes last 1.6 seconds; the stab source uses four 160 ms hits. THROW is 100%, Output is 0 dB. The original input is subtracted to isolate wet audio. Measurements separate active notes, strongest 100 ms, first-arrival 100 ms and the first 250 ms after a note; they do not use whole-clip RMS that rewards long decays.

Across all 150 preset/source pairs, every measured wet window rises exactly 18.0000 dB relative to 0.1.3. Small-room active wet median improves from -16.51 to +1.49 dB versus dry, and Medium from -18.45 to -0.45 dB. Their strongest-window medians are now +2.75 and +3.36 dB; the quietest Small/Medium strongest window is -4.76 dB. Separate first-arrival and post-note limits ensure that short rooms cannot pass only by accumulating a long tail. Large/Huge and deliberately band-limited Creative presets use suitable separate floors.

The corrected build passes 150/150 audibility cases; the saved 0.1.3 engine passes only 11/150. Baseline and corrected CSVs, thresholds and summary are included in Checks. An independent shorter-note fixture also found an exact 18 dB increase across the bank, with Small/Medium active medians moving from -20.2/-24.5 to -2.2/-6.5 dB versus dry.

At full THROW the loudest audit fixture reaches +6.44 dBFS combined peak. Floating-point headroom is intentional: use Output trim for hot sources or intense throws. This release applies no limiter or automatic dry attenuation. The narrow, maximum-decay resonance check allows a bounded wet peak below 4.0 rather than the old 0.85 ceiling, checks decay afterward, and verifies that -18 dB Output recovers headroom without altering wet/dry balance. Measured resonance wet peak is 3.08931; combined peak with trim is 0.434255.

## Before/after audio

The included 37.25-second comparison plays dry → 0.1.3 → 0.1.4 for Orange Crate (Small), then Back Room (Medium). Separate 18.5-second examples and a timing guide are included. Sources and gain are identical, with no normalization, gain matching or limiting. Samples verify that the new wet signal equals the old wet signal times the +18 dB gain within 24-bit PCM roundoff. Dry is sample-identical. Exported peaks are -0.74 dBFS for Small and -3.73 dBFS for Medium, with no clipping.

## Compatibility checks

Pluginval passes at strictness 5, including processing, state, automation, editor automation and bus layouts across 44.1/48/96 kHz and 64–1024-sample blocks. It runs with normal macOS application access and does not install the plugin or edit a DAW project. The optimized DSP suite and AddressSanitizer/UndefinedBehaviorSanitizer suite passed at 44.1/48/96/192 kHz, with zero audio-processing allocations and no sanitizer findings. Checks retain exact dry/bypass, parallel summation, mono/stereo, wet-only pre-delay, the 4 ms THROW ramp, variable blocks, filtering, decay, all presets and invalid-input stress. Extreme automation peak is 6.31031 in the deliberate hot-input stress case.

The native host suite passes with 50 factory presets, stable controls and saved state, exact VST3/reference output (maximum difference 0), audible reverb on silent input, native/compact editor renders and INFO-page checks. Preset menu and arrow callbacks preserve THROW without unwanted gestures; manual knob edits still record normal automation.

The installer passes 43 isolated checks, including payload hashes, update/rollback and preservation of other plugins. The copied VST3 and installer have verified local signatures. Screenshots, icons, original dry/throws demo and the new level comparisons are included.

The actual installer payload also passes the raw VST3 callback regression: 200 preset selections (50 menu, 50 previous, 50 next, 50 host Program) over 950 queued THROW-automation blocks, stable parameter ID 108124, no preset-originated THROW/Program edits or global parameter-cache resets, and maximum audio error 0 against the direct processor. Manual THROW gestures still record normally.

Both registered CTest checks pass: core DSP and all 150 musical-level cases. The optional comparison renderer also builds through CMake. Reproducible commands are in README.md.

## Limits and compatibility

The audio examples use synthesized bass, not the user's stems. An actual Ableton Live session, Live automation recording, freeze/flatten and Live export have not been tested. No installation into the user's plugin folder was performed. The release contains an Apple Silicon VST3, not Intel, Windows, AU or AAX binaries.

A DAW's generic Program display can lag behind the plugin's own preset menu because the global program-change notification remains suppressed to preserve THROW automation. The internal preset display, saved selection and host-originated Program selection remain correct. Existing sessions retain parameter mappings but play stronger nonzero throws; keep an older release for exact recall of its previous sound.
