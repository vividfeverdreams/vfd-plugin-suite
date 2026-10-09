# Vivid Fever Dreams plug-in suite

Six audio plug-ins built with JUCE 9.0.2, one folder each:

| Folder | Plug-in | What it is |
| --- | --- | --- |
| `yoi-boi/` | YOI-BOI | Talking bass synth (instrument) |
| `throw-up/` | THROW-UP | Mid-bass reverb throws |
| `back-scratch/` | BACK-SCRATCH | Tempo-synced scratch patterns |
| `shifty/` | SHIFTY | Formant shifter for bass |
| `energy-harvester/` | ENERGY HARVESTER | Tape start, stop and brake |
| `stutter-step/` | STUTTER-STEP | Swing gate with slap delay |

`shared/` holds the INFO page and logo that every plug-in uses; the plug-in folders must stay next to it.

## Platforms

- **macOS 12+ (Apple Silicon):** VST3 and Audio Unit, built locally with CMake. Each plug-in's README has its build steps and tests. The Mac installer is `vfd-suite-installer/` (`build-installer.sh`). Releases are signed with the Developer ID and notarized by Apple (`sign-plugins.sh`, then `build-installer.sh` with `VFD_SIGN_IDENTITY` and `VFD_NOTARY_PROFILE`; see its README).
- **Windows 10/11 (x64):** VST3, built on GitHub Actions by `.github/workflows/windows.yml`. Every push to `main` builds each plug-in with MSVC, checks it with pluginval (strictness 5) and packages `vfd-suite-installer/windows/VividFeverDreamsSuite.iss` (Inno Setup). Download the installer from the run's artifacts. A plug-in is rebuilt only when something that goes into its VST3 changes (its folder except docs and tests, `shared/` or `patches/`).

## JUCE

JUCE is not in the repo. Put a JUCE 9.0.2 checkout at `<plug-in>/external/JUCE`. BACK-SCRATCH and ENERGY HARVESTER replace one JUCE file with their copy in `patches/juce-9.0.2/` (see the README there).
