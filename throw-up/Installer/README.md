# THROW-UP installer

`Install THROW-UP.app` is a native Apple Silicon macOS installer for the VST3 audio effect. It supports macOS 12 or later and installs to:

```text
~/Library/Audio/Plug-Ins/VST3/THROW-UP.vst3
```

Double-click the app, quit your DAW and other audio hosts, then click **Install / Update**. Future releases replace this same bundle. The installer preserves other plug-ins, user presets, project files and DAW settings. It does not request administrator access or write to the system plug-in folder. A duplicate at `/Library/Audio/Plug-Ins/VST3/THROW-UP.vst3` blocks installation and is reported in the app.

The embedded payload is checked against a SHA-256 manifest, its bundle identity, VST3 processor and controller class IDs, version, and code signature. Class IDs are captured from the built payload and must match an installed copy before replacement. Updates stage beside the destination and use an atomic rename swap. A failed final verification restores the previous bundle. Known audio hosts and processes using the installed executable block installation, and symbolic links are refused. A best-effort log is written to `~/Library/Logs/THROW-UP/installer.log`. It shares the Vivid Fever Dreams Suite installer's lock, will not run while an interrupted suite installation is pending, clears the download quarantine flag from the installed copy, and stages under a hidden name without a .vst3 extension. If a rollback cannot finish, the previous copy is saved to `~/Library/Application Support/Vivid Fever Dreams/Recovered`.

Build from the THROW-UP source root after building the VST3:

```sh
bash Installer/build-installer.sh "/absolute/path/THROW-UP.vst3" "/absolute/path/new-installer-output" 0.1.4
```

The third argument must match the built plug-in. The optional fourth argument sets the installer build independently; it defaults to the plug-in version. The output directory must not already contain `Install THROW-UP.app`. The script compiles the native app, copies the embedded plug-in and verifies its existing signature (it never re-signs it), creates its manifest, signs the installer, verifies both bundles, and runs isolated checks covering host detection, installation, updates, rollback and preservation of existing YOI-BOI plug-ins. It does not install the plug-in or write to the user's VST3 folder.

The compiled executable supports `--inspect-payload`, `--check-hosts` and `--self-test`; it has no silent install option. The installer uses `Assets/AppIcon.icns`; the plug-in uses the matching THROW-UP icon supplied with the source.

To run the isolated installer tests before a VST3 payload is ready:

```sh
mkdir -p build/installer-checks
xcrun swiftc -O -target arm64-apple-macosx12.0 \
  -module-cache-path build/installer-checks/module-cache \
  Installer/InstallerCore.swift Installer/InstallerTests.swift \
  Installer/InstallerApp.swift Installer/main.swift \
  -framework AppKit -framework CryptoKit \
  -o build/installer-checks/THROW-UP-Installer
build/installer-checks/THROW-UP-Installer --self-test
```

The app and plug-in are signed locally with an ad hoc identity. Public distribution without macOS's **Open Anyway** step requires a Developer ID certificate and Apple notarization. This release is not notarized.
