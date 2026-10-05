#!/bin/bash
set -euo pipefail

# Usage: build-installer.sh \
#   YOI-BOI.vst3 YOI-BOI.component THROW-UP.vst3 THROW-UP.component BACK-SCRATCH.vst3 BACK-SCRATCH.component \
#   SHIFTY.vst3 SHIFTY.component "ENERGY HARVESTER.vst3" "ENERGY HARVESTER.component" \
#   OUTPUT-DIRECTORY SUITE_VERSION WHATS-NEW.json
# Every path must be absolute. Each plug-in is supplied as its signed VST3 bundle followed by its signed AU bundle.
installer_names=("YOI-BOI" "THROW-UP" "BACK-SCRATCH" "SHIFTY" "ENERGY HARVESTER")
if [[ $# -ne 13 ]]; then
    printf 'Usage: %s <VST3> <AU> for YOI-BOI, THROW-UP, BACK-SCRATCH, SHIFTY and ENERGY HARVESTER (in that order), then /absolute/output-directory 1.3.0 /absolute/whats-new.json\n' "$0" >&2
    exit 1
fi
installer_source_dir="$(cd "$(dirname "$0")" && pwd -P)"
installer_vst3=("$1" "$3" "$5" "$7" "$9")
installer_au=("$2" "$4" "$6" "$8" "${10}")
installer_output="${11}"
installer_version="${12}"
installer_whats_new="${13}"
if [[ ! "$installer_version" =~ ^[0-9]+(\.[0-9]+){1,3}$ ]]; then
    printf 'Suite version must have two to four numeric components.\n' >&2; exit 1
fi
case "$installer_output" in /*) ;; *) printf 'Output must be an absolute path.\n' >&2; exit 1 ;; esac
case "$installer_whats_new" in /*) ;; *) printf 'The what'"'"'s-new JSON path must be absolute.\n' >&2; exit 1 ;; esac
[[ -f "$installer_whats_new" ]] || { printf 'Missing what'"'"'s-new JSON: %s\n' "$installer_whats_new" >&2; exit 1; }
installer_whats_new="$(cd "$(dirname "$installer_whats_new")" && pwd -P)/$(basename "$installer_whats_new")"
installer_helper_template="$installer_source_dir/scripts/Prepare VFD Beta.command.template"
[[ -f "$installer_helper_template" ]] || { printf 'Missing beta helper template: %s\n' "$installer_helper_template" >&2; exit 1; }
for installer_index in 0 1 2 3 4; do
    installer_name="${installer_names[$installer_index]}"
    for installer_kind in vst3 component; do
        if [[ "$installer_kind" == vst3 ]]; then installer_payload="${installer_vst3[$installer_index]}"; else installer_payload="${installer_au[$installer_index]}"; fi
        case "$installer_payload" in /*) ;; *) printf 'Payload paths must be absolute.\n' >&2; exit 1 ;; esac
        if [[ ! -d "$installer_payload" || "$(basename "$installer_payload")" != "$installer_name.$installer_kind" ]]; then
            printf 'Expected the built %s.%s bundle, got: %s\n' "$installer_name" "$installer_kind" "$installer_payload" >&2; exit 1
        fi
        installer_payload="$(cd "$installer_payload" && pwd -P)"
        if [[ "$installer_kind" == vst3 ]]; then installer_vst3[$installer_index]="$installer_payload"; else installer_au[$installer_index]="$installer_payload"; fi
    done
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
installer_swift_sources=(InstallerCore.swift InstallerMaintenance.swift InstallerGuidance.swift InstallerTests.swift InstallerApp.swift main.swift)
installer_swift_paths=()
for installer_file in "${installer_swift_sources[@]}"; do installer_swift_paths+=("$installer_source_dir/$installer_file"); done
mkdir -p "$installer_app/Contents/MacOS" "$installer_resources/Payload"
xcrun swiftc -O -target arm64-apple-macosx12.0 -module-cache-path "$installer_work/module-cache" \
    "${installer_swift_paths[@]}" -framework AppKit -framework CryptoKit -o "$installer_app/Contents/MacOS/VFD-Suite-Installer"
xcrun swiftc -O -target arm64-apple-macosx12.0 -module-cache-path "$installer_work/module-cache" \
    "$installer_source_dir/MakeAssets.swift" -framework AppKit -o "$installer_work/make-assets"
"$installer_work/make-assets" "$installer_source_dir/Assets/VfdLogo.svg" "$installer_work/Assets"
cp "$installer_work/Assets/AppIcon.icns" "$installer_resources/AppIcon.icns"
cp "$installer_work/Assets/VfdLogo.png" "$installer_resources/VfdLogo.png"
"$installer_app/Contents/MacOS/VFD-Suite-Installer" --check-whats-new "$installer_whats_new"
cp "$installer_whats_new" "$installer_resources/whats-new.json"
for installer_index in 0 1 2 3 4; do
    # Preserve the supplied signatures and bytes. Never re-sign plug-in payloads here.
    /usr/bin/ditto --noqtn "${installer_vst3[$installer_index]}" "$installer_resources/Payload/${installer_names[$installer_index]}.vst3"
    /usr/bin/ditto --noqtn "${installer_au[$installer_index]}" "$installer_resources/Payload/${installer_names[$installer_index]}.component"
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
installer_helper="Prepare VFD Beta.command"
# The beta helper pins the SHA-256 of the signed installer executable, so it is generated after signing.
installer_hash="$(/usr/bin/shasum -a 256 "$installer_app/Contents/MacOS/VFD-Suite-Installer")"
installer_hash="${installer_hash%% *}"
sed -e "s/@SUITE_VERSION@/$installer_version/g" -e "s/@INSTALLER_SHA256@/$installer_hash/g" \
    "$installer_helper_template" > "$installer_stage/$installer_helper"
chmod 755 "$installer_stage/$installer_helper"
if grep -q '@[A-Z_0-9]*@' "$installer_stage/$installer_helper"; then
    printf 'The beta helper template has unfilled placeholders.\n' >&2; exit 1
fi
/bin/bash -n "$installer_stage/$installer_helper"
installer_source_pack="$installer_work/VFD-Suite-Installer-Source"
mkdir -p "$installer_source_pack/Assets" "$installer_source_pack/scripts"
cp "${installer_swift_paths[@]}" "$installer_source_dir/MakeAssets.swift" "$installer_source_dir/build-installer.sh" \
    "$installer_source_dir/README.md" "$installer_source_pack/"
cp "$installer_source_dir/Assets/VfdLogo.svg" "$installer_source_pack/Assets/"
cp "$installer_helper_template" "$installer_source_pack/scripts/"
/usr/bin/ditto -c -k --keepParent --sequesterRsrc "$installer_source_pack" "$installer_stage/$installer_source_zip"
/usr/bin/ditto -c -k --keepParent --sequesterRsrc "$installer_app" "$installer_stage/$installer_app_zip"
(
    cd "$installer_stage"
    /usr/bin/shasum -a 256 "$installer_app_zip" "$installer_source_zip" "$installer_helper" > SHA256SUMS.txt
)
cp "$installer_source_dir/README.md" "$installer_stage/README.md"
cp "$installer_resources/suite-manifest.json" "$installer_stage/suite-manifest.json"
cp "$installer_work/payload-verification.txt" "$installer_stage/payload-verification.txt"
cp "$installer_work/installer-tests.txt" "$installer_stage/installer-tests.txt"
installer_artifacts=(README.md suite-manifest.json payload-verification.txt installer-tests.txt
    "$installer_source_zip" "$installer_app_zip" "$installer_helper" SHA256SUMS.txt)
for installer_artifact in "${installer_artifacts[@]}"; do
    if [[ -e "$installer_output/$installer_artifact" ]]; then
        printf 'Output already contains %s. Use a fresh output directory.\n' "$installer_artifact" >&2; exit 1
    fi
done
for installer_artifact in "${installer_artifacts[@]}"; do
    mv "$installer_stage/$installer_artifact" "$installer_output/$installer_artifact"
done
# The app moves last: its presence marks a complete release.
mv "$installer_app" "$installer_output/Install Vivid Fever Dreams Suite.app"
printf 'Built and verified: %s\n' "$installer_output/Install Vivid Fever Dreams Suite.app"
