# SHIFTY 0.4.1

SHIFTY is a formant-shifting audio effect for bass sounds, with a translucent green interface. It reshapes the sound's resonant character while retaining its underlying note frequencies. Place it after a bass instrument or on an audio track to explore darker or brighter resonances, new textures and rhythmic movement. Version 0.4.0 puts the incoming sound’s own formants first, with refined envelope tracking and optional vowel color that starts off. It accepts mono or stereo audio and does not generate sound on its own.

This build is a **VST3 effect for Apple Silicon Macs running macOS 12 or later**.

Version 0.4.1 adds a physical **INFO** key in the lower shell. It opens the Vivid Fever Dreams logo, development credit, executive producers, music and donation links. Close the page with its close button, Escape, or by clicking outside it. The editor is now 650 × 844 at its default size; all existing musical controls and the three-stripe header mark retain their layout. The 0.4.0 audio engine, parameters and preset identity are unchanged.

## Install and open

The release package includes **Install SHIFTY.app**. Quit Ableton Live and other audio hosts, open the installer, and click **Install / Update**. It installs `SHIFTY.vst3` in your user's `Library/Audio/Plug-Ins/VST3` folder and updates an existing SHIFTY installation in place. YOI-BOI is a separate plug-in with its own identity and bundle. User presets remain outside the installed bundle.

This package includes the corrected installer host detection: another SHIFTY installer is not treated as an audio app. The audio plug-in and installer are version **0.4.1**.

Reopen your DAW and rescan its plug-ins if SHIFTY does not appear. In Ableton Live, enable **Use VST3 Plug-In System Folders** under **Settings/Preferences → Plug-Ins**, then click **Rescan**. Add SHIFTY after your instrument on a MIDI track, or directly to an audio track. Play audio through it to hear the effect.

The installer and plug-in use a local development signature rather than Apple notarization. A downloaded copy may require macOS's **Open Anyway** approval. See [installer details](Installer/README.md).

## Shape the sound

Start with **Rubber Throat** in **PRESETS** and play a bass phrase. Keep **SUB SAFE** enabled, turn **SHIFT**, then adjust the character controls.

| Control | What it does |
| --- | --- |
| **SHIFT** | Moves the formant character from −12 to +12 semitones. Negative values tend toward a larger, darker character; positive values tend toward a smaller, brighter character. This does not transpose the played notes. |
| **FOCUS** | Sets the detail of the incoming spectral envelope being shifted: broad at low settings, more defined at high settings. |
| **BITE** | Exaggerates the contrast of the source-derived formant shift. |
| **DRIVE** | Adds saturation before formant processing. |
| **MIX** | Blends the original and processed audio, from 0% to 100% processed. |
| **SUB SAFE** | Preserves the original low-frequency content, with a gradual transition into the processed sound above the chosen 60–200 Hz boundary. The default is 120 Hz. Output gain still applies. |
| **OUTPUT** | Adjusts the final level from −24 to +6 dB. |
| **VOWEL** | Optional added vowel contour. Off by default; the source formant shift works independently. |
| **BYPASS** | Passes the original audio at unity gain while retaining the processing delay. |

Drag knobs to adjust them; double-click a knob to reset it. Double-click a displayed value to type a precise setting. **UNDO** and **REDO** are available at the bottom of the interface.

## Add movement

Turn on the bottom **MOTION** switch and open the **MOTION** page. The sliding thumb and explicit **ON/OFF** label show whether movement is enabled. Choose **Sine**, **Triangle**, **Ramp Up** or **Ramp Down**, then set the depth in semitones. Motion moves around the main SHIFT value and stays within its −12 to +12 range.

With **SYNC ON**, the cycle follows the host's musical position during playback. Rates are 1/16, 1/8, 1/4, 1/2 and 1 Bar; synced motion pauses when the host stops. With **SYNC OFF**, set a free rate from 0.05 to 20 Hz.

## Make quick variations

Press **RANDOM** to generate a new combination of every unlocked parameter. All 16 parameters can participate, including the Motion controls, Sub Safe and its frequency, Output, Bypass, and optional Vowel Color. The random ranges favor useful mid-bass variations: Shift stays at least 2 semitones from zero, Mix between 70% and 100%, Focus and Bite between 35% and 100%, Drive between 0% and 65%, and Motion Depth between 0.5 and 8 semitones. Manual controls still offer their full ranges. **UNDO** restores the previous sound; **REDO** brings the variation back.

**OUTPUT**, **BYPASS**, and **VOWEL COLOR** start locked. Random keeps the level, active state and your choice about added vowel color steady. Each can be unlocked. When unlocked, Random chooses Output between −12 and 0 dB and can turn Bypass on or off.

Click a padlock beside a main knob to keep that parameter unchanged by Random. Right-click a control to access its randomization lock, or open **LOCKS** to manage all 16 parameters, **Lock all**, or **Unlock all**. A checked menu item means the parameter is locked. For variations with the same rhythm and low end, lock the Motion settings, Sub Safe and Sub Safe Frequency before pressing Random.

Locks affect randomization only; manual adjustments and sound preset loading still work normally. Each plug-in instance saves its locks with the DAW project. Loading a factory or user sound preset retains that instance's current locks.

## Presets and recall

**PRESETS** contains **24 factory settings**, twice the original count, with search, user presets and favorites. The 12 additions focus on mid-bass variation, including subtle **Midrange Nudge**, static character changes such as **Nasal Fold** and **Warm Snarl**, and moving sounds such as **Pocket Wobble**, **Liquid Twitch** and **Ratcheted Vowel**. Double-click a row, or select it and press Return, to load it. Click the star beside the current sound on **SOUND** to mark it as a favorite.

Factory presets now recall every sound parameter, including the free Motion rate, so loading the same preset gives the same settings regardless of the previous sound.

To keep your own settings, choose **SAVE AS**, enter a name and press Return. Saving an existing name replaces that user preset. Presets use `.shifty` files in `~/Library/Application Support/SHIFTY/Presets/`. Saving a DAW project also stores the current plug-in settings.

## Source formants first

SHIFTY follows the spectral envelope already present in the incoming sound and moves that envelope up or down while retaining the played note frequencies. The main effect does not impose a stock vowel shape. Version 0.4.0 refines the estimate around the source's spectral peaks so existing resonances and textures can move more clearly. FOCUS changes the envelope detail; BITE exaggerates the movement's contrast.

For a first try, leave **VOWEL OFF**, set Mix to 100%, Focus around 70% and Bite around 50%, then sweep Shift from −6 to +6 on a textured mid-bass. Motion can animate the same source-derived shift. A nearly pure sine or a smooth, featureless spectrum has less formant structure to move; the result will naturally be subtler. Drive adds harmonics, but a universal large change is not promised on every source.

**VOWEL** is a separate optional color on the SOUND page. When enabled, it adds the moving vowel contour from the previous direction. It starts off in new instances, all 24 factory presets and older presets that predate this switch. It is locked out of Random by default; unlock **Vowel Color** in LOCKS if you want it randomized.

Stereo-linked compensation helps control the shifted band's energy before the original protected sub is restored. It is not a peak limiter or a perceptual loudness guarantee. Drive still adds its own saturation and level character. Output and parameter changes remain smoothed.

The 0.4.0 update retained the original parameter values, automation IDs and randomization locks, appended Vowel Color, and changed the processing sound. Earlier states load Vowel Color off with its randomization lock enabled. Previous releases remain available for exact pre-0.4.0 audio recall. The 0.4.1 INFO update preserves the 0.4.0 sound and processing latency.

## Latency and validation status

SHIFTY reports its processing latency to the host for plug-in delay compensation: **2,048 samples at 48 kHz, approximately 43 ms**. It is approximately 46 ms at 44.1 kHz and 43 ms at 96 kHz. This delay also applies to the dry mix and internal bypass, and can be noticeable while playing live through the effect.

An actual Ableton Live session test has not yet been completed. Scan, project save/reopen, automation, freeze/flatten and export still need verification in Live; automated checks do not establish complete DAW compatibility or subjective sound quality.

See [validation details](VALIDATION.md) for the automated checks that were completed.

## Build from source

The source package requires JUCE 9.0.2, CMake, Xcode command line tools and macOS 12 or later. Set `JUCE_SOURCE_DIR` to an existing JUCE checkout when configuring:

Keep the shared `VfdInfo.h` and `VfdLogo.svg` files in the sibling `../shared` folder.

```sh
cmake -S . -B build -DJUCE_SOURCE_DIR=/absolute/path/to/JUCE -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --target Shifty_VST3 --parallel 4
```

The built plug-in is `build/Shifty_artefacts/Release/VST3/SHIFTY.vst3`. The included installer build script accepts this bundle, an empty output directory, the plug-in version `0.4.1` (which also sets the installer version).
