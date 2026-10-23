# GH1 PS2 lighting, colour scaling and fog (VU1 microcode traced)

Unless marked *(inference)*, everything here was read from code: EE decompilation and VU1 microcode I disassembled myself.

## 0. Where the VU1 microcode is

- The ELF has a `.vutext` section at 0x2e4a20 (size 0x3710), plus `.DVP.overlay..*` / `.DVP.ovlytab` sections. `.vutext` is a single DMA `cnt` packet (tag qwc 0x36d). It holds 18 VIF `MPG` commands that load VU1 micro memory 0x139..0x800 (instruction units). The packet ends with `BASE 0`, `OFFSET 0x14a` and `STCYCL 1,1`.
- The overlay names give byte addresses. Divided by 8 they are the MSCAL/JALR entry points: 0x3e28/8 = **0x7c5**, 0x3760/8 = **0x6ec**, 0x21b0/8 = **0x436**.
- Tools, all in the scratchpad: `vif.py` extracts the image to `vu1.bin`; `vudis.py <start> <end>` is a small VU disassembler I wrote; `all.txt` is the full listing.
  - (scratch) vif.py
  - (scratch) vudis.py
  - (scratch) all.txt

### VU1 data-memory map (qword addresses)

| addr | contents | written by |
|---|---|---|
| 0x294..0x2a3 | 4 skin bone matrices (offset·boneWorld) | PsMesh_v5 0x2ec320 (skinned branch, `0x6c140294`) |
| 0x2a4..0x2a7 | mesh world rows 0,1,2 + translation (identity for skinned meshes) | PsMesh_v5 (`0x6c0402a4`, via FUN_001da848 = world unless billboard) |
| 0x2a8 | per-batch header | PsMesh_v5 |
| 0x2a9 | ambient (r,g,b,**1.0**) | PsEnviron_v5 (`0x6c0702a9`, 7 qwords) |
| 0x2aa..0x2ac | directional light colours (r,g,b,**0**); unused slots zeroed | PsEnviron_v5 |
| 0x2ad..0x2af | directions **transposed**: X row (x0,x1,x2), Y row, Z row | PsEnviron_v5 |
| 0x2b0 | x = lighting entry point, y = vert_amb, z = vert_dyn, w = use_environ | FUN_002ebcb0 (`0x6c0802b0`) |
| 0x2b1 | x = texgen entry point | FUN_002ebcb0 |
| 0x2b2 | material colour (mat+0x30..0x3c); w is forced to 0.035 when TFX = HIGHLIGHT | FUN_002ebcb0 |
| 0x2b3..0x2b6 | texgen matrix | FUN_002ebcb0 |
| 0x2b7 | **colour→GS scale** (per pass) | FUN_002ebcb0, from pass+0x50 (built in FUN_0019d3b8) |
| 0x2b8 / 0x2b9 | xyz = screen scale / offset (camera, masked unpack `0x7c0202b8`); w = **fog scale / fog offset** (environ, masked `0x700202b8`, mask 0x3f3f3f3f = w only) | PsCam_v5 / PsEnviron_v5 |
| 0x2ba | camera world position | PsCam_v5 |
| 0x2bc..0x2bf | view-projection | PsCam_v5 |

- Vertex layout in VU memory, 4 qwords per vertex, from the unpacks built at load time in FUN_002e8130:
  - `+0` position
  - `+1` normal
  - `+2` colour: the file's 4 floats (RGBA, or bone weights)
  - `+3` uv
- Main loop (0x19b):
  1. `JALR vi1`: the batch's skin program (0x614 none; 0x369 / 0x373 / 0x394 / 0x3c6 for 1–4 bones).
  2. `JALR vi14`: the lighting program from 0x2b0.x.
  3. `JALR vi15`: texgen.
  4. Transform and kick.

## 1. Lighting: exact formula

### Program selection

**PsEnviron_v5 (0x1a2528)**, run when an Environ is drawn. It walks the environ's light list (env+0x44):

- The loop stops at the **first point light (type 0)**. If there is one:
  - Program **0x436** (point) is used, and **no directional light is uploaded at all**.
  - Upload: colour → 0x2aa; then `(px,0,0,-1/range)`, `(py,0,0,range²)`, `(pz,0,0,0)` → 0x2ad..0x2af. The position is world +0x90..0x98.
- Otherwise it takes up to **3 type-1 lights in list order**. For each:
  - `D_i = -normalize(world row 1)`: `lqc2 world+0x70`, VU0 rsqrt, then three `neg.s` at 0x1a28d0/0x1a28dc/0x1a28e8.
  - Colour (light+0xc0..0xc8) is copied **raw**, with w forced to 0.
  - Program **0x6ec**.
- With no lights at all, program **0x7c5** (ambient only).
- Ambient: env+0x50..0x58 raw, w = 1.0.
- **No scaling** of colours anywhere on the EE: no ×0.5, ×128 or /255. Light colours > 1 are legal and are used as is.
- It also sets `DAT_002f1bf4 = env` (the current environ for fog) and `DAT_002edda8 = program`.

**FUN_002ebcb0 (material pass setup, called from PsMesh_v5)** sets the 0x2b0 qword:

- If the pass is "unlit" (`pass[1] == 0`): x = 0x614 (just `JR vi2`), so the vertex colour slot passes through untouched.
  - `pass[1] = use_environ || !vert_amb` (FUN_0019d3b8). Decal (TFX 1) passes are also forced unlit.
- Otherwise:
  - x = `DAT_002edda8` if `use_environ`, else **0x7c5**.
  - y = vert_amb (mat+0x54), z = vert_dyn (mat+0x58), w = use_environ (mat+0x40).

### Directional program 0x6ec

Let `M` be rows 0x2a4..0x2a6, applied row-vector style: `n_w = n·M`, with **no renormalisation**. For skinned meshes M = identity and the skin program has already put the normal in world space.

1. The VU computes `vf13/14/15 = D·M` so that, per vertex, `I_i = max(0, n_w · D_i)` with `D_i = -Ŷ_i` (`MAXx … vf0x`).
2. Then:

```
AmbBase = use_environ ? ambient : (1,1,1)                (w: 1)
ambTerm = (vert_amb ? vertColor : matColor) * AmbBase
dynTerm = (vert_dyn ? vertColor : matColor) * Σ_i lightColor_i * I_i     (light w = 0)
out     = min(ambTerm + dynTerm, 1.0)     per component incl. alpha (MINIw vf0w), no lower clamp
```

The four code paths match this exactly:

| vert_amb | vert_dyn | entry |
|---|---|---|
| 0 | 0 | 0x6fc |
| 1 | 0 | 0x72e |
| 0 | 1 | 0x761 |
| 1 | 1 | 0x795 |

- Alpha out = (vert_amb ? vertex a : mat a) × 1, clamped to 1.
- The result is written back to the vertex colour slot (+2).
- Lighting is per vertex, so the clamp happens **before** Gouraud interpolation.

### Ambient-only program 0x7c5

- vert_amb == 0: `out = min(matColor * (use_environ ? ambient : 1), 1)`.
- vert_amb == 1: `out = min(vertColor * ambient, 1)` if use_environ; otherwise the vertex colour is left untouched.
- vert_dyn is ignored here.

### Point program 0x436

Only one point light is supported, and its presence disables directional lights. Per vertex, in **object space**: the light position is taken relative to the translation in 0x2a7, then rotated by Mᵀ.

```
d   = Lobj - P            (unnormalised)
contrib = lightColor * (N·d) * (1/|d| - 1/range)  = lightColor * (N·d̂) * (1 - |d|/range)
```

- The contribution is zeroed when |d|² > range² or N·d < 0 (S-flag tests; the exact flag bits are *(inference)*).
- It is combined with the ambient term and the vert_amb / vert_dyn flags as above, then clamped to 1.
- The engine's current point formula (renderer.cpp:50-53) already matches.

### Vertex colour

- For **skinned meshes, every skin program (0x369/0x373/…) overwrites slot +2 with (1,1,1,1)** after using the bone weights. Characters therefore have vertexColor = 1.
- All character materials checked have `use_environ = 1`, `vert_amb = vert_dyn = 0` and colour 1 (eyes 0.7). That gives:

  `charColour = min(matColor * (ambient + Σ lightColor_i * max(0, N·(-Ŷ_i))), 1)`

  This is the same structure as our formula. **The blow-out is not a missing scale factor; it is the light direction sign (section 2).**

### Colour → GS

The transform loop at 0x1c6/0x1d1 does `FTOI0(colour * vf11)`, where vf11 is 0x2b7, from FUN_0019d3b8 pass+0x50..0x5c:

| case | (r,g,b,a) scale |
|---|---|
| no texture | (255,255,255,128) |
| textured, stage-0 blend 3 ("Multiply2") | (255,255,255,128) |
| other textured stages | (128,128,128,128) |

- TFX comes from table `DAT_002edd80 = {0,1,0,0,0,0}` indexed by stage blend: 1 → DECAL, everything else → MODULATE. Stage blend 2 on a material whose mat blend is also 2 and vert_amb == 0 gets HIGHLIGHT (TFX 2) with alpha 0.035.
- With MODULATE the GS computes `tex * col / 128`. So:
  - Normal textured surfaces: final = `tex × min(lit, 1)`. **There is no 2× overbright.** 0x80 = 1.0 is the maximum because the VU clamps to 1 before ×128.
  - Stage blend 3 ("Multiply2"): ×255, so `tex × min(lit,1) × ~2` (255/128) and the GS clamps to 255.
  - Untextured: `min(lit,1) × 255`.
  - Alpha: always ×128 (0x80 = 1.0).
- Characters use stage blend 2, so ×128; their eyes use 3, so ×2.

## 2. Direction sign: settled

- The VU computes `max(0, n_w · D)` with `D = -normalize(world +Y row)`.
- A surface is lit when its normal points along **−Y** of the light. **The light travels along +Y** of the light's world matrix.
- FINDINGS.md ("lit surfaces face +Y, light travels along −Y") has it backwards.
- The engine has it backwards too:
  - `scene_renderer.cpp:178` stores `dir = -Ŷ`.
  - `renderer.cpp:48` uses `dot(n, -dir) = dot(n, +Ŷ)`.
  - **Fix: use `max(0, dot(n, -Ŷ))`**, e.g. keep `dir = -Ŷ` and use `dot(n, dir)` in the shader.

The data agrees with this:

- big_club `lighting.rnd_ps2` `spotlight01.lit` has row1 = (0,0,−1). Under the correct convention it shines straight **down**, which resolves the "overhead spotlight disagrees" note.
- Under the correct convention every crowd light and `fanlight` points downwards (row1 z < 0) as well.
- Cameras look along +Y (engine `cameraView`). Big club attaches `rim_light.lit` (colour **7,7,0.31**) to the camera; its row1 is ≈ (−0.62,−0.74,−0.28), i.e. pointing back toward the viewer.
  - With the correct sign it only lights normals facing away from the camera (silhouette edges and back faces), which is a rim light.
  - With our sign it floods every camera-facing surface with 7.0, which explains the white characters.
- Lights with scaled axes (`char_light02a` row1 length ≈ 2.4) are why the EE normalises.

Two more points:

- Light colours are often animated by LightAnim; the file colour is only the start value.
- *(Minor quirk.)* In the 2-bone skin program 0x373, the normal uses only the dominant bone's rotation (no blending). When bone 0 dominates, its z row is taken from bone 1 (`MADDz vf23, vf11` at 0x388). This looks like a microcode bug; it is cosmetic.

## 3. Fog

PsEnviron_v5:

- `FOGCOL` (GS reg 0x3d) = `int(fog_color*255)` per channel (r | g<<8 | b<<16).
- If `fog_enable` (env+0x60):
  - `scale = 255/(fog_start(+0x64) − fog_end(+0x68))`
  - `offset = −fog_end·scale`
- Otherwise `scale = 0`, `offset = 255`.
- Both go to the w components of 0x2b8 / 0x2b9.

VU (0x1c5/0x1c9/0x1d0/0x1dd):

```
F = clamp(offset + scale * w_clip, 0, 255) = clamp(255 * (fog_end - w)/(fog_end - fog_start), 0, 255)
```

- F is written as XYZF2 F (via FTOI4) and is per vertex, Gouraud-interpolated.
- F = 255 (no fog) at w ≤ start; F = 0 (full fog colour) at w ≥ end.
- GS result: `C = (F·C + (255−F)·FOGCOL) >> 8`.
- `w_clip` = depth along the camera's view axis, not Euclidean distance *(inference from the projection layout in PsCam_v5)*.
- FGE (PRIM bit 0x20) is set in FUN_002ebcb0 only when `pass[2] != 0` **and** the current environ (`DAT_002f1bf4`, the last drawn) has fog enabled.
  - `pass[2]` is 1 for pass 0, and for later passes unless the stage blend is 0, 2, 4 or 5.
  - So fog follows the same "current environ" selection as lighting.
- Data: singer*/stagechar envs have fog on, 500 → 3000, colour ≈ (0.024, 0.224, 0.167).

## Key addresses

- PsEnviron_v5 0x1a2528.
- Mat pass setup FUN_002ebcb0; pass build FUN_0019d3b8; PsMesh_v5 0x2ec320; mesh packet build FUN_002e8130.
- PsCam_v5 0x19c7b8.
- Microcode: lighting 0x7c5 / 0x6ec / 0x436; no-op 0x614; skin 0x369 / 0x373 / 0x394 / 0x3c6; main loop 0x19b.
