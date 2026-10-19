# THROW-UP

A reverb thrower for mid-basses and left-field bass music by **YOI-BOI**. The giant **THROW** knob adds a separate, fully wet reverb layer over the original dry bass. You can also throw with the **THROW!** key, with MIDI notes, or let **Auto-catch** throw the end of every phrase for you.

Version **0.2.2** · **VST3** and **Audio Unit** · **Apple Silicon** · **macOS 12 or later**

## Your existing projects are safe

Projects saved with THROW-UP 0.1.5 or earlier open with every setting intact and **play exactly as before**. That is tested bit-for-bit against the released 0.1.5 plug-in (see VALIDATION.md). They keep:

- the **0.1.5 sound**: its low cut, Grit, Duck and reverb return level;
- their **room preset**: the original 50 rooms are still in the menu under **0.1.5 rooms (original sound)**. An old project recalls its room from that folder by its old number;
- all **automation**: the 13 original parameters keep their IDs (THROW is still VST3 parameter 108124). Every new control was added after them.

The display badge reads **0.1.5 SOUND** while a project uses the old sound. When you want the new sound, open the **MIX** page and press **USE LATEST SOUND**. You confirm it, it is a normal undoable edit in your DAW, and **USE 0.1.5 SOUND** switches straight back. New instances start with the latest sound.

## What's new in 0.2.0

- **Accurate low cut.** The reverb is 3 dB down exactly at the Low cut frequency. 0.1.5's filter was about 2.5 times higher than its label, so the latest sound keeps more mid-bass body in the tail.
- **Real Grit.** Grit now saturates the reverb at the level you hear it, with antialiasing. It no longer makes the throw louder: higher Grit sets a lower soft ceiling (+9 dBFS at the start of the knob, down to −6 dBFS at 100%).
- **THROW! key, MIDI throws and Auto-catch.** See *Throwing* below.
- **Tempo-synced pre-delay** from 1/64 to 1/2, straight, dotted or triplet (up to 2.5 seconds).
- **Freeze.** Holds the tail forever, exactly, and stops feeding it.
- **Shimmer.** Pitch inside the tail: an octave or a fourth down, a fifth or an octave up.
- **Swell.** A reverse-style rise synced to the beat or bar that cuts on the grid line, for the gap before a drop.
- **Sidechain ducking.** Duck can listen to an external sidechain (a kick or snare). There are also Duck release and threshold controls, and the latest Duck works in decibels.
- **Send mode.** The **SEND** key removes the dry signal for use on a return track.
- **Headroom tools.** **Return** trims only the reverb; the optional **SAFE** key soft-clips the whole output so it never passes 0 dBFS (off by default). The meters read up to +18 dBFS and have a 0 dB mark and CLIP lights.
- **140 factory programs.** 90 new and reworked rooms in 11 folders, plus the original 50. You can also save your own **user presets** in folders, and **A/B** compare.
- **Size menu.** Right-click the enclosure for 90%, 100%, 125% or 150%. Sizes that don't fit your screen are greyed out, and the size is saved with the project.
- **Audio Unit** format alongside VST3.
- **Smoother changes** (also listed in VALIDATION.md):
  - Size and preset changes no longer crackle or bend pitch.
  - Pre-delay changes no longer pitch-dive.
  - Bypass fades over 15 ms instead of clicking.
  - THROW automation follows your curve inside each audio block.
  - Double-click on Output resets it to 0 dB.

## Quick start

1. Quit your DAW and open the Vivid Fever Dreams installer. Choose THROW-UP (VST3 and/or Audio Unit) and click **Install / Update**.
2. Reopen your DAW and rescan plug-ins. In Ableton Live, enable VST3 in Settings → Plug-Ins and rescan.
3. Load **VIVID FEVER DREAMS → THROW-UP** on your mid-bass track and choose a room preset.
4. Throw: automate **THROW**, hold the **THROW!** key, play MIDI notes, or switch on Auto-catch.

**THROW is the added reverb level:**

- **0%:** the original dry bass only.
- **50%:** the dry bass plus half-level wet reverb.
- **100%:** the dry bass plus the full wet reverb return.

The dry path stays at unity before Output, and nothing secretly limits or turns down the dry. Full THROW is loud on purpose, so loud basses can take the combined output well above 0 dBFS. Use **Return** to turn the reverb down, **Output** for the whole signal, or **SAFE** to keep the output below 0 dBFS. THROW changes take at least 4 ms (no clicks) and follow your automation curve within each audio block. Returning THROW to 0% gives the exact dry signal. The reverb keeps running underneath, so raising THROW again can reveal an existing tail.

The plug-in reports **zero latency**. Pre-delay delays only the reverb.

## Throwing

The effective THROW is the larger of the THROW knob and the trigger. The knob's cream arc shows a triggered throw.

- **THROW! key** (bottom strip): hold it to open the throw to **Amount**. It rises over **Attack** and falls over **Release** when you let go. It is a host parameter, so you can record it as automation.
- **MIDI** (THROW page, MODE = MIDI): send MIDI notes to THROW-UP. Note-on opens the throw and velocity sets the level; note-off releases it. With **NOTE ROOMS** on, each note also picks a factory room: C1 (MIDI note 36) is the first room, C#1 the second, and so on through the 90 current rooms.
  - *Ableton Live:* set a MIDI track's "MIDI To" to the track holding THROW-UP and choose THROW-UP.
- **Auto-catch** (MODE = Auto-catch): when a phrase louder than **CATCH** stops for 60 ms, THROW-UP throws its tail. It rises to Amount over Attack, then falls over Release.
- **SYNC** sets the release to a note length (1/16 to 2 bars) at the host tempo.

*Audio Unit note:* THROW-UP's AU is a "MIDI-controlled effect", so Logic can send it MIDI. Insert it from the Audio FX slot under Audio Units → VIVID FEVER DREAMS.

## The display pages

The top display has four pages: **ROOM**, **THROW**, **SPACE** and **MIX**. The preset menu, **SAVE**, **A**/**B**, the dice and the arrows stay on top.

| Page | Controls |
| --- | --- |
| ROOM | **Low cut** (80–1,000 Hz), **High cut**, **Width** (0–150%), **Motion**, **Grit**, **Duck** |
| THROW | **Mode** (Manual, MIDI, Auto-catch), **Amount**, **Attack**, **Release**, **Sync**, **Catch** threshold, **NOTE ROOMS** |
| SPACE | **Pre sync** (ms or 1/64–1/2), **Feel** (straight, dotted, triplet), **Swell** (off, 1/4, 1/2, 1 bar, 2 bars), **Shimmer**, **Pitch** |
| MIX | **Return** trim (−24 to +6 dB, reverb only), **Duck source** (Dry or External), **Duck release**, **Duck threshold**, and the sound version |

The plate holds **THROW**, **Size**, **Decay** (0.15–18 s), **Pre-delay** (0–250 ms; it shows the note value when synced) and **Diffusion**. **Bypass**, the **Output** knob (0.2.1; its ring lights from the 0 dB tick) and the IN/OUT meters sit below. The bottom strip holds the **THROW!**, **FREEZE**, **INFO**, **SAFE** and **SEND** keys.

- **Knobs and bars:** drag to adjust; hold **Shift** for fine control. Double-click a control to reset it, and double-click its value to type one (choices accept their names, such as "1/8" or "Dotted").
- **Output:** drag the knob or its value. On the value, a single click types a number and a double-click resets it to 0 dB.
- **INFO** shows the suite version and lights up when a newer suite is out; INSTALL on the INFO page downloads and opens its installer (0.2.1).
- **Size:** right-click empty enclosure to choose 90%, 100%, 125% or 150%. Host resizing (80–150%) still works.

## Duck and sidechain

Duck lowers the reverb while its source plays, then lets it bloom into the gaps.

- **Latest sound:** Duck is a depth in decibels, up to 30 dB at 100%. It reaches full depth when the source is above **Duck threshold**.
- **0.1.5 sound:** Duck works as it did in 0.1.5.
- **Source:** set **Duck source** to **External** and route a kick or snare to THROW-UP's sidechain input. In Live, use the Sidechain section in the plug-in's device header. With **External** selected and nothing routed, nothing ducks.

## Presets

The menu has 11 folders of factory rooms, the **0.1.5 rooms (original sound)** folder, and **User**:

- **Factory folders:** Small, Medium, Large, Huge, Creative, Synced Throws, Freeze Pads, Shimmer, Swells, Sidechain Pumps and Send FX.
- **Loading a room** changes only the sound: size, decay, pre-delay and sync, filters, diffusion, motion, width, grit, duck, shimmer, swell, and the sound version. It never changes THROW, Output, Bypass, the trigger settings, Freeze, Safe, Send, Return or the duck source, so your THROW automation keeps running while you audition rooms.
- **Choosing the current room again** reverts your edits to it. The arrows step through the current folder bank (or the 0.1.5 rooms).
- **The dice** (left of the arrows, 0.2.2) loads a random preset: a different room from the factory folders or your user presets. On a 0.1.5 room it picks another 0.1.5 room, like the arrows. It loads exactly as the menu does, so THROW, Output and the rest stay where you set them.
- **SAVE** saves the current room as a user preset in a folder you choose or create, in `~/Library/Application Support/THROW-UP/Presets/<Folder>/`. THROW-UP never overwrites or edits an existing preset file; saving a name that already exists makes "Name 2".
- **A / B** keeps two versions of your settings (all except THROW, Output, Bypass and THROW!) so you can compare them. The first switch to B copies A. Both are saved with the project.

See [PRESETS.md](PRESETS.md) for every room.

## Formats and installation

- **VST3:** `~/Library/Audio/Plug-Ins/VST3/THROW-UP.vst3`, unchanged identity (class IDs, parameter IDs).
- **Audio Unit:** `~/Library/Audio/Plug-Ins/Components/THROW-UP.component`. It is type `aumf` (MIDI-controlled effect), subtype `Thr1`, manufacturer `Yoib`, bundle ID `com.yoiboi.throwup`.

The installer writes only THROW-UP bundles to your user plug-in folders and needs no administrator password. Both are signed locally (ad hoc), not Apple-notarized. If macOS blocks an installer you trust, use System Settings → Privacy & Security → **Open Anyway**. Native Apple Silicon only (no Intel or Rosetta). Plug-ins installed from the Vivid Fever Dreams Suite installer (1.5.0 and later) are signed with the developer's Developer ID and notarized by Apple.

## Version history

- **0.2.2:** the dice key loads a random preset.
- **0.2.1:** the Output knob and the INFO page update check.
- **0.2.0:** everything above.
- **0.1.5** (Vivid Fever Dreams Suite 1.2.2): plug-in browsers show **VIVID FEVER DREAMS** as the creator. The sound was the same as 0.1.4. Its README and VALIDATION still said 0.1.4; that mix-up is fixed here.
- **0.1.4:** the reverb return was raised by 18 dB so small and medium rooms throw clearly.
- **0.1.3:** THROW changed from a crossfade to dry plus reverb.

## Build from source

You need Xcode command-line tools and CMake. Keep the `throw-up`, `shared` and `back-scratch` folders together: the build uses **JUCE 9.0.2** from `../back-scratch/external/JUCE` (or `external/JUCE` if present) and the shared INFO page.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --config Release --parallel
ctest --test-dir build --output-on-failure
```

That builds the VST3, the AU, and every test (see VALIDATION.md for the full list and how to run them). Nothing is installed by the build. To use another JUCE checkout, add `-DJUCE_SOURCE_DIR=/absolute/path/to/JUCE`.
