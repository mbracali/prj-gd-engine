#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Developer build: Linux, with the system's SDL3, into dist/gd02-linux-x86_64.
# Run on Fedora with gcc-c++ cmake ninja-build SDL3-devel. The release
# pipeline (local_build.sh, Windows / macOS / Android / Steam Deck) comes later,
# copied from gd-game-01.
set -euo pipefail
cd "$(dirname "$0")"
. ./build_common.sh

say "configuring build/linux"
configure -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
compile build/linux "gd02 (Linux)"

d=dist/gd02-linux-x86_64
rm -rf "$d"
mkdir -p "$d/licenses"
cp build/linux/gd02 "$d/"
# The project licence, and the third-party licences.
cp ../../LICENSE ../../LICENSE-ADDITIONAL-TERMS.md "$d/"
cp third_party/stb_vorbis/LICENSE.txt "$d/licenses/stb_vorbis-LICENSE.txt"

say "built:"
find dist -type f | sort | sed 's/^/      /'
