#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/out"
mkdir -p "$out"
c++ -std=c++11 -Wall -Wextra -Werror -I"$here/../tools/shim" -I"$here/../inc" "$here/../src/spinner.cpp" \
    "$here/spinner_preview.cpp" -o "$out/spinner_preview"
"$out/spinner_preview" "$out/spinner.pgm"
echo "wrote $out/spinner.pgm"
