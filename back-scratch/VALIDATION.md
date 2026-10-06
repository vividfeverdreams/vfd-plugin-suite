# BACK-SCRATCH 0.11.0 validation

Checked on Apple Silicon macOS, 5 October 2026, with the Release build from this source (VST3 and AU in `build/BackScratch_artefacts/Release`). Nothing was installed into `~/Library` or `/Library`; every run used a scratch folder as HOME, and preset tests used a temporary preset root, never the real one.

## What was checked

**Old projects, against the real 0.10.2 release.** A plain host with no BACK-SCRATCH code loaded the BACK-SCRATCH.vst3 from the Vivid Fever Dreams Suite 1.2.2 installer payload (read-only). For four projects (a factory recipe with dotted timing, grid shift and Keep Sub off; a generated two-bar triplet pattern; a legacy-motion recipe; a hand-drawn custom pattern) a fresh 0.10.2 instance saved the project and rendered 240 blocks of a bass-and-tone test signal at 140 BPM. The check confirms the 0.10.2 build kept each setting and was actually scratching. In a second process, a fresh 0.11 instance opened each saved project. All 14 0.10 controls came back exactly, all 11 new controls sat at their neutral defaults, and the audio matched:

```
0.10.2 payload -> 0.11 factory recipe, dotted, shifted, Keep Sub off: maximum difference 2.98023e-08 over 122880 stereo frames
0.10.2 payload -> 0.11 generated pattern, two bars, triplet: maximum difference 2.98023e-08 over 122880 stereo frames
0.10.2 payload -> 0.11 legacy motion recipe: maximum difference 2.98023e-08 over 122880 stereo frames
0.10.2 payload -> 0.11 hand-drawn custom pattern: maximum difference 2.98023e-08 over 122880 stereo frames
BACK-SCRATCH 0.10.2 project compatibility passed
```

2.98e-8 is the smallest step a 32-bit float can take near these sample values, so this is a rounding-level difference, not a change in sound. While building this test, a reused 0.10.2 instance ignored host parameter changes made after its first prepare/release cycle. That is the 0.10 automation-queue behaviour fixed in 0.11, so each project is now saved from a fresh instance, as a real session would be.

**Default sound unchanged.** The engine suite checks that the new shaping stage (Mix, Output, split choice, speed limit) at its default settings produces exactly the same samples as the engine without it; the 0.10.2 comparison above is the end-to-end proof against the real old release. Existing regression suites (75 factories, 5,000 generated phrases, motion profiles, history, continuity, transport, drawing, recall and schema migration) all still pass.

**New features.** The engine tests cover Mix and Output, all four Keep Sub splits at the same 730-sample latency, 4× strokes, live history resizing and validity of all 230 bank presets. The host tests cover the PERFORM page (real clicks), Engage snap timing on the audible clock, Retrigger (two processors in lockstep: the retriggered phrase restarts at the beat-9 snap point, one beat behind song position), Play Every, slots and chains, Mix/Depth/Output bars, the roll tool and starters, the Size menu (fit rules), the factory bank and arrows, user preset folders (temporary root), and project and preset round trips including slots, history mode and editor size.

**Factory bank export.** All 230 presets were written as schema 10 files in 17 folders, reopened in a fresh instance and compared setting by setting and slot by slot.

**Audio Unit.** The AU builds and is signed with the agreed identity (type `aufx`, subtype `Bsc1`, manufacturer `Yoib`, bundle ID `com.yoiboi.backscratch`, version 0.11.0). pluginval cannot test an AU that macOS has not registered, and registering it means installing it into a Components folder, which was not allowed here. So pluginval on the `.component` stops at "No types found". Instead, the plain host registers the built `.component` with its own process only (`AudioComponentRegister`; nothing is installed or written to the system), hosts it through JUCE's AU host, and compares it with the VST3: same 33 controls, same 730-sample latency, identical audio, and a saved AU state reopened in a new AU instance sounds identical. **Apple's `auval` and pluginval have not been run on the AU**; run them after installing (for example `auval -v aufx Bsc1 Yoib`).

## Verbatim results

Engine and pattern suite (`BackScratchTests`), exit 0:

```
BACK-SCRATCH 75 factory patterns, 5,000 complete generated phrases, exact duration scaling, and circular grid shifts passed; maximum 183 segments
Grid editing: all 75 factories at 16 shifts, exact curve clipping, safe draw slopes, dry erasing and retiming passed
Drawing tools: event-independent freehand, fractional/reversed lines, eased curves, exact shifted clipping, 10 musical shapes, 5 roll/tape starters and the roll tool passed
Motion profiles: Sub Tug speed step 3.12 -> 0.00149712, accelerating wet frames 0.004% -> 99.944%; 61518 curved intervals, 22097 clipped/gated intervals, largest continuous velocity corner 5.94191e-13; audio/trajectory relative error 4.59853e-06 over 20530 samples; legacy golden geometry exact
75 shortest half-beat phrases at 400 BPM, both sub modes: peak 0.432171, smallest source change 0.166368, energy ratio 0.776383..0.993329
36 full generated source-family/sub-mode renders at 140 BPM: peak 0.475974, energy ratio 0.667613..1.11396, minimum original bass 0.999625x, rejected alias RMS 2.46463e-05
75 presets x bass/drums/vocal-like sources: peak 0.634231, smallest normalized change 0.0767872
128-second history: 96-second impulse error 6.06676e-17, 16-bar / 20 BPM source error 1.31117e-08 across 381768 deep-lookback samples
Live scratch engine: neutral relative error 1.47976e-24, broadband transition error 0, time-warp relative error 2.50489e-05; sub kept/full 0.999728/0.948897x; 100 Hz kept/full 0.990301/0.945598x; jump step 0.0522105; rejected alias RMS 6.01481e-06
Engine continuity: bypass-on step 1.05376x normal, gapless invalidation, moving fallback step 0.0143984, tempo-ramp crossfades 0 (1 beat) / 34 (4 beats) over 100 callbacks, memory fallback 128 -> 16 -> 0 s
0.11 engine features: defaults bit-identical, mix/output, four Keep Sub splits at one latency, 4x strokes, live history resize and 230 valid factory bank presets passed
BACK-SCRATCH live engine and pattern tests passed
```

Host suite (`BackScratchHostSmoke`), final build, run three times, exit 0 each time. The slowest display read varied with machine load (3.05, 1.01 and 0.76 ms; 0.81 ms in the VST3 run; the test limit is 250 ms). Output of the third run:

```
BACK-SCRATCH live processor, automation, preset and transport tests passed
BACK-SCRATCH single-pass duration scaling, short preset recall, meter and delayed automation integration passed
BACK-SCRATCH audible display, shifted/modulated automation, tempo and seek alignment tests passed
BACK-SCRATCH all 75 factory shifts isolated across direct recall, reselect and host programs; saved offsets and audio retained
BACK-SCRATCH dotted/triplet shape identity, shifted preset and project recall, schema migration and timing validation passed
BACK-SCRATCH immutable edit lifetime, single-stroke undo, custom recall and audible drawing alignment passed
BACK-SCRATCH clipped factory/generated curve recall, timed retiming and atomic invalid-geometry rejection passed
BACK-SCRATCH smooth/legacy recipe recall, schema4–8 migration, custom geometry preservation and invalid version rejection passed
BACK-SCRATCH freehand, line/curve, actual tool/shape menus, live preview, erase, undo/redo, cancellation and scaling checks passed
BACK-SCRATCH transport continuity: host bypass 830-sample impulse, ramp silent samples 512:0 512:0 512:0 1024:0 1024:0 1024:0, gapless wraps/seeks, bypass-on step 1x normal, no-op writes silent, slowest display read 0.759958 ms, per-instance queues, atomic parameter groups passed
BACK-SCRATCH 0.11 PERFORM page, Engage snap, Retrigger, Play Every, slots and chains, Mix/Depth/Output, roll tool, Size menu, factory bank, user preset folders and project round trips passed
BACK-SCRATCH control layout, indicator clearance, duration knob, dotted/triplet timing, circular pattern nudges, crop, generation, phrase lock, sub toggle, preset navigation and editor lifecycle checks passed
BACK-SCRATCH live pattern host smoke passed
```

The same suite with screenshots and the built VST3 added these lines (lines printed several times are shown once), exit 0:

```
Packaged VST3 real processing matches processor; worst difference 0
Packaged VST3 automated timing and looping maximum difference 0
Packaged VST3 recalled/static timing and looping maximum difference 0
Packaged VST3 dotted/triplet, grid-shift, loop and project-recall tests passed
Packaged VST3 pencil/eraser contour audio and custom project-container recall passed
Packaged VST3 host bypass: impulse delayed by 730 samples (reported latency 730)
BACK-SCRATCH live pattern host smoke passed
```

In-process AU check (`BackScratchCompatHost --au`), exit 0:

```
AU bundle BACK-SCRATCH.component: VIVID FEVER DREAMS: BACK-SCRATCH, aufx/Bsc1/Yoib, version 0xb00, registered in-process only
AU and VST3 publish the same 33 plug-in controls
AU vs VST3 default render: maximum difference 0 over 122880 stereo frames
AU state reopened in a new instance: maximum difference 0
BACK-SCRATCH AU in-process check passed
```

Factory bank export (`BackScratchHostSmoke --export-presets`), exit 0:

```
Exported and recalled 230 schema 10 factory bank presets in 17 folders
```

pluginval 1.0.4, strictness 5, in-process, HOME pointed at a scratch folder: **VST3 `SUCCESS`**, exit 0. AU: `!!! Test 1 failed: No types found ... an AU that isn't found by macOS so can't be created.`, exit 1 (the AU is not installed, see above).

## Screens

28 editor screenshots were rendered from the real editor and inspected: the PATTERN and PERFORM pages, a busy PERFORM page (slot C selected while the A–D chain plays A, Mix 70%, Depth 140%, Output −3 dB, 160 Hz split, 64 s history), the slot right-click menu, the Size menu (only 90% fits the test display, so the larger sizes are greyed out), the editor at 125%, the preset menu with folders, the roll tool with its hover preview, the Shapes and tools menus with the roll entries, Play Every waiting and Engage standby states, four new presets, and the earlier pencil, eraser, timing and INFO screens. They use the existing Chakra Petch type, palette, LEDs and soft-key style.

## Not verified

- Use inside an actual Ableton Live (or Logic) session, automation recording there, and listening tests.
- The AU with `auval`, pluginval or Logic, which needs it installed.
- How the new presets sound on real material; their audio was checked only for validity, not for taste.
- The installer was not rebuilt or run for 0.11.0.

Earlier release evidence follows as historical context.

# Prior 0.10.1 validation

Checked on Apple Silicon macOS, 4 October 2026.

## Factory position isolation

Reproduced the reported carryover on 0.10.0: shifting one factory pattern then selecting another left the old grid offset applied. The new regression failed before the fix. Factory selection now writes zero to the existing Pattern Grid Shift parameter with a balanced host gesture. The menu, browsing arrows, same-preset reselection and host program selection share this path.

The focused regression passes for all 75 factory presets with length lock on/off and straight/dotted/triplet timing. It checks direct selection, reselection, host program selection, generated/custom-to-factory transitions, original curve/display identity, length-lock behavior and Keep Sub preferences. Two rendered stereo comparisons exactly match freshly loaded unshifted instances. User preset and project recall retain their intentionally saved offsets. Randomize and the Shapes picker retain their existing shift behavior.

## Packaged verification

The complete native host suite passes against the installer's embedded VST3. Real editor click tests shift a pattern using the bottom arrows, navigate both directions across the bank and verify original positioning. The independently delayed audio/display oracle expects the new factory position after switching from a shifted drawing. Existing drawing, state, timing, loop and actual VST3 processing checks pass; recorded VST3 audio comparisons have maximum difference zero.

Pluginval passes at strictness 5. All 40 isolated installer transaction/rollback checks pass. Manifest hashes, plug-in identities and ad hoc signatures verify. Installer and plug-in versions are 0.10.1; the embedded executable is byte-identical to the tested build. All 75 bundled factory preset files retain zero grid shift. Build, pre-fix failure, post-fix success, host and validation logs are in Checks.

Only one production behavior changed: factory selection clears the prior pattern's grid offset. The scratch engine, motion recipes, editor layout, parameter identities and saved-state schema are unchanged from 0.10.0. The independent DSP-only suite was not rerun for this preset-loading fix; the full processor/audio/display/VST3 host suite was run. The source comparison is in Checks/production-diff.txt.

Actual Ableton-session use remains unverified. These checks did not install into the user's plug-in folder or modify a DAW session. The included Audio Comparisons are the unchanged 0.10.0 motion examples, not new listening evidence for this fix.

Earlier release evidence follows as historical context.

# Prior 0.10.0 validation

Checked on Apple Silicon macOS, 1 October 2026.

## Scratch-motion finding and correction

Several original factory and generated gestures used straight time-delay ramps. Such a ramp has a constant playback derivative, so it produces a sustained shifted pitch. At a corner, source position could remain continuous while speed jumped abruptly from reverse to fast forward. The engine reproduced that supplied motion accurately; no independent interpolation or crossover defect was found.

New recipe version 2 replaces moving strokes with bounded smoothstep trajectories and preserves the original polynomial domain through gated subdivisions. Tape brakes also decelerate continuously. Delay, displayed curve and source speed share the same analytic evaluator. There is no added high-frequency filtering, output limiter or hidden readhead smoothing. Linear hand-drawn ramps remain intentionally linear; Curve and the new scratch starters provide rounded movement.

In the Sub Tug regression sampled at 100,000 positions, the largest adjacent active speed change falls from 3.12× to 0.00150×. The fraction of active samples with changing speed rises from 0.004% to 99.94%. Across 61,518 moving intervals, including 22,097 clipped/gated intervals, the largest continuous-position velocity-corner error is 5.94e-13. An independent sine-phase oracle compares actual rendered audio against the displayed read position across 20,530 accelerated forward/reverse samples: relative error 4.60e-6.

Original recipe version 1 is retained. Frozen-source fingerprints cover 300 factory variants and 1,200 generated phrases. A separate comparison of 1,536,000 evaluated samples against the archived v0.9.0 source is byte-identical. This preserves older sessions; choosing a factory preset or Randomize explicitly starts the new smooth recipe. Existing hand drawings retain their exact coordinates.

## Drawing tools and shape library

The split pencil provides Free Draw, Line and Curve. Free Draw uses 128 spatial samples, exact gesture anchors/endpoints and light spatial smoothing, independent of mouse-event timing. It no longer resets to the baseline or advances one entire grid cell per movement. Line and Curve rebuild their preview from the original gesture snapshot, so shortening or reversing a drag leaves no obsolete edits. All actual trajectory slopes are bounded to source rates between -2× and +2×. Eraser, gesture undo/redo, cancellation, stable graph range, timing and shifts retain their existing behavior.

Ten starter shapes are separate from the 75 full factory patterns. Each is a complete editable custom contour, retains timing and shift, and loads as one undoable action. The actual JUCE menus show miniature trajectories and are attached inside the editor. Their callbacks use safe component lifetimes; closing the editor dismisses its own popup.

Focused math tests cover all 16 shifts, precise fractional anchors, sparse versus 387-event equivalent strokes, backward/revisited freehand, untouched-region preservation, exact clipping, dense 512-segment replacement and invalid/capacity failure without partial edits. Every starter is checked at six duration extremes for valid geometry, reverse movement and continuous speed through joined strokes. Existing and new math suites pass AddressSanitizer and UndefinedBehaviorSanitizer.

The focused freehand benchmark records dense 107-segment render CPU time at 0.383 ms median / 0.861 ms p95, below a 16.7 ms frame budget. Typical rendering is 0.054 ms median / 0.142 ms p95. Wall-clock outliers during the concurrent build reflect scheduling and are retained in the log; these are helper benchmarks, not complete DAW UI performance measurements.

## Integrated verification

The complete native engine and host suites pass. Host coverage includes real JUCE popup selection and dismissal, editor closure with a pending menu, Free Draw/Line/Curve gestures, shape selection and refinement, undo/redo, resize, unchanged hardware-light pixels, length/timing/shift preservation and strict saved-state validation. The engine suite covers all 75 factories and 5,000 generated patterns, new motion/shape math and the existing full audio regressions.

The actual VST3 scans and instantiates. Baseline output, motion-version/timing/shift automation, host loops, custom drawing and VST3 project-container recall match the direct processor with maximum difference zero. Independent per-sample audio/display oracles cover the new motion-version changes inside the reported latency window. Eleven native editor screenshots include normal, Free Draw, eraser, selected starter, both actual open dropdown menus, timing variants and INFO. The normal screen and both dropdown screenshots were visually inspected.

The first sandboxed GUI run exposed JUCE's assumption of an available screen when placing popups. A null-display guard now avoids that crash. Native menu tests still require real menus and ran successfully with normal macOS GUI access; they were not skipped or replaced by direct action calls.

The final installer payload passes the complete native host suite again, including the actual VST3's custom drawing and project-container recall with zero difference. Pluginval passes at strictness 5. All 75 exported schema9 factory files were loaded and compared against their source patterns. Installer hashes, identities and ad hoc signatures verify; all 40 isolated installation/rollback checks pass. The payload executable matches the tested build byte-for-byte. Verification does not install into the user's plug-in folder.

## Compatibility and persistence

Schema 9 stores the recipe motion version with the existing geometry. Motion Version is appended to the host controls, preserving earlier parameter IDs and ordering. Schemas 4–8 load their original motion; schema 8 drawings remain exact. New portable factory presets use schema 9 and rounded motion version 2. Invalid or conflicting version metadata is rejected before state changes. The graph and delayed audio context carry the same motion version through automation.

The DSP filter/interpolator code is unchanged. Current audio regression checks include all 75 factories on bass/drum/vowel-like signals, short phrases at 400 BPM, 36 generated source/sub combinations and the long-history checks. Generated Keep Sub retains the original bass at a minimum measured 0.999625×; the factory/source-family maximum peak is 0.634231. Latency stays 730 samples at 48 kHz and the rolling-history budget stays 128 seconds.

Audio Comparisons contains controlled synthetic bass, drums and vowel-like signals, with matched original and revised Sub Tug motion. Eleven 48 kHz / 24-bit WAVs preserve original levels and compensate engine latency; peak 0.83548, with no clipping or normalization. These examples isolate motion behavior and are not a listening test on the user's own tracks.

Actual Ableton session behavior and subjective listening remain unverified. The automated host provides explicit audio, transport and parameter data; physical speaker timing still depends on host/device buffering. No installation or changes to a user's DAW session are performed by verification.

Earlier release evidence follows as historical context.

# Prior 0.9.0 validation

Checked on Apple Silicon macOS, 29 September 2026.

## Grid editing and audible behavior

Pencil, Eraser, Undo and Redo are integrated into the existing display. The shell, INFO page, length and timing controls, shift arrows and original light remain intact. Tools start unarmed; click Pencil or Eraser to edit. Pencil joins grid points with immediate playback preview. Eraser restores clean input in touched cells, including the full signal when Keep Sub is off. Each completed mouse gesture creates one undo step, up to 64; Escape or closing the editor cancels an unfinished gesture. The graph retains its editing scale after erasing all cells.

Editing changes only the touched interval. Clipped factory and generated curves retain the original smoothstep domain, so untouched portions retain their exact source trajectory. The actual drawn delay slope is constrained to keep forward/reverse source speed within 2×, matching the displayed line; the processor does not merely clamp a speed label. Length, dotted/triplet timing and grid shifts preserve custom geometry.

Custom patterns use immutable snapshots in a fixed 32-slot pool allocated at processor creation. Audio history, the rendered pattern and display retain their own snapshots. Each delayed control frame carries the geometry that belonged to that frame, so rapid live edits do not rewrite audio still inside the latency path. The pre-gesture snapshot stays pinned until commit or cancellation. Pool exhaustion leaves the last valid preview intact, and cancellation can restore its pinned original without needing another free slot. The edit handoff introduces no audio-thread locks or allocations.

## Verification

- The complete engine suite passes: all 75 factory recipes, 5,000 generated phrases, timing/shift invariants, real audio renders, interpolation, protected bass, long history, loops and transport regressions.
- New pattern tests cover all 75 recipes at all 16 shifts, exact curve clipping, dry erasure, untouched-region preservation, both drawing directions, rate limits, fixed graph scale, extreme phrase lengths and transactional storage-capacity failure. The math suite also passes AddressSanitizer and UndefinedBehaviorSanitizer.
- Snapshot-pool stress exercises full capacity, recycling, concurrent acquisition and guaranteed cancellation. A standalone ThreadSanitizer run covers 20,000 publications with concurrent reads and reports no data races.
- Direct processor checks cover repeated live previews, one-stroke undo/redo, cancellation, 24 length/timing combinations, state and preset recall, legacy-schema migration and independent per-sample audio/display comparison with multiple geometry changes inside the latency window. Block sizes include 64, 257 and 1,024. The maximum accepted audio error remains 3e-5.
- Saved factory and generated edits include genuinely clipped curved segments. Tests compare all curve-domain and graph-scale fields and 4,096 trajectory samples after project/preset recall, then retime and shift them. Fourteen malformed-geometry cases are checked against both loaders; rejected data leaves the whole saved state and undo history unchanged. The maximum 512-segment preset is also saved and loaded above the former 64 KiB limit.
- Native mouse tests cover tool arming, live drawing, sparse drags, vertical refinement, backward drawing, erasing across cells, one-step undo/redo, Escape, editor closure, resizing, baseline and right-edge input, inert off-grid starts and DAW-key passthrough. Tool hit regions do not overlap existing controls, and the original shell light passes its pixel comparison.
- Native screenshots show the normal screen, both active tools, short and long names, timing modifiers and INFO. Drawing/erasing screenshots contain actual committed geometry, not an illustration. The new toolbar and both edited views were visually inspected.
- The actual built VST3 scans and processes in the local JUCE host. Baseline, automated timing/shift/loop, custom pencil/eraser geometry and actual VST3 state-container recall match the direct processor with maximum difference zero. The custom geometry is drawn through the direct native editor and then passed through the VST3 state boundary; these tests do not claim native mouse delivery through JUCE's embedded VST3 window wrapper.
- Pluginval passes at strictness 5. Its sandboxed launch aborted before producing results; the successful run used normal local macOS GUI access without installation or changes to a DAW session.

The final installer's embedded VST3 also passed the complete host suite and Pluginval at strictness 5. Its executable matches the tested build byte-for-byte; the installer manifest and plug-in identities verify, ad hoc signatures verify, and all 40 isolated installer transaction/rollback tests pass. No installation into the user's plug-in folder was performed. Final logs accompany this release.

## Persistence and scope

Preset/project schema 8 saves custom geometry and the fixed graph scale. Schemas 4–7 remain supported and restore with custom editing off. Existing automation parameter IDs remain; Edited Pattern is appended. Points are saved with the project and preset, rather than recorded as separate mouse-point automation. Undo history belongs to the editing session and is cleared when loading or replacing a pattern.

The 75 portable factory files retain their valid schema 7 format, straight timing and zero shift. New user-saved presets use schema 8. Source archives include the sibling shared directory required for the VFD Info page.

The audio interpolation, crossover, gate smoothing and jump crossfades retain their previous design. Current engine measurements are in Checks/engine-tests.txt. Latency remains 730 samples at 48 kHz. After a start or seek, missing history falls back to live input; the 128-second history budget is unchanged. Screen refresh and host/device buffering still affect physical display/speaker timing.

Actual Ableton session use and subjective listening on the user's tracks remain unverified. Tests establish deterministic processing, state recall and stable output, not a subjective sound-quality rating. The installer and VST3 are locally ad hoc signed. Validation does not install into the user's plug-in directory or modify an Ableton project.

Earlier release evidence follows as historical context.

# Prior 0.8.1 validation

This release adds the physical INFO key and Vivid Fever Dreams credits page. The original header and controls are retained. All fourteen processor, DSP and original artwork files checked against the 0.8.0 source release are byte-identical. The native closed-page screenshot is pixel-identical to 0.8.0 outside the INFO key rectangle.

The complete live-engine/pattern and host suites passed. The host suite exercises INFO opening, Escape and close-button dismissal, full-editor overlay bounds at native and compact sizes, clearance from existing controls, exactly four external links with their approved URLs, and preservation of every host parameter. It also reruns existing control, timing, state, playback and actual VST3 checks. Hosted VST3 audio differences remain zero in the recorded comparisons.

Native shell and credits screenshots are included. Pluginval passes at strictness 5. macOS GUI validation required normal local application access; the tests do not install the plugin or modify a DAW session. Existing audio processing, parameter IDs, preset schemas and plugin identities are unchanged.

The 0.8.1 installer was rebuilt with its current payload. Embedded file hashes, plugin identities and ad hoc signatures verify, and the isolated installer self-tests pass. No installation into the user's plugin directory was performed.

The final host run also checks that unrelated keys are consumed while the credits page stays open. The shared page absorbs mouse-wheel input, preventing shortcuts or wheel actions from reaching the controls underneath it. Final rebuild and host logs use the `input-guard` suffix.

Detailed current logs are in the release's `Checks` directory. The earlier engine/design validation below remains historical context; its date and limitations are unchanged.

## Prior 0.8.0 validation

Checked on Apple Silicon macOS, 26 September 2026.

## Playback and display corrections

Three concrete defects were reproduced and corrected:

- The display used the input clock and current controls while audio used a latency-delayed context. Graph, playhead, duration, pattern identity, tempo and meter now come from one coherent snapshot of the final rendered audio sample. While stopped, the display previews edits immediately. The moving point uses the exact double-precision audible trajectory, rather than reconstructing it from a rounded display phase.
- A valid host loop wrap was treated as a seek and cleared the rolling audio history. The processor now wraps the clock within each callback and preserves history across expected loop boundaries. Actual seeks, start/stop and project restoration still reset history. Pre-roll into a short loop and near-boundary floating-point host positions are covered.
- Numerically equivalent PPQ positions on a scratch boundary could choose different segments. A difference around 4.4e-16 beats selected neighboring gate/readhead states and caused an audio delta of 0.00837569 in the strict independent loop test. Transport evaluation now chooses the same right-hand segment within a microscopic boundary tolerance (max 1e-12 beats or phrase length × 1e-12). The UI uses a separate, shared segment evaluator for exact one-sided curve endpoints, so this correction cannot connect unrelated retriggers or distort wrapped curves.

The interface refreshes at 60 Hz during playback and 30 Hz while idle. Closed scratch gates dim their curve. LIVE INPUT appears when a wet gesture needs unavailable past audio and the engine falls back to incoming audio; ordinary dry segments do not trigger the notice.

## Timing and circular shifts

Dotted timing multiplies the base duration by 1.5; Triplet multiplies it by 2/3. One mode can be active; selecting it again returns to straight timing. Both retime the existing contour, preserving segment count, source playback rates, gates, rests and composition identity. The knob still has eight base positions from half a beat through sixteen bars.

Sixteen equal visible grid cells are shared with the nudge controls. Left/right move the complete effect by one cell, wrapping at the phrase edges. Curves retain their original parameterization across the seam. Sixteen nudges restore the original effect exactly. Changing duration retains the same fractional offset; randomization and factory navigation retain timing mode and offset. Preset files and host projects store both.

Factory generation remains unchanged and bounded to its prior source range. Playback retiming now supports 1/6–384 quarter-note beats, covering the modifier extremes within the supported host meters. Storage remains fixed at 512 segments.

## Automated coverage

- Pattern tests cover all 75 factory recipes, 5,000 generated phrases, pure duration scaling, signed circular shifts, negative transport positions, curved seams and exact restoration after sixteen steps. Boundary regressions exercise all factory recipes plus generated examples at six durations, four offsets and every segment boundary, with neighboring floating-point positions on positive and negative cycles. Explicit display endpoints retain the intended side of discontinuities.
- Processor timing/state tests cover all 75 factories and 16 generated compositions at all eight lengths and all three timing modes. They check saved preset/project identity, exact shifted recall, unlocked randomization retaining modifier/offset, extreme meters and strict invalid-field rejection before changing state.
- Schema 7 adds timing and shift fields. Version 4, 5 and 6 presets/projects open straight and unshifted even in an already modified instance. Captured-wheel states retain their existing bypassed migration. Existing meter-aware generated-pattern migration remains covered.
- Independent per-sample timeline/audio oracles cover 8, 44.1, 48 and 96 kHz at 64-, 257- and 1,024-sample buffers. They include sample-offset seed/timing/shift events, tempo and meter changes, negative PPQ, GUI edits between callbacks, start/stop, seeks and history availability. The graph and active trajectory match the context that produced the audio; audio comparisons pass the unchanged 3e-5 maximum-error threshold.
- Loop tests compare against a continuously running engine at 257- and 1,024-sample buffers, including loops that wrap inside a callback. A tiny pre-roll loop covers multiple wraps in one callback. A position just below loop end followed by the exact loop start does not cause a spurious reset. An actual jump inside loop mode still clears history.
- Real mouse interaction checks cover dotted/triplet/straight transitions, modifier lamps, balanced automation gestures, both nudge directions, sixteen-step wrap, no recomposition, canceled clicks/drags, Escape, length lock, all preset navigation, knob drag/fine drag/wheel, resize and editor closure during a gesture. All control regions remain inside the cropped shell, do not overlap, and clear the original top LED.
- Five native editor screenshots cover straight generated, shortest phrase, long factory name, active Dotted with a positive shift and active Triplet with a wrapped negative shift. Dotted and Triplet views were visually inspected. The original hardware LED also passes the existing pixel comparison.
- The actual VST3 scans and instantiates in the JUCE host. Baseline audio matches the direct processor exactly. Dotted/triplet/straight automation, grid shifts, sub changes, generated identity, loops and actual VST3 state-container recall also match exactly: maximum difference zero in every packaged-processing comparison.

## Audio regression measurements

The interpolation, crossover, gate smoothing and jump-crossfade design are retained. All engine checks were rerun with the final boundary evaluator:

- All 225 factory/source-family renders remain finite; maximum peak 0.633212.
- All 75 factories at the shortest straight half-beat length, 400 BPM and both sub modes pass; peak 0.433714.
- Thirty-six generated bass/drum/vocal-like renders pass; peak 0.498534, minimum original bass retention with Keep Sub on 0.999705×.
- The 128-second history reproduces the 96-second delayed impulse before/after wrap with error 6.07e-17. The sixteen-bar straight phrase at 20 BPM follows deep source positions with maximum error 1.31e-8 over 498,424 checked samples.
- Neutral relative null error is 1.48e-24; neutral switching error is zero; time-warp interpolation relative error is 2.50e-5; rejected alias RMS is 6.01e-6 on the defined test.
- Latency remains 730 samples / 15.2083 ms at 48 kHz. Keep Sub protects the complementary low layer around 200 Hz; off scratches the full signal.

The engine suite was run normally for this release. Sanitizer coverage recorded for the preceding engine release is not presented as a new 0.8 sanitizer run.

## Release checks and limits

The 75 portable factory preset files use schema 7 with straight timing and zero shift. The final installer payload passes the complete VST3 host test again, including timing, shift, looping and recall with zero difference from the direct processor. Installer and payload signatures verify, and all 40 installer integrity/transaction/rollback checks pass in temporary directories. Detailed test logs accompany the release.

The display reflects the output being rendered by the plug-in. Host/device buffering and screen refresh still affect when that output physically reaches the speakers and screen. Actual Ableton Live session automation, compensation, looping and freeze/export remain unverified; these checks use a local VST3 host with explicit transport and automation data.

History must fill after a fresh start or seek. The unchanged 128-second budget is about 98 MB at 48 kHz stereo and 393 MB at 192 kHz. Dotted sixteen-bar phrases at very slow tempos, or long phrases in large meters, can exceed that history; unavailable reads smoothly fall back to live input. Test renders establish deterministic processing and stable levels, not a subjective rating on the user's own sounds.

The installer and plug-in are ad hoc signed for local use. Validation does not install into the user's plug-in directory or alter an Ableton session. Included Audio-Demos are clearly labeled retained straight-timing reference renders; they do not demonstrate the new modifiers and shifts.
