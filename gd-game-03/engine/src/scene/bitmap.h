// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "data/bytes.h"

namespace scene {

// RndBitmap as GH2 stores it: the tail of every Tex record, and every loose
// .bmp_ps2 / .png_ps2 file (docs/milo_and_textures.md, "Textures").
// Header (RndBitmap::LoadHeader @ 0x821efd90), little-endian:
//   u8 version, u8 bpp, u32 encoding (u8 when version 0), u8 extra mips,
//   u16 width, u16 height, u16 bytes per line, then 19 zero bytes (6 for v0).
// Then a palette (bpp <= 8 and no compressed encoding bit, never in GH2) and
// each level, largest first (RndBitmap::Load @ 0x821f1f38).
struct Bitmap {
    enum class Format { None, DXT1, DXT5, ATI2, Other, RGBA8 };  // RGBA8: made by the port (never on the disc)
    struct Level {
        int width = 0, height = 0;
        size_t offset = 0, size = 0;  // into `data`
    };

    int version = 0, bpp = 0, encoding = 0, extraMips = 0;
    int width = 0, height = 0, bytesPerLine = 0;
    Format format = Format::None;
    std::vector<Level> levels;
    // Every level's blocks, back to back, already converted from the Xbox
    // 360's 16-bit word order to the PC (D3D / OpenGL) BC layout.
    Bytes data;

    bool empty() const { return levels.empty() || width == 0 || height == 0; }
    const uint8_t* level(size_t i) const { return data.data() + levels[i].offset; }
};

// Bytes one level of `format` takes at w x h (4x4 blocks, rounded up).
size_t bitmapLevelSize(Bitmap::Format format, int bpp, int w, int h);

// Parses a bitmap at r's position; advances past it. Throws on bad data.
Bitmap readBitmap(ByteReader& r);

// Decodes one level to RGBA8 (width x height x 4). ATI2 (BC5) normal maps come
// out as (x, y, rebuilt z, 255). Returns false for unsupported formats.
bool decodeBitmapRgba(const Bitmap& bmp, size_t level, std::vector<uint8_t>& out);

}  // namespace scene
