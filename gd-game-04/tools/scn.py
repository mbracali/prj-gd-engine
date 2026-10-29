#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero III (Xbox 360) geometry: .scn, .mdl, .skin (scene files) and
.hkc (Havok 4.6 packfile collision).

    scn.py check <rip dir>              parse every .scn/.mdl/.skin/.hkc to its last byte
    scn.py dump  <pak> <name>           structure summary (name: full/short name or key)
    scn.py obj   <pak> <name> <out.obj> geometry as Wavefront OBJ

Big-endian throughout. "B" below is the scene base: every scene offset counts
from it (the loader relocates them, Function_822D7E78 @ 0x822d7e78 and
FUN_822d6d48 @ 0x822d6d48). Layout (docs in the hand-back / formats.md):

  0x00  u32  skin trailer offset from 0x20 (.skin: file size - 0x30; else 0)
  0x04  7 x 0xFAAABACA
  0x20  material header: u8 4, u8 0x10, u16 count, u32 section size (from
        0x20), u32 0x10 (first material, from 0x20), u32 -1
  0x30  materials (variable size, +0xe4 = size)       Function_822B27E8
  ...   0xBABEFACE, u32 n (8 / 0x18), n-4 zero bytes, u32 0xEA, then B:
  B     scene header 0x90, sectors (0x60 each), geoms (0x60 each), a zero
        0x14-byte record per sector (padded to 16), meshes (0x80 each), three
        u32 tables of mesh count entries, 0xAA padding, then per mesh: vertex
        block (0x20 descriptor + data), index block (0x20 + u16 strips),
        and for skins the CPU skinning block (aligned to 0x1000 from B).
  end   .skin: 16-byte trailer 01 10 00 fe 00.. 00 00 00 10.
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pak import Pak, all_paks  # noqa: E402
from qbkey import qbkey, default_table  # noqa: E402

KIND_KEYS = {qbkey(".scn"): "scn", qbkey(".mdl"): "mdl", qbkey(".skin"): "skin",
             qbkey(".hkc"): "hkc", qbkey(".col"): "col"}

NONE = 0xFFFFFFFF


class ScnError(Exception):
    pass


def need(cond, what):
    if not cond:
        raise ScnError(what)


def u32(d, o):
    need(0 <= o and o + 4 <= len(d), "read past end at %#x" % o)
    return struct.unpack_from(">I", d, o)[0]


def u32s(d, o, n):
    need(0 <= o and o + 4 * n <= len(d), "read past end at %#x" % o)
    return struct.unpack_from(">%dI" % n, d, o)


def f32s(d, o, n):
    need(0 <= o and o + 4 * n <= len(d), "read past end at %#x" % o)
    return struct.unpack_from(">%df" % n, d, o)


def zero(d, a, b, what):
    need(not any(d[a:b]), "%s: non-zero bytes in %#x..%#x" % (what, a, b))


def fill(d, a, b, allowed, what):
    need(set(d[a:b]) <= set(allowed), "%s: unexpected padding bytes in %#x..%#x" % (what, a, b))


def align(x, a):
    return (x + a - 1) & ~(a - 1)


def half(h):
    return struct.unpack(">e", struct.pack(">H", h))[0]


def hend3n(v):
    """D3DDECLTYPE_HEND3N (0x2a2190): x 11 bits, y 11 bits, z 10 bits, signed normalised."""
    def s(x, bits):
        if x & (1 << (bits - 1)):
            x -= 1 << bits
        return max(-1.0, x / ((1 << (bits - 1)) - 1))
    return (s(v & 0x7FF, 11), s((v >> 11) & 0x7FF, 11), s(v >> 22, 10))


# ---- vertex declaration from the mesh's vertex flags (Function_823B9A28 @ 0x823b9a28)

USAGE = {0: "position", 1: "blendweight", 2: "blendindices", 3: "normal", 5: "texcoord",
         6: "tangent", 7: "binormal", 10: "color"}


def vertex_decl(fl):
    """[(stream, offset, type, size, usage, index)]; stream 1 exists when bit 0x80 is set."""
    out = []
    off = 0
    st = 0
    for i in range(((fl >> 12) & 3) + 1):            # positions
        if fl & 1:
            out.append((0, off, "f4", 16, 0, i)); off += 16
        else:
            out.append((0, off, "f3", 12, 0, i)); off += 12
    if fl & 8:                                        # normal
        if fl & 0x28 == 0x28:
            out.append((0, off, "hend3n", 4, 3, 0)); off += 4
        else:
            out.append((0, off, "f3", 12, 3, 0)); off += 12
    for i in range((fl >> 10) & 3):
        out.append((0, off, "hend3n", 4, 7, i)); off += 4
    for i in range((fl >> 8) & 3):
        out.append((0, off, "hend3n", 4, 6, i)); off += 4
    if fl & 0x80 == 0:
        if fl & 2:
            out.append((0, off, "udhen3n", 4, 1, 0)); off += 4
        if fl & 4:
            out.append((0, off, "ubyte4", 4, 2, 0)); off += 4
    else:
        st, off = 1, 0
    if fl & 0x10:
        out.append((st, off, "color", 4, 10, 0)); off += 4
    if fl & 0x4000:
        out.append((st, off, "ubyte4", 4, 2, 0)); off += 4
    t = fl >> 16
    i = 0
    while t:
        c = t & 3
        if c:
            if fl & 0x40:
                if c < 3:
                    out.append((st, off, "h2", 4, 5, i)); off += 4
                else:
                    out.append((st, off, "h4", 8, 5, i)); off += 8
            elif c == 1:
                out.append((st, off, "f1", 4, 5, i)); off += 4
            elif c == 2:
                out.append((st, off, "f2", 8, 5, i)); off += 8
            else:
                out.append((st, off, "f3", 12, 5, i)); off += 12
        t >>= 2
        i += 1
    return out


def decl_strides(dc):
    s = [0, 0]
    for e in dc:
        s[e[0]] = max(s[e[0]], e[1] + e[3])
    return s


def read_elem(d, o, typ):
    if typ == "f3":
        return f32s(d, o, 3)
    if typ == "f4":
        return f32s(d, o, 4)
    if typ == "f2":
        return f32s(d, o, 2)
    if typ == "f1":
        return f32s(d, o, 1)
    if typ == "h2":
        a, b = struct.unpack_from(">2H", d, o)
        return (half(a), half(b))
    if typ == "h4":
        return tuple(half(x) for x in struct.unpack_from(">4H", d, o))
    if typ == "hend3n":
        return hend3n(u32(d, o))
    if typ == "color":
        v = u32(d, o)
        return ((v >> 16) & 255, (v >> 8) & 255, v & 255, v >> 24)  # r, g, b, a
    return tuple(d[o:o + 4])


# ---- scene files

def parse_scene(d, kind):
    """Parses a .scn/.mdl/.skin; raises ScnError. Returns a dict."""
    need(len(d) >= 0x30, "too small")
    first = u32(d, 0)
    need(u32s(d, 4, 7) == (0xFAAABACA,) * 7, "bad 0xFAAABACA header")
    if kind == "skin":
        need(first == len(d) - 0x30, "skin trailer offset %#x != size - 0x30" % first)
        need(d[-16:] == bytes.fromhex("011000fe000000000000000000000010"), "bad skin trailer")
        end = len(d) - 16
    else:
        need(first == 0, "first word %#x, expected 0" % first)
        end = len(d)

    # materials
    need(d[0x20] == 4 and d[0x21] == 0x10, "material header %02x %02x" % (d[0x20], d[0x21]))
    nmat = struct.unpack_from(">H", d, 0x22)[0]
    msize, mfirst, mnone = u32s(d, 0x24, 3)
    need(mfirst == 0x10 and mnone == NONE, "material header words")
    mend = 0x20 + msize
    need(mend <= end, "material section past end")
    mats = []
    p = 0x30
    for i in range(nmat):
        need(p + 0x100 <= mend, "material %d past the section" % i)
        w = u32s(d, p, 64)
        zero(d, p + 4, p + 0xA0, "material %d name area" % i)
        nA, oA, nB, oB, nT, oT, nT2, oT2 = w[0xA8 // 4:0xC8 // 4]
        size = w[0xC4 // 4]
        need(w[0xA4 // 4] == 0 and w[0xE0 // 4] == 0 and w[0xF4 // 4:] == (0, 0, 0), "material %d reserved words" % i)
        need(oA == 0x100 and oB == oA + 16 * nA and oT == oB + 16 * nB, "material %d array offsets" % i)
        need(nT2 == 0 and oT2 == size and size == align(oT + 4 * nT, 16) and w[0xE4 // 4] == size,
             "material %d size" % i)
        need(p + size <= mend, "material %d past the section" % i)
        zero(d, p + oT + 4 * nT, p + size, "material %d padding" % i)
        mats.append({
            "offset": p, "key": w[0], "shader": w[0xA0 // 4],
            "constsA": [f32s(d, p + oA + 16 * k, 4) for k in range(nA)],
            "constsB": [f32s(d, p + oB + 16 * k, 4) for k in range(nB)],
            "textures": list(u32s(d, p + oT, nT)),
            "c8": w[0xC8 // 4], "cc": w[0xCC // 4], "d0": w[0xD0 // 4], "d4": f32s(d, p + 0xD4, 1)[0],
            "d8": w[0xD8 // 4], "dc": w[0xDC // 4], "e8": w[0xE8 // 4], "ec": w[0xEC // 4], "f0": w[0xF0 // 4],
        })
        p += size
    need(p == mend, "materials end at %#x, section at %#x" % (p, mend))

    # scene header
    s = mend
    need(u32(d, s) == 0xBABEFACE, "no 0xBABEFACE at %#x" % s)
    pad = u32(d, s + 4)
    need(pad >= 4 and pad % 4 == 0, "scene padding %#x" % pad)
    zero(d, s + 8, s + 4 + pad, "scene padding")
    need(u32(d, s + 4 + pad) == 0xEA, "no 0xEA before the scene")
    B = s + 8 + pad
    need(B + 0x90 <= end, "scene header past end")
    h = u32s(d, B, 36)
    W = lambda o: h[o // 4]  # noqa: E731
    bmin, bmax, sphere = f32s(d, B, 3), f32s(d, B + 0x10, 3), f32s(d, B + 0x20, 4)
    need(W(0x0C) == 0 and W(0x1C) == 0, "scene bbox w")
    need(W(0x30) == 0x000E0090, "scene header word 0x30 %#x" % W(0x30))
    need(W(0x3C) == end - 0x20, "scene size %#x" % W(0x3C))
    nm, ns = W(0x4C), W(0x6C)
    need(nm < 0x10000 and ns < 0x10000, "counts")
    exp = {0x34: 0, 0x38: 0, 0x40: 0, 0x44: NONE, 0x54: 0, 0x58: 0, 0x5C: 0, 0x60: 0, 0x64: NONE, 0x68: 0,
           0x70: 0x90, 0x78: NONE, 0x84: 0, 0x88: 0}
    exp[0x74] = 0x90 + 0x60 * ns
    exp[0x7C] = exp[0x74] + 0x60 * ns
    exp[0x80] = align(exp[0x7C] + 0x14 * ns, 16)
    exp[0x8C] = exp[0x80] + 0x80 * nm
    exp[0x48] = exp[0x8C] + 4 * nm
    exp[0x50] = exp[0x48] + 4 * nm
    for o, v in exp.items():
        need(W(o) == v, "scene header +%#x = %#x, expected %#x" % (o, W(o), v))
    tables_end = exp[0x50] + 4 * nm
    need(B + tables_end <= end, "scene tables past end")

    sectors = []
    total = 0
    for k in range(ns):
        so = B + 0x90 + 0x60 * k
        sw = u32s(d, so, 24)
        need(sw[0] == 0 and sw[4] == NONE and sw[3] in (k, NONE), "sector %d words" % k)
        zero(d, so + 0x14, so + 0x40, "sector %d" % k)
        zero(d, so + 0x50, so + 0x60, "sector %d" % k)
        go = B + exp[0x74] + 0x60 * k
        gw = u32s(d, go, 24)
        need(gw[0] == 0x60 * ns and gw[1] == 0 and gw[2] == 0 and gw[3] == 0x100, "geom %d header" % k)
        need(gw[7] == 0 and gw[11] == 0, "geom %d bbox w" % k)
        zero(d, go + 0x30, go + 0x48, "geom %d" % k)
        zero(d, go + 0x50, go + 0x5C, "geom %d" % k)
        need(gw[18] == total, "geom %d first mesh %d, expected %d" % (k, gw[18], total))
        total += gw[19]
        sectors.append({"key": sw[1], "flags": sw[2], "index": sw[3], "sphere": f32s(d, so + 0x40, 4),
                        "bmin": f32s(d, go + 0x10, 3), "bmax": f32s(d, go + 0x20, 3),
                        "first": gw[18], "count": gw[19], "g5c": gw[23]})
    need(total == nm, "geoms cover %d meshes of %d" % (total, nm))
    zero(d, B + exp[0x7C], B + exp[0x80], "per-sector 0x14 records")
    need(u32s(d, B + exp[0x8C], nm) == (0xEAEAEAEA,) * nm, "mesh pointer table not 0xEAEAEAEA")
    order = u32s(d, B + exp[0x48], nm)
    need(sorted(order) == list(range(nm)), "mesh order table is not a permutation")
    need(u32s(d, B + exp[0x50], nm) == (NONE,) * nm, "second mesh table not -1")

    meshes = []
    blocks = []
    for i in range(nm):
        mo = B + exp[0x80] + 0x80 * i
        w = u32s(d, mo, 32)
        fl = w[7]
        ni, nv = w[9] >> 16, w[9] & 0xFFFF
        need(w[8] == 0xFF000000 and w[10] == 6, "mesh %d words 0x20/0x28" % i)
        zero(d, mo + 0x2C, mo + 0x40, "mesh %d" % i)
        need(w[17] == NONE and w[18] == NONE and w[20] == 0 and w[21] == 0 and w[25] == 0
             and w[27] == 0 and w[28] == 0 and w[29] == 0 and w[31] == 0, "mesh %d reserved words" % i)
        dc = vertex_decl(fl)
        st = decl_strides(dc)
        skinned = w[24] != NONE
        need(skinned == (kind == "skin"), "mesh %d skin stream presence" % i)
        if skinned:
            need(fl & 0x80 and st[0] == 24 and w[30] == 0x05000000, "skin mesh %d vertex flags %#x" % (i, fl))
            gstride = st[1]
        else:
            need(not fl & 0x80 and w[26] == 0, "mesh %d vertex flags %#x" % (i, fl))
            gstride = st[0]
        need(w[19] == gstride * nv, "mesh %d vertex size %#x != %d x %d" % (i, w[19], gstride, nv))
        vo, io = B + w[16], B + w[23]
        need(io == vo + 0x20 + w[19], "mesh %d index block does not follow its vertices" % i)
        need(io + 0x20 + 2 * ni <= end, "mesh %d index block past end" % i)
        vdesc, idesc = d[vo:vo + 0x20], d[io:io + 0x20]
        need(idesc[:16] == vdesc[16:][::-1], "mesh %d index descriptor mirror" % i)
        idx = struct.unpack_from(">%dH" % ni, d, io + 0x20)
        need(all(x < nv or x == 0x7FFF for x in idx), "mesh %d index out of range" % i)
        blocks.append((vo, io + 0x20 + 2 * ni, "v", i))
        m = {"offset": mo, "sphere": f32s(d, mo, 4), "f10": w[4], "material": w[5], "f18": w[6], "flags": fl,
             "numIndices": ni, "numVerts": nv, "f58": w[22], "f68": w[26], "f78": w[30], "decl": dc,
             "vdata": vo + 0x20, "stride": gstride, "indices": idx, "vdesc": vdesc, "skin": None}
        if skinned:
            sk = B + w[24]
            need(u32s(d, sk, 4) == (0xCAFEBAB4,) * 4, "mesh %d no 0xCAFEBAB4" % i)
            counts = u32s(d, sk + 16, 4)
            need(counts[0] == 0, "mesh %d skin group count 0" % i)
            need(idesc[16:] == struct.pack(">4I", *counts[::-1]), "mesh %d skin index descriptor" % i)
            p = sk + 0x20
            groups = []
            tot = 0
            for nb in (1, 2, 3):
                for _ in range(counts[nb]):
                    gn = u32(d, p)
                    bones = d[p + 4:p + 8]
                    need(not any(bones[nb:]), "mesh %d skin group bone padding" % i)
                    need(u32s(d, p + 8, 2) == (0xFACEF000, 0xFACEF001), "mesh %d skin group markers" % i)
                    need(gn > 0 and p + 16 + 32 * gn <= end, "mesh %d skin group size" % i)
                    for k in range(gn):
                        need(u32(d, p + 16 + 32 * k + 28) == 0xBAADF00D, "mesh %d skin vertex marker" % i)
                    groups.append((nb, tuple(bones[:nb]), gn, p + 16))
                    tot += gn
                    p += 16 + 32 * gn
            need(tot == nv, "mesh %d skin groups cover %d of %d vertices" % (i, tot, nv))
            m["skin"] = groups
            blocks.append((sk, p, "s", i))
        meshes.append(m)

    # every byte between the blocks: padding only
    blocks.sort()
    pos = B + tables_end
    first_block = True
    for a, b, t, i in blocks:
        need(a >= pos, "block overlap at %#x" % a)
        if t == "s":
            # 2 x mesh+0x68 zero bytes (runtime area), then 0xEE up to the next 0x1000 from B
            zl = 2 * (meshes[i]["f68"] >> 16)
            need(meshes[i]["f68"] & 0xFFFF == 0, "mesh %d word 0x68 low half" % i)
            need(a == B + align(pos + zl - B, 0x1000), "skin block %d not at the next 0x1000 boundary" % i)
            zero(d, pos, pos + zl, "skin %d runtime area" % i)
            fill(d, pos + zl, a, (0xEE,), "padding before skin block %d" % i)
        elif first_block:
            need(a == B + align(pos - B, 32), "first vertex block not at the next 32-byte boundary")
            fill(d, pos, a, (0xAA,), "padding after the tables")
        else:
            need(a == align(pos, 16), "vertex block %d not 16-aligned after the previous block" % i)
            zero(d, pos, a, "padding before vertex block %d" % i)
        first_block = False
        pos = b
    if not blocks:
        need(end == B + align(pos - B, 32), "empty scene padding")
        fill(d, pos, end, (0xAA,), "empty scene padding")
    else:
        need(end == align(pos, 16), "scene end %#x, data end %#x" % (end, pos))
        zero(d, pos, end, "tail padding")
    return {"kind": kind, "materials": mats, "B": B, "bmin": bmin, "bmax": bmax, "sphere": sphere,
            "sectors": sectors, "meshes": meshes, "order": order}


def mesh_vertices(d, m):
    """Per-vertex dicts of decoded attributes ('position', 'normal', 'texcoord0', ...)."""
    out = []
    nv = m["numVerts"]
    skin_rec = []
    if m["skin"]:
        for nb, bones, gn, p in m["skin"]:
            for k in range(gn):
                skin_rec.append((nb, bones, p + 32 * k))
    for v in range(nv):
        a = {}
        for st, off, typ, size, usage, ix in m["decl"]:
            if m["skin"] and st == 0:
                continue
            o = m["vdata"] + v * m["stride"] + off
            a["%s%d" % (USAGE.get(usage, "u%d" % usage), ix)] = read_elem(d, o, typ)
        if m["skin"]:
            nb, bones, p = skin_rec[v]
            a["position0"] = f32s(d, p, 3)
            wv = u32(d, p + 12)
            w0, w1 = (wv & 0xFFFF) / 65535.0, (wv >> 16) / 65535.0
            ws = [w0, w1, max(0.0, 1.0 - w0 - w1)][:nb] if nb > 1 else [1.0]
            a["bones"] = bones
            a["weights"] = tuple(ws)
            # stream-0 elements after the position sit 4 bytes later (the weights word)
            for st, off, typ, size, usage, ix in m["decl"]:
                if st == 0 and usage != 0:
                    a["%s%d" % (USAGE.get(usage, "u%d" % usage), ix)] = read_elem(d, p + off + 4, typ)
        out.append(a)
    return out


def strip_triangles(idx):
    tris = []
    run = []
    for x in list(idx) + [0x7FFF]:
        if x == 0x7FFF:
            for k in range(2, len(run)):
                a, b, c = (run[k - 2], run[k - 1], run[k]) if k % 2 == 0 else (run[k - 1], run[k - 2], run[k])
                if a != b and b != c and a != c:
                    tris.append((a, b, c))
            run = []
        else:
            run.append(x)
    return tris


# ---- Havok packfile (.hkc)

def parse_hkc(d):
    need(len(d) >= 0x40, "too small")
    need(d[:8] == bytes.fromhex("57e0e05710c0c010"), "no Havok packfile magic")
    (tag, ver) = u32s(d, 8, 2)
    rules = d[16:20]
    nsec, csec, coff, cnsec, cnoff = u32s(d, 20, 5)
    cver = d[40:56]
    flags, pad = u32s(d, 56, 2)
    need(tag == 0x786E6B68 and ver == 4 and rules == bytes([4, 0, 0, 1]), "packfile header")
    need(cver.rstrip(b"\xff") == b"Havok-4.6.0-r1\x00" and flags == NONE and pad == NONE, "packfile version")
    need(nsec == 3, "section count %d" % nsec)
    secs = []
    pos = 0x40 + 0x30 * nsec
    for k in range(nsec):
        o = 0x40 + 0x30 * k
        name = d[o:o + 19].rstrip(b"\x00")
        need(d[o + 19] == 0xFF, "section %d tag terminator" % k)
        need(d[o + len(name):o + 19] == bytes(19 - len(name)), "section %d tag padding" % k)
        start, loc, glob, virt, exps, imps, send = u32s(d, o + 20, 7)
        need(start >= pos, "section %d overlaps" % k)
        fill(d, pos, start, (0xFF,), "padding before section %d" % k)
        need(loc <= glob <= virt <= exps <= imps <= send and start + send <= len(d), "section %d offsets" % k)
        secs.append({"name": name.decode("latin-1"), "start": start, "data": loc, "local": (loc, glob),
                     "global": (glob, virt), "virtual": (virt, exps), "exports": (exps, imps),
                     "imports": (imps, send), "end": send})
        pos = start + send
    fill(d, pos, len(d), (0xFF,), "file tail")
    need([s["name"] for s in secs] == ["__classnames__", "__data__", "__types__"], "section names")
    need(csec == 1 and cnsec == 0, "contents section indices")
    # class names: {u32 signature, u8 0x09, name\0} up to 0xFF padding
    cn = secs[0]
    a, b = cn["start"], cn["start"] + cn["data"]
    names = {}
    p = a
    while p < b and d[p] != 0xFF:
        need(p + 5 <= b and d[p + 4] == 0x09, "class name entry at %#x" % p)
        z = d.index(b"\x00", p + 5, b)
        names[p + 5 - a] = d[p + 5:z].decode("latin-1")
        p = z + 1
    fill(d, p, b, (0xFF,), "class name padding")
    need(cnoff in names, "contents class name offset")
    classes = {}
    for si, s in enumerate(secs):
        base = s["start"]
        size = s["data"]
        for kind, width in (("local", 2), ("global", 3), ("virtual", 3)):
            lo, hi = s[kind]
            need((hi - lo) % 4 == 0 and hi % 16 == 0, "%s %s fixups size" % (s["name"], kind))
            ents = u32s(d, base + lo, (hi - lo) // 4)
            full = len(ents) - len(ents) % width
            need(set(ents[full:]) <= {NONE}, "%s %s fixup tail" % (s["name"], kind))
            ents = ents[:full]
            done = False
            for e in range(0, len(ents), width):
                r = ents[e:e + width]
                if r[0] == NONE:
                    need(set(r) == {NONE}, "%s %s fixup padding" % (s["name"], kind))
                    done = True
                    continue
                need(not done, "%s %s fixup after padding" % (s["name"], kind))
                need(r[0] + 4 <= size, "%s %s fixup source" % (s["name"], kind))
                if kind == "local":
                    need(r[1] < size, "%s local fixup target" % s["name"])
                elif kind == "global":
                    need(r[1] < nsec and r[2] < secs[r[1]]["data"], "%s global fixup target" % s["name"])
                else:
                    need(r[1] == 0 and r[2] in names, "%s virtual fixup class" % s["name"])
                    if si == 1:
                        classes[names[r[2]]] = classes.get(names[r[2]], 0) + 1
        need(s["exports"][0] == s["exports"][1] and s["imports"][0] == s["imports"][1], "exports/imports")
    need(coff < secs[1]["data"], "contents offset")
    return {"sections": secs, "classnames": names, "objects": classes, "contents": names[cnoff]}


# ---- commands

def find_entry(p, name, names):
    want = None
    try:
        want = int(name, 16) if name.lower().startswith("0x") else None
    except ValueError:
        pass
    for e in p.entries:
        if e.type not in KIND_KEYS:
            continue
        if want is not None and want in (e.fullname, e.name):
            return e
        full = names.get(e.fullname).lower()
        if name.lower() in (full, names.get(e.name).lower()) or full.endswith(name.lower()):
            return e
    raise SystemExit("no scene entry %r in %s" % (name, p.path))


def cmd_check(rip):
    counts = {}
    bad = 0
    names = default_table()
    for path in all_paks(rip):
        p = Pak.open(path)
        for e in p.entries:
            kind = KIND_KEYS.get(e.type)
            if not kind:
                continue
            counts[kind] = counts.get(kind, 0) + 1
            try:
                if kind == "hkc":
                    parse_hkc(p.data(e))
                elif kind == "col":
                    raise ScnError("no .col decoder (none on the disc)")
                else:
                    sc = parse_scene(p.data(e), kind)
                    d = p.data(e)
                    for m in sc["meshes"]:
                        for a in mesh_vertices(d, m):
                            need(all(math.isfinite(x) for x in a["position0"]), "non-finite position")
            except (ScnError, struct.error, ValueError) as ex:
                bad += 1
                print("%s: %s: %s" % (os.path.relpath(path, rip), names.get(e.fullname), ex))
    print(", ".join("%s %d" % (k, counts.get(k, 0)) for k in ("scn", "mdl", "skin", "hkc", "col"))
          + ", with problems: %d" % bad)
    return 1 if bad else 0


def cmd_dump(pakpath, name):
    names = default_table()
    p = Pak.open(pakpath)
    e = find_entry(p, name, names)
    kind = KIND_KEYS[e.type]
    d = p.data(e)
    print("%s (%s, %d bytes, parent %s)" % (names.get(e.fullname), kind, e.size,
                                          names.get(e.parent) if e.parent else "-"))
    if kind == "hkc":
        h = parse_hkc(d)
        for s in h["sections"]:
            print("  section %-15s at %#x, data %#x, end %#x" % (s["name"], s["start"], s["data"], s["end"]))
        print("  contents: %s; objects: %s" % (h["contents"], ", ".join("%s x%d" % kv for kv in sorted(h["objects"].items()))))
        return 0
    sc = parse_scene(d, kind)
    print("materials: %d" % len(sc["materials"]))
    for i, m in enumerate(sc["materials"]):
        print("  [%d] %s shader %s, %d+%d consts, textures: %s" % (
            i, names.get(m["key"]), names.get(m["shader"]), len(m["constsA"]), len(m["constsB"]),
            ", ".join(names.get(t) for t in m["textures"]) or "-"))
    print("scene: bbox %s .. %s, sphere %s" % (fmt(sc["bmin"]), fmt(sc["bmax"]), fmt(sc["sphere"])))
    for k, s in enumerate(sc["sectors"]):
        print("  sector %d %s flags %08x, meshes %d..%d, bbox %s .. %s" % (
            k, names.get(s["key"]), s["flags"], s["first"], s["first"] + s["count"] - 1, fmt(s["bmin"]), fmt(s["bmax"])))
    for i, m in enumerate(sc["meshes"]):
        el = " ".join("%s%d:%s%s" % (USAGE.get(u, u)[:4], ix, t, "@1" if st else "") for st, o, t, sz, u, ix in m["decl"])
        print("  mesh %d mat %s flags %08x, %d verts (stride %d), %d indices, %s%s" % (
            i, names.get(m["material"]), m["flags"], m["numVerts"], m["stride"], m["numIndices"], el,
            ", skin groups %d" % len(m["skin"]) if m["skin"] else ""))
    return 0


def fmt(v):
    return "(" + ", ".join("%.3f" % x for x in v) + ")"


def cmd_obj(pakpath, name, out):
    names = default_table()
    p = Pak.open(pakpath)
    e = find_entry(p, name, names)
    d = p.data(e)
    sc = parse_scene(d, KIND_KEYS[e.type])
    base = 1
    lo = [math.inf] * 3
    hi = [-math.inf] * 3
    nverts = ntris = 0
    with open(out, "w") as f:
        f.write("# %s\n" % names.get(e.fullname))
        for i, m in enumerate(sc["meshes"]):
            vs = mesh_vertices(d, m)
            f.write("o mesh%d_%s\n" % (i, names.get(m["material"])))
            for a in vs:
                x, y, z = a["position0"][:3]
                f.write("v %.6f %.6f %.6f\n" % (x, y, z))
                for k, c in enumerate((x, y, z)):
                    lo[k], hi[k] = min(lo[k], c), max(hi[k], c)
            for a in vs:
                t = a.get("texcoord0", (0.0, 0.0))
                f.write("vt %.6f %.6f\n" % (t[0], 1.0 - t[1]))
            for a in vs:
                n = a.get("normal0", (0.0, 0.0, 1.0))
                f.write("vn %.4f %.4f %.4f\n" % n)
            for a, b, c in strip_triangles(m["indices"]):
                f.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n" % ((a + base,) * 3 + (b + base,) * 3 + (c + base,) * 3))
                ntris += 1
            base += len(vs)
            nverts += len(vs)
    print("%d meshes, %d vertices, %d triangles, bounds %s .. %s" % (len(sc["meshes"]), nverts, ntris, fmt(lo), fmt(hi)))
    return 0


def main(argv):
    if len(argv) >= 3 and argv[1] == "check":
        return cmd_check(argv[2])
    if len(argv) >= 4 and argv[1] == "dump":
        return cmd_dump(argv[2], argv[3])
    if len(argv) >= 5 and argv[1] == "obj":
        return cmd_obj(argv[2], argv[3], argv[4])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
