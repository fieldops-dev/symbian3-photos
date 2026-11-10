#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/out"
mkdir -p "$out"
c++ -std=c++11 -O2 -Wall -Wextra -Werror -I"$here/shim" -I"$here/../inc" "$here/../src/texcodec.cpp" \
    "$here/texcodec_test.cpp" -o "$out/texcodec_test"
"$out/texcodec_test"
