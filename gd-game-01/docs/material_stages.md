# GH1 PS2: multi-stage materials (RndMat stages / passes) and texgen

[C] means it was read from code: the EE decompile (analysis/decomp), my disassembly of the ELF (scratchpad `fdis.py`), the VU1 listing (scratchpad `all.txt`, see docs/lighting.md §0), or the shipped data. [I] means inference.
This doc builds on docs/render_gems_dof.md §1 (wrap, texgen 1/4) and docs/lighting.md (VU1 lighting and colour scale).

---------------------------------------------------------------------------------------------------
## 0. Summary

- **The PS2 has no multitexturing. Every stage is a separate full geometry pass.**
  - PsMesh_v5 (0x2ec320) loops `next = FUN_002ebcb0(mat, pass, &prim)` and re-sends the whole mesh (FUN_002ec1b0) once per pass until `next == 0`. [C]
  - PsMultiMesh_v5 (0x1a34a0), PsParticleSys (0x1a2fd8) and PsRnd_v8 (0x19b598) run the same loop. [C]
- The mat's `multipass` field (0 Multitexture, 1 MultipassSrc, 2 MultipassLit) only picks the **texture function of passes 1+**:
  - 0 and 1 → DECAL: unlit texture colour.
  - 2 → MODULATE: texture × lit colour, like pass 0.
  - **Multitexture and MultipassSrc render identically.** [C]
- The `blend` enum has two roles:
  - As a **framebuffer blend** (GS ALPHA_1): pass 0 uses **mat.blend**; pass k ≥ 1 uses **stage[k].blend**.
  - As a **texture-combine selector**: only **stage[0].blend** is used this way (1 → DECAL, 3 → ×2 colour, others → MODULATE). That is why the strings read "Add (Multiply)" and "SrcAlpha (Multiply2)". [C]
- Depth: pass 0 follows zmode. Passes 1+ never write Z, and test GEQUAL (PS2), which is GL_LEQUAL, except in zmode 0/3. [C]
- Texgen 5 (Environ) is a per-vertex **reflection-vector map in world space**: `uv = 0.5 + 0.5·R'.xy/|V'|`, with R' = reflect of the eye vector in a stage-rotated, swizzled frame (§4.3). [C]

---------------------------------------------------------------------------------------------------
## 1. The pass array (PsMat)

- PsMat_v8 (0x19cf00): after RndMat load, it sizes `mat+0x70` (vector of 0xa0-byte pass records) to `max(1, stageCount)`. [C]
- FUN_002ebcb0 (0x2ebcb0, called per draw per pass):
  - If `mat+0x64 & 2` (dirty), it rebuilds every pass with **FUN_0019d3b8(mat, i)**.
  - Then it uploads the pass's GS registers and VU constants, unless the same pass was the last one sent. [C]

### 1.1 Which passes are drawn [C, FUN_0019d3b8 tail]
- Pass 0 is always drawn.
- `pass[i]+0x10` = the index of the next stage j > i with **stage[j].blend ≠ 0**. If there is none, it is 0, which ends the loop.
- **A stage whose blend is 0 ("Dest") at index ≥ 1 is skipped entirely.** No shipped stage ≥ 1 uses blend 0.

### 1.2 Pass record (0xa0 bytes) written by FUN_0019d3b8 (0x19d3b8) [C]

| off | meaning | consumer |
|---|---|---|
| +0x00 | ABE (alpha blend enable): 0 for blend 1 (Src), else 1 | PRIM bit 6 (FUN_002ebcb0 end) |
| +0x04 | lit: `use_environ \|\| !vert_amb`, forced 0 when TFX = DECAL | VU 0x2b0.x program (lighting.md) |
| +0x08 | fog allowed: pass 0 → 1; pass k → 1 only for blend 1 / 3, 0 for blend 0/2/4/5 | PRIM FGE (bit 5), only if environ fog is on (env+0x60) |
| +0x0c | VU texgen entry (0x614 / 0x17c / 0x347 / 0x410 / 0x139) | VU 0x2b1.x |
| +0x10 | next pass index | return value |
| +0x14 | TFX index into `DAT_002edd80 = {0,1,0,0,0,0}` (0 MODULATE, 1 DECAL), or 2 = HIGHLIGHT | TEX0 bits 35-36 (FUN_002ec728) |
| +0x18 | texture (null → untextured pass) | TEX0/TME |
| +0x20 | GS **ALPHA_1** (u64) | reg 0x42 |
| +0x28 | GS DIMX dither matrix (blend 0/2/4/5: 0x1212303012120303; 1/3: 0x2435607135247160; render-target tex: 0x3333…) | reg 0x44; irrelevant on PC |
| +0x30 | GS **TEST_1** (alpha + Z test) | reg 0x47, mask 0x6ffff |
| +0x38 | CLAMP_1: `wrap ? 0 : 5` | reg 0x08 |
| +0x40 | ZBUF_1 ZMSK (bit 32 = no Z write) | reg 0x4e |
| +0x50..0x5c | colour→GS scale (r,g,b,a) | VU 0x2b7 |
| +0x60..0x9c | texgen matrix rows + offset | VU 0x2b3..0x2b6 |

Other registers set per pass by FUN_002ebcb0 [C]:
- FBA_1 (0x4a) = `!alpha_write` (mat+0x60). FBA forces the written alpha MSB to 1.
- TEST DATE (0x4000) is set if mat+0x80 ≠ 0. That is a runtime field; its setter was not traced. [I: shadow/stencil use]
- Neither affects colour on PC.

---------------------------------------------------------------------------------------------------
## 2. Blend enum → GS ALPHA (framebuffer blend)

GS: `C = ((A − B) · C' >> 7) + D`. A, B, D ∈ {0 Cs, 1 Cd, 2 zero}. C' ∈ {0 As, 1 Ad, 2 FIX}. Alpha 0x80 = 1.0.

| value | name (FUN_001bfba0 strings @0x321f90..) | ALPHA_1 [C 0x19d428 switch] | equation | GL |
|---|---|---|---|---|
| 0 | Dest | 0x4a: A=0,B=0,C=As,D=Cd | `Cd` (colour unchanged) | `glBlendFunc(ZERO, ONE)` |
| 1 | Src | ABE = 0 | `Cs` | blending off |
| 2 | Add (Multiply) | 0x80_00000068: A=Cs,B=0,C=FIX 0x80,D=Cd | `Cs + Cd` | `(ONE, ONE)`, ADD |
| 3 | SrcAlpha (Multiply2) | 0x44: A=Cs,B=Cd,C=As,D=Cd | `(Cs−Cd)·As + Cd` | `(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)` |
| 4 | SrcAlphaAdd | 0x48: A=Cs,B=0,C=As,D=Cd | `Cs·As + Cd` | `(SRC_ALPHA, ONE)` |
| 5 | Subtract | 0x80_000000a1: A=Cd,B=Cs,C=FIX 0x80,D=0 | `Cd − Cs` | `(ONE, ONE)`, `GL_FUNC_REVERSE_SUBTRACT` |

- Pass 0 uses **mat.blend** (`mat+0x24`); pass k ≥ 1 uses **stage[k].blend** (`stage+0x00`). [C]
- The results are clamped to [0,255] (COLCLAMP). The GS blends only RGB; the written alpha is As.
- As is on the GS 0..0x80 scale, so As can exceed 1.0 when texture alpha > 0x80. [I] Our texture decoder should already normalise 0x80 → 1.0.

## 3. Texture function, colour scale, lighting, fog per pass [C FUN_0019d3b8 + FUN_002ebcb0]

`idx` = the TFX selector:
- Pass 0: idx = **stage[0].blend**.
- Pass k ≥ 1: idx = 1 if multipass ∈ {0, 1}, or 2 if multipass = 2 (MultipassLit).

| idx | TFX | colour scale (0x2b7) | lit (`+0x04`) | GS result |
|---|---|---|---|---|
| 1 | DECAL | 128 | **forced unlit** | `Cs = tex.rgb`, `As = tex.a` (TCC=1 [I]) |
| 3 | MODULATE | **(255,255,255,128)** | mat flags | `Cs = tex.rgb · min(lit,1) · 255/128` (≈ ×2, clamped), `As = tex.a · a` |
| 0, 2, 4, 5 | MODULATE | 128 | mat flags | `Cs = tex.rgb · min(lit,1)`, `As = tex.a · a` |
| 2 + HIGHLIGHT rule | HIGHLIGHT | 128 | mat flags | `Cs = tex.rgb · lit + Af`, `As = tex.a`; Af = mat alpha forced to **0.035** (VU 0x2b2.w = 0x3d0f5c29) |

- **Untextured pass** (stage tex null, or that stage's blend is 0, or no stages):
  - TME off, scale (255,255,255,128), texgen 0x614.
  - `Cs = min(lit,1)`, `As = a`.
  - Stage-1 passes with an empty tex really do this, e.g. light_bright.mat and lens.mat **add the lit colour a second time**.
- **HIGHLIGHT rule**: `blend_of_this_pass == 2 && vert_amb == 0 && idx == 2`.
  - "blend_of_this_pass" is mat.blend for pass 0 and stage.blend for later passes.
  - Pass 0 case: mat blend Add + stage0 blend Add + !vert_amb, e.g. floorspot_glow.mat.
  - Pass k case: stage Add + MultipassLit + !vert_amb, e.g. gem_red/orange/yellow.mat specular passes.
  - The +0.035·128 ≈ 4/255 offset is visually negligible; MODULATE is a fine approximation.
  - PsParticleSys resets TFX to MODULATE (and DIMX) for HIGHLIGHT passes (0x1a2fd8). [C]
- **Lighting** runs again per pass (the VU re-transforms every pass). It uses the same lighting.md formula with the mat colour, so MultipassLit passes get the same lit colour as pass 0. DECAL passes skip it, and their colour is irrelevant. [C]
- **Fog**:
  - Pass 0 is always fogged (when the environ fog is on).
  - Passes 1+ are fogged only when their blend is Src or SrcAlpha. Additive and subtract passes are not fogged.
  - Particles clear FGE. [C]

## 4. Texgen (stage+0x04) — VU programs, matrices

Common facts [C]:
- Vertex layout in VU memory: +0 pos, +1 normal, +2 colour, +3 uv.
- `M` = mesh world rows 0x2a4..0x2a7 (rot rows + translation; identity for skinned meshes, where the skin program already put pos/normal in world space).
- `E` = camera world position 0x2ba.
- The uploaded rows `U0, U1, U2` = 0x2b3..0x2b5 and the offset `O` = 0x2b6 (pass+0x60..0x9c).
- All math is row-vector style: `w·U = w.x·U0 + w.y·U1 + w.z·U2`.
- The stage xfm S is the stage's 3×3 rotation rows. In file floats: `S[0] = x0..x2`, `S[1] = x3..x5`, `S[2] = x6..x8`, translation `t = x9..x11`.
- Only the xy of the result are written to the UV slot (SQ also stores zw).
- The **stage xfm is ignored for texgen 0**.

| v | name (strings @0x322058..) | VU entry | used in data |
|---|---|---|---|
| 0 | None | 0x614 (`JR vi2`) | 1153 stage0 + 48 stage1 |
| 1 | Xfm | 0x17c | 227 + 4 |
| 2 | Sphere | 0x347 | 4 + 3 |
| 3 | Projected | 0x410 | 0 |
| 4 | XfmOrigin | 0x17c | 0 |
| 5 | Environ | 0x139 | 14 + 50 |

### 4.1 Texgen 1 / 4
Unchanged from render_gems_dof.md §1.2. They apply to any pass the same way.

### 4.2 NPOT textures [C 0x19dc5c..0x19de44]
- If the texture's width or height is not a power of two (FUN_001d6388), U and O are scaled per output axis by `w/pow2ceil(w)` and `h/pow2ceil(h)`. Texgen 0 becomes 0x17c with that scale.
- This is a PS2 storage artefact. **Ignore it on PC.**
- Every env/sphere texture in the data is a power of two (gem_specular 32², sphere_REF01 64², guitar_envi01 64², …).

### 4.3 Texgen 5 — Environ (reflection map) [C]
**EE side** (FUN_0019d3b8 case 5, asm 0x19da98..0x19dc50):
1. Rows `X_i` = **column i of S**: `X_i = (S[0][i], S[1][i], S[2][i])`. Verified by the lwc1/swc1 pairs at 0x19dab0..0x19db18.
2. Each row is multiplied by the constant F, with rows (1,0,0), (0,0,1), (0,−1,0). VMULAx/VMADDAy/VMADDz at 0x19db4c.., so `U_i = X_i·F = (S[0][i], −S[2][i], S[1][i])`.
3. Offset `O = (0.5, 0.5, 0)`.

So for any world vector w: **`T(w) := w·U = ( (S·w).x, −(S·w).z, (S·w).y )`**, where `(S·w)_k = Σ_i S[k][i]·w_i`.

**VU program 0x139..0x17a**:

```
C   = M3 · U            (each M row r → r·U);    tT = M.row3 · U ;   E' = E · U
per vertex:  P' = p·C + tT          = T(P_world)
             N' = n·C               = T(n·M3)            (n not renormalised)
             V  = E' − P'           = T(E − P_world)
             Q  = O.x / sqrt(V·V)   (RSQRT, O.x = 0.5)
             R  = 2(N'·V)N' − V
             uv = O.xy + R.xy · Q   = 0.5 + 0.5·R.xy / |V|
```

- In words, R is the reflection of the surface→eye vector about the normal, so it points away from the surface. The world reflection is taken into the stage frame S, and then **u = 0.5 + 0.5·R̂.x and v = 0.5 − 0.5·R̂.z**, where z is Milo world up. A reflection pointing up maps to the top of the texture (t = 0).
- The reflection is computed **after** T. When S is not orthonormal (e.g. the guitars' `S = diag(1, 1, 0.1..0.7)` or the tails' `diag(0.5, 1, 1)`), the result differs from applying S to a world R. To be exact, transform N and V by T first, then reflect.
- The VU's `uv` is perspective-correct per-vertex ST, so compute it in the vertex shader, not per pixel.
- Representative S:
  - gem2.mat: rotation about Y by +20° (rows (.939,0,−.342), (0,1,0), (.342,0,.939)).
  - star2.mat: a general rotation.
  - gem_bonus.mat: rotation about X, animated by gem_bonus.mnm at −2π per 100 frames.
  - Guitars and tails: axis scales.

### 4.4 Texgen 2 — Sphere [C, euler convention C; meaning I]
**EE** (FUN_0019d3b8 case 2): `U_i = X_i` (S transposed, no swizzle).

**Per upload** (FUN_002ebcb0 → **FUN_0019cf58**), with `Cam` = the current camera's world rotation rows (`DAT_002f1be8+0x50` → cam+0x60/0x70/0x80):
1. `A = Cam · U` (rows `A_k = Cam_k · U`).
2. Euler from A (FUN_0024a350): `ex = asin(A[1][2])`, `ez = atan2(−A[1][0], A[1][1])`, `ey = atan2(−A[0][2], A[2][2])`.
3. Wrap ex and ez to [−π, π) and **halve** them; ey is unchanged.
4. Rebuild `A'` (FUN_0024acd0).
5. `B = Camᵀ · A'`.
6. Rows `U_k = B_k·G`, with G rows (0.5,0,0), (0,0,1), (0,−0.5,0), i.e. `U_k = (0.5·B_k.x, −0.5·B_k.z, B_k.y)`.
7. `O = (0.5, 0.5, 0)`.

**VU 0x347**: `uv = O.xy + (n·M3·U).xy` (normal only, no position or eye).

- [I] This is a camera-relative sphere map whose pitch and yaw follow the camera at half rate.
- Approximation: `u = 0.5 + 0.5·n_view.x`, `v = 0.5 − 0.5·n_view.up`.
- It is used only by 7 mats (characters' enviro, basement green/red/yellow).

### 4.5 Texgen 3 — Projected (unused in data) [C]
- `U = FUN_0024b668(S)`, the inverse of the scaled rotation (transpose with 1/|row|²).
- `O = (−t)·U`.
- Then the rows and O are multiplied by the global `0x364750` matrix, default rows (1,0,0), (0,0,1), (0,1,0) (FUN_0019dfd0).
- VU 0x410: `uv = (P_world·U + O).xy`.

---------------------------------------------------------------------------------------------------
## 5. Depth and alpha test per pass [C FUN_0019d3b8 0x19d5b8 switch]

PS2 Z is larger = nearer, so GS GEQUAL = GL_LEQUAL and GREATER = GL_LESS. ZTE stays on (bit 16 is outside the mask).

| zmode | pass 0: test / write | pass ≥ 1: test / write |
|---|---|---|
| 0 Disable | ALWAYS / no | ALWAYS / no |
| 1 Normal | GREATER (GL_LESS) / **yes** | GEQUAL (GL_LEQUAL) / no |
| 2 Transparent | GEQUAL (LEQUAL) / no | GEQUAL / no |
| 3 Force | ALWAYS / **yes** | ALWAYS / no |
| 4 Decal | GEQUAL (LEQUAL) / **yes** | GEQUAL / no |

Alpha test (TEST_1 low bits), the same for every pass:
- `alpha_cutout` (mat+0x5c): ATE=1, ATST=GREATER, AREF=0, AFAIL=KEEP. **Discard where final alpha == 0** (no colour, no Z).
- No cutout and the pass blend = 3 (SrcAlpha): 0x200d = alpha > 0, else AFAIL=ZB_ONLY. Alpha-0 pixels still write Z (when ZMSK allows) but no colour. That equals plain blending, so in GL do nothing special.
- Otherwise: no alpha test.

Consequences:
- Pass 1+ of a zmode-1 mat redraws on the depth pass 0 wrote (equal Z → LEQUAL passes). There is no z-fighting.
- Where pass 0 was cut out, pass 1 still draws when it is nearer than what is behind.

---------------------------------------------------------------------------------------------------
## 6. Inventory (all `Mat` records in extracted/*.rnd_ps2 and *.gh)

Script: `(scratch) ms/inv.py` (tools/rnd.py + rnd_classes.material.decode_mat). Full output is in `.../scratchpad/ms/inv.txt`, and the per-name list is in `.../scratchpad/ms/uniq.txt`.

- 2072 Mat records: 674 with 0 stages, 1294 with 1, **103 with 2**, **1 with 3** (fest atom_bomb.mat).
- multipass: Multitexture 1961, MultipassSrc 91, MultipassLit 20.
- Stage ≥ 1 blends used: Add 91, SrcAlphaAdd 10, SrcAlpha 4. **Dest and Subtract are never used for stages ≥ 1.**
- Texgen: Environ 14 at stage 0 + **50 at stage 1**; Sphere 4 + 3; Xfm 227 + 4; Projected and XfmOrigin 0.
- Two-stage combinations (multipass / stage1 blend / stage1 texgen):

| mp | st1 blend | st1 texgen | count |
|---|---|---|---|
| 1 | Add | Environ | 46 |
| 1 | Add | None | 28 |
| 1 | SrcAlphaAdd | None | 8 |
| 0 | Add | None | 4 |
| 1 | Add | Xfm | 3 |
| 1 | Add | Sphere | 3 |
| 2 | Add | Environ | 3 |
| 2 | Add / SrcAlpha / SrcAlphaAdd | None | 2 / 2 / 2 |
| 0 | Add | Xfm / Environ | 1 / 1 |
| 1 | SrcAlpha | None | 1 |

- By area (multi-stage or texgen 2/5): guitars 40, track 29, small_club 10 (+6 multi), basement 9, hud 6, big_club 5, arena 5, theatre 2, fest 2, characters 8, ghui 1.

### 6.1 Representative materials and what the PS2 does

| mat (file) | data | passes |
|---|---|---|
| **gem2.mat** (track) | mb 4 SrcAlphaAdd, z 3 Force, mp 0, env 0, va 0; st0 b2 **tg5** gem_specular.tex, S = rotY(20°) | 1 pass: MODULATE (lit = ambient-only 0x7c5, matColor 1 → white), `Cd += tex.rgb·tex.a`, Z ALWAYS **+ write**, fogged |
| **star2.mat** (track) | same as gem2, with S = general rotation | same as gem2 |
| gem_bonus2.mat | mb 4, z 3, col (.75,1,1); tg5 line01.tex, S = rotX(~125°) | as gem2, tinted |
| gem_bonus.mat | mb 3, z 1, env 1; st0 b2 tg5 gem_bonus.tex (rotX, animated) | 1 pass, env-mapped body |
| **gem_red/orange/yellow.mat** | mb 3, z 1, **mp 2**, env 1; st0 b2 gem_<c>.tex; **st1 b2 tg5 gem_specular** | P0 lit tex SrcAlpha, Z write; **P1 Add, MODULATE lit (HIGHLIGHT), env map, LEQUAL no write, no fog** |
| gem_miss.mat | mb 3, mp 1, col .2; st1 b2 tg5 specular.tex | P1 Add DECAL (unlit) env |
| tail_<c>/star/bonus.mat | mb 3, mp 0, va 1; st0 **b3** tg5 gem_star.tex, S = diag(.5,1,1), wrap 0 | 1 pass, ×2 colour, env UV (render_gems_dof §2) |
| phrase.mat | st0 b2 tg1 mop.tex (scroll), st1 b2 tg5 specular.tex, mp 1 | P1 Add DECAL env |
| nowbar.mat / now_ring_<c>.mat | st1 b2 tg0 `*_add.tex`, mp 1 | P1 Add DECAL, raw UV |
| gem_smasher_<c>.mat | st1 b2 tg0 smasher_add_<c>.tex, mp 1 | P1 Add DECAL |
| **snake_neon.mat** (small_club) | mb 3, z 2, **mp 2**, env 1, va 1; st1 **b4** snake_glow.tex | P1 `Cd += (tex·lit).rgb · tex.a·a`, lit (vertex colour × ambient) |
| snake_neon.mat (small_club_multi) | same, but **mp 1**, env 0 | P1 SrcAlphaAdd DECAL (unlit glow) |
| crown_sign / lightrig / lava / lightstring | st1 b4 `*_glow/_add.tex` | P1 SrcAlphaAdd glow |
| bones / swill (small_club) | mb 4 / 2, mp 2; st1 **b3** `*_glow.tex` | P1 SrcAlpha blend of the lit glow over pass 0 |
| hot_white / fire / suite / arena / tracklight | st1 b2 tg0 `*_add.tex` | P1 Add DECAL |
| **guitar_* (40)** | mb 3, mp 1; st1 b2 **tg5** guitar_envi01 / firebird_envi, S = diag(1,1,0.1–1) | P1 Add DECAL chrome reflection, vertical spread scaled by S[2][2] |
| window_cbgb.mat | st1 b2 tg5 sphere_REF01 | window + additive reflection |
| "Material #507" (small_club glass) | mb 3, z 2, col a .05, **st0 tex empty**; st1 b2 tg5 sphere_REF02 | P0 untextured lit colour at alpha .05; P1 Add DECAL reflection |
| basement 01/04 - Default | st0 empty, st1 tg5 sphere_REF01 | same as #507 |
| basement green/red/yellow | st1 b2 **tg2** sphere_REF01 | P1 Add DECAL sphere map |
| amp_chrome_base (hud) | mp 1; st1 **b4** specular2.tex, tg0 | P1 SrcAlphaAdd DECAL |
| amp_end_bar (hud) | mb 1, mp 0; st1 b2 specular2.tex | P1 Add DECAL |
| atom_bomb (fest) | 3 stages: b2 atom; b2 tg1 wisp_white (scale 2,3); **b3** atom_lines | P1 Add DECAL; P2 SrcAlpha DECAL |
| characters: earings, hair_enviro, necklace, enviro, fsinger_boot, shinplates, torsoenv | single stage tg5 / tg2, st0 b3 for most | 1 pass, ×2, env or sphere UV |

Note: gem_blue.mat and gem_green.mat do not exist. The track has gem.mat with 1 stage, plus gem_red/orange/yellow with the specular pass.

---------------------------------------------------------------------------------------------------
## 7. Implementation plan for our renderer (GLSL 330)

**Recommendation: real extra passes**, not one multitexture shader. Reasons:
- Each PS2 pass blends against the framebuffer with its own ALPHA, depth state, fog and clamp. Pass 1 of a SrcAlpha mat sees `lerp(Cd, C0, a0)`, not C0.
- The same VAO is reused, and only about 110 materials have 2+ stages, so the cost is negligible.
- A fused premultiplied shortcut (`ONE, ONE_MINUS_SRC_ALPHA` with `C0·a0 + C1`) is exact only for Add / SrcAlphaAdd second passes, with no fog and no clamp ordering. Keep it as a later optimisation only.

### 7.1 Data (scene.h / scene.cpp)
- Keep `MatObj::multipass`; scene.cpp:176 currently discards it.
- At build, make `std::vector<Pass> passes` per DrawMesh, mirroring FUN_0019d3b8:
```
struct Pass { int tex; bool repeat; int texgen; float U[3][3]; float O[2];   // texgen matrix (CPU-built)
              Blend blend; bool lit; bool decal; float colorScale; bool fog;
              DepthFunc depthFunc; bool depthWrite; bool cutout; };
passes: i = 0, then every i>=1 with stages[i].blend != 0
blend      = i==0 ? mat.blend : stages[i].blend
idx        = i==0 ? stages[0].blend : (mat.multipass==2 ? 2 : 1)
textured   = !stages.empty() && stages[i].tex != "" && stages[i].blend != 0
decal      = textured && idx==1
lit        = (mat.useEnviron || !mat.vertAmb) && !decal
colorScale = textured && idx==3 ? 255/128 : 1       (untextured: GS 255 = 1.0, so 1 in our float pipeline)
fog        = i==0 || blend==1 || blend==3
depth      = table §5 (pass 0 vs pass>=1)
```
- Rebuild U/O when the stage xfm changes (MatAnim), the same trigger as `mat+0x64 & 2`.

### 7.2 Vertex shader: texgen per pass
Uniforms: `int uTexgen; mat3 uTexU; vec2 uTexO; vec3 uEye;` (camera world position, in the same space as `uModel`).

Build `uTexU` on the CPU as `mat3(U0, U1, U2)`, with each **row of §4 as a GLSL column**, so `uTexU * w == w·U`.

```glsl
vec3 wp = (uModel * pos).xyz;           // after skinning (skinned: uModel = I)
vec3 wn = mat3(uModel) * normal;        // NOT renormalised (VU doesn't)
if (uTexgen == 5) {                     // Environ, VU 0x139
    vec3 N = uTexU * wn;
    vec3 V = uTexU * (uEye - wp);
    vec3 R = 2.0 * dot(N, V) * N - V;
    vUv = uTexO + R.xy * (0.5 * inversesqrt(max(dot(V, V), 1e-12)));
} else if (uTexgen == 2) {              // Sphere, VU 0x347 (uTexU from FUN_0019cf58 per draw)
    vUv = uTexO + (uTexU * wn).xy;
} else if (uTexgen == 3) {              // Projected, VU 0x410
    vUv = uTexO + (uTexU * wp).xy;
} else if (uTexgen == 1 || uTexgen == 4) {
    vUv = uTexO + vec2(dot(uTexU[0].xy, aUv), dot(uTexU[1].xy, aUv));  // or keep the current CPU bake
} else vUv = aUv;                       // 0: raw, xfm ignored
```

- For texgen 5, the CPU builds `U_i = (S[0][i], −S[2][i], S[1][i])` with `S[k] = xfm[3k..3k+2]`, and `O = (0.5, 0.5)`.
- For texgen 1/4, move the existing CPU formula into uniforms, so later passes (atom_bomb wisp, paramount_lights_add) and MatAnims work without re-baking:
  - `U0 = (r00, −r01)`, `U1 = (−r10, r11)`.
  - O from render_gems_dof §1.2.
- For Sphere, implement FUN_0019cf58 on the CPU per draw (§4.4), or use the [I] approximation.

### 7.3 Fragment shader per pass
```glsl
vec4 c = vColor;                                  // lit or vertex/mat colour (existing code), min(...,1)
if (uUseTex != 0) {
    vec4 t = texture(uTex, vUv);
    c = uDecal != 0 ? t                           // DECAL: texture only (TCC=1)
                    : vec4(c.rgb * uColorScale, c.a) * t;   // MODULATE (uColorScale 255/128 for idx 3)
}
if (uCutout != 0 && c.a <= 0.0) discard;          // alpha_cutout: alpha > 0 passes
if (uFogOn != 0) c.rgb = mix(uFogColor, c.rgb, vFog);
fragColor = clamp(c, 0.0, 1.0);
```

- HIGHLIGHT may be treated as MODULATE. It adds 0.035 to RGB and uses alpha = tex.a.
- Untextured passes keep `c = min(lit, 1)`. The engine's colorScale already treats untextured as 1.0, because 255 at the GS means 1.0.

### 7.4 Draw loop (scene_renderer.cpp drawObject / drawMesh)
```
for (const Pass& p : dm.passes) {
    st = liveState(dm, p);          // texture, U/O, blend, decal, colorScale, lit, fog, depth func/write, cutout
    renderer_.drawMesh(dm.mesh, mvp, st);
}
```

Renderer::RenderState needs these additions:
- `int texgen; float texU[9]; float texO[2]; bool decal; DepthFunc depthFunc` (ALWAYS / LESS / LEQUAL).
- `uEye`, set per frame from the active camera.

Blend mapping in applyState, per the §2 table:
- Add `Blend::Dest` → (ZERO, ONE).
- Subtract → REVERSE_SUBTRACT.

The draw order stays the same: passes are consecutive for each mesh, as on PS2.

### 7.5 Checks
1. Gem specular: gem2.mesh overlays (Force, SrcAlphaAdd) and the gem_<c> P1 highlight should slide across the gem as the highway camera moves. With S = rotY(20°), the highlight is offset sideways.
2. Guitars: the chrome sweep should stretch horizontally (small S[2][2]), additive.
3. small_club window and glass ("Material #507"): additive sphere_REF reflections, no fog on P1.
4. Neon (snake_neon, crown_sign, lightrig, lava): an additive glow pass over the base. In small_club the glow is lit; in small_club_multi it is unlit.

---------------------------------------------------------------------------------------------------
## Key addresses
- Pass build: FUN_0019d3b8 (blend switch 0x19d428; zmode 0x19d5b8; texgen 1/4 0x19d9a0, 2 0x19da34, 3 0x19d804, 5 0x19da98; NPOT 0x19dc5c; next pass at the tail).
- Pass upload: FUN_002ebcb0 (GS 0x42 ALPHA, 0x44 DIMX, 0x47 TEST, 0x4a FBA, 0x08 CLAMP, 0x4e ZBUF; VU 0x2b0..0x2b7; PRIM bits).
- TEX0 TFX: FUN_002ec728.
- Sphere per-draw matrix: FUN_0019cf58 (euler FUN_0024a350 / FUN_0024acd0).
- Projected inverse: FUN_0024b668; global 0x364750 set by FUN_0019dfd0.
- Pass loops: PsMesh_v5 0x2ec320, PsMultiMesh_v5 0x1a34a0, PsParticleSys 0x1a2fd8, PsRnd_v8 0x19b598.
- PsMat_v8 0x19cf00 (pass vector size). TFX table DAT_002edd80.
- Enum strings: blend 0x321f90.., texgen 0x322058.., multipass FUN_001bfe60.
- VU1 texgen programs: 0x139 environ, 0x17c xfm, 0x347 sphere, 0x410 projected, 0x614 none.
