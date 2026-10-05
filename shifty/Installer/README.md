# SHIFTY installer

`Install SHIFTY.app` is a native Apple Silicon macOS installer for the VST3 audio effect. It supports macOS 12 or later and installs to:

```
~/Library/Audio/Plug-Ins/VST3/SHIFTY.vst3
```

Double-click the app, quit Ableton Live and other audio hosts, then click **Install / Update**. Future releases replace this same bundle. The installer does not touch user presets, project files, Live settings, or any other plug-in. It does not request administrator access or write to the system plug-in folder. A duplicate at `/Library/Audio/Plug-Ins/VST3/SHIFTY.vst3` blocks installation and is reported in the app.

Installer build **0.2.0.1** fixes an error where another copy of the SHIFTY installer was mistaken for an audio host. That correction is included in the **0.4.0** installer and later releases. Real audio hosts and processes loading SHIFTY still block an update, and any installation error remains visible in the main window after closing the alert.

The embedded payload is checked against a SHA-256 manifest, its bundle identity, VST3 processor and controller class IDs, version, and code signature. The class IDs are captured from the built payload and must match an installed copy before replacement. Updates stage beside the destination and use an atomic rename swap. A failed final verification restores the previous bundle. Known audio hosts and processes using the installed executable block installation, and symbolic links are refused. A best-effort log is written to `~/Library/Logs/SHIFTY/installer.log`.

Build from the SHIFTY source root after building the VST3:

```sh
bash Installer/build-installer.sh "/absolute/path/SHIFTY.vst3" "/absolute/path/new-installer-output" 0.4.1
```

The third argument must match the built plug-in. The optional fourth argument sets the installer build independently; it defaults to the plug-in version. The output directory must not already contain `Install SHIFTY.app`. The script compiles the native app, copies and signs the embedded plug-in, creates its manifest, signs the installer, verifies both bundles, and runs 40 isolated checks covering host detection, installation, updates and rollback. It does not install the plug-in or write to the user's VST3 folder. The compiled executable supports `--inspect-payload`, `--check-hosts` and `--self-test`; it has no silent install option.

The installer uses the approved green mascot icon from `Assets/AppIcon.icns`; the VST3 build uses `Assets/AppIcon.png`. These Finder icons do not change the audio processing, editor, plug-in identity, or version.

The app and plug-in are signed locally with an ad hoc identity. Public distribution without macOS's Open Anyway step requires a Developer ID certificate and Apple notarization.
