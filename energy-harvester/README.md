# ENERGY HARVESTER 0.4.3

A tape-gesture effect for bass music: tape starts, tape stops, turntable brakes, stop-and-restart stutters and rewinds. Each gesture can land exactly on the beat, and five screen effects can join it. Plasma-pink hardware interface, with a light show that follows the tape.

Apple Silicon, macOS 12+, VST3 and Audio Unit (and VST3 on Windows, from the suite). Works on matching mono or stereo tracks. No MIDI is used. The only network access is the INFO page's suite update check (GitHub, at most once a day while the window is open; see the suite README).

**New in 0.4.3:** a die on the preset strip loads a random preset. (0.4.2: tooltips stay fully inside the device and wrap between words, and INFO shows the suite version and offers updates.)

## Use

Insert ENERGY HARVESTER on a track and switch HARVEST on (or automate it) to launch one gesture. Switching it off rearms it. To repeat a gesture, automate OFF followed by ON.

**Display pages.** The display has three touch tabs:
- **FX** shows the five screen effects.
- **MODE** shows the five gestures.
- **SETUP** flips up a settings screen over the HARVEST socket; DONE closes it.

**Modes.**
- **START** rises from below speed to the original pitch. This is the original effect.
- **STOP** powers the tape down to silence. It stays silent while HARVEST is on and returns to the dry signal when you switch it off.
- **BRAKE** is a turntable brake: constant deceleration to a stop, brighter and louder than STOP.
- **STOP>GO** stops over the first half of the length and restarts over the second.
- **REWIND** brakes through standstill into a backspin up to three times normal speed, then snaps back to the live signal at the end.

The stop, brake and rewind gestures use a varispeed deck that plays back what the track has just played (up to 16 seconds).

**Length.** SPIN-UP (LENGTH for the other modes) sets 80 ms to 8 seconds; the default is 800 ms. The TIME/SYNC switch selects free time or host-synced lengths: 1/8, 1/4, 1/2, 1 bar, 2 bars or 4 bars. These follow the host tempo and time signature at the moment of the launch.

A launch keeps the length, mode, engine, curve, depth and landing it started with; changes apply to the next launch. FX, filter, Sub Keep, Mix and OUTPUT changes apply immediately. In VST3 hosts, automation of HARVEST and the other controls is sample-accurate. In Audio Unit hosts it takes effect at the start of the next audio block (a few milliseconds).

**SETUP screen.**
- **Engine:** CLASSIC is the original two-head pitch climb. TRUE TAPE is continuous varispeed for the START mode, with no grain flutter. True Tape replays the recorded audio at the changing speed, so it falls behind the track; at the end it jumps back to the live signal.
- **Landing:**
  - CLEAN returns exactly to the dry signal at the end.
  - TAIL lets the Washout hall ring on over the dry signal after the landing.
  - HOLD stays effected at speed until HARVEST is switched off.
  - For STOP and BRAKE the deck always rests in silence until OFF.
- **Curve:** LOG moves the speed early, S-CURVE is the original, and SLAM LATE holds back and hits at the end.
- **Depth:** where a start begins, −1 to −48 semitones. 24 is the original quarter speed.
- **Filter:** LP 12, LP 24 or BP, with Resonance and Start frequency. Resonance 0 is the original soft brightness sweep.
- **Sub Keep:** OFF, 60, 90, 120 or 150 Hz. Below the chosen split, the output stays the dry input, so the sub keeps going while the rest of the track spins or stops. Default OFF, because the falling and rising sub is part of the classic sound.
- **Grid:** OFF, 1/4, BAR or 2 BARS. With a playing transport, the gesture ends exactly on the next beat, bar line or two-bar line after HARVEST turns on, whatever TIME or SYNC says. With the transport stopped, TIME or SYNC applies.
- **Mix:** a parallel blend of the effected signal.
- **Sound:** see below.

**FX and OUTPUT.** FX AMOUNT sets the intensity of every selected screen effect (PANNING, CHORUS, PHASER, FLANGER, WASHOUT). OUTPUT trims only the effected voice, from −18 to +18 dB; the dry signal keeps its level.

**Presets.** The strip on the title plate shows the current preset:
- ◀ and ▶ step through all presets; clicking the name opens the browser.
- The die beside ▶ loads a random preset, factory or user, never the one already loaded. It loads like any other pick, so the host can undo it.
- There are 56 factory presets in 10 folders: Tape Starts, Tape Stops, Brakes, Rewinds, Power-Ups, Risers, Drops, Washouts, Lo-Fi and Grid-Locked. See PRESETS.md.
- SAVE stores the current settings as a user preset in `~/Library/Application Support/ENERGY HARVESTER/Presets/<Folder>/`. Pick an existing folder or type a new one.
- Saving never overwrites an existing file; a taken name gets a number.
- Loading a preset never touches HARVEST, and the host can undo it.

**Holding, saving and recall.**
- Saving or reopening a project with HARVEST on restores clean audio without launching. A STOP that was resting in silence comes back playing clean until you switch HARVEST off and on again.
- Starting playback, seeking or wrapping a loop never manufactures a launch. Put an OFF point before an ON point when you want the gesture at the start of a bounce or loop.
- Host bypass is separate from HARVEST. Bypass, transport changes and recalls during a gesture fade over 5 ms instead of clicking.

**Other controls.** Right-click the shell for Reduced Motion and the size menu: 90 %, 100 %, 125 % or 150 %. Sizes that don't fit your display are greyed out. INFO opens the shared Vivid Fever Dreams credits; Escape closes it. Tab reaches the controls and Space activates the focused one. Every control has a tooltip.

## Projects from earlier versions: Latest Sound

Projects saved with ENERGY HARVESTER 0.1 to 0.3.2 open with every setting intact and **render exactly as they did in 0.3.2**. The new controls start at settings that reproduce the old sound, and **Latest Sound** is off.

When you want the 0.4.0 refinements, press **USE LATEST SOUND** in SETUP. The refinements:
- the effects recede as the tape reaches speed;
- Washout fades to nothing exactly at the landing;
- chorus, phaser, flanger and Washout leave the sub alone;
- panning only attenuates;
- the landing is a single clean tap.

The upgrade is an ordinary parameter change, so the host can undo it, and you can switch back. New instances start with Latest Sound on.

Click and recall fixes (bypass and transport fades, recall-race and parameter-flush fixes) apply to every project. They never change the sound of a normal launch.

## Install

The existing ENERGY HARVESTER installer (and the suite installer) installs only the VST3. To use the Audio Unit, copy `ENERGY HARVESTER.component` from the build folder to `~/Library/Audio/Plug-Ins/Components/` yourself and let your host rescan.

This release was built and tested without installing anything into the real plug-in folders. Local builds are ad hoc signed and not Apple notarized, so macOS may ask for approval in Privacy & Security. Plug-ins installed from the Vivid Fever Dreams Suite installer (1.5.0 and later) are signed with the developer's Developer ID and notarized by Apple.

## Build from source

The source distribution keeps `energy-harvester` and `shared` as sibling folders. It includes JUCE 9.0.2 with the ENERGY queue patch already applied, the artwork, fonts and their licences. Use CMake 3.22+ and Apple command-line developer tools.

```sh
cmake -S energy-harvester -B energy-harvester/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build energy-harvester/build -j 6
ctest --test-dir energy-harvester/build --output-on-failure
```

The VST3 and AU appear in `energy-harvester/build/EnergyHarvester_artefacts/Release/VST3/` and `.../AU/`. The test programs:

| Program | What it does |
|---|---|
| `EnergyDspTests` | Engine and effect checks, including the 0.4.0 feature suite. |
| `EnergyHostTests` | State, migration, presets, grid and editor checks. Given a folder, it also writes screenshots, animation frames and demo WAVs. `--render-presets <folder>` writes one WAV per factory preset. `--spin-frames <folder> [mode]` renders the editor at 30 fps through one gesture (mode 0–4). |
| `EnergyVstAutomation` | Drives the built VST3 through its real wrapper. `--compat <0.3.2 bundle> <new bundle>` checks that a state saved by the shipped 0.3.2 plug-in renders the same. |
| `EnergyAuValidation` | Registers the built AU in-process (nothing installed) and runs auval-style checks. |

Tests use a temporary preset folder and never touch your real presets.

State schema 4 reads schemas 1 to 3. Parameter IDs are append-only: the original eleven keep their IDs and order, and twelve are appended. Plug-in bundle, VST3 class and AU identities are unchanged from 0.3.2 (AU is new: aufx Enh1 Yoib).

See CHANGELOG.md for the full list of changes, VALIDATION.md for measured results, PRESETS.md for the presets, JUCE-PIN.md for the wrapper patch and ARTWORK.md for art provenance. JUCE's licence is in external/JUCE/LICENSE.md; font terms are in Assets/ChakraPetch-OFL.txt.
