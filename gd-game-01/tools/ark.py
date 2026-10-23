#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""List or extract files from a Guitar Hero (PS2) version-3 ARK archive.

Usage:
  ark.py list    <MAIN.HDR>
  ark.py extract <MAIN.HDR> <out_dir> [substring]
"""
import os
import struct
import sys


def read_hdr(path):
    data = open(path, "rb").read()
    pos = 0

    def u32():
        nonlocal pos
        (v,) = struct.unpack_from("<I", data, pos)
        pos += 4
        return v

    version = u32()
    if version != 3:
        raise SystemExit(f"unsupported ARK version {version}")
    u32()  # archive part count; repeated as the length of the size list
    ark_sizes = [u32() for _ in range(u32())]
    table_size = u32()
    table = data[pos:pos + table_size]
    pos += table_size
    string_offsets = [u32() for _ in range(u32())]

    def name(index):
        start = string_offsets[index]
        return table[start:table.index(b"\0", start)].decode("latin-1")

    entries = []
    for _ in range(u32()):
        offset, name_idx, dir_idx, size, inflated = struct.unpack_from("<5I", data, pos)
        pos += 20
        entries.append((f"{name(dir_idx)}/{name(name_idx)}", offset, size, inflated))
    if pos != len(data):
        raise SystemExit(f"parsed {pos} of {len(data)} header bytes; layout mismatch")
    return ark_sizes, entries


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("list", "extract"):
        raise SystemExit(__doc__)
    hdr = sys.argv[2]
    ark_sizes, entries = read_hdr(hdr)
    if sys.argv[1] == "list":
        for path, offset, size, inflated in entries:
            print(f"{offset:#012x}\t{size}\t{inflated}\t{path}")
        print(f"# {len(entries)} files, ark sizes {ark_sizes}", file=sys.stderr)
        return

    out_dir, needle = sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else ""
    ark_path = os.path.join(os.path.dirname(hdr), "MAIN_0.ARK")
    with open(ark_path, "rb") as ark:
        for path, offset, size, _ in entries:
            if needle not in path:
                continue
            # Paths may contain "../"; keep everything inside out_dir.
            dest = os.path.normpath(os.path.join(out_dir, path.replace("../", "_up_/")))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            ark.seek(offset)
            with open(dest, "wb") as f:
                f.write(ark.read(size))


if __name__ == "__main__":
    main()
