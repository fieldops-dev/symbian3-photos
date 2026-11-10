#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/out"
mkdir -p "$out"
c++ -std=c++11 -Wall -Wextra -Werror -I"$here/shim" -I"$here/../inc" "$here/../src/avsync.cpp" \
    "$here/avsync_test.cpp" -o "$out/avsync_test"
"$out/avsync_test"
cc -std=c99 -Wall -Wextra -Werror -I"$here/../stub" "$here/../src/n8mstub.c" "$here/n8mstub_test.c" \
    -o "$out/n8mstub_test"
"$out/n8mstub_test"
