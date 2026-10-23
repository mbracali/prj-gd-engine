# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Decoders for Guitar Hero (PS2) material-side scene records:
Mat, Tex, Environ, Light, Cam, Font, Text, Flare, Movie.

Each decode_<class>(record_bytes) mirrors the class's Load routine (vtable
slot 8) for the versions present in the shipped data, reading exactly the
same fields in the same order.  Field names come from each class's Print
routine (vtable slot 3) and script handler (slot 1).  Object offsets (+0x..)
are offsets in the in-memory object written by the Load routine.
"""
import struct

from .base import Reader, drawable, transformable

# ---------------------------------------------------------------- enums
# RndMat / stage blend: printed by FUN_001bfba0 (jump table @ 0x321fe0).
# These map to PS2 GS ALPHA register settings.
BLEND = {0: "Dest", 1: "Src", 2: "Add (Multiply)", 3: "SrcAlpha (Multiply2)",
         4: "SrcAlphaAdd", 5: "Subtract"}
# zmode: FUN_001bfc70 (jump table @ 0x322030).
ZMODE = {0: "Disable", 1: "Normal", 2: "Transparent", 3: "Force", 4: "Decal"}
# multiPass: FUN_001bfe60.
MULTIPASS = {0: "Multitexture", 1: "MultipassSrc", 2: "MultipassLit"}
# Stage texgen (genMode): FUN_001bfd28 (jump table @ 0x322090).
TEXGEN = {0: "None", 1: "Xfm", 2: "Sphere", 3: "Projected", 4: "XfmOrigin", 5: "Environ"}
# Stage wrap: FUN_001bfdf8.
WRAP = {0: "Clamp", 1: "Repeat"}
# RndTex type: FUN_001d6428.
TEX_TYPE = {1: "Regular", 2: "Rendered", 4: "Movie", 8: "BackBuffer",
            0x18: "FrontBuffer", 0x22: "RenderedNoZ"}
# RndLight type: FUN_001baeb8.
LIGHT_TYPE = {0: "Point", 1: "Directional"}
# RndAnimatable filter types (FUN_001ac640 factory; RTTI names
# RndAnimatable::ScaleOffset/MinMaxLoop/ZeroOrder/FirstOrder/SecondOrder).
ANIM_FILTER = {0: "ScaleOffset", 1: "MinMaxLoop", 2: "ZeroOrder", 3: "FirstOrder", 4: "SecondOrder"}


def text_align(v):
    """RndText align bit flags (FUN_001d9598): 0x10 Top, 0x20 Middle, 0x40 Bottom;
    1 Left, 2 Center, 4 Right."""
    vert = "Top" if v & 0x10 else "Middle" if v & 0x20 else "Bottom" if v & 0x40 else ""
    horz = "Left" if v & 1 else "Center" if v & 2 else "Right" if v & 4 else ""
    return vert + horz


def _check_version(r, maximum, cls):
    v = r.u32()
    if v > maximum:
        raise ValueError(f"{cls} version {v} > supported {maximum}")
    return v


def _finish(r, out):
    if r.remaining():
        raise ValueError(f"{r.remaining()} unread bytes at {r.p:#x}")
    return out


def _string2(r):
    """FUN_0022ae00: String-object variant of the u32-length string (same layout)."""
    return r.string()


def animatable(r):
    """RndAnimatable::Load (RndAnimatable_t1_v8 @ 0x1ac0c0), full version.

    u32 version (unused); filter list (FUN_002b50f8): u32 count, then per filter
    u32 type (FUN_001ac7b0 -> factory FUN_001ac640) + type data (vtable slot 4:
    FUN_002b56d8 / 57d0 / 58d8 / 59c0 / 5b00); then an object list of
    RndAnimatables (FUN_002b5188): u32 count + references.
    """
    out = {"version": r.u32(), "filters": []}
    for _ in range(r.u32()):
        t = r.u32()
        f = {"type": ANIM_FILTER.get(t, t)}
        if t == 1:  # MinMaxLoop: f32, f32, bool loop
            f["a"], f["b"] = r.floats(2)
            f["loop"] = r.bool8()
        elif t == 4:  # SecondOrder: 3 x f32
            f["params"] = r.floats(3)
        elif t in (0, 2, 3):  # ScaleOffset (scale, offset), ZeroOrder, FirstOrder
            f["params"] = r.floats(2)
        else:
            raise ValueError(f"unknown animatable filter type {t}")
        out["filters"].append(f)
    out["anims"] = r.ref_list()
    return out


# ---------------------------------------------------------------- Mat
def _mat_stage(r, v):
    """RndMat::Stage load (FUN_001bf790), 0x60-byte in-memory struct; v >= 20 path."""
    s = {}
    s["blend"] = r.u32()                 # +0x00 (v>=20 raw; older remapped)
    s["texgen"] = r.u32()                # +0x04 genMode (v>=15 raw)
    # +0x10..+0x48: texture transform, 3x3 rotation rows (each padded to 16
    # bytes in memory) then translation: 12 floats.
    s["xfm"] = r.floats(12)
    s["wrap"] = r.u32()                  # +0x50
    s["tex"] = r.ref()                   # +0x54 Tex reference (ObjPtr)
    s["blend_name"] = BLEND.get(s["blend"], s["blend"])
    s["texgen_name"] = TEXGEN.get(s["texgen"], s["texgen"])
    s["wrap_name"] = WRAP.get(s["wrap"], s["wrap"])
    return s


def decode_mat(data):
    """RndMat::Load (RndMat_v8 @ 0x1be900; PsMat_v8 @ 0x19cf00 only adds
    runtime setup).  Max version DAT_002f1c10; data is all version 21."""
    r = Reader(data)
    v = _check_version(r, 21, "Mat")
    if v < 21:
        raise NotImplementedError(f"Mat version {v}")
    out = {"version": v}
    # +0x10 stages: FUN_002ba820, u32 count + Stage records
    out["stages"] = [_mat_stage(r, v) for _ in range(r.u32())]
    out["blend"] = r.u32()               # +0x24
    out["color"] = r.floats(4)           # +0x30 r,g,b,a (alpha +0x3c, script set_alpha)
    out["use_environ"] = r.bool8()       # +0x40 "useEnv" (lit by the Environ's lights)
    out["vert_amb"] = r.bool8()          # +0x54 "vertAmb"
    out["vert_dyn"] = r.bool8()          # +0x58 "vertDyn"
    out["cull"] = r.bool8()              # +0x50 back-face culling (v>=13: raw byte)
    out["multipass"] = r.u32()           # +0x44 (v>=18 raw)
    out["normalize"] = r.bool8()         # +0x4c (v>3)
    out["zmode"] = r.u32()               # +0x48 (v>=15 raw)
    out["alpha_cutout"] = r.bool8()      # +0x5c (v>=17)
    out["alpha_write"] = r.bool8()       # +0x60 (v>=17)
    out["blend_name"] = BLEND.get(out["blend"], out["blend"])
    out["zmode_name"] = ZMODE.get(out["zmode"], out["zmode"])
    out["multipass_name"] = MULTIPASS.get(out["multipass"], out["multipass"])
    return _finish(r, out)


# ---------------------------------------------------------------- Tex / bitmap
def bitmap(r):
    """RndBitmap load (FUN_001b0f10; header FUN_001adc50).

    Header: u8 version; u8 bpp (+6); version 0: u8 order, else u32 order (+8);
    u8 mip count; u16 width, u16 height, u16 bytes-per-line; then 0x13 padding
    bytes (version 0: 6) -> 32-byte header for version 1.
    Palette (FUN_001ae188): none if order == 0x80 or bpp > 8, else
    (order & 0x10 ? 2 : 4) bytes per entry << bpp.  Pixels: bpl * height
    (FUN_001ae178).  Each further mip (FUN_001aea28): w >>= 1, h >>= 1,
    bpl = (w (+1 if odd and 4bpp)) * bpp >> 3; shares the palette.
    """
    start = r.p
    ver = r.u8()
    bpp = r.u8()
    order = r.u32() if ver else r.u8()
    mips = r.u8()
    w, h, bpl = r.take("HHH")
    r.bytes(0x13 if ver else 6)
    pal = 0
    if order != 0x80 and bpp < 9:
        pal = (2 if order & 0x10 else 4) << bpp
    r.bytes(pal)
    r.bytes(bpl * h)
    mw, mh = w, h
    for _ in range(mips):
        mw, mh = mw >> 1, mh >> 1
        mbpl = ((mw + (mw & 1 if bpp == 4 else 0)) * bpp) >> 3
        r.bytes(mbpl * mh)
    return {"version": ver, "bpp": bpp, "order": order, "mips": mips,
            "width": w, "height": h, "bpl": bpl, "offset": start, "size": r.p - start}


def decode_tex(data):
    """RndTex::Load (PsTex_v8 @ 0x1d6788).  Max version DAT_002f1c58; data is v8."""
    r = Reader(data)
    v = _check_version(r, 8, "Tex")
    if v < 8:
        raise NotImplementedError(f"Tex version {v}")
    out = {"version": v}
    out["width"] = r.u32()               # +0x34
    out["height"] = r.u32()              # +0x38
    out["bpp"] = r.u32()                 # +0x3c
    out["file"] = r.string()             # +0x40 FilePath (FUN_0022ace0 -> FUN_00238528)
    out["mip_map_k"] = r.f32()           # +0x2c "mipMapK" (GS mip LOD K; v>=8 float)
    out["type"] = r.u32()                # +0x30 (v>=7 raw)
    out["type_name"] = TEX_TYPE.get(out["type"], out["type"])
    out["unknown_bool"] = r.bool8()      # v>7: one byte read and discarded
    # Inline bitmap: read when the loading dir's "inline textures" flag
    # (DAT_00364490+0xd8, set from the dir by FUN_001bc400) is on; otherwise
    # the texture is loaded from `file` (FUN_001d60f8) or created empty
    # (FUN_001d5d98).  Every shipped Tex record carries one.
    out["bitmap"] = bitmap(r) if r.remaining() else None
    return _finish(r, out)


def tex_bitmap_bytes(data):
    """Return the inline bitmap bytes of a Tex record (tools/tex.py format)."""
    t = decode_tex(data)
    b = t["bitmap"]
    return data[b["offset"]:b["offset"] + b["size"]] if b else None


# ---------------------------------------------------------------- Environ
def decode_environ(data):
    """RndEnviron::Load (PsEnviron_v8 @ 0x1b48b8).  Data is v1."""
    r = Reader(data)
    v = _check_version(r, 1, "Environ")
    out = {"version": v, "drawable": drawable(r)}
    out["lights"] = r.ref_list()         # +0x40 (FUN_002b66d8)
    out["ambient_color"] = r.floats(4)   # +0x50
    out["fog_start"] = r.f32()           # +0x64
    out["fog_end"] = r.f32()             # +0x68
    if v < 1:
        r.u32()
    out["fog_color"] = r.floats(4)       # +0x70
    out["fog_enable"] = r.bool8() if v >= 1 else r.u32() != 0  # +0x60
    return _finish(r, out)


# ---------------------------------------------------------------- Light
def decode_light(data):
    """RndLight::Load (RndLight_v8 @ 0x1bac28).  Data is v3."""
    r = Reader(data)
    v = _check_version(r, 3, "Light")
    out = {"version": v, "transformable": transformable(r)}
    out["color"] = r.floats(4)           # +0xc0
    if v < 2:
        r.floats(8)
    if v < 3:
        r.floats(2)
    out["range"] = r.f32()               # +0xd0
    if v < 3:
        r.floats(3)
    if v > 0:
        out["type"] = r.u32()            # +0xd4
        out["type_name"] = LIGHT_TYPE.get(out["type"], out["type"])
    return _finish(r, out)


# ---------------------------------------------------------------- Cam
def decode_cam(data):
    """RndCam::Load (PsCam_v8 @ 0x1b15c8).  Data is v9."""
    r = Reader(data)
    v = _check_version(r, 9, "Cam")
    if v == 8:
        raise NotImplementedError("Cam v8 legacy list")
    out = {"version": v, "transformable": transformable(r), "drawable": drawable(r)}
    out["near_plane"] = r.f32()          # +0x300
    out["far_plane"] = r.f32()           # +0x304
    out["fov"] = r.f32()                 # +0x308 (radians)
    if v < 2:
        r.u32()
    out["screen_rect"] = r.floats(4)     # +0x314 x, y, w, h
    if 1 <= v <= 2:
        r.u32()
    if v > 3:
        out["z_range"] = r.floats(2)     # +0x30c
    if v > 4:
        out["target_tex"] = r.ref()      # +0x324 render target Tex
    if v == 6:
        r.u32()
    return _finish(r, out)


# ---------------------------------------------------------------- Font
def decode_font(data):
    """RndFont::Load (RndFont_v8 @ 0x1b7600).  Data is v7."""
    r = Reader(data)
    v = _check_version(r, 7, "Font")
    if v < 4:
        raise NotImplementedError(f"Font version {v}")
    out = {"version": v}
    out["mat"] = r.ref()                 # +0x414 Mat holding the glyph sheet
    out["cell_size"] = r.floats(2)       # +0x41c, +0x420 (v>=4)
    out["deprecated_size"] = r.f32()     # +0x424
    out["space"] = r.f32()               # +0x428
    out["chars"] = _string2(r)           # +0x42c characters in the sheet, in order
    out["kerning"] = None
    if v > 4 and r.bool8():
        # v>=7: u32 count, then {u32 packed (byte0 = first char, byte1 = second
        # char), f32 amount} (stored in a 32-bucket hash, FUN_002b8f78)
        ker = []
        for _ in range(r.u32()):
            packed = r.u32()
            ker.append({"first": chr(packed & 0xff), "second": chr((packed >> 8) & 0xff),
                        "amount": r.f32()})
        out["kerning"] = ker
    return _finish(r, out)


# ---------------------------------------------------------------- Text
def decode_text(data):
    """RndText::Load (RndText_t2_v8 @ 0x1d75c8).  Data is v15."""
    r = Reader(data)
    v = _check_version(r, 15, "Text")
    if v < 13:
        raise NotImplementedError(f"Text version {v}")
    out = {"version": v, "drawable": drawable(r)}
    out["transformable"] = transformable(r)   # v>1
    out["font"] = r.ref()                # +0x114
    out["align"] = r.u32()               # +0x120 (v>=3 raw)
    out["align_name"] = text_align(out["align"])
    out["text"] = _string2(r)            # +0x140 String
    out["color"] = r.floats(4)           # +0x160 (v>0)
    out["wrap_width"] = r.f32()          # +0x11c (v>=13)
    out["leading"] = r.f32()             # +0x128 (v>7)
    # +0x144 (v>=12): nonzero -> FUN_00240578(+0x12c) reserves the string buffer;
    # not printed.  Likely a fixed text length (inference).
    out["fixed_length"] = r.u32()
    out["italics"] = r.f32()             # +0x154 (v>9), italic slant
    out["size"] = r.f32()                # +0x150 (v>=13)
    out["markup"] = r.bool8()            # +0x170 (v>13)
    out["caps_mode"] = r.u32()           # +0x124 (v>=15) "capsMode"
    return _finish(r, out)


# ---------------------------------------------------------------- Flare
def decode_flare(data):
    """RndFlare::Load (RndFlare_t2_v8 @ 0x1f4710).  Data is v3."""
    r = Reader(data)
    v = _check_version(r, 3, "Flare")
    out = {"version": v, "transformable": transformable(r), "drawable": drawable(r)}
    if v > 0:
        out["mat"] = r.ref()             # +0x10c
    if v < 3:
        s = r.f32()
        out["sizes"] = [s, s]
    else:
        out["sizes"] = r.floats(2)       # +0x104, +0x108
    if v > 1:
        out["range"] = r.floats(2)       # +0x114, +0x118 (visibility test range)
        out["steps"] = r.u32()           # +0x11c
    return _finish(r, out)


# ---------------------------------------------------------------- Movie
def decode_movie(data):
    """RndMovie::Load (PsMovie_v8 @ 0x1cafa0, vtable slot 8 of RndMovie/PsMovie).
    Data is v6."""
    r = Reader(data)
    v = _check_version(r, 6, "Movie")
    out = {"version": v, "animatable": animatable(r)}
    out["file"] = r.string()             # +0x24 FilePath (.ipu stream)
    if v > 3:
        out["tex"] = r.ref()             # +0x44 Tex the frames are decoded into
    if v > 4:
        out["stream"] = r.bool8()        # +0x3c
    if v >= 6:
        out["loop"] = r.bool8()          # +0x40
    elif v == 5:
        out["loop"] = not out["stream"]
    return _finish(r, out)


DECODERS = {
    "Mat": decode_mat, "Tex": decode_tex, "Environ": decode_environ,
    "Light": decode_light, "Cam": decode_cam, "Font": decode_font,
    "Text": decode_text, "Flare": decode_flare, "Movie": decode_movie,
}
