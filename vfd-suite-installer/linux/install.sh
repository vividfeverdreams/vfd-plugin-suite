#!/usr/bin/env bash
# Vivid Fever Dreams Suite for Linux: installs the six VST3 plug-ins from the VST3 folder next to this script.
#
#   ./install.sh                     all six, for you (~/.vst3)
#   ./install.sh SHIFTY YOI-BOI      only these
#   sudo ./install.sh --system       for every user (/usr/local/lib/vst3)
#   ./install.sh --uninstall         remove them again (add --system for the shared folder)
#
# Each bundle is copied next to its destination first and then renamed into place, so a DAW that has the old
# plug-in loaded keeps running it until you restart the DAW; a half-copied plug-in is never left behind.
set -euo pipefail

plugins=("YOI-BOI" "THROW-UP" "BACK-SCRATCH" "SHIFTY" "ENERGY HARVESTER" "STUTTER-STEP")
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$here/VST3"
target_dir="$HOME/.vst3"
uninstall=0
chosen=()

usage() { sed -n '2,8s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --system) target_dir="/usr/local/lib/vst3" ;;
        --uninstall) uninstall=1 ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
        *)
            name="$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')"
            [ "$name" = "ENERGY-HARVESTER" ] && name="ENERGY HARVESTER"
            known=0
            for plugin in "${plugins[@]}"; do [ "$plugin" = "$name" ] && known=1; done
            if [ "$known" -eq 0 ]; then
                echo "Unknown plug-in: $1 (choose from: ${plugins[*]})" >&2
                exit 2
            fi
            chosen+=("$name")
            ;;
    esac
    shift
done
[ ${#chosen[@]} -eq 0 ] && chosen=("${plugins[@]}")

if [ "$uninstall" -eq 1 ]; then
    for plugin in "${chosen[@]}"; do
        if [ -d "$target_dir/$plugin.vst3" ]; then
            rm -rf -- "$target_dir/$plugin.vst3"
            echo "Removed $target_dir/$plugin.vst3"
        fi
    done
    exit 0
fi

if [ "$(uname -m)" != "x86_64" ]; then
    echo "These plug-ins are built for 64-bit Intel/AMD (x86_64); this computer is $(uname -m)." >&2
    exit 1
fi
for plugin in "${chosen[@]}"; do
    if [ ! -f "$source_dir/$plugin.vst3/Contents/x86_64-linux/$plugin.so" ]; then
        echo "$plugin.vst3 is missing from $source_dir. Unpack the whole download and run install.sh from it." >&2
        exit 1
    fi
done

mkdir -p -- "$target_dir"
for plugin in "${chosen[@]}"; do
    final="$target_dir/$plugin.vst3"
    staged="$target_dir/.$plugin.vst3.new"
    previous="$target_dir/.$plugin.vst3.old"
    rm -rf -- "$staged" "$previous"
    cp -R -- "$source_dir/$plugin.vst3" "$staged"
    [ -e "$final" ] && mv -- "$final" "$previous"
    mv -- "$staged" "$final"
    rm -rf -- "$previous"
    echo "Installed $final"
done

echo
echo "Done. Open your DAW (or restart it if it was open) and rescan plug-ins."
case "$target_dir" in
    "$HOME/.vst3") echo "Most Linux DAWs look in ~/.vst3 already; if yours doesn't list them, add that folder to its VST3 paths." ;;
esac
