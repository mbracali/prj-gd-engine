#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero II (Xbox 360) scene files: .milo_ps2 object directories.

    milo.py list   <file>             class, name and record size of every object
    milo.py unpack <file> <out dir>   decompressed stream + one blob per object
    milo.py scan   <dir>              parse every .milo_ps2 below <dir>: totals
    milo.py ui     <file|dir> [-v]    decode the UI component, Font and Text
                                      records (checks each ends at its marker)

Container (all little-endian, recovered by inspection; see
docs/milo_and_textures.md):
    u32 0xCBBEDEAF  (bytes AF DE BE CB)
    u32 header size (offset of the first block; 0x810 in every file)
    u32 block count
    u32 largest inflated block size
    u32 compressed size of each block, then zero padding up to header size
    blocks: raw deflate streams (no zlib header); concatenated, they give the
    directory stream below.

Directory stream (version 25 in every file):
    u32 version (25)
    string directory class (ObjectDir, RndDir, PanelDir, Character, ...)
    string directory name
    u32 string-table count, u32 string-table size (allocation hints)
    u32 object count, then per object: string class, string name
    the directory object's own data, then 0xADDEADDE
    per object, in table order: its data, then 0xADDEADDE
Strings are u32 length + bytes.

Inline sub-directories: an object that is itself a directory (RndDir,
Character, ...) whose proxy is stored inline carries, inside its own data, a
complete directory header (version 25, class, name, counts, object table).
That sub-directory's own data and its objects follow (each with its own
0xADDEADDE) before the outer object's remaining data and end marker.
"""
import os
import re
import struct
import sys
import zlib

MAGIC_DEFLATE = 0xCBBEDEAF   # deflate blocks (the only kind in GH2's data)
MAGIC_STORED = 0xCABEDEAF    # same table, blocks stored as is (older games)
MARKER = b"\xad\xde\xad\xde"
VERSION = 24
# A string that can be a class name, used to spot inline directory headers.
_CLASS_RE = re.compile(rb"[A-Za-z][A-Za-z0-9_]{1,40}\Z")


def decompress(data):
    """Container -> directory stream. Returns (stream, info dict)."""
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic not in (MAGIC_DEFLATE, MAGIC_STORED):
        return data, {"magic": None}
    header, count, largest = struct.unpack_from("<III", data, 4)
    sizes = struct.unpack_from(f"<{count}I", data, 16)
    out, pos = [], header
    for s in sizes:
        blk = data[pos:pos + s]
        out.append(zlib.decompress(blk, -15) if magic == MAGIC_DEFLATE else blk)
        pos += s
    if pos != len(data):
        raise ValueError(f"blocks end at {pos:#x}, file is {len(data):#x}")
    if out and max(len(b) for b in out) != largest:
        raise ValueError("largest-block field does not match")
    return b"".join(out), {"magic": magic, "header": header, "blocks": count,
                           "largest": largest}


class Reader:
    def __init__(self, data, pos=0):
        self.d, self.p = data, pos

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def string(self, limit=1 << 20):
        n = self.u32()
        if n > limit or self.p + n > len(self.d):
            raise ValueError(f"bad string length {n} at {self.p - 4:#x}")
        s = self.d[self.p:self.p + n].decode("latin-1")
        self.p += n
        return s


def read_header(d, pos):
    """Directory header at pos -> (dict, position after the object table)."""
    r = Reader(d, pos)
    h = {"version": r.u32()}
    if h["version"] != VERSION:
        raise ValueError(f"directory version {h['version']}, expected {VERSION}")
    h["class"], h["name"] = r.string(), r.string()
    h["string_count"], h["string_size"] = r.u32(), r.u32()
    n = r.u32()
    if n > 100000:
        raise ValueError("implausible object count")
    h["entries"] = [(r.string(256), r.string(1024)) for _ in range(n)]
    return h, r.p


def _inline_header_at(d, q, end):
    """True if a plausible directory header starts at d[q] (before end)."""
    try:
        n = struct.unpack_from("<I", d, q + 4)[0]
        if not 3 <= n <= 40 or not _CLASS_RE.match(d[q + 8:q + 8 + n]):
            return False
        h, after = read_header(d, q)
        return after <= end and all(_CLASS_RE.match(c.encode()) for c, _ in h["entries"])
    except (ValueError, struct.error, UnicodeError):
        return False


def _read_object(d, pos, cls, name, out, depth):
    """One object's data starting at pos, through its end marker.

    Appends (class, name, depth, record) to out; an inline sub-directory found
    inside the record is parsed recursively (its objects are appended at
    depth + 1, before this object). Returns the position after the marker.
    """
    start = pos
    while True:
        end = d.find(MARKER, pos)
        if end < 0:
            raise ValueError(f"{cls} {name}: no end marker")
        q = d.find(struct.pack("<I", VERSION), pos, end)
        while q >= 0 and not _inline_header_at(d, q, end):
            q = d.find(struct.pack("<I", VERSION), q + 1, end)
        if q < 0:
            break
        sub, pos = _read_dir(d, q, out, depth + 1)
        out.append(("(inline)", f"{sub['class']} {sub['name']}", depth + 1, b""))
    out.append((cls, name, depth, d[start:end]))
    return end + 4


def _read_dir(d, pos, out, depth=0):
    h, pos = read_header(d, pos)
    pos = _read_object(d, pos, h["class"], h["name"] + " (directory)", out, depth)
    for cls, name in h["entries"]:
        pos = _read_object(d, pos, cls, name, out, depth)
    return h, pos


def parse(data):
    """File bytes -> (container info, top directory header, objects).

    objects: list of (class, name, depth, record bytes) in stream order; the
    directory's own record comes first and is named '<name> (directory)'.
    Records contain no final end marker. An object holding an inline
    sub-directory has a record spanning that whole sub-directory (header,
    records, their markers); the sub-directory's own records are also listed,
    at depth + 1, just before it, followed by an '(inline)' summary row.
    """
    stream, info = decompress(data)
    out = []
    h, pos = _read_dir(stream, 0, out)
    if pos != len(stream):
        raise ValueError(f"{len(stream) - pos} trailing bytes")
    info["stream"] = len(stream)
    return info, h, out


def objects(data):
    """(class, name, record) of every top-level object (not the directory)."""
    _, h, objs = parse(data)
    return [(c, n, rec) for c, n, dep, rec in objs
            if dep == 0 and not n.endswith(" (directory)")]


def tex_info(rec):
    """Tex (version 10) record -> dict with the inline bitmap offset or None.

    u32 version; 13 bytes common object header; u32 width, u32 height,
    u32 bpp; string source file; f32 (mip bias?); u32 (?); u8 use external
    file; if 0, the bitmap (tex.py) follows.
    """
    r = Reader(rec)
    v = r.u32()
    r.p += 13
    w, h, bpp = r.u32(), r.u32(), r.u32()
    path = r.string()
    bias, kind = struct.unpack_from("<fI", rec, r.p)
    r.p += 8
    flag = rec[r.p]
    r.p += 1
    return {"version": v, "width": w, "height": h, "bpp": bpp, "file": path,
            "bias": bias, "type": kind, "flag": flag,
            "bitmap": r.p if len(rec) - r.p >= 32 else None}


# ---- UI components, Font and Text --------------------------------------
# Record layouts confirmed against the Load routines of default.xex (each
# class's PreLoad data, then its PostLoad data; addresses in the docstrings).
# Field names come from each class's SyncProperty strings; "Q:" marks guesses.

class Rec(Reader):
    """Reader over one object record with the shared Harmonix sub-records."""

    def f32(self):
        v = struct.unpack_from("<f", self.d, self.p)[0]
        self.p += 4
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]
        self.p += 4
        return v

    def b(self):
        v = self.d[self.p]
        self.p += 1
        return v != 0

    def floats(self, n):
        return [self.f32() for _ in range(n)]

    def version(self, cls, allowed):
        v = self.u32()
        if v not in allowed:
            raise ValueError(f"{cls} version {v}")
        return v

    def data_array(self, depth=0):
        """DataArray::Load @ 0x8231aa20 (skipped, returns the node count)."""
        n = struct.unpack_from("<h", self.d, self.p)[0]
        self.p += 6
        for _ in range(n):
            t = struct.unpack_from("<H", self.d, self.p)[0]
            self.p += 4
            if t in (0x10, 0x11, 0x13):
                self.data_array(depth + 1)
            elif t in (0x12, 2, 3, 4, 5, 7, 0x20, 0x21, 0x22, 0x23):
                self.string()
            else:
                self.p += 4
        return n

    def object_header(self):
        """Hmx::Object::Load @ 0x82315350: rev, type, props, (rev>0) note."""
        rev = self.u32()
        typ = self.string()
        if self.b():
            self.data_array()
        if rev > 0:
            self.string()
        return typ

    def trans(self):
        """RndTransformable v9 @ 0x821b0a28 -> local position."""
        self.version("Trans", (9,))
        local = self.floats(12)
        self.floats(12)  # world
        self.u32()       # constraint
        self.string()    # target
        self.b()         # preserve_scale
        parent = self.string()
        return {"rot": local[:9], "pos": local[9:], "parent": parent}

    def draw(self):
        """RndDrawable v3 @ 0x821b36c0 -> showing."""
        self.version("Draw", (3,))
        showing = self.b()
        self.floats(5)   # sphere, draw order
        return showing


def ui_component(r, o):
    """UIComponent PreLoad @ 0x8236fa30: u32 rev (<= 1), Object, Trans,
    Draw, (rev >= 1) nav_right and nav_down object names (@ 0x8236f568)."""
    rev = r.version("UIComponent", (0, 1))
    o["type"] = r.object_header()
    o["trans"] = r.trans()
    o["showing"] = r.draw()
    if rev >= 1:
        o["nav_right"], o["nav_down"] = r.string(), r.string()


def ui_label(r, o):
    """UILabel PreLoad @ 0x8236ddb8: u32 rev (<= 1), UIComponent, (rev >= 1)
    bool localize; localize ? symbol text_token : string text."""
    rev = r.version("UILabel", (0, 1))
    ui_component(r, o)
    o["localize"] = r.b() if rev >= 1 else False
    if o["localize"]:
        o["text_token"] = r.string()
    else:
        o["text"] = r.string()


def band_text_fields(r, o, color):
    """The text fields BandLabel v12 / BandButton v10 share (offsets from the
    setters @ 0x8214c4f8.. and SyncProperty @ 0x8214b400 / 0x82123b88)."""
    o["fit_text"] = r.u32()        # Q: enum (0 wrap?)
    o["width"], o["height"] = r.f32(), r.f32()
    o["leading"] = r.f32()
    o["alignment"] = r.u32()       # RndText flags: 1/2/4 left/centre/right | 0x10/0x20/0x40 top/middle/bottom
    o["unknown"] = r.u32()         # Q: no property; read since v3
    o["delta"] = r.floats(3)       # deltaX, deltaZ, deltaRot (jitter ranges)
    o["all_caps"] = r.b()
    o["kerning"] = r.f32()
    o["text_size"] = r.f32()


def band_label(r, o):
    """BandLabel v12 PreLoad @ 0x8214acb8 (PostLoad @ 0x8214a7a8 reads nothing)."""
    r.version("BandLabel", (11, 12))
    ui_label(r, o)
    band_text_fields(r, o, True)
    o["wrap_width"] = r.f32()      # v12; v11 copies width
    o["color"] = r.floats(4)       # @ 0x821222e0


def band_button(r, o):
    """BandButton v10 PreLoad @ 0x82123398: UIButton (@ 0x82370498: u32 rev
    <= 0 + UILabel), the shared text fields, wrap_width (v10)."""
    r.version("BandButton", (9, 10))
    r.version("UIButton", (0,))
    ui_label(r, o)
    band_text_fields(r, o, False)
    o["wrap_width"] = r.f32()


def ui_list(r, o):
    """UIList v3: PreLoad @ 0x8237a4e8 (UIComponent), PostLoad @ 0x8237a540."""
    rev = r.version("UIList", (1, 2, 3))
    ui_component(r, o)
    o["spacing"], o["arrow_offset"] = r.f32(), r.f32()
    o["fade_offset"] = r.i32()
    o["display_num"] = r.i32()
    o["circular"] = r.b()
    o["scroll_time"] = r.f32()
    if rev > 2:
        o["paginate"] = r.b()
    if rev == 1:
        r.u32(), r.u32()


def simple_component(cls, allowed=(0,)):
    def fn(r, o):
        r.version(cls, allowed)
        ui_component(r, o)
    return fn


def band_slider(r, o):
    """BandSlider v2 PreLoad @ 0x82145b90: UISlider (@ 0x823757f0: u32 rev
    <= 0 + UIComponent), then symbol text_token."""
    r.version("BandSlider", (2,))
    r.version("UISlider", (0,))
    ui_component(r, o)
    o["text_token"] = r.string()


def band_text_entry(r, o):
    """BandTextEntry v3: PreLoad @ 0x82142df8 (UIComponent), PostLoad @
    0x82141f78 (names from SyncProperty @ 0x82144910)."""
    rev = r.version("BandTextEntry", (1, 2, 3))
    ui_component(r, o)
    if rev > 1:
        o["text_scale"], o["flash_time"], o["arrow_offset"] = r.floats(3)
    if rev > 2:
        o["entered_color"], o["dynamic_color"] = r.floats(4), r.floats(4)


def ui_trigger(r, o):
    """UITrigger v0 PreLoad @ 0x8237d898: UIComponent, symbol event,
    ObjPtr anim (@ 0x82127788), bool block_transition."""
    r.version("UITrigger", (0,))
    ui_component(r, o)
    o["event"], o["anim"] = r.string(), r.string()
    o["block_transition"] = r.b()


def screen_mask(r, o):
    """RndScreenMask v2 Load @ 0x8221a970 (SyncProperty @ 0x8221aa28)."""
    rev = r.version("ScreenMask", (0, 1, 2))
    o["type"] = r.object_header()
    o["showing"] = r.draw()
    o["mat"] = r.string()
    o["color"] = r.floats(4)       # rgb + alpha
    if rev > 0:
        o["screen_rect"] = r.floats(4)
    if rev > 1:
        o["use_cam_rect"] = r.b()


def font(r, o):
    """RndFont v15 Load @ 0x821d4cd8. Glyphs are stored (no texture scan as
    in GH1): u, v of the cell's top-left, width and advance in cell widths;
    the quad's UV size is (cell_uv.x * width, cell_uv.y) (@ 0x821d2e40)."""
    r.version("Font", (15,))
    o["type"] = r.object_header()
    o["mat"] = r.string()
    o["cell_size"] = r.floats(2)   # pixels (+0x1048 / +0x104c)
    o["deprecated_size"] = r.f32()
    o["kerning"] = r.f32()         # Q: name; added between every pair (+0x1044)
    o["chars"] = r.string()
    pairs = []
    if r.b():                      # @ 0x821d4a00: key = first | second << 8
        for _ in range(r.u32()):
            pairs.append((r.u32(), r.f32()))
    o["pairs"] = pairs
    o["texture_owner"] = r.string()  # Font whose glyph table is used
    o["monospace"] = r.b()         # Q: name; +0x1068: advance 1, no kerning
    o["flag_1074"] = r.b()         # Q: unused by layout
    o["tex_size"] = (r.u32(), r.u32())
    o["cell_uv"] = r.floats(2)
    o["glyphs"] = [r.floats(4) for _ in range(256)]


def text(r, o):
    """RndText v17 Load @ 0x821d1a68."""
    r.version("Text", (17,))
    o["type"] = r.object_header()
    o["showing"] = r.draw()
    o["trans"] = r.trans()
    o["font"] = r.string()
    o["align"] = r.u32()
    o["text"] = r.string()
    o["color"] = r.floats(4)
    o["wrap_width"], o["leading"] = r.f32(), r.f32()
    o["fixed_length"] = r.u32()
    o["italics"], o["size"] = r.f32(), r.f32()
    o["markup"] = r.b()
    o["caps_mode"] = r.u32()


def band_placer(r, o):
    """BandPlacer v2 (Load not located; layout from the data, every record
    ends exactly): Object, Draw, Trans, then one object name (Q: "center",
    the other SyncProperty string @ 0x8212a4a0 besides "proxy")."""
    r.version("BandPlacer", (2,))
    o["type"] = r.object_header()
    o["showing"] = r.draw()
    o["trans"] = r.trans()
    o["center"] = r.string()


UI_DECODERS = {
    "BandPlacer": band_placer, "UILabel": ui_label,
    "BandLabel": band_label, "BandButton": band_button, "UIList": ui_list,
    "CheckBox": simple_component("CheckBox"),
    "UIPicture": simple_component("UIPicture"),
    "UIProxy": simple_component("UIProxy"),
    "BandSlider": band_slider, "BandTextEntry": band_text_entry,
    "UITrigger": ui_trigger, "ScreenMask": screen_mask,
    "Font": font, "Text": text,
}


def decode_ui(cls, rec):
    """Record -> dict; raises if the decoder does not end exactly at the end."""
    r = Rec(rec)
    o = {}
    UI_DECODERS[cls](r, o)
    if r.p != len(rec):
        raise ValueError(f"{len(rec) - r.p} bytes left")
    return o


def ui_cmd(target, verbose):
    from collections import Counter
    files = [target] if os.path.isfile(target) else sorted(
        os.path.join(b, f) for b, _, fs in os.walk(target) for f in fs
        if f.endswith(".milo_ps2"))
    ok, bad, values = Counter(), [], {}
    for path in files:
        for cls, name, rec in objects(open(path, "rb").read()):
            if cls not in UI_DECODERS:
                continue
            try:
                o = decode_ui(cls, rec)
            except Exception as e:
                bad.append(f"{path}: {cls} {name}: {e}")
                continue
            ok[cls] += 1
            for k, v in o.items():
                if isinstance(v, (int, float, bool)) and not isinstance(v, list):
                    values.setdefault((cls, k), Counter())[v] += 1
            if verbose:
                show = {k: v for k, v in o.items() if k not in ("glyphs", "pairs")}
                print(f"{os.path.basename(path)}: {cls} {name}: {show}")
    for cls, n in sorted(ok.items()):
        print(f"{cls:14} {n:5} decoded")
    for (cls, k), c in sorted(values.items()):
        if len(c) <= 8:
            print(f"  {cls}.{k}: {dict(c.most_common())}")
    for b in bad[:30]:
        print("FAIL", b)
    print(f"{sum(ok.values())} decoded, {len(bad)} failed")


def _safe(name):
    return re.sub(r"[^A-Za-z0-9._ -]", "_", name)


def main():
    a = sys.argv[1:]
    if len(a) >= 2 and a[0] == "list":
        info, h, objs = parse(open(a[1], "rb").read())
        print(f"version {h['version']}  {h['class']} '{h['name']}'  "
              f"{len(h['entries'])} objects  blocks {info.get('blocks')}  "
              f"stream {info['stream']} bytes")
        for cls, name, dep, rec in objs:
            ver = struct.unpack_from("<I", rec)[0] if len(rec) >= 4 else ""
            if cls == "(inline)":
                print(f"{'  ' * dep}-- end of inline directory {name}")
                continue
            print(f"{'  ' * dep}{cls:20} v{ver!s:3} {len(rec):8}  {name}")
    elif len(a) == 3 and a[0] == "unpack":
        data = open(a[1], "rb").read()
        stream, _ = decompress(data)
        _, h, objs = parse(data)
        os.makedirs(a[2], exist_ok=True)
        open(os.path.join(a[2], "_stream.bin"), "wb").write(stream)
        for i, (cls, name, dep, rec) in enumerate(objs):
            if cls == "(inline)":
                continue
            fn = f"{i:04d}_{_safe(cls)}_{_safe(name)}.bin"
            open(os.path.join(a[2], fn), "wb").write(rec)
        print(f"{len(objs)} records written to {a[2]}")
    elif len(a) >= 2 and a[0] == "ui":
        ui_cmd(a[1], "-v" in a)
    elif len(a) == 2 and a[0] == "scan":
        scan(a[1])
    else:
        raise SystemExit(__doc__)


def scan(root):
    from collections import Counter, defaultdict
    classes, files_with, versions = Counter(), Counter(), defaultdict(Counter)
    dirs, fails, n, inline = Counter(), [], 0, 0
    for base, _, files in os.walk(root):
        for f in sorted(files):
            if not f.endswith(".milo_ps2"):
                continue
            path = os.path.join(base, f)
            try:
                _, h, objs = parse(open(path, "rb").read())
            except Exception as e:  # report and go on
                fails.append((path, str(e)))
                continue
            n += 1
            dirs[h["class"]] += 1
            seen = set()
            for cls, name, dep, rec in objs:
                if cls == "(inline)":
                    inline += 1
                    continue
                if name.endswith(" (directory)"):
                    cls = cls + " (dir)"
                classes[cls] += 1
                seen.add(cls)
                if len(rec) >= 4:
                    versions[cls][struct.unpack_from("<I", rec)[0]] += 1
            for c in seen:
                files_with[c] += 1
    print(f"{n} files parsed, {len(fails)} failed, {inline} inline sub-directories")
    print("top directory classes:", dict(dirs.most_common()))
    print(f"{'class':24}{'objects':>8}{'files':>7}  versions")
    for c, k in classes.most_common():
        vs = ", ".join(f"{v}" + (f"x{m}" if len(versions[c]) > 1 else "")
                       for v, m in sorted(versions[c].items()))
        print(f"{c:24}{k:8}{files_with[c]:7}  {vs}")
    for p, e in fails[:20]:
        print("FAIL", p, e)


if __name__ == "__main__":
    main()
