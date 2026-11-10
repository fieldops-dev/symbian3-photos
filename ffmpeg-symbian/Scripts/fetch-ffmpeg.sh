#!/bin/bash
set -euo pipefail
shopt -s nullglob
here="$(cd "$(dirname "$0")" && pwd)"
source "$here/../../../symbian3-toolchain/Scripts/env.sh"

version=4.4.8
sha256=c73848c4ae283d9eaee7be3b276affbc3543380483555500d0dd2c9b7e1c39c3
tarball="$N8_DOWNLOADS/ffmpeg-$version.tar.xz"
dest="$N8_SRC/ffmpeg-$version"
patches="$here/../patches/ffmpeg"
stamp="$dest/.n8-prepared"

want="$(cat "$patches"/*.patch "$0" "$here/configure-ffmpeg.sh" "$here/gen-ffmpeg-build.py" 2>/dev/null \
        | shasum -a 256 | cut -d' ' -f1)"
if [[ -z "${FORCE:-}" && -f "$stamp" && "$(cat "$stamp")" == "$want" ]]; then
  echo "skip: $dest already prepared"
  exit 0
fi

mkdir -p "$N8_DOWNLOADS" "$N8_SRC"
if [[ ! -f "$tarball" ]] || ! echo "$sha256  $tarball" | shasum -a 256 -c - > /dev/null 2>&1; then
  curl -fL -C - -o "$tarball" "https://ffmpeg.org/releases/ffmpeg-$version.tar.xz"
fi
echo "$sha256  $tarball" | shasum -a 256 -c -

rm -rf "$dest"
tar -xJf "$tarball" -C "$N8_SRC"
cd "$dest"
for p in "$patches"/*.patch; do
  patch -p1 -s --no-backup-if-mismatch < "$p"
  echo "patched: $(basename "$p")"
done

"$here/configure-ffmpeg.sh"
echo "$want" > "$stamp"
echo "prepared: $dest"
