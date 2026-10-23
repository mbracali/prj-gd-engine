# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Geometry classes of Guitar Hero (PS2) scene records: Mesh, MultiMesh, View,
Morph, MeshAnim.

Each decode_<class>(record_bytes) mirrors the game's Load routine; comments
give the struct offset (in the loaded object) or the reading function. Only
the branches taken by the versions present in the data are decoded; other
versions raise ValueError.
"""
from .base import Reader, transformable, drawable


def animatable(r):
    """RndAnimatable::Load (RndAnimatable_t1_v8 @ 0x1ac0c0), with the entry
    list that base.animatable does not decode yet.

    FUN_002b50f8 (list at +0x18): u32 count, per entry FUN_001ac7b0:
    u32 kind -> factory FUN_001ac640, then the kind's virtual Load:
      0 (vt 0x2f9d58, FUN_002b56d8): 2 x 4 bytes
      1 (vt 0x2f9d10, FUN_002b57d0): 2 x 4 bytes + bool8
      2 (vt 0x2f9cc8, FUN_002b58d8): 2 x 4 bytes
      3 (vt 0x2f9c80, FUN_002b59c0): 2 x 4 bytes
      4 (vt 0x2f9c38, FUN_002b5b00): 3 x 4 bytes
    FUN_002b5188 (list at +0x0c): u32 count + references.
    """
    out = {"version": r.u32()}
    entries = []
    for _ in range(r.u32()):
        kind = r.u32()
        if kind in (0, 2, 3):
            e = {"kind": kind, "a": r.f32(), "b": r.f32()}
        elif kind == 1:
            e = {"kind": kind, "a": r.f32(), "b": r.f32(), "flag": r.bool8()}
        elif kind == 4:
            e = {"kind": kind, "a": r.f32(), "b": r.f32(), "c": r.f32()}
        else:
            raise ValueError(f"animatable entry kind {kind}")
        entries.append(e)
    out["entries"] = entries
    out["anims"] = r.ref_list()
    return out


def _done(r, out):
    if r.remaining():
        raise ValueError(f"{r.remaining()} bytes left at {r.p:#x}")
    return out


# --------------------------------------------------------------------------
# Mesh: PsMesh_v8 @ 0x2e8c68 (RndMesh::Load). Data version is 25 everywhere.

def _vertex(r):
    """FUN_001c5f88 (version >= 0x17): 12 floats, 48 bytes on disk.

    In memory a vertex is 0x40 bytes: +0x00 pos xyz, +0x10 normal xyz,
    +0x20 four floats, +0x30 uv. The +0x20 floats are the bone weights of
    skinned meshes (PsMesh_v8 rewrites them as weights for versions < 0x19;
    they sum to 1 in every mesh with bones) and an RGBA vertex color in
    meshes without bones (mostly (1,1,1,1), alpha 0 or 1; inference from data).
    """
    pos = r.floats(3)       # +0x00 +0x04 +0x08
    normal = r.floats(3)    # +0x10 +0x14 +0x18
    weights = r.floats(4)   # +0x20..+0x2c bone weights 0..3 / RGBA color
    uv = r.floats(2)        # +0x30 +0x34
    return pos, normal, weights, uv


def decode_mesh(data):
    r = Reader(data)
    out = {"version": r.u32()}                     # DAT_003689f8
    v = out["version"]
    if v != 25:
        raise ValueError(f"Mesh version {v} not handled")
    out["trans"] = transformable(r)                # this+0x40
    out["draw"] = drawable(r)                      # this+0x00
    out["mat"] = r.ref()                           # this+0x118 (RndMat)
    out["geom_owner"] = r.ref()                    # this+0x128 (RndMesh sharing geometry; self if own)
    out["mutable"] = r.u32()                       # this+0x134 (v >= 0x10; old bool -> 0x1f)
    out["volume"] = r.u32()                        # this+0x138 (v > 0x11)
    out["bsp"] = _bsp(r)                           # this+0x13c, FUN_00244af8 (v > 0x12)
    # FUN_001c4f68: vertex vector at this+0x100 (u32 count + vertices)
    nv = r.u32()
    verts = [_vertex(r) for _ in range(nv)]
    out["pos"] = [x[0] for x in verts]
    out["normal"] = [x[1] for x in verts]
    out["weights"] = [x[2] for x in verts]     # = vertex color when no bones
    out["uv"] = [x[3] for x in verts]
    # FUN_002be5a8: face vector at this+0x108, 6-byte faces of 3 u16 (FUN_001c6948)
    nf = r.u32()
    out["faces"] = [r.take("3H") for _ in range(nf)]
    # FUN_002be800 (v >= 0x18): list at this+0x120 of u8 per group:
    # number of consecutive faces in each group ("parts")
    out["groups"] = [r.u8() for _ in range(r.u32())]
    # v > 0xd: bones. First ref is the bone-0 transform; if it is set (v >= 0x17)
    # three more refs and four 3x4 matrices (rotation rows + position) follow,
    # stored in the 0x120-byte skin block at this+0x130: refs at +0/+8/+0x10/+0x18,
    # matrices at +0x20/+0x60/+0xa0/+0xe0 (bone offset / inverse-bind matrices).
    bone0 = r.ref()
    out["bones"] = []
    if bone0:
        names = [bone0, r.ref(), r.ref(), r.ref()]
        mats = [r.floats(12) for _ in range(4)]
        out["bones"] = [{"name": n, "offset": m} for n, m in zip(names, mats)]
    # FUN_001c53c8 / FUN_001c5328: precomputed per-group strip data, read only
    # when a renderer flag is set and the mesh owns its geometry and has faces.
    # Detected here by remaining bytes: per group {u32 n, u32 m, n*u32, m*u16}.
    out["group_data"] = []
    if r.remaining():
        for _ in out["groups"]:
            n, m = r.u32(), r.u32()
            out["group_data"].append({"ints": list(r.take(f"{n}I")),
                                      "shorts": list(r.take(f"{m}H"))})
    return _done(r, out)


def _bsp(r):
    """FUN_00244af8: recursive BSP node: bool8 present, then plane (4 f32)
    and two child nodes (+0x10 front, +0x14 back)."""
    if not r.bool8():
        return None
    return {"plane": r.floats(4), "front": _bsp(r), "back": _bsp(r)}


# --------------------------------------------------------------------------
# MultiMesh: PsMultiMesh_v8 @ 0x1cb698 (version 0)

def decode_multimesh(data):
    r = Reader(data)
    out = {"version": r.u32()}                     # DAT_00368aa0
    if out["version"] != 0:
        raise ValueError("MultiMesh version")
    out["draw"] = drawable(r)
    out["mesh"] = r.ref()                          # this+0x40 (RndMesh instanced)
    # FUN_002c22c0: list at this+0x48: u32 count, per instance 12 f32
    # (3x3 rotation rows then position: node +0x10..+0x48)
    out["instances"] = [r.floats(12) for _ in range(r.u32())]
    return _done(r, out)


# --------------------------------------------------------------------------
# View (RndGroup in later engines): RndView_t3_v8 @ 0x2eb3f0 (version 7)

def decode_view(data):
    r = Reader(data)
    out = {"version": r.u32()}
    v = out["version"]
    if v != 7:
        raise ValueError(f"View version {v}")
    out["anim"] = animatable(r)                    # this+0x00
    out["trans"] = transformable(r)                # this+0x70
    out["draw"] = drawable(r)                      # this+0x30 (draws = member objects)
    # v > 3
    out["env"] = r.ref()                           # this+0x130 (class desc 0x2c82e0, probably RndEnviron)
    out["lod_width"] = r.f32()                     # this+0x138 (probably LOD screen size)
    out["lod_height"] = r.f32()                    # this+0x13c
    return _done(r, out)


# --------------------------------------------------------------------------
# Morph: RndMorph_v8 @ 0x1fbe28 (version 3)

def decode_morph(data):
    r = Reader(data)
    out = {"version": r.u32()}                     # DAT_00368fd4
    v = out["version"]
    if v != 3:
        raise ValueError(f"Morph version {v}")
    out["anim"] = animatable(r)
    # FUN_002cd470: pose vector at this+0x24, 0x18 bytes each (FUN_001fbc30):
    # ref mesh (+0), then (v >= 2) FUN_002bbcd8 key vector (+8):
    # u32 count, per key {f32 weight, f32 frame} (FUN_002bbc90)
    poses = []
    for _ in range(r.u32()):
        mesh = r.ref()
        keys = [tuple(r.floats(2)) for _ in range(r.u32())]
        poses.append({"mesh": mesh, "weight_keys": keys})
    out["poses"] = poses
    out["target"] = r.ref()                        # this+0x38 (RndMesh deformed)
    out["normals"] = r.bool8()                     # this+0x40 (v > 0) probably "morph normals"
    out["spline"] = r.bool8()                      # this+0x44 (v > 0) probably spline interpolation
    out["intensity"] = r.f32()                     # this+0x48 (v > 2)
    return _done(r, out)


# --------------------------------------------------------------------------
# MeshAnim: RndMeshAnim_v8 @ 0x1f9940 (version 0)

def _keys(r, width):
    """Key vector (0x14-byte elements): u32 count, per key a vector of
    `width`-float values (u32 count + values) then f32 frame (+0x10)."""
    keys = []
    for _ in range(r.u32()):
        vals = [r.floats(width) for _ in range(r.u32())]
        keys.append({"values": vals, "frame": r.f32()})
    return keys


def decode_meshanim(data):
    r = Reader(data)
    out = {"version": r.u32()}
    if out["version"] != 0:
        raise ValueError("MeshAnim version")
    out["anim"] = animatable(r)
    out["mesh"] = r.ref()                          # this+0x24 (RndMesh animated)
    out["vert_point_keys"] = _keys(r, 3)           # this+0x2c FUN_002ca728 / FUN_002ca5e0 (vec3)
    out["vert_tex_keys"] = _keys(r, 2)             # this+0x3c FUN_002caf20 / FUN_002cade8 (vec2)
    out["vert_color_keys"] = _keys(r, 4)           # this+0x4c FUN_002cb728 / FUN_002cb5d0 (rgba)
    out["keys_owner"] = r.ref()                    # this+0x5c (RndMeshAnim sharing keys)
    return _done(r, out)


DECODERS = {
    "Mesh": decode_mesh,
    "MultiMesh": decode_multimesh,
    "View": decode_view,
    "Morph": decode_morph,
    "MeshAnim": decode_meshanim,
}
