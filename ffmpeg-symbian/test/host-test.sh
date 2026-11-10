#!/bin/bash
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
ff=/opt/homebrew/bin/ffmpeg
dec="$here/out/host_decode"
clips="$here/clips"
tmp="$here/out/cmp"
mkdir -p "$tmp"
[[ -x "$dec" ]] || "$here/host-build.sh"
[[ -d "$clips" ]] || "$here/make-clips.sh"
fail=0

for clip in "$clips"/*; do
  name="$(basename "$clip")"
  "$dec" --count "$clip" > "$tmp/$name.ours" 2> "$tmp/$name.err" || { echo "FAIL $name: $(cat "$tmp/$name.err")"; fail=1; continue; }
  info="$(grep '^info' "$tmp/$name.ours")"
  vcodec="$(sed -n 's/.* video=\([^ ]*\).*/\1/p' <<< "$info")"
  acodec="$(sed -n 's/.* audio=\([^ ]*\).*/\1/p' <<< "$info")"
  rate="$(sed -n 's/.* rate=\([0-9]*\).*/\1/p' <<< "$info")"
  ch="$(sed -n 's/.* ch=\([0-9]*\).*/\1/p' <<< "$info")"
  vres=skip; ares=skip
  if [[ -n "$vcodec" ]]; then
    pixfmt="$(/opt/homebrew/bin/ffprobe -v error -select_streams v:0 -show_entries stream=pix_fmt -of csv=p=0 "$clip")"
    planes="format=yuv420p"; col=4; what="yuv420p"
    [[ "$pixfmt" == *420* ]] || { planes="extractplanes=y"; col=5; what="luma only, $pixfmt"; }
    "$ff" -hide_banner -nostdin -y -loglevel error -threads 1 -flags +bitexact -idct simple -noautorotate -i "$clip" -map 0:v:0 -an \
      -vf "$planes" -flags +bitexact -f framemd5 - 2>/dev/null \
      | grep -v '^#' | awk '{print $NF}' > "$tmp/$name.ref.v"
    grep '^v ' "$tmp/$name.ours" | awk -v c=$col '{print $c}' > "$tmp/$name.our.v"
    if cmp -s "$tmp/$name.ref.v" "$tmp/$name.our.v"; then vres="ok ($(wc -l < "$tmp/$name.our.v" | tr -d ' ') frames, $what)"
    else vres="MISMATCH (ref $(wc -l < "$tmp/$name.ref.v" | tr -d ' ') ours $(wc -l < "$tmp/$name.our.v" | tr -d ' ') frames, first diff line $(cmp "$tmp/$name.ref.v" "$tmp/$name.our.v" | awk '{print $NF}'))"; fail=1; fi
  fi
  if [[ -n "$acodec" ]]; then
    adec=""
    case "$acodec" in aac) adec="-c:a aac_fixed";; mp3) adec="-c:a mp3";; esac
    layout=mono; [[ $ch == 2 ]] && layout=stereo
    "$ff" -hide_banner -nostdin -y -loglevel error -threads 1 -flags +bitexact $adec -i "$clip" -map 0:a:0 -vn \
      -af "aformat=sample_fmts=s16:channel_layouts=$layout" -f s16le "$tmp/$name.ref.pcm" 2>/dev/null
    N8M_PCM_OUT="$tmp/$name.our.pcm" "$dec" "$clip" > /dev/null 2>&1 || true
    if cmp -s <(md5 -q "$tmp/$name.ref.pcm") <(grep '^a ' "$tmp/$name.ours" | awk '{print $5}'); then
      ares="exact ($(grep '^a ' "$tmp/$name.ours" | awk '{print $2}') samples)"
    elif r="$(python3 -I "$here/pcm-compare.py" "$tmp/$name.ref.pcm" "$tmp/$name.our.pcm" "$ch" 2)"; then
      ares="ok ($(sed 's/.*max abs diff/max diff/' <<< "$r"))"
    else
      ares="MISMATCH ($r)"; fail=1
    fi
  fi
  mem="$(grep "^mem " "$tmp/$name.ours" | sed "s/mem //; s/, live 0//")"
  printf '%-26s %-10s %-9s video %-22s audio %-18s %s\n' "$name" "$vcodec" "$acodec" "$vres" "$ares" "$mem"
done

for clip in "$clips"/mpeg4-aac.mp4 "$clips"/mpeg4-mp3-rot90.mp4 "$clips"/h264-baseline-aac.mp4; do
  name="$(basename "$clip")"
  N8M_POSTER_PPM="$tmp/$name.poster.ppm" "$dec" --poster 96 "$clip" > "$tmp/$name.poster" || { echo "FAIL poster $name"; fail=1; }
  printf '%-26s poster %s\n' "$name" "$(cat "$tmp/$name.poster")"
done
"$dec" --seek 2000 "$clips/mpeg4-aac.mp4" > "$tmp/seek.out" || { echo "FAIL seek"; fail=1; }
first="$(grep '^v ' "$tmp/seek.out" | head -1 | awk '{print $3}')"
frames="$(grep -c '^v ' "$tmp/seek.out")"
printf 'seek 2000 ms in mpeg4-aac.mp4: first frame at %s ms, %s frames to the end\n' "$first" "$frames"
[[ "$first" -le 2000 && "$frames" -lt 100 && "$frames" -gt 0 ]] || { echo "FAIL seek: unexpected first pts or frame count"; fail=1; }

exit $fail
