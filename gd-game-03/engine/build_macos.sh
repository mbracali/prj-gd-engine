#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# The macOS download (dist/gd03-macos/gd03.app, SDL3 inside the app), built
# on a Mac with Homebrew: brew install cmake sdl3 ninja dylibbundler.
# Ad-hoc signed, not notarised: the first start is right-click > Open.
set -euo pipefail
cd "$(dirname "$0")"
. ./build_common.sh

say "gd03 (macOS): configuring (CMake)..."
configure -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release
compile build/macos "gd03 (macOS)"

say "Assembling dist/gd03-macos (app with SDL3 inside, ad-hoc signed)..."
d=dist/gd03-macos
rm -rf "$d" && mkdir -p "$d/licenses"
cp -R build/macos/gd03.app "$d/"
app="$d/gd03.app"
dylibbundler -od -b -x "$app/Contents/MacOS/gd03" -d "$app/Contents/Frameworks/" -p @executable_path/../Frameworks/ >/dev/null
codesign --force --deep --sign - "$app"
cp README.md ../../LICENSE ../../LICENSE-ADDITIONAL-TERMS.md "$d/"
say "Ready: $d"
