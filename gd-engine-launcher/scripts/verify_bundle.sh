#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Checks that a launcher download is complete: the launcher, and for EVERY
# game in games.yaml with ready_to_play true, on every system (no exemption:
# a launcher never ships without a ready game's module), its module
# (exporting gd_game_run) and the libraries it needs, non-empty, and listed
# in gd-engine-games/manifest.json. Fails loudly otherwise. build.sh runs it on every build it assembles, the
# gdul release workflow on every archive before it is uploaded.
#
# Usage: gd-engine-launcher/scripts/verify_bundle.sh <build folder | .zip | .tar.gz | .apk> [linux|steamdeck|windows|macos|android]
#   (the system is read from the manifest when not given; steamdeck is checked
#   as linux: every ready game's module and libraries, gd01's DLSS too)
# GDUL_REQUIRE_DLSS=1 (or CI=true): gd01's DLSS library must be in a Linux download.
# CI=true: a module whose exports cannot be read (no nm/objdump) fails too.
set -euo pipefail
shopt -s inherit_errexit nullglob
here="$(cd "$(dirname "$0")" && pwd)"
[ $# -ge 1 ] || { sed -n '4,15p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
target="$1" os="${2:-}"
if [ -t 1 ]; then G=$'\e[32m' R=$'\e[31m' Y=$'\e[33m' N=$'\e[0m'; else G="" R="" Y="" N=""; fi
problems=0
fail() { printf '   %sverify: MISSING/BROKEN: %s%s\n' "$R" "$*" "$N"; problems=$((problems + 1)); }
note() { printf '   verify: %s\n' "$*"; }
warn() { printf '   %sverify: %s%s\n' "$Y" "$*" "$N"; }
# A check this machine cannot make: a warning locally, a failure in CI.
cannot() { if [ "${CI:-}" = true ]; then fail "$* (CI requires it)"; else warn "$*"; fi; }

# An archive is unpacked into a temporary folder first.
root="$target" tmp=""
if [ -f "$target" ]; then
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT
  case "$target" in
    *.tar.gz|*.tgz) tar -xzf "$target" -C "$tmp" ;;
    *.zip|*.apk) python3 -I -m zipfile -e "$target" "$tmp" ;;
    *) echo "verify_bundle.sh: $target: not a folder, .zip, .tar.gz or .apk" >&2; exit 2 ;;
  esac
  case "$target" in *.apk) os="${os:-android}" ;; esac
  root="$tmp"
fi
[ -d "$root" ] || { echo "verify_bundle.sh: $target: no such folder or file" >&2; exit 2; }

mapfile -t manifests < <(find "$root" -path '*gd-engine-games/manifest.json' | LC_ALL=C sort)
if [ ${#manifests[@]} -ne 1 ]; then
  fail "expected one gd-engine-games/manifest.json in $target, found ${#manifests[@]}"
  exit 1
fi
manifest="${manifests[0]}"
games="$(dirname "$manifest")"
manifest_get() {  # <expression on m (the manifest)>
  python3 -I -c 'import json,sys; m=json.load(open(sys.argv[1])); print(eval(sys.argv[2]))' "$manifest" "$1"
}
[ -n "$os" ] || os="$(manifest_get 'm.get("os","")')"
case "$os" in linux|steamdeck|windows|macos|android) ;; *) echo "verify_bundle.sh: unknown system '$os'" >&2; exit 2 ;; esac
note "checking $(basename "$target") ($os)"
if [ "$os" = steamdeck ]; then  # the Linux download built for the Deck: the same files, plus its STEAM-DECK.txt
  os=linux
  find "$root" -maxdepth 2 -name STEAM-DECK.txt | grep -q . || fail "STEAM-DECK.txt (missing)"
fi

yaml_value() {  # <file> <key>: a top-level scalar, quotes removed (as build.sh)
  sed -n "s/^$2:[[:space:]]*//p" "$1" | sed 's/[[:space:]]*#.*$//; s/^"\(.*\)"$/\1/; s/^'"'"'\(.*\)'"'"'$/\1/' | head -1
}
nonempty() { [ -s "$1" ] || fail "${1#"$root"/} (missing or empty)"; }
one_of() {  # <description> <glob matches...>: at least one non-empty file
  local what="$1"; shift
  local f
  for f in "$@"; do [ -s "$f" ] && return 0; done
  fail "$what (missing or empty)"
}

# The launcher itself.
exe="Guitar Dream Universal Launcher"
apk=""
bundle="$(dirname "$games")"  # the download's top folder (macOS: the app's Resources)
case "$os" in
  windows) nonempty "$bundle/$exe.exe"; nonempty "$bundle/SDL3.dll" ;;
  linux) nonempty "$bundle/$exe"; nonempty "$bundle/libSDL3.so.0" ;;
  macos) nonempty "$bundle/../MacOS/$exe" ;;
  android)
    # The APK itself, or a build folder with the APK in it.
    if [ -d "$root/lib" ] && [ -f "$root/AndroidManifest.xml" ]; then apk="$root"
    else
      a=("$root"/*.apk)
      if [ ${#a[@]} -eq 1 ]; then
        tmp2="$(mktemp -d)"; trap 'rm -rf "$tmp" "$tmp2"' EXIT
        python3 -I -m zipfile -e "${a[0]}" "$tmp2"; apk="$tmp2"
      else fail "no .apk in $target"; fi
    fi
    abi=""
    if [ -n "$apk" ]; then
      for a in arm64-v8a x86_64; do [ -d "$apk/lib/$a" ] && abi="$a"; done
      [ -n "$abi" ] || fail "no lib/arm64-v8a or lib/x86_64 in the APK"
    fi
    [ -n "$abi" ] && { note "APK ABI: $abi"; nonempty "$apk/lib/$abi/libmain.so"; nonempty "$apk/lib/$abi/libSDL3.so"; } ;;
esac

# exports <library>: it exports gd_game_run (when a tool can read it).
exports() {
  local lib="$1" tool out
  case "$os" in
    windows)
      tool="$(command -v x86_64-w64-mingw32-objdump || command -v objdump || true)"
      [ -n "$tool" ] || { cannot "no objdump: exports of $(basename "$lib") not checked"; return 0; }
      out="$("$tool" -p "$lib" 2>/dev/null || true)"
      grep -qw gd_game_run <<<"$out" || fail "$(basename "$lib") does not export gd_game_run"
      # The DLLs it imports that a download must carry (not Windows' own).
      local dll
      while IFS= read -r dll; do
        case "$dll" in
          SDL3.dll) [ -s "$bundle/SDL3.dll" ] || fail "$(basename "$lib") needs SDL3.dll" ;;
          av*.dll|sw*.dll|lib*.dll) [ -s "$(dirname "$lib")/$dll" ] || [ -s "$bundle/$dll" ] ||
            fail "$(basename "$lib") needs $dll, not in the download" ;;
        esac
      done < <(sed -n 's/^[[:space:]]*DLL Name: //p' <<<"$out")
      ;;
    linux|android)
      tool="nm"
      if [ "$os" = android ]; then
        local ndk="${ANDROID_NDK:-}"
        [ -n "$ndk" ] || ndk="$(ls -d "${ANDROID_HOME:-$HOME/Android/Sdk}"/ndk/* 2>/dev/null | sort -V | tail -1 || true)"
        for t in "$ndk"/toolchains/llvm/prebuilt/*/bin/llvm-nm; do tool="$t"; done
      fi
      command -v "$tool" >/dev/null || { cannot "no nm: exports of $(basename "$lib") not checked"; return 0; }
      if ! out="$("$tool" -D --defined-only "$lib" 2>/dev/null)"; then
        cannot "$tool cannot read $(basename "$lib"): exports not checked"; return 0
      fi
      grep -qw gd_game_run <<<"$out" || fail "$(basename "$lib") does not export gd_game_run"
      ;;
    macos)
      command -v nm >/dev/null || { cannot "no nm: exports of $(basename "$lib") not checked"; return 0; }
      if ! out="$(nm -gU "$lib" 2>/dev/null)"; then
        cannot "nm cannot read $(basename "$lib") (not on a Mac?): exports not checked"; return 0
      fi
      grep -qw _gd_game_run <<<"$out" || fail "$(basename "$lib") does not export gd_game_run"
      ;;
  esac
}

mapfile -t game_infos < <(sed -n 's/^[[:space:]]*-[[:space:]]*\([^#[:space:]]*game-info\.yaml\).*/\1/p' "$games/games.yaml")
[ ${#game_infos[@]} -gt 0 ] || fail "gd-engine-games/games.yaml lists no game"
modules=0
for gi in "${game_infos[@]}"; do
  [ -f "$games/$gi" ] || { fail "gd-engine-games/$gi"; continue; }
  id="$(yaml_value "$games/$gi" id)"
  case "$(yaml_value "$games/$gi" ready_to_play)" in true|True|yes) ;; *) continue ;; esac
  nn="${id##*-}"
  # Every ready game on every system: no "listed only" (gdul 0.1.5's APK had no gd01).

  d="$games/$id"
  case "$os" in
    windows) mod="$d/$id.dll" ;;
    linux) mod="$d/$id.so" ;;
    macos) mod="$d/$id.dylib" ;;
    android) mod="$apk/lib/${abi:-arm64-v8a}/lib$id.so" ;;
  esac
  libs="$(dirname "$mod")"
  want="$(basename "$mod")"
  [ "$(manifest_get "m.get('games',{}).get('$id',{}).get('module','')")" = "$want" ] ||
    fail "manifest.json does not list $id's module $want"
  if [ ! -s "$mod" ]; then
    fail "$id's module ${mod#"$root"/} (missing or empty)"
  else
    exports "$mod"
    # Linux: every symbol the module needs is there (a game source half
    # written, a function declared but not built, links into a .so that
    # passes every other check and fails only on Play: "undefined symbol").
    if [ "$os" = linux ] && [ "$(uname -s)" = Linux ] && command -v ldd >/dev/null; then
      undef="$(LD_LIBRARY_PATH="$bundle:$libs" ldd -r "$mod" 2>&1 | grep 'undefined symbol' || true)"
      [ -z "$undef" ] || fail "$id's module has undefined symbols (it would not load): $(echo "$undef" | head -3 | sed 's/^[[:space:]]*//' | tr '\n' ' ')"
    fi
  fi
  case "$nn" in
    02)  # FFmpeg (videos, sound effects) and its LGPL notice
      for lib in avformat avcodec avutil; do
        case "$os" in
          windows) one_of "$id: $lib-*.dll" "$libs/$lib"-*.dll ;;
          linux) one_of "$id: lib$lib.so.N" "$libs/lib$lib".so.[0-9]* ;;
          macos) one_of "$id: lib$lib.N.dylib" "$libs/lib$lib".[0-9]*.dylib ;;
          android) one_of "$id: lib$lib.so" "$libs/lib$lib.so" ;;
        esac
      done
      nonempty "$d/licenses/FFmpeg-LICENSE.LGPL-2.1.txt" ;;
    01)  # NVIDIA DLSS on Linux, next to the launcher (when the build had the SDK; always in CI)
      if [ "$os" = linux ] && { [ -f "$d/root-files.txt" ] || [ "${GDUL_REQUIRE_DLSS:-}" = 1 ] || [ "${CI:-}" = true ]; }; then
        one_of "$id: libnvidia-ngx-dlss.so.* next to the launcher" "$bundle"/libnvidia-ngx-dlss.so.*
      fi ;;
  esac
  modules=$((modules + 1))
done

# Windows: every DLL any program or DLL of the download imports must be in the
# download or be one of Windows' own (a missing one is LoadLibrary error 126,
# e.g. FFmpeg's avutil needs MinGW's libwinpthread-1.dll).
if [ "$os" = windows ]; then
  tool="$(command -v x86_64-w64-mingw32-objdump || command -v objdump || true)"
  if [ -z "$tool" ]; then
    cannot "no objdump: the DLLs' own imports were not checked"
  else
    syswin=" advapi32.dll bcrypt.dll cfgmgr32.dll comctl32.dll comdlg32.dll crypt32.dll d3d11.dll d3d12.dll dbghelp.dll dinput8.dll dwmapi.dll dxgi.dll gdi32.dll hid.dll imm32.dll iphlpapi.dll kernel32.dll msvcrt.dll ntdll.dll ole32.dll oleaut32.dll opengl32.dll powrprof.dll psapi.dll secur32.dll setupapi.dll shcore.dll shell32.dll shlwapi.dll user32.dll userenv.dll uxtheme.dll version.dll winmm.dll ws2_32.dll xinput1_4.dll "
    while IFS= read -r -d '' bin; do
      dir="$(dirname "$bin")"
      while IFS= read -r dll; do
        low="$(printf '%s' "$dll" | tr '[:upper:]' '[:lower:]')"
        case "$syswin" in *" $low "*) continue ;; esac
        case "$low" in api-ms-win-*|ext-ms-*) continue ;; esac
        found=""
        for c in "$dir" "$bundle"; do
          [ -n "$(find "$c" -maxdepth 1 -iname "$dll" -size +0 2>/dev/null | head -1)" ] && found=1 && break
        done
        [ -n "$found" ] || fail "${bin#"$root"/} needs $dll, which is neither in the download nor part of Windows"
      done < <("$tool" -p "$bin" 2>/dev/null | sed -n 's/^[[:space:]]*DLL Name: //p')
    done < <(find "$bundle" \( -iname '*.dll' -o -iname '*.exe' \) -print0)
  fi
fi

if [ "$problems" != 0 ]; then
  printf '%sverify: %s is INCOMPLETE (%d problem(s)): do not ship it. See docs/release-process.md, "Never again: incomplete bundles".%s\n' \
    "$R" "$(basename "$target")" "$problems" "$N"
  exit 1
fi
printf '   %sverify: %s complete (%d game module(s) with their libraries)%s\n' "$G" "$(basename "$target")" "$modules" "$N"
