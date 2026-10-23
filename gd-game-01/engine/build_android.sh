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
#     source release matching the desktop SDL3, unpacked into build/android/).
# Options: ABIS=arm64-v8a ./build_android.sh   (one ABI only)
#          GD01_RELEASE_KEYSTORE=... (+ _PASSWORD, _ALIAS, _KEY_PASSWORD) to sign with your key;
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

# The debug key (what Android Studio would create) signs the APK by default.
if [ -z "${GD01_RELEASE_KEYSTORE:-}" ] && [ ! -f "$HOME/.android/debug.keystore" ]; then
  mkdir -p "$HOME/.android"
  "$JAVA_HOME/bin/keytool" -genkeypair -keystore "$HOME/.android/debug.keystore" -storepass android -keypass android \
    -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null
fi
sign=()
if [ -n "${GD01_RELEASE_KEYSTORE:-}" ]; then
  sign=(-PreleaseKeystore="$GD01_RELEASE_KEYSTORE" -PreleaseKeystorePassword="$GD01_RELEASE_KEYSTORE_PASSWORD"
        -PreleaseKeyAlias="$GD01_RELEASE_KEY_ALIAS" -PreleaseKeyPassword="${GD01_RELEASE_KEY_PASSWORD:-$GD01_RELEASE_KEYSTORE_PASSWORD}")
fi

say "Building the APK with Gradle (ABIs: ${ABIS:-arm64-v8a,x86_64}; Gradle skips what is already up to date)..."
(cd android && ./gradlew --no-daemon -q assembleRelease -Psdl3Dir="$SDL3_DIR" -Pabis="${ABIS:-arm64-v8a,x86_64}" "${sign[@]}")

APK=build/android/gradle/outputs/apk/release/app-release.apk
BT=$(ls -d "$ANDROID_HOME"/build-tools/* | sort -V | tail -1)
"$BT/apksigner" verify "$APK" 2>/dev/null
if [ ${#sign[@]} -gt 0 ]; then say "APK signed with the release key"; else say "APK signed with the Android debug key (no release key given)"; fi
rm -rf dist/android
mkdir -p dist/android
cp "$APK" dist/android/gd01.apk
cp README.md dist/android/
say "Ready: dist/android/gd01.apk"
