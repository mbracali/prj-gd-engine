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

uint32_t be32at(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

constexpr size_t kMarker = 0x20054, kTexOffset = 0x200A0,
                 kGlyphs = 0x200A4, kGlyphSize = 0x24;

}  // namespace

// Layout: i32 vShift, i32 bearing, i32 tracking, f32 line height, u32 glyph
// table offset (0x200A4); u16 char map[65536] at 0x14; u16 button map[32] at
// 0x20014; u32 0x0001DEAD, u32 -1 (next-font link), f32 space width at
// 0x20054; zeros; u32 texture offset at 0x200A0; glyphs of 9 x 4 bytes
// (7 f32, 2 i32) at 0x200A4, as many as the maps reference; zeros up to the
// 4 KiB-aligned texture.
Font parseFont(const uint8_t* d, size_t size) {
    need(size > kGlyphs + 4, "fnt: short");
    ByteReader r(d, size);
    Font f;
    // gh4 (Metallica) adds one f32 after `tracking` (1.0 .. 10.0 on the disc
    // [C data]; meaning [I], kept as `scale`), which moves every
    // later field by 4: glyph table at 0x200A8 instead of 0x200A4.
    const size_t sh = (be32at(d + 0x14) == kGlyphs + 4 && be32at(d + 0x10) != kGlyphs) ? 4 : 0;
    // gh4 stores these three as f32 (whole numbers: bearing 10.0, tracking
    // -3.0 in fontgrid_text_a8) [C data: every font on the disc].
    auto field = [&r, sh]() { return sh ? int32_t(std::lround(f32be(r))) : int32_t(r.u32be()); };
    f.vShift = field();
    f.bearing = field();
    f.tracking = field();
    if (sh) f.scale = f32be(r);
    f.lineHeight = f32be(r);
    need(r.u32be() == kGlyphs + sh, "fnt: glyph table offset");
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
    need(r.pos() == kMarker + sh && r.u32be() == 0x0001DEADu && r.u32be() == 0xFFFFFFFFu, "fnt: marker");
    f.spaceWidth = f32be(r);
    zeros(d, r.pos(), kTexOffset + sh, "fnt: reserved bytes");
    r.seek(kTexOffset + sh);
    f.textureOffset = r.u32be();
    const size_t count = size_t(maxIndex) + 1;
    const size_t glyphEnd = kGlyphs + sh + kGlyphSize * count;
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
        // gh4: f32 too (A, T, Y: 3.0 / -3.0 in fontgrid_text_a8; read as
        // integers they threw the glyph and the rest of the line off screen:
        // "EASY" drawn as "E") [C data].
        g.xOffset = field();
        g.advanceAdjust = field();
    }
    zeros(d, glyphEnd, f.textureOffset, "fnt: glyph padding");
    // Texture: 0x28-byte header (magic 0x0A281102, u16 width, height at +8),
    // +0x1C = 0x28 (GPU header that follows); pixels at the next 4 KiB page
    // past tex + 0x5B (Function_8232B458: (tex + 0x105B) & ~0xFFF).
    r.seek(f.textureOffset);
    {
        const uint32_t m = r.u32be();  // GH3 0x0A281102, gh4 0x0A281100 (class 0) [C data]
        need(m == 0x0A281102u || m == 0x0A281100u, "fnt: texture magic");
    }
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

namespace font {

gh3tex::Texture decodeFontTexture(const Font& f, const uint8_t* data, size_t size) {
    constexpr size_t kHeaderBytes = 0x28 + 0x34, kData = 0x1000;
    if (size_t(f.textureOffset) + kHeaderBytes > size || f.pixelOffset > size) throw std::runtime_error("font texture outside the file");
    const size_t pixels = size - size_t(f.pixelOffset);
    Bytes img(kData + pixels, 0);
    std::memcpy(img.data(), data + f.textureOffset, kHeaderBytes);
    if (pixels) std::memcpy(img.data() + kData, data + f.pixelOffset, pixels);
    return gh3tex::decodeImg(img);
}

}  // namespace font
