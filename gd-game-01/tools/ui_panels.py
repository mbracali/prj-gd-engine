#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Read Guitar Hero (PS2) UI panel files (ghui/*.gh) and decode their UI objects.

How the game loads them (see FINDINGS.md "UI panels"):
  UIPanel::Load (HudPanel_v13 @ 0x205e38, shared by every panel class) takes
  the panel's `(rnd_file ...)` from its script, or builds "%s/%s.%s" from the
  script file's directory, the panel name and the UI `rnd_extension` (`gh`,
  config/gh.dta). The file is read by the ordinary RndLoader
  (FUN_001bb100) into the panel's RndDir. Only "*.rnd" names get rewritten to
  "<dir>/gen/<name>.rnd_ps2" (FUN_001bb078), so ".gh" files are read as is.
  `(shared_rnd_files ghui/common.gh)` is loaded once by the UIManager.

Container (RndLoader states FUN_001bb598 -> 7a8 -> 9a0 -> bc040 -> bc400):
  optional 0xCBBEDEAF chunked raw-deflate wrapper (ChunkStream)
  u32 version (<= 10; every file here is 10)
  u32 count; count x {string class (Symbol), string name, u8 only if 1<=version<=7}
  if version > 8: u32 n; n x string  -- external files to preload
       (".gh": the panel's textures, e.g. "image/brick02.bmp", resolved to
        ghui/image/gen/brick02.bmp_ps2; ".rnd_ps2" files have n strings of
        length 0)
  then each object's Load (vtable slot 8 of its Hmx::Object table), and for
  version >= 2 the loader skips forward to the next bytes AD DE AD DE
  (FUN_002e8a78), so trailing bytes in a record are tolerated by the game.
Strings are u32 length + bytes.

UI classes decoded here (Load addresses are vtable table 2 slot 8):
  LabelEx RndLabelEx 0x11e9b8, ButtonEx RndButtonEx 0x12b8e0,
  PictureEx 0x120508, PictureToggle 0x1217e8, List RndScrollList 0x123e80,
  Slider 0x127b10, TextEntry 0x1292e8, CreditList 0x12e2e8,
  Text RndText 0x1d75c8, Placer RndPlacer 0x122c60.
Other classes (Mesh, Mat, Tex, View, Cam, Environ, Light, *Anim) are generic
Rnd classes; this tool only reports their record sizes.

Usage: ui_panels.py list <file.gh>...    objects, types, names, decoded fields
       ui_panels.py check <dir>...        validate every .gh below <dir>
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rnd_classes.base import Reader, drawable, transformable  # noqa: E402

MARKER = b"\xad\xde\xad\xde"


def decompress(data):
    if data[:4] != b"\xaf\xde\xbe\xcb":
        return data
    header, count = struct.unpack_from("<II", data, 4)
    sizes = struct.unpack_from(f"<{count}I", data, 16)
    out, pos = [], header
    for s in sizes:
        out.append(zlib.decompress(data[pos:pos + s], -15))
        pos += s
    return b"".join(out)


def _base(r, order="td"):
    out = {}
    for part in order:
        if part == "t":
            out["trans"] = transformable(r)
        else:
            out["draw"] = drawable(r)
    return out


def label_ex(r):
    """RndLabelEx (max version 6). Wraps an RndText (+0x114)."""
    v = r.u32()
    o = {"version": v, **_base(r)}
    if v >= 6:
        # FUN_0011f868: scale the text down to fit this box when enabled.
        o["fit"] = r.bool8()
        o["fit_width"], o["fit_height"] = r.f32(), r.f32()
    if v > 4:
        o["leading"] = r.f32()            # -> RndText leading (+0x128)
    if v > 3:
        # -> RndText align (+0x120): high nibble 1 top/2 middle/4 bottom,
        # low nibble 1 left/2 centre/4 right (table in RndText_t2_v8)
        o["align"] = r.u32()
    if v > 2:
        # Per-glyph random jitter (FUN_0011f640 / FUN_0011f240): seed, then
        # max x offset, max z offset, max rotation in degrees.
        o["jitter_seed"] = r.u32()
        o["jitter"] = r.floats(3)
    o["style"] = r.string() if v >= 2 else "labelex"   # ghui style symbol
    if v >= 1:
        o["caps"] = r.bool8()             # RndText capsMode 2 when set
    o["text"] = r.string()                # localisation token (ghui/eng/locale)
    o["width"] = r.f32()                  # -> RndText wrap width (+0x11c)
    return o


def button_ex(r):
    """RndButtonEx (max version 4); same style/jitter scheme as LabelEx."""
    v = r.u32()
    o = {"version": v, **_base(r)}
    if v > 2:
        o["jitter_seed"] = r.u32()
        o["jitter"] = r.floats(3)
    if v >= 2:
        o["style"] = r.string()
        o["flag_134"] = r.bool8()
        if v < 4:
            r.u8()
    else:
        o["style"], o["flag_134"] = "buttonex", True
    if v >= 1:
        o["flag_12c"] = r.bool8()
    # Same 400/40 defaults as LabelEx fit_width/fit_height (meaning inferred).
    o["box_width"], o["box_height"] = r.f32(), r.f32()
    o["text"] = r.string()
    return o


def picture_ex(r):
    """RndPictureEx (max version 1): base + style symbol."""
    v = r.u32()
    o = {"version": v, **_base(r)}
    o["style"] = r.string() if v >= 1 else "pictureex"
    return o


def picture_toggle(r):
    """RndPictureToggle (version 0): base + style symbol."""
    v = r.u32()
    return {"version": v, **_base(r), "style": r.string()}


def scroll_list(r):
    """RndScrollList ("List", max version 2)."""
    v = r.u32()
    o = {"version": v}
    if v < 1:
        r.u32(), r.u32()
    o.update(_base(r))
    o["style"] = r.string()
    if v < 1:
        o["object"] = r.ref()
    o["count_138"], o["spacing"] = r.u32(), r.f32()   # e.g. 60, 40.0
    o["mode"] = (0 if r.bool8() else 2) if v < 2 else r.u32()
    return o


def slider(r):
    """RndSlider (version 1): base, two symbols, object reference."""
    v = r.u32()
    o = {"version": v, **_base(r)}
    o["style"] = r.string()
    o["symbol_120"] = r.string()
    o["object"] = r.ref()
    return o


def text_entry(r):
    """RndTextEntry (version 0): base + style symbol."""
    v = r.u32()
    return {"version": v, **_base(r), "style": r.string()}


def credit_list(r):
    """RndCreditList (version 0): drawable THEN transformable, u32, f32."""
    v = r.u32()
    o = {"version": v, **_base(r, "dt")}
    o["count_144"], o["spacing"] = r.u32(), r.f32()   # e.g. 15, 25.0
    return o


def placer(r):
    """RndPlacer: drawable, transformable, symbol (no version check)."""
    v = r.u32()
    return {"version": v, **_base(r, "dt"), "symbol": r.string()}


def text(r):
    """RndText (max version 15), RndText_t2_v8 @ 0x1d75c8."""
    v = r.u32()
    o = {"version": v, "draw": drawable(r)}
    if v < 7:
        r.u32()
        o["old_list"] = [r.u32() for _ in range(r.u32())]  # FUN_002b5d28 (guess)
    if v > 1:
        o["trans"] = transformable(r)
    o["font"] = r.ref()
    o["align"] = r.u32()
    if v < 2:
        o["old_size"] = r.floats(2)
    o["text"] = r.string()
    if v > 0:
        o["color"] = r.floats(4)
    if v >= 13:
        o["wrap_width"] = r.f32()
    elif v > 3:
        on = r.bool8()
        w = r.f32()
        o["wrap_width"] = w if on else 0.0
    if v == 5:
        r.string()
    if 5 <= v <= 10:
        o["flag"] = r.bool8()
    if v > 7:
        o["leading"] = r.f32()
    if v >= 12:
        o["fixed_length"] = r.u32()
    elif v >= 9:
        o["fixed"] = r.bool8()
    if v > 9:
        o["italics"] = r.f32()
    if v >= 13:
        o["size"] = r.f32()
    if v > 13:
        o["markup"] = r.bool8()
    if v >= 15:
        o["caps_mode"] = r.u32()
    return o


DECODERS = {
    "LabelEx": label_ex, "ButtonEx": button_ex, "PictureEx": picture_ex,
    "PictureToggle": picture_toggle, "List": scroll_list, "Slider": slider,
    "TextEntry": text_entry, "CreditList": credit_list, "Placer": placer,
    "Text": text,
}


def parse(data):
    """Returns dict(version, files, objects=[(type, name, record, decoded|None)])."""
    data = decompress(data)
    r = Reader(data)
    version = r.u32()
    if version > 10:
        raise ValueError(f"version {version} > 10")
    entries = []
    for _ in range(r.u32()):
        entries.append((r.string(), r.string()))
        if 1 <= version <= 7:
            r.u8()
    files = [r.string() for _ in range(r.u32())] if version > 8 else []
    objects = []
    for t, n in entries:
        end = data.find(MARKER, r.p)
        if end < 0:
            raise ValueError(f"{t} {n}: no end marker")
        rec = data[r.p:end]
        dec = None
        if t in DECODERS:
            rr = Reader(rec)
            dec = DECODERS[t](rr)
            if rr.p != len(rec):
                raise ValueError(f"{t} {n}: decoded {rr.p} of {len(rec)} bytes")
        objects.append((t, n, rec, dec))
        r.p = end + 4
    if r.p != len(data):
        raise ValueError(f"{len(data) - r.p} trailing bytes")
    return {"version": version, "files": files, "objects": objects}


def _short(dec):
    skip = {"trans", "draw", "version"}
    parts = [f"{k}={v!r}" for k, v in dec.items() if k not in skip]
    if "trans" in dec:
        w = dec["trans"]["world"]
        parts.append(f"pos=({w[9]:g},{w[10]:g},{w[11]:g})")
        if dec["trans"].get("parent"):
            parts.append(f"parent={dec['trans']['parent']!r}")
    return " ".join(parts)


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("list", "check"):
        raise SystemExit(__doc__)
    if sys.argv[1] == "list":
        for path in sys.argv[2:]:
            p = parse(open(path, "rb").read())
            print(f"{path}: version {p['version']}, {len(p['objects'])} objects")
            for f in p["files"]:
                print(f"  file {f}")
            for t, n, rec, dec in p["objects"]:
                extra = "  " + _short(dec) if dec else ""
                print(f"  {t:14} {len(rec):7}  {n}{extra}")
        return
    ok, bad, types, decoded = 0, [], {}, 0
    for root in sys.argv[2:]:
        for base, _, names in os.walk(root):
            for f in sorted(names):
                if not f.endswith(".gh"):
                    continue
                path = os.path.join(base, f)
                try:
                    p = parse(open(path, "rb").read())
                except Exception as e:  # report and continue
                    bad.append((path, repr(e)))
                    continue
                ok += 1
                for t, _, _, dec in p["objects"]:
                    types[t] = types.get(t, 0) + 1
                    decoded += dec is not None
    print(f"{ok} files parsed, {decoded} UI objects decoded exactly")
    print("types:", ", ".join(f"{t} {c}" for t, c in sorted(types.items(), key=lambda x: -x[1])))
    for p, e in bad:
        print("  FAIL", p, e)
    print(f"{len(bad)} failures")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
