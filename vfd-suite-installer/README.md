# Vivid Fever Dreams Suite 1.2.2

One native installer for YOI-BOI 0.1.6, THROW-UP 0.1.5, BACK-SCRATCH 0.10.2, SHIFTY 0.4.2, and ENERGY HARVESTER 0.3.2.

All five report **VIVID FEVER DREAMS** as their creator/vendor. This is the manufacturer field used by plug-in browsers, including Ableton Live’s Creator filter. Plug-in names, bundle identifiers, and VST3 component/controller identities remain compatible with earlier releases. Rescan VST3 plug-ins after updating to refresh the browser metadata.

Requires **macOS 12 or later on Apple Silicon**. These are **VST3** plug-ins. This package does not provide Intel, Audio Unit, or AAX builds. The installer and included plug-ins are ad-hoc signed and are not Apple notarized.

## Install or update

1. Quit your audio applications.
2. Open **Install Vivid Fever Dreams Suite.app**.
3. Click **Install / Update All Five**.
4. Open your audio application and rescan VST3 plug-ins.

All five go to `~/Library/Audio/Plug-Ins/VST3`. Compatible existing copies update in place; presets and DAW projects are not edited. The installer refuses downgrades, conflicting VST3 identities, symbolic-link targets, running VST3 hosts (by bundle identifier or name, plus any process that has an installed suite plug-in loaded), and duplicate copies — matched by bundle identifier or VST3 class ID anywhere in the user or system VST3 folder and their subfolders. Resolve a reported conflict before retrying. Administrative access is not required. The download quarantine flag is cleared from the installed copies, so a DAW does not show a Gatekeeper prompt for each plug-in.

The log is `~/Library/Logs/Vivid Fever Dreams Suite/installer.log`; the installer’s **View Log** button opens it. Each attempt is appended; the log rotates to `installer.log.1` past 256 KB.

## Complete-suite transaction

The installer verifies every payload’s SHA-256 file inventory, bundle identity, actual VST3 component/controller identities, executable permission, and code signature. It stages and rechecks all five before changing installed plug-ins. Updates use atomic per-bundle swaps, and new installs use exclusive renames. Every prior bundle is retained until all five destinations have passed verification. A detected failure rolls committed items back in reverse order, including mixed new/update installations.

A lock file (`.vfd-suite-installer.lock`, shared with the single-plugin installers) prevents concurrent changes to the VST3 folder; it is an advisory lock that is released when the installer exits, so the file itself never blocks a later run. Since macOS has no single atomic rename for five bundles, the group transaction is implemented by staging and rollback. Staged copies use extension-less names (`<index>.staged`) so audio apps never scan them as plug-ins. The journal (`transaction.json`) records each plug-in’s state — pending, committing, committed, restoring, restored, or rollback-failed — and is written before every rename.

Abrupt process termination or power loss can leave a `.vfd-suite-transaction-*` folder. The next run removes it automatically only when that is provably safe: the journal says staging, prepared or rolled back and every installed plug-in still matches its recorded previous state, or the journal says all five were verified and every installed plug-in matches that transaction’s payload hashes. Anything else blocks installation with the folder’s location; review its journal and retained bundles before recovery, and do not delete it casually. If another program independently changes a committed destination, recovery preserves the transaction and reports its path instead of overwriting that change. A temporary copy left by an interrupted single-plugin update (`.<name>-stage-<UUID>.vst3`) is reported with directions, and removed automatically only when it is byte-identical to the installed plug-in or the payload.

## Inspect and test

Run these read-only or temporary-fixture commands from the built app:

```sh
"Install Vivid Fever Dreams Suite.app/Contents/MacOS/VFD-Suite-Installer" --inspect-payload
"Install Vivid Fever Dreams Suite.app/Contents/MacOS/VFD-Suite-Installer" --check-hosts
"Install Vivid Fever Dreams Suite.app/Contents/MacOS/VFD-Suite-Installer" --self-test
```

Self-tests write only in a fresh temporary directory. They never install into the live user VST3 folder. Coverage includes injected failures after each of the five commits for fresh installs, updates, and mixed installations; conflicting identities; newer versions; corrupt payloads; duplicate copies; symlinks; host detection; staging changes; and unfinished recovery data. Build-time test and payload-verification logs accompany the app.

For visual review, `--render-preview /absolute/path/preview.png` saves the native window as an image without displaying it or running installation. This diagnostic needs access to the macOS window server.

## Build from source

The source archive contains the Swift/AppKit transaction engine, native UI, tests, exact supplied logo SVG, asset renderer, and packaging script. It does not include the five plug-in source trees or their payloads. Supply the current signed VST3 bundles in this exact order:

```sh
bash build-installer.sh \
  /absolute/path/YOI-BOI.vst3 \
  /absolute/path/THROW-UP.vst3 \
  /absolute/path/BACK-SCRATCH.vst3 \
  /absolute/path/SHIFTY.vst3 \
  "/absolute/path/ENERGY HARVESTER.vst3" \
  /absolute/path/new-release-directory \
  1.2.2
```

Requires Apple’s Xcode command-line tools. Packaging preserves plug-in bytes and their existing signatures; component and controller IDs are read from the actual supplied bundles. The script verifies required versions and identities, embeds a manifest, signs the installer ad hoc, runs verification and fixture tests, and produces the installer ZIP, source ZIP, and checksums. The supplied SVG is rendered into a restrained monochrome suite icon using AppKit, without redrawing its shapes.

The suite includes the current sources of all five plugins, including the BACK-SCRATCH factory-position fix from 0.10.1. The changes from those current versions are the creator metadata and patch version numbers.

ENERGY HARVESTER includes a precise −18 to +18 dB OUTPUT dial that adjusts the spin-up only and returns to original clean level afterward.
