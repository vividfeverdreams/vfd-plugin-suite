# SHIFTY 0.5.0

SHIFTY is a formant effect for bass. It changes the "mouth" of a sound (its resonances: bigger and darker, or smaller and brighter, or a different vowel) while the notes stay where they are. Put it after a bass synth or on an audio track. It accepts mono or stereo audio and makes no sound on its own.

This build is a **VST3 and Audio Unit (AU) effect for Apple Silicon Macs running macOS 12 or later**.

## What's new in 0.5.0

- **Three engines.** **TALK** (the new default) is a talking formant filter with no latency. **MOUTH** keeps your bass's own resonances and resizes them, note by note. **CLASSIC** is the 0.4 engine with its bugs fixed, plus a low-latency option.
- **Your existing projects sound exactly as before.** Projects and presets made with SHIFTY 0.4.2 or older open on the **original 0.4.2 sound**, at the original latency. The VOICE page says **ORIGINAL SOUND PRESERVED**. Switching to the new sound is your choice (**USE LATEST SOUND**), and **UNDO** switches back.
- **A VOICE page** with a vowel morph (OO, OH, AH, EH, EE), three voice sets (HUMAN, GROWL, WOW), and two macro knobs: **CHARACTER** and **TALK**.
- **More movement.** The MOTION page now has three tabs: **LFO** (as before, and on TALK it can move the vowel too), **ENV** (louder playing moves the sound) and **YOI** (each new note sweeps from one vowel to another).
- **114 factory presets in folders.** 66 new sounds, new versions of the 24 originals, and the 24 originals themselves, unchanged, in a **Classic** folder.
- **User preset folders.** Save into a folder of your own; browse by folder.
- **Live displays.** The SOUND page draws what the engine is doing: TALK's resonance curve, MOUTH's tracked pitch, CLASSIC's detected and moved envelope.
- **Window size.** Right-click an empty part of the device for **Size 90% / 100% / 125% / 150%**. The size is saved with your project.
- **Smoother DRIVE.** Drive is now anti-aliased (in TALK, MOUTH and CLASSIC; the original sound keeps its old drive).
- **Audio Unit.** SHIFTY now also comes as an AU, for Logic Pro and GarageBand.

## Install and open

Install with the release's installer, then reopen your DAW and rescan plug-ins if SHIFTY does not appear. In Ableton Live, enable **Use VST3 Plug-In System Folders** under **Settings → Plug-Ins**, then click **Rescan**. Logic Pro scans Audio Units by itself when it starts.

The plug-in is signed locally rather than notarized by Apple. A downloaded copy may need macOS's **Open Anyway** approval. See [installer details](Installer/README.md).

## The three engines

Choose the engine on the **SOUND** page. Changing engine is one UNDO step. Each engine reports its own latency to your DAW, which compensates automatically. The engine is deliberately not automatable and RANDOM never changes it, so the latency never jumps during playback.

| Engine | What it does | Best on | Latency at 48 kHz |
| --- | --- | --- | --- |
| **TALK** (default) | A bank of five vowel resonances that you morph between. SHIFT scales the whole vowel up or down. Works on any source, including plain saws. | Talking basses, growls, wobbles, "yoi" sounds | **0 ms** |
| **MOUTH** | Finds the pitch of each note and moves the sound's own resonances without moving the note. CHARACTER adds the chosen vowel's mouth shape on top. When there is no clear pitch (noise, chords), it falls back to a fixed grain size. | Bigger or smaller versions of a bass you already like | **21 ms** (1,024 samples) |
| **CLASSIC** | The 0.4 engine: measures the sound's spectral envelope and moves it. Bug-fixed. **LOW LATENCY** halves its delay. | Textured, already-resonant sources | **43 ms** (2,048 samples); **21 ms** with LOW LATENCY |

A plain sawtooth has no resonances of its own, so CLASSIC and MOUTH change it only a little. That is expected: use TALK, DRIVE, VOWEL COLOR or CHARACTER to give it a mouth.

## The controls

| Control | TALK | MOUTH | CLASSIC |
| --- | --- | --- | --- |
| **SHIFT** (−12 to +12 st) | Scales the vowel. Down = bigger, darker. Up = smaller, brighter. | Resizes the sound's own resonances. | Moves the detected envelope. |
| **FOCUS** | Width of the resonances: broad to narrow. | Grain length: smoother to tighter. | Detail of the detected envelope. |
| **BITE** | Resonance and drive into the vowel bank. | Pitch-synchronous buzz. | Contrast of the shift. |
| **DRIVE** | Anti-aliased saturation before the engine. | Same. | Same. |
| **MIX** | Dry/wet. 100% gives the clearest effect. | Same. | Same. |
| **SUB SAFE** + frequency | Keeps everything below the frequency (60–200 Hz) untouched, through a phase-matched crossover. | Same. | Restores the original lows inside the engine, as in 0.4. |
| **OUTPUT** | Final level, −24 to +6 dB. | Same. | Same. |
| **BYPASS** | Passes the input, keeping the reported latency. | Same. | Same. |

On the **VOICE** page:

- **VOWEL** (TALK) and **SHAPE** (MOUTH): a continuous morph through OO, OH, AH, EH and EE. On TALK, a small dot shows where Motion, ENV or YOI has moved the vowel right now.
- **VOICE**: **HUMAN** vowels, **GROWL** (darker, wider, more beastly) or **WOW** (exaggerated, for wobbles).
- **CHARACTER** macro: more resonance, bite and drive. On MOUTH it adds the chosen vowel's mouth shape.
- **TALK** macro: more movement from ENV, YOI and the LFO.
- **CLASSIC** shows **LOW LATENCY** and **VOWEL COLOR** (the optional 0.4 vowel contour).

Drag a knob to change it, double-click to reset, double-click a value to type it. **UNDO** and **REDO** are at the bottom of the device.

## Movement: LFO, ENV and YOI

Open **MOTION** and pick a tab.

- **LFO.** Turn on the bottom **MOTION** switch. Choose Sine, Triangle, Ramp Up or Ramp Down, the depth in semitones, and either a synced rate (1/16 to 1 Bar) or a free rate (0.05–20 Hz). On TALK, **TARGET** chooses what the LFO moves: SHIFT, the VOWEL, or BOTH. The graph shows the LFO's live position.
  - Synced Motion follows the host's bars and time signature while playing. **With the transport stopped it keeps running at the host tempo** (120 BPM without one); in 0.4.2 it froze.
- **ENV.** Louder playing opens the vowel (TALK) or raises SHIFT (MOUTH, CLASSIC). A negative amount does the opposite. ATTACK and RELEASE set how quickly it follows. The meters show the level and what it is moving.
- **YOI.** Each new note starts a sweep from the **FROM** vowel to the **TO** vowel over **TIME**, easing in. **SHIFT** adds a formant drop (or rise) at the start of the note that glides back. YOI listens for note starts, including a quieter note after a short gap. It does not need MIDI.

ENV and YOI need the latest engines. They do nothing on the original 0.4.2 sound.

## Your existing projects

- A project or preset saved with SHIFTY 0.4.2 or older has no engine stored, so it opens on the **original 0.4.2 sound**. It renders the same audio as 0.4.2 (measured within 0.0000025, about −112 dB) at the same latency (2,048 samples at 48 kHz). Your automation keeps working: the 16 original parameters keep their IDs and order, and the 13 new ones are added after them.
- **VOICE** shows **ORIGINAL SOUND PRESERVED**, and the SOUND page shows **ORIGINAL SOUND**. Press **USE LATEST SOUND** to switch that instance to the fixed CLASSIC engine. All your settings stay as they are. It is one UNDO step.
- A few fixes apply even to the original sound, because they fix behaviour rather than the sound:
  - With the transport stopped, synced Motion keeps running at the host tempo instead of freezing.
  - The DAW's own bypass now passes delay-compensated audio (no timing jump), and no old audio is left when you switch bypass off.
  - UNDO no longer collects host automation moves; it holds your own edits, RANDOM and preset loads.
  - User presets are stored in `~/Library/Application Support/SHIFTY/Presets/`. Presets from the old folder (`~/Library/SHIFTY/Presets/`) are copied there once; the originals are left in place.
- What changes when you choose **USE LATEST SOUND** (CLASSIC):
  - Level compensation reacts per frame, which removes level bumps on note starts and SHIFT jumps. Dark, heavily lowered sounds can be up to about 1.5 dB louder than before.
  - Fast Motion keeps its depth (about 95% of the slow depth at 9.4 Hz, up from about 50%), is timed with the audio, and drops out far less.
  - The low-note warble on notes below G1 is reduced: from about 1.0 dB to about 0.3 dB.
  - "1 Bar" follows the host's time signature, and synced cycles restart on bar lines.
  - Drive is anti-aliased.

## Make quick variations

**RANDOM** creates a new variation from every unlocked parameter. Its ranges favour useful sounds: SHIFT stays at least 2 st from zero, MIX stays between 70% and 100%, and so on. **OUTPUT**, **BYPASS** and **VOWEL COLOR** start locked. Click a padlock beside a main knob, right-click any control, or open **LOCKS** to choose what RANDOM may change. RANDOM never changes the engine. UNDO brings back the previous sound.

## Presets

**PRESETS** lists factory and user sounds. Use **FACTORY / USER / FAV** and the folder menu to filter, and the search box to find a name. Double-click a row (or select it and press Return) to load it. Click the star column, or the star on SOUND, to mark a favourite.

| Folder | Sounds | What's in it |
| --- | --- | --- |
| **Talk** | 11 | Speaking, gargling, chattering basses (TALK). |
| **Yoi** | 10 | A vowel sweep on every note, from snappy to slow. |
| **Growl** | 9 | Dark, driven throats (TALK, MOUTH, CLASSIC). |
| **Wobble** | 11 | Synced vowel and formant wobbles, 1/16 to 1 Bar. |
| **Mouth** | 10 | Your bass's own resonances, resized (MOUTH). |
| **Squelch** | 11 | Envelope-driven, rubbery, acid-ish shapes. |
| **Subtle Mid** | 9 | Gentle colour for mixing. |
| **Vowel Morphs** | 10 | Slowly moving and stepping vowels. |
| **Space Bass** | 9 | Wide, hollow, moving sounds. |
| **Classic** | 24 | The 24 presets from SHIFTY 0.4, exactly as they were (original sound). |

New versions of all 24 originals, rebuilt on the new engines, sit in the matching folders under the same names.

**Save your own:** choose **SAVE AS**, type a name, and optionally a folder. Use **FOLDERS** to pick an existing folder, or type a new name to create one. Press Return. Presets are `.shifty` files in `~/Library/Application Support/SHIFTY/Presets/<Folder>/`. Saving with a name that already exists in that folder replaces that one preset; nothing else is ever changed. A saved preset remembers its engine. Presets from 0.4 load on the original sound, like projects.

## Window size

Right-click an empty part of the device to choose **Size 90%, 100%, 125% or 150%**. Sizes that do not fit your screen are greyed out. You can also drag the window corner. The size is saved with your project. SHIFTY opens at the saved size, or at the largest size that fits if the saved one does not.

## Latency

| Engine | 44.1 kHz | 48 kHz | 96 kHz |
| --- | --- | --- | --- |
| TALK | 0 | 0 | 0 |
| MOUTH | 941 samples (21 ms) | 1,024 (21 ms) | 2,048 (21 ms) |
| CLASSIC | 2,048 (46 ms) | 2,048 (43 ms) | 4,096 (43 ms) |
| CLASSIC LOW LATENCY | 1,024 (23 ms) | 1,024 (21 ms) | 2,048 (21 ms) |
| Original 0.4.2 sound | 2,048 (46 ms) | 2,048 (43 ms) | 4,096 (43 ms) |

The dry signal, MIX and BYPASS are delayed by the same amount, so blending never causes phasing. Changing engine changes the reported latency once, at the moment you change it.

## Validation status

Automated tests, the 0.4.2 compatibility test, pluginval (strictness 5) on the VST3, and an in-process Audio Unit check all pass. See [validation details](VALIDATION.md) and [how the engines work](FORMANT-DESIGN.md).

Not yet done: a full session in Ableton Live and Logic Pro (scan, save and reopen, automation, freeze, export), Apple's `auval` on an installed AU, and listening on your own material.

## Build from source

You need JUCE 9.0.2, CMake, the Xcode command line tools and macOS 12 or later. Keep `VfdInfo.h` and `VfdLogo.svg` in the sibling `../shared` folder.

```sh
cmake -S . -B build -DJUCE_SOURCE_DIR=/absolute/path/to/JUCE -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --target Shifty_VST3 Shifty_AU --parallel 4
```

The plug-ins are `build/Shifty_artefacts/Release/VST3/SHIFTY.vst3` and `build/Shifty_artefacts/Release/AU/SHIFTY.component`. The AU identity is type `aufx`, subtype `Shf1`, manufacturer `Yoib`, bundle ID `com.yoiboi.shifty`. The build does not install anything. Pass version `0.5.0` to the installer build script.

Test targets: `ShiftyDspTests`, `ShiftyImpactTests` and `ShiftyEditorSnapshot`:

| Command | What it checks |
| --- | --- |
| `ShiftyEditorSnapshot --feature-tests` | Parameters, presets, undo, Motion, bypass, engines, ENV/YOI, folders, size. |
| `ShiftyEditorSnapshot --compat <0.4.2 SHIFTY.vst3> <new SHIFTY.vst3>` | Old projects render as before. |
| `ShiftyEditorSnapshot --preset-audit` | Every factory preset on three test sounds. |
| `ShiftyEditorSnapshot --host-latency <SHIFTY.vst3>` | Latency per engine and the parameter list, through the VST3. |
| `ShiftyEditorSnapshot --au <SHIFTY.component>` | The AU from the build folder, without installing it. |
| `ShiftyEditorSnapshot --gallery <folder>` | Renders every page and state as PNGs. |
