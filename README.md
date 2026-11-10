<p align="center">
  <img src=".github/icon.png" width="128" alt="Photos icon">
</p>

<h1 align="center">Photos</h1>

<p align="center">A modern media gallery for Symbian^3, Anna, and Belle.</p>

## Features

- Library by years, months and days, with pinch to zoom between them
- Favourites, albums, Hidden and Recently Deleted (kept for 30 days)
- Full-screen viewer with pinch and double-tap zoom, and swipe down to close
- Video playback through the phone's player, with an FFmpeg fallback for MP4, MOV, 3GP, AVI and MPEG-TS
- Animated GIFs
- Sharing through the system Send menu (Bluetooth, MMS, email)
- Hardware-accelerated rendering with OpenGL ES 2.0

## Install

1. Download `n8photos.sisx` from [Releases](../../releases).
2. On the phone, set **App manager → Settings → Software installation** to **All**.
3. Open the file and accept the self-signed certificate warning.

Tested on the Nokia N8 with Belle Refresh.

## Permissions

| Capability | Used for |
|---|---|
| `NetworkServices` | Sharing by MMS or email |
| `LocalServices` | Sharing over Bluetooth |
| `ReadUserData`, `WriteUserData` | The messaging part of the Send menu |

Photos never connects to a network on its own. Browsing and playback need none of these.

## Build

Requires [symbian3-toolchain](https://github.com/fieldops-dev/symbian3-toolchain), cloned next to this repository and set up as described in its README.

```sh
source ../symbian3-toolchain/Scripts/env.sh

cd ffmpeg-symbian
Scripts/fetch-ffmpeg.sh
(cd group && bldmake bldfiles && abld build gcce urel)

cd ../photos
(cd group && bldmake bldfiles && abld build gcce urel)
N9_FONTS=/path/to/fonts/nokia sis/stage-fonts.sh
$N8_REPO/Scripts/package-sis.sh sis/n8photos.pkg
```

The Nokia Pure fonts are not included. Set `N9_FONTS` to the `fonts/nokia` folder of an N9 firmware image, and set
`NP_HEADLINE_BOLD` if `NokiaPureHeadline-Bold.ttf` is not in `/Library/Fonts`.

## Repository layout

| Path | Contents |
|---|---|
| `photos/` | The app |
| `ffmpeg-symbian/` | FFmpeg 4.4 port: static decoding libraries with a small C API |

## License

[MIT](LICENSE). FFmpeg is licensed under the [LGPL 2.1](ffmpeg-symbian/COPYING.LGPLv2.1) or later; every change made
to it is in `ffmpeg-symbian/patches/ffmpeg/`.

Nokia, Symbian and Nokia Pure are trademarks of their respective owners.
