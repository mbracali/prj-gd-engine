#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# The macOS download (dist/gd02-macos/gd02.app, SDL3 and FFmpeg's libraries
# inside the app), built on a Mac with Homebrew: brew install cmake sdl3 ninja
# dylibbundler (FFmpeg is built from source, see build_common.sh).
# Ad-hoc signed, not notarised: the first start is right-click > Open.
set -euo pipefail
cd "$(dirname "$0")"
. ./build_common.sh

build_ffmpeg build/ffmpeg/macos macOS --disable-x86asm

say "gd02 (macOS): configuring (CMake)..."
configure -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DGD02_FFMPEG_DIR="$PWD/build/ffmpeg/macos/ffmpeg-prefix"
compile build/macos "gd02 (macOS)"

say "Assembling dist/gd02-macos (app with SDL3 and FFmpeg inside, ad-hoc signed)..."
d=dist/gd02-macos
rm -rf "$d" && mkdir -p "$d/licenses"
cp -R build/macos/gd02.app "$d/"
app="$d/gd02.app"
dylibbundler -od -b -x "$app/Contents/MacOS/gd02" -d "$app/Contents/Frameworks/" -p @executable_path/../Frameworks/ >/dev/null
codesign --force --deep --sign - "$app"
cp README.md ../../LICENSE ../../LICENSE-ADDITIONAL-TERMS.md "$d/"
cp third_party/stb_vorbis/LICENSE.txt "$d/licenses/stb_vorbis-LICENSE.txt"
ship_ffmpeg_licence build/ffmpeg/macos/ffmpeg-prefix "$d"
say "Ready: $d"
