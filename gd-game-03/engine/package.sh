#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Turns a built dist/ folder into the download a release publishes:
#   package.sh <windows|linux|steamdeck|macos|android> <version> <out dir>
# writes <out dir>/gd03-<version>-<platform>.<zip|tar.gz|apk>.
set -euo pipefail
cd "$(dirname "$0")"
platform="$1" version="$2" out="$3"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

# FFmpeg's libraries (the videos) are LGPL: a download that has them must
# carry the licence and the notice (version, source, configure line) that
# build_common.sh's ship_ffmpeg_licence writes.
lgpl_check() {  # <what> <has FFmpeg: 0/1> <has the notice: 0/1>
  if [ "$2" = 1 ] && [ "$3" != 1 ]; then
    echo "package.sh: $1 has FFmpeg's libraries but not its LGPL notice (licenses/FFmpeg-*.txt)" >&2
    exit 1
  fi
}
case "$platform" in
  windows|linux|steamdeck|macos)
    dir="dist/gd03-$(case "$platform" in windows) echo windows-x64 ;; linux) echo linux-x86_64 ;; *) echo "$platform" ;; esac)"
    has=0; [ -n "$(find "$dir" -name 'libavcodec*' -o -name 'avcodec*.dll' | head -1)" ] && has=1
    notice=0; [ -f "$dir/licenses/FFmpeg-NOTICE.txt" ] && [ -f "$dir/licenses/FFmpeg-LICENSE.LGPL-2.1.txt" ] && notice=1
    lgpl_check "$dir" "$has" "$notice" ;;
  android)
    list="$(unzip -l dist/android/gd03.apk 2>/dev/null || true)"
    has=0; grep -q 'lib/.*/libavcodec\.so' <<<"$list" && has=1
    notice=0; grep -q 'assets/licenses/FFmpeg-NOTICE\.txt' <<<"$list" && grep -q 'assets/licenses/FFmpeg-LICENSE' <<<"$list" && notice=1
    lgpl_check "the APK" "$has" "$notice" ;;
esac
case "$platform" in
  windows) f="$out/gd03-$version-windows-x64.zip"; rm -f "$f"
           (cd dist && if command -v zip >/dev/null; then zip -qr "$f" gd03-windows-x64; else 7z a -tzip "$f" gd03-windows-x64 >/dev/null; fi) ;;
  linux)   f="$out/gd03-$version-linux-x86_64.tar.gz"; tar czf "$f" -C dist gd03-linux-x86_64 ;;
  steamdeck) f="$out/gd03-$version-steamdeck.tar.gz"; tar czf "$f" -C dist gd03-steamdeck ;;
  macos)   f="$out/gd03-$version-macos.zip"; rm -f "$f"; (cd dist && ditto -c -k --keepParent gd03-macos "$f") ;;
  android) f="$out/gd03-$version-android.apk"; cp dist/android/gd03.apk "$f" ;;
  *) echo "package.sh: unknown platform $platform" >&2; exit 2 ;;
esac
echo "Packaged $f"
