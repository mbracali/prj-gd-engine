// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/bitmap.h"

#include <algorithm>
#include <stdexcept>

Image decodeBitmap(const uint8_t* data, size_t size, size_t* consumed) {
    ByteReader r(data, size);
    if (r.u8() != 1) throw std::runtime_error("unsupported bitmap version");
    int bpp = r.u8();
    r.skip(4);
    int mips = r.u8();
    int w = r.u16le(), h = r.u16le(), bpl = r.u16le();
    if (bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24) throw std::runtime_error("unsupported bitmap depth");
    r.seek(32);

    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(size_t(w) * h * 4);

    if (bpp <= 8) {
        int colors = 1 << bpp;
        std::vector<uint8_t> pal(colors * 4);
        for (int i = 0; i < colors; ++i) {
            // 8-bit palettes are in GS CSM1 order: swap entries 8..15 and 16..23 of each 32.
            int dst = i;
            if (bpp == 8 && (i & 0x18) == 0x08) dst = i + 8;
            else if (bpp == 8 && (i & 0x18) == 0x10) dst = i - 8;
            for (int c = 0; c < 3; ++c) pal[dst * 4 + c] = r.u8();
            pal[dst * 4 + 3] = static_cast<uint8_t>(std::min(255, r.u8() * 2));
        }
        if (r.remaining() < size_t(bpl) * h) throw std::runtime_error("truncated bitmap");
        const uint8_t* pix = r.ptr();
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint8_t* row = pix + size_t(y) * bpl;
                int idx = bpp == 8 ? row[x] : (x & 1 ? row[x >> 1] >> 4 : row[x >> 1] & 0x0F);
                std::copy_n(&pal[idx * 4], 4, &img.rgba[(size_t(y) * w + x) * 4]);
            }
        }
    } else {
        if (r.remaining() < size_t(bpl) * h) throw std::runtime_error("truncated bitmap");
        const uint8_t* pix = r.ptr();
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                uint8_t* out = &img.rgba[(size_t(y) * w + x) * 4];
                const uint8_t* p = pix + size_t(y) * bpl + x * (bpp / 8);
                if (bpp == 24) {
                    out[0] = p[0], out[1] = p[1], out[2] = p[2], out[3] = 255;
                } else {
                    int v = p[0] | p[1] << 8;
                    out[0] = uint8_t((v & 31) << 3), out[1] = uint8_t((v >> 5 & 31) << 3);
                    out[2] = uint8_t((v >> 10 & 31) << 3), out[3] = v & 0x8000 ? 255 : 0;
                }
            }
        }
    }

    if (consumed) {
        size_t total = 32 + (bpp <= 8 ? (size_t(1) << bpp) * 4 : 0);
        for (int m = 0, rows = h, line = bpl; m <= mips; ++m, rows /= 2, line /= 2) total += size_t(line) * rows;
        *consumed = total;
    }
    return img;
}

void bleedTransparent(Image& img) {
    const int w = img.width, h = img.height;
    if (w <= 0 || h <= 0 || img.rgba.size() < size_t(w) * size_t(h) * 4) return;
    const std::vector<uint8_t> src = img.rgba;
    auto at = [&](int x, int y) { return &src[(size_t(y) * size_t(w) + size_t(x)) * 4]; };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (at(x, y)[3] != 0) continue;
            int sum[3] = {0, 0, 0}, n = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const uint8_t* q = at(nx, ny);
                    if (q[3] == 0) continue;
                    sum[0] += q[0], sum[1] += q[1], sum[2] += q[2], ++n;
                }
            if (!n) continue;
            uint8_t* d = &img.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
            for (int k = 0; k < 3; ++k) d[k] = uint8_t(sum[k] / n);
        }
}
