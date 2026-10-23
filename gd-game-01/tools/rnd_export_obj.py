#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Export every Mesh of a .rnd_ps2 scene to Wavefront OBJ, in world space.

Usage: rnd_export_obj.py <scene.rnd_ps2> <out.obj> [--no-lod]

Transform convention (verified: child.world == child.local * parent.world
exactly for the bone hierarchy in charsys/metal): the 12 floats are three
rotation/scale rows then the position, applied to row vectors:
    p_world = p.x * row0 + p.y * row1 + p.z * row2 + pos
Vertices are stored in the mesh's local space; the world matrix stored in the
file is used directly. Scenes are Z-up.
Skinned meshes (Mesh "bones" set) are posed like the game skins them:
    p_world = sum_i weight_i * (p * offset_i * bone_i.world)
(offset_i = the mesh's per-bone 3x4 matrix). For the mesh itself this gives
the same result as its own world matrix when its world is the bind pose, and
puts the split body pieces of characters (world = identity) onto the skeleton.
Meshes whose geometry is owned by another mesh (geom_owner) have no vertices
of their own; they are exported with the owner's geometry and their own matrix.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rnd  # noqa: E402
from rnd_classes.geometry import decode_mesh  # noqa: E402


def xform(m, p):
    return (p[0] * m[0] + p[1] * m[3] + p[2] * m[6] + m[9],
            p[0] * m[1] + p[1] * m[4] + p[2] * m[7] + m[10],
            p[0] * m[2] + p[1] * m[5] + p[2] * m[8] + m[11])


def xform_dir(m, n):
    return (n[0] * m[0] + n[1] * m[3] + n[2] * m[6],
            n[0] * m[1] + n[1] * m[4] + n[2] * m[7],
            n[0] * m[2] + n[1] * m[5] + n[2] * m[8])


def load_meshes(path, skip_lod=False):
    """Yields (name, world_positions, world_normals, uvs, faces, material)."""
    _, records = rnd.parse(open(path, "rb").read())
    meshes = {n: decode_mesh(b) for t, n, b in records if t == "Mesh"}
    for name, m in meshes.items():
        if skip_lod and "lod" in name.lower():
            continue
        geo = m
        if not m["faces"] and m["geom_owner"] in meshes and m["geom_owner"] != name:
            geo = meshes[m["geom_owner"]]
        if not geo["faces"] or not m["draw"]["showing"]:
            continue
        w = m["trans"]["world"]
        bones = [(b["offset"], meshes[b["name"]]["trans"]["world"])
                 if b["name"] in meshes else None for b in geo["bones"]]
        if bones and all(b or not g["name"] for b, g in zip(bones, geo["bones"])):
            pos, nrm = [], []
            for p, n, wt in zip(geo["pos"], geo["normal"], geo["weights"]):
                ap, an = [0.0] * 3, [0.0] * 3
                for bone, k in zip(bones, wt):
                    if k and bone:
                        off, bw = bone
                        q = xform(bw, xform(off, p))
                        d = xform_dir(bw, xform_dir(off, n))
                        ap = [a + k * c for a, c in zip(ap, q)]
                        an = [a + k * c for a, c in zip(an, d)]
                pos.append(tuple(ap))
                nrm.append(tuple(an))
        else:
            pos = [xform(w, p) for p in geo["pos"]]
            nrm = [xform_dir(w, n) for n in geo["normal"]]
        yield name, pos, nrm, geo["uv"], geo["faces"], m["mat"]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        raise SystemExit(__doc__)
    base = 1
    with open(args[1], "w") as f:
        for name, pos, nrm, uv, faces, mat in load_meshes(args[0], "--no-lod" in sys.argv):
            f.write(f"o {name}\nusemtl {mat or 'none'}\n")
            for p in pos:
                f.write("v %.5f %.5f %.5f\n" % p)
            for n in nrm:
                f.write("vn %.5f %.5f %.5f\n" % n)
            for t in uv:
                f.write("vt %.5f %.5f\n" % (t[0], 1.0 - t[1]))
            for a, b, c in faces:
                a, b, c = a + base, b + base, c + base
                f.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")
            base += len(pos)


if __name__ == "__main__":
    main()
