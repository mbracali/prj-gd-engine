// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// The vendored single-header libraries, compiled once each in their own file
// (stb_impl.cpp, bcdec_impl.cpp) with internal linkage (STB_*_STATIC,
// BCDEC_STATIC): no symbol of theirs can clash with another copy in the same
// program (the launcher has its own stb_image, SDL3_image another). These
// wrappers are the only way the backends reach them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gd::core::image::detail {

// ---- stb ----
bool stbInfo(const void* data, size_t size, int* w, int* h);
bool stbIsHdr(const void* data, size_t size);
unsigned char* stbLoadRgba8(const void* data, size_t size, int* w, int* h);  // free with stbFree
float* stbLoadRgbaF(const void* data, size_t size, int* w, int* h);          // free with stbFree
void stbFree(void* p);
const char* stbFailureReason();
// kind: 0 PNG, 1 TGA, 2 BMP, 3 JPEG; 4-channel RGBA8 rows.
bool stbWrite(int kind, int w, int h, const uint8_t* rgba, int quality, std::vector<uint8_t>& out);
// filter: 0 default, 1 triangle (linear), 2 point sample; isFloat: RGBA32F else RGBA8.
bool stbResize(const void* in, int w, int h, void* out, int ow, int oh, bool isFloat, int filter);

// ---- bcdec ---- one 4x4 block each, into rows `pitch` apart.
// RGBA8 (pitch in bytes): kind 1, 2, 3 or 7.
void bcdecRgba8(int kind, const void* block, uint8_t* dst, int pitchBytes);
// One (BC4) or two (BC5) unsigned channels, 8 bits each (pitch in bytes).
void bcdecBc4(const void* block, uint8_t* dst, int pitchBytes);
void bcdecBc5(const void* block, uint8_t* dst, int pitchBytes);
// Signed BC4 / BC5 as floats in [-1, 1] (pitch in floats).
void bcdecBc4Snorm(const void* block, float* dst, int pitchFloats);
void bcdecBc5Snorm(const void* block, float* dst, int pitchFloats);
// BC6H as RGB floats (pitch in floats).
void bcdecBc6h(const void* block, float* dst, int pitchFloats, bool isSigned);

}  // namespace gd::core::image::detail
