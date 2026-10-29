#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero III (Xbox 360) QScript bytecode: tokenizer and disassembler.

    qbscript.py dis   <file.pak.xen> <script name>     print a script as text
    qbscript.py check <rip dir>                        tokenize every script; every
                                                       byte must be a known token

Scripts are the LZSS-unpacked "script" values of QB files (qb.py). Tokens are
one opcode byte and operands; numbers in the bytecode are little-endian
(unlike the QB file around it). The opcode table below is checked against
every script on the disc (`check`) and, as it is confirmed in the XEX's
interpreter, marked [C] in docs/qscript.md.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pak  # noqa: E402
import qb  # noqa: E402
from qbkey import default_table  # noqa: E402

# opcode: (name, operand kind)
OPS = {
    0x00: ("end", None),
    0x01: ("nl", None),
    0x02: ("nl_line", "u32"),
    0x03: ("{", None),
    0x04: ("}", None),
    0x05: ("[", None),
    0x06: ("]", None),
    0x07: ("=", None),
    0x08: (".", None),
    0x09: (",", None),
    0x0A: ("-", None),
    0x0B: ("+", None),
    0x0C: ("/", None),
    0x0D: ("*", None),
    0x0E: ("(", None),
    0x0F: (")", None),
    0x10: ("debuginfo", None),
    0x11: ("==", None),
    0x12: ("<", None),
    0x13: ("<=", None),
    0x14: (">", None),
    0x15: (">=", None),
    0x16: ("name", "key"),
    0x17: ("int", "i32"),
    0x18: ("hex", "u32"),
    0x19: ("enum", "u32"),
    0x1A: ("float", "f32"),
    0x1B: ("string", "str"),
    0x1C: ("wstring", "wstr"),
    0x1D: ("array", None),
    0x1E: ("vector", "vec3"),
    0x1F: ("pair", "vec2"),
    0x20: ("begin", None),
    0x21: ("repeat", None),
    0x22: ("break", None),
    0x23: ("script", None),
    0x24: ("endscript", None),
    0x25: ("if", None),
    0x26: ("else", None),
    0x27: ("elseif", "u16x2"),
    0x28: ("endif", None),
    0x29: ("return", None),
    0x2A: ("undefined", None),
    0x2B: ("checksumtable", "table"),
    0x2C: ("<...>", None),
    0x2D: ("<arg>", None),
    0x2E: ("jump", "u32"),
    0x2F: ("random", "random"),
    0x30: ("random_range", None),
    0x31: ("at", None),
    0x32: ("or", None),
    0x33: ("and", None),
    0x34: ("xor", None),
    0x35: ("shl", None),
    0x36: ("shr", None),
    0x37: ("random2", "random"),
    0x38: ("random_range2", None),
    0x39: ("not", None),
    0x3A: ("and2", None),
    0x3B: ("or2", None),
    0x3C: ("switch", None),
    0x3D: ("endswitch", None),
    0x3E: ("case", None),
    0x3F: ("default", None),
    0x40: ("random_no_repeat", "random"),
    0x41: ("random_permute", "random"),
    0x42: (":", None),
    0x47: ("fastif", "u16"),
    0x48: ("fastelse", "u16"),
    0x49: ("shortjump", "u16"),
    0x4A: ("struct", "qbstruct"),
    0x4B: ("$", None),
    0x4C: ("wstring2", "wstr"),
    0x4D: ("!=", None),
    0x4E: ("qbstruct2", "qbstruct"),
}


class ScriptError(Exception):
    pass


def tokens(code):
    """Yields (offset, opcode, name, operand)."""
    i = 0
    n = len(code)
    while i < n:
        op = code[i]
        if op not in OPS:
            raise ScriptError("unknown opcode %#04x at %#x" % (op, i))
        name, kind = OPS[op]
        at = i
        i += 1
        val = None
        if kind in ("key", "u32"):
            val = struct.unpack_from("<I", code, i)[0]
            i += 4
        elif kind == "i32":
            val = struct.unpack_from("<i", code, i)[0]
            i += 4
        elif kind == "f32":
            val = struct.unpack_from("<f", code, i)[0]
            i += 4
        elif kind == "vec3":
            val = struct.unpack_from("<3f", code, i)
            i += 12
        elif kind == "vec2":
            val = struct.unpack_from("<2f", code, i)
            i += 8
        elif kind == "u16x2":
            val = struct.unpack_from("<2H", code, i)
            i += 4
        elif kind == "u16":
            val = struct.unpack_from("<H", code, i)[0]
            i += 2
        elif kind == "str":
            ln = struct.unpack_from("<I", code, i)[0]
            i += 4
            val = code[i:i + ln]
            if ln == 0 or val[-1:] != b"\0":
                raise ScriptError("string without NUL at %#x" % at)
            val = val[:-1].decode("latin-1")
            i += ln
        elif kind == "wstr":
            ln = struct.unpack_from("<I", code, i)[0]
            i += 4
            val = code[i:i + ln]
            i += ln
        elif kind == "random":
            cnt = struct.unpack_from("<I", code, i)[0]
            i += 4
            weights = struct.unpack_from("<%dH" % cnt, code, i)
            i += 2 * cnt
            offs = struct.unpack_from("<%di" % cnt, code, i)
            i += 4 * cnt
            val = (weights, offs)
        elif kind == "qbstruct":
            ln = struct.unpack_from("<H", code, i)[0]
            i += 2
            # the struct is aligned to 4 in the script
            while i % 4:
                i += 1
            val = code[i:i + ln]
            i += ln
        elif kind == "table":
            # the rest of the script: a checksum -> name table
            val = code[i:]
            i = n
        if i > n:
            raise ScriptError("operand past the end at %#x" % at)
        yield at, op, name, val


def disassemble(code, names):
    out = []
    line = []
    for at, op, name, val in tokens(code):
        if op in (0x01, 0x02):
            out.append(" ".join(line))
            line = []
            continue
        if op == 0x16:
            line.append(names.get(val))
        elif op in (0x17, 0x1A):
            line.append(repr(val))
        elif op == 0x1B:
            line.append(repr(val))
        elif op in (0x47, 0x48, 0x49):
            line.append("%s(+%d)" % (name, val))
        elif op in (0x4A, 0x4E):
            try:
                r = qb.Reader(val)
                items = r.struct(0)
                line.append(qb.fmt(items, names).replace("\n", " "))
            except Exception:
                line.append("{qbstruct %d bytes}" % len(val))
        elif val is not None:
            line.append("%s:%r" % (name, val))
        else:
            line.append(name)
    if line:
        out.append(" ".join(line))
    return "\n".join(out)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    names = default_table()
    if argv[1] == "dis":
        from qbkey import qbkey
        k = qbkey(argv[3])
        p = pak.Pak.open(argv[2])
        for e in qb.qb_entries(p):
            items, _ = qb.decode(p.data(e), e.fullname)
            for n, t, v in items:
                if n == k and t == qb.T_SCRIPT:
                    print("script %s (%s)" % (names.get(n), names.get(e.fullname)))
                    print(disassemble(v.code, names))
        return 0
    if argv[1] == "check":
        import collections
        count = bad = 0
        ops = collections.Counter()
        for path in pak.all_paks(argv[2]):
            p = pak.Pak.open(path)
            for e in qb.qb_entries(p):
                items, _ = qb.decode(p.data(e), e.fullname)
                for n, t, v in items:
                    if t != qb.T_SCRIPT:
                        continue
                    count += 1
                    try:
                        for at, op, name, val in tokens(v.code):
                            ops[op] += 1
                    except (ScriptError, struct.error) as ex:
                        bad += 1
                        if bad <= 15:
                            print("%s %s: %s" % (names.get(e.fullname), names.get(n), ex))
        print("opcodes:", " ".join("%02x:%d" % (k, ops[k]) for k in sorted(ops)))
        print("scripts: %d, with problems: %d" % (count, bad))
        return 1 if bad else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
