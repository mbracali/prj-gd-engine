#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# The macOS download (dist/gd01-macos/gd01.app, SDL3 inside the app),
# built on a Mac with Homebrew: brew install cmake sdl3 ninja dylibbundler.
# Ad-hoc signed, not notarised: the first start is right-click > Open.
set -euo pipefail
cd "$(dirname "$0")"
. ./build_common.sh

say "gd01 (macOS): configuring (CMake)..."
configure -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release
compile build/macos "gd01 (macOS)"

say "Assembling dist/gd01-macos (app with SDL3 inside, ad-hoc signed)..."
d=dist/gd01-macos
rm -rf "$d" && mkdir -p "$d/licenses"
cp -R build/macos/gd01.app "$d/"
app="$d/gd01.app"
dylibbundler -od -b -x "$app/Contents/MacOS/gd01" -d "$app/Contents/Frameworks/" -p @executable_path/../Frameworks/ >/dev/null
codesign --force --deep --sign - "$app"
cp README.md ../../LICENSE ../../LICENSE-ADDITIONAL-TERMS.md "$d/"
cp third_party/fidelityfx/LICENSE.txt "$d/licenses/FidelityFX-SDK-LICENSE.txt"
cp third_party/volk/LICENSE.md "$d/licenses/volk-LICENSE.md"
cp third_party/vulkan/LICENSE.md "$d/licenses/Vulkan-Headers-LICENSE.md"
say "Ready: $d"
