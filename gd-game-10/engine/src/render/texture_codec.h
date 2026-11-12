// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "render/device.h"

// Block-compressed texture helpers for GH2's bitmaps (docs/milo_and_textures.md,
// "Textures"): DXT1 = BC1, DXT5 = BC3, ATI2 = BC5, stored by the Xbox 360 in
// 16-bit big-endian words, untiled, mips largest first.
namespace gfx {

// Bytes of one level of a `format` texture of w x h (blocks round up to 4x4).
size_t levelSize(TexFormat format, int w, int h);

// Undoes the Xbox 360's 16-bit byte order in place (swaps every byte pair).
void swapBytes16(uint8_t* data, size_t size);

// Decodes one level to RGBA8 (w x h x 4 bytes). BC5 gives (x, y, 0, 255),
// as OpenGL's RG_RGTC2 samples it. Returns false if `size` is too small.
bool decodeToRgba8(TexFormat format, const uint8_t* data, size_t size, int w, int h, std::vector<uint8_t>& out);

}  // namespace gfx
