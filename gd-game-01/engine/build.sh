#!/usr/bin/env bash
# Builds the Linux and Windows executables into dist/ (run on Fedora with
# gcc-c++ cmake ninja-build SDL3-devel mingw64-gcc-c++ mingw64-SDL3).
# macOS builds need a Mac or the GitHub Actions workflow in .github/workflows.
set -euo pipefail
cd "$(dirname "$0")"

cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux

cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake
cmake --build build/windows

rm -rf dist
mkdir -p dist/gd01-linux-x86_64 dist/gd01-windows-x64
cp build/linux/gd01 README.md dist/gd01-linux-x86_64/
cp build/windows/gd01.exe README.md dist/gd01-windows-x64/
cp /usr/x86_64-w64-mingw32/sys-root/mingw/bin/SDL3.dll dist/gd01-windows-x64/
# The project licence, and the third-party licences, with both builds.
for d in dist/gd01-linux-x86_64 dist/gd01-windows-x64; do
  mkdir -p "$d/licenses"
  cp ../../LICENSE ../../LICENSE-ADDITIONAL-TERMS.md "$d/"
  cp third_party/fidelityfx/LICENSE.txt "$d/licenses/FidelityFX-SDK-LICENSE.txt"
  cp third_party/volk/LICENSE.md "$d/licenses/volk-LICENSE.md"
  cp third_party/vulkan/LICENSE.md "$d/licenses/Vulkan-Headers-LICENSE.md"
done
# AI upscaling: the DLSS model library next to the Linux executable (NGX looks
# there), when ../tools/fetch_dlss.sh has downloaded the DLSS SDK.
if ls third_party/dlss/lib/Linux_x86_64/libnvidia-ngx-dlss.so.* >/dev/null 2>&1; then
  cp third_party/dlss/lib/Linux_x86_64/libnvidia-ngx-dlss.so.* dist/gd01-linux-x86_64/
  cp third_party/dlss/LICENSE.txt dist/gd01-linux-x86_64/licenses/NVIDIA-RTX-SDK-LICENSE.txt
else
  echo "build.sh: no DLSS SDK (run ../tools/fetch_dlss.sh to include DLSS)" >&2
fi
echo "Built:"
find dist -type f | sort

# Android APK (dist/android/), when an Android SDK is set up (see README.md).
if [ -d "${ANDROID_HOME:-$HOME/Android/Sdk}/ndk" ]; then
  ./build_android.sh || echo "Android build failed (desktop builds above are fine)" >&2
fi
