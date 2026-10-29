#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero III (Xbox 360) textures: single images (.img) and texture
dictionaries (.tex). Format notes: docs/formats.md ("Textures").

    img.py list   <file.pak.xen | file.img.xen>          the images and their layout
    img.py export <file.pak.xen | file.img.xen> <name> <out.png|out.ppm> [level]
                  <name>: an .img entry's name, or "<tex name>:<texture key or index>";
                  "-" for a loose .img.xen. Cube faces / volume slices side by side.
    img.py check  <rip dir> [--layout-only]
                  decode every image of every pak and every loose .img.xen; every
                  byte of every file accounted for (--layout-only: no decoding)

An image is a 0x28-byte header, a 0x34-byte D3D texture header (whose last six
words are the GPU texture fetch constant) and the texel data: Xenos-tiled
DXT1/DXT3/DXT5/DXN blocks or 8_8_8_8 / 16_16_FLOAT texels, stored in 16-bit
(blocks) or 32-bit (texels) big-endian words, mip levels <= 16 texels packed
together into one tile. A .tex holds many such headers and their data.
"""
import os
import struct
import sys
import zlib
from multiprocessing import Pool

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pak import Pak, all_paks, read_disc_file  # noqa: E402
from qbkey import qbkey, default_table  # noqa: E402

KEY_IMG = qbkey(".img")
KEY_TEX = qbkey(".tex")

TEX_MAGIC = 0xFACECAA7
IMG_VERSION = 0x0A28
IMG_HEADER = 0x28
D3D_HEADER = 0x34
D3D_FIXED = (0x00200003, 1, 0, 0, 0, 0xFFFF0000, 0xFFFF0000)  # Common, RefCount, Fence, ReadFence, Identifier, BaseFlush, MipFlush
IMG_DATA = 0x1000  # a lone .img's texel data starts at the next 4 KiB page

# GPU texture formats: name, block size in texels, bytes per block, header byte 0x15 (bits per texel), byte 0x16 codes
FORMATS = {
    0x06: ("8_8_8_8", 1, 4, 32, (0,)),
    0x12: ("DXT1", 4, 8, 4, (1, 2)),
    0x13: ("DXT3", 4, 16, 4, (2,)),
    0x14: ("DXT5", 4, 16, 8, (5,)),
    0x1F: ("16_16_FLOAT", 1, 4, 32, (0,)),
    0x31: ("DXN", 4, 16, 8, (6,)),
}
ENDIAN_FOR = {4: 1, 1: 2}  # 8in16 for blocks, 8in32 for 32-bit texels (by block width)
DIM_2D, DIM_3D, DIM_CUBE = 1, 2, 3


class TexError(Exception):
    pass


def align(x, a):
    return (x + a - 1) // a * a


def npot(x):
    p = 1
    while p < x:
        p *= 2
    return p


# --- the fetch constant --------------------------------------------------------

class Fetch:
    """The six words of a GPUTEXTURE_FETCH_CONSTANT, every field checked."""

    def __init__(self, w):
        self.words = w
        d0, d1, d2, d3, d4, d5 = w
        if d0 & 3 != 2:
            raise TexError("fetch constant type %d" % (d0 & 3))
        self.sign = (d0 >> 2) & 0xFF        # sign X/Y/Z/W, 2 bits each
        self.clamp = (d0 >> 10) & 0x1FF     # clamp X/Y/Z
        rest0 = (d0 >> 19) & 7              # signed_rf_mode, dim_tbd
        self.pitch = ((d0 >> 22) & 0x1FF) * 32  # texels
        self.tiled = d0 >> 31
        self.fmt = d1 & 0x3F
        self.endian = (d1 >> 6) & 3
        rest1 = (d1 >> 8) & 0xF             # request size, stacked, clamp policy
        self.base_address = d1 >> 12
        self.dim = (d5 >> 9) & 3
        if self.dim == DIM_3D:
            self.width = (d2 & 0x7FF) + 1
            self.height = ((d2 >> 11) & 0x7FF) + 1
            self.depth = (d2 >> 22) + 1
        else:
            self.width = (d2 & 0x1FFF) + 1
            self.height = ((d2 >> 13) & 0x1FFF) + 1
            self.depth = (d2 >> 26) + 1      # cube: 6 faces; 2D: 1
        self.num_format = d3 & 1
        self.swizzle = [(d3 >> (1 + 3 * i)) & 7 for i in range(4)]
        rest3 = d3 >> 13                    # exp adjust, filters
        self.mip_min = (d4 >> 2) & 0xF
        self.mip_max = (d4 >> 6) & 0xF
        rest4 = (d4 & 3) | (d4 >> 10)
        rest5 = d5 & 0x1FF                  # border colour, bcw, tri clamp, aniso bias
        self.packed = (d5 >> 11) & 1
        self.mip_address = d5 >> 12         # 4 KiB pages
        if self.fmt not in FORMATS:
            raise TexError("texture format 0x%02x" % self.fmt)
        bw = FORMATS[self.fmt][1]
        if self.endian != ENDIAN_FOR[bw]:
            raise TexError("endian %d for format %s" % (self.endian, FORMATS[self.fmt][0]))
        for what, v in (("dword0 bits 19-21", rest0), ("dword1 bits 8-11", rest1), ("base address", self.base_address),
                        ("dword4 lod/filter bits", rest4), ("min mip", self.mip_min), ("dword5 bits 0-8", rest5),
                        ("clamp", self.clamp)):
            if v:
                raise TexError("unexpected %s 0x%x" % (what, v))
        if (rest3, self.sign) not in ((0x0, 0), (0x0, 0x55)):
            raise TexError("unexpected filter/sign bits 0x%x 0x%x" % (rest3, self.sign))
        if self.dim == 0 or (self.dim == DIM_2D and self.depth != 1) or (self.dim == DIM_CUBE and self.depth != 6):
            raise TexError("dimension %d depth %d" % (self.dim, self.depth))
        if not self.packed:
            raise TexError("mips not packed")
        if any(s > 5 for s in self.swizzle):
            raise TexError("swizzle %r" % self.swizzle)

    @property
    def levels(self):
        return self.mip_max + 1


# --- tiling -------------------------------------------------------------------

def tiled_offset(x, y, pitch, log_bpb):
    """Byte offset of block (x, y) in a Xenos-tiled surface `pitch` blocks wide
    (a multiple of 32): 32x32-block macro tiles, row-major; inside, the bank and
    pipe interleave of the 360 GPU."""
    macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (log_bpb + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log_bpb
    off = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpb)) + ((y & 1) << 4)
    return (((off & ~511) << 3) + ((off & 448) << 2) + (off & 63) + ((y & 16) << 7)
            + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6))


_tables = {}


def offsets(wb, hb, pitch, bpb, tiled, ox=0, oy=0):
    """Byte offsets of the wb x hb blocks of a level, row-major (cached)."""
    k = (wb, hb, pitch, bpb, tiled, ox, oy)
    t = _tables.get(k)
    if t is None:
        if tiled:
            lb = bpb.bit_length() - 1
            t = [tiled_offset(x + ox, y + oy, pitch, lb) for y in range(hb) for x in range(wb)]
        else:
            t = [((y + oy) * pitch + x + ox) * bpb for y in range(hb) for x in range(wb)]
        if len(_tables) > 512:
            _tables.clear()
        _tables[k] = t
    return t


def tail_offset(w, h, wide):
    """Texel offset of a mip level (w x h) inside the packed mip tail. A wide
    texture (`wide`: base width > height) stacks its levels down the tile
    (y = h), the others across (x = w); levels under 4 texels on that side go
    the other way, at 4x their size."""
    if wide:
        return (0, h) if h >= 4 else (4 * w, 0)
    return (w, 0) if w >= 4 else (0, 4 * h)


class Level:
    __slots__ = ("level", "face", "w", "h", "wb", "hb", "addr", "pitch", "tiled", "ox", "oy", "src")

    def blocks(self, bpb):
        return [self.addr + o for o in offsets(self.wb, self.hb, self.pitch, bpb, self.tiled, self.ox, self.oy)]


def layout(f, base, mips):
    """The levels of fetch constant `f` with level 0 at `base` and levels 1+ at
    `mips` (file offsets): a list per level of a Level per face / slice."""
    name, bw, bpb, _, _ = FORMATS[f.fmt]
    faces = f.depth if f.dim == DIM_CUBE else 1
    out = []
    foot = []  # (offset, size) of every level / tail allocation
    addr = mips
    tail = None  # (addr, pitch, face stride) of the packed tail
    for i in range(f.levels):
        if i == 0:
            w, h = f.width, f.height
        else:
            w, h = max(1, npot(f.width) >> i), max(1, npot(f.height) >> i)
        wb, hb = (w + bw - 1) // bw, (h + bw - 1) // bw
        lv = []
        if f.packed and min(w, h) <= 16 and f.tiled:
            if tail is None:
                p = align(wb, 32)
                tail = (base if i == 0 else addr, p, align(p * align(hb, 32) * bpb, 4096))
                foot.extend((tail[0] + k * tail[2], tail[2]) for k in range(faces))
            if faces == 1:
                ox, oy = tail_offset(w, h, f.width > f.height)
                lv.append(_level(i, 0, w, h, wb, hb, tail[0], tail[1], 1, ox // bw, oy // bw))
            else:
                # a cube's tail holds each level as its six faces stacked
                # down one column (x = w); the 2x2 faces sit at (16, 8) over
                # the 16x16 faces' first columns; the 1x1 faces were not found
                # (made from the 2x2 ones) [C data: every cube on the disc]
                if bw != 1:
                    raise TexError("compressed cube map")
                for face in range(faces):
                    if w >= 4:
                        L = _level(i, face, w, h, wb, hb, tail[0], tail[1], 1, w, face * h)
                    elif w == 2:
                        L = _level(i, face, w, h, wb, hb, tail[0], tail[1], 1, 16, 8 + face * h)
                        L.src = "shared"
                    else:
                        L = _level(i, face, w, h, wb, hb, tail[0], tail[1], 1, 0, 0)
                        L.src = "derived"
                    lv.append(L)
        else:
            if not f.tiled:
                pitch = f.pitch // bw
                if i:
                    raise TexError("mips of a linear texture")
            else:
                pitch = align(wb, 32)
            fsize = align(pitch * align(hb, 32) * bpb, 4096)
            a = base if i == 0 else addr
            for face in range(faces):
                lv.append(_level(i, face, w, h, wb, hb, a + face * fsize, pitch, f.tiled, 0, 0))
            foot.append((a, fsize * faces))
            if i:
                addr += fsize * faces
        if i == 0 and f.tiled and f.pitch != align(wb, 32) * bw:
            raise TexError("pitch %d for width %d" % (f.pitch, f.width))
        out.append(lv)
    return out, foot


def _level(i, face, w, h, wb, hb, addr, pitch, tiled, ox, oy):
    L = Level()
    L.level, L.face, L.w, L.h, L.wb, L.hb, L.addr, L.pitch, L.tiled, L.ox, L.oy = i, face, w, h, wb, hb, addr, pitch, tiled, ox, oy
    L.src = None  # "shared": blocks also used by another level; "derived": not stored, box-filtered from the level above
    return L


# --- block decoding -----------------------------------------------------------

def _565(c):
    return ((c >> 11) * 255 + 15) // 31, (((c >> 5) & 63) * 255 + 31) // 63, ((c & 31) * 255 + 15) // 31


def _palette(c0, c1, alpha):
    r0, g0, b0 = _565(c0)
    r1, g1, b1 = _565(c1)
    if c0 > c1 or not alpha:
        return (bytes((r0, g0, b0, 255)), bytes((r1, g1, b1, 255)),
                bytes(((2 * r0 + r1 + 1) // 3, (2 * g0 + g1 + 1) // 3, (2 * b0 + b1 + 1) // 3, 255)),
                bytes(((r0 + 2 * r1 + 1) // 3, (g0 + 2 * g1 + 1) // 3, (b0 + 2 * b1 + 1) // 3, 255)))
    return (bytes((r0, g0, b0, 255)), bytes((r1, g1, b1, 255)),
            bytes(((r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2, 255)), b"\0\0\0\0")


def _channel(b, o):
    a0, a1 = b[o], b[o + 1]
    if a0 > a1:
        v = [a0, a1] + [((7 - i) * a0 + i * a1 + 3) // 7 for i in range(1, 7)]
    else:
        v = [a0, a1] + [((5 - i) * a0 + i * a1 + 2) // 5 for i in range(1, 5)] + [0, 255]
    bits = int.from_bytes(b[o + 2:o + 8], "little")
    return [v[(bits >> (3 * i)) & 7] for i in range(16)]


def _half(h):
    e, m = (h >> 10) & 31, h & 1023
    if e == 0:
        v = m / 1024.0 / 16384.0
    elif e == 31:
        v = float("inf")
    else:
        v = (1 + m / 1024.0) * 2.0 ** (e - 15)
    return -v if h & 0x8000 else v


def decode_level(data, L, fmt, swizzle):
    """One level of one face: PC-order bytes in the PC format (BC1/BC2/BC3/BC5
    blocks, or RGBA8 texels) and the RGBA8 picture (L.w x L.h)."""
    name, bw, bpb, _, _ = FORMATS[fmt]
    offs = L.blocks(bpb)
    if offs and (min(offs) < 0 or max(offs) + bpb > len(data)):
        raise TexError("level %d outside the data" % L.level)
    raw = b"".join(data[o:o + bpb] for o in offs)
    pc = bytearray(raw)
    if bw == 4:  # 16-bit words
        pc[0::2], pc[1::2] = raw[1::2], raw[0::2]
    else:  # 32-bit words
        pc[0::4], pc[1::4], pc[2::4], pc[3::4] = raw[3::4], raw[2::4], raw[1::4], raw[0::4]
    W = L.wb * bw
    rgba = bytearray(W * L.hb * bw * 4)
    if bw == 1:
        comps = []
        if fmt == 0x06:  # little-endian word: component 0 = bits 0-7
            comps = [pc[0::4], pc[1::4], pc[2::4], pc[3::4]]
        else:  # 16_16_FLOAT, clamped to [0, 1]
            vals = struct.unpack("<%dH" % (len(pc) // 2), pc)
            c0 = bytes(max(0, min(255, round(_half(v) * 255))) for v in vals[0::2])
            c1 = bytes(max(0, min(255, round(_half(v) * 255))) for v in vals[1::2])
            comps = [c0, c1, bytes(len(c0)), bytes(len(c0))]
        n = len(comps[0])
        for ch, s in enumerate(swizzle):
            rgba[ch::4] = comps[s] if s < 4 else bytes([0 if s == 4 else 255]) * n
        out = rgba
        pc_out = bytes(rgba) if fmt == 0x06 else bytes(pc)
    else:
        rows = [W * 4 * r for r in range(4)]
        for i in range(len(offs)):
            o = i * bpb
            x0 = (i % L.wb) * 16
            y0 = (i // L.wb) * 4 * W * 4
            if fmt == 0x12:
                c0, c1, idx = struct.unpack_from("<HHI", pc, o)
                pal = _palette(c0, c1, True)
                px = [pal[(idx >> (2 * k)) & 3] for k in range(16)]
            elif fmt == 0x31:
                xs, ys = _channel(pc, o), _channel(pc, o + 8)
                px = [bytes((xs[k], ys[k], 0, 255)) for k in range(16)]
            else:
                c0, c1, idx = struct.unpack_from("<HHI", pc, o + 8)
                pal = _palette(c0, c1, False)
                if fmt == 0x14:
                    a = _channel(pc, o)
                else:
                    bits = int.from_bytes(pc[o:o + 8], "little")
                    a = [((bits >> (4 * k)) & 15) * 17 for k in range(16)]
                px = [pal[(idx >> (2 * k)) & 3][:3] + bytes((a[k],)) for k in range(16)]
            for r in range(4):
                q = y0 + rows[r] + x0
                rgba[q:q + 16] = b"".join(px[4 * r:4 * r + 4])
        out = rgba
        pc_out = bytes(pc)
    if W != L.w or L.hb * bw != L.h:  # crop to the level's size
        out = b"".join(out[y * W * 4:y * W * 4 + L.w * 4] for y in range(L.h))
    return pc_out, bytes(out)


# --- the containers -----------------------------------------------------------

class Image:
    """One texture: its 0x28-byte header, fetch constant and levels."""

    def __init__(self, d, pos, kind, rel):
        """`rel`: what the header's data offsets count from (a .tex: the file;
        a .img: the 4 KiB page after the headers)."""
        (ver, self.flags, self.cls, self.key, self.w, self.h, self.depth, w2, h2, d2,
         self.levels, self.bpp, self.comp, pad, mipoff, self.hdr, self.size, baseoff) = struct.unpack_from(
            ">HBBI6H4B4I", d, pos)
        self.pos = pos
        self.kind = kind
        if ver != IMG_VERSION:
            raise TexError("image version 0x%04x" % ver)
        if (kind, self.flags) not in (("img", 0x11), ("tex", 0x02), ("tex", 0x12)):
            raise TexError("%s flags 0x%02x" % (kind, self.flags))
        if kind == "img" and self.cls != 0 or self.cls > 5:
            raise TexError("class %d" % self.cls)
        if (w2, h2, d2) != (self.w, self.h, self.depth) or pad:
            raise TexError("size fields differ")
        if self.hdr + D3D_HEADER > len(d) or (kind == "img" and self.hdr != IMG_HEADER):
            raise TexError("D3D header at 0x%x" % self.hdr)
        words = struct.unpack_from(">13I", d, self.hdr)
        if words[:7] != D3D_FIXED:
            raise TexError("D3D header %r" % (words[:7],))
        f = self.fetch = Fetch(words[7:])
        name, bw, bpb, bits, codes = FORMATS[f.fmt]
        if (f.width, f.height, f.depth if f.dim == DIM_3D else 1) != (self.w, self.h, self.depth):
            raise TexError("size %dx%dx%d vs fetch %dx%dx%d" % (self.w, self.h, self.depth, f.width, f.height, f.depth))
        if self.levels != f.levels or self.bpp != bits or self.comp not in codes:
            raise TexError("levels/bpp/compression %d/%d/%d vs %s, %d levels" % (self.levels, self.bpp, self.comp, name, f.levels))
        if baseoff == 0xFFFFFFFF:
            raise TexError("no texel data")
        self.base = rel + baseoff
        if kind == "img":
            # [C 0x82415fd8] mips = data page + header offset, or the base when
            # it equals it; then the fetch constant's mip address is added
            if mipoff != 0 or baseoff != 0:
                raise TexError("img data offsets 0x%x 0x%x" % (baseoff, mipoff))
            self.mips = self.base + f.mip_address * 4096
        else:
            if mipoff == 0xFFFFFFFF:
                raise TexError("no mip offset")
            self.mips = rel + mipoff
            if f.mip_address and (f.dim != DIM_CUBE or self.mips != self.base + f.mip_address * 4096):
                raise TexError("mip address 0x%x" % (f.mip_address * 4096))
        if f.dim == DIM_3D:
            self.levels_ = None
            self.footprint = [(self.base, volume_size(f))]
        else:
            self.levels_, self.footprint = layout(f, self.base, self.mips)

    @property
    def format(self):
        return FORMATS[self.fetch.fmt][0]

    def faces(self):
        f = self.fetch
        return f.depth if f.dim != DIM_2D else 1

    def decode(self, d, level=0):
        """[(pc bytes, rgba bytes)] per face / slice of `level`."""
        f = self.fetch
        if f.dim == DIM_3D:
            return decode_volume(d, self, level)
        res = []
        for L in self.levels_[level]:
            if L.src == "derived":
                up = self.levels_[level - 1][L.face]
                rgba = box_filter(self.decode(d, level - 1)[L.face][1], up.w, up.h, L.w, L.h)
                res.append((rgba, rgba))
            else:
                res.append(decode_level(d, L, f.fmt, f.swizzle))
        return res

    def spans(self):
        """(offset, size) of every block / texel run this image uses."""
        f = self.fetch
        bpb = FORMATS[f.fmt][2]
        if f.dim == DIM_3D:
            return [(o, bpb) for o in volume_blocks(self)]
        res = []
        for lv in self.levels_:
            for L in lv:
                if L.src is None:
                    res.extend((o, bpb) for o in L.blocks(bpb))
        return res


def box_filter(rgba, w, h, w2, h2):
    """RGBA8 w x h -> w2 x h2 (an integer ratio), rounded box filter."""
    fx, fy = w // w2, h // h2
    out = bytearray(w2 * h2 * 4)
    n = fx * fy
    for y in range(h2):
        for x in range(w2):
            for c in range(4):
                t = sum(rgba[((y * fy + j) * w + x * fx + i) * 4 + c] for j in range(fy) for i in range(fx))
                out[(y * w2 + x) * 4 + c] = (t + n // 2) // n
    return bytes(out)


def volume_size(f):
    name, bw, bpb, _, _ = FORMATS[f.fmt]
    wb, hb = (f.width + bw - 1) // bw, (f.height + bw - 1) // bw
    return align(align(wb, 32) * align(hb, 32) * bpb, 4096) * f.depth


def volume_blocks(im):
    """A volume texture's blocks, slice-major: each slice a 2D tiled surface
    [I: the only volume on the disc, see docs]."""
    f = im.fetch
    name, bw, bpb, _, _ = FORMATS[f.fmt]
    wb, hb = (f.width + bw - 1) // bw, (f.height + bw - 1) // bw
    pitch = align(wb, 32)
    ssize = align(pitch * align(hb, 32) * bpb, 4096)
    res = []
    for z in range(f.depth):
        res.extend(im.base + z * ssize + o for o in offsets(wb, hb, pitch, bpb, 1))
    return res


def decode_volume(d, im, level):
    f = im.fetch
    name, bw, bpb, _, _ = FORMATS[f.fmt]
    wb, hb = (f.width + bw - 1) // bw, (f.height + bw - 1) // bw
    pitch = align(wb, 32)
    ssize = align(pitch * align(hb, 32) * bpb, 4096)
    res = []
    for z in range(f.depth):
        L = _level(0, z, f.width, f.height, wb, hb, im.base + z * ssize, pitch, 1, 0, 0)
        res.append(decode_level(d, L, f.fmt, f.swizzle))
    return res


def tex_hash_bits(n):
    b = 1
    while (1 << (b - 1)) < n and b < 8:
        b += 1
    return b


class TexDict:
    """A .tex: the textures of a scene or model, by name key."""

    def __init__(self, d):
        self.d = d
        if len(d) < 0x1C:
            raise TexError("short .tex")
        (magic, ver, hsize, self.count, self.table, self.objects, unused, self.hash_bits,
         self.hash) = struct.unpack_from(">IBBHIIiII", d, 0)
        if magic != TEX_MAGIC or ver != 1 or hsize != 0x1C:
            raise TexError("tex header %08x %d 0x%x" % (magic, ver, hsize))
        if unused != -1 or self.hash != 0x1C or self.hash_bits != tex_hash_bits(self.count):
            raise TexError("tex hash table %d bits at 0x%x, word 0x10 %d" % (self.hash_bits, self.hash, unused))
        hash_end = self.hash + 0x1C + 12 * (1 << self.hash_bits)
        if self.table != hash_end or self.objects != self.table + self.count * (IMG_HEADER + D3D_HEADER):
            raise TexError("tex table at 0x%x, objects at 0x%x" % (self.table, self.objects))
        self.images = []
        for i in range(self.count):
            im = Image(d, self.table + i * IMG_HEADER, "tex", 0)
            if im.hdr != self.table + self.count * IMG_HEADER + i * D3D_HEADER:
                raise TexError("texture %d: D3D header at 0x%x" % (i, im.hdr))
            self.images.append(im)
        # bytes the game fills at load: hash table (0xef here), texture objects (zero)
        self.meta = [(0, 0x1C, None), (self.hash, hash_end - self.hash, 0xEF),
                     (self.table, self.objects - self.table, None), (self.objects, self.count * 0x14, 0)]


def open_img(d):
    im = Image(d, 0, "img", IMG_DATA)
    return im


# --- check: decode everything, account for every byte -------------------------

def account(d, meta, images, decode=True):
    """Problems of one file: data outside the file or used twice, and non-zero
    bytes that neither a header nor a texture allocation explains. Texel data
    is a texture's blocks; the rest of its allocations (tile padding) is left
    as the converter's memory had it (0xcefa fill, stale texels), not checked."""
    probs = []
    owner = bytearray(len(d))  # 1 header / texel data, 2 allocation padding
    for pos, size, fill in meta:
        if pos + size > len(d):
            probs.append("header part outside the file")
            continue
        if fill is not None and d[pos:pos + size] != bytes((fill,)) * size:
            probs.append("bytes at 0x%x are not 0x%02x" % (pos, fill))
        owner[pos:pos + size] = b"\1" * size
    for im in images:
        for pos, size in im.footprint + ([(im.base, im.size)] if im.kind == "tex" else []):
            pos, end = max(0, pos), min(len(d), pos + size)
            if end > pos:
                seg = owner[pos:end]
                owner[pos:end] = seg.replace(b"\0", b"\2")
    for n, im in enumerate(images):
        try:
            if decode:
                for lv in range(im.fetch.levels if im.fetch.dim != DIM_3D else 1):
                    im.decode(d, lv)
            spans = im.spans()
        except (TexError, struct.error, ValueError, IndexError) as e:
            probs.append("texture %d: %s" % (n, e))
            continue
        bpb = spans[0][1] if spans else 0
        one = b"\1" * bpb
        for o, s in spans:
            if o < 0 or o + s > len(d):
                probs.append("texture %d: data outside the file" % n)
                break
            if 1 in owner[o:o + s]:
                probs.append("texture %d: data at 0x%x used twice" % (n, o))
                break
            owner[o:o + s] = one
    stray = 0
    first = None
    for i in range(0, len(d), 4096):
        page_o = owner[i:i + 4096]
        if 0 not in page_o:
            continue
        page = d[i:i + 4096]
        for j in range(len(page_o)):
            if not page_o[j] and page[j]:
                stray += 1
                if first is None:
                    first = i + j
    if stray:
        probs.append("%d stray non-zero bytes (first at 0x%x)" % (stray, first))
    return probs


def img_size_problems(d, im):
    """A lone .img ends with its last allocation; its size field is the data
    size times the number of levels (an exporter quirk) [C data]."""
    end = max(a + s for a, s in im.footprint)
    if len(d) != end or im.size != (end - IMG_DATA) * im.fetch.levels:
        return ["file size 0x%x, data end 0x%x, size field 0x%x" % (len(d), end, im.size)]
    return []


def check_one(job):
    kind, path, decode = job
    out = []  # (label, format counts, problems)
    if kind == "loose":
        d = read_disc_file(path)
        try:
            im = open_img(d)
            meta = [(0, IMG_HEADER, None), (IMG_HEADER, D3D_HEADER, None)]
            probs = account(d, meta, [im], decode)
            probs += img_size_problems(d, im)
            out.append((os.path.basename(path), [(im.format, im.fetch.dim)], probs))
        except (TexError, struct.error) as e:
            out.append((os.path.basename(path), [], [str(e)]))
        return out
    p = Pak.open(path)
    names = default_table()
    for e in p.entries:
        if e.type not in (KEY_IMG, KEY_TEX):
            continue
        d = p.data(e)
        label = "%s:%s" % (os.path.basename(path), names.get(e.fullname))
        try:
            if e.type == KEY_IMG:
                im = open_img(d)
                ims = [im]
                meta = [(0, IMG_HEADER, None), (IMG_HEADER, D3D_HEADER, None)]
                probs = account(d, meta, ims, decode)
                probs += img_size_problems(d, im)
            else:
                t = TexDict(d)
                ims = t.images
                probs = account(d, t.meta, ims, decode)
                keys = [im.key for im in ims]
                if len(set(keys)) != len(keys):
                    probs.append("duplicate texture keys")
            out.append((label, [(im.format, im.fetch.dim) for im in ims], probs))
        except (TexError, struct.error) as ex:
            out.append((label, [], [str(ex)]))
    return out


def check(rip, decode=True):
    jobs = [("pak", p, decode) for p in all_paks(rip)]
    for root, _, files in os.walk(rip):
        for f in sorted(files):
            if f.endswith(".img.xen"):
                jobs.append(("loose", os.path.join(root, f), decode))
    counts = {}
    files = bad = 0
    with Pool() as pool:
        for res in pool.imap_unordered(check_one, jobs):
            for label, fmts, probs in res:
                files += 1
                for k in fmts:
                    counts[k] = counts.get(k, 0) + 1
                if probs:
                    bad += 1
                    print("%s: %s" % (label, "; ".join(probs[:4])))
    dims = {DIM_2D: "", DIM_3D: " volume", DIM_CUBE: " cube"}
    for (fmt, dim), n in sorted(counts.items()):
        print("  %-12s %5d" % (fmt + dims[dim], n))
    print("files: %d, textures: %d, with problems: %d" % (files, sum(counts.values()), bad))
    return 1 if bad else 0


# --- list / export ------------------------------------------------------------

def find(path, name):
    """(file bytes, Image) for `name` in a pak or loose .img.xen."""
    if not path.endswith(".pak.xen"):
        d = read_disc_file(path)
        return d, open_img(d)
    p = Pak.open(path)
    names = default_table()
    tex, _, sub = name.rpartition(":")
    if not tex:
        tex, sub = name, ""
    for e in p.entries:
        n = names.get(e.fullname)
        if e.type == KEY_IMG and not sub and (n == name or names.get(e.name) == name):
            d = p.data(e)
            return d, open_img(d)
        if e.type == KEY_TEX and sub and tex in (n, names.get(e.name)):
            d = p.data(e)
            t = TexDict(d)
            for i, im in enumerate(t.images):
                if sub in (str(i), "%08x" % im.key, names.get(im.key)):
                    return d, im
    raise SystemExit("no image %r" % name)


def write_png(path, w, h, rgba):
    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def write_ppm(path, w, h, rgba):
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(bytes(b for i, b in enumerate(rgba) if i % 4 != 3))


def export(path, name, out, level=0):
    d, im = find(path, name)
    pics = [rgba for _, rgba in im.decode(d, level)]
    L = im.levels_[level][0] if im.levels_ else None
    w = L.w if L else im.w
    h = L.h if L else im.h
    n = len(pics)
    rgba = b"".join(b"".join(p[y * w * 4:(y + 1) * w * 4] for p in pics) for y in range(h))
    (write_ppm if out.endswith(".ppm") else write_png)(out, w * n, h, rgba)
    print("%s %dx%d level %d of %d, %s%s -> %s" % (name, w, h, level, im.fetch.levels, im.format,
                                                    " x%d" % n if n > 1 else "", out))


def describe(im, names):
    f = im.fetch
    kind = {DIM_2D: "2D", DIM_3D: "3D", DIM_CUBE: "cube"}[f.dim]
    return "%-28s %4dx%-4d %-4s %-11s lv%-2d %s base=%x mips=%x size=%x cls=%d" % (
        names.get(im.key), im.w, im.h, kind, FORMATS[f.fmt][0], f.levels, "tiled" if f.tiled else "linear",
        im.base, im.mips, im.size, im.cls)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "check":
        return check(argv[2], "--layout-only" not in argv)
    if cmd == "export" and len(argv) >= 5:
        export(argv[2], argv[3], argv[4], int(argv[5]) if len(argv) > 5 else 0)
        return 0
    if cmd == "list":
        names = default_table()
        path = argv[2]
        if not path.endswith(".pak.xen"):
            print(describe(open_img(read_disc_file(path)), names))
            return 0
        p = Pak.open(path)
        for e in p.entries:
            if e.type == KEY_IMG:
                print("img %-40s %s" % (names.get(e.fullname), describe(open_img(p.data(e)), names)))
            elif e.type == KEY_TEX:
                t = TexDict(p.data(e))
                print("tex %s: %d textures" % (names.get(e.fullname), t.count))
                for i, im in enumerate(t.images):
                    print("  %3d %s" % (i, describe(im, names)))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
