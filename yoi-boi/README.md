# YOI-BOI 0.1.5

YOI-BOI is a monophonic talking-bass synthesizer for Apple Silicon macOS, built as a **VST3 instrument** and **standalone app**. The installer updates the VST3 instrument. Play a MIDI phrase, press **NEW YOI**, and shape the result with Mouth, Size, Bite, Dirt, Length and Sub.

Version 0.1.5 adds a physical **INFO** key in a new lower shell band. It opens the exact Vivid Fever Dreams logo, development credit, executive producers, music and donation links. Close the page with its close button, Escape, or by clicking outside it. The editor is now 1070 × 724 at 100%; existing musical controls and the three-stripe header mark keep their original positions. Audio processing, parameter identities, presets and historical sound engines are unchanged from 0.1.4.

Version 0.1.4 introduced the **Install / Update app** and **built-in historical sound engines**. The installer replaces the same YOI-BOI plug-in in place. Projects and presets saved by versions 0.1.0–0.1.3 automatically select their original engine; you do not need separate old plug-in files. See `VALIDATION.md` for the exact compatibility checks and their limits.

The current engine retains the clean Direct sub route, sub Off, body low-cut, individual randomization exclusions, triplet/dotted timing, and twelve authored factory sounds introduced in 0.1.3. The translucent lavender device, in-shell zoom/close controls, and corrected die remain unchanged.

## Make and keep a sound

1. Press **PREVIEW**, or play MIDI notes around **36–48**. External MIDI takes priority over Preview. The synth uses one voice with glide, sustain-pedal support, velocity, and ±2-semitone pitch bend.
2. Choose Rubber, Metal, Throat or Squeak and press **NEW YOI**. **MUTATE** makes a smaller change within the current family. The revised generator favors clear open-to-bright vowel sweeps and restrained drive.
3. Right-click a parameter and select **Exclude from randomization**. Its setting remains unchanged by New Yoi and Mutate, while manual adjustment and host automation still work. Repeat the action to include it again. The six padlocks retain their group behavior; Length and Sub begin locked. Including one parameter inside a locked group keeps the other members protected.
4. Use **SHAPE** to choose Start, Body and End vowels, position the Body point, and adjust Curve. **LOCK** protects Mouth and Length groups together. Undo/Redo remembers sound edits during the current instance.
5. To keep a reusable sound, open **LIBRARY → SAVE PRESET**, enter a name, and confirm **Save**. New Yoi and Mutate do not add library entries or write files. There is no automatically saved Recent collection. Closing/reopening a session does not retain the Undo stack.

Saving the **Live Set still preserves the current sound**, timing/routing, locks, Favorites and randomizer sequence. That is normal session recall, separate from creating a preset. Factory and manually saved presets are searchable in the Library. Save a generated sound before favoriting it if you want to find it later.

User preset files live at `~/Library/Application Support/YOI-BOI/Presets/` and are shared across instances. A preset stores the complete sound, Sub route, body low-cut, free/synced Length and feel, Sync, Loop, Glide and Output. Saving the same user preset name updates that file. Failed writes are reported in the interface. Existing saved presets remain available.

## Sub and timing

**SUB ROUTE** offers three choices:

- **Off:** mutes the dedicated sub oscillator. Enable **LOW CUT** as well to reduce bass frequencies in the vowel/body signal.
- **Direct:** a clean sine one octave below the played note bypasses the internal formants and distortion, then mixes into the main output. It does not create a separate auxiliary output in Ableton. Output level and the final protective ceiling still apply.
- **Driven:** sends the dedicated sub through its saturation stage, controlled by Dirt, before mixing it with the body.

**LOW CUT** applies a 110 Hz high-pass to the vowel/body signal, independently of the sub route. This keeps the body clear of the dedicated sub, or makes room for your own separate bass instrument when Sub is Off.

With **SYNC** enabled, Length has **15 musical choices**: 1/16, 1/8, 1/4, 1/2 and 1 Bar, each available as **straight, triplet or dotted**. Use the feel selector or right-click Length → **Choose musical length**. A triplet lasts two-thirds of the straight value; dotted lasts one-and-a-half times. With Sync off, Length uses **80–2000 ms**. One Shot and Loop refer to vowel motion; note duration is controlled by MIDI. Sync follows host tempo but starts from each triggered note, rather than locking to the song position. Without host tempo it uses 120 BPM.

## Factory sounds

The bank contains twelve original recipes, led by **Tape Bounce**, a Tape B-inspired direction with a rounded opening and bright vowel reply. It is not an official Tape B preset, an endorsement, or an exact reconstruction of a recording. See [FACTORY-PRESETS.md](FACTORY-PRESETS.md) for each sound's contour, timing and use. All factory patches use Direct sub and the body low-cut; aggressive Dirt remains available as a deliberate choice.

The release's audio demonstrations are rendered through the actual processor at 140 BPM. Use them to compare the bank before installing; they do not imply a completed test inside Live.

## Window controls

The editor removes the previous dark internal margin and places **ZOOM** and **CLOSE** within the handheld shell. Zoom offers 100%, 125% and 150%.

Ableton owns its floating plug-in window and native title bar. This build leaves that host frame under Ableton's control. On macOS, the in-shell Close control only closes a recognized floating plug-in window named YOI-BOI; it is disabled when the host window cannot be identified safely. It closes the editor while the instrument continues running.

The standalone app has a borderless device window. Drag its logo area to move it. **ZOOM → Audio / MIDI settings** opens the audio device and MIDI input controls; the app remembers those device choices. Closing the standalone window quits that app. The standalone does not automatically save sounds or sound variations on exit.

## Install in Ableton Live

1. Quit Live and other audio hosts. Unzip the installer download and open **Install YOI-BOI.app**.
2. Click **Install / Update**. It installs or replaces `~/Library/Audio/Plug-Ins/VST3/YOI-BOI.vst3`. No administrator password or manual file juggling is needed for this standard user-folder installation.
3. Reopen Live. Existing YOI-BOI tracks use the updated plug-in. For a first installation, enable **Use VST3 Plug-In System Folders** under **Settings/Preferences → Plug-Ins**, then Rescan if needed. Find **YOI-BOI** in the Plug-Ins browser and add it to a MIDI track.

Use the same procedure for future updates. The installer checks the payload, refuses downgrades and updates while known audio hosts are running, replaces the bundle atomically, and restores the previous copy if final verification fails. After a successful update it removes the temporary old copy. Your presets and projects are left in place.

If you previously placed YOI-BOI in the computer-wide `/Library/Audio/Plug-Ins/VST3/` folder, the installer reports that duplicate instead of silently creating two copies. Move that copy into the user folder once before updating. Custom plug-in locations are not searched. The installer updates the VST3; it does not manage a separately copied standalone app.

The installer requires **Apple Silicon and macOS 12 or later**; the instrument supports macOS 11 or later. Both have an **ad-hoc development signature**, not Developer ID notarization. macOS may require a one-time **Open Anyway** approval for a downloaded installer. Intel macOS, Windows and AU builds are not supplied.

## Older projects and saved sounds

The VST3 identity and original parameter IDs/order remain fixed. Saved state chooses one of three engines contained inside the same plug-in: original 0.1.0/0.1.1, original 0.1.2, or current 0.1.3–0.1.5. The saved engine also determines reported audio latency: zero samples for the original engines and 24 samples for the current one. Saving a project or preset again records that engine explicitly.

An older sound shows **ORIGINAL SOUND PRESERVED** in the header. Adjustments and Mutate retain its engine. The newer sub-routing and body low-cut controls remain disabled for those sounds; turning the Sub knob to zero still mutes the dedicated sub. **ZOOM → Use latest sound (changes tone)** deliberately upgrades the current sound and can be undone. New Yoi and current factory presets use the current engine.

This preserves the version used when a sound was saved. A sound already changed and saved with 0.1.3 is recalled as a 0.1.3 sound; the update cannot reconstruct an earlier version of that project's state. Keep normal project backups, but separate historical plug-in binaries are not required for the supported versions.

## Build and check the source

Requirements: Apple's C++ development tools/SDK, Git, **CMake 3.22+**, and **JUCE 9.0.2** at `external/JUCE`. Run from the source folder. Skip the clone when the pinned dependency is already present.

An existing JUCE checkout can instead be selected with `-DJUCE_SOURCE_DIR=/absolute/path/to/JUCE`. Keep the shared `VfdInfo.h` and `VfdLogo.svg` files in the sibling `../shared` folder.

```sh
git clone --depth 1 --branch 9.0.2 https://github.com/juce-framework/JUCE.git external/JUCE
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DYOIBOI_BUILD_TESTS=ON
cmake --build build --config Release --target YoiBoi_VST3 YoiBoi_Standalone YoiBoiTests --parallel 4
```

Products are `build/YoiBoi_artefacts/Release/VST3/YOI-BOI.vst3` and `build/YoiBoi_artefacts/Release/Standalone/YOI-BOI.app`. The build signs the finished macOS bundles and does not install them.

Run the patch, processor and editor checks plus a real VST3 scan/load/render:

```sh
./build/YoiBoiTests_artefacts/Release/YoiBoiTests "$(pwd)/build/YoiBoi_artefacts/Release/VST3/YOI-BOI.vst3"
```

Render the factory bank through the processor, including the combined tour:

```sh
mkdir -p build/demos
./build/YoiBoiTests_artefacts/Release/YoiBoiTests --render-demos "$(pwd)/build/demos"
```

The separate DSP stress test is `Source/Dsp/tests/YoiVoiceOfflineTest.cpp`. Tests cover seeded generation, individual/group locks, manual-save behavior, state and preset migration, host timing, MIDI handling, and editor interactions. The engine processes synthesis and nonlinear stages at 4× sample rate, with **24 samples of reported latency** for host compensation. Those checks do not establish subjective sound quality or complete Ableton compatibility. Consult the release's `VALIDATION.md` for actual results and remaining DAW checks.

Build the installer after validating the final VST3:

```sh
bash Installer/build-installer.sh "$(pwd)/build/YoiBoi_artefacts/Release/VST3/YOI-BOI.vst3" "$(pwd)/build/installer-release" 0.1.5
```

The build verifies the embedded payload and runs isolated installer tests without installing it. See `Installer/README.md`. To compare authentic historical VST3 state and rendered audio with the update:

```sh
./build/YoiBoiTests_artefacts/Release/YoiBoiTests --check-compat "/absolute/old/YOI-BOI.vst3" "$(pwd)/build/YoiBoi_artefacts/Release/VST3/YOI-BOI.vst3"
```

Historical engines in `Source/Dsp/Legacy/` are frozen. Future changes that alter rendered sound must introduce a new engine identifier and preserve existing engines and state migrations. Run comparisons against every previously shipped release before replacing its installed bundle. Objective-C++ symbol visibility and the VST3 export list prevent a loaded JUCE plug-in from exposing its private implementation to other modules.

## Current scope and licenses

There is one mono voice sent to the main mono/stereo output, no separate sub output, and no sample input, polyphony, MPE, stereo widening or editable amplitude envelope. Randomization clicks are not repeatable timeline automation events; save a selected sound or resample its performance. A complete Live save/reopen, automation, freeze/flatten, export and long-session test remains separate from the automated checks.

JUCE is a separate dependency under its own license terms; review `external/JUCE/LICENSE.md` and [JUCE's licensing information](https://juce.com/get-juce/) before distribution. The interface embeds Chakra Petch under the SIL Open Font License 1.1; its notice is `Assets/ChakraPetch-OFL.txt`. The shell artwork and generation provenance are documented in `Assets/ARTWORK.md`.
