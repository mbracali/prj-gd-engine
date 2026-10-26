#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero II (Xbox 360) archive: gen/main.hdr (index) + gen/main_N.ark.

    ark.py list    <disc dir>             every file: size, archive, path
    ark.py extract <disc dir> <out dir> [substring]   files whose path contains it

The header is encrypted with Harmonix's Park-Miller cipher (see crypt()):
u32 seed, then each byte XORed with the low byte of the next key
(key = 16807 * key mod 2^31 - 1, Schrage's method). Decrypted, version 3:
    u32 version (3)
    u32 archive count; u32 size count, then that many u32 archive sizes
    u32 string table size, the string table (NUL-terminated names)
    u32 name count, then that many u32 offsets into the string table
    u32 entry count, then entries:
        u32 offset (in the archive set, as if the archives were one file),
        u32 name index (file name), u32 name index (directory),
        u32 size, u32 inflated size (0: stored as is)
"""
import os
import struct
import sys


def crypt(buf):
    """Harmonix Park-Miller XOR cipher: buf[0:4] is the seed (key)."""
    key = struct.unpack_from("<i", buf, 0)[0]
    out = bytearray(len(buf) - 4)
    for i, b in enumerate(buf[4:]):
        key = (key % 0x1F31D) * 0x41A7 - (key // 0x1F31D) * 0xB14
        if key <= 0:
            key += 0x7FFFFFFF
        out[i] = b ^ (key & 0xFF)
    return bytes(out)


class Reader:
    def __init__(self, data):
        self.d, self.o = data, 0

    def u32(self):
        (v,) = struct.unpack_from("<I", self.d, self.o)
        self.o += 4
        return v


def read_index(disc):
    raw = open(os.path.join(disc, "gen", "main.hdr"), "rb").read()
    r = Reader(crypt(raw))
    version = r.u32()
    if version != 3:
        sys.exit(f"ark.py: header version {version}, expected 3")
    r.u32()  # archive count
    sizes = [r.u32() for _ in range(r.u32())]
    table_size = r.u32()
    table = r.d[r.o: r.o + table_size]
    r.o += table_size
    offsets = [r.u32() for _ in range(r.u32())]

    def name(i):
        start = offsets[i]
        return table[start: table.index(b"\0", start)].decode("latin-1")

    files = []
    for _ in range(r.u32()):
        off, fname, dname, size, inflated = (r.u32() for _ in range(5))
        d = name(dname)
        files.append((off, f"{d}/{name(fname)}" if d and d != "." else name(fname), size, inflated))
    return sizes, files


def locate(sizes, off):
    """(archive index, offset inside it) of an offset in the archive set."""
    for i, s in enumerate(sizes):
        if off < s:
            return i, off
        off -= s
    raise ValueError("offset past the archives")


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("list", "extract"):
        sys.exit(__doc__)
    disc = sys.argv[2]
    sizes, files = read_index(disc)
    if sys.argv[1] == "list":
        print(f"{len(files)} files in {len(sizes)} archive(s)")
        for off, path, size, inflated in sorted(files, key=lambda f: f[1]):
            print(f"{size:>11}{' z' + str(inflated) if inflated else ''}  {path}")
        return
    out, sub = sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else ""
    handles, n = {}, 0
    for off, path, size, inflated in files:
        if sub not in path:
            continue
        i, at = locate(sizes, off)
        h = handles.setdefault(i, open(os.path.join(disc, "gen", f"main_{i}.ark"), "rb"))
        h.seek(at)
        # Paths like "../../system/run/..." stay inside the output folder.
        parts = [x for x in path.split("/") if x not in ("", ".", "..")]
        dst = os.path.join(out, *parts)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as o:
            o.write(h.read(size))
        n += 1
    print(f"extracted {n} files into {out}")


if __name__ == "__main__":
    main()
