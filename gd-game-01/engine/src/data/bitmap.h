// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <vector>

#include "data/bytes.h"

// An RGBA8 image (rows top to bottom).
struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

// Decodes the game's bitmap format (.bmp_ps2 / .png_ps2 files and the bitmaps
// embedded in scene Tex records): 32-byte header, then a palette for 4/8 bpp
// (PS2 alpha 0..0x80, CSM1 order for 8 bpp) and pixel rows, or RGBA5551 /
// RGB pixels for 16/24 bpp. Only the top mip level is returned.
// `consumed` (optional) receives the total size including all mip levels.
// Throws std::runtime_error on malformed input.
Image decodeBitmap(const uint8_t* data, size_t size, size_t* consumed = nullptr);
inline Image decodeBitmap(const Bytes& b) { return decodeBitmap(b.data(), b.size()); }
