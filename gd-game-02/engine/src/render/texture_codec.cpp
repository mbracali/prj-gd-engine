// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/texture_codec.h"

#include <algorithm>
#include <utility>

namespace gfx {
namespace {

size_t blockBytes(TexFormat f) { return f == TexFormat::BC1 ? 8 : 16; }

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

// The four colours of a BC1-style colour block, RGBA8. `allowAlpha`: the
// "c0 <= c1" mode gives a transparent black fourth colour (BC1 only).
void colorPalette(const uint8_t* b, bool allowAlpha, uint8_t pal[4][4]) {
    const uint16_t c0 = le16(b), c1 = le16(b + 2);
    auto expand = [](uint16_t c, uint8_t* o) {
        o[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
        o[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
        o[2] = uint8_t((c & 31) * 255 / 31);
        o[3] = 255;
    };
    expand(c0, pal[0]);
    expand(c1, pal[1]);
    if (c0 > c1 || !allowAlpha) {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k] + 1) / 3);
            pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k] + 1) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
        pal[2][3] = 255;
        pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
    }
}

// The 16 values of a BC3 alpha / BC4 channel block.
void channelBlock(const uint8_t* b, uint8_t out[16]) {
    const int a0 = b[0], a1 = b[1];
    int v[8] = {a0, a1};
    if (a0 > a1) {
        for (int i = 1; i < 7; ++i) v[i + 1] = ((7 - i) * a0 + i * a1 + 3) / 7;
    } else {
        for (int i = 1; i < 5; ++i) v[i + 1] = ((5 - i) * a0 + i * a1 + 2) / 5;
        v[6] = 0, v[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) out[i] = uint8_t(v[(bits >> (3 * i)) & 7]);
}

}  // namespace

size_t levelSize(TexFormat format, int w, int h) {
    if (format == TexFormat::RGBA8) return size_t(w) * size_t(h) * 4;
    return size_t((w + 3) / 4) * size_t((h + 3) / 4) * blockBytes(format);
}

void swapBytes16(uint8_t* data, size_t size) {
    for (size_t i = 0; i + 1 < size; i += 2) std::swap(data[i], data[i + 1]);
}

bool decodeToRgba8(TexFormat format, const uint8_t* data, size_t size, int w, int h, std::vector<uint8_t>& out) {
    out.assign(size_t(w) * size_t(h) * 4, 0);
    if (format == TexFormat::RGBA8) {
        if (size < out.size()) return false;
        std::copy_n(data, out.size(), out.begin());
        return true;
    }
    if (size < levelSize(format, w, h)) return false;
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    const size_t bb = blockBytes(format);
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const uint8_t* b = data + (size_t(by) * size_t(bw) + size_t(bx)) * bb;
            uint8_t px[16][4];
            if (format == TexFormat::BC5) {
                uint8_t x[16], y[16];
                channelBlock(b, x);
                channelBlock(b + 8, y);
                for (int i = 0; i < 16; ++i) px[i][0] = x[i], px[i][1] = y[i], px[i][2] = 0, px[i][3] = 255;
            } else {
                const uint8_t* cb = format == TexFormat::BC3 ? b + 8 : b;
                uint8_t pal[4][4];
                colorPalette(cb, format == TexFormat::BC1, pal);
                const uint32_t idx = uint32_t(cb[4]) | uint32_t(cb[5]) << 8 | uint32_t(cb[6]) << 16 | uint32_t(cb[7]) << 24;
                for (int i = 0; i < 16; ++i) std::copy_n(pal[(idx >> (2 * i)) & 3], 4, px[i]);
                if (format == TexFormat::BC3) {
                    uint8_t a[16];
                    channelBlock(b, a);
                    for (int i = 0; i < 16; ++i) px[i][3] = a[i];
                }
            }
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + i % 4, y = by * 4 + i / 4;
                if (x < w && y < h) std::copy_n(px[i], 4, &out[(size_t(y) * size_t(w) + size_t(x)) * 4]);
            }
        }
    return true;
}

}  // namespace gfx
