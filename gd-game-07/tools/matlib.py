#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero III (Xbox 360) material templates: DATA/FXFILES/MaterialLibrary.bin.xen.

    matlib.py list  <MaterialLibrary.bin.xen>               templates and their shaders
    matlib.py dump  <MaterialLibrary.bin.xen> <name>         constants, links and microcode
                                                            (name: "D_3ChkBox_Pass0_Default")
    matlib.py check <MaterialLibrary.bin.xen>               every shader header and control flow

Layout (big-endian; docs/formats.md "Material library"; the engine's
reader is engine/src/data/matlib.cpp, the translator render/xenos_glsl.cpp):

  0x00  "MATL", u32 2, u32 count, u32 0, u32 string base, u32 0, u32 offsets[count]
  B     template records of 0x28 bytes: u32 0, u32 name (from B), u32 key = qbkey(name), ...
  ...   XDK compiled shaders, one vertex (0x102A1101) and one pixel (0x102A1100)
        shader per technique, named by their .updb path "<template>_<technique>".

A shader: 0x00 magic, 0x04 region offset, 0x08 region size, 0x0c 0x24, 0x10
the constant table's size word (the D3DXSHADER_CONSTANTTABLE follows), 0x14
the literal ranges (or 0), 0x18 the shader info; 0x24 hash, path length,
path. Info: u32 microcode offset in the region (after the literal floats),
u32 microcode size; vertex: [7] fetch count, [8] patch count, from [10] the
fetches (address | usage << 12 | index << 16), the exports (semantic |
register << 8 | mask << 12 | patch start << 16), the patch addresses;
pixel: from [8] the inputs (semantic | register << 8 | mask << 12).
"""
import struct
import sys

VOP = ["add", "mul", "max", "min", "seq", "sgt", "sge", "sne", "frc", "trunc", "floor", "mad", "cndeq", "cndge",
       "cndgt", "dp4", "dp3", "dp2add", "cube", "max4", "setp_eq_push", "setp_ne_push", "setp_gt_push",
       "setp_ge_push", "kill_eq", "kill_gt", "kill_ge", "kill_ne", "dst", "maxa"]
SOP = ["adds", "adds_prev", "muls", "muls_prev", "muls_prev2", "maxs", "mins", "seqs", "sgts", "sges", "snes",
       "frcs", "truncs", "floors", "exp", "logc", "log", "rcpc", "rcpf", "rcp", "rsqc", "rsqf", "rsq", "maxas",
       "maxasf", "subs", "subs_prev", "setp_eq", "setp_ne", "setp_gt", "setp_ge", "setp_inv", "setp_pop",
       "setp_clr", "setp_rstr", "kills_eq", "kills_gt", "kills_ge", "kills_ne", "kills_one", "sqrt", "op41",
       "mulsc0", "mulsc1", "addsc0", "addsc1", "subsc0", "subsc1", "sin", "cos", "retain_prev"]
CFOP = ["nop", "exec", "exec_end", "cexec", "cexec_end", "cexec_pred", "cexec_pred_end", "loop_start",
        "loop_end", "ccall", "ret", "cjmp", "alloc", "cexec_pred_clean", "cexec_pred_clean_end",
        "mark_vs_fetch_done"]
TECHNIQUES = ["Default", "DepthOnly", "AlphaOnly", "DynamicShadowReciever", "ReducedLOD"]


def bits(v, lo, n):
    return (v >> lo) & ((1 << n) - 1)


def cstr(d, o):
    return d[o:d.index(b"\0", o)].decode("latin1")


def load(path):
    d = open(path, "rb").read()
    if d[:4] != b"MATL" or struct.unpack_from(">I", d, 4)[0] != 2:
        raise SystemExit("not a MATL v2 file")
    n = struct.unpack_from(">I", d, 8)[0]
    base = 0x18 + 4 * n
    templates = []
    for i in range(n):
        w = struct.unpack_from(">10I", d, base + 0x28 * i)
        templates.append((cstr(d, base + w[1]), w[2]))
    shaders = []
    o = base + 0x28 * n
    while o + 0x40 <= len(d):
        if d[o:o + 4] not in (b"\x10\x2a\x11\x00", b"\x10\x2a\x11\x01"):
            o += 4
            continue
        s = parse_shader(d, o)
        if not s:
            o += 4
            continue
        shaders.append(s)
        o += (s["length"] + 3) & ~3
    return d, templates, shaders


def parse_shader(d, o):
    h = struct.unpack_from(">7I", d, o)
    region, rsize, ctab, lit, info = h[1], h[2], h[4], h[5], h[6]
    if h[3] != 0x24 or not region or region > 0x10000 or info >= region or ctab >= region:
        return None
    plen = struct.unpack_from(">I", d, o + 0x28)[0]
    name = d[o + 0x2c:o + 0x2c + plen].rstrip(b"\0").decode("latin1").split("\\")[-1].replace(".updb", "")
    ct = o + ctab + 4
    size, _, version, nc, ci = struct.unpack_from(">5I", d, ct)
    if size != 0x1c or version not in (0xFFFE0300, 0xFFFF0300):
        return None
    pixel = version == 0xFFFF0300
    consts = []
    for k in range(nc):
        no, rs, ri, rc, _, _, _ = struct.unpack_from(">IHHHHII", d, ct + ci + 20 * k)
        consts.append((cstr(d, ct + no), "bifs"[rs] if rs < 4 else str(rs), ri, rc))
    words = (region - info) // 4
    iw = struct.unpack_from(">%dI" % words, d, o + info)
    prefix, usize = iw[0], iw[1]
    fetches, links = [], []
    if pixel:
        for w in iw[8:]:
            if not w:
                break
            links.append((w >> 8 & 0xF, w >> 12 & 0xF, w & 0xFF))
    else:
        nf, npatch = iw[7], iw[8]
        for w in iw[10:10 + nf]:
            fetches.append((w & 0xFFF, w >> 12 & 0xF, w >> 16 & 0xF))
        for w in iw[10 + nf:words - npatch]:
            links.append((w >> 8 & 0xF, w >> 12 & 0xF, w & 0xFF))
    literals = []
    if lit:
        p = o + lit + 20
        while p + 8 <= o + info:
            w, off = struct.unpack_from(">2I", d, p)
            if not w:
                break
            reg, cnt = (w >> 16) & 0x1FF, w & 0xFFFF
            literals.append((reg & 0xFF, struct.unpack_from(">%df" % cnt, d, o + region + off)))
            p += 8
    ucode = struct.unpack_from(">%dI" % (usize // 4), d, o + region + prefix)
    return {"offset": o, "length": region + rsize, "name": name, "pixel": pixel, "consts": consts,
            "fetches": fetches, "links": links, "literals": literals, "ucode": ucode}


def control_flow(w):
    out = []
    limit = len(w) // 3
    i = 0
    while i < limit:
        a = w[i * 3] | ((w[i * 3 + 1] & 0xFFFF) << 32)
        b = (w[i * 3 + 1] >> 16) | (w[i * 3 + 2] << 16)
        for x in (a, b):
            out.append(x)
            if bits(x, 44, 4) in (1, 2, 3, 4, 5, 6, 13, 14) and bits(x, 12, 3):
                limit = min(limit, bits(x, 0, 12))
        i += 1
    return out


def swz4(s):
    return "".join("xyzw"[((s >> (2 * i)) + i) & 3] for i in range(4))


def alu(w, pixel):
    w0, w1, w2 = w
    out = []
    exp = bits(w0, 15, 1)

    def dst(r, m):
        if exp:
            n = ({0: "oC0", 1: "oC1", 2: "oC2", 3: "oC3", 61: "oDepth"} if pixel else {62: "oPos", 63: "oPts"}).get(r, "o%d" % r)
        else:
            n = "r%d" % r
        return n + "." + "".join("xyzw"[i] for i in range(4) if m >> i & 1)

    def src(i):
        reg = (bits(w2, 16, 8), bits(w2, 8, 8), bits(w2, 0, 8))[i - 1]
        sel = (bits(w2, 31, 1), bits(w2, 30, 1), bits(w2, 29, 1))[i - 1]
        sw = (bits(w1, 16, 8), bits(w1, 8, 8), bits(w1, 0, 8))[i - 1]
        neg = (bits(w1, 26, 1), bits(w1, 25, 1), bits(w1, 24, 1))[i - 1]
        s = ("r%d" % (reg & 0x3F)) if sel else ("c%d" % reg)
        if (sel and reg & 0x80) or (not sel and bits(w0, 7, 1)):
            s = "|%s|" % s
        return ("-" if neg else "") + s, sw

    s1, s2, s3 = src(1), src(2), src(3)
    vop, sop = bits(w2, 24, 5), bits(w0, 26, 6)
    out.append("%s %s, %s.%s, %s.%s, %s.%s" % (VOP[vop] if vop < 30 else vop, dst(bits(w0, 0, 6), bits(w0, 16, 4)),
                                                s1[0], swz4(s1[1]), s2[0], swz4(s2[1]), s3[0], swz4(s3[1])))
    sw = s3[1]
    out.append("%s %s, %s.%s%s" % (SOP[sop] if sop < 51 else sop, dst(bits(w0, 8, 6), bits(w0, 20, 4)), s3[0],
                                   "xyzw"[((sw >> 6) + 3) & 3], "xyzw"[sw & 3]))
    pred = (" (p%s)" % ("" if bits(w1, 27, 1) else "!")) if bits(w1, 28, 1) else ""
    return " ; ".join(out) + pred


def fetch(w):
    w0, w1, w2 = w
    op = bits(w0, 0, 5)
    ds = "".join("xyzw01?_"[bits(w1, 3 * i, 3)] for i in range(4))
    if op == 0:
        return "vfetch r%d.%s, r%d" % (bits(w0, 12, 6), ds, bits(w0, 5, 6))
    if op == 1:
        ss = "".join("xyzw"[bits(w0, 26 + 2 * i, 2)] for i in range(3))
        return "tfetch%s r%d.%s, r%d.%s, tf%d" % (["1D", "2D", "3D", "Cube"][bits(w2, 14, 2)], bits(w0, 12, 6), ds,
                                                   bits(w0, 5, 6), ss, bits(w0, 20, 5))
    return "fetch op %d" % op


def disassemble(s):
    lines = []
    w = s["ucode"]
    for c in control_flow(w):
        op = bits(c, 44, 4)
        if op in (1, 2, 3, 4, 5, 6, 13, 14):
            addr, cnt, seq = bits(c, 0, 12), bits(c, 12, 3), bits(c, 16, 12)
            lines.append("%s addr %d count %d%s" % (CFOP[op], addr, cnt, " cond %d" % bits(c, 42, 1) if op > 2 else ""))
            for k in range(cnt):
                iw = w[(addr + k) * 3:(addr + k) * 3 + 3]
                lines.append("  %3d %s" % (addr + k, fetch(iw) if (seq >> (2 * k)) & 1 else alu(iw, s["pixel"])))
        elif op:
            lines.append("%s %012x" % (CFOP[op], c))
    return lines


def main(argv):
    if len(argv) < 3 or argv[1] not in ("list", "dump", "check"):
        print(__doc__)
        return 2
    d, templates, shaders = load(argv[2])
    if argv[1] == "list":
        by = {s["name"]: s for s in shaders}
        for name, key in templates:
            techs = [t for t in TECHNIQUES if name + "_" + t in by]
            print("%08x  %-60s %s" % (key, name, " ".join(techs)))
        print("%d templates, %d shaders" % (len(templates), len(shaders)))
        return 0
    if argv[1] == "check":
        bad = 0
        for s in shaders:
            try:
                for c in control_flow(s["ucode"]):
                    if bits(c, 44, 4) in (1, 2, 3, 4, 5, 6, 13, 14):
                        need = (bits(c, 0, 12) + bits(c, 12, 3)) * 3
                        if need > len(s["ucode"]):
                            raise ValueError("exec past the microcode")
            except ValueError as e:
                bad += 1
                print(s["name"], e)
        print("shaders: %d, with problems: %d" % (len(shaders), bad))
        return 1 if bad else 0
    for s in shaders:
        if s["name"] != argv[3]:
            continue
        print("== %s shader %s at %#x" % ("pixel" if s["pixel"] else "vertex", s["name"], s["offset"]))
        for c in s["consts"]:
            print("  const %-36s %s%d x%d" % c)
        for f in s["fetches"]:
            print("  fetch at %d: usage %d index %d" % f)
        for l in s["links"]:
            print("  %s r%d mask %x semantic %02x" % ("input" if s["pixel"] else "export", l[0], l[1], l[2]))
        for reg, vals in s["literals"]:
            print("  literals c%d..: %s" % (reg, ", ".join("%g" % v for v in vals)))
        for line in disassemble(s):
            print("  " + line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
