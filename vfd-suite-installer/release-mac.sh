#!/bin/bash
# A Mac suite release, Developer ID signed and notarized, from the current local builds.
#
# Usage: release-mac.sh VERSION [IDENTITY] [NOTARY-PROFILE]
#   VERSION   suite version; needs whats-new-VERSION.json and START HERE.txt (edit both first)
#   IDENTITY  Developer ID Application certificate, name or SHA-1 (default: Daniel Hopin's)
#   PROFILE   notarytool keychain profile (default: VFD-NOTARY)
# Writes ../../outputs/Vivid-Fever-Dreams-Suite-vVERSION{,-Beta-Install,-Beta-Install.zip,-Beta-Install.sha256,-signed-plugins}.
set -euo pipefail
[[ $# -ge 1 ]] || { sed -n '2,8p' "$0" >&2; exit 1; }
V="$1"
ID="${2:-379A73366527F07653D6774A0FAD6FDAB1B891FC}"     # Developer ID Application: Daniel Hopin (LZXM44W38L)
PROFILE="${3:-VFD-NOTARY}"
W="$(cd "$(dirname "$0")/.." && pwd -P)"; O="$(cd "$W/.." && pwd -P)/outputs"
[[ -f "$W/vfd-suite-installer/whats-new-$V.json" ]] || { echo "Missing whats-new-$V.json" >&2; exit 1; }
grep -q "SUITE $V " "$W/vfd-suite-installer/START HERE.txt" || { echo "START HERE.txt is not for $V" >&2; exit 1; }
xcrun notarytool history --keychain-profile "$PROFILE" >/dev/null || { echo "No notary profile $PROFILE" >&2; exit 1; }
for old in "Vivid-Fever-Dreams-Suite-v$V" "Vivid-Fever-Dreams-Suite-v$V-Beta-Install" \
           "Vivid-Fever-Dreams-Suite-v$V-Beta-Install.zip" "Vivid-Fever-Dreams-Suite-v$V-Beta-Install.sha256" \
           "Vivid-Fever-Dreams-Suite-v$V-signed-plugins"; do
    [[ -e "$O/$old" ]] && { echo "Release $V already exists in $O ($old). Move it away first." >&2; exit 1; }
done

bundles=()
for p in "yoi-boi/build/YoiBoi_artefacts|YOI-BOI" "throw-up/build/ThrowUp_artefacts|THROW-UP" \
         "back-scratch/build/BackScratch_artefacts|BACK-SCRATCH" "shifty/build-v041/Shifty_artefacts|SHIFTY" \
         "energy-harvester/build/EnergyHarvester_artefacts|ENERGY HARVESTER" "stutter-step/build/StutterStep_artefacts|STUTTER-STEP"; do
    d="$W/${p%%|*}/Release"; n="${p##*|}"
    bundles+=("$d/VST3/$n.vst3" "$d/AU/$n.component")
done

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
