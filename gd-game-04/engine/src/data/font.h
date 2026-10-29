// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// GH3 fonts (.fnt), port of tools/anim.py parse_fnt [C: all 16 on the disc;
// XEX: fix-up Function_8232B458, text width Function_8232ADF0, drawing
// Function_8232B5E8]. parseFont throws std::runtime_error on malformed data.
namespace font {

struct Glyph {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // texture coordinates (0..1)
    float width = 0, height = 0;            // pixels; width is the advance
    float baseline = 0;                     // drawn at top = lineHeight - (height - baseline)
    int32_t xOffset = 0, advanceAdjust = 0; // [I] extra left offset / extra advance
};

struct Font {
    int32_t vShift = 0;    // header +0 (subtracted from the glyph top)
    int32_t bearing = 0;   // header +4 (per-glyph x offset)
    int32_t tracking = 0;  // header +8 (extra advance per glyph)
    float lineHeight = 0;  // header +0xC
    float spaceWidth = 0;  // +0x2005C (advance of ' ' when it has no glyph)
    std::vector<uint16_t> charMap;      // 65536 entries, UTF-16 unit -> glyph index (0 = glyph 0)
    std::array<uint16_t, 32> buttonMap{};  // "\b<n>" button glyphs
    std::vector<Glyph> glyphs;
    // The embedded texture: header at textureOffset (u16 width/height at
    // +8/+10), pixels from pixelOffset to the end of the file; decode them
    // with the texture decoder.
    uint32_t textureOffset = 0, pixelOffset = 0, pixelBytes = 0;
    uint16_t texWidth = 0, texHeight = 0;

    const Glyph& glyphFor(uint16_t unit) const { return glyphs[charMap[unit]]; }
    const Glyph& button(size_t i) const { return glyphs[buttonMap.at(i)]; }
};

Font parseFont(const uint8_t* data, size_t size);

}  // namespace font
