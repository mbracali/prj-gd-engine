/* SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
 * Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
 *
 * libchdr and the decoders it bundles, compiled once, unmodified, as libchdr's
 * own unity.c does, plus the A/V codec its unity.c leaves out (chd.c refers
 * to it) (cmake/gdcore_chd.cmake). Every global symbol is renamed
 * with a gdchd_ prefix first (chd_libchdr_names.h): KTX links its own zstd
 * into the same game modules, and the launcher and a module must never share
 * one by accident. */
#include "chd_libchdr_names.h"

#ifndef MINIZ_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_APIS
#endif
#ifndef MINIZ_DEFLATE_APIS
#define MINIZ_NO_DEFLATE_APIS
#endif
#ifndef MINIZ_STDIO
#define MINIZ_NO_STDIO
#endif
#ifndef MINIZ_TIME
#define MINIZ_NO_TIME
#endif

#include "deps/lzma-26.02/src/LzmaDec.c"
#include "deps/miniz-3.1.2/miniz.c"
#include "deps/zstd-1.5.7/zstddeclib.c"

#include "src/libchdr_bitstream.c"
#include "src/libchdr_cdrom.c"
#include "src/libchdr_chd.c"
#include "src/libchdr_codec_avhuff.c"
#include "src/libchdr_codec_cdfl.c"
#include "src/libchdr_codec_cdlz.c"
#include "src/libchdr_codec_cdzl.c"
#include "src/libchdr_codec_cdzs.c"
#include "src/libchdr_codec_flac.c"
#include "src/libchdr_codec_huff.c"
#include "src/libchdr_codec_lzma.c"
#include "src/libchdr_codec_zlib.c"
#include "src/libchdr_codec_zstd.c"
#include "src/libchdr_flac.c"
#include "src/libchdr_huffman.c"
