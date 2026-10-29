#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero III (Xbox 360): animations, skeletons, fonts and the small
auxiliary pak files (.ska .ske .fnt .wav .table .nqb .pfx .rag) and the loose
DATA/ANIMS/standardkey{Q,T}.bin.xen.

    anim.py check <rip dir>          parse every such file to its last byte
    anim.py dump  <pak> <name>       summary of one entry (name, or hex key)

Big-endian unless said. [C] = all disc data agrees (or the XEX address given),
[I] = inferred. Layouts: see the parse_* functions and docs (hand-back notes).

.ska (header at file+0x20, flags at +4; XEX: pointer fix-up FUN_82420f90,
decompressor Function_82421900, Q key decoder Function_824213B8, T key decoder
FUN_82421840):
  Q key: [u16 time if flags&0x100] u16 W; W&0x4000 clear: 3 x s16 (2-aligned);
         W&0x4000 and W&0x3800 == 0: one byte = standard key index (all 0 on
         the disc: x=y=z=0); else x/y/z are one unsigned byte when bit
         0x2000/0x1000/0x800 is set, else an s16 aligned to 2. Time = W
         (plain) or W&0x7ff, unless flags&0x100 gave it. Key ends 2-aligned.
         Component/16384; w = sqrt(max(0, 1-x2-y2-z2)).
  T key: byte b; b&0x40: time = b&0x3f, else time = s16 that follows; b&0x80:
         no value (hold), end 2-aligned; else 3 floats at the next 16-aligned
         offset of the file.
  Times in frames (60 per second). flags&0x10000000: one pose, 16-byte float
  keys (Q xyzw, T xyz+1).
"""
import math
import os
import struct
import sys
from concurrent.futures import ProcessPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pak import Pak, all_paks  # noqa: E402
from qbkey import qbkey, default_table  # noqa: E402
import qb  # noqa: E402

TYPES = [".ska", ".ske", ".fnt", ".wav", ".table", ".nqb", ".pfx", ".rag"]
TYPE_OF = {qbkey(t): t for t in TYPES}
FPS = 60.0
Q_SCALE = 1.0 / 16384.0


class FormatError(Exception):
    pass


def need(cond, what):
    if not cond:
        raise FormatError(what)


def zeros(d, a, b, what):
    need(0 <= a <= b <= len(d), "%s: range %#x..%#x" % (what, a, b))
    need(not any(d[a:b]), "%s: non-zero padding at %#x..%#x" % (what, a, b))


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


# ---------------------------------------------------------------- .ske

def parse_ske(d):
    """Skeleton. Header 0x30: u32 0x00010030, u32 bone count, u32 0, u32 0, then
    8 offsets: names, parents, mirror names, mirror indices, flags (u8),
    inverse bind matrices (4x4 f32, row-major, row vectors), local
    translations (vec4, w = 1), local rotations (quat xyzw).
    [C: all 35] Parent key 0 = root; parents precede children; mirror name 0
    <=> mirror index -1, else names[mirror index] == mirror name; quaternions
    unit; with R(q) the usual quaternion matrix used for row vectors (v' = v R),
    World = Local * World(parent) and matrix[i] = inverse(World[i]) (max error
    3.6e-6). Flags byte per bone: 0,5,7,8,9,10 [I: bone class, e.g. 7 twist,
    8 finger]."""
    need(len(d) >= 0x30, "ske: short")
    h = struct.unpack_from(">12I", d, 0)
    need(h[0] == 0x00010030 and h[2] == 0 and h[3] == 0, "ske: header %08x" % h[0])
    n = h[1]
    on, op, ofn, ofi, ofl, om, ot, oq = h[4:12]
    need(ot == 0x30 and oq == ot + 16 * n and om == oq + 16 * n and on == om + 64 * n and
         op == on + 4 * n and ofn == op + 4 * n and ofi == ofn + 4 * n and ofl == ofi + 4 * n,
         "ske: table layout")
    end = ofl + n
    need((end + 3) & ~3 == len(d), "ske: size %d, tables end at %d" % (len(d), end))
    zeros(d, end, len(d), "ske tail")
    names = struct.unpack_from(">%dI" % n, d, on)
    pkeys = struct.unpack_from(">%dI" % n, d, op)
    mnames = struct.unpack_from(">%dI" % n, d, ofn)
    mirror = struct.unpack_from(">%di" % n, d, ofi)
    flags = d[ofl:ofl + n]
    T = [struct.unpack_from(">4f", d, ot + 16 * i) for i in range(n)]
    Q = [struct.unpack_from(">4f", d, oq + 16 * i) for i in range(n)]
    M = [struct.unpack_from(">16f", d, om + 64 * i) for i in range(n)]
    idx = {k: i for i, k in enumerate(names)}
    need(len(idx) == n, "ske: duplicate bone names")
    parent = []
    for i, p in enumerate(pkeys):
        if p == 0:
            parent.append(-1)
        else:
            need(p in idx and idx[p] < i, "ske: bone %d parent" % i)
            parent.append(idx[p])
    for i in range(n):
        if mirror[i] < 0:
            need(mirror[i] == -1 and mnames[i] == 0, "ske: bone %d mirror" % i)
        else:
            need(mirror[i] < n and names[mirror[i]] == mnames[i], "ske: bone %d mirror name" % i)
        need(T[i][3] == 1.0, "ske: translation w")
        need(abs(sum(c * c for c in Q[i]) - 1) < 1e-4, "ske: bone %d quaternion not unit" % i)
    # Bind check: World = Local * World(parent) (row vectors), matrix = World^-1.
    W = []
    for i in range(n):
        L = mat_rt(Q[i], T[i])
        W.append(L if parent[i] < 0 else mat_mul(L, W[parent[i]]))
        P = mat_mul(W[i], M[i])
        err = max(abs(P[r * 4 + c] - (1.0 if r == c else 0.0)) for r in range(4) for c in range(4))
        need(err < 1e-3, "ske: bone %d inverse bind error %g" % (i, err))
    return dict(n=n, names=names, parent=parent, mirror=mirror, flags=flags, T=T, Q=Q)


def mat_rt(q, t):
    x, y, z, w = q
    return [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0,
            2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0,
            2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0,
            t[0], t[1], t[2], 1]


def mat_mul(a, b):
    return [sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4)) for r in range(4) for c in range(4)]


# ---------------------------------------------------------------- .ska

def dec_q_track(d, o, end, flags):
    keys = []
    p = o
    while p < end:
        sep = None
        if flags & 0x100:
            sep = struct.unpack_from(">H", d, p)[0]
            p += 2
        W = struct.unpack_from(">H", d, p)[0]
        p += 2
        if not W & 0x4000:
            c = struct.unpack_from(">3H", d, p)
            p += 6
            tm = W
        elif not W & 0x3800:
            i = d[p]
            p += 1
            need(i == 0, "ska: standard key index %d (table is all zero)" % i)
            c = (0, 0, 0)
            tm = W & 0x7FF
        else:
            c = []
            for bit in (0x2000, 0x1000, 0x800):
                if W & bit:
                    c.append(d[p])
                    p += 1
                else:
                    if p & 1:
                        need(d[p] == 0, "ska: q pad")
                        p += 1
                    c.append(struct.unpack_from(">H", d, p)[0])
                    p += 2
            tm = W & 0x7FF
        if p & 1:
            need(p < end and d[p] == 0, "ska: q key pad")
            p += 1
        if sep is not None:
            tm = sep
        x, y, z = (s16(v) * Q_SCALE for v in c)
        n2 = x * x + y * y + z * z
        need(n2 <= 1.0 + 1e-4, "ska: quaternion xyz length %g" % n2)
        keys.append((tm, (x, y, z, math.sqrt(max(0.0, 1.0 - n2)))))
    need(p == end, "ska: q track overrun")
    return keys


def dec_t_track(d, o, end):
    keys = []
    p = o
    last = None
    while p < end:
        b = d[p]
        p += 1
        if b & 0x40:
            tm = b & 0x3F
        else:
            tm = s16(struct.unpack_from(">H", d, p)[0])
            p += 2
        if b & 0x80:
            need(last is not None, "ska: hold key first")
            if p & 1:
                need(d[p] == 0, "ska: t pad")
                p += 1
            keys.append((tm, last))
            continue
        a = (p + 15) & ~15
        zeros(d, p, a, "ska t align")
        v = struct.unpack_from(">3f", d, a)
        need(all(math.isfinite(c) and abs(c) < 1e5 for c in v), "ska: translation %r" % (v,))
        p = a + 12
        last = v
        keys.append((tm, v))
    need(p == end, "ska: t track overrun")
    return keys


def parse_ska(d):
    need(len(d) >= 0x80, "ska: short")
    h = struct.unpack_from(">8I", d, 0)
    need(h[0] == 0 and h[1] == 0xFFFFFFFF and h[2] == len(d) and h[3] == 0x20 and
         h[4] == h[5] == h[7] == 0xFFFFFFFF, "ska: file header")
    a = 0x20
    ver, flags, dur = struct.unpack_from(">IIf", d, a)
    nb, nq16 = struct.unpack_from(">HH", d, a + 0x0C)
    va = struct.unpack_from(">4f", d, a + 0x10)
    vb = struct.unpack_from(">4f", d, a + 0x20)
    nt16, z0, custom, oq, ot, oqs, ots = struct.unpack_from(">HHIIIII", d, a + 0x30)
    need(ver == 0x48 and z0 == 0 and custom == 0xFFFFFFFF, "ska: anim header")
    zeros(d, a + 0x48, 0x80, "ska header tail")
    need(oq == 0x80 and ot % 128 == 0 and oqs % 128 == 0 and ots % 128 == 0 and
         oq <= ot <= oqs <= ots, "ska: section offsets")
    need(not flags & 0x8000 and not flags & 0x2000, "ska: flags %#x (8-bit / custom keys)" % flags)
    qs = struct.unpack_from(">%dH" % nb, d, oqs)
    ts = struct.unpack_from(">%dH" % nb, d, ots)
    zeros(d, oqs + 2 * nb, ots, "ska q sizes pad")
    need(oq + sum(qs) <= ot and ot + sum(ts) <= oqs, "ska: track sizes")
    rot, pos = [], []
    flt = bool(flags & 0x10000000)
    o = oq
    for b in range(nb):
        if flt:
            need(qs[b] % 16 == 0, "ska: float q size")
            ks = [(0, struct.unpack_from(">4f", d, o + 16 * i)) for i in range(qs[b] // 16)]
            for _, q in ks:
                need(abs(sum(c * c for c in q) - 1) < 1e-3, "ska: float quaternion not unit")
        else:
            ks = dec_q_track(d, o, o + qs[b], flags)
        rot.append(ks)
        o += qs[b]
    zeros(d, o, ot, "ska q pad")
    o = ot
    for b in range(nb):
        if flt:
            need(ts[b] % 16 == 0, "ska: float t size")
            ks = [(0, struct.unpack_from(">4f", d, o + 16 * i)) for i in range(ts[b] // 16)]
            for _, v in ks:
                need(v[3] == 1.0 and all(math.isfinite(c) for c in v), "ska: float translation")
        else:
            ks = dec_t_track(d, o, o + ts[b])
        pos.append(ks)
        o += ts[b]
    zeros(d, o, oqs, "ska t pad")
    nq = sum(len(k) for k in rot)
    nt = sum(len(k) for k in pos)
    need(nq & 0xFFFF == nq16 and nt & 0xFFFF == nt16, "ska: key counts %d/%d vs %d/%d" % (nq, nt, nq16, nt16))
    tail = ots + 2 * nb
    mask = None
    if flags & 0x80000:
        m = h[6]
        need(m != 0xFFFFFFFF and m >= tail and m - tail < 4, "ska: mask offset")
        zeros(d, tail, m, "ska sizes pad")
        cnt = struct.unpack_from(">I", d, m)[0]
        nw = (cnt + 31) // 32
        need(cnt == nb and m + 4 + 4 * nw == len(d), "ska: mask size")
        words = struct.unpack_from(">%dI" % nw, d, m + 4)
        mask = [(words[i >> 5] >> (i & 31)) & 1 for i in range(nb)]
        if not flt:
            need(mask == [1 if (rot[i] or pos[i]) else 0 for i in range(nb)], "ska: mask != animated bones")
    else:
        need(h[6] == 0xFFFFFFFF and len(d) - tail < 4, "ska: tail")
        zeros(d, tail, len(d), "ska tail")
    frames = round(dur * FPS)
    for ks in rot + pos:
        if not ks:
            continue
        tms = [k[0] for k in ks]
        need(tms[0] == 0, "ska: first key time %d" % tms[0])
        need(all(b > a for a, b in zip(tms, tms[1:])), "ska: key times not increasing")
        if not flt and len(ks) > 1:
            need(tms[-1] == frames, "ska: last key %d, duration %d frames" % (tms[-1], frames))
    return dict(flags=flags, duration=dur, bones=nb, rot=rot, pos=pos, mask=mask, va=va, vb=vb,
                qkeys=nq, tkeys=nt)


# ---------------------------------------------------------------- .fnt

FNT_GLYPHS = 0x200A4


def parse_fnt(d):
    """Font [C: all 16; XEX fix-up Function_8232B458, text width Function_8232ADF0,
    drawing Function_8232B5E8]."""
    need(len(d) >= FNT_GLYPHS, "fnt: short")
    vshift, bearing, tracking, line_h, goff = struct.unpack_from(">iiifI", d, 0)
    need(goff == FNT_GLYPHS, "fnt: glyph offset %#x" % goff)
    cmap = struct.unpack_from(">65536H", d, 0x14)
    bmap = struct.unpack_from(">32H", d, 0x20014)
    marker, link, space = struct.unpack_from(">IIf", d, 0x20054)
    need(marker == 0x0001DEAD and link == 0xFFFFFFFF, "fnt: marker")
    zeros(d, 0x20060, 0x200A0, "fnt reserved")
    tex = struct.unpack_from(">I", d, 0x200A0)[0]
    n = max(max(cmap), max(bmap)) + 1
    gend = FNT_GLYPHS + 0x24 * n
    need(gend <= tex < len(d) and tex % 0x1000 == 0, "fnt: texture offset %#x" % tex)
    zeros(d, gend, tex, "fnt glyph pad")
    glyphs = []
    for i in range(n):
        g = struct.unpack_from(">7f2i", d, FNT_GLYPHS + 0x24 * i)
        need(all(math.isfinite(v) for v in g[:7]) and all(0 <= v <= 1 for v in g[:4]), "fnt: glyph %d" % i)
        glyphs.append(g)
    th = parse_font_texture(d, tex)
    return dict(vshift=vshift, bearing=bearing, tracking=tracking, line_height=line_h, space=space,
                glyphs=glyphs, cmap=cmap, bmap=bmap, tex=tex, texinfo=th,
                chars=sum(1 for v in cmap if v))


def parse_font_texture(d, tex):
    """The embedded texture: a 0x28-byte header (+0 0x0A281102, +8 u16 width,
    +10 u16 height, +0x1C offset 0x28 of the GPU header that follows [C: all 16]);
    the pixels start at the next 4 KiB boundary past tex+0x5B (Function_8232B458:
    (tex + 0x105B) & ~0xFFF) and run to the end of the file. Pixel format: the
    texture decoder (.img)."""
    need(struct.unpack_from(">I", d, tex)[0] == 0x0A281102, "fnt: texture magic")
    w, h = struct.unpack_from(">HH", d, tex + 8)
    need(struct.unpack_from(">I", d, tex + 0x1C)[0] == 0x28, "fnt: texture header offset")
    pix = (tex + 0x105B) & ~0xFFF
    zeros(d, tex + 0x5C, pix, "fnt texture header pad")
    need(pix < len(d), "fnt: no pixels")
    return dict(width=w, height=h, header=tex, pixels=pix, pixel_bytes=len(d) - pix)


# ---------------------------------------------------------------- .wav

def parse_wav(d):
    """RIFF/WAVE (little-endian) with fmt (XMA, tag 0x165), data, seek [C: all 227;
    read by FMOD Ex's XMA codec, fmod_codec_xma.cpp: Function_826F8FF8 'RIFF',
    Function_826F8A00 'fmt ' / 'seek']."""
    need(d[:4] == b"RIFF" and d[8:12] == b"WAVE", "wav: RIFF")
    need(struct.unpack_from("<I", d, 4)[0] + 8 == len(d), "wav: RIFF size")
    p, ck = 12, {}
    order = []
    while p < len(d):
        need(p + 8 <= len(d), "wav: chunk header")
        cid = d[p:p + 4]
        n = struct.unpack_from("<I", d, p + 4)[0]
        need(p + 8 + n <= len(d), "wav: chunk size")
        ck[cid] = d[p + 8:p + 8 + n]
        order.append(cid)
        p += 8 + n + (n & 1)
    need(p == len(d) and order == [b"fmt ", b"data", b"seek"], "wav: chunks %r" % order)
    f = ck[b"fmt "]
    need(len(f) == 32, "wav: fmt size")
    tag, bits, enc, skip, nstreams, loops, ver = struct.unpack_from("<HHHHHBB", f, 0)
    pbs, rate, ls, le, sub, ch, cmask = struct.unpack_from("<IIIIBBH", f, 12)
    need(tag == 0x165 and bits == 16 and nstreams == 1 and ver == 2 and ch in (1, 2) and cmask == 0,
         "wav: fmt")
    data = ck[b"data"]
    need(len(data) % 2048 == 0 and data, "wav: data not whole 2 KiB packets")
    s = ck[b"seek"]
    sv, sn = struct.unpack_from("<II", s, 0)
    need(sv == 1 and sn == len(data) // 2048 and len(s) == 8 + 4 * sn, "wav: seek table")
    seek = struct.unpack_from("<%dI" % sn, s, 8)
    need(seek[0] == 0 and all(b >= a for a, b in zip(seek, seek[1:])), "wav: seek order")
    frames = xma_frames(data)
    return dict(rate=rate, channels=ch, loops=loops, loop=(ls, le), packets=sn, seek=seek,
                samples=512 * frames, enc=enc)


def xma_frames(data):
    """Counts the XMA frames (512 samples each) [I]: packet header (u32 BE)
    = 4-bit sequence, 2 bits, 15-bit bit offset of the first frame, 3 + 8
    bits; frames are 15-bit-length-prefixed bit strings running across the
    packets' 2044-byte payloads; length 0x7FFF ends the packet's frames."""
    np_ = len(data) // 2048
    payload = b"".join(data[2048 * i + 4:2048 * (i + 1)] for i in range(np_))
    val = int.from_bytes(payload, "big")
    nbits = len(payload) * 8
    pb = 2044 * 8
    hdr = [struct.unpack_from(">I", data, 2048 * i)[0] for i in range(np_)]
    for i, h in enumerate(hdr):
        need((h >> 28) == (i & 15), "wav: packet sequence")
    pos = (hdr[0] >> 11) & 0x7FFF
    n = 0
    while pos + 15 <= nbits:
        L = (val >> (nbits - pos - 15)) & 0x7FFF
        if L in (0, 0x7FFF):
            k = pos // pb + 1
            if k >= np_:
                break
            pos = k * pb + ((hdr[k] >> 11) & 0x7FFF)
            continue
        n += 1
        pos += L
    return n


# ---------------------------------------------------------------- small types

def parse_table(d):
    """.table: one u32, 0xE5F50254 in all 12 [C data; meaning unknown, I]."""
    need(len(d) == 4, "table: size")
    return dict(value=struct.unpack(">I", d)[0])


def parse_pfx(d):
    """Node-array prefix index [C: all 24]: u32 node array name key, u32 count,
    count x {u32 prefix key, u32 byte offset into the index list, u32 count},
    sorted by key; then the u16 node indices; zero pad to 2... end."""
    need(len(d) >= 8, "pfx: short")
    name, n = struct.unpack_from(">2I", d, 0)
    base = 8 + 12 * n
    need(base <= len(d), "pfx: table")
    ents = [struct.unpack_from(">3I", d, 8 + 12 * i) for i in range(n)]
    need(all(a[0] < b[0] for a, b in zip(ents, ents[1:])), "pfx: keys not sorted")
    used = bytearray(len(d) - base)
    for k, o, c in ents:
        need(o % 2 == 0 and c > 0 and base + o + 2 * c <= len(d), "pfx: entry range")
        for i in range(o, o + 2 * c):
            used[i] = 1
    need(all(used), "pfx: unreferenced index bytes")
    idx = {k: struct.unpack_from(">%dH" % c, d, base + o) for k, o, c in ents}
    return dict(name=name, prefixes=idx)


def parse_nqb(d, filekey):
    """.nqb: an ordinary QB file (the zone's node array, NodeArray_SortedNames /
    _SortedIndices, node scripts) [C: all 24 decode with qb.decode, every byte]."""
    try:
        out, unused = qb.decode(d, filekey)
    except qb.QbError as e:
        raise FormatError("nqb: %s" % e)
    need(not unused, "nqb: %d unused bytes" % len(unused))
    return dict(sections=out)


def parse_rag(d):
    """.rag: Havok 4.6.0-r1 binary packfile (big-endian layout rules 04 00 00 01)
    [C: all 27]: 0x40-byte header, 3 section headers of 0x30 (name[20], data
    start, then local / global / virtual fix-up, exports, imports, end offsets
    relative to the start), sections padded with 0xFF; local fix-ups (src, dst)
    pairs, global and virtual (src, section, dst) triples, 0xFF padded."""
    need(len(d) >= 0x40, "rag: short")
    m0, m1, tag, ver = struct.unpack_from(">4I", d, 0)
    need((m0, m1) == (0x57E0E057, 0x10C0C010) and ver == 4, "rag: magic")
    need(d[0x10:0x14] == b"\x04\x00\x00\x01", "rag: layout rules")
    ns, csi, cso, cnsi, cnso = struct.unpack_from(">5i", d, 0x14)
    need(d[0x28:0x38] == b"Havok-4.6.0-r1\x00\xff" and d[0x38:0x40] == b"\xff" * 8, "rag: version")
    need(0 < ns <= 8 and 0 <= csi < ns and 0 <= cnsi < ns, "rag: sections")
    secs = []
    for i in range(ns):
        o = 0x40 + 0x30 * i
        nm = d[o:o + 20]
        z = nm.index(b"\x00")
        secs.append((nm[:z].decode("latin-1"), struct.unpack_from(">7I", d, o + 20)))
    pos = 0x40 + 0x30 * ns
    for nm, (start, loc, glo, vir, exp, imp, end) in secs:
        need(start >= pos and d[pos:start] == b"\xff" * (start - pos), "rag: gap before %s" % nm)
        need(loc <= glo <= vir <= exp <= imp <= end and start + end <= len(d), "rag: %s offsets" % nm)
        need(exp == imp == end, "rag: exports / imports")
        hk_fixups(d, start + loc, start + glo, 8, loc, None, secs)
        hk_fixups(d, start + glo, start + vir, 12, loc, ns, secs)
        hk_fixups(d, start + vir, start + exp, 12, loc, ns, secs)
        pos = start + end
    need(pos == len(d), "rag: tail")
    start, loc = secs[cnsi][1][0], secs[cnsi][1][1]
    o, names = start, {}
    while o < start + loc and d[o] != 0xFF:
        need(d[o + 4] == 9, "rag: class name entry")
        e = d.index(b"\x00", o + 5)
        names[o + 5 - start] = d[o + 5:e].decode("latin-1")
        o = e + 1
    need(d[o:start + loc] == b"\xff" * (start + loc - o), "rag: class names pad")
    need(cnso in names, "rag: contents class name")
    return dict(sections=[s[0] for s in secs], classes=list(names.values()), root=names[cnso])


def hk_fixups(d, a, b, size, limit, ns, secs):
    o = a
    while o + size <= b:
        v = struct.unpack_from(">%dI" % (size // 4), d, o)
        if v[0] == 0xFFFFFFFF:
            break
        need(v[0] < limit, "rag: fix-up source")
        if ns is None:
            need(v[1] < limit, "rag: local fix-up target")
        else:
            need(v[1] < ns and v[2] < secs[v[1]][1][1], "rag: fix-up target")
        o += size
    need(d[o:b] == b"\xff" * (b - o), "rag: fix-up pad")


def parse_standardkey(d):
    """standardkey{Q,T}.bin.xen: 256 x 8 bytes, LITTLE-endian u16 (x, y, z, v) =
    (0, 0, 0, i) [C: both files]. The XEX never names these files; its Q decoder
    (Function_824213B8) reads a standard table of 8-byte entries (x,y,z at +0,
    +2, +4; byte components via +6) - consistent with this table: index keys
    give x=y=z=0, byte components their own value."""
    need(len(d) == 2048, "standardkey: size")
    for i in range(256):
        need(struct.unpack_from("<4H", d, 8 * i) == (0, 0, 0, i), "standardkey: entry %d" % i)
    return {}


# ---------------------------------------------------------------- driver

def parse(t, d, e=None):
    if t == ".ska":
        return parse_ska(d)
    if t == ".ske":
        return parse_ske(d)
    if t == ".fnt":
        return parse_fnt(d)
    if t == ".wav":
        return parse_wav(d)
    if t == ".table":
        return parse_table(d)
    if t == ".pfx":
        return parse_pfx(d)
    if t == ".nqb":
        return parse_nqb(d, e.fullname if e is not None else None)
    if t == ".rag":
        return parse_rag(d)
    raise FormatError("type %s" % t)


def check_pak(path):
    counts, probs = {}, []
    p = Pak.open(path)
    for e in p.entries:
        t = TYPE_OF.get(e.type)
        if not t:
            continue
        counts[t] = counts.get(t, 0) + 1
        try:
            parse(t, p.data(e), e)
        except (FormatError, struct.error, ValueError, IndexError) as x:
            probs.append("%s #%d %08x: %s" % (os.path.basename(path), e.index, e.fullname, x))
    return counts, probs


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    if argv[1] == "check":
        rip = argv[2]
        counts, probs = {}, []
        for name in ("standardkeyQ.bin.xen", "standardkeyT.bin.xen"):
            path = os.path.join(rip, "DATA", "ANIMS", name)
            counts["standardkey"] = counts.get("standardkey", 0) + 1
            try:
                parse_standardkey(open(path, "rb").read())
            except (FormatError, OSError) as x:
                probs.append("%s: %s" % (name, x))
        paks = list(all_paks(rip))
        with ProcessPoolExecutor() as ex:
            for c, pr in ex.map(check_pak, paks, chunksize=4):
                for k, v in c.items():
                    counts[k] = counts.get(k, 0) + v
                probs.extend(pr)
        for x in probs[:40]:
            print(x)
        print(", ".join("%s %d" % (k, counts.get(k, 0)) for k in TYPES + ["standardkey"]))
        print("files: %d, with problems: %d" % (sum(counts.values()), len(probs)))
        return 1 if probs else 0
    if argv[1] == "dump" and len(argv) >= 4:
        names = default_table()
        p = Pak.open(argv[2])
        want = argv[3].lower()
        for e in p.entries:
            t = TYPE_OF.get(e.type)
            nm = names.get(e.fullname)
            if not t or (want not in nm.lower() and want != "%08x" % e.fullname):
                continue
            r = parse(t, p.data(e), e)
            print("%s %s (%d bytes)" % (t, nm, e.size))
            dump(t, r, names)
        return 0
    print(__doc__)
    return 2


def dump(t, r, names):
    if t == ".ska":
        print("  flags %#010x, duration %.4f s (%d frames), bones %d, keys Q %d T %d" % (
            r["flags"], r["duration"], round(r["duration"] * FPS), r["bones"], r["qkeys"], r["tkeys"]))
        print("  header vec A %s, vec B %s" % (fmtv(r["va"]), fmtv(r["vb"])))
        anim = [i for i in range(r["bones"]) if r["rot"][i] or r["pos"][i]]
        print("  animated bones %d%s" % (len(anim), ", partial mask" if r["mask"] else ""))
        for i in anim[:12]:
            q, tr = r["rot"][i], r["pos"][i]
            print("   bone %3d: %4d rot keys %s, %4d pos keys %s" % (
                i, len(q), fmtv(q[0][1]) if q else "-", len(tr), fmtv(tr[0][1]) if tr else "-"))
    elif t == ".ske":
        print("  bones %d" % r["n"])
        for i in range(r["n"]):
            print("   %3d %-32s parent %3d mirror %3d flags %2d T %s Q %s" % (
                i, names.get(r["names"][i]), r["parent"][i], r["mirror"][i], r["flags"][i],
                fmtv(r["T"][i][:3]), fmtv(r["Q"][i])))
    elif t == ".fnt":
        ti = r["texinfo"]
        print("  glyphs %d (chars mapped %d), line height %g, space %g, vshift %d, bearing %d, tracking %d" % (
            len(r["glyphs"]), r["chars"], r["line_height"], r["space"], r["vshift"], r["bearing"], r["tracking"]))
        print("  texture %dx%d, header at %#x, pixels at %#x (%d bytes)" % (
            ti["width"], ti["height"], ti["header"], ti["pixels"], ti["pixel_bytes"]))
    elif t == ".wav":
        print("  XMA, %d Hz, %d channel(s), %d samples (%.3f s), %d packets, loops %d" % (
            r["rate"], r["channels"], r["samples"], r["samples"] / r["rate"], r["packets"], r["loops"]))
    elif t == ".pfx":
        print("  node array %s, %d prefixes" % (names.get(r["name"]), len(r["prefixes"])))
    elif t == ".nqb":
        print("  %d sections: %s" % (len(r["sections"]), ", ".join(names.get(s[0]) for s in r["sections"][:8])))
    elif t == ".rag":
        print("  Havok packfile, root %s, %d classes, sections %s" % (r["root"], len(r["classes"]), r["sections"]))
    elif t == ".table":
        print("  value %#010x" % r["value"])


def fmtv(v):
    return "(" + ", ".join("%.4f" % c for c in v) + ")"


if __name__ == "__main__":
    sys.exit(main(sys.argv))
