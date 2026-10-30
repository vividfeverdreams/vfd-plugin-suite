#!/bin/bash
# A Mac suite release, Developer ID signed and notarized. Rebuilds every plug-in from its one build folder
# first, then checks that each bundle carries its CMake version, so a stale build can't ship.
#
# Usage: release-mac.sh VERSION [IDENTITY] [NOTARY-PROFILE]
#   VERSION   suite version; run `scripts/suite_versions.py bump VERSION` and edit whats-new-VERSION.json
#             and START HERE.txt first
#   IDENTITY  Developer ID Application certificate, name or SHA-1 (default: Daniel Hopin's)
#   PROFILE   notarytool keychain profile (default: VFD-NOTARY)
#   CMAKE     environment variable: the cmake to build with (default: cmake on PATH)
# Writes ../../outputs/Vivid-Fever-Dreams-Suite-vVERSION{,-Beta-Install,-Beta-Install.zip,-Beta-Install.sha256,-signed-plugins}.
set -euo pipefail
[[ $# -ge 1 ]] || { sed -n '2,11p' "$0" >&2; exit 1; }
V="$1"
ID="${2:-379A73366527F07653D6774A0FAD6FDAB1B891FC}"     # Developer ID Application: Daniel Hopin (LZXM44W38L)
PROFILE="${3:-VFD-NOTARY}"
W="$(cd "$(dirname "$0")/.." && pwd -P)"; O="$(cd "$W/.." && pwd -P)/outputs"
VERSIONS="$W/vfd-suite-installer/scripts/suite_versions.py"
grep -q "VFD_SUITE_VERSION \"$V\"" "$W/shared/VfdSuiteVersion.h" || { echo "shared/VfdSuiteVersion.h is not $V: run scripts/suite_versions.py bump $V" >&2; exit 1; }
python3 "$VERSIONS" check --release || { echo "Versions disagree (see FAIL lines above)." >&2; exit 1; }
CMAKE="${CMAKE:-$(command -v cmake || true)}"
[[ -x "$CMAKE" ]] || { echo "No cmake at $CMAKE (set CMAKE=...)" >&2; exit 1; }
xcrun notarytool history --keychain-profile "$PROFILE" >/dev/null || { echo "No notary profile $PROFILE" >&2; exit 1; }
for old in "Vivid-Fever-Dreams-Suite-v$V" "Vivid-Fever-Dreams-Suite-v$V-Beta-Install" \
           "Vivid-Fever-Dreams-Suite-v$V-Beta-Install.zip" "Vivid-Fever-Dreams-Suite-v$V-Beta-Install.sha256" \
           "Vivid-Fever-Dreams-Suite-v$V-signed-plugins"; do
    [[ -e "$O/$old" ]] && { echo "Release $V already exists in $O ($old). Move it away first." >&2; exit 1; }
done

bundles=()
for p in "yoi-boi|YoiBoi|YOI-BOI" "throw-up|ThrowUp|THROW-UP" "back-scratch|BackScratch|BACK-SCRATCH" \
         "shifty|Shifty|SHIFTY" "energy-harvester|EnergyHarvester|ENERGY HARVESTER" "stutter-step|StutterStep|STUTTER-STEP"; do
    IFS='|' read -r folder target n <<< "$p"
    echo "STEP building $n ($folder/build)"
    "$CMAKE" --build "$W/$folder/build" --config Release --target "${target}_VST3" "${target}_AU" -- -j8 >/dev/null \
        || { echo "Building $n failed; rerun the command above without >/dev/null to see why." >&2; exit 1; }
    d="$W/$folder/build/${target}_artefacts/Release"
    bundles+=("$d/VST3/$n.vst3" "$d/AU/$n.component")
done
python3 "$VERSIONS" check --release --bundles || { echo "A built bundle doesn't carry its CMake version." >&2; exit 1; }

SIGNED="$O/Vivid-Fever-Dreams-Suite-v$V-signed-plugins"
echo "STEP signing and notarizing ${#bundles[@]} plug-in bundles"
bash "$W/vfd-suite-installer/sign-plugins.sh" "$ID" "$PROFILE" "$SIGNED" "${bundles[@]}"


echo "STEP building the installer (signed, notarized, stapled)"
s() { echo "$SIGNED/$1"; }
cd "$W/vfd-suite-installer"
VFD_SIGN_IDENTITY="$ID" VFD_NOTARY_PROFILE="$PROFILE" bash build-installer.sh \
    "$(s YOI-BOI.vst3)" "$(s YOI-BOI.component)" "$(s THROW-UP.vst3)" "$(s THROW-UP.component)" \
    "$(s BACK-SCRATCH.vst3)" "$(s BACK-SCRATCH.component)" "$(s SHIFTY.vst3)" "$(s SHIFTY.component)" \
    "$(s 'ENERGY HARVESTER.vst3')" "$(s 'ENERGY HARVESTER.component')" "$(s STUTTER-STEP.vst3)" "$(s STUTTER-STEP.component)" \
    "$O/Vivid-Fever-Dreams-Suite-v$V" "$V" "$W/vfd-suite-installer/whats-new-$V.json"

SRC="$O/Vivid-Fever-Dreams-Suite-v$V"
cp "$W/vfd-suite-installer/START HERE.txt" "$SRC/START HERE.txt"
B="$O/Vivid-Fever-Dreams-Suite-v$V-Beta-Install"
mkdir "$B"
ditto "$SRC/Install Vivid Fever Dreams Suite.app" "$B/Install Vivid Fever Dreams Suite.app"
cp "$SRC/README.md" "$SRC/START HERE.txt" "$B/"
cd "$O"
ditto -c -k --keepParent "Vivid-Fever-Dreams-Suite-v$V-Beta-Install" "Vivid-Fever-Dreams-Suite-v$V-Beta-Install.zip"
shasum -a 256 "Vivid-Fever-Dreams-Suite-v$V-Beta-Install.zip" > "Vivid-Fever-Dreams-Suite-v$V-Beta-Install.sha256"

echo "STEP verifying like a downloaded copy"
T="$(mktemp -d)"
cp "Vivid-Fever-Dreams-Suite-v$V-Beta-Install.zip" "$T/"
xattr -w com.apple.quarantine "0081;$(printf %x "$(date +%s)");Safari;" "$T/Vivid-Fever-Dreams-Suite-v$V-Beta-Install.zip"
(cd "$T" && ditto -x -k "Vivid-Fever-Dreams-Suite-v$V-Beta-Install.zip" .)
APP="$T/Vivid-Fever-Dreams-Suite-v$V-Beta-Install/Install Vivid Fever Dreams Suite.app"
xattr -w com.apple.quarantine "0081;$(printf %x "$(date +%s)");Safari;" "$APP"
spctl --assess --type execute -vv "$APP" 2>&1
xcrun stapler validate "$APP" 2>&1 | tail -1
codesign -dvv "$APP" 2>&1 | grep -E "^Authority=Developer ID|TeamIdentifier|Runtime Version|flags"
for b in "$APP"/Contents/Resources/Payload/*; do
    printf '%s: ' "$(basename "$b")"
    xcrun stapler validate -q "$b" && printf 'stapled, ' || printf 'NOT STAPLED, '
    flags="$(codesign -dv "$b" 2>&1 || true)"
    [[ "$flags" == *"(runtime)"* ]] && printf 'hardened runtime\n' || printf 'NO RUNTIME\n'
done
rm -rf "$T"
echo "DONE"
