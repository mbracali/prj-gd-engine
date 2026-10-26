#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero II (Xbox 360) scene files: .milo_xbox object directories.

    milo.py list   <file>             class, name and record size of every object
    milo.py unpack <file> <out dir>   decompressed stream + one blob per object
    milo.py scan   <dir>              parse every .milo_xbox below <dir>: totals

Container (all little-endian, recovered by inspection; see
docs/milo_and_textures.md):
    u32 0xCBBEDEAF  (bytes AF DE BE CB)
    u32 header size (offset of the first block; 0x810 in every file)
    u32 block count
    u32 largest inflated block size
    u32 compressed size of each block, then zero padding up to header size
    blocks: raw deflate streams (no zlib header); concatenated, they give the
    directory stream below.

Directory stream (version 25 in every file):
    u32 version (25)
    string directory class (ObjectDir, RndDir, PanelDir, Character, ...)
    string directory name
    u32 string-table count, u32 string-table size (allocation hints)
    u32 object count, then per object: string class, string name
    the directory object's own data, then 0xADDEADDE
    per object, in table order: its data, then 0xADDEADDE
Strings are u32 length + bytes.

Inline sub-directories: an object that is itself a directory (RndDir,
Character, ...) whose proxy is stored inline carries, inside its own data, a
complete directory header (version 25, class, name, counts, object table).
That sub-directory's own data and its objects follow (each with its own
0xADDEADDE) before the outer object's remaining data and end marker.
"""
import os
import re
import struct
import sys
import zlib

MAGIC_DEFLATE = 0xCBBEDEAF   # deflate blocks (the only kind in GH2's data)
MAGIC_STORED = 0xCABEDEAF    # same table, blocks stored as is (older games)
MARKER = b"\xad\xde\xad\xde"
VERSION = 25
# A string that can be a class name, used to spot inline directory headers.
_CLASS_RE = re.compile(rb"[A-Za-z][A-Za-z0-9_]{1,40}\Z")


def decompress(data):
    """Container -> directory stream. Returns (stream, info dict)."""
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic not in (MAGIC_DEFLATE, MAGIC_STORED):
        return data, {"magic": None}
    header, count, largest = struct.unpack_from("<III", data, 4)
    sizes = struct.unpack_from(f"<{count}I", data, 16)
    out, pos = [], header
    for s in sizes:
        blk = data[pos:pos + s]
        out.append(zlib.decompress(blk, -15) if magic == MAGIC_DEFLATE else blk)
        pos += s
    if pos != len(data):
        raise ValueError(f"blocks end at {pos:#x}, file is {len(data):#x}")
    if out and max(len(b) for b in out) != largest:
        raise ValueError("largest-block field does not match")
    return b"".join(out), {"magic": magic, "header": header, "blocks": count,
                           "largest": largest}


class Reader:
    def __init__(self, data, pos=0):
        self.d, self.p = data, pos

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def string(self, limit=1 << 20):
        n = self.u32()
        if n > limit or self.p + n > len(self.d):
            raise ValueError(f"bad string length {n} at {self.p - 4:#x}")
        s = self.d[self.p:self.p + n].decode("latin-1")
        self.p += n
        return s


def read_header(d, pos):
    """Directory header at pos -> (dict, position after the object table)."""
    r = Reader(d, pos)
    h = {"version": r.u32()}
    if h["version"] != VERSION:
        raise ValueError(f"directory version {h['version']}, expected {VERSION}")
    h["class"], h["name"] = r.string(), r.string()
    h["string_count"], h["string_size"] = r.u32(), r.u32()
    n = r.u32()
    if n > 100000:
        raise ValueError("implausible object count")
    h["entries"] = [(r.string(256), r.string(1024)) for _ in range(n)]
    return h, r.p


def _inline_header_at(d, q, end):
    """True if a plausible directory header starts at d[q] (before end)."""
    try:
        n = struct.unpack_from("<I", d, q + 4)[0]
        if not 3 <= n <= 40 or not _CLASS_RE.match(d[q + 8:q + 8 + n]):
            return False
        h, after = read_header(d, q)
        return after <= end and all(_CLASS_RE.match(c.encode()) for c, _ in h["entries"])
    except (ValueError, struct.error, UnicodeError):
        return False


def _read_object(d, pos, cls, name, out, depth):
    """One object's data starting at pos, through its end marker.

    Appends (class, name, depth, record) to out; an inline sub-directory found
    inside the record is parsed recursively (its objects are appended at
    depth + 1, before this object). Returns the position after the marker.
    """
    start = pos
    while True:
        end = d.find(MARKER, pos)
        if end < 0:
            raise ValueError(f"{cls} {name}: no end marker")
        q = d.find(struct.pack("<I", VERSION), pos, end)
        while q >= 0 and not _inline_header_at(d, q, end):
            q = d.find(struct.pack("<I", VERSION), q + 1, end)
        if q < 0:
            break
        sub, pos = _read_dir(d, q, out, depth + 1)
        out.append(("(inline)", f"{sub['class']} {sub['name']}", depth + 1, b""))
    out.append((cls, name, depth, d[start:end]))
    return end + 4


def _read_dir(d, pos, out, depth=0):
    h, pos = read_header(d, pos)
    pos = _read_object(d, pos, h["class"], h["name"] + " (directory)", out, depth)
    for cls, name in h["entries"]:
        pos = _read_object(d, pos, cls, name, out, depth)
    return h, pos


def parse(data):
    """File bytes -> (container info, top directory header, objects).

    objects: list of (class, name, depth, record bytes) in stream order; the
    directory's own record comes first and is named '<name> (directory)'.
    Records contain no final end marker. An object holding an inline
    sub-directory has a record spanning that whole sub-directory (header,
    records, their markers); the sub-directory's own records are also listed,
    at depth + 1, just before it, followed by an '(inline)' summary row.
    """
    stream, info = decompress(data)
    out = []
    h, pos = _read_dir(stream, 0, out)
    if pos != len(stream):
        raise ValueError(f"{len(stream) - pos} trailing bytes")
    info["stream"] = len(stream)
    return info, h, out


def objects(data):
    """(class, name, record) of every top-level object (not the directory)."""
    _, h, objs = parse(data)
    return [(c, n, rec) for c, n, dep, rec in objs
            if dep == 0 and not n.endswith(" (directory)")]


def tex_info(rec):
    """Tex (version 10) record -> dict with the inline bitmap offset or None.

    u32 version; 13 bytes common object header; u32 width, u32 height,
    u32 bpp; string source file; f32 (mip bias?); u32 (?); u8 use external
    file; if 0, the bitmap (tex.py) follows.
    """
    r = Reader(rec)
    v = r.u32()
    r.p += 13
    w, h, bpp = r.u32(), r.u32(), r.u32()
    path = r.string()
    bias, kind = struct.unpack_from("<fI", rec, r.p)
    r.p += 8
    flag = rec[r.p]
    r.p += 1
    return {"version": v, "width": w, "height": h, "bpp": bpp, "file": path,
            "bias": bias, "type": kind, "flag": flag,
            "bitmap": r.p if len(rec) - r.p >= 32 else None}


def _safe(name):
    return re.sub(r"[^A-Za-z0-9._ -]", "_", name)


def main():
    a = sys.argv[1:]
    if len(a) >= 2 and a[0] == "list":
        info, h, objs = parse(open(a[1], "rb").read())
        print(f"version {h['version']}  {h['class']} '{h['name']}'  "
              f"{len(h['entries'])} objects  blocks {info.get('blocks')}  "
              f"stream {info['stream']} bytes")
        for cls, name, dep, rec in objs:
            ver = struct.unpack_from("<I", rec)[0] if len(rec) >= 4 else ""
            if cls == "(inline)":
                print(f"{'  ' * dep}-- end of inline directory {name}")
                continue
            print(f"{'  ' * dep}{cls:20} v{ver!s:3} {len(rec):8}  {name}")
    elif len(a) == 3 and a[0] == "unpack":
        data = open(a[1], "rb").read()
        stream, _ = decompress(data)
        _, h, objs = parse(data)
        os.makedirs(a[2], exist_ok=True)
        open(os.path.join(a[2], "_stream.bin"), "wb").write(stream)
        for i, (cls, name, dep, rec) in enumerate(objs):
            if cls == "(inline)":
                continue
            fn = f"{i:04d}_{_safe(cls)}_{_safe(name)}.bin"
            open(os.path.join(a[2], fn), "wb").write(rec)
        print(f"{len(objs)} records written to {a[2]}")
    elif len(a) == 2 and a[0] == "scan":
        scan(a[1])
    else:
        raise SystemExit(__doc__)


def scan(root):
    from collections import Counter, defaultdict
    classes, files_with, versions = Counter(), Counter(), defaultdict(Counter)
    dirs, fails, n, inline = Counter(), [], 0, 0
    for base, _, files in os.walk(root):
        for f in sorted(files):
            if not f.endswith(".milo_xbox"):
                continue
            path = os.path.join(base, f)
            try:
                _, h, objs = parse(open(path, "rb").read())
            except Exception as e:  # report and go on
                fails.append((path, str(e)))
                continue
            n += 1
            dirs[h["class"]] += 1
            seen = set()
            for cls, name, dep, rec in objs:
                if cls == "(inline)":
                    inline += 1
                    continue
                if name.endswith(" (directory)"):
                    cls = cls + " (dir)"
                classes[cls] += 1
                seen.add(cls)
                if len(rec) >= 4:
                    versions[cls][struct.unpack_from("<I", rec)[0]] += 1
            for c in seen:
                files_with[c] += 1
    print(f"{n} files parsed, {len(fails)} failed, {inline} inline sub-directories")
    print("top directory classes:", dict(dirs.most_common()))
    print(f"{'class':24}{'objects':>8}{'files':>7}  versions")
    for c, k in classes.most_common():
        vs = ", ".join(f"{v}" + (f"x{m}" if len(versions[c]) > 1 else "")
                       for v, m in sorted(versions[c].items()))
        print(f"{c:24}{k:8}{files_with[c]:7}  {vs}")
    for p, e in fails[:20]:
        print("FAIL", p, e)


if __name__ == "__main__":
    main()
