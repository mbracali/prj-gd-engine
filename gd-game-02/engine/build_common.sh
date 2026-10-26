# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Status lines shared by the build_*.sh scripts (sourced; also inside the
# build containers), so every step says what it is doing.

say() { printf '   -> %s\n' "$*"; }

# compile <build dir> <name>: builds with CMake/Ninja, saying first whether
# anything needs compiling (a Ninja dry run) and how much.
compile() {
  local n logp="$1/compile.log"
  case "$1" in /*) ;; *) logp="gd-game-02/engine/$logp" ;; esac
  n="$(cmake --build "$1" -- -n 2>/dev/null | grep -c '^\[' || true)"
  if [ "$n" = 0 ]; then
    say "$2: nothing to compile, already up to date"
    return 0
  fi
  say "$2: compiling ($n steps; full compiler output in $logp)..."
  # On screen only the errors, if any; warnings and progress go to the log.
  if ! cmake --build "$1" > "$1/compile.log" 2>&1; then
    grep -E 'error|Error|FAILED' "$1/compile.log" | head -40
    say "$2: compile FAILED (see $logp)"
    return 1
  fi
  say "$2: compiled"
}

# configure <cmake args...>: CMake configure, quiet unless it fails.
configure() {
  local log
  log="$(mktemp)"
  if ! cmake "$@" > "$log" 2>&1; then cat "$log"; rm -f "$log"; return 1; fi
  rm -f "$log"
}

# ---- FFmpeg (GH2's videos: WMV3 video + WMA v2 audio in ASF; the sound
# effects: XMA, decoded by the xma1 / xma2 decoders) ----
# A minimal LGPL build, shared libraries shipped next to gd02 (libavformat,
# libavcodec, libavutil), from the pinned release tarball, unmodified. The
# tarball's SHA-256 was taken after checking its signature with FFmpeg's
# release key (FCF986EA15E6E293A5644F10B4322F04D67658D8). No GPL, version3 or
# nonfree options: the libraries stay LGPL 2.1 or later.
FFMPEG_VERSION=7.1.5
FFMPEG_SHA256=de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f
FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
FFMPEG_CONFIGURE=(--disable-everything --disable-programs --disable-doc --disable-network --disable-autodetect
  --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample --disable-debug
  --enable-shared --disable-static --enable-pic
  --enable-demuxer=asf --enable-decoder=wmv3,wmav2,xma1,xma2)

sha256_of() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

# build_ffmpeg <work dir> <label> [extra configure args...]: builds FFmpeg
# into <work dir>/ffmpeg-prefix (include/, lib/, bin/ on Windows), reusing
# it when it was built with the same version and options. The source
# tarball is downloaded once into build/ffmpeg-src/ and checked. The prefix
# also gets the licence and FFMPEG-BUILD.txt (version, source, options),
# which ship_ffmpeg_licence puts into the download.
build_ffmpeg() {
  local work="$1" label="$2"; shift 2
  # Threads (an autodetected feature, off with --disable-autodetect): FFmpeg
  # built without them must not be used from more than one thread, and gd02
  # decodes on its own thread. Win32 threads on Windows (no extra DLL).
  case " $* $(uname -s)" in
    *--target-os=mingw32*|*MINGW*|*MSYS*|*CYGWIN*) set -- "$@" --enable-w32threads ;;
    *) set -- "$@" --enable-pthreads ;;
  esac
  local prefix="$work/ffmpeg-prefix" tarball="build/ffmpeg-src/ffmpeg-$FFMPEG_VERSION.tar.xz"
  case "$work" in /*) ;; *) work="$PWD/$work" prefix="$PWD/$prefix" ;; esac
  local options="${FFMPEG_CONFIGURE[*]} $*"
  local stamp="FFmpeg $FFMPEG_VERSION ($label): $options"
  if [ -f "$prefix/FFMPEG-BUILD.txt" ] && [ "$(sed -n 1p "$prefix/FFMPEG-BUILD.txt")" = "$stamp" ]; then
    say "FFmpeg $FFMPEG_VERSION ($label): already built (reused)"
    return 0
  fi
  mkdir -p build/ffmpeg-src "$work"
  # One build at a time per folder (build.sh also runs build_windows.sh).
  if command -v flock >/dev/null; then
    exec 9>"$work.lock"
    flock 9
    if [ -f "$prefix/FFMPEG-BUILD.txt" ] && [ "$(sed -n 1p "$prefix/FFMPEG-BUILD.txt")" = "$stamp" ]; then
      exec 9>&-
      say "FFmpeg $FFMPEG_VERSION ($label): already built (reused)"
      return 0
    fi
  fi
  if [ ! -f "$tarball" ] || [ "$(sha256_of "$tarball")" != "$FFMPEG_SHA256" ]; then
    say "FFmpeg $FFMPEG_VERSION: downloading the source ($FFMPEG_URL)..."
    if command -v curl >/dev/null; then curl -fsSL --retry 3 -o "$tarball.part" "$FFMPEG_URL"
    else wget -q -O "$tarball.part" "$FFMPEG_URL"; fi
    if [ "$(sha256_of "$tarball.part")" != "$FFMPEG_SHA256" ]; then
      rm -f "$tarball.part"
      say "FFmpeg: the download's SHA-256 is not the pinned one; stopping"
      return 1
    fi
    mv "$tarball.part" "$tarball"
  fi
  say "FFmpeg $FFMPEG_VERSION ($label): building the video decoder libraries (first time only, a minute or two)..."
  rm -rf "$work/ffmpeg-$FFMPEG_VERSION" "$prefix"
  tar xJf "$tarball" -C "$work"
  local log="$work/ffmpeg-build.log" jobs
  jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  if ! (cd "$work/ffmpeg-$FFMPEG_VERSION" && ./configure --prefix="$prefix" "${FFMPEG_CONFIGURE[@]}" "$@" &&
        make -j"$jobs" && make install) > "$log" 2>&1; then
    tail -30 "$log"
    say "FFmpeg ($label): build FAILED (see $log)"
    return 1
  fi
  if ! grep -q '#define FFMPEG_LICENSE "LGPL version 2.1 or later"' "$work/ffmpeg-$FFMPEG_VERSION/config.h"; then
    say "FFmpeg ($label): the build is not LGPL 2.1; stopping"
    return 1
  fi
  cp "$work/ffmpeg-$FFMPEG_VERSION/COPYING.LGPLv2.1" "$prefix/COPYING.LGPLv2.1"
  { echo "$stamp"
    echo
    echo "FFmpeg $FFMPEG_VERSION, built unmodified from the release tarball"
    echo "  $FFMPEG_URL"
    echo "  SHA-256 $FFMPEG_SHA256"
    echo "Licence: GNU Lesser General Public License 2.1 or later (licenses/FFmpeg-LICENSE.LGPL-2.1.txt)."
    echo "Configured with:"
    echo "  ./configure ${FFMPEG_CONFIGURE[*]} $*" | sed -e "s|$PWD/||g" -e "s|$HOME/|~/|g"
  } > "$prefix/FFMPEG-BUILD.txt"
  rm -rf "$work/ffmpeg-$FFMPEG_VERSION"
  command -v flock >/dev/null && exec 9>&-
  say "FFmpeg $FFMPEG_VERSION ($label): built"
}

# ship_ffmpeg_licence <ffmpeg prefix> <dist folder>: the LGPL notice the
# download needs next to the FFmpeg libraries: licenses/FFmpeg-LICENSE.LGPL-2.1.txt
# and licenses/FFmpeg-NOTICE.txt (what was built, from where, with which
# options, and how to swap in your own build).
ship_ffmpeg_licence() {
  local prefix="$1" d="$2"
  mkdir -p "$d/licenses"
  cp "$prefix/COPYING.LGPLv2.1" "$d/licenses/FFmpeg-LICENSE.LGPL-2.1.txt"
  { echo "gd02 plays Guitar Hero II's videos and sound effects with FFmpeg (https://ffmpeg.org), a separate"
    echo "work under the GNU LGPL 2.1 or later, used as shared libraries (avformat, avcodec,"
    echo "avutil) next to the program. Its source code:"
    echo
    sed -n '3,$p' "$prefix/FFMPEG-BUILD.txt"
    echo
    echo "You may replace these libraries with your own build of the same FFmpeg major"
    echo "versions (libavformat 61, libavcodec 61, libavutil 59: FFmpeg 7.1.x), for example"
    echo "from the source above with the same configure line; gd02 loads whatever is"
    echo "next to it. FFmpeg is a trademark of Fabrice Bellard."
  } > "$d/licenses/FFmpeg-NOTICE.txt"
}
