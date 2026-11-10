#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
source "$here/../../../symbian3-toolchain/Scripts/env.sh"
version=4.4.8
src="$N8_SRC/ffmpeg-$version"
cfg="$src/n8-config/host"
out="$here/out"
[[ -f "$cfg/config.h" ]] || { echo "error: run Scripts/fetch-ffmpeg.sh first" >&2; exit 1; }
mkdir -p "$out"

gmake -C "$cfg" -j"$(sysctl -n hw.ncpu)" libavcodec/libavcodec.a libavformat/libavformat.a libavutil/libavutil.a \
  > "$out/ffmpeg-host-build.log" 2>&1 || { tail -20 "$out/ffmpeg-host-build.log"; exit 1; }

cflags=(-O2 -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter -fno-strict-aliasing
        -I"$here/../inc" -I"$here/../src" -I"$cfg" -I"$src" -DHAVE_AV_CONFIG_H -DN8M_ALLOC_HOOK)
cc "${cflags[@]}" -c "$here/../src/n8media.c" -o "$out/n8media.o"
cc "${cflags[@]}" -Wno-format -c "$here/host_decode.c" -o "$out/host_decode.o"
cc -o "$out/host_decode" "$out/host_decode.o" "$out/n8media.o" \
   "$cfg/libavformat/libavformat.a" "$cfg/libavcodec/libavcodec.a" "$cfg/libavutil/libavutil.a" -lm
echo "built: $out/host_decode"
