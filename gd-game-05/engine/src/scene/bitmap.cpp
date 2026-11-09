// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "scene/bitmap.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scene {

namespace {

Bitmap::Format formatOf(int encoding) {
    switch (encoding) {
        case 0: return Bitmap::Format::None;
        case 8: return Bitmap::Format::DXT1;
        case 24: return Bitmap::Format::DXT5;
        case 32: return Bitmap::Format::ATI2;
        default: return Bitmap::Format::Other;
    }
}

void rgb565(uint16_t c, uint8_t out[3]) {
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    out[0] = uint8_t(r << 3 | r >> 2);
    out[1] = uint8_t(g << 2 | g >> 4);
    out[2] = uint8_t(b << 3 | b >> 2);
}

// BC1 colour block -> 16 RGBA texels. `opaque` forces four-colour mode (BC3).
void colorBlock(const uint8_t* b, uint8_t px[16][4], bool opaque) {
    uint16_t c0 = uint16_t(b[0] | b[1] << 8), c1 = uint16_t(b[2] | b[3] << 8);
    uint32_t idx = uint32_t(b[4]) | uint32_t(b[5]) << 8 | uint32_t(b[6]) << 16 | uint32_t(b[7]) << 24;
    uint8_t pal[4][4];
    rgb565(c0, pal[0]);
    rgb565(c1, pal[1]);
    pal[0][3] = pal[1][3] = 255;
    for (int k = 0; k < 3; ++k) {
        if (c0 > c1 || opaque) {
            pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3);
            pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3);
        } else {
            pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
            pal[3][k] = 0;
        }
    }
    pal[2][3] = 255;
    pal[3][3] = (c0 > c1 || opaque) ? 255 : 0;
    for (int i = 0; i < 16; ++i)
        for (int k = 0; k < 4; ++k) px[i][k] = pal[(idx >> (2 * i)) & 3][k];
}

// BC3 alpha / BC4 channel block -> 16 values.
void channelBlock(const uint8_t* b, uint8_t out[16]) {
    int a0 = b[0], a1 = b[1];
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
    int pal[8] = {a0, a1};
    if (a0 > a1) {
        for (int i = 1; i < 7; ++i) pal[1 + i] = ((7 - i) * a0 + i * a1) / 7;
    } else {
        for (int i = 1; i < 5; ++i) pal[1 + i] = ((5 - i) * a0 + i * a1) / 5;
        pal[6] = 0;
        pal[7] = 255;
    }
    for (int i = 0; i < 16; ++i) out[i] = uint8_t(pal[(bits >> (3 * i)) & 7]);
}

}  // namespace

size_t bitmapLevelSize(Bitmap::Format format, int bpp, int w, int h) {
    size_t bw = size_t(std::max(1, (w + 3) / 4)), bh = size_t(std::max(1, (h + 3) / 4));
    switch (format) {
        case Bitmap::Format::DXT1: return bw * bh * 8;
        case Bitmap::Format::DXT5:
        case Bitmap::Format::ATI2: return bw * bh * 16;
        default: return size_t(w) * size_t(h) * size_t(bpp) / 8;
    }
}

Bitmap readBitmap(ByteReader& r) {
    Bitmap b;
    b.version = r.u8();
    b.bpp = r.u8();
    b.encoding = b.version != 0 ? int(r.u32le()) : r.u8();
    b.extraMips = r.u8();
    b.width = r.u16le();
    b.height = r.u16le();
    b.bytesPerLine = r.u16le();
    r.skip(b.version != 0 ? 19 : 6);
    b.format = formatOf(b.encoding);
    // A palette only for paletted encodings (bpp <= 8, no 0x38 bits): never in GH2.
    if (b.bpp <= 8 && (b.encoding & 0x38) == 0) r.skip(size_t(4) << b.bpp);
    if (b.width == 0 || b.height == 0) return b;  // runtime placeholder (render targets, photos)

    // Level 0 is bytesPerLine x height bytes; each smaller level is
    // (width x bpp / 8) x height of the halved size (RndBitmap::Create @ 0x821f02c0).
    int w = b.width, h = b.height;
    size_t total = 0;
    for (int i = 0; i <= b.extraMips; ++i) {
        size_t bpl = i == 0 ? size_t(b.bytesPerLine) : size_t(w) * size_t(b.bpp) / 8;
        Bitmap::Level lv{w, h, total, bpl * size_t(h)};
        b.levels.push_back(lv);
        total += lv.size;
        w >>= 1;
        h >>= 1;
    }
    if (total > r.remaining()) throw std::runtime_error("bitmap data runs past the record");
    b.data.assign(r.ptr(), r.ptr() + total);
    r.skip(total);
    if (b.format == Bitmap::Format::DXT1 || b.format == Bitmap::Format::DXT5 || b.format == Bitmap::Format::ATI2)
        for (size_t i = 0; i + 1 < b.data.size(); i += 2) std::swap(b.data[i], b.data[i + 1]);
    return b;
}

bool decodeBitmapRgba(const Bitmap& bmp, size_t level, std::vector<uint8_t>& out) {
    if (level >= bmp.levels.size()) return false;
    const auto& lv = bmp.levels[level];
    int w = lv.width, h = lv.height;
    size_t blockBytes;
    if (bmp.format == Bitmap::Format::RGBA8) {
        if (lv.size < size_t(w) * size_t(h) * 4) return false;
        out.assign(bmp.level(level), bmp.level(level) + size_t(w) * size_t(h) * 4);
        return true;
    }
    switch (bmp.format) {
        case Bitmap::Format::DXT1: blockBytes = 8; break;
        case Bitmap::Format::DXT5:
        case Bitmap::Format::ATI2: blockBytes = 16; break;
        default: return false;
    }
    out.assign(size_t(w) * size_t(h) * 4, 0);
    int bw = std::max(1, (w + 3) / 4), bh = std::max(1, (h + 3) / 4);
    const uint8_t* src = bmp.level(level);
    uint8_t px[16][4], a[16], y[16];
    uint8_t blank[16] = {};
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            size_t o = (size_t(by) * size_t(bw) + size_t(bx)) * blockBytes;
            // Levels below one block hold fewer bytes than a block (see readBitmap).
            const uint8_t* blk = o + blockBytes <= lv.size ? src + o : blank;
            if (bmp.format == Bitmap::Format::DXT1) {
                colorBlock(blk, px, false);
            } else if (bmp.format == Bitmap::Format::DXT5) {
                channelBlock(blk, a);
                colorBlock(blk + 8, px, true);
                for (int i = 0; i < 16; ++i) px[i][3] = a[i];
            } else {
                channelBlock(blk, a);
                channelBlock(blk + 8, y);
                for (int i = 0; i < 16; ++i) {
                    float nx = a[i] / 127.5f - 1, ny = y[i] / 127.5f - 1;
                    float nz = std::sqrt(std::max(0.0f, 1 - nx * nx - ny * ny));
                    px[i][0] = a[i];
                    px[i][1] = y[i];
                    px[i][2] = uint8_t(nz * 127.5f + 127.5f);
                    px[i][3] = 255;
                }
            }
            for (int i = 0; i < 16; ++i) {
                int X = bx * 4 + (i & 3), Y = by * 4 + (i >> 2);
                if (X < w && Y < h)
                    for (int k = 0; k < 4; ++k) out[(size_t(Y) * size_t(w) + size_t(X)) * 4 + size_t(k)] = px[i][k];
            }
        }
    return true;
}

}  // namespace scene
