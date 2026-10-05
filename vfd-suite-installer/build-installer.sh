#!/bin/bash
set -euo pipefail

# Usage: build-installer.sh YOI-BOI.vst3 THROW-UP.vst3 BACK-SCRATCH.vst3 SHIFTY.vst3 ENERGY-HARVESTER.vst3 OUTPUT SUITE_VERSION
if [[ $# -ne 7 ]]; then
    printf 'Usage: %s /absolute/YOI-BOI.vst3 /absolute/THROW-UP.vst3 /absolute/BACK-SCRATCH.vst3 /absolute/SHIFTY.vst3 "/absolute/ENERGY HARVESTER.vst3" /absolute/output-directory 1.2.2\n' "$0" >&2
    exit 1
fi
installer_source_dir="$(cd "$(dirname "$0")" && pwd -P)"
installer_sources=("$1" "$2" "$3" "$4" "$5")
installer_names=("YOI-BOI" "THROW-UP" "BACK-SCRATCH" "SHIFTY" "ENERGY HARVESTER")
installer_output="$6"
installer_version="$7"
if [[ ! "$installer_version" =~ ^[0-9]+(\.[0-9]+){1,3}$ ]]; then
    printf 'Suite version must have two to four numeric components.\n' >&2; exit 1
fi
case "$installer_output" in /*) ;; *) printf 'Output must be an absolute path.\n' >&2; exit 1 ;; esac
for installer_index in 0 1 2 3 4; do
    installer_payload="${installer_sources[$installer_index]}"
    installer_name="${installer_names[$installer_index]}"
    case "$installer_payload" in /*) ;; *) printf 'Payload paths must be absolute.\n' >&2; exit 1 ;; esac
    if [[ ! -d "$installer_payload" || "$(basename "$installer_payload")" != "$installer_name.vst3" ]]; then
        printf 'Expected the built %s.vst3 bundle, got: %s\n' "$installer_name" "$installer_payload" >&2; exit 1
    fi
    installer_sources[$installer_index]="$(cd "$installer_payload" && pwd -P)"
done
mkdir -p "$installer_output"
installer_output="$(cd "$installer_output" && pwd -P)"
if [[ "$installer_output" == / || "$installer_output" == "$HOME" ]]; then
    printf 'Choose a dedicated release directory.\n' >&2; exit 1
fi
if [[ -e "$installer_output/Install Vivid Fever Dreams Suite.app" ]]; then
    printf 'Installer already exists. Use a fresh output directory: %s\n' "$installer_output" >&2; exit 1
fi
installer_work="$(mktemp -d "$installer_output/.vfd-suite-build.XXXXXX")"
trap 'rm -rf "$installer_work"' EXIT
installer_app="$installer_work/Install Vivid Fever Dreams Suite.app"
installer_resources="$installer_app/Contents/Resources"
mkdir -p "$installer_app/Contents/MacOS" "$installer_resources/Payload"
xcrun swiftc -O -target arm64-apple-macosx12.0 -module-cache-path "$installer_work/module-cache" \
    "$installer_source_dir/InstallerCore.swift" "$installer_source_dir/InstallerTests.swift" \
    "$installer_source_dir/InstallerApp.swift" "$installer_source_dir/main.swift" \
    -framework AppKit -framework CryptoKit -o "$installer_app/Contents/MacOS/VFD-Suite-Installer"
xcrun swiftc -O -target arm64-apple-macosx12.0 -module-cache-path "$installer_work/module-cache" \
    "$installer_source_dir/MakeAssets.swift" -framework AppKit -o "$installer_work/make-assets"
"$installer_work/make-assets" "$installer_source_dir/Assets/VfdLogo.svg" "$installer_work/Assets"
cp "$installer_work/Assets/AppIcon.icns" "$installer_resources/AppIcon.icns"
cp "$installer_work/Assets/VfdLogo.png" "$installer_resources/VfdLogo.png"
for installer_index in 0 1 2 3 4; do
    # Preserve the supplied signatures and bytes. Never re-sign plugin payloads here.
    /usr/bin/ditto --noqtn "${installer_sources[$installer_index]}" "$installer_resources/Payload/${installer_names[$installer_index]}.vst3"
done
cat > "$installer_app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>xyz.vividfeverdreams.suite.installer</string>
<key>CFBundleExecutable</key><string>VFD-Suite-Installer</string>
<key>CFBundleName</key><string>Install Vivid Fever Dreams Suite</string>
<key>CFBundleDisplayName</key><string>Install Vivid Fever Dreams Suite</string>
<key>CFBundleIconFile</key><string>AppIcon.icns</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$installer_version</string>
<key>CFBundleVersion</key><string>$installer_version</string>
<key>LSMinimumSystemVersion</key><string>12.0</string>
<key>NSHighResolutionCapable</key><true/>
<key>NSPrincipalClass</key><string>NSApplication</string>
</dict></plist>
EOF
"$installer_app/Contents/MacOS/VFD-Suite-Installer" --make-manifest "$installer_resources/Payload" \
    "$installer_resources/suite-manifest.json" "$installer_version"
/usr/bin/codesign --force --sign - "$installer_app"
/usr/bin/codesign --verify --deep --strict "$installer_app"
"$installer_app/Contents/MacOS/VFD-Suite-Installer" --inspect-payload > "$installer_work/payload-verification.txt"
"$installer_app/Contents/MacOS/VFD-Suite-Installer" --self-test > "$installer_work/installer-tests.txt"
# Every release artifact is produced inside the work directory first. Nothing reaches the output directory
# until all of them exist, so a failed step never leaves a half-finished release that blocks a rerun.
installer_stage="$installer_work/release"
mkdir -p "$installer_stage"
installer_app_zip="Vivid-Fever-Dreams-Suite-v$installer_version-macOS-AppleSilicon.zip"
installer_source_zip="VFD-Suite-Installer-Source-v$installer_version.zip"
installer_source_pack="$installer_work/VFD-Suite-Installer-Source"
mkdir -p "$installer_source_pack/Assets"
cp "$installer_source_dir/InstallerCore.swift" "$installer_source_dir/InstallerTests.swift" \
    "$installer_source_dir/InstallerApp.swift" "$installer_source_dir/main.swift" \
    "$installer_source_dir/MakeAssets.swift" "$installer_source_dir/build-installer.sh" \
    "$installer_source_dir/README.md" "$installer_source_pack/"
cp "$installer_source_dir/Assets/VfdLogo.svg" "$installer_source_pack/Assets/"
/usr/bin/ditto -c -k --keepParent --sequesterRsrc "$installer_source_pack" "$installer_stage/$installer_source_zip"
/usr/bin/ditto -c -k --keepParent --sequesterRsrc "$installer_app" "$installer_stage/$installer_app_zip"
(
    cd "$installer_stage"
    /usr/bin/shasum -a 256 "$installer_app_zip" "$installer_source_zip" > SHA256SUMS.txt
)
cp "$installer_source_dir/README.md" "$installer_stage/README.md"
cp "$installer_resources/suite-manifest.json" "$installer_stage/suite-manifest.json"
cp "$installer_work/payload-verification.txt" "$installer_stage/payload-verification.txt"
cp "$installer_work/installer-tests.txt" "$installer_stage/installer-tests.txt"
for installer_artifact in README.md suite-manifest.json payload-verification.txt installer-tests.txt \
    "$installer_source_zip" "$installer_app_zip" SHA256SUMS.txt; do
    if [[ -e "$installer_output/$installer_artifact" ]]; then
        printf 'Output already contains %s. Use a fresh output directory.\n' "$installer_artifact" >&2; exit 1
    fi
done
for installer_artifact in README.md suite-manifest.json payload-verification.txt installer-tests.txt \
    "$installer_source_zip" "$installer_app_zip" SHA256SUMS.txt; do
    mv "$installer_stage/$installer_artifact" "$installer_output/$installer_artifact"
done
# The app moves last: its presence marks a complete release.
mv "$installer_app" "$installer_output/Install Vivid Fever Dreams Suite.app"
printf 'Built and verified: %s\n' "$installer_output/Install Vivid Fever Dreams Suite.app"
