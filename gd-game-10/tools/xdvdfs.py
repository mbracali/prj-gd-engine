#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Lists or extracts the game partition of an Xbox 360 disc image (XGD2/XGD3,
the Xbox DVD file system, "XDVDFS").

    xdvdfs.py list    <disc.iso>
    xdvdfs.py extract <disc.iso> <out dir>

XDVDFS: 2048-byte sectors. At sector 32 of the game partition, the volume
descriptor: "MICROSOFT*XBOX*MEDIA", u32 root directory sector, u32 root
directory size (little-endian). A directory is a binary tree of entries
(u16 left offset, u16 right offset in dwords; u32 first sector; u32 size;
u8 attributes; u8 name length; name), 4-byte aligned, never crossing a
sector; offset 0xffff fills the rest of a sector.
"""
import os
import struct
import sys

SECTOR = 2048
# Where the game partition starts in the image, by disc type.
PARTITIONS = [0x0, 0xFD90000, 0x2080000, 0x18300000]
MAGIC = b"MICROSOFT*XBOX*MEDIA"


def find_partition(f):
    for base in PARTITIONS:
        f.seek(base + 32 * SECTOR)
        if f.read(20) == MAGIC:
            return base
    sys.exit("xdvdfs.py: no Xbox file system found")


def walk(f, base, sector, size, path=""):
    """Yields (path, first sector, size, is_dir) for a directory tree."""
    f.seek(base + sector * SECTOR)
    data = f.read(size)
    seen = set()
    stack = [0]
    while stack:
        off = stack.pop()
        if off in seen or off + 14 > len(data):
            continue
        seen.add(off)
        left, right, first, length, attr, nlen = struct.unpack_from("<HHIIBB", data, off)
        if left == 0xFFFF and right == 0xFFFF:
            continue
        name = data[off + 14: off + 14 + nlen].decode("latin-1")
        full = f"{path}/{name}" if path else name
        is_dir = bool(attr & 0x10)
        yield full, first, length, is_dir
        if is_dir and length:
            pos = f.tell()
            yield from walk(f, base, first, length, full)
            f.seek(pos)
        for child in (left, right):
            if child and child != 0xFFFF:
                stack.append(child * 4)


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("list", "extract"):
        sys.exit(__doc__)
    with open(sys.argv[2], "rb") as f:
        base = find_partition(f)
        f.seek(base + 32 * SECTOR + 20)
        root, root_size = struct.unpack("<II", f.read(8))
        entries = list(walk(f, base, root, root_size))
        if sys.argv[1] == "list":
            print(f"game partition at 0x{base:x}; {len(entries)} entries")
            for p, s, n, d in sorted(entries):
                print(f"{'<dir>' if d else n:>12}  {p}")
            return
        out = sys.argv[3]
        total = 0
        for p, s, n, d in entries:
            dst = os.path.join(out, *p.split("/"))
            if d:
                os.makedirs(dst, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            f.seek(base + s * SECTOR)
            with open(dst, "wb") as o:
                left = n
                while left:
                    chunk = f.read(min(left, 1 << 22))
                    if not chunk:
                        break
                    o.write(chunk)
                    left -= len(chunk)
            total += n
        print(f"extracted {sum(1 for e in entries if not e[3])} files, {total / 1e9:.2f} GB, into {out}")


if __name__ == "__main__":
    main()
