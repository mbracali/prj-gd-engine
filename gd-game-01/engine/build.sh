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
mkdir -p dist/gh1re-linux-x86_64 dist/gh1re-windows-x64
cp build/linux/gh1re README.md dist/gh1re-linux-x86_64/
cp build/windows/gh1re.exe README.md dist/gh1re-windows-x64/
cp /usr/x86_64-w64-mingw32/sys-root/mingw/bin/SDL3.dll dist/gh1re-windows-x64/
# AI upscaling: the DLSS model library next to the Linux executable (NGX looks
# there), and the third-party licences with both builds.
cp third_party/dlss/lib/Linux_x86_64/libnvidia-ngx-dlss.so.* dist/gh1re-linux-x86_64/
for d in dist/gh1re-linux-x86_64 dist/gh1re-windows-x64; do
  mkdir -p "$d/licenses"
  cp third_party/fidelityfx/LICENSE.txt "$d/licenses/FidelityFX-SDK-LICENSE.txt"
  cp third_party/volk/LICENSE.md "$d/licenses/volk-LICENSE.md"
  cp third_party/vulkan/LICENSE.md "$d/licenses/Vulkan-Headers-LICENSE.md" 2>/dev/null || true
done
cp third_party/dlss/LICENSE.txt dist/gh1re-linux-x86_64/licenses/NVIDIA-RTX-SDK-LICENSE.txt
echo "Built:"
find dist -type f | sort

# Android APK (dist/android/), when an Android SDK is set up (see README.md).
if [ -d "${ANDROID_HOME:-$HOME/Android/Sdk}/ndk" ]; then
  ./build_android.sh || echo "Android build failed (desktop builds above are fine)" >&2
fi
