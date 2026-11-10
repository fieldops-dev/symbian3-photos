#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/out"
mkdir -p "$out"
c++ -std=c++11 -Wall -Wextra -Werror -I"$here/shim" -I"$here/../inc" "$here/../src/exif.cpp" "$here/exif_test.cpp" \
    -o "$out/exif_test"
"$out/exif_test"
