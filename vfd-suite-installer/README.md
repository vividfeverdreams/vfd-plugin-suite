# Vivid Fever Dreams Suite 1.6.0

One native installer for YOI-BOI 0.2.2, THROW-UP 0.2.1, BACK-SCRATCH 0.11.1, SHIFTY 0.5.2, ENERGY HARVESTER 0.4.2 and STUTTER-STEP 0.4.0, each as a **VST3** and an **Audio Unit**. New in 1.6.0: every plug-in's INFO page checks the GitHub releases for a newer suite and can download, verify and open its installer; SHIFTY and THROW-UP have OUTPUT knobs; STUTTER-STEP saves grooves to a USER folder; YOI-BOI's MOD page drops the FEEL column; ENERGY HARVESTER's tooltips stay inside the window; BACK-SCRATCH's fills say when they play. The suite is also available for Windows (VST3) as a separate installer, built from the same source (see `windows/`). Source: https://github.com/vividfeverdreams/vfd-plugin-suite (GNU AGPLv3).

All six report **VIVID FEVER DREAMS** as their creator/vendor. Plug-in names, bundle identifiers, and VST3 component/controller identities remain compatible with earlier releases. The Audio Units register under these codes (type/subtype/manufacturer): YOI-BOI `aumu/YB01/YBoi`, THROW-UP `aumf/Thr1/Yoib` (a MIDI-controlled effect, so MIDI throws work in Logic), BACK-SCRATCH `aufx/Bsc1/Yoib`, SHIFTY `aufx/Shf1/Yoib`, ENERGY HARVESTER `aufx/Enh1/Yoib`, STUTTER-STEP `aufx/Stp1/Yoib`.

Requires **macOS 12 or later on Apple Silicon**. This package does not provide Intel or AAX builds. Since 1.5.0 the installer and every included plug-in are signed with the developer's Apple Developer ID and notarized by Apple, so the installer opens with a double-click like any downloaded Mac app. (Builds made without a Developer ID, see below, are ad hoc signed and need System Settings → Privacy & Security → Open Anyway once.)

## Install or update

1. Quit your audio applications. Logic Pro, GarageBand and MainStage only need to be quit when an Audio Unit is selected.
2. Open **Install Vivid Fever Dreams Suite.app**.
3. Each plug-in has a **VST3** and an **AU** checkbox, all ticked by default, with its status: *New*, *Installed 0.10.2 → 0.11.0*, *Up to date*, *Reinstall*, or *Newer … installed* (unticked, because the installer never downgrades). Untick anything you don’t want.
4. Click **Install / Update**. Items whose installed bytes already match are left untouched; everything else selected is installed as one transaction.
5. Follow the **Next steps** the installer shows for the audio apps on your Mac (for example Ableton Live: Settings → Plug-Ins → Rescan, Option-click for a full rescan; Logic Pro: reopen Logic, or Plug-in Manager → Reset & Rescan Selection). The same page shows a short **What’s new** for every plug-in that changed.

VST3 bundles go to `~/Library/Audio/Plug-Ins/VST3` and Audio Units to `~/Library/Audio/Plug-Ins/Components`. Presets and DAW projects are not edited. Administrative access is not required. The download quarantine flag is cleared from the installed copies, so a DAW does not show a Gatekeeper prompt for each plug-in.

After any Audio Unit is installed or removed, the installer asks macOS to refresh its Audio Unit list by ending the current user’s `AudioComponentRegistrar` agent with a normal SIGTERM (`killall -u <you> AudioComponentRegistrar`). This is the approach plug-in installers commonly use; Apple does not document it, and launchd restarts the agent on demand. Restart Logic Pro, GarageBand or MainStage afterwards.

The installer refuses downgrades, conflicting identities (VST3 class IDs, or AU codes), symbolic-link targets, running hosts, and duplicate copies. Running hosts are found by bundle identifier or name, plus any process that has an installed suite plug-in loaded. Duplicates are matched by bundle identifier, VST3 class ID or AU codes anywhere in the user or system VST3 and Components folders and their subfolders.

The log is `~/Library/Logs/Vivid Fever Dreams Suite/installer.log` (**View Log**). Each attempt is appended; the log rotates to `installer.log.1` past 256 KB.

## Uninstall

Tick the formats to remove and click **Uninstall…**, then confirm in the window. Every selected bundle is verified first (bundle identifier plus VST3 class IDs or AU codes); if any one cannot be verified, nothing moves. Verified bundles are moved to the **Trash**, so an uninstall can be undone by dragging them back.

## Repair

**Repair…** lists anything left behind by an interrupted installation:

- an interrupted suite transaction (`.vfd-suite-transaction-*`) — **Restore Previous Versions** puts back the backups it holds, after checking each against its recorded SHA-256 hashes, and moves any plug-in the transaction had added to the Trash; it is only offered when every step verifies;
- a temporary copy from a single-plugin installer (`.<name>-stage-<UUID>`);
- a previous copy a single-plugin installer saved to `~/Library/Application Support/Vivid Fever Dreams/Recovered` — **Restore Previous Versions** puts it back after verifying its identity and moves the current copy to the Trash;
- leftovers of a finished installation.

**Clean Up…** moves the item to the Trash. **Reveal in Finder** shows it (hidden items need Command-Shift-Period).

## Transaction details

A lock file (`.vfd-suite-installer.lock`, shared with the single-plugin installers) in each plug-in folder prevents concurrent changes; it is an advisory lock released when the installer exits, so the file never blocks a later run. macOS has no single atomic rename for several bundles, so the transaction stages every selected bundle under extension-less names (`<n>.staged`, never scanned by hosts or the AU registrar), verifies them, then commits each with an atomic rename swap (updates) or exclusive rename (new installs). Every previous bundle is kept until all selected bundles verify. A detected failure rolls committed items back in reverse order. The journal (`transaction.json`, format 3) records each bundle’s destination and state — pending, committing, committed, restoring, restored or rollback-failed — and is written before every rename.

Abrupt termination or power loss can leave a `.vfd-suite-transaction-*` folder. The next run removes it automatically only when that is provably safe: the journal says staging, prepared or rolled back and every bundle still matches its recorded previous state, or the journal says everything was verified and every bundle matches that transaction’s payload hashes. Anything else blocks installation and points to **Repair…**. A temporary copy left by an interrupted single-plugin update is reported with directions, and removed automatically only when it is byte-identical to the installed plug-in or the payload.

## Inspect and test

Run these read-only or temporary-fixture commands from the built app:

```sh
"Install Vivid Fever Dreams Suite.app/Contents/MacOS/VFD-Suite-Installer" --inspect-payload
"Install Vivid Fever Dreams Suite.app/Contents/MacOS/VFD-Suite-Installer" --check-hosts
"Install Vivid Fever Dreams Suite.app/Contents/MacOS/VFD-Suite-Installer" --self-test
"Install Vivid Fever Dreams Suite.app/Contents/MacOS/VFD-Suite-Installer" --check-whats-new whats-new.json
```

Self-tests write only in a fresh temporary directory, use a fixture Trash and a recorded Audio Unit refresh, and never touch your plug-in folders. Coverage includes injected failures after each of the ten commits for fresh installs, updates and mixed installations; selection subsets; skipping identical bundles; status labels; AU identities and AU duplicates in a fixture system Components folder; AU-only hosts; uninstall and its verification; every Repair path; conflicting identities; newer versions; corrupt payloads; duplicates; symlinks; host detection; staging changes; and unfinished recovery data.

For visual review, `--render-preview /absolute/path/preview.png [main|uninstall|result|repair]` saves one window state, drawn with sample data, without displaying it or reading your plug-in folders. This needs access to the macOS window server.

## Build from source

The source archive contains the Swift/AppKit transaction engine (`InstallerCore.swift`, `InstallerMaintenance.swift`), the shared guidance and What’s new code (`InstallerGuidance.swift`, also used by the single-plugin installers), the native UI, tests, the logo SVG, the asset renderer, the packaging script and `scripts/Prepare VFD Beta.command.template`. Supply each plug-in’s signed VST3 and AU bundle, in this order, plus the output folder, suite version and What’s new file:

```sh
bash build-installer.sh \
  /absolute/path/YOI-BOI.vst3 /absolute/path/YOI-BOI.component \
  /absolute/path/THROW-UP.vst3 /absolute/path/THROW-UP.component \
  /absolute/path/BACK-SCRATCH.vst3 /absolute/path/BACK-SCRATCH.component \
  /absolute/path/SHIFTY.vst3 /absolute/path/SHIFTY.component \
  "/absolute/path/ENERGY HARVESTER.vst3" "/absolute/path/ENERGY HARVESTER.component" \
  /absolute/path/STUTTER-STEP.vst3 /absolute/path/STUTTER-STEP.component \
  /absolute/path/new-release-directory 1.5.0 /absolute/path/whats-new.json
```

### Developer ID release

1. Sign, notarize and staple the twelve plug-in bundles (copies; the build folders are untouched). `VFD-NOTARY` is a keychain profile made once with `xcrun notarytool store-credentials VFD-NOTARY --apple-id … --team-id …`:

   ```sh
   bash sign-plugins.sh "Developer ID Application: NAME (TEAMID)" VFD-NOTARY /absolute/path/signed-plugins \
     /absolute/path/YOI-BOI.vst3 /absolute/path/YOI-BOI.component …
   ```

2. Build the installer from `/absolute/path/signed-plugins`, with the app signed, notarized and stapled too:

   ```sh
   VFD_SIGN_IDENTITY="Developer ID Application: NAME (TEAMID)" VFD_NOTARY_PROFILE=VFD-NOTARY \
     bash build-installer.sh /absolute/path/signed-plugins/YOI-BOI.vst3 … /absolute/path/new-release-directory 1.5.0 /absolute/path/whats-new.json
   ```

`release-mac.sh VERSION` runs both steps on the local builds, then packages the Beta-Install folder (with `START HERE.txt`), its ZIP and checksum, and checks the result as a downloaded copy. Edit `whats-new-VERSION.json` and `START HERE.txt` first. Plug-ins are signed with the hardened runtime and a secure timestamp. A notarized, stapled installer needs neither Open Anyway nor `Prepare VFD Beta.command`.

`whats-new.json` is a JSON object keyed by plug-in name; each value is one to six short lines (at most 160 characters each), for example `{"YOI-BOI": ["Now also an Audio Unit for Logic Pro and GarageBand."], …}`. Every suite plug-in needs an entry.

Requires Apple’s Xcode command-line tools. Packaging preserves plug-in bytes and their existing signatures. It reads the VST3 class IDs and AU codes from the supplied bundles and refuses any AU whose codes differ from `SuiteCatalog` (update the catalog if a plug-in’s codes change on purpose). It embeds the manifest and What’s new, signs the installer (ad hoc unless `VFD_SIGN_IDENTITY` is set), runs verification and fixture tests, and builds everything in a work folder before moving it into place. The release contains the installer ZIP, source ZIP, `SHA256SUMS.txt`, and `Prepare VFD Beta.command`, generated from the template with the signed installer executable’s SHA-256 filled in.
