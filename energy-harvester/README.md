# ENERGY HARVESTER 0.3.1

A magnetic tape-start effect with selectable effects, spin-up output trim and a plasma-pink hardware interface and an independent six-second circuit-light loop.

Apple Silicon, macOS 12+, VST3; matching mono or stereo tracks. No MIDI or network access is required. This build does not include Audio Unit, Intel, Windows, preset browser.

## Use

Insert ENERGY HARVESTER on a track. Start with HARVEST OFF, then switch or automate it ON to launch one spin-up. The sound rises from approximately quarter speed toward the original pitch, opening in volume and brightness. The five touchscreen-style buttons choose which secondary effects join the spin-up: PANNING, CHORUS, PHASER, FLANGER and WASHOUT. They can work alone or together; Phaser starts selected.

SPIN-UP sets 80 ms to 8 seconds; default 800 ms. A launch keeps the duration it started with. Changing the slider during a spin updates the next launch. The small TIME/SYNC switch beneath SPIN-UP selects free time or host-synced timing. SYNC offers 1/8, 1/4, 1/2, 1 bar, 2 bars, and 4 bars. Bar lengths respect the host time signature. Tempo and division are captured at launch; subsequent tempo changes apply to the next launch. With no valid host tempo, SYNC uses 120 BPM and displays FALLBACK. Each mode remembers its own setting.

FX AMOUNT sets the intensity of every selected effect; default 45%. Zero keeps the basic pitch/volume/brightness start. FX amount and effect-enable changes are smoothed and affect the current launch. Panning moves stereo tracks with constant-power balance and leaves mono unchanged. Chorus uses three modulated voices; Phaser uses six sweeping stages; Flanger uses short modulated delay and negative feedback; Washout adds a diffuse, damped stereo hall. Washout fades with the spin-up and returns to exact clean audio at the endpoint.

OUTPUT adjusts only the harvested spin-up from −18 to +18 dB, in 0.1 dB steps; default 0 dB. The ivory dial sits directly in the footer beside INFO, inside a molded circular pink socket. Its concentric resin rim, recessed collar and lighting match the existing hardware. Click its borderless readout to type a dB value, hold Shift when starting a drag for finer adjustment, or double-click to restore 0 dB. Changes are smoothed over 10 ms. The trim fades out with the spin-up or cancellation, so OFF, AT SPEED and host bypass keep the original track level. OUTPUT is host-automatable and saved with the project. At 0 dB the audio is identical to the preceding build.

Holding ON completes the spin and returns to exact clean audio at AT SPEED. The lights keep harvesting while ON, including with silent input. Switching OFF cancels smoothly and rearms. To repeat the sound, automate OFF followed by ON.

Saving or reopening a project with HARVEST ON restores clean AT SPEED. Starting playback, seeking or wrapping a loop with an initial ON value does not manufacture a launch. Place an OFF point before an ON point when you want the effect at the beginning of a bounce or loop. The DAW's host bypass is separate from HARVEST; unbypassing a held ON state returns clean audio.

The value readouts sit directly on the pink fascia, with no box outlines or backgrounds, including while typing. Click a value to edit it.

Double-click a slider to restore its default. Click its value to enter a number or sync division; time accepts `800 ms`, `0.8 s` or a bare millisecond value. Tab reaches controls; Space activates the focused button. Right-click the shell for Reduced Motion and 90%, 100% or 125% sizes. INFO opens the shared Vivid Fever Dreams credits; Escape closes it.

## Install

Quit audio applications, unzip the installer download and open **Install ENERGY HARVESTER.app**. Click **Install / Update**, then reopen your DAW, enable VST3 scanning if needed and rescan.

The installer writes only `~/Library/Audio/Plug-Ins/VST3/ENERGY HARVESTER.vst3`. It checks the payload and plugin identities, stages a replacement, and can roll back a failed update. A conflicting system-folder copy is reported instead of silently duplicating it. Building and testing this release did not install into the real user or system plugin folders.

This local preview is ad hoc signed and is not Apple notarized. macOS may require local approval in Privacy & Security. For manual installation, use the separately supplied ENERGY HARVESTER.vst3 instead of installing both copies.

## Previews and verification

The release includes native screenshots, ten nine-second WAV comparisons. Each WAV contains the same synthetic phrase: 0–3 seconds dry, 3–6 seconds basic tape start, 6–9 seconds selected FX at 75%. Files 4–8 isolate the individual effects; 09 combines all five; 10 repeats the combined example with +6 dB applied only in the final selected-FX section. Each effect launches 350 ms into its phrase and uses a 1600 ms spin-up. These are level-preserving renders without final loudness normalization.

Read VALIDATION.md for measured results and remaining audition work. Ableton session testing, real-source listening and Retina/many-editor profiling remain to be done; this is a local preview build for trying the sound and interface, not a claim of finished commercial validation.

## Build from source

The source distribution keeps `energy-harvester` and `shared` as sibling folders. It includes JUCE 9.0.2 with the ENERGY queue patch already applied, the artwork, fonts and their licenses. Use CMake 3.22+ and Apple command-line developer tools.

```sh
cmake -S energy-harvester -B energy-harvester/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build energy-harvester/build -j 6
ctest --test-dir energy-harvester/build --output-on-failure
```

The VST3 appears in `energy-harvester/build/EnergyHarvester_artefacts/Release/VST3/`. To generate screenshots, frames and demos, run `EnergyHostTests` with an absolute output folder. To test the shipping bundle, run `EnergyVstAutomation` with its absolute VST3 path; optional additional VST3 paths keep sibling modules loaded during the ENERGY tests.

Installer packaging requires the generated icon in Assets/AppIcon.icns (included). Run `Installer/build-installer.sh` with the absolute VST3 path, a fresh absolute output directory, and `0.3.1`. It builds the Swift app, signs it locally, makes the payload manifest, inspects it and runs isolated transaction tests. It does not install the plugin during packaging.

Version 0.1 projects migrate automatically, preserving the original parameter IDs, manual timing and intensity. The updated Phaser becomes the selected effect. Plugin bundle and VST3 class identities remain unchanged. Version 0.2 projects retain their settings and receive 0 dB output trim. New state uses schema 3; malformed or future states are rejected without partial restoration.

See BUILD-REVISION-0.2.md for this update, JUCE-PIN.md for the wrapper patch and hashes, ARTWORK.md for art provenance, and the included design folder for the revision-3 implementation plan. JUCE's license is included at external/JUCE/LICENSE.md; font terms are in Assets/ChakraPetch-OFL.txt.
