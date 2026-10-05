# YOI-BOI installer

A native Apple Silicon macOS app, requiring macOS 12 or later (the plug-in itself supports macOS 11 or later). Double-click **Install YOI-BOI.app**, quit audio hosts, and click **Install / Update**. It installs only the VST3 instrument, to `~/Library/Audio/Plug-Ins/VST3/YOI-BOI.vst3`. Future releases replace this same bundle and retain the same VST3 identities. Older installers refuse to downgrade a newer installed copy. Compatibility of old saved sounds is supplied by the instrument's state migration and versioned sound engines.

The installer does not alter saved presets, project files, Live settings, macOS security settings, or the standalone application. A previous copy in `/Library/Audio/Plug-Ins/VST3/` is reported and blocks installation to prevent duplicate entries. Custom plug-in directories are not searched: move an existing custom-location copy to the standard user folder before the first update. No administrator access is requested.

Each payload file is checked against a SHA-256 manifest; bundle and VST3 class IDs, version, and code signature are checked before and after copying. Installation stages on the destination volume. Updates use macOS's atomic rename swap; a failed final verification restores the previous bundle automatically. Successful updates remove the temporary old bundle. Known audio hosts and processes using the installed executable block installation. Symlinks and unexpected special files are refused. A best-effort log is written to `~/Library/Logs/YOI-BOI/installer.log`.

Build from the source root:

```sh
bash Installer/build-installer.sh "/absolute/path/YOI-BOI.vst3" "/absolute/path/new-installer-output" 0.1.5
```

The output directory must not already contain `Install YOI-BOI.app`. The script compiles the native app, embeds the plug-in, creates a manifest, signs the installer ad hoc, verifies the embedded bundle and installer, and runs isolated transaction tests. It never invokes the install action or touches the user's plug-in folder. Run the compiled app's executable with `--self-test` to repeat tests or `--inspect-payload` to verify its embedded payload. There is deliberately no silent-install command-line flag.

The installer and plug-in are locally ad-hoc signed, not Developer ID signed or notarized. Downloaded copies may require macOS's per-app Open Anyway approval. A Developer ID certificate and Apple notarization are needed before broad public distribution with the normal trusted-app opening experience.
