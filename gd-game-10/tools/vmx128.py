#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Decodes the Xbox 360 VMX128 (and the AltiVec Ghidra leaves undefined)
instructions that Ghidra's PowerPC language shows as `.word` in a DumpAsm
listing (tools/ghidra_scripts/DumpAsm.java), so the light manager, the
screen effects and the other vector code of default.xex can be read.

    vmx128.py <opcode table> <listing.asm>     the listing with each decodable
                                               `.word 0x...` replaced by its
                                               instruction (original kept after ;)

<opcode table>: the public Xenia emulator's src/xenia/cpu/ppc/ppc_opcode_table_gen.cc
(INSTRUCTION(opcode, "name", format, ...) lines; not shipped here). The
register fields follow its ppc_decode_data.h: VD128 = VD128l | VD128h << 5,
VA128 = VA128l | VA128h << 5 | VA128H << 6, VB128 = VB128l | VB128h << 5;
vrlimi128 prints its rotate as zN. Plain PowerPC words in regions Ghidra
did not define can also match (e.g. as srawix / orx): check those against
the hex.
"""
import re
import sys


def field(lo, n):
    return ((1 << n) - 1) << lo


VX128 = field(0, 2) | field(2, 2) | field(5, 1) | field(10, 1) | field(11, 5) | field(16, 5) | field(21, 5)
VARIABLE = {
    "kVX128": VX128, "kVX128_1": field(2, 2) | field(11, 15), "kVX128_2": VX128 | field(6, 3),
    "kVX128_3": field(0, 4) | field(11, 15), "kVX128_4": field(0, 4) | field(6, 2) | field(11, 15),
    "kVX128_5": VX128 | field(6, 4), "kVX128_R": VX128 | field(6, 1), "kVX128_P": field(0, 4) | field(6, 3) | field(11, 15),
    "kX": field(0, 1) | field(11, 15), "kVX": field(11, 15), "kVA": field(6, 20), "kVC": field(10, 1) | field(11, 15),
}


def bits(w, lo, n):
    return (w >> lo) & ((1 << n) - 1)


def load(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    return [(int(m.group(1), 16), m.group(2), m.group(3))
            for m in re.finditer(r'INSTRUCTION\((0x[0-9a-fA-F]+),\s*"([^"]+)"\s*,\s*(k\w+)', text)]


def decode(table, w):
    for op, name, fmt in table:
        if fmt in VARIABLE and op >> 26 == w >> 26 and (w & ~VARIABLE[fmt] & 0xFFFFFFFF) == op:
            break
    else:
        return None
    vd = bits(w, 21, 5) | bits(w, 2, 2) << 5
    va = bits(w, 16, 5) | bits(w, 5, 1) << 5 | bits(w, 10, 1) << 6
    vb = bits(w, 11, 5) | bits(w, 0, 2) << 5
    if fmt in ("kVX128", "kVX128_R"):
        return "%s v%d,v%d,v%d" % (name, vd, va, vb)
    if fmt == "kVX128_2":
        return "%s v%d,v%d,v%d,v%d" % (name, vd, va, vb, bits(w, 6, 3))
    if fmt == "kVX128_5":
        return "%s v%d,v%d,v%d,%d" % (name, vd, va, vb, bits(w, 6, 4))
    if fmt == "kVX128_1":
        return "%s v%d,r%d,r%d" % (name, vd, bits(w, 16, 5), bits(w, 11, 5))
    if fmt == "kVX128_3":
        return "%s v%d,v%d,%d" % (name, vd, vb, bits(w, 16, 5))
    if fmt == "kVX128_4":
        return "%s v%d,v%d,%d,z%d" % (name, vd, vb, bits(w, 16, 5), bits(w, 6, 2))
    if fmt == "kVX128_P":
        return "%s v%d,v%d,%d" % (name, vd, vb, bits(w, 16, 5) | bits(w, 6, 3) << 5)
    if fmt == "kVA":
        return "%s v%d,v%d,v%d,v%d" % (name, bits(w, 21, 5), bits(w, 16, 5), bits(w, 11, 5), bits(w, 6, 5))
    return "%s v%d,%s%d,%s%d" % (name, bits(w, 21, 5), "r" if fmt == "kX" else "v", bits(w, 16, 5), "r" if fmt == "kX" else "v",
                                 bits(w, 11, 5))


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    table = load(argv[1])
    for line in open(argv[2], encoding="utf-8", errors="replace"):
        m = re.search(r"\.word 0x([0-9a-fA-F]+)", line)
        if m:
            d = decode(table, int(m.group(1), 16))
            if d:
                line = line[:m.start()] + d + "   ; " + m.group(0) + line[m.end():]
        sys.stdout.write(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
