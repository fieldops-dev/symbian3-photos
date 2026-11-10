#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:-$here/clips}"
ff=/opt/homebrew/bin/ffmpeg
mkdir -p "$out"
common=(-hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=320x240:rate=25" -f lavfi -i "sine=frequency=440:sample_rate=44100" -t 4 -shortest -flags +bitexact -fflags +bitexact)

clip() {
  local name="$1"; shift
  [[ -f "$out/$name" ]] && { echo "skip: $name"; return; }
  "$ff" "${common[@]}" "$@" "$out/$name"
  echo "made: $name"
}

clip mpeg4-aac.mp4        -c:v mpeg4 -q:v 4 -c:a aac -b:a 96k
clip h264-baseline-aac.mp4 -c:v libx264 -profile:v baseline -level 3.0 -preset fast -crf 23 -x264-params "threads=1:sliced-threads=0" -c:a aac -b:a 96k
clip h264-main-aac.mp4    -c:v libx264 -profile:v main -level 3.0 -preset fast -crf 23 -x264-params "threads=1:sliced-threads=0:ref=2:bframes=2" -c:a aac -b:a 96k
clip h263-aac.3gp         -s 176x144 -c:v h263 -b:v 200k -c:a aac -ar 32000 -ac 1 -b:a 48k
clip mjpeg-mp3.avi        -c:v mjpeg -q:v 5 -pix_fmt yuvj420p -c:a libmp3lame -b:a 96k
clip mjpeg422-pcm.avi     -c:v mjpeg -q:v 5 -pix_fmt yuvj422p -c:a pcm_s16le
clip mpeg4-mp3.mp4        -c:v mpeg4 -q:v 4 -c:a libmp3lame -b:a 96k
[[ -f "$out/mpeg4-mp3-rot90.mp4" ]] || "$ff" -hide_banner -loglevel error -y -display_rotation -90 -i "$out/mpeg4-mp3.mp4" -c copy "$out/mpeg4-mp3-rot90.mp4"
clip h264-aac.ts          -c:v libx264 -profile:v baseline -preset fast -crf 23 -x264-params "threads=1" -c:a aac -b:a 96k -f mpegts
clip mpeg4-640.mp4        -s 640x360 -c:v mpeg4 -q:v 4 -c:a aac -b:a 96k

amr="$out/amrnb-4.75k.3gp"
if [[ ! -f "$amr" ]]; then
  tmp="$out/.4.75k.amr"
  curl -fsSL -o "$tmp" https://fate-suite.ffmpeg.org/amrnb/4.75k.amr
  echo "02f83e3d2cf78d9224e57d2441e521f8063eeea5d92de67b4e574e6b3f229973  $tmp" | shasum -a 256 -c - > /dev/null
  "$ff" -hide_banner -loglevel error -y -i "$tmp" -c:a copy -t 4 "$amr"
  rm -f "$tmp"
  echo "made: $(basename "$amr")"
fi
