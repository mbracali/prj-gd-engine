#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Find code in SLUS_212.24 that builds given addresses with lui/addiu (or
lui/ori, or a load/store offset), even where Ghidra did not disassemble it.

Usage: addr_refs.py <elf> <functions.tsv> <hexaddr>...
Prints: target, instruction address, containing function (if known).
"""
import bisect
import struct
import sys

TEXT_ADDR, TEXT_OFF, TEXT_SIZE = 0x100000, 0x1000, 0x1E4A18


def main():
    elf, ftsv, targets = sys.argv[1], sys.argv[2], [int(a, 16) for a in sys.argv[3:]]
    data = open(elf, "rb").read()
    words = struct.unpack_from(f"<{TEXT_SIZE // 4}I", data, TEXT_OFF)
    funcs = []
    for line in open(ftsv).read().splitlines()[1:]:
        entry, name, size = line.split("\t")[:3]
        funcs.append((int(entry, 16), int(size), name))
    funcs.sort()
    starts = [f[0] for f in funcs]

    def owner(addr):
        i = bisect.bisect_right(starts, addr) - 1
        if i >= 0 and addr < funcs[i][0] + funcs[i][1] * 1:
            return funcs[i][2]
        return f"(no function; nearest start {funcs[i][2]})" if i >= 0 else "?"

    for target in targets:
        hi = (target + 0x8000) >> 16 & 0xFFFF
        lo = target & 0xFFFF
        for i, w in enumerate(words):
            op = w >> 26
            if op not in (0x09, 0x0D, 0x23, 0x2B, 0x31, 0x39, 0x1E, 0x1F, 0x37, 0x3F) or w & 0xFFFF != lo:
                continue
            rs = w >> 21 & 31
            # A matching lui into the same register within the previous 12 instructions.
            for j in range(max(0, i - 12), i):
                v = words[j]
                if v >> 26 == 0x0F and v & 0xFFFF == hi and (v >> 16 & 31) == rs:
                    addr = TEXT_ADDR + i * 4
                    print(f"{target:#x}\t{addr:#x}\t{owner(addr)}")
                    break


if __name__ == "__main__":
    main()
