#!/bin/bash
set -euo pipefail

# Usage: Installer/build-installer.sh "/absolute/BACK-SCRATCH.vst3" "/absolute/BACK-SCRATCH.component" /absolute/output-directory 0.11.0 /absolute/whats-new.json [installer-build]
# The what's-new JSON may be the suite's file; only the BACK-SCRATCH entry is required and shown.
installer_source_dir="$(cd "$(dirname "$0")" && pwd -P)"
installer_icon="$installer_source_dir/../Assets/AppIcon.icns"
plugin_vst3="${1:?Supply the built VST3 bundle path}"
plugin_au="${2:?Supply the built Audio Unit (.component) bundle path}"
installer_output="${3:?Supply the output directory}"
installer_version="${4:?Supply the plug-in version, for example 0.11.0}"
installer_whats_new="${5:?Supply the whats-new.json file}"
installer_build="${6:-$installer_version}"
for installer_path in "$plugin_vst3" "$plugin_au" "$installer_output" "$installer_whats_new"; do
    case "$installer_path" in /*) ;; *) printf 'Paths must be absolute: %s\n' "$installer_path" >&2; exit 1 ;; esac
done
if [[ ! "$installer_version" =~ ^[0-9]+(\.[0-9]+){1,3}$ || ! "$installer_build" =~ ^[0-9]+(\.[0-9]+){1,3}$ ]]; then
    printf 'Version must contain two to four numeric components, for example 0.11.0.\n' >&2
    exit 1
fi
for installer_bundle in "$plugin_vst3" "$plugin_au"; do
    installer_kind="${installer_bundle##*.}"
    if [[ ! -d "$installer_bundle" || "$(basename "$installer_bundle")" != "BACK-SCRATCH.$installer_kind" || ( "$installer_kind" != vst3 && "$installer_kind" != component ) \
          || ! -f "$installer_bundle/Contents/Info.plist" || ! -x "$installer_bundle/Contents/MacOS/BACK-SCRATCH" ]]; then
        printf 'Supply built BACK-SCRATCH.vst3 and BACK-SCRATCH.component bundles with their executables (got %s).\n' "$installer_bundle" >&2
        exit 1
    fi
done
[[ "$(basename "$plugin_vst3")" == "BACK-SCRATCH.vst3" && "$(basename "$plugin_au")" == "BACK-SCRATCH.component" ]] || { printf 'Supply the VST3 first, then the AU.\n' >&2; exit 1; }
if [[ ! -f "$installer_icon" ]]; then
    printf 'Missing installer icon: %s\n' "$installer_icon" >&2
    exit 1
fi
[[ -f "$installer_whats_new" ]] || { printf 'Missing what'"'"'s-new JSON: %s\n' "$installer_whats_new" >&2; exit 1; }
# Normalize explicit absolute paths before hashing bundle-relative filenames.
# Foundation resolves '..' while enumerating, so an unnormalized root can produce
# incorrect manifest keys even though all payload bytes are valid.
plugin_vst3="$(cd "$plugin_vst3" && pwd -P)"
plugin_au="$(cd "$plugin_au" && pwd -P)"
installer_whats_new="$(cd "$(dirname "$installer_whats_new")" && pwd -P)/$(basename "$installer_whats_new")"
mkdir -p "$installer_output"
installer_output="$(cd "$installer_output" && pwd -P)"
if [[ "$installer_output" == "/" || "$installer_output" == "$HOME" ]]; then
    printf 'Choose a dedicated build or release directory.\n' >&2
    exit 1
fi
if [[ -e "$installer_output/Install BACK-SCRATCH.app" ]]; then
    printf 'Output already exists: %s\nUse a fresh output directory.\n' "$installer_output/Install BACK-SCRATCH.app" >&2
    exit 1
fi
installer_work="$(mktemp -d "$installer_output/.backscratch-installer-build.XXXXXX")"
trap 'rm -rf "$installer_work"' EXIT
installer_app="$installer_work/Install BACK-SCRATCH.app"
installer_executable="$installer_app/Contents/MacOS/BACK-SCRATCH-Installer"
mkdir -p "$installer_app/Contents/MacOS" "$installer_app/Contents/Resources/Payload"
cp "$installer_icon" "$installer_app/Contents/Resources/AppIcon.icns"
xcrun swiftc -O -target arm64-apple-macosx12.0 -module-cache-path "$installer_work/module-cache" \
    "$installer_source_dir/InstallerCore.swift" "$installer_source_dir/InstallerGuidance.swift" "$installer_source_dir/InstallerTests.swift" \
    "$installer_source_dir/InstallerApp.swift" "$installer_source_dir/main.swift" \
    -framework AppKit -framework CryptoKit -o "$installer_executable"
ditto --noqtn "$plugin_vst3" "$installer_app/Contents/Resources/Payload/BACK-SCRATCH.vst3"
ditto --noqtn "$plugin_au" "$installer_app/Contents/Resources/Payload/BACK-SCRATCH.component"
# Preserve the supplied plug-in bytes and signatures: verify them, never re-sign the payloads.
for installer_payload in "$installer_app/Contents/Resources/Payload/BACK-SCRATCH.vst3" "$installer_app/Contents/Resources/Payload/BACK-SCRATCH.component"; do
    codesign --verify --deep --strict "$installer_payload" || { printf 'The supplied plug-in is not validly signed: %s\n' "$installer_payload" >&2; exit 1; }
done
"$installer_executable" --check-whats-new "$installer_whats_new"
cp "$installer_whats_new" "$installer_app/Contents/Resources/whats-new.json"
cat > "$installer_app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>com.yoiboi.backscratch.installer</string>
<key>CFBundleExecutable</key><string>BACK-SCRATCH-Installer</string>
<key>CFBundleName</key><string>Install BACK-SCRATCH</string>
<key>CFBundleDisplayName</key><string>Install BACK-SCRATCH</string>
<key>CFBundleIconFile</key><string>AppIcon.icns</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$installer_version</string>
<key>CFBundleVersion</key><string>$installer_build</string>
<key>LSMinimumSystemVersion</key><string>12.0</string>
<key>NSHighResolutionCapable</key><true/>
<key>NSPrincipalClass</key><string>NSApplication</string>
</dict></plist>
EOF
"$installer_executable" --make-manifest "$installer_app/Contents/Resources/Payload" \
    "$installer_app/Contents/Resources/payload-manifest.json" "$installer_version"
codesign --force --sign - "$installer_app"
codesign --verify --deep --strict "$installer_app"
"$installer_executable" --inspect-payload
"$installer_executable" --self-test > "$installer_work/installer-tests.txt"
mv "$installer_app" "$installer_output/Install BACK-SCRATCH.app"
cp "$installer_work/installer-tests.txt" "$installer_output/installer-tests.txt"
printf 'Built and verified: %s\n' "$installer_output/Install BACK-SCRATCH.app"
