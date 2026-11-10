#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/out"
mkdir -p "$out"
c++ -std=c++11 -Wall -Wextra -Werror -I"$here/shim" -I"$here/../inc" "$here/../src/mp4.cpp" "$here/mp4_test.cpp" \
    -o "$out/mp4_test"
"$out/mp4_test"
