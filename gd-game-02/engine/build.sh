#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Developer build: Linux (with the system's SDL3) and Windows (build_windows.sh)
# into dist/, plus Android when its SDK is set up. Run on Fedora with
# gcc-c++ cmake ninja-build SDL3-devel mingw64-gcc-c++ mingw64-SDL3.
# For the same packages a release publishes, use ../local_build.sh.
set -euo pipefail
cd "$(dirname "$0")"
. ./build_common.sh

say "gd02 (Linux): configuring (CMake)..."
configure -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
compile build/linux "gd02 (Linux)"

d=dist/gd02-linux-x86_64
rm -rf "$d"
mkdir -p "$d/licenses"
cp build/linux/gd02 README.md "$d/"
# The project licence, and the third-party licences.
cp ../../LICENSE ../../LICENSE-ADDITIONAL-TERMS.md "$d/"
cp third_party/stb_vorbis/LICENSE.txt "$d/licenses/stb_vorbis-LICENSE.txt"

if command -v x86_64-w64-mingw32-g++ >/dev/null || [ -n "${MSYSTEM:-}" ]; then
  ./build_windows.sh
else
  echo "build.sh: no MinGW-w64 (dnf install mingw64-gcc-c++ mingw64-SDL3), skipping Windows" >&2
fi

say "built:"
find dist -type f | sort | sed 's/^/      /'

# Android APK (dist/android/), when an Android SDK is set up (see build_android.sh).
if [ -d "${ANDROID_HOME:-$HOME/Android/Sdk}/ndk" ]; then
  ./build_android.sh || echo "Android build failed (desktop builds above are fine)" >&2
fi
