#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero III (Xbox 360) compiled QScript files (.qb, inside the paks).

    qb.py dump  <file.pak.xen> [qb name substring]   print the QB items
    qb.py check <rip dir>                            decode every .qb in every pak;
                                                     every byte must be accounted for

Layout (big-endian; see docs/formats.md, "QB"):
  header   u32 0, u32 file size, 20 fixed bytes (1c 08 02 04 10 04 08 0c 0c 08 02 04
           14 02 04 0c 10 10 0c 00).
  section  u32 type 0x0020TT00, u32 name key, u32 file key, u32 value or offset, u32 0.
  array    u32 type 0x0001TT00, u32 count, u32 offset (the values, or for a single
           simple value the value itself... see decode_array).
  struct   u32 0x00000100, u32 offset of the first item (0 = empty).
  item     u32 type 0x00TT0000 (TT = 0x80 | type), u32 name key (0 = unnamed),
           u32 value or offset, u32 offset of the next item (0 = last).
  types    1 int, 2 float, 3 string, 4 wide string, 5 pair, 6 vector, 7 script,
           0xa struct, 0xc array, 0xd key, 0x1a key pointer, 0x1b string pointer.
  script   u32 checksum, u32 size, u32 packed size, the bytes (LZSS when the sizes
           differ), padded to 4.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pak  # noqa: E402
from qbkey import qbkey, default_table  # noqa: E402

T_INT, T_FLOAT, T_STRING, T_WSTRING, T_PAIR, T_VECTOR, T_SCRIPT = 1, 2, 3, 4, 5, 6, 7
T_STRUCT, T_ARRAY, T_KEY = 0xA, 0xC, 0xD
T_KEYPTR, T_STRPTR = 0x1A, 0x1B
SIMPLE = {T_INT, T_FLOAT, T_KEY, T_KEYPTR, T_STRPTR}
HEADER_MAGIC = bytes.fromhex("1c0802041004080c0c0802041402040c10100c00")


class QbError(Exception):
    pass


class Script:
    def __init__(self, crc, code):
        self.crc, self.code = crc, code

    def __repr__(self):
        return "<script %d bytes>" % len(self.code)


class Key(int):
    pass


class KeyPtr(int):
    pass


class StrPtr(int):
    pass


def lzss(src, size):
    """Neversoft's LZSS: 4 KiB ring buffer filled with spaces (0x20, as the XEX 0x823071B0), writes start at
    4078, one flag byte per 8 tokens (LSB first, 1 = literal), a reference is
    two bytes: position = b0 | (b1 & 0xf0) << 4, length = (b1 & 0x0f) + 3. [I]
    (checked by the decoded sizes over every script; confirm in the XEX)."""
    ring = bytearray(b" " * 4096)
    r = 4078
    out = bytearray()
    i = 0
    flags = 0
    n = len(src)
    while len(out) < size and i < n:
        flags >>= 1
        if not flags & 0x100:
            flags = src[i] | 0xFF00
            i += 1
            if i >= n:
                break
        if flags & 1:
            c = src[i]
            i += 1
            out.append(c)
            ring[r] = c
            r = (r + 1) & 4095
        else:
            if i + 1 >= n:
                break
            b0, b1 = src[i], src[i + 1]
            i += 2
            pos = b0 | ((b1 & 0xF0) << 4)
            ln = (b1 & 0x0F) + 3
            for k in range(ln):
                c = ring[(pos + k) & 4095]
                out.append(c)
                ring[r] = c
                r = (r + 1) & 4095
    return bytes(out[:size]), i


class Reader:
    def __init__(self, data):
        self.d = data
        self.used = bytearray(len(data))

    def u32(self, o):
        if o + 4 > len(self.d):
            raise QbError("read past the end at %#x" % o)
        self.used[o:o + 4] = b"\x01" * 4
        return struct.unpack_from(">I", self.d, o)[0]

    def f32(self, o):
        self.u32(o)
        return struct.unpack_from(">f", self.d, o)[0]

    def mark(self, o, n):
        if o + n > len(self.d):
            raise QbError("data past the end at %#x+%d" % (o, n))
        self.used[o:o + n] = b"\x01" * n

    def cstring(self, o):
        e = self.d.index(b"\0", o)
        self.mark(o, e + 1 - o)
        self.pad(e + 1)
        return self.d[o:e].decode("latin-1")

    def wstring(self, o):
        e = o
        while self.d[e:e + 2] != b"\0\0":
            e += 2
        self.mark(o, e + 2 - o)
        self.pad(e + 2)
        return self.d[o:e].decode("utf-16-be")

    def pad(self, o):
        while o % 4 and o < len(self.d) and self.d[o] == 0:
            self.used[o] = 1
            o += 1

    def value(self, t, v):
        """A value of type t whose u32 field holds v (value or offset)."""
        if t == T_INT:
            return struct.unpack(">i", struct.pack(">I", v))[0]
        if t == T_FLOAT:
            return struct.unpack(">f", struct.pack(">I", v))[0]
        if t == T_KEY:
            return Key(v)
        if t == T_KEYPTR:
            return KeyPtr(v)
        if t == T_STRPTR:
            return StrPtr(v)
        if t == T_STRING:
            return self.cstring(v)
        if t == T_WSTRING:
            return self.wstring(v)
        if t in (T_PAIR, T_VECTOR):
            hdr = self.u32(v)
            n = 2 if t == T_PAIR else 3
            if hdr != 0x00010000:
                raise QbError("pair/vector header %#x at %#x" % (hdr, v))
            return tuple(self.f32(v + 4 + 4 * k) for k in range(n))
        if t == T_STRUCT:
            return self.struct(v)
        if t == T_ARRAY:
            return self.array(v)
        if t == T_SCRIPT:
            crc, size, packed = self.u32(v), self.u32(v + 4), self.u32(v + 8)
            raw = self.d[v + 12:v + 12 + packed]
            self.mark(v + 12, packed)
            self.pad(v + 12 + packed)
            if packed == size:
                code = raw
            else:
                code, _ = lzss(raw, size)
                if len(code) != size:
                    raise QbError("script at %#x unpacks to %d of %d bytes" % (v, len(code), size))
            return Script(crc, code)
        raise QbError("unknown type %#x" % t)

    def struct(self, o):
        hdr = self.u32(o)
        if hdr != 0x100:
            raise QbError("struct header %#x at %#x" % (hdr, o))
        items = []
        nxt = self.u32(o + 4)
        guard = 0
        while nxt:
            t = self.u32(nxt)
            if t & 0xFF00FFFF or not t & 0x00800000:
                raise QbError("struct item type %#x at %#x" % (t, nxt))
            tt = (t >> 16) & 0x7F
            name = self.u32(nxt + 4)
            v = self.u32(nxt + 8)
            following = self.u32(nxt + 12)
            items.append((name, tt, self.value(tt, v)))
            nxt = following
            guard += 1
            if guard > 100000:
                raise QbError("struct loop at %#x" % o)
        return items

    def array(self, o):
        hdr = self.u32(o)
        if hdr & 0xFFFF00FF != 0x00010000:
            raise QbError("array header %#x at %#x" % (hdr, o))
        t = (hdr >> 8) & 0xFF
        if t == 0:  # empty array
            self.u32(o + 4)
            self.u32(o + 8)
            return (t, [])
        count = self.u32(o + 4)
        if count == 0:
            self.u32(o + 8)
            return (t, [])
        if t in SIMPLE:
            if count == 1:
                return (t, [self.value(t, self.u32(o + 8))])
            base = self.u32(o + 8)
            return (t, [self.value(t, self.u32(base + 4 * k)) for k in range(count)])
        # Complex elements: a list of offsets (a single one is stored inline).
        if count == 1:
            return (t, [self.value(t, self.u32(o + 8))])
        base = self.u32(o + 8)
        return (t, [self.value(t, self.u32(base + 4 * k)) for k in range(count)])


def decode(data, filekey=None):
    """Returns [(name key, type, value)] for the root sections."""
    r = Reader(data)
    if len(data) < 28:
        raise QbError("too short")
    r.mark(0, 28)
    if data[8:28] != HEADER_MAGIC:
        raise QbError("header bytes %s" % data[8:28].hex())
    size = struct.unpack_from(">I", data, 4)[0]
    if size != len(data):
        raise QbError("size field %d, file %d" % (size, len(data)))
    o = 28
    out = []
    while o < len(data):
        t = r.u32(o)
        if t & 0xFF0000FF != 0 or (t >> 16) & 0xFF != 0x20:
            raise QbError("section type %#x at %#x" % (t, o))
        tt = (t >> 8) & 0xFF
        name, fk, v, z = r.u32(o + 4), r.u32(o + 8), r.u32(o + 12), r.u32(o + 16)
        if filekey is not None and fk != filekey:
            raise QbError("file key %#x != %#x at %#x" % (fk, filekey, o))
        if z != 0:
            raise QbError("reserved %#x at %#x" % (z, o))
        out.append((name, tt, r.value(tt, v)))
        o += 20
        if tt not in SIMPLE:
            # The data follows the section; the next section starts after it.
            if v != o:
                raise QbError("data of section at %#x is at %#x" % (o - 20, v))
            while o < len(data) and r.used[o]:
                o += 1
    unused = [i for i, u in enumerate(r.used) if not u]
    return out, unused


def fmt(v, names, ind=0):
    sp = "  " * ind
    if isinstance(v, Key):
        return "$" + names.get(v)
    if isinstance(v, KeyPtr):
        return "<" + names.get(v) + ">"
    if isinstance(v, StrPtr):
        return "*" + names.get(v)
    if isinstance(v, list):  # struct
        if not v:
            return "{}"
        lines = ["{"]
        for n, t, x in v:
            lines.append("%s  %s%s" % (sp, (names.get(n) + " = ") if n else "", fmt(x, names, ind + 1)))
        return "\n".join(lines + [sp + "}"])
    if isinstance(v, tuple) and len(v) == 2 and isinstance(v[1], list) and isinstance(v[0], int):
        return "[" + ", ".join(fmt(x, names, ind + 1) for x in v[1]) + "]"
    if isinstance(v, str):
        return repr(v)
    return repr(v)


def qb_entries(p):
    k = qbkey(".qb")
    for e in p.entries:
        if e.type in (k, qbkey(".mqb"), qbkey(".sqb")):
            yield e


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    names = default_table()
    if argv[1] == "dump":
        p = pak.Pak.open(argv[2])
        sub = argv[3].lower() if len(argv) > 3 else ""
        for e in qb_entries(p):
            n = names.get(e.fullname)
            if sub and sub not in n.lower():
                continue
            print("== %s" % n)
            items, unused = decode(p.data(e), e.fullname)
            for name, t, v in items:
                print("%s = %s" % (names.get(name), fmt(v, names)))
            if unused:
                print("!! %d unused bytes, first at %#x" % (len(unused), unused[0]))
        return 0
    if argv[1] == "check":
        files = bad = sections = scripts = 0
        for path in pak.all_paks(argv[2]):
            p = pak.Pak.open(path)
            for e in qb_entries(p):
                files += 1
                try:
                    items, unused = decode(p.data(e), e.fullname)
                    sections += len(items)
                    scripts += sum(1 for _, t, _ in items if t == T_SCRIPT)
                    if unused:
                        raise QbError("%d unused bytes, first at %#x" % (len(unused), unused[0]))
                except (QbError, ValueError, IndexError) as ex:
                    bad += 1
                    if bad <= 20:
                        print("%s %s: %s" % (os.path.basename(path), names.get(e.fullname), ex))
        print("qb files: %d, sections: %d, scripts: %d, with problems: %d" % (files, sections, scripts, bad))
        return 1 if bad else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
