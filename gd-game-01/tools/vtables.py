#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Recover GCC 2.x vtables from SLUS_212.24 using the exported Ghidra tables.

Each vtable entry is 8 bytes {s32 this_delta, u32 function}; an object's
vptr points at entry 0, whose function is the class's type-info function.
A call through vptr offset 0x0c + 8k reaches "slot k" (entry k + 1).
Classes with virtual or multiple bases have one vtable per base subobject;
each is listed with its index (0 = the first found in the data section).
Type-info functions are found as the functions that reference an RTTI name
string ("<len><Class>").

Usage: vtables.py <elf> <analysis_dir>
Writes <analysis_dir>/vtables.tsv: class, vptr, table index, slot, method, delta, note.
"""
import csv
import re
import struct
import sys


def load_sections(elf):
    data = open(elf, "rb").read()
    shoff, = struct.unpack_from("<I", data, 0x20)
    shnum, = struct.unpack_from("<H", data, 0x30)
    sections = []
    for i in range(shnum):
        name, typ, flags, addr, off, size = struct.unpack_from("<6I", data, shoff + i * 40)
        if typ == 1 and addr and flags & 2:  # PROGBITS, allocated
            sections.append((addr, off, size, bool(flags & 4)))
    return data, sections


def main():
    elf, adir = sys.argv[1], sys.argv[2]
    data, sections = load_sections(elf)
    funcs = {}
    with open(f"{adir}/functions.tsv") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            funcs[int(row["entry"], 16)] = (row["name"], int(row["size"]))

    by_name = {name: addr for addr, (name, _) in funcs.items()}
    typeinfo = {}
    with open(f"{adir}/strings.tsv") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            m = re.fullmatch(r"(\d+)([A-Z]\w+)", row["value"])
            if not m or int(m.group(1)) != len(m.group(2)) or not row["referenced_from"]:
                continue
            refs = [by_name[n] for n in row["referenced_from"].split(",") if n in by_name]
            if refs:
                typeinfo[min(refs, key=lambda a: funcs[a][1])] = m.group(2)

    text = [(a, n) for a, _, n, x in sections if x]
    in_text = lambda w: w % 4 == 0 and any(a <= w < a + n for a, n in text)

    rows, tables = [], {}
    for addr, off, size, executable in sections:
        if executable:
            continue
        words = struct.unpack_from(f"<{size // 4}i", data, off)
        for i in range(1, len(words)):
            cls = typeinfo.get(words[i] & 0xFFFFFFFF)
            if cls is None or not -0x10000 < words[i - 1] < 0x10000:
                continue
            vptr = addr + (i - 1) * 4
            index = tables.setdefault(cls, 0)
            tables[cls] += 1
            slot = 0
            for j in range(i + 1, len(words) - 1, 2):
                delta, fn = words[j], words[j + 1] & 0xFFFFFFFF
                if not -0x10000 < delta < 0x10000 or not in_text(fn) or fn in typeinfo:
                    break
                rows.append((cls, f"{vptr:08x}", index, slot, f"{fn:08x}", delta,
                             "" if fn in funcs else "not-a-ghidra-function"))
                slot += 1

    with open(f"{adir}/vtables.tsv", "w") as f:
        f.write("class\tvptr\ttable\tslot\tmethod\tdelta\tnote\n")
        for r in rows:
            f.write("\t".join(map(str, r)) + "\n")
    print(f"{len(typeinfo)} type-info functions, {len(tables)} classes, "
          f"{sum(tables.values())} vtables, {len(rows)} slots, "
          f"{sum(1 for r in rows if r[6])} slots outside known functions")


if __name__ == "__main__":
    main()
