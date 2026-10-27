#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero II (Xbox 360) textures: .bmp_xbox / .png_xbox and Tex objects in milos.

    tex.py info <file>...                 header of each texture
    tex.py png  <file> <out.png> [mip]    convert to PNG (mip 0 = full size)
    tex.py milo <file.milo_xbox> <out dir>  every inline Tex bitmap in a milo -> PNG
    tex.py all  <dir> <out dir>           every .bmp_xbox/.png_xbox below <dir> -> PNG

Bitmap layout (see docs/milo_and_textures.md), all little-endian:
    u8  version (1)
    u8  bits per pixel (4 for DXT1, 8 for DXT5 / ATI2)
    u32 encoding: 8 = DXT1 (BC1), 24 = DXT5 (BC3), 32 = ATI2 (BC5, normal maps)
    u8  extra mip levels (0 = only the full-size image)
    u16 width, u16 height, u16 bytes per line (of 4-pixel-high block rows / 4)
    19 bytes zero (header is 32 bytes)
    then each mip level, largest first, as packed 4x4 blocks in linear
    (row-major) order: no Xbox 360 tiling and no padding.
The block data is in Xbox 360 GPU byte order: every 16-bit word is
byte-swapped compared with the PC DXT layout.
"""
import os
import struct
import sys
import zlib

HEADER = 32
ENCODINGS = {8: ("DXT1", 8), 24: ("DXT5", 16), 32: ("ATI2", 16)}


def parse_header(d, pos=0):
    ver, bpp, enc, mips, w, h, bpl = struct.unpack_from("<BBIBHHH", d, pos)
    return {"version": ver, "bpp": bpp, "encoding": enc, "mips": mips,
            "width": w, "height": h, "bpl": bpl}


def level_sizes(h):
    """(width, height, byte size) of every mip level."""
    name, block = ENCODINGS.get(h["encoding"], (None, None))
    w, hh, out = h["width"], h["height"], []
    for _ in range(h["mips"] + 1):
        if block:
            size = max(1, (w + 3) // 4) * max(1, (hh + 3) // 4) * block
        else:
            size = w * hh * h["bpp"] // 8
        out.append((w, hh, size))
        w, hh = max(1, w // 2), max(1, hh // 2)
    return out


def total_size(h):
    return sum(s for _, _, s in level_sizes(h))


def swap16(b):
    a = bytearray(b)
    a[0::2], a[1::2] = b[1::2], b[0::2]
    return bytes(a)


def rgb565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def color_block(b, o, out, opaque):
    """BC1 colour block at b[o:o+8] -> 16 RGB(A) tuples."""
    c0, c1, idx = struct.unpack_from("<HHI", b, o)
    p0, p1 = rgb565(c0), rgb565(c1)
    if c0 > c1 or opaque:
        pal = [p0 + (255,), p1 + (255,),
               tuple((2 * x + y) // 3 for x, y in zip(p0, p1)) + (255,),
               tuple((x + 2 * y) // 3 for x, y in zip(p0, p1)) + (255,)]
    else:
        pal = [p0 + (255,), p1 + (255,),
               tuple((x + y) // 2 for x, y in zip(p0, p1)) + (255,), (0, 0, 0, 0)]
    for i in range(16):
        out[i] = pal[(idx >> (2 * i)) & 3]


def alpha_block(b, o):
    """BC3/BC4 interpolated 8-bit channel block at b[o:o+8] -> 16 values."""
    a0, a1 = b[o], b[o + 1]
    bits = int.from_bytes(b[o + 2:o + 8], "little")
    if a0 > a1:
        pal = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        pal = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    return [pal[(bits >> (3 * i)) & 7] for i in range(16)]


def decode(h, data):
    """One level's block data (already in PC byte order) -> RGBA bytes."""
    w, hh = h["width"], h["height"]
    name, block = ENCODINGS[h["encoding"]]
    bw, bh = max(1, (w + 3) // 4), max(1, (hh + 3) // 4)
    out = bytearray(w * hh * 4)
    px = [None] * 16
    for by in range(bh):
        for bx in range(bw):
            o = (by * bw + bx) * block
            if name == "DXT1":
                color_block(data, o, px, False)
            elif name == "DXT5":
                a = alpha_block(data, o)
                color_block(data, o + 8, px, True)
                px = [p[:3] + (a[i],) for i, p in enumerate(px)]
            else:  # ATI2: X then Y channel, rebuild Z for a viewable normal map
                x, y = alpha_block(data, o), alpha_block(data, o + 8)
                for i in range(16):
                    nx, ny = x[i] / 127.5 - 1, y[i] / 127.5 - 1
                    nz = max(0.0, 1 - nx * nx - ny * ny) ** 0.5
                    px[i] = (x[i], y[i], int(nz * 127.5 + 127.5), 255)
            for i in range(16):
                X, Y = bx * 4 + (i & 3), by * 4 + (i >> 2)
                if X < w and Y < hh:
                    q = (Y * w + X) * 4
                    out[q:q + 4] = bytes(px[i])
    return bytes(out)


def to_rgba(d, pos=0, mip=0):
    """Bitmap at d[pos:] -> (width, height, RGBA bytes) of the given mip."""
    h = parse_header(d, pos)
    if h["encoding"] not in ENCODINGS:
        raise ValueError(f"unknown encoding {h['encoding']}")
    off = pos + HEADER
    for i, (w, hh, size) in enumerate(level_sizes(h)):
        if i == mip:
            lvl = dict(h, width=w, height=hh)
            return w, hh, decode(lvl, swap16(d[off:off + size]))
        off += size
    raise ValueError("no such mip")


def write_png(path, w, h, rgba):
    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))
    raw = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def describe(h):
    name = ENCODINGS.get(h["encoding"], (f"enc{h['encoding']}",))[0]
    return (f"{h['width']}x{h['height']} {name} bpp={h['bpp']} "
            f"mips={h['mips'] + 1} bpl={h['bpl']} v{h['version']}")


def main():
    a = sys.argv[1:]
    if not a:
        raise SystemExit(__doc__)
    if a[0] == "info":
        for p in a[1:]:
            d = open(p, "rb").read()
            h = parse_header(d)
            ok = "ok" if total_size(h) + HEADER == len(d) else f"size {len(d)}?"
            print(f"{p}: {describe(h)} {ok}")
    elif a[0] == "png" and len(a) >= 3:
        d = open(a[1], "rb").read()
        w, h, px = to_rgba(d, 0, int(a[3]) if len(a) > 3 else 0)
        write_png(a[2], w, h, px)
    elif a[0] == "milo" and len(a) == 3:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import milo
        os.makedirs(a[2], exist_ok=True)
        for cls, name, rec in milo.objects(open(a[1], "rb").read()):
            if cls != "Tex":
                continue
            t = milo.tex_info(rec)
            hdr = parse_header(rec, t["bitmap"]) if t["bitmap"] is not None else None
            if hdr is None or hdr["width"] == 0:
                print(f"{name}: no bitmap (type {t['type']:#x}, {t['width']}x{t['height']})")
                continue
            w, h, px = to_rgba(rec, t["bitmap"])
            write_png(os.path.join(a[2], name + ".png"), w, h, px)
            print(f"{name}: {describe(hdr)}")
    elif a[0] == "all" and len(a) == 3:
        n = 0
        for base, _, files in os.walk(a[1]):
            for f in files:
                if not f.endswith((".bmp_xbox", ".png_xbox")):
                    continue
                src = os.path.join(base, f)
                dst = os.path.join(a[2], os.path.relpath(src, a[1]) + ".png")
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                w, h, px = to_rgba(open(src, "rb").read())
                write_png(dst, w, h, px)
                n += 1
        print(n, "textures converted")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
