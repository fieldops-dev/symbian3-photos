#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/out/shaders"
mkdir -p "$out"
python3 -I "$here/extract_shaders.py" "$here/../src/glrenderer.cpp" "$out" > /dev/null
status=0
if ! python3 -I - "$out"/*.vert "$out"/*.frag <<'PY'
import re, sys
bad = 0
for path in sys.argv[1:]:
    for number, line in enumerate(open(path), 1):
        if not line.lstrip().startswith('#') and re.search(r'(?<![\w.])\d+(?![\w.])', line):
            print('%s:%d: %s' % (path, number, line.rstrip()))
            bad = 1
sys.exit(bad)
PY
then
    echo "bare integer literals above (write 1.0, not 1)"
    status=1
fi
cc -Wall -Werror -framework OpenGL "$here/check_shaders.c" -o "$here/out/check_shaders"
"$here/out/check_shaders" "$out/KQuadVs.vert" "$out/KQuadFs.frag" "$out/KQuadVs.vert" "$out/KBlurFs.frag" \
    "$out/KQuadVs.vert" "$out/KVideoFs.frag" "$out/KQuadVs.vert" "$out/KTextFs.frag" || status=1
exit $status
