# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Shared by gd24's build scripts (sourced). The FFmpeg part is gd-game-10's
# (engine/build_common.sh) with Rock Band Unplugged's decoders.

say() { printf '   -> %s\n' "$*"; }

# ---- FFmpeg (the song previews: <SONG>_PREVIEW.AT3, ATRAC3plus in a RIFF
# WAVE: src/audio/at3_preview.cpp parses the RIFF itself, so no demuxer is
# needed, only the atrac3p decoder; later the PMF movies: mpegps + h264) ----
# A minimal LGPL build, shared libraries shipped next to gd24 (libavformat,
# libavcodec, libavutil), from the pinned release tarball, unmodified: the
# same version, tarball, SHA-256 and licence checks as gd10's. No GPL,
# version3 or nonfree options: the libraries stay LGPL 2.1 or later.
FFMPEG_VERSION=7.1.5
FFMPEG_SHA256=de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f
FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
FFMPEG_URLS=("$FFMPEG_URL"
  "https://github.com/mbracali/prj-gd-engine/releases/download/deps/ffmpeg-$FFMPEG_VERSION.tar.xz")
FFMPEG_CONFIGURE=(--disable-everything --disable-programs --disable-doc --disable-network --disable-autodetect
  --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample --disable-debug
  --enable-shared --disable-static --enable-pic
  --enable-decoder=atrac3p)

sha256_of() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

# build_ffmpeg <work dir> <label> [extra configure args...]: builds FFmpeg
# into <work dir>/ffmpeg-prefix, reusing it when it was built with the same
# version and options. The tarball is kept in build/ffmpeg-src/ (relative to
# the current directory, gd-game-24/engine) and checked. FFMPEG_JOBS sets
# make's jobs (default: every core).
build_ffmpeg() {
  local work="$1" label="$2"; shift 2
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
  if [ ! -f "$tarball" ] || [ "$(sha256_of "$tarball")" != "$FFMPEG_SHA256" ]; then
    local url got=0
    for url in "${FFMPEG_URLS[@]}"; do
      say "FFmpeg $FFMPEG_VERSION: downloading the source ($url)..."
      rm -f "$tarball.part"
      curl -fsSL --retry 5 --retry-all-errors --retry-delay 5 --connect-timeout 30 -o "$tarball.part" "$url" || continue
      if [ "$(sha256_of "$tarball.part")" = "$FFMPEG_SHA256" ]; then got=1; break; fi
      say "FFmpeg: the download from $url is not the pinned SHA-256; trying the next source"
    done
    if [ "$got" != 1 ]; then rm -f "$tarball.part"; say "FFmpeg: no source gave the pinned tarball"; return 1; fi
    mv "$tarball.part" "$tarball"
  fi
  say "FFmpeg $FFMPEG_VERSION ($label): building the decoder libraries (first time only)..."
  rm -rf "$work/ffmpeg-$FFMPEG_VERSION" "$prefix"
  tar xJf "$tarball" -C "$work"
  local log="$work/ffmpeg-build.log" jobs
  jobs="${FFMPEG_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
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
  say "FFmpeg $FFMPEG_VERSION ($label): built"
}

# ship_ffmpeg_licence <ffmpeg prefix> <dist folder>: the LGPL notice next to
# the FFmpeg libraries (licenses/FFmpeg-LICENSE.LGPL-2.1.txt, FFmpeg-NOTICE.txt).
ship_ffmpeg_licence() {
  local prefix="$1" d="$2"
  mkdir -p "$d/licenses"
  cp "$prefix/COPYING.LGPLv2.1" "$d/licenses/FFmpeg-LICENSE.LGPL-2.1.txt"
  { echo "gd24 plays Rock Band Unplugged's song previews with FFmpeg (https://ffmpeg.org), a separate"
    echo "work under the GNU LGPL 2.1 or later, used as shared libraries (avformat, avcodec,"
    echo "avutil) next to the program. Its source code:"
    echo
    sed -n '3,$p' "$prefix/FFMPEG-BUILD.txt"
    echo
    echo "You may replace these libraries with your own build of the same FFmpeg major"
    echo "versions (libavformat 61, libavcodec 61, libavutil 59: FFmpeg 7.1.x), for example"
    echo "from the source above with the same configure line; gd24 loads whatever is"
    echo "next to it. FFmpeg is a trademark of Fabrice Bellard."
  } > "$d/licenses/FFmpeg-NOTICE.txt"
}
