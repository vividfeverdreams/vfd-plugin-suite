#!/bin/bash
set -euo pipefail

# Usage: Installer/build-installer.sh /absolute/THROW-UP.vst3 /absolute/output-directory version [installer-build]
installer_source_dir="$(cd "$(dirname "$0")" && pwd)"
installer_icon="$installer_source_dir/../Assets/AppIcon.icns"
plugin_source="${1:?Supply the built VST3 bundle path}"
installer_output="${2:?Supply the output directory}"
installer_version="${3:?Supply the plug-in version, for example 0.1.0}"
installer_build="${4:-$installer_version}"
case "$plugin_source" in /*) ;; *) printf 'VST3 path must be absolute.\n' >&2; exit 1 ;; esac
case "$installer_output" in /*) ;; *) printf 'Output path must be absolute.\n' >&2; exit 1 ;; esac
if [[ ! "$installer_version" =~ ^[0-9]+(\.[0-9]+){1,3}$ || ! "$installer_build" =~ ^[0-9]+(\.[0-9]+){1,3}$ ]]; then
    printf 'Version must contain two to four numeric components, for example 0.1.0.\n' >&2
    exit 1
fi
if [[ ! -d "$plugin_source" || "$(basename "$plugin_source")" != "THROW-UP.vst3" || ! -f "$plugin_source/Contents/Info.plist" || ! -x "$plugin_source/Contents/MacOS/THROW-UP" ]]; then
    printf 'Supply a built THROW-UP.vst3 bundle with its executable.\n' >&2
    exit 1
fi
if [[ ! -f "$installer_icon" ]]; then
    printf 'Missing installer icon: %s\n' "$installer_icon" >&2
    exit 1
fi
if [[ "$installer_output" == "/" || "$installer_output" == "$HOME" ]]; then
    printf 'Choose a dedicated build or release directory.\n' >&2
    exit 1
fi
if [[ -e "$installer_output/Install THROW-UP.app" ]]; then
    printf 'Output already exists: %s\nUse a fresh output directory.\n' "$installer_output/Install THROW-UP.app" >&2
    exit 1
fi
mkdir -p "$installer_output"
installer_work="$(mktemp -d "$installer_output/.throwup-installer-build.XXXXXX")"
trap 'rm -rf "$installer_work"' EXIT
installer_app="$installer_work/Install THROW-UP.app"
mkdir -p "$installer_app/Contents/MacOS" "$installer_app/Contents/Resources/Payload"
cp "$installer_icon" "$installer_app/Contents/Resources/AppIcon.icns"
xcrun swiftc -O -target arm64-apple-macosx12.0 -module-cache-path "$installer_work/module-cache" \
    "$installer_source_dir/InstallerCore.swift" "$installer_source_dir/InstallerTests.swift" \
    "$installer_source_dir/InstallerApp.swift" "$installer_source_dir/main.swift" \
    -framework AppKit -framework CryptoKit -o "$installer_app/Contents/MacOS/THROW-UP-Installer"
ditto --noqtn "$plugin_source" "$installer_app/Contents/Resources/Payload/THROW-UP.vst3"
codesign --force --deep --sign - "$installer_app/Contents/Resources/Payload/THROW-UP.vst3"
cat > "$installer_app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>com.yoiboi.throwup.installer</string>
<key>CFBundleExecutable</key><string>THROW-UP-Installer</string>
<key>CFBundleName</key><string>Install THROW-UP</string>
<key>CFBundleDisplayName</key><string>Install THROW-UP</string>
<key>CFBundleIconFile</key><string>AppIcon.icns</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$installer_version</string>
<key>CFBundleVersion</key><string>$installer_build</string>
<key>LSMinimumSystemVersion</key><string>12.0</string>
<key>NSHighResolutionCapable</key><true/>
<key>NSPrincipalClass</key><string>NSApplication</string>
</dict></plist>
EOF
"$installer_app/Contents/MacOS/THROW-UP-Installer" --make-manifest \
    "$installer_app/Contents/Resources/Payload/THROW-UP.vst3" \
    "$installer_app/Contents/Resources/payload-manifest.json" "$installer_version"
codesign --force --sign - "$installer_app"
codesign --verify --deep --strict "$installer_app"
"$installer_app/Contents/MacOS/THROW-UP-Installer" --inspect-payload
"$installer_app/Contents/MacOS/THROW-UP-Installer" --self-test > "$installer_work/installer-tests.txt"
mv "$installer_app" "$installer_output/Install THROW-UP.app"
cp "$installer_work/installer-tests.txt" "$installer_output/installer-tests.txt"
printf 'Built and verified: %s\n' "$installer_output/Install THROW-UP.app"
