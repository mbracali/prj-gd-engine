#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Decrypt Guitar Hero (PS2) .dtb files, as done by the game's BinStream.

Recovered from SLUS_212.24:
  FUN_0022af80  reads a 4-byte seed (unencrypted), then attaches the cipher
  FUN_0024a028  seeds 256 words with the C rand() LCG; indices start at 0, 103
  FUN_0024a140  next(): x[a] ^= x[b]; a, b advance and wrap after 249
  FUN_0022b000  every following byte read is XORed with next() & 0xff

The plaintext is a serialized DataArray: a 0x01 byte, then an array
(u16 node count, u16 line, u16 id) whose nodes are a u32 type plus a value.

Usage:
  dtb.py decrypt <in.dtb> <out.bin>
  dtb.py dta     <in.dtb>            print as .dta text
"""
import struct
import sys


class Cipher:
    def __init__(self, seed):
        self.table = []
        for _ in range(256):
            a = (seed * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
            seed = (a * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
            self.table.append((a >> 16) | (seed & 0x7FFF0000))
        self.a, self.b = 0, 0x67

    def next(self):
        t = self.table
        v = t[self.a] ^ t[self.b]
        t[self.a] = v
        self.a = 0 if self.a + 1 > 0xF8 else self.a + 1
        self.b = 0 if self.b + 1 > 0xF8 else self.b + 1
        return v


def decrypt(data):
    (seed,) = struct.unpack_from("<I", data, 0)
    c = Cipher(seed)
    return bytes(b ^ (c.next() & 0xFF) for b in data[4:])


# Node type -> (brackets for nested arrays) or directive keyword.
ARRAYS = {0x10: "()", 0x11: "{}", 0x13: "[]"}
DIRECTIVES = {0x20: "#define", 0x21: "#include", 0x22: "#merge", 0x23: "#ifndef",
              0x07: "#ifdef", 0x08: "#else", 0x09: "#endif"}


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, fmt):
        vals = struct.unpack_from(fmt, self.data, self.pos)
        self.pos += struct.calcsize(fmt)
        return vals if len(vals) > 1 else vals[0]

    def text(self):
        n = self.take("<I")
        s = self.data[self.pos:self.pos + n].decode("latin-1")
        self.pos += n
        return s

    def array(self, depth):
        count, _line, _id = self.take("<HHH")
        return [self.node(depth) for _ in range(count)]

    def node(self, depth):
        t = self.take("<I")
        if t == 0x00:
            return str(self.take("<i"))
        if t == 0x01:
            return repr(self.take("<f"))
        if t == 0x05:
            return quote_symbol(self.text())
        if t == 0x12:
            return '"' + self.text().replace('"', '\\q') + '"'
        if t == 0x02:
            return "$" + self.text()
        if t in ARRAYS:
            return (ARRAYS[t], self.array(depth + 1))
        if t in (0x08, 0x09):
            self.take("<I")
            return DIRECTIVES[t]
        if t == 0x06:
            self.take("<I")
            return "kDataUnhandled"
        if t in DIRECTIVES:
            return DIRECTIVES[t] + " " + self.text()
        raise ValueError(f"unknown node type {t:#x} at {self.pos - 4:#x}")


def quote_symbol(s):
    return s if s and all(c.isalnum() or c in "_-./!?%*+<>=&|^~:" for c in s) else "'" + s + "'"


def render(node, depth=0):
    pad = "   " * depth
    if isinstance(node, str):
        return pad + node
    head, children = node
    flat = [c for c in children if isinstance(c, str)]
    if len(flat) == len(children) and sum(map(len, flat)) < 70:
        return pad + head[0] + " ".join(flat) + head[1]
    inner = "\n".join(render(c, depth + 1) for c in children)
    return pad + head[0] + "\n" + inner + "\n" + pad + head[1]


def to_dta(data):
    plain = decrypt(data)
    r = Reader(plain)
    if r.take("<B") != 1:
        raise ValueError("no root array")
    root = r.array(0)
    if r.pos != len(plain):
        raise ValueError(f"parsed {r.pos} of {len(plain)} bytes")
    return "\n".join(render(n) for n in root) + "\n"


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "decrypt":
        open(sys.argv[3], "wb").write(decrypt(open(sys.argv[2], "rb").read()))
    elif len(sys.argv) == 3 and sys.argv[1] == "dta":
        sys.stdout.write(to_dta(open(sys.argv[2], "rb").read()))
    else:
        raise SystemExit(__doc__)
