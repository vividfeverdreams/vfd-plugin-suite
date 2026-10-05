# YOI-BOI 0.1.4 validation

Apple Silicon macOS Release build, JUCE 9.0.2. Checked 22 September 2026. The installed user copy was not replaced during development or testing.

## Final update checks

- **144 historical VST3 compatibility cases passed, with exactly zero audio sample differences.** Actual released binaries from 0.1.0, 0.1.1, 0.1.2 and 0.1.3 generated the reference state and audio. Each case checks the initial import and a second instance reopened from a new 0.1.4 save. Old and new modules are loaded together and destroyed normally.
- The matrix uses four sound configurations, 44.1/48/96 kHz, and 64/257/1024-sample blocks for each historical release. It includes MIDI offsets, velocity, overlapping notes/glide, sustain, pitch bend, panic, parameter automation, timing and sub controls where present, and a 20 BPM duration-limit case. VST3 component/controller identities, original parameter IDs/order/steps/values and saved engine selection are checked.
- Historical latency remains **zero samples for 0.1.0–0.1.2 sounds** and **24 samples for 0.1.3/current sounds**. Switching engines updates the host's latency report.
- **43 integrated suites: 22,492 assertions passed; zero failures.** Includes migration, preset save/load, session save/reopen, engine upgrade and Undo/Redo, Mutate retaining the original engine, New Yoi using the current engine, invalid future-state rejection, manual-save-only presets, randomization exclusions and the existing processor/editor regressions.
- **Real VST3 scan/load/render passed.** The final bundle produces finite nonzero audio in the JUCE host.
- **Tracktion pluginval 1.0.4 strictness 10: SUCCESS, exit 0.** GUI tests skipped; random seed 59404942; 60-second per-test timeout. Includes processing, state, automation, repeated loads and parameter fuzzing.
- **Installer: 28 isolated transaction checks passed.** First install, in-place update/reinstall, no leftover old bundle, preset preservation, downgrade refusal, early/late running-host detection, duplicate and unknown-bundle refusal, payload integrity, symlink refusal, successful rollback after a failed update, failed first-install cleanup and staging cleanup. All writes for these tests were confined to a temporary test directory.
- The final installer verifies every embedded file against its manifest and checks bundle ID, VST3 IDs, version and code signature. Both the installer and final instrument bundles pass strict code-signature verification. These are ad-hoc development signatures, not notarization.
- VST3 export inspection confirms exactly three public entrypoints: GetPluginFactory, bundleEntry and bundleExit. Hidden Objective-C++ visibility plus a restricted export list fixes a crash found while loading an old and new JUCE-based module together during development.

Final VST3 executable SHA256:

`a2bfaa31d514c7aeecdb54dcb403bf4b17ebc7e73d7533682fb5a8c711e1cb6c`

The installer embeds this same executable. Release packaging verifies its signature, hashes and payload again after ZIP extraction. Final logs accompany the release in `validation/`; `compatibility-provenance.txt` identifies the exact historical binaries and matrix.

## Preserved audio engines

Frozen 0.1.0/0.1.1 and 0.1.2 DSP sources are retained inside the updated plug-in. Separate source-archive verification compares the original and preserved engines across 252 rendering scenarios, with identical audio and gesture hashes. Only namespaces and included header basenames differ from the original source. See `Source/Dsp/Legacy/README.md` and its verification script.

The current 0.1.3 synthesis implementation is unchanged. Its previously completed oversampling, Direct sub, low-cut and stress checks are documented in the 0.1.3 release; this release's hosted comparisons independently establish unchanged current-engine audio for the tested configurations.

## 0.1.6 processor and editor fixes

No engine's synthesis changed. The following behaviour changed for every sound, including recalled sessions:

- **CC123 (All Notes Off)** now releases current-engine (0.1.3+) notes with their normal release tail instead of hard-resetting the voice, so hosts that send it on transport stop no longer click. **CC120 (All Sound Off)** still silences immediately. Sounds that use the preserved 0.1.0/0.1.1 and 0.1.2 engines keep their original CC123 hard cut.
- **Host bypass** renders silence and releases held notes when bypass starts, so a note-off that arrives while bypassed can no longer leave a stuck note. Pitch bend, sustain pedal and All Notes/Sound Off are still followed during bypass. Zero-sample and zero-channel process calls also apply their MIDI.
- **Engine switches while sounding** (Undo across engines, Use latest sound, preset or session load) fade the outgoing engine over 5 ms instead of cutting it. A silent engine (for example right after a session loads) switches exactly as before, and once the fade ends only the selected engine is heard, unchanged.
- The generation family is stored canonically, so it is recalled after reopening a session. Sessions that stored the editor's upper-case spelling are repaired.
- The Library rescans the shared preset folder when opened and refreshed, and before saving. Presets saved by other instances appear. Saving an existing name replaces that file instead of creating a "(2)" copy. Deleted files leave the list.
- Loading the same preset twice or repeating Reset Shape no longer adds an Undo step that changes nothing. The unused APVTS UndoManager is gone; Undo/Redo still uses the sound history.
- The output meter holds the peak between editor refreshes. Clicking a control no longer gives it keyboard focus, so later arrow or Return keys cannot silently edit the sound; Tab still reaches every control, and clicking a knob still enables its scroll wheel.
- The standalone app remembers its audio/MIDI device choice in `~/Library/Application Support/YOI-BOI/YOI-BOI.settings` (device settings only; the sound is still never saved automatically). ZOOM and CLOSE no longer start a window drag.

**Historical comparison change.** `--check-compat` still compares the very same saved project in the old and new binaries sample for sample. For engine-3 sounds (saved by 0.1.3–0.1.5), the comparison now stops at the fixture's final CC123, at 1.79 s of a 2.0 s render. Before that frame the tolerance is unchanged (maximum error 2e-6; observed 0). After it, the old binaries hard-cut while the update releases. The harness therefore also requires the new tail to stay at or below 1.25× the peak of the 50 ms before CC123, and the last 50 ms to be no louder than that window. Engines 1 and 2 are still compared over the full render.

## Practical limits

These checks exercise real VST3 binaries in an automated host. A complete interactive Ableton save/reopen, automation, freeze/flatten, export and long-session test has not been performed. Installer transactions are tested in isolation; the actual user installation was deliberately left for the installer button. The native installer window has not been interactively inspected in this run.

Backward compatibility covers the saved state from releases 0.1.0–0.1.3. It cannot reconstruct an older sound if that project was already changed and saved using a newer engine. Original DSP behavior is preserved; processor fixes for pathological MIDI such as repeated stray note-offs remain in effect. New factory presets and New Yoi deliberately select the current engine, while Mutate retains the selected engine.

The current synth interface is retained, with a saved-engine indicator and an explicit, undoable sound-upgrade option. The VST3 still leaves Ableton's native title bar under the host's control. No change claims removal of that host frame.

Future releases must preserve the permanent engine identifiers and VST3/parameter identities and rerun historical comparisons. This is tested compatibility for the releases above, not a promise about untested future versions.

## Reproduce

See README.md for build, integrated test, historical VST3 comparison and installer build commands. The historical comparison implementation is `Tests/CompatibilityFixtures.h`; `Tools/compat-fixtures-driver.cpp` can also host the matrix independently of the instrument's compiled processor. Installer tests run during `Installer/build-installer.sh` and can be repeated with the built installer executable's `--self-test` option. No silent install action is exposed.
