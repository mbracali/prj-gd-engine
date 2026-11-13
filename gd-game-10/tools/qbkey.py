#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""QB checksums ("QB keys") and the checksum -> name table of Guitar Hero III.

A QB key is CRC-32 (reflected polynomial 0xEDB88320, initial value 0xFFFFFFFF)
of the lower-cased name, *without* the final XOR. File paths use backslashes.
[C] data: every pak entry type (".qb" -> 0xa7f505c4) and every compress.toc
path key match; see docs/formats.md.

    qbkey.py <name> ...          print the keys
    qbkey.py --table <out.tsv>   build the debug-name table from the rip
"""
import os
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def qbkey(name):
    if isinstance(name, str):
        name = name.lower().encode("latin-1")
    else:
        name = name.lower()
    return (zlib.crc32(name) ^ 0xFFFFFFFF) & 0xFFFFFFFF


class Names:
    """checksum -> name, collected from every source the rip offers."""

    def __init__(self):
        self.by_key = {}

    def add(self, name):
        if not name:
            return
        k = qbkey(name)
        self.by_key.setdefault(k, name)

    def get(self, k, default=None):
        n = self.by_key.get(k)
        if n is not None:
            return n
        return default if default is not None else "0x%08x" % k

    def load_tsv(self, path):
        with open(path, encoding="latin-1") as f:
            for line in f:
                p = line.rstrip("\n").split("\t")
                if len(p) == 2:
                    self.by_key[int(p[0], 16)] = p[1]

    def save_tsv(self, path):
        with open(path, "w", encoding="latin-1") as f:
            for k in sorted(self.by_key):
                f.write("%08x\t%s\n" % (k, self.by_key[k]))


def default_table():
    """The table built by `qbkey.py --table` into extracted/names.tsv."""
    n = Names()
    p = os.path.join(HERE, "..", "extracted", "names.tsv")
    if os.path.exists(p):
        n.load_tsv(p)
    return n


def build_table(out):
    sys.path.insert(0, HERE)
    import pak
    ext = os.path.join(HERE, "..", "extracted")
    names = Names()
    # Extensions seen on the disc and in the paks.
    for e in ("qb", "img", "tex", "scn", "col", "ska", "ske", "fnt", "pak", "pab", "mqb", "nqb", "dbg",
              "last", "skin", "mdl", "fam", "fsb", "dat", "qs", "qs.en", "qs.de", "qs.fr", "qs.it",
              "qs.es", "rag", "cif", "pimg", "stex", "anim", "geom", "empty", "sqb", "png", "nav",
              "clt", "note", "perm", "fx", "rnb", "wav", "tvx", "cam", "lst", "toc", "xen", "bik"):
        names.add("." + e)
    # Every path on the disc and in compress.toc.lst.
    for root, _, files in os.walk(ext):
        for f in files:
            rel = os.path.relpath(os.path.join(root, f), ext).replace("/", "\\")
            names.add(rel)
            base = f
            while "." in base:
                names.add(base)
                base = base.rsplit(".", 1)[0]
            names.add(base)
    lst = os.path.join(ext, "DATA", "COMPRESSED", "compress.toc.lst.xen")
    if os.path.exists(lst):
        for line in open(lst, encoding="latin-1"):
            if line.startswith("Name: "):
                names.add(line[6:].rsplit(" ", 2)[0])
    # dbg.pak: one text file per QB file, lines "0x%08x name".
    dbg = os.path.join(ext, "DATA", "COMPRESSED", "PAK", "dbg.pak.xen")
    if os.path.exists(dbg):
        p = pak.Pak.open(dbg)
        for e in p.entries:
            for line in p.data(e).decode("latin-1", "replace").splitlines():
                parts = line.strip().split(None, 1)
                if len(parts) == 2 and parts[0].lower().startswith("0x"):
                    try:
                        names.by_key.setdefault(int(parts[0], 16), parts[1].strip())
                    except ValueError:
                        pass
    names.save_tsv(out)
    print("names: %d" % len(names.by_key))


def main(argv):
    if len(argv) >= 3 and argv[1] == "--table":
        build_table(argv[2])
        return 0
    if len(argv) < 2:
        print(__doc__)
        return 2
    for a in argv[1:]:
        print("%08x  %s" % (qbkey(a), a))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
