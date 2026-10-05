# BACK-SCRATCH 0.10.1

A live scratch-pattern effect for bass, drums and vocals. Insert it on a track, start playback, and click **Randomize**. A repeating time-warp pattern processes fresh incoming audio; no capture is needed.

The **700 × 940** interface retains its translucent red shell, recessed smoked-glass display, frosted coral control plates and sculpted ivory controls. Small illuminated **Dotted** and **Triplet** buttons sit beside the length control. A second pair of arrows, centered along the bottom of the display, shifts the pattern across its grid.

Version 0.10.1 fixes factory preset navigation: each selection restores the preset’s original grid position, rather than inheriting the previous pattern’s arrow offset. Saved user presets and projects still recall their intentionally stored offsets.

Version 0.10 adds **Free Draw, Line and Curve** tools in a split pencil button, plus **10 Scratch Shapes** with miniature previews. The tools and shape picker sit in the existing screen beside Eraser, Undo and Redo. New factory and generated patterns use rounded motion so their source speed accelerates through the scratch instead of holding a shifted pitch and changing direction abruptly.

The physical **INFO** key introduced in 0.8.1 remains in the lower shell. It opens the Vivid Fever Dreams logo and credits, executive producer profiles, music website, and Dream Sequence donation link. Close the page with **X** or **Escape**.

Version 0.8 aligned the displayed curve and playhead with the audio engine’s delayed playback context, showed gate cuts in the curve, and preserved incoming audio history across normal host loops. The bank still contains **75 presets**, alongside independent pattern generation and the Keep Sub switch.

## Start here

1. Quit your audio apps, open **Install BACK-SCRATCH.app**, and choose **Install / Update**. Rescan VST3 plug-ins in Live if necessary.
2. Insert BACK-SCRATCH and start the track. Keep Sub starts on; phrase length starts at one bar, locked.
3. Drag the **Phrase Length** knob up or down to select **½ beat, 1 beat, 2 beats, 1 bar, 2 bars, 4 bars, 8 bars or 16 bars**. The wheel also changes its value; Shift-drag makes finer movements, and double-click restores one bar.
4. Click **Dotted** to multiply the base length by **1.5**, or **Triplet** to multiply it by **⅔**. Only one can be active; click the lit button again for straight timing.
5. Click **Randomize** to compose a new pattern. Each click builds scratch gestures, placements, cuts and rests independently of the factory bank. The length lock holds the base knob setting; unlock it to let Randomize choose that base length too. Timing mode and grid shift stay as you set them. Manual adjustments always remain available.
6. Browse the **75 factory patterns** with the preset menu or **upper arrows**. The centered **bottom arrows** move the current pattern one visible grid cell left or right, wrapping across the edges. Selecting a factory preset resets this offset to its original position.
7. Click the screen's **Pencil** button to draw. Its small arrow opens **Free Draw, Line and Curve**. Drag on the graph; choose **Eraser** to restore clean input in selected cells. Click the selected tool again to stop editing.
8. Open **Shapes** for ten scratch contours. Selecting one replaces the phrase with an editable starting shape; Undo brings back the previous phrase.
9. **Save** stores a `.backscratch` preset, including your drawing; **Load** restores it. Host projects preserve the same settings and custom pattern.

## Draw, shape and erase

The pencil is a split button: click its main area to arm or disarm the selected drawing tool, or its arrow to choose a different tool. The selected tool lights up and the display gives a short instruction.

- **Free Draw** follows the path of your hand across the graph. It no longer restarts from the baseline in every grid cell. Strokes are sampled by distance across the display, so a slower mouse-event rate does not create larger steps. Light smoothing reduces tiny hand wobbles. Retracing an area redraws that area.
- **Line** stretches a straight line from the point where you press to the point where you release. Moving the endpoint back shortens the preview cleanly.
- **Curve** draws a rounded transition between those two points. This varies the source speed smoothly instead of sustaining a single shifted pitch.

The graph is continuous for drawing; the 16-cell grid remains a timing guide. Line and Curve start at the height where you press, and Free Draw keeps the same precise cursor anchor. Steep gestures are constrained to the engine's forward/reverse speed range of **2×**; the displayed endpoint shows the applied result. Drawing on the baseline or right edge also works.

**Shapes** opens a menu of ten miniature scratch contours. Selecting one replaces the current phrase, retains length, timing and grid shift, and arms Free Draw for further edits. It is one undoable action. These starting shapes are separate from the 75 complete factory patterns.

Click **Eraser**, then click or drag across cells to restore original clean input there. Erasing does not mute the track. Both tools change only the selected interval; curves and cuts elsewhere keep their sound. The graph retains its editing scale, including after erasing everything.

The **Undo** and **Redo** icons restore whole mouse gestures or shape selections, with up to **64 actions** in history. While a tool is armed, **Cmd+Z** undoes and **Cmd+Shift+Z** redoes. **Escape** cancels an unfinished gesture and disarms the tool; closing the editor also cancels an unfinished drag. Click the active drawing tool or Eraser again to disarm it without changing completed work.

Length, Dotted, Triplet and the bottom shift arrows retain your edits. **Randomize** or choosing a factory pattern starts a new pattern and clears drawing history. Loading a preset also begins a new history. Save a drawing before replacing it if you want to keep it; presets and host projects preserve the finished geometry, while undo history is local to the editing session.

## Smooth scratch motion

A straight time-delay ramp produces a constant playback speed. Several previous recipes therefore held a higher pitch on their return stroke and switched speeds sharply at the turn. New recipes use curved time trajectories with acceleration and release; their gates clip the same underlying curve without restarting it. The graph and audio evaluate the same trajectory and exact playback derivative.

This changes the movement of the sound rather than filtering away its high frequencies. Keep Sub, the phase-aligned crossover and the full-range interpolation path retain their existing design. Drawing a straight line intentionally still produces a constant playback speed; choose Curve or a Scratch Shape for rounded motion.

Older saved presets and projects keep their original recipe version. Pressing Randomize or selecting a factory pattern chooses the revised motion for new work. Saved hand-drawn shapes retain their exact geometry.

## One pattern, different durations

Turning the length knob changes how long it takes to traverse the pattern already displayed. It **never repeats, adds, removes or regenerates its gestures**. A pattern with three scratches still has three scratches when moved from one bar to four bars; the whole phrase unfolds four times more slowly. Changing it to half a beat compresses the same phrase into half a beat.

The time and source-offset coordinates scale together. This preserves the contour, stroke order, cuts and source playback speeds while changing the duration of each gesture. **Dotted** and **Triplet** apply the same scaling to the selected base length: a one-beat pattern becomes 1½ beats or ⅔ beat. Only **Randomize** composes a new pattern. For generated patterns, the seed, original composition duration and original meter remain part of the saved identity. The composer stores the unmodified base duration, so changing timing modes or recalling a preset retimes the same composition.

Beat choices mean quarter-note beats. Bar choices follow the host’s time signature: one bar is three quarter-note beats in 3/4. Factory selection retains the chosen base duration when locked; when unlocked, it selects the closest supported base duration to the factory recipe’s original length. Timing mode remains unchanged during factory navigation and Randomize. Factory navigation resets the grid shift to zero; Randomize retains the current shift. See **PRESETS.md** for the complete bank.

## Move the pattern and follow playback

The display has **16 equal grid cells** at every duration. Its bottom arrows move the entire effect one cell earlier or later; material passing one edge reappears at the opposite edge. Curves, gate cuts and gesture order remain intact. Sixteen clicks in one direction return to the starting position. A length change preserves the shift as a fraction of the phrase.

The curve and moving playhead share the same playback context, including the plug-in’s latency and timed parameter changes. Closed scratch gates appear dimmer, so a silent cut no longer looks like a fully audible stroke. The editable curve shows the time effect; it is not an audio waveform, and the result still depends on the incoming track.

Patterns follow song position. Normal loop wraps reported by the host retain the rolling audio history, including when a processing buffer crosses the loop boundary. Starting playback or manually seeking resets history. **LIVE INPUT** appears when an active gesture needs past audio that is unavailable, and the engine fades to the original input for that portion.

## Keep Sub and audio quality

**On:** the original low layer stays at its original pitch and rhythm beneath the scratched upper layer. The complementary split is centered at **200 Hz**, with a gradual transition.

**Off:** the entire signal is scratched, including the bass. It does not remove the sub. Randomizing or selecting a factory pattern preserves this switch; loading a saved preset restores its saved setting.

The phase-aligned audio path from 0.5.0 is retained. It reports **730 samples of latency at 48 kHz**, about **15.2 ms**, so the host can compensate. Dry audio, preserved sub and scratching share this timing.

A **128-second rolling history** stores incoming audio. It uses approximately **98 MB at 48 kHz stereo** or **393 MB at 192 kHz stereo**, per instance, plus processing overhead. After startup or a manual seek, unavailable history fades to dry audio until enough material has arrived. Long settings can also exceed the window: some **dotted 16-bar patterns in 4/4 at 20 BPM** already need more history, and larger host meters can extend the lookback further. Those portions fade to dry and show **LIVE INPUT**; the plug-in never reads invalid audio. Shortening the phrase or choosing a shallower pattern reduces the lookback.

## Save, automate and print

Saved presets and host projects retain your custom drawing and its graph scale, generated identity and seed, original composition duration and meter, the selected factory recipe and variation where applicable, base playback duration, straight/dotted/triplet timing, grid shift, length lock and Keep Sub. The same pattern repeats over fresh incoming audio rather than drawing new randomness on each pass.

Host controls include **Pattern Motion Version** (1 for original recipes, 2 for rounded recipes), **Edited Pattern**, **Generated Pattern**, **Pattern Seed**, **Phrase Length**, **Phrase Timing**, **Pattern Grid Shift**, **Phrase Length Lock**, **Keep Sub** and **Bypass**, plus **Scratch Pattern** and **Variation** for factory mode. **Edited Pattern** selects the saved custom geometry. Individual drawing points are saved with presets and projects; mouse coordinates are not separate automation parameters, and drawing does not record a sequence of gestures into a Live automation lane. The current Phrase Length parameter uses the ID `phraseDuration`; the old `phraseLength` ID remains as **Legacy Phrase Length** and no longer controls duration. Timing uses `phraseTiming` (0 straight, 1 dotted, 2 triplet), and `patternShift` stores 0–15 grid steps. **Pattern Original Length** and **Pattern Original Meter** preserve generated composition identity.

Record controls with Live’s automation recording. Use Live’s resampling, freeze or export to print audio. Space remains available for host transport.

## Updating

Version 0.10 writes **preset/project schema 9**, including custom geometry, graph scale and recipe motion version. **Schemas 4–8** remain supported and retain their original recipe motion. Schema 8 also retains saved drawings. Schema 7 retains its timing mode and grid shift; schemas 4, 5 and 6 load with **straight timing and zero shift**, including files saved by 0.6 and 0.7. Schemas 4–7 start without a custom drawing. Invalid timing or custom geometry is rejected before settings change.

Original factory IDs 0–24 retain their names and recipes; the expanded bank appends 50 patterns. Factory length stretches one complete motif, replacing the earlier repeated-motif behavior, so older factory phrases can sound different. Version 0.5 generated states retain their original bar count and use the actual host meter for their composition, matching the older meter behavior; the knob then retimes that graph. New patterns freeze their original composition meter when Randomize is pressed. Review existing factory-selection automation from the 25-preset releases: expanding the host parameter to 75 choices can change the meaning of old normalized automation values. Re-record old duration automation against the current Phrase Length control if needed.

The former 0.4 Remove Sub setting means **Keep Sub off: full-range scratching**. Versions 0.1–0.3 used captured audio and wheel gestures; those states open bypassed. Choose a factory pattern or Randomize to start the live engine. Keep an earlier release for old wheel performances. Retired parameter identities remain as Legacy entries; the plug-in identity is unchanged.

## Build and verification

Apple Silicon macOS 12+, VST3. Plug-in and installer are locally ad hoc signed. See **VALIDATION.md** for automated audio, VST3, preset, transport and interface checks. Actual Ableton session recording and subjective listening remain unverified.

Build with C++20, CMake and the included patched JUCE tree. Keep the source archive's `back-scratch` and `shared` directories beside each other: the shared folder supplies the VFD logo and Info interface. Run these commands from `back-scratch`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --target BackScratch_VST3 BackScratchTests BackScratchHostSmoke --parallel 4
build/BackScratchTests_artefacts/Release/BackScratchTests
build/BackScratchHostSmoke_artefacts/Release/BackScratchHostSmoke
```

`Installer/build-installer.sh` accepts the absolute VST3 path, a fresh output directory, and version `0.10.1`.
