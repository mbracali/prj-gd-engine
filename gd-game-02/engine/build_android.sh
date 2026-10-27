#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Builds the Android APK (arm64-v8a + x86_64) into dist/android/.
#
# Needs (all user-local, no root):
#   - a JDK 17 or 21 (JAVA_HOME; ~/.local/opt/jdk-21* is picked up),
#   - the Android SDK command-line tools at $ANDROID_HOME (default ~/Android/Sdk)
#     with: sdkmanager "ndk;28.2.13676358" "cmake;4.1.2" "platforms;android-36" "build-tools;36.1.0" "platform-tools"
#   - network on the first run (Gradle, the Android Gradle plugin and the SDL3
#     source release matching the desktop SDL3, unpacked into build/android/;
#     the FFmpeg source for the videos, built per ABI with the NDK, see
#     build_common.sh; GD02_VIDEO=0 builds without it).
# Options: ABIS=arm64-v8a ./build_android.sh   (one ABI only)
#          GD02_RELEASE_KEYSTORE=... (+ _PASSWORD, _ALIAS, _KEY_PASSWORD) to sign with your key;
#          otherwise the APK is signed with the Android debug key.
set -euo pipefail
cd "$(dirname "$0")"
. ./build_common.sh

SDL3_VERSION=3.4.18
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
if [ -z "${JAVA_HOME:-}" ]; then
  for j in "$HOME"/.local/opt/jdk-21* "$HOME"/.local/opt/jdk-17* /usr/lib/jvm/java-21-openjdk /usr/lib/jvm/java-17-openjdk; do
    [ -x "$j/bin/javac" ] && export JAVA_HOME="$j" && break
  done
fi
[ -n "${JAVA_HOME:-}" ] && export PATH="$JAVA_HOME/bin:$PATH"
if [ ! -d "$ANDROID_HOME/ndk" ] || [ -z "${JAVA_HOME:-}" ]; then
  echo "build_android.sh: Android SDK/NDK ($ANDROID_HOME) or a JDK not found; see the comment at the top." >&2
  exit 2
fi

mkdir -p build/android
SDL3_DIR="$PWD/build/android/SDL3-$SDL3_VERSION"
if [ ! -f "$SDL3_DIR/CMakeLists.txt" ]; then
  say "Downloading the SDL3 $SDL3_VERSION source (first time only)..."
  curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/SDL3-$SDL3_VERSION.tar.gz" \
    | tar xz -C build/android
fi

# FFmpeg (the videos), one build per ABI with the NDK's clang, in
# build/ffmpeg/android/<ABI>/ffmpeg-prefix; its LGPL notice goes into the
# APK's assets (licenses/).
ffmpeg_args=()
rm -rf build/android/assets
if [ "${GD02_VIDEO:-1}" != 0 ]; then
  ndk="$(ls -d "$ANDROID_HOME"/ndk/* | sort -V | tail -1)"
  case "$(uname -s)" in Darwin) host=darwin-x86_64 ;; *) host=linux-x86_64 ;; esac
  tc="$ndk/toolchains/llvm/prebuilt/$host/bin"
  abis="${ABIS:-arm64-v8a,x86_64}"
  for abi in ${abis//,/ }; do
    case "$abi" in
      arm64-v8a) arch=aarch64 triple=aarch64-linux-android ;;
      x86_64) arch=x86_64 triple=x86_64-linux-android ;;
      *) echo "build_android.sh: no FFmpeg setup for ABI $abi" >&2; exit 2 ;;
    esac
    # 16 KB pages (Android 15+ devices may use them).
    build_ffmpeg "build/ffmpeg/android/$abi" "Android $abi, NDK $(basename "$ndk")" --enable-cross-compile --target-os=android \
      --arch="$arch" --cc="$tc/${triple}24-clang" --cxx="$tc/${triple}24-clang++" --nm="$tc/llvm-nm" --ar="$tc/llvm-ar" \
      --ranlib="$tc/llvm-ranlib" --strip="$tc/llvm-strip" --pkg-config=false --disable-x86asm \
      --extra-ldflags=-Wl,-z,max-page-size=16384
  done
  ffmpeg_args=(-PffmpegDir="$PWD/build/ffmpeg/android")
  ship_ffmpeg_licence "build/ffmpeg/android/${abis%%,*}/ffmpeg-prefix" build/android/assets
  for abi in ${abis//,/ }; do  # each ABI's configure line
    { echo; echo "$abi: $(tail -1 "build/ffmpeg/android/$abi/ffmpeg-prefix/FFMPEG-BUILD.txt" | sed 's/^ *//')"; } >> build/android/assets/licenses/FFmpeg-NOTICE.txt
  done
fi

# The debug key (what Android Studio would create) signs the APK by default.
if [ -z "${GD02_RELEASE_KEYSTORE:-}" ] && [ ! -f "$HOME/.android/debug.keystore" ]; then
  mkdir -p "$HOME/.android"
  "$JAVA_HOME/bin/keytool" -genkeypair -keystore "$HOME/.android/debug.keystore" -storepass android -keypass android \
    -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null
fi
sign=()
if [ -n "${GD02_RELEASE_KEYSTORE:-}" ]; then
  sign=(-PreleaseKeystore="$GD02_RELEASE_KEYSTORE" -PreleaseKeystorePassword="$GD02_RELEASE_KEYSTORE_PASSWORD"
        -PreleaseKeyAlias="$GD02_RELEASE_KEY_ALIAS" -PreleaseKeyPassword="${GD02_RELEASE_KEY_PASSWORD:-$GD02_RELEASE_KEYSTORE_PASSWORD}")
fi

say "Building the APK with Gradle (ABIs: ${ABIS:-arm64-v8a,x86_64}; Gradle skips what is already up to date)..."
(cd android && ./gradlew --no-daemon -q assembleRelease -Psdl3Dir="$SDL3_DIR" -Pabis="${ABIS:-arm64-v8a,x86_64}" "${ffmpeg_args[@]}" "${sign[@]}")

APK=build/android/gradle/outputs/apk/release/app-release.apk
BT=$(ls -d "$ANDROID_HOME"/build-tools/* | sort -V | tail -1)
"$BT/apksigner" verify "$APK" 2>/dev/null
if [ ${#sign[@]} -gt 0 ]; then say "APK signed with the release key"; else say "APK signed with the Android debug key (no release key given)"; fi
rm -rf dist/android
mkdir -p dist/android
cp "$APK" dist/android/gd02.apk
cp README.md dist/android/
say "Ready: dist/android/gd02.apk"
