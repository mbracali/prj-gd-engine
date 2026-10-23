#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Read Guitar Hero (PS2) .rnd_ps2 / .milo scene files (object directories).

Container (recovered by inspection, see FINDINGS.md "Scene files"):
  optional chunked compression: u32 0xCBBEDEAF, u32 header size, u32 block
      count, u32 ?, u32 block sizes[n]; blocks are raw deflate streams
  u32 version (10), u32 object count, then per object: string type, string name
  u32 n, n x string: external file paths (one per Tex/Movie; usually empty,
      e.g. a Movie's "../movies/snow_1c.ipu")
  per object, in table order: the class's serialized data, then 0xADDEADDE
Strings are u32 length + bytes.

Usage: rnd.py list <file>       objects and record sizes
       rnd.py check <dir>...     validate every .rnd_ps2 file below <dir>
"""
import os
import struct
import sys
import zlib

MARKER = b"\xad\xde\xad\xde"


def decompress(data):
    if data[:4] != b"\xaf\xde\xbe\xcb":
        return data
    header, count = struct.unpack_from("<II", data, 4)
    sizes = struct.unpack_from(f"<{count}I", data, 16)
    out, pos = [], header
    for s in sizes:
        out.append(zlib.decompress(data[pos:pos + s], -15))
        pos += s
    return b"".join(out)


class Reader:
    def __init__(self, data, pos=0):
        self.d, self.p = data, pos

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def string(self):
        n = self.u32()
        s = self.d[self.p:self.p + n].decode("latin-1")
        self.p += n
        return s


def parse(data):
    """Returns (version, [(type, name, record_bytes)])."""
    data = decompress(data)
    r = Reader(data)
    version = r.u32()
    entries = [(r.string(), r.string()) for _ in range(r.u32())]
    paths = [r.string() for _ in range(r.u32())]
    records = []
    for i, (t, n) in enumerate(entries):
        end = data.find(MARKER, r.p)
        if end < 0:
            raise ValueError(f"object {i} ({t} {n}) has no end marker")
        records.append((t, n, data[r.p:end]))
        r.p = end + 4
    if r.p != len(data):
        raise ValueError(f"{len(data) - r.p} trailing bytes")
    return version, records


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    if sys.argv[1] == "list":
        version, records = parse(open(sys.argv[2], "rb").read())
        print("version", version)
        for t, n, rec in records:
            print(f"{t:16} {len(rec):7}  {n}")
        return
    ok, bad = 0, []
    for root in sys.argv[2:]:
        for base, _, files in os.walk(root):
            for f in files:
                if not f.endswith(".rnd_ps2"):
                    continue
                path = os.path.join(base, f)
                try:
                    parse(open(path, "rb").read())
                    ok += 1
                except Exception as e:
                    bad.append((path, str(e)))
    print(f"{ok} files parsed")
    for p, e in bad[:20]:
        print("  FAIL", p, e)
    print(f"{len(bad)} failures")


if __name__ == "__main__":
    main()
