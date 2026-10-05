#!/bin/bash
set -euo pipefail

# Usage: bash verify-archives.sh RELEASE_010.zip RELEASE_011.zip RELEASE_012.zip
# No installed plugin or preset data is accessed or changed by this check.
if [[ $# -ne 3 ]]; then
    echo "Usage: $0 RELEASE_010.zip RELEASE_011.zip RELEASE_012.zip" >&2
    exit 2
fi

test_dir="$(cd "$(dirname "$0")" && pwd)"
legacy_dir="$(cd "$test_dir/.." && pwd)"
verify_dir="$(mktemp -d "${TMPDIR:-/tmp}/yoi-boi-legacy-check.XXXXXX")"
trap 'rm -rf "$verify_dir"' EXIT
compiler="${CXX:-clang++}"

extract_voice() {
    local archive="$1" version="$2" destination="$3"
    mkdir -p "$destination"
    unzip -p "$archive" "YOI-BOI-source-v${version}/Source/Dsp/YoiVoice.h" > "$destination/YoiVoice.h"
    unzip -p "$archive" "YOI-BOI-source-v${version}/Source/Dsp/YoiVoice.cpp" > "$destination/YoiVoice.cpp"
}

extract_voice "$1" 0.1.0 "$verify_dir/010"
extract_voice "$2" 0.1.1 "$verify_dir/011"
extract_voice "$3" 0.1.2 "$verify_dir/012"
cmp "$verify_dir/010/YoiVoice.h" "$verify_dir/011/YoiVoice.h"
cmp "$verify_dir/010/YoiVoice.cpp" "$verify_dir/011/YoiVoice.cpp"
echo "0.1.0 and 0.1.1 engine source is byte-identical."

for version in 010 012; do
    for extension in h cpp; do
        # The production copies are required to differ only in namespace and
        # header basename. Fail before rendering if anything else has changed.
        sed -e "s/namespace yoiboi::dsp::legacy${version}/namespace yoiboi::dsp/g" \
            -e "s/\"YoiVoice${version}\.h\"/\"YoiVoice.h\"/g" \
            "$legacy_dir/YoiVoice${version}.${extension}" > "$verify_dir/normalized.${extension}"
        cmp "$verify_dir/${version}/YoiVoice.${extension}" "$verify_dir/normalized.${extension}"
    done
    "$compiler" -std=c++20 -O2 -Wall -Wextra -pedantic \
        -I "$verify_dir/$version" "$test_dir/LegacyRender.cpp" \
        "$verify_dir/$version/YoiVoice.cpp" -o "$verify_dir/reference-$version"
    "$compiler" -std=c++20 -O2 -Wall -Wextra -pedantic \
        "-DYOIBOI_TEST_LEGACY$version=1" "$test_dir/LegacyRender.cpp" \
        "$legacy_dir/YoiVoice$version.cpp" -o "$verify_dir/preserved-$version"
    "$verify_dir/reference-$version" > "$verify_dir/reference-$version.txt"
    "$verify_dir/preserved-$version" > "$verify_dir/preserved-$version.txt"
    cmp "$verify_dir/reference-$version.txt" "$verify_dir/preserved-$version.txt"
    echo "legacy$version: source preservation and 126 exact audio/gesture comparisons passed."
    LC_ALL=C shasum -a 256 "$verify_dir/preserved-$version.txt" | cut -d ' ' -f 1
done
