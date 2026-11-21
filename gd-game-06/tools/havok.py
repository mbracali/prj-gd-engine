#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero III (Xbox 360) Havok 4.6.0-r1 packfiles: collision (.hkc) and
ragdolls (.rag), decoded through the reflection they carry in __types__.
Format notes: docs/formats.md ("Havok packfiles").

    havok.py check  <rip dir>                 decode every .hkc / .rag; every pointer
                                               fixup of __data__ used by a member
    havok.py dump   <file.pak.xen> <name>     the object tree (name: full / short name)
    havok.py types  <file.pak.xen> <name>     the classes of its __types__ section
    havok.py rag    <file.pak.xen> <name>     ragdoll summary: bodies, shapes, constraints

A packfile is a 0x40-byte header, a 0x30-byte record per section
(__classnames__, __data__, __types__), then the sections. Each section's
data is followed by its fixup tables: local (u32 src, u32 dst: a pointer
inside the section), global (src, dst section, dst: a pointer into another
section) and virtual (src, 0, class name offset: an object of that class
starts at src). Big-endian, 32-bit pointers (stored as 0, set by the fixups).

__types__ holds hkClass objects (0x30 bytes: name, parent, object size,
interface count, enums, enum count, members, member count, defaults, 3 words
0) with their hkClassMember arrays (0x14 bytes: name, class, enum, u8 type,
u8 subtype, s16 C array size, u16 flags, u16 offset) and hkClassEnum objects
(0x0C: name, items, item count; items {s32 value, name}). __data__'s
objects are laid out by those classes; the contents object is an
hkPhysicsData.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pak import Pak, all_paks  # noqa: E402
from qbkey import qbkey, default_table  # noqa: E402

NONE = 0xFFFFFFFF
MAGIC = bytes.fromhex("57e0e05710c0c010")
KINDS = {qbkey(".hkc"): "hkc", qbkey(".rag"): "rag"}

# hkClassMember::Type (Havok 4.6): name, size in bytes (None: from the
# subtype / class)
T = ["VOID", "BOOL", "CHAR", "INT8", "UINT8", "INT16", "UINT16", "INT32", "UINT32", "INT64", "UINT64", "REAL",
     "VECTOR4", "QUATERNION", "MATRIX3", "ROTATION", "QSTRANSFORM", "MATRIX4", "TRANSFORM", "ZERO", "POINTER",
     "FUNCTIONPOINTER", "ARRAY", "INPLACEARRAY", "ENUM", "STRUCT", "SIMPLEARRAY", "HOMOGENEOUSARRAY", "VARIANT",
     "CSTRING", "ULONG", "FLAGS"]
SCALAR = {"BOOL": ">B", "CHAR": ">b", "INT8": ">b", "UINT8": ">B", "INT16": ">h", "UINT16": ">H", "INT32": ">i",
          "UINT32": ">I", "INT64": ">q", "UINT64": ">Q", "REAL": ">f", "ULONG": ">I"}
FIXED = {"BOOL": 1, "CHAR": 1, "INT8": 1, "UINT8": 1, "INT16": 2, "UINT16": 2, "INT32": 4, "UINT32": 4, "INT64": 8,
         "UINT64": 8, "REAL": 4, "VECTOR4": 16, "QUATERNION": 16, "MATRIX3": 48, "ROTATION": 48, "QSTRANSFORM": 48,
         "MATRIX4": 64, "TRANSFORM": 64, "POINTER": 4, "FUNCTIONPOINTER": 4, "ARRAY": 12, "SIMPLEARRAY": 8,
         "HOMOGENEOUSARRAY": 12, "VARIANT": 8, "CSTRING": 4, "ULONG": 4}
FLOATS = {"VECTOR4": 4, "QUATERNION": 4, "MATRIX3": 12, "ROTATION": 12, "QSTRANSFORM": 12, "MATRIX4": 16,
          "TRANSFORM": 16}
ENUM_SIZE = {8: 1, 16: 2, 32: 4}  # member flags ENUM_8 / ENUM_16 / ENUM_32 give the storage


class HavokError(Exception):
    pass


def need(c, what):
    if not c:
        raise HavokError(what)


class Member:
    __slots__ = ("name", "cls", "enum", "type", "subtype", "carray", "flags", "offset")


class Klass:
    __slots__ = ("name", "parent", "size", "interfaces", "enums", "members", "at")

    def all_members(self):
        return (self.parent.all_members() if self.parent else []) + self.members


class Packfile:
    def __init__(self, d):
        self.d = d
        need(len(d) >= 0x40 and d[:8] == MAGIC, "no Havok packfile magic")
        tag, ver = struct.unpack_from(">II", d, 8)
        need(tag == 0x786E6B68 and ver == 4 and d[16:20] == bytes([4, 0, 0, 1]), "packfile header")
        need(d[40:55] == b"Havok-4.6.0-r1\0", "packfile version")
        nsec, self.csec, self.coff, cnsec, self.cnoff = struct.unpack_from(">5I", d, 20)
        need(nsec == 3 and self.csec == 1 and cnsec == 0, "section indices")
        self.secs = []
        for k in range(nsec):
            o = 0x40 + 0x30 * k
            name = d[o:o + 19].rstrip(b"\0").decode("latin-1")
            v = struct.unpack_from(">7I", d, o + 20)
            self.secs.append({"name": name, "start": v[0], "size": v[1], "local": (v[1], v[2]),
                              "global": (v[2], v[3]), "virtual": (v[3], v[4])})
        need([s["name"] for s in self.secs] == ["__classnames__", "__data__", "__types__"], "section names")
        self.ptr = {}   # (section, offset) -> (section, offset)
        self.obj = {}   # (section, offset) -> class name
        cb = self.secs[0]["start"]
        for si, s in enumerate(self.secs):
            b = s["start"]
            for kind, w in (("local", 2), ("global", 3), ("virtual", 3)):
                lo, hi = s[kind]
                n = (hi - lo) // 4
                ents = struct.unpack_from(">%dI" % n, d, b + lo)
                for i in range(0, n - n % w, w):
                    r = ents[i:i + w]
                    if r[0] == NONE:
                        continue
                    if kind == "local":
                        self.ptr[(si, r[0])] = (si, r[1])
                    elif kind == "global":
                        self.ptr[(si, r[0])] = (r[1], r[2])
                    else:
                        z = d.index(b"\0", cb + r[2])
                        self.obj[(si, r[0])] = d[cb + r[2]:z].decode("latin-1")
        self.used = set()  # pointer fixups a member read
        self.classes = self._read_types()

    # --- raw access
    def at(self, p, n):
        sec, o = p
        s = self.secs[sec]
        need(0 <= o and o + n <= s["size"], "read past section %s (%#x+%d)" % (s["name"], o, n))
        b = s["start"] + o
        return self.d[b:b + n]

    def u32(self, p):
        return struct.unpack(">I", self.at(p, 4))[0]

    def pointer(self, p):
        """The target of the pointer stored at p, or None (stored 0, no fixup)."""
        t = self.ptr.get(p)
        if t is None:
            need(self.u32(p) == 0, "pointer without fixup at %s:%#x" % p)
            return None
        need(self.u32(p) == 0, "pointer with a stored value at %s:%#x" % p)
        self.used.add(p)
        return t

    def cstr(self, p):
        sec, o = p
        b = self.secs[sec]["start"] + o
        z = self.d.index(b"\0", b, self.secs[sec]["start"] + self.secs[sec]["size"])
        return self.d[b:z].decode("latin-1")

    # --- reflection
    def _read_types(self):
        T_SEC = 2
        raw = {}
        enums = {}

        def enum_at(p):  # an hkClassEnum (a class's enum table holds them inline)
            if p not in enums:
                name = self.cstr(self.pointer(p))
                items_p = self.pointer((p[0], p[1] + 4))
                n = struct.unpack(">i", self.at((p[0], p[1] + 8), 4))[0]
                items = {}
                for i in range(n):
                    q = (items_p[0], items_p[1] + 8 * i)
                    items[struct.unpack(">i", self.at(q, 4))[0]] = self.cstr(self.pointer((q[0], q[1] + 4)))
                enums[p] = (name, items)
            return enums[p]

        for (si, o), cname in sorted(self.obj.items()):
            if si != T_SEC:
                continue
            p = (si, o)
            if cname == "hkClassEnum":
                enum_at(p)
            else:
                need(cname == "hkClass", "__types__ object %s" % cname)
                raw[(si, o)] = p
        classes = {}
        byaddr = {}
        for p in raw:
            k = Klass()
            k.at = p
            k.name = self.cstr(self.pointer(p))
            w = struct.unpack(">12I", self.at(p, 0x30))
            k.size, k.interfaces = w[2], w[3]
            need(w[5] == 0 or self.ptr.get((p[0], p[1] + 0x10)), "enum table")
            need(w[8] == 0 and w[9] == 0 and w[10] == 0 and w[11] == 0, "hkClass tail words of %s" % k.name)
            byaddr[p] = k
            classes[k.name] = k
        for p, k in byaddr.items():
            pp = self.pointer((p[0], p[1] + 4))
            k.parent = byaddr[pp] if pp else None
            ep = self.pointer((p[0], p[1] + 0x10))
            ne = struct.unpack(">i", self.at((p[0], p[1] + 0x14), 4))[0]
            k.enums = []
            for i in range(ne):
                need(ep is not None, "enum pointer")
                k.enums.append(enum_at((ep[0], ep[1] + 0x0C * i)))
            mp = self.pointer((p[0], p[1] + 0x18))
            nm = struct.unpack(">i", self.at((p[0], p[1] + 0x1C), 4))[0]
            self.pointer((p[0], p[1] + 0x20))  # defaults: none in GH3's files
            k.members = []
            for i in range(nm):
                q = (mp[0], mp[1] + 0x14 * i)
                m = Member()
                m.name = self.cstr(self.pointer(q))
                cp = self.pointer((q[0], q[1] + 4))
                m.cls = byaddr[cp] if cp else None
                enp = self.pointer((q[0], q[1] + 8))
                m.enum = enum_at(enp) if enp else None
                t, st, m.carray, m.flags, m.offset = struct.unpack(">BBhHH", self.at((q[0], q[1] + 12), 8))
                need(t < len(T) and st < len(T), "member type %d/%d" % (t, st))
                m.type, m.subtype = T[t], T[st]
                k.members.append(m)
        return classes

    def msize(self, m):
        """Bytes of one element of member m (before its C array size)."""
        if m.type in ("ENUM", "FLAGS"):
            return ENUM_SIZE[m.flags & 0x38]
        if m.type == "STRUCT":
            return m.cls.size
        if m.type == "ZERO":
            return FIXED.get(m.subtype) or (m.cls.size if m.cls else 0)
        need(m.type in FIXED, "member %s type %s" % (m.name, m.type))
        return FIXED[m.type]

    # --- values
    def value(self, p, t, m, depth):
        """One element of type t (the member m's type or subtype) at p."""
        if t in SCALAR:
            v = struct.unpack(SCALAR[t], self.at(p, struct.calcsize(SCALAR[t])))[0]
            return bool(v) if t == "BOOL" else v
        if t in FLOATS:
            return struct.unpack(">%df" % FLOATS[t], self.at(p, 4 * FLOATS[t]))
        if t in ("ENUM", "FLAGS"):
            n = ENUM_SIZE[m.flags & 0x38]
            v = int.from_bytes(self.at(p, n), "big")
            if t == "ENUM" and m.enum:
                return m.enum[1].get(v, v)
            return v
        if t == "STRUCT":
            return self.read(p, m.cls, depth)
        if t == "CSTRING":
            q = self.pointer(p)
            return self.cstr(q) if q else None
        if t == "POINTER":
            q = self.pointer(p)
            if q is None:
                return None
            need(m.cls is not None or self.obj.get(q), "untyped pointer %s" % m.name)
            return self.ref(q, m.cls, depth)
        if t == "FUNCTIONPOINTER":
            return self.u32(p)
        if t == "VARIANT":
            q = self.pointer(p)
            self.pointer((p[0], p[1] + 4))
            return self.ref(q, None, depth) if q else None
        raise HavokError("value of type %s (%s)" % (t, m.name))

    def ref(self, q, cls, depth):
        """A pointed-to object: virtual objects by their own class, others by cls."""
        name = self.obj.get(q)
        k = self.classes.get(name) if name else cls
        need(k is not None, "object of unknown class at %s:%#x" % q)
        if depth <= 0:
            return "<%s @%#x>" % (k.name, q[1])
        key = (q, k.name)
        if key in self.cache:
            return self.cache[key]
        r = {"__class__": k.name, "__at__": q[1]}
        self.cache[key] = r
        r.update(self.read(q, k, depth - 1))
        return r

    def read(self, p, k, depth=64):
        """The members of class k (its parents' first) at p, as a dict."""
        out = {}
        for m in k.all_members():
            q = (p[0], p[1] + m.offset)
            n = m.carray or 1
            if m.type in ("ARRAY", "SIMPLEARRAY"):
                dp = self.pointer(q)
                cnt = struct.unpack(">i", self.at((q[0], q[1] + 4), 4))[0]
                if m.type == "ARRAY":
                    cap = struct.unpack(">I", self.at((q[0], q[1] + 8), 4))[0]
                    need(cap & 0x3FFFFFFF in (0, cnt) and cap & 0x80000000, "array capacity of %s" % m.name)
                need((dp is None) == (cnt == 0), "array %s pointer vs count" % m.name)
                es = self.msize_of(m.subtype, m)
                v = [self.value((dp[0], dp[1] + i * es), m.subtype, m, depth) for i in range(cnt)] if cnt else []
            elif m.type == "HOMOGENEOUSARRAY":
                raise HavokError("homogeneous array %s" % m.name)
            elif m.type == "ZERO":
                v = None
            else:
                es = self.msize(m)
                vals = [self.value((q[0], q[1] + i * es), m.type, m, depth) for i in range(n)]
                v = vals if m.carray else vals[0]
            out[m.name] = v
        return out

    def msize_of(self, t, m):
        if t == "STRUCT":
            return m.cls.size
        if t in ("ENUM", "FLAGS"):
            return ENUM_SIZE[m.flags & 0x38]
        need(t in FIXED, "array of %s (%s)" % (t, m.name))
        return FIXED[t]

    def contents(self):
        self.cache = {}
        name = self.cstr((0, self.cnoff))
        need(self.obj.get((self.csec, self.coff)) == name, "contents object")
        root = self.ref((self.csec, self.coff), None, 64)
        # every object of __data__ reachable, every pointer fixup used
        reached = {q for (q, _) in self.cache}
        objs = {q for q in self.obj if q[0] == 1}
        need(objs <= reached, "%d objects not reached" % len(objs - reached))
        data_ptrs = {q for q in self.ptr if q[0] == 1}
        need(data_ptrs <= self.used, "%d pointer fixups not used by a member" % len(data_ptrs - self.used))
        for q in objs:  # each object's class size fits before the next one
            k = self.classes[self.obj[q]]
            need(q[1] + k.size <= self.secs[1]["size"], "object past the section")
        return root


def objects_of(root, cls):
    """Every object of class cls reachable from root, once each."""
    seen, out, stack = set(), [], [root]
    while stack:
        v = stack.pop()
        if isinstance(v, dict):
            if id(v) in seen:
                continue
            seen.add(id(v))
            if v.get("__class__") == cls:
                out.append(v)
            stack.extend(v.values())
        elif isinstance(v, (list, tuple)):
            stack.extend(v)
    out.sort(key=lambda o: o["__at__"])
    return out


def shape_desc(s):
    if s is None:
        return "-"
    c = s["__class__"]
    if c == "hkCapsuleShape":
        a, b = s["vertexA"][:3], s["vertexB"][:3]
        return "capsule r=%.3f A=(%.3f %.3f %.3f) B=(%.3f %.3f %.3f)" % ((s["radius"],) + a + b)
    if c == "hkBoxShape":
        return "box half=(%.3f %.3f %.3f) r=%.3f" % (tuple(s["halfExtents"][:3]) + (s["radius"],))
    if c == "hkConvexTranslateShape":
        return "translate (%.3f %.3f %.3f) of %s" % (tuple(s["translation"][:3]) + (shape_desc(s["childShape"]["childShape"]),))
    return c


def ragdoll_summary(root):
    lines = []
    for sysi, ps in enumerate(root["systems"]):
        lines.append("system %d %r: %d bodies, %d constraints, active %s" % (
            sysi, ps.get("name"), len(ps["rigidBodies"]), len(ps["constraints"]), ps.get("active")))
        for b in ps["rigidBodies"]:
            mo = b["motion"]
            ms = mo["motionState"]
            t = ms["transform"]
            lines.append("  body %-24s mass %.3f pos (%.3f %.3f %.3f) %s" % (
                b.get("name"), 1.0 / mo["inertiaAndMassInv"][3] if mo["inertiaAndMassInv"][3] else 0.0,
                t[12], t[13], t[14], shape_desc(b["collidable"]["shape"])))
        for c in ps["constraints"]:
            ents = [e.get("name") if isinstance(e, dict) else e for e in c["entities"]]
            data = c["data"]
            lines.append("  constraint %-24s %s %s -> %s" % (c.get("name"), data["__class__"], ents[0], ents[1]))
    return lines


# --- commands

def find(path, name):
    names = default_table()
    p = Pak.open(path)
    for e in p.entries:
        if e.type in KINDS and name.lower() in (names.get(e.fullname).lower(), names.get(e.name).lower()):
            return p.data(e)
    raise SystemExit("no .hkc / .rag %r in %s" % (name, path))


def cmd_check(rip):
    names = default_table()
    n = {"hkc": 0, "rag": 0}
    bad = 0
    bodies = cons = 0
    for path in all_paks(rip):
        p = Pak.open(path)
        for e in p.entries:
            k = KINDS.get(e.type)
            if not k:
                continue
            try:
                pf = Packfile(p.data(e))
                root = pf.contents()
                n[k] += 1
                bodies += len(objects_of(root, "hkRigidBody"))
                cons += len(objects_of(root, "hkConstraintInstance"))
            except (HavokError, KeyError, struct.error, ValueError) as ex:
                bad += 1
                print("FAIL %s %s: %s" % (os.path.basename(path), names.get(e.fullname), ex))
    print("havok: hkc %d, rag %d, rigid bodies %d, constraints %d, with problems: %d" % (n["hkc"], n["rag"], bodies,
                                                                                         cons, bad))
    return 1 if bad else 0


def show(v, ind=0, out=None, seen=None):
    pad = "  " * ind
    if isinstance(v, dict):
        if id(v) in seen:
            out.append(pad + "<%s @%#x (above)>" % (v.get("__class__"), v.get("__at__", 0)))
            return
        seen.add(id(v))
        for k, x in v.items():
            if isinstance(x, (dict, list)) and x:
                out.append(pad + "%s:" % k)
                show(x, ind + 1, out, seen)
            else:
                out.append(pad + "%s: %s" % (k, fmt(x)))
    else:
        for i, x in enumerate(v):
            if isinstance(x, (dict, list)) and x:
                out.append(pad + "[%d]" % i)
                show(x, ind + 1, out, seen)
            else:
                out.append(pad + "[%d] %s" % (i, fmt(x)))


def fmt(x):
    if isinstance(x, tuple):
        return "(" + " ".join("%.4g" % f for f in x) + ")"
    if isinstance(x, float):
        return "%.6g" % x
    return repr(x)


def main(argv):
    if len(argv) >= 3 and argv[1] == "check":
        return cmd_check(argv[2])
    if len(argv) >= 4 and argv[1] in ("dump", "types", "rag"):
        pf = Packfile(find(argv[2], argv[3]))
        if argv[1] == "types":
            for k in sorted(pf.classes.values(), key=lambda k: k.at[1]):
                print("%s : %s  size %#x" % (k.name, k.parent.name if k.parent else "-", k.size))
                for m in k.members:
                    print("    +%#05x %-28s %s%s%s%s" % (m.offset, m.name, m.type,
                                                       "/" + m.subtype if m.subtype != "VOID" else "",
                                                       " " + m.cls.name if m.cls else "",
                                                       "[%d]" % m.carray if m.carray else ""))
            return 0
        root = pf.contents()
        if argv[1] == "rag":
            print("\n".join(ragdoll_summary(root)))
            return 0
        out = []
        show(root, 0, out, set())
        print("\n".join(out))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
