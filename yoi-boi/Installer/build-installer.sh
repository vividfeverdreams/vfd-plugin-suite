#!/bin/bash
set -euo pipefail

# Usage: Installer/build-installer.sh /absolute/YOI-BOI.vst3 /absolute/output-directory [version]
installer_source_dir="$(cd "$(dirname "$0")" && pwd)"
plugin_source="${1:?Supply the built VST3 bundle path}"
installer_output="${2:?Supply the output directory}"
installer_version="${3:-0.1.5}"
case "$plugin_source" in /*) ;; *) printf 'VST3 path must be absolute.\n' >&2; exit 1 ;; esac
case "$installer_output" in /*) ;; *) printf 'Output path must be absolute.\n' >&2; exit 1 ;; esac
if [[ "$installer_output" == "/" || "$installer_output" == "$HOME" ]]; then
    printf 'Choose a dedicated build or release directory.\n' >&2
    exit 1
fi
mkdir -p "$installer_output"
installer_work="$(mktemp -d "$installer_output/.yoi-installer-build.XXXXXX")"
trap 'rm -rf "$installer_work"' EXIT
installer_app="$installer_work/Install YOI-BOI.app"
mkdir -p "$installer_app/Contents/MacOS" "$installer_app/Contents/Resources/Payload"
xcrun swiftc -O -target arm64-apple-macosx12.0 -module-cache-path "$installer_work/module-cache" \
    "$installer_source_dir/InstallerCore.swift" "$installer_source_dir/InstallerTests.swift" \
    "$installer_source_dir/InstallerApp.swift" "$installer_source_dir/main.swift" \
    -framework AppKit -framework CryptoKit -o "$installer_app/Contents/MacOS/YOI-BOI-Installer"
ditto --noqtn "$plugin_source" "$installer_app/Contents/Resources/Payload/YOI-BOI.vst3"
cp "$installer_source_dir/../Assets/AppIcon.icns" "$installer_app/Contents/Resources/AppIcon.icns"
cat > "$installer_app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>com.yoiboi.installer</string>
<key>CFBundleExecutable</key><string>YOI-BOI-Installer</string>
<key>CFBundleName</key><string>Install YOI-BOI</string>
<key>CFBundleDisplayName</key><string>Install YOI-BOI</string>
<key>CFBundleIconFile</key><string>AppIcon.icns</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$installer_version</string>
<key>CFBundleVersion</key><string>$installer_version</string>
<key>LSMinimumSystemVersion</key><string>12.0</string>
<key>NSHighResolutionCapable</key><true/>
<key>NSPrincipalClass</key><string>NSApplication</string>
</dict></plist>
EOF
"$installer_app/Contents/MacOS/YOI-BOI-Installer" --make-manifest \
    "$installer_app/Contents/Resources/Payload/YOI-BOI.vst3" \
    "$installer_app/Contents/Resources/payload-manifest.json" "$installer_version"
codesign --force --sign - "$installer_app"
codesign --verify --deep --strict "$installer_app"
"$installer_app/Contents/MacOS/YOI-BOI-Installer" --inspect-payload
"$installer_app/Contents/MacOS/YOI-BOI-Installer" --self-test > "$installer_work/installer-tests.txt"
if [[ -e "$installer_output/Install YOI-BOI.app" ]]; then
    printf 'Output already exists: %s\nUse a fresh output directory.\n' "$installer_output/Install YOI-BOI.app" >&2
    exit 1
fi
mv "$installer_app" "$installer_output/Install YOI-BOI.app"
cp "$installer_work/installer-tests.txt" "$installer_output/installer-tests.txt"
printf 'Built and verified: %s\n' "$installer_output/Install YOI-BOI.app"
