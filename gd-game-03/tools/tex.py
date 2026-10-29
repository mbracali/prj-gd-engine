#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Convert Guitar Hero (PS2) .bmp_ps2 / .png_ps2 textures to PNG.

Layout (observed): 32-byte header
  u8 version (1), u8 bits per pixel (4, 8, 16 or 24), u32 3,
  u8 mipmap count @6, u16 width @7, u16 height @9, u16 bytes per line @11
4/8 bpp: a palette of 2^bpp RGBA entries (PS2 alpha 0..0x80; 8-bit palettes
in the GS CSM1 order), then palette indices row by row (4-bit: low nibble
first). 16 bpp: RGBA5551; 24 bpp: RGB. Each mip level (half size) follows.

Usage: tex.py <in.bmp_ps2> <out.png> [--no-csm] [--raw-alpha]
"""
import struct
import sys

from PIL import Image


def expected_size(data):
    bpp, mips = data[1], data[6]
    w, h, bpl = struct.unpack_from("<HHH", data, 7)
    size = 32 + ((1 << bpp) * 4 if bpp <= 8 else 0)
    for _ in range(mips + 1):
        size += bpl * h
        bpl, h = bpl // 2, h // 2
    return size


def decode(data, csm=True, raw_alpha=False):
    bpp = data[1]
    w, h, bpl = struct.unpack_from("<HHH", data, 7)
    if bpp in (16, 24):
        out = []
        for y in range(h):
            row = data[32 + y * bpl:32 + (y + 1) * bpl]
            for x in range(w):
                if bpp == 24:
                    out.append((row[x * 3], row[x * 3 + 1], row[x * 3 + 2], 255))
                else:
                    v = row[x * 2] | row[x * 2 + 1] << 8
                    out.append(((v & 31) << 3, (v >> 5 & 31) << 3, (v >> 10 & 31) << 3, 255 if v & 0x8000 else 0))
        img = Image.new("RGBA", (w, h))
        img.putdata(out)
        return img
    colors = 1 << bpp
    pal_raw = data[32:32 + colors * 4]
    pal = [tuple(pal_raw[i * 4:i * 4 + 4]) for i in range(colors)]
    if bpp == 8 and csm:
        # Swap entries 8..15 with 16..23 in every block of 32.
        fixed = list(pal)
        for base in range(0, 256, 32):
            for i in range(8):
                fixed[base + 8 + i], fixed[base + 16 + i] = pal[base + 16 + i], pal[base + 8 + i]
        pal = fixed
    if not raw_alpha:
        pal = [(r, g, b, min(255, a * 2)) for r, g, b, a in pal]
    pix = data[32 + colors * 4:]
    out = []
    for y in range(h):
        row = pix[y * bpl:(y + 1) * bpl]
        for x in range(w):
            if bpp == 8:
                idx = row[x]
            else:
                byte = row[x >> 1]
                idx = (byte >> 4) if x & 1 else (byte & 0x0F)
            out.append(pal[idx])
    img = Image.new("RGBA", (w, h))
    img.putdata(out)
    return img


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    img = decode(open(sys.argv[1], "rb").read(), csm="--no-csm" not in sys.argv, raw_alpha="--raw-alpha" in sys.argv)
    img.save(sys.argv[2])
