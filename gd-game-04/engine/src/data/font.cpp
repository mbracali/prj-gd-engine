// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/font.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "data/bytes.h"

namespace font {
namespace {

void need(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}

float f32be(ByteReader& r) {
    uint32_t u = r.u32be();
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

void zeros(const uint8_t* d, size_t a, size_t b, const char* what) {
    for (size_t i = a; i < b; ++i) need(d[i] == 0, what);
}

constexpr size_t kCharMap = 0x14, kButtonMap = 0x20014, kMarker = 0x20054, kTexOffset = 0x200A0,
                 kGlyphs = 0x200A4, kGlyphSize = 0x24;

}  // namespace

// Layout: i32 vShift, i32 bearing, i32 tracking, f32 line height, u32 glyph
// table offset (0x200A4); u16 char map[65536] at 0x14; u16 button map[32] at
// 0x20014; u32 0x0001DEAD, u32 -1 (next-font link), f32 space width at
// 0x20054; zeros; u32 texture offset at 0x200A0; glyphs of 9 x 4 bytes
// (7 f32, 2 i32) at 0x200A4, as many as the maps reference; zeros up to the
// 4 KiB-aligned texture.
Font parseFont(const uint8_t* d, size_t size) {
    need(size > kGlyphs, "fnt: short");
    ByteReader r(d, size);
    Font f;
    f.vShift = int32_t(r.u32be());
    f.bearing = int32_t(r.u32be());
    f.tracking = int32_t(r.u32be());
    f.lineHeight = f32be(r);
    need(r.u32be() == kGlyphs, "fnt: glyph table offset");
    f.charMap.resize(65536);
    uint16_t maxIndex = 0;
    for (auto& v : f.charMap) {
        v = r.u16be();
        maxIndex = std::max(maxIndex, v);
    }
    for (auto& v : f.buttonMap) {
        v = r.u16be();
        maxIndex = std::max(maxIndex, v);
    }
    need(r.pos() == kMarker && r.u32be() == 0x0001DEADu && r.u32be() == 0xFFFFFFFFu, "fnt: marker");
    f.spaceWidth = f32be(r);
    zeros(d, r.pos(), kTexOffset, "fnt: reserved bytes");
    r.seek(kTexOffset);
    f.textureOffset = r.u32be();
    const size_t count = size_t(maxIndex) + 1;
    const size_t glyphEnd = kGlyphs + kGlyphSize * count;
    need(glyphEnd <= f.textureOffset && f.textureOffset % 0x1000 == 0 && f.textureOffset + 0x5C < size,
         "fnt: texture offset");
    f.glyphs.resize(count);
    for (auto& g : f.glyphs) {
        float* fl[7] = {&g.u0, &g.v0, &g.u1, &g.v1, &g.width, &g.height, &g.baseline};
        for (int i = 0; i < 7; ++i) {
            *fl[i] = f32be(r);
            need(std::isfinite(*fl[i]), "fnt: glyph value");
            if (i < 4) need(*fl[i] >= 0.0f && *fl[i] <= 1.0f, "fnt: glyph uv");
        }
        g.xOffset = int32_t(r.u32be());
        g.advanceAdjust = int32_t(r.u32be());
    }
    zeros(d, glyphEnd, f.textureOffset, "fnt: glyph padding");
    // Texture: 0x28-byte header (magic 0x0A281102, u16 width, height at +8),
    // +0x1C = 0x28 (GPU header that follows); pixels at the next 4 KiB page
    // past tex + 0x5B (Function_8232B458: (tex + 0x105B) & ~0xFFF).
    r.seek(f.textureOffset);
    need(r.u32be() == 0x0A281102u, "fnt: texture magic");
    r.seek(f.textureOffset + 8);
    f.texWidth = r.u16be();
    f.texHeight = r.u16be();
    r.seek(f.textureOffset + 0x1C);
    need(r.u32be() == 0x28, "fnt: texture header");
    f.pixelOffset = (f.textureOffset + 0x105Bu) & ~0xFFFu;
    need(f.pixelOffset < size, "fnt: no pixels");
    zeros(d, f.textureOffset + 0x5C, f.pixelOffset, "fnt: texture header padding");
    f.pixelBytes = uint32_t(size - f.pixelOffset);
    return f;
}

}  // namespace font
