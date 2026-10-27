#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero II (Xbox 360) .dtb scripts -> .dta text. See docs/dtb_format.md.

    dtb.py <file.dtb>                 print the file as .dta text
    dtb.py dump <ark dir> <out dir>   every .dtb under <ark dir> -> <out dir>/<same path>.dta

A .dtb is encrypted with the Park-Miller cipher in tools/ark.py (first 4 bytes
= seed). The plaintext is a serialised array tree, the same layout as GH1:
    u8 1 (a root array follows)
    array: u16 node count, u16 line, u16 id (always 0), then the nodes
    node:  u32 type, then a value that depends on the type (see the type tables below)
"""
import importlib.util
import os
import struct
import sys


def _load_cipher():
    # Loaded by path under its own module name: gd-game-01 also has an ark.py.
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ark.py")
    spec = importlib.util.spec_from_file_location("gh2_ark", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.crypt


crypt = _load_cipher()

# Nested arrays: type -> brackets.
ARRAYS = {0x10: "()", 0x11: "{}", 0x13: "[]"}
# Directives whose value is a length-prefixed name.
NAMED = {0x07: "#ifdef", 0x20: "#define", 0x21: "#include", 0x22: "#merge",
         0x23: "#ifndef", 0x24: "#autorun", 0x25: "#undef"}
# Directives whose value is an unused u32 (always 0).
BARE = {0x08: "#else", 0x09: "#endif"}


class Directive(str):
    """A directive line: never folded onto one line with its neighbours."""


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

    def array(self):
        count, _line, _id = self.take("<HHH")
        return [self.node() for _ in range(count)]

    def node(self):
        t = self.take("<I")
        if t == 0x00:
            return str(self.take("<i"))
        if t == 0x01:
            return fmt_float(self.take("<f"))
        if t == 0x02:
            return "$" + self.text()
        if t == 0x05:
            return quote_symbol(self.text())
        if t == 0x12:
            return '"' + self.text().replace('"', "\\q") + '"'
        if t in ARRAYS:
            return (ARRAYS[t], self.array())
        if t in NAMED:
            return Directive(NAMED[t] + " " + self.text())
        if t in BARE:
            self.take("<I")
            return Directive(BARE[t])
        if t == 0x06:
            self.take("<I")
            return "kDataUnhandled"
        raise ValueError(f"unknown node type {t:#x} at {self.pos - 4:#x}")


def fmt_float(f):
    s = repr(f)
    return s if ("." in s or "e" in s or "n" in s) else s + ".0"


def quote_symbol(s):
    ok = "_-./!?%*+<>=&|^~:"
    return s if s and all(c.isalnum() or c in ok for c in s) else "'" + s + "'"


def one_line(node):
    """The node on one line, or None (directives inside, or too long)."""
    if isinstance(node, Directive):
        return None
    if isinstance(node, str):
        return node
    head, children = node
    parts = [one_line(c) for c in children]
    if None in parts:
        return None
    s = head[0] + " ".join(parts) + head[1]
    return s if len(s) <= 76 else None


def render(node, depth=0):
    pad = "   " * depth
    line = one_line(node)
    if line is not None:
        return pad + line
    if isinstance(node, str):
        return pad + node
    head, children = node
    first = ""
    if children and isinstance(children[0], str) and not isinstance(children[0], Directive):
        first, children = children[0], children[1:]  # keep the key on the bracket line
    return pad + head[0] + first + "\n" + render_list(children, depth + 1) + "\n" + pad + head[1]


def render_list(nodes, depth):
    """One node per line; a #define and its value (the next node) share a line."""
    lines, i = [], 0
    while i < len(nodes):
        line = render(nodes[i], depth)
        if isinstance(nodes[i], Directive) and nodes[i].startswith("#define "):
            if i + 1 < len(nodes):
                line += " " + render(nodes[i + 1], depth).lstrip()
                i += 1
        lines.append(line)
        i += 1
    return "\n".join(lines)


def parse(data):
    """Encrypted .dtb bytes -> list of root nodes. Every byte must be consumed."""
    plain = crypt(data)
    r = Reader(plain)
    if r.take("<B") != 1:
        raise ValueError("no root array")
    root = r.array()
    if r.pos != len(plain):
        raise ValueError(f"parsed {r.pos} of {len(plain)} bytes")
    return root


def to_dta(data):
    return render_list(parse(data), 0) + "\n"


def dump(ark_dir, out_dir):
    ok = bad = 0
    for base, _dirs, files in os.walk(ark_dir):
        for name in sorted(files):
            if not name.lower().endswith(".dtb"):
                continue
            src = os.path.join(base, name)
            rel = os.path.relpath(src, ark_dir)
            dst = os.path.join(out_dir, rel[:-4] + ".dta")
            try:
                text = to_dta(open(src, "rb").read())
            except (ValueError, struct.error) as e:
                print(f"{rel}: {e}", file=sys.stderr)
                bad += 1
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "w", encoding="utf-8", newline="\n") as f:  # strings are Latin-1 in the .dtb
                f.write(text)
            ok += 1
    print(f"{ok} .dtb parsed exactly and written, {bad} failed")
    return bad == 0


if __name__ == "__main__":
    if len(sys.argv) == 2:
        sys.stdout.write(to_dta(open(sys.argv[1], "rb").read()))
    elif len(sys.argv) == 4 and sys.argv[1] == "dump":
        sys.exit(0 if dump(sys.argv[2], sys.argv[3]) else 1)
    else:
        raise SystemExit(__doc__)
