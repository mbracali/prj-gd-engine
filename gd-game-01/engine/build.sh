#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Developer build: Linux (with the system's SDL3) and Windows (build_windows.sh)
# into dist/, plus Android when its SDK is set up. Run on Fedora with
# gcc-c++ cmake ninja-build SDL3-devel mingw64-gcc-c++ mingw64-SDL3.
# For the same packages a release publishes, use ../local_build.sh.
set -euo pipefail
cd "$(dirname "$0")"

cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux

rm -rf dist/gd01-linux-x86_64
mkdir -p dist/gd01-linux-x86_64
cp build/linux/gd01 README.md dist/gd01-linux-x86_64/
# The project licence, and the third-party licences.
d=dist/gd01-linux-x86_64
mkdir -p "$d/licenses"
cp ../../LICENSE ../../LICENSE-ADDITIONAL-TERMS.md "$d/"
cp third_party/fidelityfx/LICENSE.txt "$d/licenses/FidelityFX-SDK-LICENSE.txt"
cp third_party/volk/LICENSE.md "$d/licenses/volk-LICENSE.md"
cp third_party/vulkan/LICENSE.md "$d/licenses/Vulkan-Headers-LICENSE.md"
# AI upscaling: the DLSS model library next to the Linux executable (NGX looks
# there), when ../tools/fetch_dlss.sh has downloaded the DLSS SDK.
if ls third_party/dlss/lib/Linux_x86_64/libnvidia-ngx-dlss.so.* >/dev/null 2>&1; then
  cp third_party/dlss/lib/Linux_x86_64/libnvidia-ngx-dlss.so.* dist/gd01-linux-x86_64/
  cp third_party/dlss/LICENSE.txt dist/gd01-linux-x86_64/licenses/NVIDIA-RTX-SDK-LICENSE.txt
else
  echo "build.sh: no DLSS SDK (run ../tools/fetch_dlss.sh to include DLSS)" >&2
fi

./build_windows.sh

echo "Built:"
find dist -type f | sort

# Android APK (dist/android/), when an Android SDK is set up (see README.md).
if [ -d "${ANDROID_HOME:-$HOME/Android/Sdk}/ndk" ]; then
  ./build_android.sh || echo "Android build failed (desktop builds above are fine)" >&2
fi
