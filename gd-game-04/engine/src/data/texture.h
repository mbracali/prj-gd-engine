// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "data/bytes.h"
#include "render/device.h"

// Guitar Hero III's Xbox 360 textures (docs/formats.md, "Textures";
// tools/img.py is the reference decoder, checked over the whole disc):
//   .img  one texture: 0x28-byte image header, 0x34-byte D3D header, texels
//         from offset 0x1000;
//   .tex  a texture dictionary (0xfacecaa7): many image headers + D3D
//         headers, texel data at offsets from the start of the file.
// Texels are Xenos-tiled and stored in big-endian 16-bit (DXT) or 32-bit
// (8_8_8_8, 16_16_FLOAT) words. These functions untile them and hand back
// data in the layout render/device.h's TextureDesc / TexLevel expect.
namespace gh3tex {

struct Texture {
    uint32_t key = 0;          // name checksum (a .tex texture's key; .img: 0x505c3a43 on the disc)
    int width = 0, height = 0;
    int depth = 1;             // volume textures only
    int mips = 1;              // levels per face, largest first
    gfx::TexFormat format = gfx::TexFormat::RGBA8;
    gfx::TexKind kind = gfx::TexKind::Tex2D;
    std::string sourceFormat;  // "DXT1", "DXT3", "DXT5", "DXN", "8_8_8_8", "16_16_FLOAT"
    uint8_t usage = 0;         // image header byte 3 (0 colour, 1 normal map, 3, 4 cube/env, 5 volume) [I]
    bool supported = true;     // false: a volume texture (no device kind yet); `levels` empty
    // levels[face * mips + level], PC order (BC1/BC3/BC5 blocks or RGBA8 texels)
    std::vector<Bytes> levels;

    gfx::TextureDesc desc() const { return {width, height, mips, format, kind}; }
    // TexLevel views of `levels` for Device::createTexture (valid while *this lives)
    std::vector<gfx::TexLevel> texLevels() const;
};

// One .img file (as stored in a pak, or a loose .img.xen after inflating).
// Throws std::runtime_error on malformed data.
Texture decodeImg(const uint8_t* data, size_t size);
inline Texture decodeImg(const Bytes& b) { return decodeImg(b.data(), b.size()); }

// Every texture of a .tex dictionary, in table order. Throws std::runtime_error.
std::vector<Texture> decodeTexDict(const uint8_t* data, size_t size);
inline std::vector<Texture> decodeTexDict(const Bytes& b) { return decodeTexDict(b.data(), b.size()); }

}  // namespace gh3tex
