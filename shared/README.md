# Vivid Fever Dreams info controls

The six plugins use `VfdInfo.h` and the supplied `VfdLogo.svg` from this directory. Each plugin adds the SVG to its existing JUCE binary data target and passes its Chakra Petch typeface to the shared components. The original logo's path geometry is preserved; only its display color is adapted to the plugin palette.

`InfoButton` paints the dedicated molded INFO key. `InfoOverlay` owns the logo, credits, external links and close control. Opening or closing this component never changes processor state. The page intercepts pointer input, contains keyboard focus while open, closes on Escape, and returns focus to the invoking key.

The plugin projects must remain siblings of this directory when building from source. The shared component is independent of their audio processors and parameter models. Plugin-specific chassis placement stays in each editor.

The approved directions are implemented by preserving the existing hardware header stripes, keeping the logo off the physical key, and showing the complete logo as the hero of the info page.

External destinations:

- Executive producer @cxy: https://x.com/cxy
- Executive producer @clt: https://x.com/clt
- Music: https://www.vividfeverdreams.xyz
- Donations: https://fundraising.fracturedatlas.org/dream-sequence

The integration checks validate the four destinations without opening browsers or submitting anything to the external sites.

## Suite updates

`VfdUpdate.h` keeps the suite version (`VfdSuiteVersion.h`) next to GitHub's latest release of `vividfeverdreams/vfd-plugin-suite`, for the foot of the INFO page and a small light on the INFO key.

- **When it checks:** once per session, after an editor has been on screen for 1.5 s, or when CHECK FOR UPDATES is clicked. The answer is cached for a day in `~/Library/Application Support/Vivid Fever Dreams/Updates/latest-release.json` (Windows: `%LOCALAPPDATA%\Vivid Fever Dreams\Updates`) and shared by every VFD plug-in. Host scans and offscreen editors never check. Drafts and pre-releases are ignored, so a release can be staged as a pre-release first.
- **INSTALL:** downloads this platform's installer from the release (Mac: the asset ending `-Install.zip`; Windows: the one ending `-Setup.exe`) into the same folder, checks its SHA-256 against the release's `SHA256SUMS.txt`, and opens it. On the Mac the zip is expanded with `ditto` and the installer must pass `codesign` with the suite's Developer ID team (LZXM44W38L) and `spctl` (notarization) before it is opened. Only files attached to this repository's releases are used. The installers themselves refuse to replace plug-ins while a DAW that loads them is running.
- **Threads:** one background thread per plug-in binary, owned by its open editors through `juce::SharedResourcePointer`. Closing the last editor cancels a check or download. Nothing touches audio or parameters.
- **Releasing:** bump `VFD_SUITE_VERSION` (and `SUITE_VERSION` in the Windows workflow) for every suite release; `release-mac.sh` refuses a mismatch. Publish the Mac zip, the Windows Setup.exe and `SHA256SUMS.txt` on the GitHub release.
- **Text:** `python3 shared/tools/check_text_literals.py` (from the suite root) finds non-ASCII text in plain string literals, which JUCE shows as junk ("â€"); wrap it in `juce::String::fromUTF8`.
- **Tests:** `shared/Tests` (see its CMakeLists.txt). `--live` checks GitHub and downloads and verifies the current release's installer without opening it.
