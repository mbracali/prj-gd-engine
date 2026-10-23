# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Decoders for the animation classes of Guitar Hero (PS2) scene records.

Each decode_<class>(record_bytes) mirrors the class's Load routine (the
`*_v8` vtable slot) and returns a dict; field names come from the matching
Print routines (`*_v3` slot) where those exist. Offsets in comments are
object-field offsets inside the in-memory instance.

Keys. All key lists are STL vectors with 32-byte elements, serialised as
u32 count followed by the element's fields. The key time is a float *frame*
(Print writes "(frame: ... value: ...)").

Time. RndAnimatable::SetFrame (FUN_001abab8(frame, blend, anim)) stores the
frame, runs it through the anim's filters (FUN_001ac468) and calls the
class's SetFrame, then forwards the filtered frame to the child anims. Frames
have no fixed unit; the caller picks it:
  * venues: VenueSection (FUN_00178db8 / FUN_00178ce8, polled from the
    arena's FUN_001691a0) loads "<section>.view", driven with the song
    position in MIDI ticks (DAT_0036379c; 480 per beat, FUN_00174ad0 divides
    by 480 for beats), and "<section>_rt.view", driven with song time in ms
    (DAT_00363798). Venue keys sit on tick values (479, 959, 3840 = 2 bars)
    and MinMaxLoop(0, 3840, loop) filters loop them per bar.
  * UI / HUD: ms * 0.03, i.e. 30 frames per second (e.g. FUN_00136e50).
  * AnimateTaskPeriod / AnimateTaskFPS (FUN_001ee988 / FUN_001ee9a0):
    frame = start + (end - start) * clamp((t - t0) / (t1 - t0)); the FPS
    variant sets t1 = t0 + |end - start| / fps (fps in frames per ms, 0.03).

  vec3 key   FUN_002bd618 / FUN_002bd5b0   f32 x, y, z, f32 frame
  quat key   FUN_002c56b8 / FUN_002c5640   f32 x, y, z, w, f32 frame
  color key  FUN_002b72f8 / FUN_002b7280   f32 r, g, b, a, f32 frame
  float key  FUN_002bbcd8 / FUN_002bbc90   f32 value, f32 frame
  vec2 key   FUN_002b7848 / FUN_002b77f0   f32 x, y, f32 frame
  obj key    FUN_002bd7b0 / FUN_002bd6f8   ref object, f32 frame

Only the paths for the versions present in the data are guaranteed; other
version branches are mirrored from the decompilation but untested. Branches
that the game reads into temporaries and discards are kept as "_legacy_*".
"""
from .base import Reader, animatable, drawable, transformable


# ---- key lists -------------------------------------------------------------

def _keys(r, nvals):
    out = []
    for _ in range(r.u32()):
        vals = r.floats(nvals)
        out.append({"value": vals if nvals > 1 else vals[0], "frame": r.f32()})
    return out


def vec3_keys(r):   # FUN_002bd618
    return _keys(r, 3)


def quat_keys(r):   # FUN_002c56b8
    return _keys(r, 4)


def color_keys(r):  # FUN_002b72f8
    return _keys(r, 4)


def float_keys(r):  # FUN_002bbcd8
    return _keys(r, 1)


def vec2_keys(r):   # FUN_002b7848
    return _keys(r, 2)


def obj_keys(r):    # FUN_002bd7b0
    return [{"value": r.ref(), "frame": r.f32()} for _ in range(r.u32())]


def _check_version(v, maximum, cls):
    # Each Load reads its u32 version and returns at once if it is newer.
    if v > maximum:
        raise ValueError(f"{cls} version {v} > supported {maximum}")


def _finish(r, out):
    if r.remaining():
        raise ValueError(f"{r.remaining()} bytes left after decode")
    return out


# ---- RndTransAnim ----------------------------------------------------------

def decode_transanim(data):
    """RndTransAnim::Load (RndTransAnim_t2_v8 @ 0x1dd538), max version 4.

    Evaluation (FUN_001de038, args frame, blend, xfm): uses the keys of
    framesOwner (+0xb4, itself when empty). Position: FUN_0024d0d8, linear
    lerp, or cubic Hermite when trans_spline (tangents FUN_0024cd68: central
    difference /2; end keys 1.5*d1 - 0.25*d2). Rotation: binary search of the
    bracketing keys, then nlerp (FUN_0024aa38, shortest arc) or slerp
    (FUN_0024ab90) when rot_slerp. Scale: as position with scale_spline.
    Frames before the first / after the last key hold the end key.
    repeat_trans: position keys repeat, each cycle offset by (last - first).
    follow_path: with position keys and no rotation keys, the rotation looks
    along the position-curve tangent.
    """
    r = Reader(data)
    v = r.u32()
    _check_version(v, 4, "TransAnim")
    out = {"version": v}
    out["animatable"] = animatable(r)
    out["drawable"] = drawable(r)           # Drawable base at +0x30
    out["trans"] = r.ref()                  # +0x70 target RndTransformable
    if v != 2:
        out["rot_keys"] = quat_keys(r)      # +0x84
        out["trans_keys"] = vec3_keys(r)    # +0x94
    out["frames_owner"] = r.ref()           # +0xb4 RndTransAnim whose keys are used
    if v < 3:
        # Legacy keys with full 3x3 rotation etc.; read and only partly kept.
        out["_legacy"] = _transanim_legacy(r, v)
    if v < 4:
        out["trans_spline"] = r.u32() != 0  # +0x78
    else:
        out["trans_spline"] = r.bool8()     # +0x78
    out["repeat_trans"] = r.bool8()         # +0xbc
    if v >= 4:
        out["scale_keys"] = vec3_keys(r)    # +0xa4
        out["scale_spline"] = r.bool8()     # +0x7c
    elif v >= 1:
        if v != 2:
            out["scale_keys"] = vec3_keys(r)
        if v < 3:
            n = r.u32()
            if v == 2 or n:
                out["_legacy_scale"] = [r.floats(12) + [r.f32()] for _ in range(n)]
        out["scale_spline"] = r.u32() != 0
    if v < 2:
        out["follow_path"] = None           # derived: no rot keys and >=2 trans keys
    else:
        out["follow_path"] = r.bool8()      # +0xc0
    if v > 3:
        out["rot_slerp"] = r.bool8()        # +0x80
    return _finish(r, out)


def _transanim_legacy(r, v):
    # v < 3: u32 count; 12 floats + frame per position key, then rotation keys
    # (15 floats + frame), then a u32. Untested (no such data).
    res = {}
    n = r.u32()
    if v == 2 or n:
        res["trans"] = [r.floats(12) + [r.f32()] for _ in range(n)]
    n = r.u32()
    if v == 2 or n:
        res["rot"] = [r.floats(15) + [r.f32()] for _ in range(n)]
    res["u32"] = r.u32()
    return res


# ---- RndMatAnim ------------------------------------------------------------

def _matanim_stage(r, v):
    """One RndMatAnim stage (FUN_001c2148), 0x44 bytes in memory."""
    st = {}
    if v < 2:
        st["_legacy_refs"] = r.ref_list()   # FUN_002b5d28, converted to tex keys
    if v > 0:
        st["trans_keys"] = vec3_keys(r)     # +0x00 texture-transform translation
        st["scale_keys"] = vec3_keys(r)     # +0x10
        st["rot_keys"] = vec3_keys(r)       # +0x20
    if v > 1:
        st["tex_keys"] = obj_keys(r)        # +0x30 (RndTex reference, frame)
    return st


def decode_matanim(data):
    """RndMatAnim::Load (RndMatAnim_v8 @ 0x1c1950), max version 5."""
    r = Reader(data)
    v = r.u32()
    _check_version(v, 5, "MatAnim")
    out = {"version": v}
    out["animatable"] = animatable(r)
    out["mat"] = r.ref()                    # +0x24 RndMat
    out["stages"] = [_matanim_stage(r, v) for _ in range(r.u32())]  # +0x2c, FUN_002bb7f0
    out["keys_owner"] = r.ref()             # +0x40
    if v > 1:
        if v < 5:
            out["_legacy_color_a"] = color_keys(r)
        if v < 3:
            out["_legacy_color_b"] = color_keys(r)
        out["base_keys"] = color_keys(r)    # +0x48 material colour
        if v < 4:
            out["_legacy_color_c"] = color_keys(r)
        out["alpha_keys"] = float_keys(r)   # +0x58
    return _finish(r, out)


# ---- RndLightAnim ----------------------------------------------------------

def decode_lightanim(data):
    """RndLightAnim::Load (RndLightAnim_v8 @ 0x1f8390), max version 1."""
    r = Reader(data)
    v = r.u32()
    _check_version(v, 1, "LightAnim")
    out = {"version": v}
    out["animatable"] = animatable(r)
    out["light"] = r.ref()                  # +0x24 RndLight
    if v < 1:
        out["_legacy_a"] = color_keys(r)
    out["color_keys"] = color_keys(r)       # +0x2c
    if v < 1:
        out["_legacy_b"] = color_keys(r)
    out["keys_owner"] = r.ref()             # +0x3c
    return _finish(r, out)


# ---- RndEnvAnim ------------------------------------------------------------

def decode_envanim(data):
    """RndEnvAnim::Load (RndEnvAnim_v8 @ 0x1b5be8), max version 3."""
    r = Reader(data)
    v = r.u32()
    _check_version(v, 3, "EnvAnim")
    out = {"version": v}
    out["animatable"] = animatable(r)
    out["environ"] = r.ref()                # +0x24 RndEnviron
    out["ambient_color_keys"] = color_keys(r)  # +0x4c
    out["keys_owner"] = r.ref()             # +0x5c
    if v > 1:
        out["fog_color_keys"] = color_keys(r)  # +0x2c
    if v > 2:
        out["fog_range_keys"] = vec2_keys(r)   # +0x3c (fog start, end)
    return _finish(r, out)


# ---- RndCamAnim ------------------------------------------------------------

def decode_camanim(data):
    """RndCamAnim::Load (RndCamAnim_v8 @ 0x1f15b0), max version 0."""
    r = Reader(data)
    v = r.u32()
    _check_version(v, 0, "CamAnim")
    out = {"version": v}
    out["animatable"] = animatable(r)
    out["cam"] = r.ref()                    # +0x24 RndCam
    out["fov_keys"] = float_keys(r)         # +0x2c
    out["keys_owner"] = r.ref()             # +0x3c
    return _finish(r, out)


# ---- RndParticleSysAnim ----------------------------------------------------

def decode_particlesysanim(data):
    """RndParticleSysAnim::Load (RndParticleSysAnim_v8 @ 0x1fe528), max version 2."""
    r = Reader(data)
    v = r.u32()
    _check_version(v, 2, "ParticleSysAnim")
    out = {"version": v}
    out["animatable"] = animatable(r)
    out["particle_sys"] = r.ref()           # +0x24
    out["start_color_keys"] = color_keys(r)  # +0x2c
    out["end_color_keys"] = color_keys(r)    # +0x3c
    if v < 2:
        # float keys converted to vec2 (value, value * scale)
        fk = float_keys(r)
        out["keys_owner"] = r.ref()         # +0x8c
        scale = r.f32() if v == 1 else 1.0
        out["emit_rate_keys"] = [{"value": [k["value"], k["value"] * scale],
                                  "frame": k["frame"]} for k in fk]
    else:
        out["emit_rate_keys"] = vec2_keys(r)  # +0x4c (low, high)
        out["keys_owner"] = r.ref()         # +0x8c
    if v > 1:
        out["speed_keys"] = vec2_keys(r)    # +0x5c
        out["life_keys"] = vec2_keys(r)     # +0x6c
        out["start_size_keys"] = vec2_keys(r)  # +0x7c
    return _finish(r, out)


# ---- RndParticleSys (PsParticleSys) ----------------------------------------

def decode_particlesys(data):
    """RndParticleSys::Load (RndParticleSys_t3_v8 @ 0x1cedc0), max version 22.

    PsParticleSys_v8 @ 0x1a2c20 calls this and then FUN_001a2a20, which only
    builds a VU buffer from the live particles (no stream reads). Vec2 fields
    are (low, high) ranges each particle draws from at emission.
    """
    r = Reader(data)
    v = r.u32()
    _check_version(v, 22, "ParticleSys")
    out = {"version": v}
    if v > 0:
        out["animatable"] = animatable(r)
        out["transformable"] = transformable(r)  # +0x30
        out["drawable"] = drawable(r)            # +0xf0
    out["life"] = r.floats(2)               # +0x158
    out["pos_low"] = r.floats(3)            # +0x160
    out["pos_high"] = r.floats(3)           # +0x170
    out["speed"] = r.floats(2)              # +0x180
    out["pitch"] = r.floats(2)              # +0x188
    out["yaw"] = r.floats(2)                # +0x190
    out["emit_rate"] = r.floats(2)          # +0x198
    out["start_size"] = r.floats(2)         # +0x1a0
    if v > 15:
        out["end_size"] = r.floats(2)       # +0x1a8
    out["start_color_low"] = r.floats(4)    # +0x1b0
    out["start_color_high"] = r.floats(4)   # +0x1c0
    out["end_color_low"] = r.floats(4)      # +0x1d0
    out["end_color_high"] = r.floats(4)     # +0x1e0
    if v < 2:
        out["_legacy_list"] = _legacy_ps_list(r)
    else:
        out["collide"] = r.bool8()          # +0x250
        if v < 12:
            p = r.floats(3)                 # point on plane
            n = r.floats(3)                 # +0x260 normal
            out["collide_plane"] = n + [-(n[0] * p[0] + n[1] * p[1] + n[2] * p[2])]
        else:
            out["collide_plane"] = r.floats(4)  # +0x260 a, b, c, d
    out["force"] = r.floats(3)              # +0x270
    out["mat"] = r.ref()                    # +0x1f8 RndMat
    if v >= 18:
        out["type"] = r.u32()               # +0x144 particle type (SetPool arg)
        out["grow_ratio"] = r.f32()         # +0x284
        out["shrink_ratio"] = r.f32()       # +0x288
        out["midcol_ratio"] = r.f32()       # +0x28c
        out["midcol_low"] = r.floats(4)     # +0x290
        out["midcol_high"] = r.floats(4)    # +0x2a0
    elif v < 13:
        out["_legacy_u32_a"] = r.u32()
    out["max_particles"] = r.u32()          # passed to SetPool (vtable +0x24)
    if 2 < v < 13:
        out["_legacy_u32_b"] = r.u32()
    if v > 3:
        out["bubble_period"] = r.floats(2)  # +0x148
        out["bubble_size"] = r.floats(2)    # +0x150
        out["bubble"] = r.bool8()           # +0x280
    if 5 <= v <= 14:
        out["_legacy_mat_flag"] = r.bool8()  # sets mat +0x48 bit 1
    if 6 <= v <= 16:
        out["_legacy_string"] = r.string()  # FUN_0022ae00
    if v == 8:
        out["_legacy_bool"] = r.bool8()
    if v == 13:
        out["_legacy_u32_c"] = r.u32()
    if v >= 20:
        out["relative_motion"] = r.f32()    # +0x204
    elif v >= 13:
        out["relative_motion"] = 1.0 if r.bool8() else 0.0
    if v > 18:
        out["mesh"] = r.ref()               # +0x1f0 emitter mesh
    if v == 21:
        out["_legacy_u32_d"] = r.u32()
    if v > 10:
        out["preserve_particles"] = r.bool8()  # +0x200
        if out["preserve_particles"]:
            # FUN_001d24e0 per particle (struct offsets in comments; meaning inferred
            # from FUN_001a2a20, which copies +0x00 (4f), +0x20 (3f), +0x60*0.5).
            out["particles"] = [{"pos": r.floats(3),     # +0x20
                                 "color": r.floats(4),   # +0x00
                                 "size": r.f32()}        # +0x60
                                for _ in range(r.i32())]
    return _finish(r, out)


def _legacy_ps_list(r):
    # v < 2: FUN_002c2de8 list (untested; no such data).
    raise NotImplementedError("ParticleSys version < 2 list (FUN_002c2de8)")


DECODERS = {
    "TransAnim": decode_transanim,
    "MatAnim": decode_matanim,
    "LightAnim": decode_lightanim,
    "EnvAnim": decode_envanim,
    "CamAnim": decode_camanim,
    "ParticleSys": decode_particlesys,
    "ParticleSysAnim": decode_particlesysanim,
}
