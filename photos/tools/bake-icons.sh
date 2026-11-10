#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/../test/out"
mkdir -p "$out"
c++ -Wall -Werror -I "$here/shim" -I "$here/../inc" "$here/bake_icons.cpp" "$here/../src/iconshapes.cpp" \
    -o "$out/bake_icons"
"$out/bake_icons" > "$here/../src/iconbake.cpp"
