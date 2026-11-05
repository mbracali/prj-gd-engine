// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <vector>

// Enhanced Options > Sharper Text and Sprites (gd01's
// gd-game-01/engine/src/render/image_enhance.h): a menu or font texture
// (RGBA8) upscaled x4 up to 256 px, x2 up to 512 px, with a Catmull-Rom
// filter on premultiplied colour; mostly hard-edged alpha (glyphs,
// cut-outs) is tightened back to a crisp edge. Larger images are left as
// they are (false).
namespace gfx {
bool enhanceImage(std::vector<uint8_t>& rgba, int& width, int& height);
// The mip chain below an RGBA8 level (2x2 box filter), smallest last.
std::vector<std::vector<uint8_t>> mipChain(const std::vector<uint8_t>& rgba, int width, int height);
}  // namespace gfx
