# YOI-BOI 0.2.0

YOI-BOI is a monophonic talking-bass synthesizer for Apple Silicon macOS, built as a **VST3 instrument**, an **Audio Unit instrument** and a **standalone app**. Play a MIDI phrase, press **NEW YOI**, and shape the result with Mouth, Size, Bite, Dirt, Length and Sub.

## What's new in 0.2.0

0.2.0 adds a new sound engine (**engine 4**) and a much bigger library. New instances, **New Yoi** and the new factory sounds use it. Projects and presets from 0.1.x keep the engine they were made with and sound exactly as before (see *Older projects*).

- **MOD page** (new tab between SHAPE and LIBRARY), in five columns:
  - **Talk:** keep one vowel sweep per note, or make the mouth talk on the beat. **Synced Talk** runs from 1/16 to one bar (straight, triplet or dotted) and is locked to the song position, so it stays on the grid wherever playback starts. **Mod Wheel** and **Aftertouch** let you scrub the vowel motion by hand. **BAR = METER** (off by default) makes "1 Bar" follow the host's time signature, for example three beats in 3/4.
  - **Pitch:** **Bend** range from ±1 to ±24 semitones. **Glide** can be Always (as before) or **Legato** (only between connected notes). **Drop / Rise** starts each note up to two octaves away and settles onto it over **Drop Time**, for dives and risers. The pitch wheel follows instantly even with long glide.
  - **Tone:** a **Shaper** choice for Dirt: Classic (the original), Diode, Fold or Crush. It works inside the 4× oversampled path. **Squash** is a three-band upward/downward compressor on the talking body, in the spirit of OTT. **Width** spreads the body in stereo above 180 Hz while the sub stays mono. **SUB RESET** restarts the sub's cycle on every note for an identical punch. The final ceiling is now a smooth oversampled limit instead of a hard clip.
  - **Attack:** an **Onset** glide into the first vowel from a **W** ("wah") or **Y** ("yoi") mouth, and an optional **Consonant** burst (**T**, **K** or **CH**) with its own level.
  - **Feel:** **Velocity** can soften Mouth, Bite and Dirt on quieter notes. Amplitude **Attack** and **Release** are adjustable; 3 ms and 75 ms are the original values.
- **More vowels:** besides EE, EH, AH, OH and OO, engine 4 adds **IH, UH, AE and ER**. Pick them on the SHAPE page or drag the points in the motion graph.
- **Separate Sub output:** YOI-BOI has an optional second stereo output called **Sub**, off by default. When your host enables it, the dedicated sub plays there instead of in the main output, ready for its own processing or sidechain. With it off, nothing changes.
- **75 new and reworked factory sounds** in folders: Yoi, Riddim, Talk, Growl, Squelch, Wobble, Reese, Sub, Space and Leads/FX. The twelve original sounds are still in **Classic**, unchanged; reworked engine-4 versions with a "II" suffix are in **Yoi**. See [FACTORY-PRESETS.md](FACTORY-PRESETS.md).
- **Library folders:** the Library lists factory and saved sounds by folder and searches names, folders and tags. **SAVE PRESET** lets you choose a folder or create a new one. Your existing preset files are never moved or rewritten.
- **Bounce variations** (LIBRARY → BOUNCES): renders 8 or 16 New Yoi or Mutate variations of the current sound to WAV files. Each file plays a one-bar phrase at the host tempo, starting on the beat. Files go to `~/Music/YOI-BOI Bounces/<date>/`, or to a folder you choose with FOLDER. Click a tile to load that variation (Undo goes back); drag a tile into your DAW to use the audio. Locks and exclusions are respected. Rendering runs in the background and never on the audio thread.
- **A / B compare** on the PLAY page: switch between two versions of a sound while you tweak; COPY duplicates the slot you are on.
- **PREVIEW** now plays a one-bar phrase at the host tempo (120 BPM when there is none), then stops.
- **Device size:** ZOOM offers 90%, 100%, 125% and 150%. Sizes that don't fit your screen are disabled. The size is saved with the project, and the window reopens at it, or at the largest size that fits.

## Make and keep a sound

1. Press **PREVIEW**, or play MIDI notes around **36–48**. External MIDI takes priority over Preview. The synth plays one voice with glide, sustain pedal, velocity and pitch bend.
2. Choose a family (Rubber, Metal, Throat or Squeak) and press **NEW YOI**. **MUTATE** makes a smaller change within the current family.
3. Right-click a parameter and choose **Exclude from randomization**. New Yoi and Mutate then leave it alone, while manual changes and automation still work. Repeat to include it again. The six padlocks lock whole groups; Length and Sub start locked.
4. Use **SHAPE** to set the Start, Body and End vowels, the Body point and the Curve. Use **MOD** for talk motion, pitch, tone, attack and feel. **LOCK** protects the Mouth and Length groups together. Undo/Redo covers your sound edits for the current session.
5. To keep a sound, open **LIBRARY → SAVE PRESET**, enter a name, choose a folder and press **Save**. New Yoi and Mutate never save files by themselves.

Saving your project keeps the current sound, timing and routing, locks, Favorites, generation family, device size and the random sequence. User presets live in `~/Library/Application Support/YOI-BOI/Presets/`, one subfolder per folder, and every open YOI-BOI sees them. Saving a name that already exists in the same folder updates that file.

## Sub and timing

**SUB ROUTE** offers three choices:

- **Off:** mutes the dedicated sub oscillator. Turn on **BODY LOW-CUT** as well to thin out the vowel body's low end.
- **Direct:** a clean sine an octave below the played note. It skips the formants and distortion and joins the output afterwards.
- **Driven:** sends the sub through its own saturation, set by Dirt.

On engine-4 sounds, a Direct or Driven sub plays on the **Sub** output instead when your host enables it.

**BODY LOW-CUT** is a 110 Hz high-pass on the vowel body only.

With **SYNC** on, Length offers 1/16, 1/8, 1/4, 1/2 and 1 Bar, each straight, triplet or dotted. Use the feel selector, or right-click Length and choose **Choose musical length**. With Sync off, Length runs from 80 to 2000 ms. **One Shot** and **Loop** refer to the vowel motion; how long a note sounds is set by MIDI. Without a host tempo, YOI-BOI uses 120 BPM.

## Window controls

**ZOOM** sets the device size and holds the engine options. **CLOSE** closes a floating plug-in window that YOI-BOI can recognise; it is disabled when that window can't be identified safely. The instrument keeps running.

The standalone app has a borderless window: drag its logo area to move it. **ZOOM → Audio / MIDI settings** opens the device and MIDI-input settings, and the app remembers them. Closing the window quits the app. The standalone never saves sounds automatically.

## Install

1. Quit Live and other audio hosts. Unzip the installer download and open **Install YOI-BOI.app**.
2. Tick **VST3**, **AU** or both, then click **Install / Update**. It installs or replaces `~/Library/Audio/Plug-Ins/VST3/YOI-BOI.vst3` and `~/Library/Audio/Plug-Ins/Components/YOI-BOI.component`. No administrator password is needed.
3. Reopen your host. Existing YOI-BOI tracks use the updated plug-in. For a first installation in Live, enable **Use VST3 Plug-In System Folders** in **Settings → Plug-Ins** and Rescan if needed.

The installer checks the payload and refuses downgrades. It won't update while known audio hosts are running. It replaces the bundle atomically and restores the previous copy if final verification fails. Your presets and projects stay where they are. If a computer-wide copy exists in `/Library/Audio/Plug-Ins/VST3/`, the installer reports it instead of creating a duplicate.

**Audio Unit** (new in 0.2.0) is for Logic Pro, GarageBand and other AU hosts. Quit Logic Pro, GarageBand and MainStage before installing it. The installer then asks macOS to refresh its Audio Unit list; restart Logic afterwards.

Everything is built for **Apple Silicon**. The instrument needs macOS 11 or later and the installer macOS 12 or later. All products carry an **ad-hoc development signature**, not Developer ID notarization, so macOS may ask for a one-time **Open Anyway** approval. Intel and Windows builds are not supplied. Plug-ins installed from the Vivid Fever Dreams Suite installer (1.5.0 and later) are signed with the developer's Developer ID and notarized by Apple.

## Older projects and saved sounds

The VST3 identity and every original parameter ID and position are unchanged; 0.2.0 only adds parameters after them. A saved sound records its engine:

| Engine | Made with | In 0.2.0 |
| --- | --- | --- |
| 1 | 0.1.0 / 0.1.1 | identical sound, zero latency |
| 2 | 0.1.2 | identical sound, zero latency |
| 3 | 0.1.3 – 0.1.6 | identical sound, 24 samples latency |
| 4 | 0.2.0 | the new engine, 24 samples latency |

A sound on engines 1–3 shows **ORIGINAL SOUND** under the logo. The MOD page and the extra vowels are disabled for it, and all of its controls work as they did. **ZOOM → Use latest sound (changes tone)** moves the current sound to engine 4 without changing its settings. Its vowels carry over, and every new control starts neutral. Undo goes back.

Apart from the note-ending fixes below, engines 1–3 play the same audio from the same MIDI, automation and saved settings as in 0.1.6 (see `VALIDATION.md`). These changes apply whatever the engine:

- **All Notes Off (CC123):** sounds from 0.1.3–0.1.6 now finish their normal release instead of stopping dead, so hosts that send it when the transport stops no longer click. Sounds from 0.1.0–0.1.2 keep the hard stop. **All Sound Off (CC120)** still silences at once.
- **Bypass** releases held notes, so a note-off sent while bypassed can't leave a stuck note.
- **Changing engine while a note plays** (Undo, Use latest sound, loading a preset or project) fades the old engine out over 5 ms instead of cutting it.
- **Library:** the new folders come first and the twelve original factory sounds are in **Classic**. Their sound, names and Favorites are unchanged. New Yoi and the new sounds use engine 4.
- **PREVIEW** plays a one-bar phrase instead of one short note.
- **New, unused until you choose them:** the Sub output (off by default), A / B compare, bounces and preset folders.

## Build and check the source

Requirements: Apple's C++ tools/SDK, **CMake 3.22+**, and **JUCE 9.0.2** (`external/JUCE`, or `-DJUCE_SOURCE_DIR=/path/to/JUCE`). Keep `../shared/VfdInfo.h` and `../shared/VfdLogo.svg` next to this folder.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DYOIBOI_BUILD_TESTS=ON
cmake --build build --config Release --target YoiBoi_VST3 YoiBoi_AU YoiBoi_Standalone YoiBoiTests --parallel 4
```

Products are in `build/YoiBoi_artefacts/Release/`: `VST3/YOI-BOI.vst3`, `AU/YOI-BOI.component` and `Standalone/YOI-BOI.app`. The build signs them and installs nothing.

Checks, all run against private temporary folders (the test binary sets its own HOME):

```sh
B=build/YoiBoiTests_artefacts/Release/YoiBoiTests
$B "$(pwd)/build/YoiBoi_artefacts/Release/VST3/YOI-BOI.vst3"           # unit, processor, editor tests + VST3 scan/render
$B --check-au "$(pwd)/build/YoiBoi_artefacts/Release/AU/YOI-BOI.component" # AU hosted in-process, not installed
$B --check-compat "/absolute/old/YOI-BOI.vst3" "$(pwd)/build/YoiBoi_artefacts/Release/VST3/YOI-BOI.vst3"
$B --preset-stats "$(pwd)/build/demos"   # peak, loudness and DC for every factory sound
$B --render-demos "$(pwd)/build/demos"   # the same, plus WAV files and a combined tour
$B --render-ui "$(pwd)/build/ui/yoi.png" # screenshots of every page and size
```

The preserved engines in `Source/Dsp/Legacy/` and `Source/Dsp/YoiVoice.cpp` (engine 3) are frozen. A change to rendered sound needs a new engine identifier. See `VALIDATION.md` for results and limits.

## Current scope and licenses

YOI-BOI is one voice: no polyphony, MPE or audio input. Randomization clicks are not timeline automation; save a sound you like, or bounce variations to audio. A complete interactive DAW session test (save/reopen, freeze/flatten, export, long sessions) is separate from the automated checks.

JUCE is a separate dependency under its own license; review its terms before distribution. The interface embeds Chakra Petch under the SIL Open Font License 1.1 (`Assets/ChakraPetch-OFL.txt`). The shell artwork is documented in `Assets/ARTWORK.md`.
