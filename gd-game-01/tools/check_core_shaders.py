#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# The post-processing shaders exist twice: local's GLSL 330 (render/post_fx.cpp,
# the True HDR pass in render/renderer.cpp) and their gd-shader-dialect twins
# for gd-engine-core (render/core_post_fx.cpp). Only the declarations differ
# (uniforms in a std140 block, GD_* macros); every other line must be the same
# text, or the core backends would no longer draw local's pixels. This
# compares them with the declarations removed. Exit status: the number of
# shaders that differ.
#
#   gd-game-01/tools/check_core_shaders.py
import re
import sys
from pathlib import Path

src = Path(__file__).resolve().parent.parent / "engine" / "src" / "render"
PAIRS = [(n, "post_fx.cpp", f"k{n}", f"k{n}Fs") for n in
         ("Down", "Up", "Composite", "Ao", "StarBlur", "Finish", "Taa", "Fxaa", "Downsample", "Easu", "Rcas", "Lum", "Adapt", "Copy")]
PAIRS.append(("HdrOut", "renderer.cpp", "kHdrFragment", "kHdrOutFs"))


def shader(text, name):
    m = re.search(r'const char\* ' + name + r' = R"\((.*?)\)";', text, re.S)
    if not m:
        sys.exit(f"check_core_shaders: {name} not found")
    return m.group(1)


def body(s):
    out, in_block = [], False
    for line in s.splitlines():
        t = line.strip()
        if in_block:
            in_block = not t.startswith("};")
            continue
        if t.startswith("GD_UNIFORMS("):
            in_block = not t.endswith("};")
            continue
        if (not t or t.startswith("#version") or t.startswith("uniform ") or t.startswith("GD_") or
                re.match(r"(in|out) vec[24] (vUv|fragColor);", t)):
            continue
        out.append(line.rstrip())
    return out


core = (src / "core_post_fx.cpp").read_text()
bad = 0
for label, local_file, local_name, core_name in PAIRS:
    a = body(shader((src / local_file).read_text(), local_name))
    b = body(shader(core, core_name))
    if a != b:
        bad += 1
        print(f"DIFFERS {label}: {local_file} {local_name} vs core_post_fx.cpp {core_name}")
        for i, (x, y) in enumerate(zip(a + [""] * len(b), b + [""] * len(a))):
            if x != y:
                print(f"  line {i}: local: {x!r}\n          core:  {y!r}")
                break
print(f"check_core_shaders: {len(PAIRS) - bad} / {len(PAIRS)} shaders match")
sys.exit(bad)
