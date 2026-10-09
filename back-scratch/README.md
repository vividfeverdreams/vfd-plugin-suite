# BACK-SCRATCH 0.11.0

A live scratch-pattern effect for bass, drums and vocals. Insert it on a track, start playback, and click **Randomize**. A repeating time-warp pattern processes fresh incoming audio; no capture is needed.

The **700 × 940** interface keeps its translucent red shell, recessed smoked-glass display, frosted coral control plates and sculpted ivory controls. Version 0.11 adds a **PERFORM** page to the same display, so nothing new hangs off the hardware.

## What's new in 0.11

- **PERFORM page.** Two soft keys at the bottom of the display switch between **PATTERN** (the graph and drawing tools) and **PERFORM**.
- **Engage with snap.** Start and stop the pattern on the next 1/16, 1/8, 1/4, 1/2 note or bar line, without a click. Optional **Retrigger** starts the phrase from its beginning when it engages.
- **Play Every.** Play the pattern only on the last of every 2, 4 or 8 phrases, so a fill can stay on the track.
- **Rolls.** A roll tool (1/8, 1/16, 1/32, 1/16 triplet, accelerating, reverse, chopped), five roll and tape-stop starter shapes, and a Snap to grid switch.
- **Pattern slots A–H** with **Chain** (A–B, A–D, A–H): a different pattern on each bar, selectable from host automation.
- **Mix, Output, Depth, Sub Split and Look Back.** Wet/dry blend, output trim, stroke depth up to 200% (and up to 4× speed), Keep Sub split at 120, 160, 200 or 300 Hz, and scratching from up to four bars back.
- **History size.** Automatic (sized to what the pattern needs) or fixed 16, 32, 64 or 128 seconds.
- **230 built-in presets in 17 folders**, including 80 new ones and the 75 originals exactly as 0.10 had them in **Classic**. See **PRESETS.md**.
- **Preset folders.** Your presets save into folders you choose and browse as folders.
- **Size menu.** Right-click an empty part of the shell for 90%, 100%, 125% or 150%.
- **Audio Unit.** BACK-SCRATCH now also builds as an AU, alongside the VST3.
- **Fixes** (also in 0.11, from the same work): no more dropouts when the tempo slows inside a block (half-time drops), host bypass stays in time with the reported latency, no 15 ms hole after seeks and loop wraps, a click-free Bypass, no automation gestures for unchanged values, and safer memory handling. See **Fixed in 0.11** below.

Projects and presets from 0.10 open with every setting intact and sound the same. See **Updating**.

## Start here

1. Quit your audio apps, open the installer and choose **Install / Update**. Rescan plug-ins in your DAW if necessary.
2. Insert BACK-SCRATCH and start the track. Keep Sub starts on; phrase length starts at one bar, locked.
3. Drag the **Phrase Length** knob up or down to select **½ beat, 1 beat, 2 beats, 1 bar, 2 bars, 4 bars, 8 bars or 16 bars**. The wheel also changes its value; Shift-drag makes finer movements, and double-click restores one bar.
4. Click **Dotted** to multiply the base length by **1.5**, or **Triplet** to multiply it by **⅔**. Only one can be active; click the lit button again for straight timing.
5. Click **Randomize** to compose a new pattern. The length lock holds the base knob setting; unlock it to let Randomize choose that base length too. Timing mode and grid shift stay as you set them.
6. Browse the presets with the **⌄** menu beside the name or the **upper arrows**. The centered **bottom arrows** move the current pattern one grid cell left or right, wrapping across the edges.
7. Click the screen's **Pencil** button to draw. Its small arrow opens **Free Draw, Line, Curve** and the **rolls**. Choose **Eraser** to restore clean input in selected cells. Click the selected tool again to stop editing.
8. Open **Shapes** for 15 starting contours. Selecting one replaces the phrase with an editable starting shape; Undo brings back the previous phrase.
9. Click **PERFORM** for Engage, Play Every, slots, Mix, Depth and the other 0.11 controls.
10. **Save** stores a `.backscratch` preset in a folder you choose; **Load** restores it. Host projects keep everything, including slots.

## The PERFORM page

Every control has a tooltip. Click a choice to open its menu; drag the Mix, Depth and Output bars, or use the mouse wheel over them; double-click a bar to reset it.

| Control | What it does | Host control |
| --- | --- | --- |
| **ENGAGE** | Starts or stops the pattern. While playing, the change waits for the next **SNAP** point, then blends in or out over 3 ms. With the transport stopped it applies at once. The light shows PLAYING or STANDBY, and SNAPS IN / SNAPS OUT while a change waits for its snap point. When disengaged, the track passes through clean (still with the 730-sample latency, so nothing moves). | Engage |
| **SNAP** | Where Engage changes land: **1/16, 1/8, 1/4, 1/2** note or **1 Bar** (bars follow the host meter). Default 1/16. | Engage Quantize |
| **RETRIG** | Off (default): the pattern follows song position, as before. On: the phrase restarts from its beginning at the moment Engage takes effect. | Retrigger |
| **PLAY** | **Every** phrase (default), or only the **last of 2, 4 or 8** phrases. Phrases are counted from the start of the song (or from the engage point with Retrigger on). In the other phrases the track passes clean, the graph dims and shows WAITING. | Play Every |
| **LOOK BACK** | Off (default), **1 beat, 1 bar, 2 bars or 4 bars**: while the pattern is scratching, it reads audio from that much earlier, so a fill can replay a line from bars ago. Clean passages stay live. | Look Back |
| **MIX** | Blend of scratched and clean sound, 0–100%. Default 100%, the 0.10 sound. | Mix |
| **DEPTH** | Scales every stroke, 0–200%. At 0% the source stays in place (gate cuts still apply); 100% (default) is the drawn pattern; above 100% strokes travel further and can reach **4×** source speed (0.10 stopped at 2×). | Depth |
| **OUTPUT** | Output trim, −12 to +12 dB. Default 0 dB. | Output |
| **A–H** | Pattern slots. Click a slot to see and edit its pattern: drawing, Shapes, Randomize and preset loading then work on that slot. Right-click a slot to copy the selected slot or slot A into it, or clear it. Slot A is the main pattern; an empty slot plays clean. A lime dot marks a slot that holds a pattern, the outlined key is the one you are editing, and the glowing key is the one playing. | Pattern Slot |
| **CHAIN** | Off (default): the selected slot plays. **A–B, A–D, A–H**: slot A plays on the first phrase, B on the second, and so on, then repeats. | Slot Chain |
| **SUB SPLIT** | Where Keep Sub divides the clean lows from the scratched highs: **120, 160, 200 or 300 Hz**. Default 200 Hz, the 0.10 sound. All four keep the same 730-sample latency. | Keep Sub Split |
| **HISTORY** | How much incoming audio is kept for scratches to reach back into: **Auto** or **16, 32, 64, 128 s**. Saved with the project; not automatable. See **Memory** below. | (not a host control) |

**Automating slots.** Pattern Slot is an ordinary host parameter, so a Live automation lane or clip envelope can switch patterns per bar. Slots are not selected by MIDI notes: adding a MIDI input would have added dozens of extra MIDI-CC controls to the VST3 and changed how the AU is registered, so it was left out.

## Draw, roll, shape and erase

The pencil is a split button: click its main area to arm or disarm the selected drawing tool, or its arrow to choose a different tool. The selected tool lights up and the display gives a short instruction.

- **Free Draw** follows the path of your hand across the graph. Strokes are sampled by distance, so a slower mouse-event rate does not create larger steps. Light smoothing reduces tiny hand wobbles. Retracing an area redraws that area.
- **Line** stretches a straight line from the point where you press to the point where you release. **Curve** draws a rounded transition between those two points.
- **Rolls**: Roll 1/8, 1/16, 1/32, 1/16 Triplet, Accelerating, Reverse and Chopped. Drag across the graph: the slice where you press repeats at that rate until you release. Hovering previews where the roll lands.
- **Snap to grid** (bottom of the same menu) makes Line, Curve and Roll start and end on the 16-cell grid. Free Draw is never snapped.

Steep Free Draw, Line and Curve gestures are kept within the engine's **2×** forward/reverse speed; the displayed endpoint shows the applied result.

**Shapes** opens 15 miniature contours: the 10 scratch shapes from 0.10 plus Accelerating Roll, Triplet Roll, Reverse Roll, Tape Stop and Half-Time Drag. Selecting one replaces the current phrase, keeps length, timing and grid shift, and arms Free Draw. It is one undoable action.

Click **Eraser**, then click or drag across cells to restore original clean input there. Erasing does not mute the track.

The **Undo** and **Redo** icons restore whole mouse gestures, shape selections, slot copies and clears, and presets loaded into a slot, with up to **64 actions** in history. While a tool is armed, **Cmd+Z** undoes and **Cmd+Shift+Z** redoes. **Escape** cancels an unfinished gesture and disarms the tool; closing the editor also cancels an unfinished drag.

Length, Dotted, Triplet and the bottom shift arrows keep your edits. **Randomize** or choosing a preset starts a new pattern and clears drawing history. Save a drawing before replacing it if you want to keep it.

## Smooth scratch motion

Factory and generated patterns use curved time trajectories with acceleration and release (motion recipe version 2), so a stroke speeds up and slows down instead of holding one shifted pitch and turning sharply. The graph and audio evaluate the same trajectory. A straight Line still produces a constant speed on purpose; choose Curve or a Shape for rounded motion. Older saved presets and projects keep their original recipe version.

## One pattern, different durations

Turning the length knob changes how long it takes to play the pattern already displayed. It **never repeats, adds, removes or regenerates its gestures**: a pattern with three scratches still has three scratches from one bar to four bars. **Dotted** and **Triplet** apply the same scaling to the selected base length. Only **Randomize** composes a new pattern. Beat choices mean quarter-note beats; bar choices follow the host's time signature.

## Move the pattern and follow playback

The display has **16 equal grid cells** at every duration. Its bottom arrows move the entire effect one cell earlier or later, wrapping at the edges. The curve and moving playhead share the same delayed playback context as the audio, including the plug-in's latency and timed parameter changes. Closed scratch gates appear dimmer.

Patterns follow song position. Host loop wraps keep the rolling audio history. Starting playback or seeking clears what the plug-in may read (it never reads audio from before the jump). **LIVE INPUT** appears when an active gesture needs past audio that is not there yet, and that part fades to the original input.

## Keep Sub and audio quality

**On:** the original low layer stays at its original pitch and rhythm beneath the scratched upper layer. The split is set on the PERFORM page (default **200 Hz**). **Off:** the entire signal is scratched, including the bass.

The plug-in reports **730 samples of latency at 48 kHz**, about **15.2 ms**, at every split, so the host compensates. Dry audio, preserved sub and scratching share this timing.

## Memory

The rolling history stores incoming audio for scratches to reach back into. Per instance at 48 kHz stereo it uses about **12 MB for 16 s, 25 MB for 32 s, 49 MB for 64 s and 98 MB for 128 s** (four times that at 192 kHz), plus processing overhead.

- **Auto** (default for new instances) starts at 16 s, or more if the current settings need it, and steps up to 32, 64 or 128 s when a longer pattern, slower tempo, more Depth or Look Back needs more. The larger buffer is allocated away from the audio thread. It does not shrink while playing; it starts small again the next time the host prepares the plug-in.
- **16, 32, 64 or 128 s** fixes the size.
- Projects saved before 0.11 keep the fixed **128 s** they always had.

When the history grows or changes size, the older audio is not copied across (that would be too much work for the audio thread). For a moment, gestures that reach further back than the new history holds fade to the clean input and show **LIVE INPUT**, exactly as after a seek. Some extreme settings (for example dotted 16-bar patterns in 4/4 at 20 BPM) need more than 128 s; those portions also fade to dry. The plug-in never reads invalid audio. If memory for a large history cannot be found, it falls back to a shorter one rather than failing.

## Presets

**230 built-in presets in 17 folders**: the 75 original recipes (tuned in Bass, Drums and Vocals; exactly as 0.10 in Classic) and 80 new presets in Fills, Rolls, Stutters, Scratches, Tape Stops, Builds, Halftime, Vocal Chops, Riddim, Space Bass and Chains. **PRESETS.md** lists every one.

**Save** offers your preset folders in **~/Library/Application Support/BACK-SCRATCH/Presets/**, or **New folder or another place...**. **Load** lists the same folders plus **Browse files...**. The preset menu has a **User Presets** submenu. Existing preset files are never moved or changed.

## Window size

Right-click an empty part of the shell to open **SIZE: 90%, 100%, 125%, 150%**. Sizes that would not fit on the current screen are greyed out. The choice is saved with the project (not as an automatable parameter). The editor reopens at that size when it fits the screen, otherwise at the largest smaller choice that fits; if none fits, or no size was ever chosen, it opens at the usual fit-to-screen size.

## Save, automate and print

Saved presets and host projects keep your drawing, slots, generated identity and seed, recipe and variation, length, timing, grid shift, length lock, Keep Sub and the 0.11 settings. The same pattern repeats over fresh incoming audio.

Host controls from 0.10 are unchanged: **Pattern Motion Version**, **Edited Pattern**, **Generated Pattern**, **Pattern Seed**, **Phrase Length**, **Phrase Timing**, **Pattern Grid Shift**, **Phrase Length Lock**, **Keep Sub**, **Bypass**, **Scratch Pattern** and **Variation**, plus the retired Legacy entries. Version 0.11 appends **Engage, Engage Quantize, Retrigger, Play Every, Pattern Slot, Slot Chain, Mix, Output, Keep Sub Split, Depth** and **Look Back**. The new Mix has its own ID (`wetMix`); the retired 0.3 control stays as **Legacy Mix** and does nothing. Drawing points are saved with presets and projects but are not automation parameters.

Record controls with your DAW's automation recording. Use resampling, freeze or export to print audio.

## Fixed in 0.11

- Slowing the tempo inside a block (for example into a half-time drop) was mistaken for a seek and reset the engine every block, which could leave long silences. Real seeks are still detected.
- Host bypass now stays aligned with the reported 730-sample latency, and un-bypassing no longer resets the engine.
- Seeks, loop wraps, start/stop and project restore no longer zero the clean signal path, so there is no 15 ms hole; audio from before the jump is never read.
- Turning Bypass on fades instead of clicking. Writing a value that is already set no longer sends automation gestures, so Live keeps your Bypass and Grid Shift automation.
- Each instance has its own automation queue, with no per-thread allocation on the audio thread. The bundled JUCE source is unchanged.
- History allocation falls back to shorter lengths instead of failing; history-loss fades follow the moving readhead; gentle tempo ramps glide long delays instead of crossfading.

Steady-tempo, uninterrupted playback sounds exactly as it did in 0.10.

## Updating

Version 0.11 writes **preset/project schema 10**. Everything 0.10 saved still loads:

- **0.10 projects** (schema 9 and older) open with every setting intact, with all 0.11 controls at neutral values (Engage on, snap 1/16, Retrigger off, every phrase, slot A, no chain, Mix 100%, Output 0 dB, 200 Hz split, Depth 100%, no Look Back) and a fixed 128-second history. They render the same audio as 0.10.2; this was checked by saving projects with the actual 0.10.2 release build and reopening them in 0.11 (see **VALIDATION.md**).
- **0.10 preset files** load the same way. Engage and Output stay as you have them, as for any preset.
- **Parameter IDs** are unchanged; the new ones are appended. Existing automation keeps working.
- **Factory selection by number** (the Scratch Pattern control) still selects the same 75 recipes.
- **Arrows:** the upper arrows now walk the whole 230-preset bank in menu order. From a project made in 0.10 they start at the Classic entry of its recipe, then step through Classic in folder order (bass, then drums, then vocals) instead of the old 1–75 numbering.
- **History:** new instances use Auto; older projects keep 128 s.
- Schemas 4–8 still load with their original motion; see PRESETS.md for older migration details. Versions 0.1–0.3 used captured audio and wheel gestures; those states open bypassed.

The plug-in identity is unchanged (VST3 and the new AU share the bundle ID `com.yoiboi.backscratch`; AU type `aufx`, subtype `Bsc1`, manufacturer `Yoib`).

## Build and verification

Apple Silicon macOS 12+, VST3 and Audio Unit. Plug-ins are locally ad hoc signed. See **VALIDATION.md** for the automated checks and what was not verified (actual DAW sessions, listening, and AU validation with Apple's `auval`, which needs the AU installed). Plug-ins installed from the Vivid Fever Dreams Suite installer (1.5.0 and later) are signed with the developer's Developer ID and notarized by Apple.

Build with C++20, CMake and the included patched JUCE tree. Keep the source archive's `back-scratch` and `shared` directories beside each other: the shared folder supplies the VFD logo and Info interface. Run these commands from `back-scratch`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --target BackScratch_VST3 BackScratch_AU BackScratchTests BackScratchHostSmoke BackScratchCompatHost --parallel 2
build/BackScratchTests_artefacts/Release/BackScratchTests
build/BackScratchHostSmoke_artefacts/Release/BackScratchHostSmoke
```

The compatibility check runs in two steps with an older release's VST3 (read-only):

```sh
build/BackScratchCompatHost_artefacts/Release/BackScratchCompatHost --save  <0.10.2 BACK-SCRATCH.vst3> <scratch folder>
build/BackScratchCompatHost_artefacts/Release/BackScratchCompatHost --check build/BackScratch_artefacts/Release/VST3/BACK-SCRATCH.vst3 <scratch folder>
build/BackScratchCompatHost_artefacts/Release/BackScratchCompatHost --au build/BackScratch_artefacts/Release/AU/BACK-SCRATCH.component build/BackScratch_artefacts/Release/VST3/BACK-SCRATCH.vst3
```

`Installer/build-installer.sh` accepts the absolute VST3 path, a fresh output directory, and version `0.11.0`.

The physical **INFO** key in the lower shell opens the Vivid Fever Dreams logo and credits, executive producer profiles, music website, and Dream Sequence donation link. Close it with **X** or **Escape**.
