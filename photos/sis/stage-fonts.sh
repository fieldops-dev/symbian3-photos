#!/bin/bash
set -euo pipefail
: "${EPOCROOT:?source ../../symbian3-toolchain/Scripts/env.sh first}"
n9="${N9_FONTS:?set N9_FONTS to the fonts/nokia folder of an N9 firmware (see README)}"
headline="${NP_HEADLINE_BOLD:-/Library/Fonts/NokiaPureHeadline-Bold.ttf}"
dest="${EPOCROOT}epoc32/data/z/resource/apps/n8photos_fonts"
mkdir -p "$dest"
rm -f "$dest/NPTextBold.ttf" "$dest/NPTextMedium.ttf" "$dest/NPTextRegular.ttf" "$dest/N9TextMEMd.ttf"
cp -f "$headline" "$dest/NPHeadlineBold.ttf"
cp -f "$n9/Nokia Pure/Proportional/NokiaPureText_Bd.ttf" "$dest/N9TextBd.ttf"
cp -f "$n9/Nokia Pure/Tabular/NokiaPureTextTBoldEuro.ttf" "$dest/N9TextTBd.ttf"
cp -f "$n9/Nokia Pure/Tabular/NokiaPureTextTRegEuro.ttf" "$dest/N9TextTRg.ttf"
cp -f "$n9/Nokia Pure/Proportional/NokiaPureText_Rg.ttf" "$dest/N9TextRg.ttf"
ls -1 "$dest"
