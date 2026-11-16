#!/bin/bash
# The separate Intel Mac suite release, Developer ID signed and notarized. Same sources and JUCE as the
# Apple Silicon release (release-mac.sh), built x86_64-only in each plug-in's build-intel folder, so the
# Apple Silicon download stays exactly as it is. Run it after release-mac.sh, from the same commit.
#
# Usage: release-mac-intel.sh VERSION [IDENTITY] [NOTARY-PROFILE]
#   VERSION         suite version, e.g. 1.6.4
#   IDENTITY        Developer ID Application identity (default: the suite's, as in release-mac.sh)
#   NOTARY-PROFILE  notarytool keychain profile (default: VFD-NOTARY)
#   CMAKE           environment variable: the cmake to build with (default: cmake on PATH)
#   VFD_JUCE_FROM   environment variable: the source tree whose <plug-in>/build/CMakeCache.txt names the JUCE
#                   each plug-in builds with (default: this tree). Set it when building from an exported copy.
#   VFD_OUTPUTS     environment variable: where releases go (default: ../../outputs, as in release-mac.sh)
# Writes OUTPUTS/Vivid-Fever-Dreams-Suite-vVERSION-Intel{,-signed-plugins}, -Beta-Intel-Mac{,.zip,.sha256}.
# The download is named ...-Intel-Mac.zip on purpose: the plug-ins' updater on Apple Silicon only takes a Mac
# download ending in -Install.zip, and the Intel plug-ins only take one ending in -Intel-Mac.zip.
set -euo pipefail
[[ $# -ge 1 ]] || { sed -n '2,16p' "$0" >&2; exit 1; }
V="$1"
ID="${2:-379A73366527F07653D6774A0FAD6FDAB1B891FC}"
PROFILE="${3:-VFD-NOTARY}"
W="$(cd "$(dirname "$0")/.." && pwd -P)"
JF="${VFD_JUCE_FROM:-$W}"
O="${VFD_OUTPUTS:-$(cd "$W/.." && pwd -P)/outputs}"
VERSIONS="$W/vfd-suite-installer/scripts/suite_versions.py"
export VFD_BUILD_DIR=build-intel
grep -q "VFD_SUITE_VERSION \"$V\"" "$W/shared/VfdSuiteVersion.h" || { echo "shared/VfdSuiteVersion.h is not $V" >&2; exit 1; }
python3 "$VERSIONS" check --release || { echo "Versions disagree (see FAIL lines above)." >&2; exit 1; }
CMAKE="${CMAKE:-$(command -v cmake || true)}"
[[ -x "$CMAKE" ]] || { echo "No cmake at $CMAKE (set CMAKE=...)" >&2; exit 1; }
xcrun notarytool history --keychain-profile "$PROFILE" >/dev/null || { echo "No notary profile $PROFILE" >&2; exit 1; }
N="Vivid-Fever-Dreams-Suite-v$V"
for old in "$N-Intel" "$N-Intel-signed-plugins" "$N-Beta-Intel-Mac" "$N-Beta-Intel-Mac.zip" "$N-Beta-Intel-Mac.sha256"; do
    [[ -e "$O/$old" ]] && { echo "Intel release $V already exists in $O ($old). Move it away first." >&2; exit 1; }
done

bundles=()
for p in "yoi-boi|YoiBoi|YOI-BOI|YOIBOI_BUILD_TESTS" "throw-up|ThrowUp|THROW-UP|THROW_UP_BUILD_TESTS" \
         "back-scratch|BackScratch|BACK-SCRATCH|BACK_SCRATCH_BUILD_TESTS" "shifty|Shifty|SHIFTY|SHIFTY_BUILD_TESTS" \
         "energy-harvester|EnergyHarvester|ENERGY HARVESTER|ENERGY_HARVESTER_BUILD_TESTS" \
         "stutter-step|StutterStep|STUTTER-STEP|STUTTER_BUILD_TESTS"; do
    IFS='|' read -r folder target n tests <<< "$p"
    B="$W/$folder/build-intel"
    if [[ ! -f "$B/CMakeCache.txt" ]]; then
        echo "STEP configuring $n for Intel ($folder/build-intel)"
        extra=()
        if [[ "$folder" != energy-harvester ]]; then   # ENERGY HARVESTER always builds its own external/JUCE
            juce="$(sed -n 's/^JUCE_SOURCE_DIR:STATIC=//p' "$JF/$folder/build/CMakeCache.txt" 2>/dev/null || true)"
            [[ -f "$juce/CMakeLists.txt" ]] || { echo "No JUCE found for $n in $JF/$folder/build/CMakeCache.txt" >&2; exit 1; }
            extra=(-DJUCE_SOURCE_DIR="$juce")
        fi
        "$CMAKE" -S "$W/$folder" -B "$B" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_OSX_ARCHITECTURES=x86_64 "-D$tests=OFF" ${extra[@]+"${extra[@]}"} >/dev/null
    fi
    grep -q '^CMAKE_OSX_ARCHITECTURES:STRING=x86_64$' "$B/CMakeCache.txt" || { echo "$folder/build-intel isn't x86_64" >&2; exit 1; }
    echo "STEP building $n for Intel"
    "$CMAKE" --build "$B" --config Release --target "${target}_VST3" "${target}_AU" -- -j8 >/dev/null \
        || { echo "Building $n failed; rerun the command above without >/dev/null to see why." >&2; exit 1; }
    d="$B/${target}_artefacts/Release"
    for b in "$d/VST3/$n.vst3" "$d/AU/$n.component"; do
        a="$(lipo -archs "$b/Contents/MacOS/$n")"
        [[ "$a" == x86_64 ]] || { echo "$b is '$a', not x86_64" >&2; exit 1; }
        bundles+=("$b")
    done
done
python3 "$VERSIONS" check --release --bundles || { echo "A built bundle doesn't carry its CMake version." >&2; exit 1; }

SIGNED="$O/$N-Intel-signed-plugins"
echo "STEP signing and notarizing ${#bundles[@]} Intel plug-in bundles"
bash "$W/vfd-suite-installer/sign-plugins.sh" "$ID" "$PROFILE" "$SIGNED" "${bundles[@]}"

echo "STEP building the Intel installer (signed, notarized, stapled)"
s() { echo "$SIGNED/$1"; }
cd "$W/vfd-suite-installer"
VFD_INSTALLER_ARCH=x86_64 VFD_SIGN_IDENTITY="$ID" VFD_NOTARY_PROFILE="$PROFILE" bash build-installer.sh \
    "$(s YOI-BOI.vst3)" "$(s YOI-BOI.component)" "$(s THROW-UP.vst3)" "$(s THROW-UP.component)" \
    "$(s BACK-SCRATCH.vst3)" "$(s BACK-SCRATCH.component)" "$(s SHIFTY.vst3)" "$(s SHIFTY.component)" \
    "$(s 'ENERGY HARVESTER.vst3')" "$(s 'ENERGY HARVESTER.component')" "$(s STUTTER-STEP.vst3)" "$(s STUTTER-STEP.component)" \
    "$O/$N-Intel" "$V" "$W/vfd-suite-installer/whats-new-$V.json"

# The download: the app, README and START HERE, worded for Intel.
SRC="$O/$N-Intel"
intel_text() {
    sed -e 's/^Apple Silicon Mac (M1 or later) and macOS 12 or later\.$/Intel Mac and macOS 12 or later. (Apple Silicon Macs: use the Apple Silicon download, ...-Install.zip.)/' \
        -e 's/Requires \*\*macOS 12 or later on Apple Silicon\*\*\. This package does not provide Intel or AAX builds\./Requires **macOS 12 or later on an Intel Mac**. This is the Intel package; Apple Silicon Macs use the Apple Silicon download (...-Install.zip). No AAX builds./' "$1"
}
intel_text "$W/vfd-suite-installer/START HERE.txt" > "$SRC/START HERE.txt"
intel_text "$SRC/README.md" > "$SRC/README.intel.md" && mv "$SRC/README.intel.md" "$SRC/README.md"
grep -q '^Intel Mac and macOS 12' "$SRC/START HERE.txt" || { echo "START HERE.txt wasn't reworded for Intel" >&2; exit 1; }
grep -q 'on an Intel Mac' "$SRC/README.md" || { echo "README.md wasn't reworded for Intel" >&2; exit 1; }
Z="$O/$N-Beta-Intel-Mac"
mkdir "$Z"
ditto "$SRC/Install Vivid Fever Dreams Suite.app" "$Z/Install Vivid Fever Dreams Suite.app"
cp "$SRC/README.md" "$SRC/START HERE.txt" "$Z/"
cd "$O"
ditto -c -k --keepParent "$N-Beta-Intel-Mac" "$N-Beta-Intel-Mac.zip"
shasum -a 256 "$N-Beta-Intel-Mac.zip" > "$N-Beta-Intel-Mac.sha256"

echo "STEP verifying like a downloaded copy"
T="$(mktemp -d)"
cp "$N-Beta-Intel-Mac.zip" "$T/"
xattr -w com.apple.quarantine "0081;$(printf %x "$(date +%s)");Safari;" "$T/$N-Beta-Intel-Mac.zip"
(cd "$T" && ditto -x -k "$N-Beta-Intel-Mac.zip" .)
APP="$T/$N-Beta-Intel-Mac/Install Vivid Fever Dreams Suite.app"
xattr -w com.apple.quarantine "0081;$(printf %x "$(date +%s)");Safari;" "$APP"
spctl --assess --type execute -vv "$APP" 2>&1
xcrun stapler validate "$APP" 2>&1 | tail -1
codesign -dvv "$APP" 2>&1 | grep -E "^Authority=Developer ID|TeamIdentifier|Runtime Version|flags"
printf 'installer: %s\n' "$(lipo -archs "$APP/Contents/MacOS/VFD-Suite-Installer")"
for b in "$APP"/Contents/Resources/Payload/*; do
    printf '%s: %s, ' "$(basename "$b")" "$(lipo -archs "$b/Contents/MacOS/"*)"
    xcrun stapler validate -q "$b" && printf 'stapled, ' || printf 'NOT STAPLED, '
    flags="$(codesign -dv "$b" 2>&1 || true)"
    [[ "$flags" == *"(runtime)"* ]] && printf 'hardened runtime\n' || printf 'NO RUNTIME\n'
done
rm -rf "$T"
echo "DONE"
