#!/bin/bash
set -euo pipefail

# Developer ID signing and notarization of the plug-in bundles, before they go into the installer.
#
# Usage: sign-plugins.sh "Developer ID Application: NAME (TEAMID)" NOTARY-PROFILE /absolute/output-directory BUNDLE...
#
# Copies each .vst3 / .component bundle into the output directory (the build folders are never touched),
# signs it with the hardened runtime and a secure timestamp, sends all of them to Apple's notary service
# in one submission, then staples the ticket to every bundle and checks it. NOTARY-PROFILE is a keychain
# profile made with `xcrun notarytool store-credentials`. Pass the stapled bundles to build-installer.sh,
# which embeds them byte for byte.
if [[ $# -lt 4 ]]; then
    printf 'Usage: %s "Developer ID Application: NAME (TEAMID)" NOTARY-PROFILE /absolute/output-directory BUNDLE...\n' "$0" >&2
    exit 1
fi
identity="$1"; profile="$2"; output="$3"; shift 3
case "$output" in /*) ;; *) printf 'Output must be an absolute path.\n' >&2; exit 1 ;; esac
[[ -e "$output" ]] && { printf 'Output already exists. Use a fresh directory: %s\n' "$output" >&2; exit 1; }
security find-identity -v -p codesigning | grep -qF "\"$identity\"" || {
    printf 'Signing identity not found in the keychain: %s\n' "$identity" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bundles"
signed=()
for bundle in "$@"; do
    case "$bundle" in /*) ;; *) printf 'Bundle paths must be absolute: %s\n' "$bundle" >&2; exit 1 ;; esac
    case "$bundle" in *.vst3|*.component) ;; *) printf 'Not a .vst3 or .component bundle: %s\n' "$bundle" >&2; exit 1 ;; esac
    copy="$work/bundles/$(basename "$bundle")"
    [[ -e "$copy" ]] && { printf 'Two bundles share the name %s\n' "$(basename "$bundle")" >&2; exit 1; }
    /usr/bin/ditto --noqtn "$bundle" "$copy"
    /usr/bin/codesign --force --options runtime --timestamp --sign "$identity" "$copy"
    /usr/bin/codesign --verify --deep --strict "$copy"
    signed+=("$copy")
done
printf 'Signed %d bundles as %s\n' "${#signed[@]}" "$identity"

(cd "$work/bundles" && /usr/bin/ditto -c -k --sequesterRsrc . "$work/plugins.zip")
xcrun notarytool submit "$work/plugins.zip" --keychain-profile "$profile" --wait --output-format json > "$work/submit.json" || true
status="$(/usr/bin/python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1])).get("status",""))' "$work/submit.json" 2>/dev/null || true)"
submission="$(/usr/bin/python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1])).get("id",""))' "$work/submit.json" 2>/dev/null || true)"
if [[ "$status" != "Accepted" ]]; then
    printf 'Notarization was not accepted (status: %s). Apple'"'"'s log:\n' "${status:-unknown}" >&2
    [[ -n "$submission" ]] && xcrun notarytool log "$submission" --keychain-profile "$profile" >&2 || true
    exit 1
fi
printf 'Notarized (submission %s)\n' "$submission"

for copy in "${signed[@]}"; do
    xcrun stapler staple -q "$copy"
    xcrun stapler validate -q "$copy"
    /usr/bin/codesign --verify --deep --strict "$copy"
done
mkdir -p "$(dirname "$output")"
mv "$work/bundles" "$output"
printf 'Stapled and verified %d bundles: %s\n' "${#signed[@]}" "$output"
