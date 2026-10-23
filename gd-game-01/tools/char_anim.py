#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Decode Guitar Hero (PS2) character animation files (.acp, .acg, .acs).

.acp  one AnimClipSamples clip (charsys/<char>/anims/[finger/|editor/]gen/<clip>.acp)
      Loaded by the AnimSet loader (FUN_0017d0e8) from "%s/gen/%s.acp" with the
      set's `directory` and the clip name from its `animations` list
      (charsys/*_anims.dta); parsed by FUN_00189f30, then the clip's Load
      (AnimClipSamples_v12 @ 0x17cdb8 -> AnimClip_v12 @ 0x189da0).
        string class ("AnimClipSamples"), string name
        AnimClip:   u32 version (18; the tool side rebuilds files < 18),
                    f32 start_frame, f32 end_frame, f32 frames_per_sec (v>=2,
                    default 1.6667), u32 flags (v>3), u32 play_flags, f32
                    blend_width (v>16)
                    Frames are beats: frames_per_sec is the authored tempo in
                    beats/s (e.g. *_fast ~2.4 = 144 bpm, *_medium ~1.24).
        AnimClipSamples: u32 version (5)
          two SampleSets, "Full" then "One" (print strings), each:
            u32 n, n x string channel ("bone_x.pos" / ".quat" / ".rotz";
                type from the suffix, FUN_00188790), u32 num_samples,
                u32 compressed (version > 3)
          then the sample data of Full, then of One (FUN_0017c358): for each
          sample, all .pos channels (3 x f32 each), then all .quat channels
          (compressed: 4 x s16 / 32767 (x,y,z,w); else 4 x f32), then all
          .rotz channels (compressed: s16 / 1638.4 radians; else f32).
      Playback (AnimClipSamples_v7, FUN_0017ace8): t = clamp((frame - start)
      / (end - start)); sample position t * (num_samples - 1), linear
      interpolation (component-wise for quats). "One" holds channels that do not
      change (num_samples is normally 1).

.acg  precomputed transition graph for one clip set (FUN_0017f440); path
      "<directory>/<set name without extension>.acg", e.g. main::hiphop.cset ->
      charsys/hiphop/anims/hiphop.acg.
        u32 version (1), u32 clip_count (must equal the set's clip count, or
        the graph is rebuilt), per clip in `animations` order: u32 n,
        n x {u32 dest_clip, f32 src_frame, f32 dest_frame}
      (meaning of the two floats inferred from the data: frames lie in the
      source and destination clip ranges.)

.acs  plain-text DTA: the list of clip-set macros to precache.

Usage: char_anim.py show <file>       dump one file
       char_anim.py check <dir>...     decode every .acp/.acg/.acs, require exact size
"""
import os
import struct
import sys


class Reader:
    def __init__(self, data):
        self.d, self.p = data, 0

    def take(self, fmt):
        v = struct.unpack_from("<" + fmt, self.d, self.p)
        self.p += struct.calcsize("<" + fmt)
        return v

    def u32(self):
        return self.take("I")[0]

    def f32(self):
        return self.take("f")[0]

    def string(self):
        n = self.u32()
        if n > len(self.d) - self.p:
            raise ValueError(f"bad string length {n} at {self.p - 4:#x}")
        s = self.d[self.p:self.p + n].decode("latin-1")
        self.p += n
        return s


def channel_type(name):
    """FUN_00188790: 0 pos, 1 quat, 2 rotz, 3 unknown (first letter after a '.')."""
    i = name.find(".")
    while i >= 0:
        c = name[i + 1:i + 2]
        if c in ("p", "q", "r"):
            return "pqr".index(c)
        i = name.find(".", i + 1)
    return 3


def _set_header(r, sver):
    chans = [r.string() for _ in range(r.u32())]
    n = r.u32()
    comp = r.u32() if sver > 3 else 0
    return {"channels": chans, "num_samples": n, "compressed": bool(comp)}


def _set_samples(r, s):
    kinds = [channel_type(c) for c in s["channels"]]
    if 3 in kinds:
        raise ValueError(f"unknown channel type in {s['channels']}")
    by = {k: [c for c, t in zip(s["channels"], kinds) if t == k] for k in (0, 1, 2)}
    s["pos"], s["quat"], s["rotz"] = by[0], by[1], by[2]
    frames = []
    for _ in range(s["num_samples"]):
        f = {}
        for c in by[0]:
            f[c] = r.take("3f")
        for c in by[1]:
            if s["compressed"]:
                f[c] = tuple(v / 32767.0 for v in r.take("4h"))
            else:
                f[c] = r.take("4f")
        for c in by[2]:
            f[c] = r.take("h")[0] / 1638.4 if s["compressed"] else r.f32()
        frames.append(f)
    s["samples"] = frames


def parse_acp(data):
    r = Reader(data)
    clip = {"class": r.string(), "name": r.string()}
    v = clip["version"] = r.u32()
    clip["start_frame"], clip["end_frame"] = r.f32(), r.f32()
    clip["frames_per_sec"] = r.f32() if v >= 2 else 1.6666666
    clip["flags"] = r.u32() if v > 3 else 0
    if v > 16:
        clip["play_flags"], clip["blend_width"] = r.u32(), r.f32()
    sver = clip["samples_version"] = r.u32()
    clip["full"] = _set_header(r, sver)
    clip["one"] = _set_header(r, sver)
    _set_samples(r, clip["full"])
    _set_samples(r, clip["one"])
    if r.p != len(data):
        raise ValueError(f"{len(data) - r.p} trailing bytes")
    return clip


def parse_acg(data):
    r = Reader(data)
    out = {"version": r.u32(), "clips": []}
    for _ in range(r.u32()):
        out["clips"].append([r.take("Iff") for _ in range(r.u32())])
    if r.p != len(data):
        raise ValueError(f"{len(data) - r.p} trailing bytes")
    return out


def parse_acs(data):
    lines = data.decode("latin-1").splitlines()
    return [l.strip() for l in lines if l.strip() and not l.lstrip().startswith((";", "#"))]


def parse(path):
    data = open(path, "rb").read()
    ext = os.path.splitext(path)[1]
    return {".acp": parse_acp, ".acg": parse_acg, ".acs": parse_acs}[ext](data)


def show(path):
    p = parse(path)
    if path.endswith(".acs"):
        print("precache macros:", " ".join(p))
        return
    if path.endswith(".acg"):
        print(f"version {p['version']}, {len(p['clips'])} clips")
        for i, ts in enumerate(p["clips"]):
            print(f"  clip {i}: {len(ts)} transitions",
                  " ".join(f"->{d}@{a:g}/{b:g}" for d, a, b in ts[:6]),
                  "..." if len(ts) > 6 else "")
        return
    print(f"{p['class']} {p['name']} v{p['version']} frames {p['start_frame']:g}..{p['end_frame']:g}"
          f" fps {p['frames_per_sec']:g} flags {p['flags']:#x} play_flags {p.get('play_flags', 0):#x}"
          f" blend {p.get('blend_width', 0):g} samples v{p['samples_version']}")
    for key in ("full", "one"):
        s = p[key]
        print(f"  {key}: {s['num_samples']} samples, compressed={s['compressed']},"
              f" {len(s['pos'])} pos, {len(s['quat'])} quat, {len(s['rotz'])} rotz")
        if s["samples"]:
            for c, v in list(s["samples"][0].items())[:4]:
                print(f"    [0] {c} = {v}")


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("show", "check"):
        raise SystemExit(__doc__)
    if sys.argv[1] == "show":
        for path in sys.argv[2:]:
            show(path)
        return
    counts, bad, stats = {}, [], {"samples": 0, "channels": set(), "comp": 0, "one_n": set()}
    for root in sys.argv[2:]:
        for base, _, names in os.walk(root):
            for f in sorted(names):
                ext = os.path.splitext(f)[1]
                if ext not in (".acp", ".acg", ".acs"):
                    continue
                path = os.path.join(base, f)
                try:
                    p = parse(path)
                except Exception as e:  # report and continue
                    bad.append((path, repr(e)))
                    continue
                counts[ext] = counts.get(ext, 0) + 1
                if ext == ".acp":
                    stats["samples"] += p["full"]["num_samples"]
                    stats["comp"] += p["full"]["compressed"]
                    stats["one_n"].add(p["one"]["num_samples"])
                    stats["channels"].update(p["full"]["channels"] + p["one"]["channels"])
    print("decoded exactly:", ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    print(f"acp: {stats['samples']} Full samples, {stats['comp']} compressed sets,"
          f" One-set sample counts {sorted(stats['one_n'])}, {len(stats['channels'])} distinct channels")
    for p, e in bad:
        print("  FAIL", p, e)
    print(f"{len(bad)} failures")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
