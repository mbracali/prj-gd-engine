# GH1 PS2: texture wrap/texgen, gems & tails, Star Power track look, depth of field

[C] = read from code (EE decompile, my own MIPS/FPU disassembly of the ELF, or the VU1 listing in scratchpad/all.txt) or from the shipped data. [I] = inference.
Helpers I wrote (all in the scratchpad): `fdis.py <hex addr> <n>` (MIPS + COP1 disassembler), `dm.py <scene> <glob>` (dumps Mat/Mesh/Tex), `meshsum.py <scene> <glob>` (one-line mesh summary), `img/` (texture dumps, gem atlas overlay).

---------------------------------------------------------------------------------------------------
## 1. Texture wrap, texgen and the stage UV transform

### 1.1 `wrap` → GS CLAMP_1 [C]
- In the pass builder FUN_0019d3b8 at 0x19d7c8: `lw v0,80(s2)` reads stage+0x50 (wrap). Then `pass+0x38 = wrap ? 0 : 5`.
- FUN_002ebcb0 writes pass+0x38 to GS register **0x08 CLAMP_1** with mask 0xf (WMS bits 0-1, WMT bits 2-3).

| wrap | CLAMP_1 | meaning |
|---|---|---|
| 0 ("Clamp") | 5 → WMS = WMT = 1 | **CLAMP** in U and V |
| 1 ("Repeat") | 0 → WMS = WMT = 0 | **REPEAT** in U and V |

- REGION_CLAMP and REGION_REPEAT are never used. GL equivalent: wrap 0 → GL_CLAMP_TO_EDGE, wrap 1 → GL_REPEAT.
- The full-screen copies in FUN_0019a998 also write CLAMP_1 = 5.

### 1.2 Texgen is per vertex in VU1; the UV matrix is rebuilt per pass on the EE [C]
- FUN_0019d3b8 `switch(stage.texgen)`. It picks the VU texgen program (pass+0xc → VU 0x2b1.x) and builds a 4-qword matrix at pass+0x60..0x9c, uploaded to VU 0x2b3..0x2b6 (= `puVar20[0x18..0x27]`).
- The pass is rebuilt whenever the Mat is dirty (`mat+0x64 & 2`, e.g. after a MatAnim changes the stage xfm). The main loop (0x19b) runs `JALR vi15` (texgen) after lighting on every vertex batch.
- Nothing is baked into the mesh.
- Stage xfm layout in memory: rows padded to 16 bytes. r00 = +0x10, r01 = +0x14, r10 = +0x20, r11 = +0x24, tx = +0x40, ty = +0x44. In the file's 12 floats (our `s.xfm`) these are x[0], x[1], x[3], x[4], x[9], x[10].

| texgen | VU program | what it does |
|---|---|---|
| 0 None | 0x614 (`JR vi2`, no-op) | **mesh UVs used raw; the stage xfm is ignored entirely** |
| 1 Xfm | 0x17c | `uv' = off + M·uv`, centred on (0.5, 0.5) (below) |
| 4 XfmOrigin | 0x17c | same matrix, offset = raw (tx, ty), not centred |
| 2 Sphere | 0x347 | `uv' = off + n_obj·(World·T)` (normal-based; T from FUN_0019cf58, camera-relative) |
| 3 Projected | 0x410 | `uv' = off + p_obj·(World·T)` (planar projection of the position) |
| 5 Environ | 0x139 | reflection map: `R = 2(N·V)N − V` with V = camPos − P (world). `uv' = 0.5 + (R·T).xy / |V|`. T = stage rows × fixed (x, z, −y) swizzle [I exact axes], offset (0.5, 0.5) |

Program 0x17c, VU lines 0x17c-0x18e:
- `vf3` = 0x2b3, `vf4` = 0x2b4, `vf5` = 0x2b6.
- Per vertex: `uv'.xy = vf5 + vf3·u + vf4·v`, written back to the vertex UV slot (+3).
- Only xy are written.

Matrix built for texgen 1/4 (asm 0x19d9a0-0x19da30) [C]:

```
0x2b3 = ( r00, -r01 )        0x2b4 = ( -r10, r11 )
texgen 1:  off.u = (-tx - 0.5)*r00   + (ty - 0.5)*(-r10) + 0.5
           off.v = (-tx - 0.5)*(-r01) + (ty - 0.5)*r11   + 0.5
texgen 4:  off = (tx, ty)
```

So for **texgen 1 (Xfm)**:

```
u' =  r00*(u - tx - 0.5) - r10*(v + ty - 0.5) + 0.5
v' = -r01*(u - tx - 0.5) + r11*(v + ty - 0.5) + 0.5
```

- Equivalently, in a centred, V-up space (U = u − 0.5, T = 0.5 − v): `(U', T') = (U − tx, T − ty) · [[r00 r01],[r10 r11]]` (Milo row-vector order).
- **Order: translate (by −t), then rotate/scale, about the texture centre.**
- With identity rotation, translation gives `u' = u − tx`, `v' = v + ty`. The signs are opposite to our engine's `+x9, +x10`.

**texgen 4:** `u' = tx + r00·u − r10·v`, `v' = ty − r01·u + r11·v`.

### 1.3 The pause.gh tiles [C data + formula above]
All four tiles:
- Mesh UVs (0,0), (0,1), (1,0), (1,1).
- Mat: mat blend 3, stage blend 2, `wrap 0` (CLAMP), translation 0.

| tile | texgen | r00, r01, r10, r11 | result on PS2 |
|---|---|---|---|
| pause_tile1 | 0 | identity | raw UV (xfm ignored) |
| pause_tile2 | 1 | −1, 0, 0, 1 | u' = 1 − u, v' = v (horizontal mirror) |
| pause_tile3 | 1 | 1, 0, 0, −1 | u' = u, v' = 1 − v (vertical mirror) |
| pause_tile4 | 1 | −0.999, 0, 0, −0.999 | u' ≈ 1 − u, v' ≈ 1 − v (both) |

- Every result stays inside [0, 1], so CLAMP never triggers and the four quadrants mirror the image exactly.
- Our engine computes `u·x0 + v·x3 + x9`, which gives −u for tile 2. That is outside the texture, the clamp then shows the edge texel, so the tile looks empty.

Fix (scene_renderer.cpp `vertices()` / `liveState()`):
- texgen 0: never apply the xfm.
- texgen 1: use the centred formula.
- texgen 4: use the non-centred one.
- Translation sign: `u − tx`, `v + ty`, so the live `uvOffset` is (−Δtx, +Δty) for identity rotation. In general, apply the full matrix in the shader, because rotations and scales animate too.
- texgen 2, 3 and 5 need per-vertex shader code. For 5, use the reflection formula above.

---------------------------------------------------------------------------------------------------
## 2. Gems and sustain tails (GemManager)

Structure [C]:
- Resources: FUN_00161f40 (strings 0x30e638-0x30e8f0).
- Per-gem representation (GemRep, 0x21c bytes): ctor FUN_0015fa58, setup FUN_0015ff98, look FUN_001602a8, per-frame update FUN_001609b0.
- GemManager: FUN_0015d208, update FUN_0015dbc8.
- `track_graphics.gem`: fps 30, tail_glow_width 1.5, tail_glow_tight_width 0.7, sparkle_len 1000.
- `mat_formats tail tail_%s` and `tail_glow tail_glow_%s` are the only formats the code uses (FUN_00163240). **`gem`, `star` and `dot` (`dot_top_hopo2_%s`) are never looked up.**

### 2.1 What a gem is made of [C]
Each GemRep is a View with 7 mesh copies, each in one of 8 shared "layer" views. The layers are appended to the gem View's draw list in order. GemRep views (with the tail) are inserted at the head of the list, so all tails draw first.

| slot | layer | default mesh (resource) | role |
|---|---|---|---|
| top | 0 | **top_black.mesh** | the round top |
| body | 1 | gem_<color>.mesh (`%s_%s.mesh`) | coloured body (gem.mat, gem.tex atlas) |
| spec | 2 | **gem2.mesh** | gem2.mat: **SrcAlphaAdd, texgen 5 Environ, gem_specular.tex** (rotated xfm), zmode Force: an additive reflection highlight |
| star_base | 3 | **star_base.mesh** | star_base.mat gem_glow.tex, alpha .99. Animated by a copy of **star.tnm**: one full turn about Z every 100 frames = 3.33 s, frame = now_ms·0.03, same phase for all stars |
| top_star | 0 | **top_star_black.mesh** | star top |
| star | 4 | star_<color>.mesh (child of star_base) | star.mat stargem.tex |
| star2 | 5 | **star2.mesh** (child of star_base) | star2.mat: additive, texgen 5, gem_specular.tex |

Looks: FUN_001602a8(rep, look), where look 0 = normal, 1 = star phrase, 2 = Star Power "bonus".

| look | shown | body | spec | top (non-HOPO / HOPO) | top_star (non-HOPO / HOPO) |
|---|---|---|---|---|---|
| 0 normal | top, body, spec | gem_<c> | gem2 | **top_black** / top_<c> | (hidden) |
| 1 star | star_base, star, star2, top_star | (hidden) | (hidden) | (hidden) | **top_star_black** / top_star_<c> |
| 2 bonus | top, body, spec | **gem_bonus** | **gem_bonus2** | top_black / **top_blue** | (hidden) |

**The black round top.** top.mat samples gem.tex (64×32, inline in track.rnd_ps2; `extracted_png/.../gem.png` is a different 64×64 file):
- top_black.mesh: 40 faces black (the ring) and 24 white (the centre dot).
- top_<c>.mesh: the ring is in the gem colour, with the same white dot.

So:
- Normal strummed gems have a **black ring with a white dot**.
- **Only HOPO gems** use the coloured top_<c>.
- The HOPO flag is rep+0x210 = gem-record [6], set in FUN_0015d840 from chart flag bit 8 plus the previous-gem check [C]. That this matches our `g.hopo` is [I].

The engine draws top_<c> for every gem, so every gem looks like a HOPO. It also tints HOPOs with dot_top_hopo2 mats, which the original never uses.

Miss (state 1, FUN_001607c8):
- body → **gem_miss.mesh**, star → **star_miss.mesh**.
- tail mat → tail_miss.mat.
- glow line hidden.
- **top, spec, star_base and star2 stay as they were.** top_miss.mesh is never used.

Hit (state 2, FUN_00160848):
- All 7 mesh slots are hidden; the gem vanishes.
- The tail stays, and its glow line is shown (§2.3).

### 2.2 Bonus (Star Power) gems and the spin [C]
- **gem_bonus.mat**: blend 3, lit (use_environ), stage blend 2, **texgen 5 (Environ)** gem_bonus.tex.
  - **gem_bonus.mnm** rotates that stage matrix about X: −2π per 100 frames.
  - Filters ScaleOffset ×1.5 and MinMaxLoop 0..200.
  - Driven by `ui::track_anims.view.SetFrame(now_ms·0.03)` in the TrackPanel Poll. So frame = now·0.045, one revolution per 2.22 s.
  - The "spin" is therefore the **environment-map lookup rotating**; the mesh itself does not rotate.
- **gem_bonus2.mat**: SrcAlphaAdd, zmode Force, colour (.75,1,1), texgen 5 line01.tex.
  - gem_bonus2.mnm turns it −2π per 200 frames (4.44 s).
  - This is an additive moving streak on top of the bonus body.
- The HOPO top during bonus is top_blue.mesh.
- **Transition burst** when SP starts (FUN_0015ec28 from set_bonus_gems TRUE):
  - Two MultiMeshes (gem_bonus_spark1 / gem_bonus_spark2) get one instance per visible unplayed gem (FUN_0015e888/FUN_0015eaa0 copy each rep's top transform).
  - An AnimateTask plays gem_bonus_spark1.tnm over 15 frames: scale 0.09→1.5 (f1)→1.8×1.5 (f9)→0.09 (f18).
  - gem_bonus_spark1.mnm: gem_bonustransition.tex, alpha 0→1 (f2)→1 (f10)→0 (f13), spinning 2π/20 f.
  - gem_bonus_spark2.mnm: starfire_part.tex, additive, alpha 0→1→0 by f11, scale 5→0.5, rotating.
- Song-start sparkle: FUN_0015ed08, gem_sparkle.mesh + gem_sparkle.tnm over 60 frames on the first gems [C mechanics, I purpose].

### 2.3 Sustain tails [C unless marked]
**Geometry.** The tail is not tail02.mesh scaled. FUN_001633d8 takes tail02.mesh's vertices, sorts them by y and keeps half: the **6-vertex cross-section** at y = 0.

| x | z | alpha |
|---|---|---|
| ±0.43 | 0 | 0 |
| ±0.37 | 0.231 | 1 |
| ±0.156 | 0.319 | 1 |

- UV v is 0, 0.1 and 0.6 respectively. The alpha-0 outer verts give soft edges.
- Each frame FUN_001609b0 rebuilds a strip of **N = 30 segments** (20 when `DAT_002edb88+0x8c` is set, [I] multiplayer). Segment length = **120/N = 4 units**.

Tail extent:
- The tail is placed at local y = `start` inside the gem rep (rep at the gem's y). Length L = end − start.
- FUN_0015eed0 clips:
  - near end: `max(start + consumed, now + remove_y)`
  - far end: `min(end, now + horizon_y)`
- While sustaining, `consumed = now + nowbar_tail_clip(1.5) − gem_y` (FUN_0015e020). The tail starts **1.5 units past the strike line**.

Row layout:
- k = floor(L/4), and the first N−k rows collapse onto y = 0.
- The remaining rows are at y = r, r+4, … (r = L − 4k), so the spacing is anchored to the far end.
- Second-to-last row: at y_last + 3 with width ×(h·0.5 + 1).
- Last row: x scale 0 at y_last + 4, a pointed 1-unit tip.

Vertex values:
- position = (profile.x · s, y, profile.z).
- colour.rgb = the current tail-material colour (written into the vertex colour).
- colour.a = profile alpha.

**Whammy wobble.**
- `whammy` = GuitarController_v5 `min(axis, 0)`. The axis is the pad analog byte /127 (FUN_00220xxx, `/127.0`), so whammy ∈ [−1, 0] [C]. That 0 is rest and −1 full press is [I].
- Through PlayerMatcher_v15 → FUN_00116930 it is stored at player+0xec and passed to the track as state[2].
- **Only while the tail is being played (rep state 2 or 3)**, every Poll pushes `h = −3·whammy` (0..3) into a 100-entry ring buffer (rep+0x6c, index rep+0x1fc).
- The row at local distance y uses the sample taken `floor(y/2)` polls ago: `s = 1 + h[(i − floor(y/2)) mod 100]`. The first collapsed rows use the newest sample.
- So **only the tail width (x) changes**, by 1× to 4×. A wiggled bar sends width bulges up the tail at 2 units per frame. When no longer sustaining, the buffer stops advancing and the shape freezes.
- The buffer is zeroed when the tail is created.
- The frame rate is the game's Poll rate; 60 Hz is [I].

**Glow line.** Each rep owns a RndLine (rep+0x58):
- 2 points `(0, start, 0.16)` → `(0, end, 0.16)`.
- hasCaps on only when the length is under 95 units.
- Width rep_line+0x100. Point colours = colour with alpha ×0.99.

Tail and line material per look and state:
- **Unplayed normal tail**: tail_<c>.mat; line hidden.
- **Star-phrase tail (look 1)**: tail_<c>.mat (still coloured!). The line is shown even before the hit, with **tail_glow_tight.mat** (tail_tight.tex, additive, colour .08,.81,1) at width **0.7**: a thin cyan core line.
- **Bonus tail (look 2)**: tail_bonus.mat (colour 0,.69,.75; texgen 5 gem_bonus.tex) with line mat tail_glow_bonus (.5,.8,.8).
- **Hit / sustaining (state 2)**:
  - Line shown with tail_glow_<c>.mat (tail_glow_star.mat for look 1), additive line01.tex.
  - Width = `1.5 · w`, where `w` follows `(max_s − 1)·0.5 + 1` with `w += (target − w)·0.2` per frame.
  - So the glow widens with the wobble.
- **Star tail while whammying** (look 1, sustaining, and player+0xc0 "whammy active" = state[3]):
  - A ramp g (gm+0x190, ±0.1 per frame toward state[3]) lerps the **tail colour toward tail_star.mat (.08,.81,1)**.
  - It lerps the **line colour from tail_glow_star toward tail_glow_whammy (0,.4,.5)**.
  - A copy of **tail_bonus.part** (whammyparticle.tex, additive) emits along the tail, with emit params ×2g. Particles past the tail end are killed.
  - The **tail turns blue only while you whammy a star sustain.**
- whammy active (FUN_00116930): `|Δwhammy/Δt| > whammy_speed (0.05)`, or less than whammy_timeout (0.5) since the last movement (scoring.dta). The time units are [I].
- **Released early / finished (state 3, FUN_00160920 via FUN_0015e388)**: tail mat → **tail_miss.mat** (grey .34, tail_miss.tex, lit by track1.lit), line hidden. The remaining tail stays at the release point.
- **Missed (state 1)**: tail_miss.mat as well.

Shading [C, lighting.md rules]:
- tail_<c>/star/bonus mats are use_environ 0, vert_amb 1, so the pass is unlit and the vertex colour passes through.
- Stage blend 3 means the GS colour scale is 255, i.e. **×2**.
- So `tail = clamp(2·colour·tex(envUV))`, alpha = profileAlpha·texAlpha, and the mat blend is SrcAlpha.
- texgen 5 env map of gem_star.tex (xfm x-scale 0.5), CLAMP.
- tail_miss.mat is lit: `dyn = 0.34 × point-light term`, ambient 0.

**Burn.** Each smasher has a burn_tail.view: burn_tail_base.part, burn_tail_big.part, and burn_castlight.mesh pulsing via burn_castlight.tnm.
- It is shown while that fret's sustain is held.
- burn_tail_state.view is frozen (MinMaxLoop min = max) at frame **0 normal / 10 whammy / 20 bonus** (FUN_00166940; whammy wins over bonus).
- Particle start colour (1,.7,.1) orange fire, or (.4,1,1) cyan for whammy/bonus. Castlight colour (1,.6,.15), (.15,.4,.7) or (.15,.6,1).
- Hit flare: smash_normal.view uses **smash_flamelight.mesh**; **smash_star.view and smash_bonus.view use smash_flamelight_starcollect**. The flare view is chosen by FUN_00166628: bonus → 2, star gem → 1, else 0.

### 2.4 Exact list of differences in engine/src/render/track_renderer.cpp
1. **Tops:**
   - Normal gem: `top_black.mesh` (black ring, white dot).
   - HOPO: `top_<c>.mesh` with top.mat; do **not** override the mat with dot_top_hopo2_*.
   - Star gem: `top_star_black.mesh`, or `top_star_<c>.mesh` for HOPO.
   - Bonus HOPO: `top_blue.mesh`.
2. **Missing overlays:**
   - `gem2.mesh` (additive env-mapped specular) on every normal and bonus gem.
   - `star_base.mesh` (rotating glow disc, 3.33 s/rev) and `star2.mesh` (additive specular) on star gems.
   - These need texgen 5 support in the renderer.
3. **Misses:**
   - Keep the top, swap only the body to `gem_miss.mesh` / `star_miss.mesh`. The engine draws top_miss.mesh, which the original never uses.
   - Missed star gems currently fall into the gem branch only if `star` is false; use star_miss for star gems.
4. **Star tails:** keep `tail_<c>.mat`. Lerp the vertex colour to tail_star's colour only while whammying (ramp ±0.1/frame). The engine uses tail_star.mat for every star tail.
5. **Tail geometry:** build the 6-vertex-profile strip (30 rows of 4 units, pointed 1-unit tip, soft alpha edges) instead of scaling tail02.mesh. Apply the **whammy width wave** (history ring, `1 + 3|w|`, delay floor(y/2) frames). Start the sustained tail at strike line + 1.5.
6. **Glow line** (RndLine at z 0.16):
   - Tight cyan 0.7-wide line on unplayed star tails.
   - 1.5×w-wide tail_glow_<c>/star/bonus line while sustaining.
   - Whammy colour lerp.
7. **Released or completed sustain** → tail_miss.mat (grey), not hidden.
8. **Bonus look during SP:** gem_bonus/gem_bonus2 with the env-map spin, tail_bonus.mat + tail_glow_bonus line, and the transition sparkle burst on deploy.
9. **Hit flare mat:** starcollect for star-phrase gems too, not only when SP is active. Plus burn particles (normal/whammy/bonus colours) and the tail_bonus.part whammy particles.
10. Draw order: tails/lines first, then tops, bodies, spec, star_base, stars, star2 (layer order). The engine draws per gem, far to near; with the depth test this is mostly equivalent [I].

---------------------------------------------------------------------------------------------------
## 3. Track Star Power effects

`track set_bonus_gems B` → Track_v1 → FUN_00159b88 [C]:
1. When B: t_bonus = now. `lightning_track.view.set_showing(B)`.
2. FUN_0015e610: every gem in [first visible, last created) that is not hit/missed (rec[3], [4] and [5] all 0) gets look **2** when B.
   - When !B it gets look 1 if `FUN_0010c5d8` (its phrase is still intact) and state[4] ≠ 1, else 0.
   - Gems created while bonus is on are born with look 2 (FUN_0015df70: `gm+0x188 ? 2 : starred`).
3. FUN_00164d00: burn view bonus flag (cyan burn). gm+0x188 = B.
4. If B: FUN_0015ec28, the transition sparkle burst on all visible gems (§2.2).
5. Scripts (player.dta `start_using`): `game anim_task track 800 2000 2025` (camera shake, see endgame_and_hud §1.5), sp_deployed sfx, crowd sfx.

**Lightning** (lightning_track.view; Poll `SetFrame((now − t_bonus)·0.03)`) [C data]:
- `lightning_trackglow.mesh`:
  - Material: lightning_trackglow.mat, blend SrcAlpha, zmode Transparent, vertex alpha.
  - Mesh: 16 verts, x ±15.1, y −18.6..119.3 (world 58.08 offset), z ±1.
  - The track-wide glow sheet.
- `lightning_1.mesh` / `lightning_2.mesh`:
  - Additive blend 2, alpha_cutout, 20 verts each, bolts spanning y −7.9..180, z ±31.
  - 8-texture flipbook lightningB_01..08, 3 frames each, 24-frame loop (0.8 s). lightning_2 is offset by +12 frames.

**Rails glow** (FUN_001657d8) [C]:
- `state = (sp_ready ? 2 : 0) + (warning ? 1 : 0)` selects `side_rails_{none,warning,star,warning_star}.mnm`, played at now·0.03, 15-frame loop (0.5 s).
- The anims set **track_side_rails.mat**'s colour: none = (1,1,1,0); warning = (1,1,1,0)→(1,.2,.2,1); star = (.3,.8,1)→(1,1,1); warning_star = red↔blue.
- Rails mat: use_environ 1, vert_amb 1, vert_dyn 0, untextured. track.env has ambient 0 and one point light `track1.lit` (white, range 472, at (1.98, 5.76, 33.99)).
- So, by the lighting.md formula, `rgb = matColour × pointLight(N,P)` and `alpha = vertex alpha` (mesh alpha 0..1 fades). Vertex rgb (.37 or 1) is multiplied by ambient 0 and drops out.
- The pulse is the mat colour lit by the overhead point light. The **rails glow red when the rock meter is in the warning zone, and blue/white when SP is ready**.
- During deployment sp_ready is false, so the rails return to "none" (white × light) unless warning [I: deploy allowed = false while active].

**Visible highway changes while SP is active** [C]:
- bonus gems, bonus tails and glow lines
- deploy burst
- lightning bolts + track glow
- camera shake on deploy
- cyan burn on held sustains
- `smash_bonus.view` hit particles + starcollect flare
- No change to surface, smashers or rims (smasher mats only follow the held state).

---------------------------------------------------------------------------------------------------
## 4. Depth of field

### 4.1 Data and switches [C]
- Venue camera.dta shots have only **`(enable_dof 0|1)`**. There is no focus distance, blur amount or strength key anywhere in the data. arena.dta `blur_*` belongs to a different effect (§4.4).
- `switch_cam` (FUN_0016f3b8, string 0x3112e0) stores enable_dof → VenueCam +0x98 (default 0).
- `set_depth_of_field B` (VenueCam_v1, string 0x311218; sent as `arena set_depth_of_field FALSE` by game_outro) overwrites +0x98.
- Per-camera update (FUN_0016e370..; the block at decomp 00160000.c:9475) [C]:
  - `d = |target.worldPos − camPos|`, where target = the shot's look-at object (singer etc.).
  - `focus = enable_dof ? 2·d : 0`.
  - `FUN_0019adf0(focus, rnd, cam)`: rnd+0x518 = 0 if focus == 0, else `cam->PsCam_v6(focus)`.
- PsCam_v6 0x19c720 converts a view distance to the **integer Z-buffer value**:
  - `ndc = (f+n)/(f−n) − 2fn/((f−n)·dist)`
  - `Z = ndc·H·(zs − ze) + H·(2 − zs − ze)`, with H = (2^zbits − 1)/2, near/far = cam+0x300/0x304, z range = +0x30c/+0x310.
  - Larger Z = nearer [I from the formula with zs < ze].
- **The focus plane is therefore at twice the camera→subject distance.** Only things farther than that get blurred; the subject and the near stage stay sharp.

### 4.2 The PS2 pass (FUN_0019afa8, end of frame; quad helper FUN_0019a998) [C]
Runs only when rnd+0x518 ≠ 0. W×H = the full display buffer (rnd+0x30/0x34; **no downsampling**). Quads are drawn as 32-px-wide SPRITE strips with FST texel UVs, bilinear (TEX1 MMAG/MMIN = 1), CLAMP.

1. Four passes from buffer A (the frame just rendered) into buffer B. Each is `HIGHLIGHT`-mode (TFX 2) with vertex colour (0x80,0x80,0x80,0x17). UV rects:
   - pass 1: (0,0)-(W,H), ALPHA = **0x20000000a8** → `B = Cs·0x20/0x80 = 0.25·Cs`
   - pass 2: (0,3)-(W,H+3), ALPHA **0x2000000068** → `B += 0.25·Cs`
   - pass 3: (3,0)-(W+3,H), same
   - pass 4: (3,3)-(W+3,H+3), same
   - Result: a **4-tap box blur with taps at 0 and +3 px in x and y** (each tap bilinear), plus the HIGHLIGHT `+Af` term. Af = 0x17, so about **+23/255 brightening** [C register values, I visual effect].
2. Composite: buffer B → buffer A, UV (0,0)-(W−1.5, H−1.5), **opaque** (no alpha blend, TFX DECAL).
   - **TEST ZTST = GEQUAL**, with the sprite's Z = rnd+0x518 (the focus Z).
   - Every pixel whose stored depth is **at or beyond the focus plane** is replaced by the blurred copy. Nearer pixels keep the sharp frame.
   - The −1.5 in the UV extent roughly re-centres the +3 px taps (it is a 1.5-px scale across the screen, not a pure shift).
   - **Hard cut:** there is no gradual blend by depth and no circle-of-confusion size.
3. FUN_00199xxx (decomp 00190000.c:6315) zeroes +0x518 around the movie/screenshot capture path.

### 4.3 PC reproduction
1. Render the scene to a colour + depth target.
2. `blur = 0.25·(c(x,y) + c(x+3,y) + c(x,y+3) + c(x+3,y+3))` sampled at a −1.5 px offset, bilinear, plus +0x17/255 (optional; it is a slight brighten).
3. Output: `depthLinear(x,y) >= 2·|subject − cam| ? blur : sharp` (or compare against the projected focus depth).
4. Enabled when the current shot's enable_dof is 1 and no `set_depth_of_field FALSE` is in effect.
5. At higher resolutions, scale the 3-px tap distance by height/448 (or 480) to keep the same look [I].

### 4.4 Not DOF: the camera-cut blur (for completeness)
- FUN_0016d230 / FUN_0016d4c8 (Arena `enter_blur` / `exit_blur`, venue_enter_blur / venue_exit_blur) uses arena.dta `blur_alpha 0.8`, `blur_grow 0.01`, `blur_duration 1000`.
- It grabs the screen into a texture (FUN_001e11a8) and redraws it with an inset growing by blur_grow and alpha fading from 0.8 over 1000 ms [C fields, I exact easing].
- It is a transition ghost effect, separate from DOF.

---------------------------------------------------------------------------------------------------
## Key addresses
- Wrap/texgen: FUN_0019d3b8 (wrap 0x19d7c8; texgen 1/4 at 0x19d9a0; jump table 0x319320), FUN_002ebcb0 (GS regs, CLAMP_1 = reg 8).
- VU texgen programs: 0x17c (xfm), 0x347 (sphere), 0x410 (projected), 0x139 (environ), 0x614 (none).
- Gems: FUN_00161f40 (resources), FUN_0015fa58 (rep ctor), FUN_0015ff98 (setup), FUN_001602a8 (look), FUN_001607c8 / FUN_00160848 / FUN_00160920 (miss / hit / released), FUN_001609b0 (tail build + whammy + glow), FUN_001606d8 (glow line extent), FUN_001633d8 (tail profile), FUN_0015dbc8 / FUN_0015de78 / FUN_0015eed0 (update/clip), FUN_0015e610 (set bonus looks), FUN_0015ec28 / FUN_0015ed08 (bonus / start sparkles), FUN_0015e388 (sustain release).
- Burn: FUN_00166758 / FUN_00166940.
- Whammy: GuitarController_v5 0x25b820, FUN_00116930 (player+0xec, +0xc0 via FUN_0011cf90).
- DOF: VenueCam_v1 0x16eb80 (+0x98), switch_cam parser FUN_0016f3b8, focus at 00160000.c:9475, FUN_0019adf0, PsCam_v6 0x19c720, pass FUN_0019afa8, quad FUN_0019a998.
