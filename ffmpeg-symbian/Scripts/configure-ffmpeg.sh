#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
source "$here/../../../symbian3-toolchain/Scripts/env.sh"

version=4.4.8
src="$N8_SRC/ffmpeg-$version"
epoc="${EPOCROOT}epoc32"
[[ -f "$src/configure" ]] || { echo "error: $src not unpacked (run Scripts/fetch-ffmpeg.sh)" >&2; exit 1; }

components=(
  --disable-everything --disable-programs --disable-doc --disable-network
  --disable-avfilter --disable-swscale --disable-swresample --disable-avdevice --disable-postproc --disable-avresample
  --disable-pthreads --disable-w32threads --disable-os2threads
  --disable-autodetect --disable-iconv --disable-zlib --disable-debug --disable-stripping
  --disable-hwaccels --disable-encoders --disable-muxers --disable-filters --disable-bsfs
  --enable-demuxer=mov,avi,mpegts
  --enable-decoder=mpeg4,h263,mjpeg,h264,aac_fixed,mp3,amrnb
  --enable-decoder=pcm_s16le,pcm_s16be,pcm_u8,pcm_s8,pcm_alaw,pcm_mulaw
  --enable-parser=mpeg4video,h263,mjpeg,h264,aac,mpegaudio
  --enable-protocol=file
  --enable-small
)

echo "configure (arm): $src/n8-config/arm"
mkdir -p "$src/n8-config/arm"
( cd "$src/n8-config/arm"
  ../../configure --enable-cross-compile --target-os=symbian --arch=arm --cpu=armv5te --disable-vfp --disable-neon \
    --cross-prefix=arm-none-symbianelf- --sysinclude="$epoc/include" \
    --extra-cflags="-nostdinc -I$epoc/include/stdapis -I$epoc/include -D__EPOC32__ -D__MARM__ -D__EABI__ -D__MARM_ARMV5__ -D__EXE__ -fno-strict-aliasing -msoft-float -mapcs -mthumb-interwork -O2" \
    --extra-ldflags="-L$epoc/release/armv5/lib -L$epoc/release/armv5/urel" \
    "${components[@]}" > configure.log 2>&1 || { tail -5 configure.log; exit 1; }
)

echo "configure (host): $src/n8-config/host"
mkdir -p "$src/n8-config/host"
( cd "$src/n8-config/host"
  ../../configure --disable-asm --disable-x86asm \
    --extra-cflags="-fno-strict-aliasing -O2 -w -DN8M_ALLOC_HOOK -I$here/../src" \
    "${components[@]}" > configure.log 2>&1 || { tail -5 configure.log; exit 1; }
)

echo "build lists: $src/n8-config/arm/n8-src, group/ffmpeg_sources*.mmph"
python3 -I "$here/gen-ffmpeg-build.py" "$src" "$src/n8-config/arm" "$here/../group"
