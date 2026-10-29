#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""PS2 IPU movies (.ipu): list the frames, decode them byte for byte like the
engine (engine/src/video/ipu.cpp + mpeg2.cpp decodeIdec), print a hash per
frame, optionally write PNGs. An independent reimplementation to check the
C++ decoder: the 64-bit FNV-1a of each frame's RGBA must match what
`gd03 <iso> --venuetest` checks (tests/venuetest.cpp, fest_1.ipu).

File (docs/venues.md "fest_1.ipu"):
  'ipum', u32 size (not trusted), u16 width, u16 height, u32 frame count,
  per frame: u8 IPU_CTRL flags (IDP bits 0-1, DTD 2, AS 4, IVF 5, QST 6,
  MP1 7), raster-order intra macroblocks (no slices), zero bits to a byte,
  00 00 01 B0; 00 00 01 B1 at the end.
Pixels: IDEC with the default intra matrix and quantiser code 1, the float
IDCT of mpeg2.cpp (emulated in float32), IPU CSC in 7-bit fixed point to
RGB16 5:5:5:1 (alpha 0 for pure black), widened c << 3, alpha 0 / 0x80.

Usage: ipu.py <file.ipu> [--frames N] [--png <dir>]
"""
import math
import struct
import sys

# ---- VLC tables (ISO/IEC 13818-2 Annex B), value = (run, level) or a tag ----
EOB, ESC = "eob", "esc"

DC_LUMA = {"100": 0, "00": 1, "01": 2, "101": 3, "110": 4, "1110": 5, "11110": 6, "111110": 7,
           "1111110": 8, "11111110": 9, "111111110": 10, "111111111": 11}
DC_CHROMA = {"00": 0, "01": 1, "10": 2, "110": 3, "1110": 4, "11110": 5, "111110": 6, "1111110": 7,
             "11111110": 8, "111111110": 9, "1111111110": 10, "1111111111": 11}
MB_TYPE_I = {"1": False, "01": True}  # value: quant
MBA_ONE = {"1": 1}

LONG = [("0000000010110", (1, 6)), ("0000000010101", (1, 7)), ("0000000010100", (2, 5)),
        ("0000000010011", (3, 4)), ("0000000010010", (5, 3)), ("0000000010001", (9, 2)),
        ("0000000010000", (10, 2)), ("0000000011111", (22, 1)), ("0000000011110", (23, 1)),
        ("0000000011101", (24, 1)), ("0000000011100", (25, 1)), ("0000000011011", (26, 1))]
LONG += [("00000000011111", (0, 16)), ("00000000011110", (0, 17)), ("00000000011101", (0, 18)),
         ("00000000011100", (0, 19)), ("00000000011011", (0, 20)), ("00000000011010", (0, 21)),
         ("00000000011001", (0, 22)), ("00000000011000", (0, 23)), ("00000000010111", (0, 24)),
         ("00000000010110", (0, 25)), ("00000000010101", (0, 26)), ("00000000010100", (0, 27)),
         ("00000000010011", (0, 28)), ("00000000010010", (0, 29)), ("00000000010001", (0, 30)),
         ("00000000010000", (0, 31))]
LONG += [("000000000011000", (0, 32)), ("000000000010111", (0, 33)), ("000000000010110", (0, 34)),
         ("000000000010101", (0, 35)), ("000000000010100", (0, 36)), ("000000000010011", (0, 37)),
         ("000000000010010", (0, 38)), ("000000000010001", (0, 39)), ("000000000010000", (0, 40)),
         ("000000000011111", (1, 8)), ("000000000011110", (1, 9)), ("000000000011101", (1, 10)),
         ("000000000011100", (1, 11)), ("000000000011011", (1, 12)), ("000000000011010", (1, 13)),
         ("000000000011001", (1, 14))]
LONG += [("0000000000010011", (1, 15)), ("0000000000010010", (1, 16)), ("0000000000010001", (1, 17)),
         ("0000000000010000", (1, 18)), ("0000000000010100", (6, 3)), ("0000000000011010", (11, 2)),
         ("0000000000011001", (12, 2)), ("0000000000011000", (13, 2)), ("0000000000010111", (14, 2)),
         ("0000000000010110", (15, 2)), ("0000000000010101", (16, 2)), ("0000000000011111", (27, 1)),
         ("0000000000011110", (28, 1)), ("0000000000011101", (29, 1)), ("0000000000011100", (30, 1)),
         ("0000000000011011", (31, 1))]

MID = [("000000011101", (0, 8)), ("000000011000", (0, 9)), ("000000010011", (0, 10)),
       ("000000010000", (0, 11)), ("000000011011", (1, 5)), ("000000010100", (2, 4)),
       ("000000011100", (3, 3)), ("000000010010", (4, 3)), ("000000011110", (6, 2)),
       ("000000010101", (7, 2)), ("000000010001", (8, 2)), ("000000011111", (17, 1)),
       ("000000011010", (18, 1)), ("000000011001", (19, 1)), ("000000010111", (20, 1)),
       ("000000010110", (21, 1))]

B14 = [("10", EOB), ("11", (0, 1)), ("011", (1, 1)), ("0100", (0, 2)), ("0101", (2, 1)),
       ("00101", (0, 3)), ("00111", (3, 1)), ("00110", (4, 1)), ("000110", (1, 2)), ("000111", (5, 1)),
       ("000101", (6, 1)), ("000100", (7, 1)), ("0000110", (0, 4)), ("0000100", (2, 2)),
       ("0000111", (8, 1)), ("0000101", (9, 1)), ("000001", ESC), ("00100110", (0, 5)),
       ("00100001", (0, 6)), ("00100101", (1, 3)), ("00100100", (3, 2)), ("00100111", (10, 1)),
       ("00100011", (11, 1)), ("00100010", (12, 1)), ("00100000", (13, 1)), ("0000001010", (0, 7)),
       ("0000001100", (1, 4)), ("0000001011", (2, 3)), ("0000001111", (4, 2)), ("0000001001", (5, 2)),
       ("0000001110", (14, 1)), ("0000001101", (15, 1)), ("0000001000", (16, 1)),
       ("0000000011010", (0, 12)), ("0000000011001", (0, 13)), ("0000000011000", (0, 14)),
       ("0000000010111", (0, 15))] + MID + LONG

B15 = [("0110", EOB), ("10", (0, 1)), ("010", (1, 1)), ("110", (0, 2)), ("00101", (2, 1)),
       ("0111", (0, 3)), ("00111", (3, 1)), ("000110", (4, 1)), ("00110", (1, 2)), ("000111", (5, 1)),
       ("0000110", (6, 1)), ("0000100", (7, 1)), ("11100", (0, 4)), ("0000111", (2, 2)),
       ("0000101", (8, 1)), ("1111000", (9, 1)), ("000001", ESC), ("11101", (0, 5)), ("000101", (0, 6)),
       ("1111001", (1, 3)), ("00100110", (3, 2)), ("1111010", (10, 1)), ("00100001", (11, 1)),
       ("00100101", (12, 1)), ("00100100", (13, 1)), ("000100", (0, 7)), ("00100111", (1, 4)),
       ("11111100", (2, 3)), ("11111101", (4, 2)), ("000000100", (5, 2)), ("000000101", (14, 1)),
       ("000000111", (15, 1)), ("0000001101", (16, 1)), ("1111011", (0, 8)), ("1111100", (0, 9)),
       ("00100011", (0, 10)), ("00100010", (0, 11)), ("00100000", (1, 5)), ("0000001100", (2, 4)),
       ("11111010", (0, 12)), ("11111011", (0, 13)), ("11111110", (0, 14)), ("11111111", (0, 15))]
B15 += [c for c in MID if c[1] not in {(0, 8), (0, 9), (0, 10), (0, 11), (1, 5), (2, 4)}] + LONG


def vlc(codes):
    table = {}
    for bits, v in (codes.items() if isinstance(codes, dict) else codes):
        key = (len(bits), int(bits, 2))
        assert key not in table, bits
        table[key] = v
    return table, sorted({k[0] for k in table})


T_DC = (vlc(DC_LUMA), vlc(DC_CHROMA))
T_MBTYPE = vlc(MB_TYPE_I)
T_MBA = vlc(MBA_ONE)
T_DCT = (vlc(B14), vlc(B15))

SCAN = (
    [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,
     7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31,
     39, 46, 53, 60, 61, 54, 47, 55, 62, 63],
    [0, 8, 16, 24, 1, 9, 2, 10, 17, 25, 32, 40, 48, 56, 57, 49, 41, 33, 26, 18, 3, 11, 4, 12, 19, 27, 34, 42,
     50, 58, 35, 43, 51, 59, 20, 28, 5, 13, 6, 14, 21, 29, 36, 44, 52, 60, 37, 45, 53, 61, 22, 30, 7, 15,
     23, 31, 38, 46, 54, 62, 39, 47, 55, 63])
INTRA_Q = [8, 16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34, 34, 38,
           22, 22, 26, 27, 29, 34, 37, 40, 22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58,
           26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83]
NONLINEAR_Q = [0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 40, 44, 48, 52,
               56, 64, 72, 80, 88, 96, 104, 112]

_F = struct.Struct("<f")


def f32(x):
    return _F.unpack(_F.pack(x))[0]


# idct[x][u] = C(u)/2 cos((2x+1)u pi/16), stored as float like mpeg2.cpp.
IDCT = [[f32((0.5 if u else 0.5 / math.sqrt(2.0)) * math.cos((2 * x + 1) * u * 3.14159265358979323846 / 16))
         for u in range(8)] for x in range(8)]


def idct8x8(coef):
    tmp = [0.0] * 64
    for y in range(8):
        r = coef[y * 8:y * 8 + 8]
        if not any(r):
            continue
        for x in range(8):
            c, s = IDCT[x], 0.0
            for u in range(8):
                s = f32(s + f32(c[u] * r[u]))
            tmp[y * 8 + x] = s
    out = [0] * 64
    for x in range(8):
        col = [tmp[v * 8 + x] for v in range(8)]
        for y in range(8):
            c, s = IDCT[y], 0.0
            for v in range(8):
                s = f32(s + f32(c[v] * col[v]))
            i = math.floor(f32(s + 0.5))
            out[y * 8 + x] = min(max(i, -256), 255)
    return out


class Bits:
    def __init__(self, data):
        self.total = len(data) * 8 + 256
        self.v = int.from_bytes(bytes(data) + bytes(32), "big")
        self.pos = 0

    def peek(self, n):
        return (self.v >> (self.total - self.pos - n)) & ((1 << n) - 1)

    def read(self, n):
        x = self.peek(n)
        self.pos += n
        return x

    def vlc(self, t):
        table, lens = t
        for n in lens:
            v = table.get((n, self.peek(n)))
            if v is not None:
                self.pos += n
                return v
        raise ValueError("invalid VLC code at bit %d" % self.pos)


def decode_picture(data, w, h, flags):
    if flags & 0x80:
        raise ValueError("MP1 not supported")
    idp, dtd, alt, ivf, qst = flags & 3, flags & 4, (flags >> 4) & 1, (flags >> 5) & 1, flags & 0x40
    mbw, mbh = w // 16, h // 16
    Y = bytearray(w * h)
    Cb = bytearray(w * h // 4)
    Cr = bytearray(w * h // 4)
    br = Bits(data)
    qfrom = (lambda c: NONLINEAR_Q[c]) if qst else (lambda c: 2 * c)
    qscale = qfrom(1)
    dc = [128 << idp] * 3
    scan = SCAN[alt]
    for addr in range(mbw * mbh):
        if addr and br.vlc(T_MBA) != 1:
            raise ValueError("skipped macroblocks")
        quant = br.vlc(T_MBTYPE)
        field = br.read(1) if dtd else 0
        if quant:
            qscale = qfrom(br.read(5))
        mx, my = addr % mbw, addr // mbw
        for b in range(6):
            cc = 0 if b < 4 else b - 3
            coef = [0] * 64
            size = br.vlc(T_DC[1 if cc else 0])
            diff = 0
            if size:
                diff = br.read(size)
                if diff < (1 << (size - 1)):
                    diff -= (1 << size) - 1
            dc[cc] += diff
            v = dc[cc] << (3 - idp)
            coef[0] = v
            total = v
            idx = 1
            table = T_DCT[ivf]
            while True:
                e = br.vlc(table)
                if e == EOB:
                    break
                if e == ESC:
                    run = br.read(6)
                    level = br.read(12)
                    if level >= 2048:
                        level -= 4096
                    if level == 0:
                        raise ValueError("escape level 0")
                else:
                    run, level = e
                    if br.read(1):
                        level = -level
                idx += run
                if idx > 63:
                    raise ValueError("coefficient index past 63")
                pos = scan[idx]
                idx += 1
                a = abs(level)
                v = (a * INTRA_Q[pos] * qscale) >> 4
                if level < 0:
                    v = -v
                v = min(max(v, -2048), 2047)
                coef[pos] = v
                total += v
            if not total & 1:
                coef[63] = coef[63] - 1 if coef[63] & 1 else coef[63] + 1
            res = idct8x8(coef)
            if b < 4:
                x0 = mx * 16 + (b & 1) * 8
                if field:
                    y0, step = my * 16 + (b >> 1), 2
                else:
                    y0, step = my * 16 + (b >> 1) * 8, 1
                for y in range(8):
                    o = (y0 + y * step) * w + x0
                    Y[o:o + 8] = bytes(min(max(t, 0), 255) for t in res[y * 8:y * 8 + 8])
            else:
                plane = Cb if b == 4 else Cr
                for y in range(8):
                    o = (my * 8 + y) * (w // 2) + mx * 8
                    plane[o:o + 8] = bytes(min(max(t, 0), 255) for t in res[y * 8:y * 8 + 8])
    if (br.pos + 7) // 8 != len(data):
        raise ValueError("data after the macroblocks")
    return Y, Cb, Cr


def to_rgba(Y, Cb, Cr, w, h):
    out = bytearray(w * h * 4)
    o = 0
    for y in range(h):
        for x in range(w):
            lum = (Y[y * w + x] - 16) * 149
            u = Cb[(y >> 1) * (w // 2) + (x >> 1)] - 128
            v = Cr[(y >> 1) * (w // 2) + (x >> 1)] - 128
            r = min(max((lum + 204 * v + 64) >> 7, 0), 255)
            g = min(max((lum - 104 * v - 50 * u + 64) >> 7, 0), 255)
            b = min(max((lum + 258 * u + 64) >> 7, 0), 255)
            out[o] = (r >> 3) << 3
            out[o + 1] = (g >> 3) << 3
            out[o + 2] = (b >> 3) << 3
            out[o + 3] = 0x80 if (r or g or b) else 0
            o += 4
    return out


def fnv(data, h=0xcbf29ce484222325):
    for c in data:
        h = ((h ^ c) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def frames(d):
    if d[:4] != b"ipum":
        raise ValueError("not an IPU movie")
    w, h, n = struct.unpack_from("<HHI", d, 8)
    pos, out = 16, []
    for i in range(n):
        flags = d[pos]
        end = d.find(b"\x00\x00\x01", pos + 1)
        if end < 0 or d[end + 3] != 0xB0:
            raise ValueError("frame %d has no 00 00 01 B0" % i)
        out.append((flags, pos + 1, end - pos - 1))
        pos = end + 4
    if d[pos:pos + 4] != b"\x00\x00\x01\xb1":
        raise ValueError("no 00 00 01 B1 after the last frame")
    return w, h, out


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    d = open(argv[1], "rb").read()
    limit = int(argv[argv.index("--frames") + 1]) if "--frames" in argv else None
    png = argv[argv.index("--png") + 1] if "--png" in argv else None
    w, h, fr = frames(d)
    print("%s: %dx%d, %d frames, header size %d (file %d)" % (argv[1], w, h, len(fr),
                                                               struct.unpack_from("<I", d, 4)[0], len(d)))
    total = 0xcbf29ce484222325
    for i, (flags, off, size) in enumerate(fr[:limit]):
        Y, Cb, Cr = decode_picture(d[off:off + size], w, h, flags)
        rgba = to_rgba(Y, Cb, Cr, w, h)
        total = fnv(rgba, total)
        print("frame %3d  flags 0x%02x  %5d bytes  rgba fnv %016x" % (i, flags, size, fnv(rgba)))
        if png:
            from PIL import Image
            Image.frombytes("RGBA", (w, h), bytes(c if k % 4 != 3 else min(255, c * 2)
                                                 for k, c in enumerate(rgba))).save("%s/frame%03d.png" % (png, i))
    print("all frames rgba fnv %016x" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
