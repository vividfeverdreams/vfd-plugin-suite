# THROW-UP

A reverb thrower for mid-basses and left-field bass music by **YOI-BOI**. The giant **THROW** knob adds a separate, fully wet reverb layer over the original dry bass. The orange enclosure, INFO page and 50 room presets are included.

Version 0.1.4 raises the reverb return by **18 dB** compared with 0.1.3, giving small and medium rooms a useful, clearly stronger throw. The extra gain applies only to reverb, after its tone, grit and ducking controls. Dry stays at full level and the preset automation fix is retained.

Version **0.1.4** · **VST3** · **Apple Silicon** · **macOS 12 or later**

Click **INFO** to see the Vivid Fever Dreams logo and credits, executive producer profiles, music website, and Dream Sequence donation link. Close the page with **X** or **Escape**.

## Quick start

1. Quit your DAW and open **Install THROW-UP.app**. Click **Install / Update**.
2. Reopen your DAW and rescan VST3 plug-ins. In Ableton Live, enable VST3 in Settings → Plug-Ins and rescan.
3. Load **YOI-BOI → THROW-UP** on your mid-bass track and choose a room preset.
4. Automate **THROW** in your DAW: start at 0%, rise for the sound you want to throw, then shape the return to dry.

Your DAW automation sets the timing of each throw. **THROW is the added reverb level:**

- **0%:** original dry bass only.
- **50%:** original dry bass plus half-level wet reverb.
- **100%:** original dry bass plus the full wet reverb return.

Full THROW is intended for pronounced effects; lower values give lighter ambience. The dry path stays at unity before Output gain. The added path contains only reverb, with no second direct copy of the dry signal. Both paths are summed inside the plug-in; no extra DAW track or output routing is needed. Output adjusts their combined level. The louder parallel return can take the combined signal above 0 dBFS with strong throws or hot input. Use Output to set the final level; the plugin does not secretly limit or turn down the dry path.

THROW uses a **4 ms full-range ramp** to avoid clicks and follows the host's audio block processing. Returning THROW to 0% mutes the added reverb after that short ramp. The reverb continues processing internally, so raising it again can reveal an existing tail. Use THROW-UP directly on the bass track; 100% includes the original dry sound.

Choose rooms with the plug-in's preset menu or previous/next buttons while THROW automation plays. These controls change only reverb parameters. The THROW parameter ID and range remain compatible with existing automation lanes. Version 0.1.3 changed the earlier crossfade to dry plus reverb; 0.1.4 makes that reverb return 18 dB louder. Saved automation lanes still work, but nonzero throws will sound stronger. Keep an older release for exact recall of its previous sound.

The processor reports **zero latency**. Pre-delay is an intentional delay within the wet sound and does not delay the dry path.

The top display holds preset selection and six compact tone controls. The central physical plate holds THROW, Size, Decay, Pre-delay and Diffusion. Output, Bypass and input/output meters sit below. All controls remain visible on one page.

## Controls

| Control | What it changes |
| --- | --- |
| **THROW** | Added wet reverb level, 0–100%; dry remains present. Exposed to your DAW for automation. |
| **Size** | Room dimensions and spacing of the reverb reflections. |
| **Decay** | Reverb decay, 0.15–18 seconds. |
| **Pre-delay** | Delay before the reverb, 0–250 ms. |
| **Low cut** | Filters low frequencies from the wet path, 80–1,000 Hz. Default: 180 Hz. |
| **High cut** | Darkens the wet path, 1,000–18,000 Hz. |
| **Diffusion** | Reflection density and smoothness. |
| **Motion** | Modulation in the reverb tail. |
| **Width** | Wet stereo spread, 0–150%. |
| **Grit** | Saturation on the wet signal. |
| **Duck** | Reduces the reverb while the incoming bass plays. |
| **Output** | Overall output level. |
| **Bypass** | Passes the input through without the effect. |

The low and high cuts shape the reverb while leaving the dry sound intact. Start with a higher low cut for dense arrangements, and increase Duck when a long tail obscures the next bass hit.

Drag a knob vertically to adjust it; hold **Shift** for fine adjustment. Double-click a knob to reset it, or double-click its displayed value to type a number. Resize the interface from **80% to 150%** to suit your screen.

## Factory presets

**50 presets**, in five groups of ten:

- **Small** — tight reflections and short rooms.
- **Medium** — wider rooms and rhythmic space.
- **Large** — halls and long, spacious throws.
- **Huge** — enormous, sustained environments.
- **Creative** — unusual textures, movement and heavily colored spaces.

Changing a factory preset preserves the current **THROW**, **Output** and **Bypass** settings. Preset selection does not send a THROW value change or edit gesture, and does not send a whole-plugin program-change notification. Your THROW automation can continue while you audition rooms.

Use THROW-UP's own preset display to see the selected room. A DAW's generic Program selector can retain its previous displayed index after an internal preset change; the selected room is still saved and recalled correctly.

See [PRESETS.md](PRESETS.md) for the complete bank and descriptions. Your DAW project saves every parameter and the selected factory preset. Use your DAW's preset-saving facilities to store custom sounds; this version's browser contains the factory bank.

## Audio demonstration

The included **THROW-UP-dry-then-throws.wav** plays a synthesized mid-bass phrase dry for the first **5 seconds**, then repeats the same phrase with automated throws for the next **5 seconds**.

The additional **THROW-UP-wet-level-comparison.wav** demonstrates the level correction with identical source gain and no normalization. Each room plays dry, then 0.1.3, then 0.1.4. Small (Orange Crate) begins at 0:00; Medium (Back Room) begins at 0:18.75. The corrected sections begin at 0:12.50 and 0:31.25. A timing guide and separate small/medium files are included.

## Installation details

The installer writes only the THROW-UP bundle to the standard per-user VST3 folder:

```text
~/Library/Audio/Plug-Ins/VST3/THROW-UP.vst3
```

No administrator password is needed. The installer preserves other plug-ins, DAW projects and saved presets. Close audio hosts before installing or updating. If a second THROW-UP copy already exists in the system VST3 folder, the installer reports it and stops to prevent duplicate entries.

The installer and VST3 are signed locally with an ad hoc identity; they are **not Apple-notarized**. If macOS blocks an installer you trust, use System Settings → Privacy & Security → **Open Anyway**. The current release is a native Apple Silicon build; Intel-only hosts and hosts running under Rosetta are not supported by this binary.

## Build from source

You need Xcode command-line tools and CMake. Open the source package's `throw-up` folder before running the commands below. It includes **JUCE 9.0.2** in `external/JUCE`, which the build uses automatically, and uses the sibling `shared` folder for the INFO artwork and controls. Keep those folders together. See `CMakeLists.txt` for the plug-in and verification targets.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --config Release --target ThrowUp_VST3 --parallel
```

To run the DSP and musical-level regressions:

```sh
cmake --build build --target ThrowUpDspTests ThrowUpWetLevelAudit --parallel
ctest --test-dir build --output-on-failure
```

The level audit checks all 50 presets on three mid-bass sources and writes `build/wet-level-audit.csv`. `ThrowUpWetLevelDemo` is an optional comparison renderer; compile it against the old engine to save a reference with `--baseline reference.bin`, then against the corrected engine and run `--compare reference.bin output-directory`. It preserves absolute levels.

To use another local JUCE checkout, add `-DJUCE_SOURCE_DIR=/absolute/path/to/JUCE` to the configure command.

After the VST3 and `Assets/AppIcon.icns` are available, build the native installer into a fresh output directory:

```sh
bash Installer/build-installer.sh \
  /absolute/path/to/THROW-UP.vst3 \
  /absolute/path/to/new-release-output \
  0.1.4
```

This packages and verifies the installer without installing anything in your user plug-in folder. See [Installer/README.md](Installer/README.md) for integrity checks and installer testing.
