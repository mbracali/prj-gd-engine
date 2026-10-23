# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Shared readers and base-class decoders for Guitar Hero (PS2) scene records.

Each decoder takes a Reader positioned at its data and returns a dict, reading
exactly what the game's Load routine reads (function addresses in comments).
Stream primitives (BinStream): FUN_0022b1d0 reads n bytes; FUN_0022b000 reads
raw bytes (used for 1-byte bools); FUN_0022ace0 reads a u32-length string;
FUN_00236c80 reads an object reference, which is the object's name string.
"""
import struct


class Reader:
    def __init__(self, data, pos=0):
        self.d, self.p = data, pos

    def remaining(self):
        return len(self.d) - self.p

    def take(self, fmt):
        vals = struct.unpack_from("<" + fmt, self.d, self.p)
        self.p += struct.calcsize("<" + fmt)
        return vals

    def u32(self):
        return self.take("I")[0]

    def i32(self):
        return self.take("i")[0]

    def f32(self):
        return self.take("f")[0]

    def u16(self):
        return self.take("H")[0]

    def u8(self):
        return self.take("B")[0]

    def bool8(self):
        return self.u8() != 0

    def floats(self, n):
        return list(self.take(f"{n}f"))

    def string(self):
        n = self.u32()
        if n > self.remaining():
            raise ValueError(f"string length {n} past end at {self.p - 4:#x}")
        s = self.d[self.p:self.p + n].decode("latin-1")
        self.p += n
        return s

    def ref(self):
        """Object reference (FUN_00236c80): the referenced object's name."""
        return self.string()

    def ref_list(self):
        """ObjPtrList (FUN_002c4f08 / FUN_002b60d0): u32 count + references."""
        return [self.ref() for _ in range(self.u32())]

    def bytes(self, n):
        b = self.d[self.p:self.p + n]
        if len(b) != n:
            raise ValueError("unexpected end of record")
        self.p += n
        return b


def transformable(r):
    """RndTransformable::Load (RndTransformable_t1_v8 @ 0x2eadc8)."""
    out = {"version": r.u32()}
    v = out["version"]
    # Local then world transform: 3x3 rotation rows + position, as 4 vec3s each.
    out["local"] = r.floats(12)
    out["world"] = r.floats(12)
    out["children"] = r.ref_list()
    if v > 0:
        if v < 3:
            out["constraint"] = r.u32()
        elif v < 6:
            out["constraint"] = r.u32()
        else:
            out["constraint"] = r.u32()
        if v < 7:
            out["origin"] = r.floats(3)
    if 2 <= v <= 4:
        r.u8()
    if 6 <= v <= 7:
        out["trans_spring"] = r.floats(4)
    if v > 5:
        out["target"] = r.ref()
    if v >= 7:
        out["preserve_scale"] = r.bool8()
        out["parent"] = r.ref()
    return out


def drawable(r):
    """RndDrawable::Load (RndDrawable_t1_v8 @ 0x2e89c8)."""
    out = {"version": r.u32(), "showing": r.bool8(), "draws": r.ref_list()}
    if out["version"] > 0:
        out["sphere"] = r.floats(4)  # center xyz, radius
    return out


# RndAnimatable frame filters, created by type id in FUN_001ac640 and loaded
# through vtable slot +0x2c. Each maps the parent frame to this anim's frame
# (forward = vtable +0x0c, inverse = +0x14, used for EndFrame).
ANIM_FILTER_TYPES = {
    0: "ScaleOffset",   # load FUN_002b56d8: f32 scale, f32 offset; f' = f*scale + offset (FUN_002b5648)
    1: "MinMaxLoop",    # load FUN_002b57d0: f32 min, f32 max, bool8 loop; clamp, or wrap into [min,max) (FUN_001abe30)
    2: "ZeroOrder",     # load FUN_002b58d8: f32 level, f32 max_delta
    3: "FirstOrder",    # load FUN_002b59c0: f32 level, f32 ratio
    4: "SecondOrder",   # load FUN_002b5b00: f32 level, f32 spring, f32 damper (velocity state not saved)
}


def anim_filter(r):
    """One RndAnimatable::Filter (FUN_001ac7b0): u32 type, then its fields."""
    t = r.u32()
    name = ANIM_FILTER_TYPES.get(t)
    if name is None:
        raise ValueError(f"unknown RndAnimatable filter type {t}")
    if t == 0:
        f = {"scale": r.f32(), "offset": r.f32()}
    elif t == 1:
        f = {"min": r.f32(), "max": r.f32(), "loop": r.bool8()}
    elif t == 2:
        f = {"level": r.f32(), "max_delta": r.f32()}
    elif t == 3:
        f = {"level": r.f32(), "ratio": r.f32()}
    else:
        f = {"level": r.f32(), "spring": r.f32(), "damper": r.f32()}
    f["type"] = name
    return f


def animatable(r):
    """RndAnimatable::Load (RndAnimatable_t1_v8 @ 0x1ac0c0).

    u32 version (not checked); filter list (FUN_002b50f8: u32 count, then
    FUN_001ac7b0 per filter) stored at +0x18; then the child-anim list
    (FUN_002b5188: u32 count + references) at +0x0c. SetFrame (FUN_001abab8)
    stores the raw frame at +4, the filtered frame at +8, and forwards the
    filtered frame to every child anim.
    """
    out = {"version": r.u32()}
    out["filters"] = [anim_filter(r) for _ in range(r.u32())]
    out["anims"] = r.ref_list()
    return out
