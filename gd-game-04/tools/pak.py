#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero III (Xbox 360) archives: .pak.xen (+ optional .pab.xen).

    pak.py list    <file.pak.xen>
    pak.py extract <file.pak.xen> <out dir>
    pak.py check   <rip dir>          every pak on the disc parses to its ".last"

Files under DATA/COMPRESSED are raw-deflate streams (no zlib header) of the
whole file; DATA/COMPRESSED/compress.toc.xen lists them (see docs/formats.md).
A pak (after inflating) is a table of 32-byte big-endian entries:

    u32 type      QB key of the extension (".qb", ".img", ... ; ".last" ends it)
    u32 offset    from the start of this entry to the file data
    u32 size
    u32 pak name  QB key (0 on the disc)
    u32 full name QB key of the file's full path
    u32 name      QB key of the short name
    u32 parent    QB key of the parent file's full name (scene -> tex), or 0
    u32 flags

When a .pab.xen sits next to the .pak.xen, the data of the entries lies past
the end of the header file: offsets count through pak + pab as one buffer.
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qbkey import qbkey, default_table  # noqa: E402

KEY_LAST = qbkey(".last")
KEY_LAST2 = qbkey("last")


def read_disc_file(path):
    """Reads a disc file, inflating it when it is one of the compressed ones."""
    raw = open(path, "rb").read()
    if os.sep + "COMPRESSED" + os.sep in path or "/COMPRESSED/" in path:
        try:
            o = zlib.decompressobj(-15)
            data = o.decompress(raw) + o.flush()
            if o.eof and not o.unused_data:
                return data
        except zlib.error:
            pass
    return raw


class Entry:
    __slots__ = ("index", "pos", "type", "offset", "size", "pakname", "fullname", "name", "parent", "flags")

    def abs_offset(self):
        return self.pos + self.offset


class Pak:
    def __init__(self, header, data, path=""):
        self.path = path
        self.header = header
        self.buf = data  # header + pab
        self.entries = []
        self.ended = False
        pos = 0
        i = 0
        while pos + 32 <= len(header):
            v = struct.unpack_from(">8I", header, pos)
            e = Entry()
            e.index, e.pos = i, pos
            (e.type, e.offset, e.size, e.pakname, e.fullname, e.name, e.parent, e.flags) = v
            if e.type in (KEY_LAST, KEY_LAST2):
                self.ended = True
                self.last = e
                break
            self.entries.append(e)
            pos += 32
            i += 1

    @staticmethod
    def open(path):
        header = read_disc_file(path)
        data = header
        pab = None
        if path.endswith(".pak.xen"):
            pab = path[:-len(".pak.xen")] + ".pab.xen"
        if pab and os.path.exists(pab):
            data = header + read_disc_file(pab)
        return Pak(header, data, path)

    def data(self, e):
        a = e.abs_offset()
        return self.buf[a:a + e.size]

    def check(self):
        """Problems: entries outside the buffer, overlaps, no .last."""
        probs = []
        if not self.ended:
            probs.append("no .last entry")
        spans = []
        for e in self.entries:
            a = e.abs_offset()
            if a < 0 or a + e.size > len(self.buf):
                probs.append("entry %d outside the data (%#x+%#x > %#x)" % (e.index, a, e.size, len(self.buf)))
            spans.append((a, a + e.size, e.index))
        spans.sort()
        for (a0, b0, i0), (a1, b1, i1) in zip(spans, spans[1:]):
            if a1 < b0:
                probs.append("entries %d and %d overlap" % (i0, i1))
        return probs


def all_paks(rip):
    for root, _, files in os.walk(rip):
        for f in sorted(files):
            if f.endswith(".pak.xen"):
                yield os.path.join(root, f)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1]
    names = default_table()
    if cmd == "list":
        p = Pak.open(argv[2])
        for e in p.entries:
            print("%4d %-6s %9d %08x  %-40s parent=%s flags=%x" % (
                e.index, names.get(e.type), e.size, e.abs_offset(), names.get(e.fullname),
                names.get(e.parent) if e.parent else "-", e.flags))
        print("%d entries%s" % (len(p.entries), "" if p.ended else " (NO .last)"))
        return 0
    if cmd == "extract":
        p = Pak.open(argv[2])
        out = argv[3]
        os.makedirs(out, exist_ok=True)
        for e in p.entries:
            n = names.get(e.fullname).replace("\\", "/").lstrip("/")
            ext = names.get(e.type)
            if not n.lower().endswith(ext.lower()):
                n += ext if ext.startswith(".") else "." + ext
            dst = os.path.join(out, n)
            os.makedirs(os.path.dirname(dst) or out, exist_ok=True)
            with open(dst, "wb") as f:
                f.write(p.data(e))
        print("extracted %d files" % len(p.entries))
        return 0
    if cmd == "check":
        n = bad = files = 0
        for path in all_paks(argv[2]):
            p = Pak.open(path)
            n += 1
            files += len(p.entries)
            probs = p.check()
            if probs:
                bad += 1
                print("%s: %s" % (os.path.relpath(path, argv[2]), "; ".join(probs[:3])))
        print("paks: %d, files: %d, with problems: %d" % (n, files, bad))
        return 1 if bad else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
