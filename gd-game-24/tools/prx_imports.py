#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""List a decrypted PSP executable's sections, module info and imports, and write
the import stubs as a TSV for tools/ghidra_scripts/NameStubs.java.

usage: prx_imports.py <EBOOT.prx> <nids.txt> <stubs.tsv>

nids.txt: one "<NID hex> <name>" per line (made from PPSSPP's Core/HLE tables, see
agents/CONTEXT.md); unknown NIDs are named <library>_<NID>.
stubs.tsv: stub_address, name, library, NID (one 8-byte stub per imported function).
"""
import struct
import sys


def main():
    d = open(sys.argv[1], 'rb').read()
    nids = {}
    for line in open(sys.argv[2]):
        a, b = line.split()
        nids[int(a, 16)] = b
    e = struct.unpack_from('<16sHHIIIIIHHHHHH', d, 0)
    typ, _mach, _ver, entry, phoff, shoff, flags, _ehs, phes, phn, shes, shn, shstr = e[1:]
    print('type', hex(typ), 'entry', hex(entry), 'flags', hex(flags), 'phn', phn, 'shn', shn)
    phs = [struct.unpack_from('<8I', d, phoff + i * phes) for i in range(phn)]
    for p in phs:
        print(' PH', [hex(x) for x in p])
    shs = [struct.unpack_from('<10I', d, shoff + i * shes) for i in range(shn)]
    so = shs[shstr][4] if shn else 0

    def nm(o):
        return d[so + o:d.index(b'\0', so + o)].decode()

    secs = {}
    for s in shs:
        n = nm(s[0])
        secs[n] = s
        print(' SH %-24s addr %08x off %08x size %08x' % (n, s[3], s[4], s[5]))

    def va2off(va):
        for p in phs:
            if p[0] == 1 and p[2] <= va < p[2] + p[4]:
                return va - p[2] + p[1]
        raise ValueError(hex(va))

    def cstr(va):
        o = va2off(va)
        return d[o:d.index(b'\0', o)].decode()

    mi = secs['.rodata.sceModuleInfo']
    _attr, _ver2, name, gp, es, ee, ss, se = struct.unpack_from('<HH28sIIIII', d, mi[4])
    print('module', name.rstrip(b'\0'), 'gp', hex(gp), 'exports', hex(es), hex(ee), 'imports', hex(ss), hex(se))
    va = ss
    known = unk = 0
    with open(sys.argv[3], 'w') as out:
        while va < se:
            o = va2off(va)
            lib, _v, _a, elen, vcount, fcount, nidp, stubp = struct.unpack_from('<IHHBBHII', d, o)
            ln = cstr(lib) if lib else '?'
            print('import %-28s funcs %3d vars %d' % (ln, fcount, vcount))
            for i in range(fcount):
                nid = struct.unpack_from('<I', d, va2off(nidp + 4 * i))[0]
                n = nids.get(nid)
                if n:
                    known += 1
                else:
                    unk += 1
                    n = '%s_%08X' % (ln, nid)
                out.write('%08x\t%s\t%s\t%08X\n' % (stubp + 8 * i, n, ln, nid))
            va += elen * 4
    print('named', known, 'unknown', unk)


if __name__ == '__main__':
    main()
