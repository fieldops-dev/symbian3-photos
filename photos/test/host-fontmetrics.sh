#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/out"
mkdir -p "$out"
c++ -std=c++11 -Wall -Wextra -Werror -I"$here/shim" -I"$here/../inc" "$here/../src/fontmetrics.cpp" \
    "$here/fontmetrics_test.cpp" -o "$out/fontmetrics_test"
n9="${N9_FONTS:-}"
fonts=()
for f in "$n9/Nokia Pure/Proportional/NokiaPureText_Bd.ttf" "$n9/Nokia Pure/Tabular/NokiaPureTextTBoldEuro.ttf" \
         "$n9/Nokia Pure/Tabular/NokiaPureTextTRegEuro.ttf"; do
    [ -f "$f" ] && fonts+=("$f")
done
"$out/fontmetrics_test" ${fonts[@]+"${fonts[@]}"}
