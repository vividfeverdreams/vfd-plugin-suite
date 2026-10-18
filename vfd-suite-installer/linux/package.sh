#!/usr/bin/env bash
# Packs the six Linux VST3 bundles with install.sh and README.txt into
# <out>/Vivid-Fever-Dreams-Suite-v<version>-Beta-Linux-x64.tar.gz. Used by .github/workflows/linux.yml:
#   package.sh <folder of .vst3 bundles> <suite version> <out folder>
set -euo pipefail

if [ $# -ne 3 ]; then
    sed -n '4s/^# \{0,1\}//p' "${BASH_SOURCE[0]}" >&2
    exit 2
fi
bundles="$1" version="$2" out="$3"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
name="Vivid-Fever-Dreams-Suite-v$version-Beta-Linux-x64"
plugins=("YOI-BOI" "THROW-UP" "BACK-SCRATCH" "SHIFTY" "ENERGY HARVESTER" "STUTTER-STEP")

stage="$(mktemp -d)"
trap 'rm -rf -- "$stage"' EXIT
mkdir -p "$stage/$name/VST3" "$out"
for plugin in "${plugins[@]}"; do
    so="$bundles/$plugin.vst3/Contents/x86_64-linux/$plugin.so"
    [ -f "$so" ] || { echo "Missing $so" >&2; exit 1; }
    cp -R -- "$bundles/$plugin.vst3" "$stage/$name/VST3/"
done
install -m 755 "$here/install.sh" "$stage/$name/install.sh"
install -m 644 "$here/README.txt" "$stage/$name/README.txt"

# Same bytes for the same bundles: sorted entries, fixed owner and times.
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime='2026-01-01 00:00:00Z' \
    -C "$stage" -czf "$out/$name.tar.gz" "$name"
echo "$out/$name.tar.gz"
