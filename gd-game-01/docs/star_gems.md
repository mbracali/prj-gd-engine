# GH1 PS2: star gems (Star Power phrase gems)

[C] = read from code (EE decompile / ELF bytes) or shipped data. [I] = inference.
Builds on docs/render_gems_dof.md §2 (GemRep / GemManager). Data: `extracted/track/gen/track.rnd_ps2`.

## 1. Resources (FUN_00161f40, table `res` = GemManager resources) [C]

| res offset | object | used for |
|---|---|---|
| +0x14 | gem2.mesh | spec slot (normal look) |
| +0x18 | top_black.mesh | top slot, non-HOPO |
| +0x1c | gem_miss.mesh | body on miss |
| +0x20 | top_blue.mesh | top, HOPO + bonus |
| +0x24 / +0x28 | gem_bonus / gem_bonus2.mesh | bonus look |
| +0x2c | **top_star_black.mesh** | top_star slot, non-HOPO |
| +0x30 | **star2.mesh** | star2 slot |
| +0x34 | **star_base.mesh** | star_base slot |
| +0x38 | **star_miss.mesh** | star slot on miss |
| +0x3c | **star.tnm** | spin anim (copied per gem) |
| +0x40 / +0x44 | gem_bonus.mnm / gem_bonus2.mnm | bonus env spin (not per gem) |
| +0x48 + 4c | gem_<c>.mesh (`%s_%s.mesh`, "gem") | body |
| +0x5c + 4c | top_<c>.mesh ("top") | HOPO top |
| +0x70 + 4c | **star_<c>.mesh** ("star") | star body |
| +0x84 + 4c | **top_star_<c>.mesh** ("top_star") | star top, HOPO |
| +0x110 | `fps/1000` = 30/1000 = **0.03** (track_graphics.dta `gem (fps 30)`, divided at 0x161f40) | gem anim rate |

c = fret 0..4; the colour names come from FUN_00163858 (a DTA list). That the order is green, red, yellow, blue, orange, as in the engine, is [I], but it is consistent with the slot templates (index 0 → star_green / gem_green) and with the atlas below.

## 2. GemRep slots (ctor FUN_0015fa58) [C]

Each slot is a fresh Mesh created by FUN_001611d0, which copies the template mesh via vtable +0x44, Copy. The slot is then parented with FUN_001da100(parent, child) and listed in a layer View (`res+4` vector, 8 Views). FUN_001630d0 appends the layers to the gem root view (track+0x34) in order 0..7. Each rep's own View (tails and glow line) is inserted at the head of that list.

| rep off | slot | template | parent | layer (draw order) |
|---|---|---|---|---|
| +0x04 | top | top_black | rep view | 0 |
| +0x10 | body | gem_green | rep view | 1 |
| +0x1c | spec | gem2 | rep view | 2 |
| +0x34 | top_star | top_star_black | **rep view** (does not spin) | 0 |
| +0x28 | star_base | star_base | rep view; **star.tnm copy targets it** (FUN_0015f6f0: FUN_00161280 copies the TransAnim, FUN_001dd008(anim, slot trans), and the anim goes into the rep view's anim list +0x10) | 3 |
| +0x40 | star | star_green | **star_base** | 4 |
| +0x4c | star2 | star2 | **star_base** | 5 |

Mesh swap, FUN_0015f808(slot, res) [C, from disassembly at 0x15f808, since the decompile drops a1]:
- FUN_001c4648(slot, res): slot+0x128 (geometry owner) = the resource mesh.
- FUN_001c45e8(slot, res+0x118): slot material = the resource's material.

So geometry and material come from the resource. The slot's **local transform stays the one copied from the template** [C copy, I that Copy takes the local xfm, which matches the data below].

Draw order, whole highway: all rep views (tails, lines), then layer 0 (every top and top_star), 1 (bodies), 2 (gem2), 3 (star_base), 4 (star_<c>), 5 (star2). Within a layer, show order is append order.

## 3. Looks and states

Setup: FUN_0015ff98(x, y, rep, lane, hasTail, …, look, root):
1. star slot ← res[0x70 + 4·lane] (star_<c>).
2. Tail setup.
3. FUN_001602a8(rep, look).
4. rep view local pos = (x, y, 0) (FUN_001da730).
5. SetFrame(0) (FUN_001abab8).

**FUN_001602a8(rep, look)** [C]. look 0 = normal, 1 = star phrase, 2 = Star Power bonus. HOPO = rep+0x210.
- Shown when look ≠ 1: top, body, spec. Shown when look == 1: star_base, star, star2, top_star.
- body = (look 2) ? gem_bonus : gem_<c>. spec = (look 2) ? gem_bonus2 : gem2.
- top = !HOPO ? top_black : (look 2 ? top_blue : top_<c>).
- **top_star = !HOPO ? top_star_black : top_star_<c>.**
- The star slot mesh is not touched here. It is star_<c> from setup, or star_miss after a miss.
- Tail/line: look 1 → tail_<c>.mat and line tail_glow_tight.mat (shown, width res+0x10c). See render_gems_dof.md §2.3.

So a **star gem = top_star (black or HOPO colour) + star_base (spinning glow and star rim) + star_<c> + star2 (additive env specular)**. It draws **no gem_<c> body, no top_black and no gem2**.

**Which gems get look 1:**
- When created (FUN_0015df70): look = bonus ? 2 : (phrase intact (FUN_0010c5d8) && !gm+0x18c).
- `on_miss_phrase` → FUN_00159b68 → FUN_0015e778 re-looks the gems from the first live one onward [I start index]. Gems of the broken phrase that are **not yet hit or missed** (rec[4] == rec[5] == 0) get look 0, and gm+0x18c is set.
- **The missed gem itself keeps look 1** [C: it is skipped because its rec flag is set].
- SP on/off: FUN_0015e610 (render_gems_dof.md §3).

**Miss (FUN_001607c8)** [C]:
- body ← gem_miss, **star ← star_miss** (res+0x38), tail mat ← tail_miss, line hidden.
- The look is unchanged. A missed star gem therefore still shows **star_base (still spinning) + star_miss + star2 + top_star_black / top_star_<c>**.

**Hit (FUN_00160848)**: all 7 slots hidden.

## 4. The spin [C]

- Per gem, per update: FUN_001609b0 (called from FUN_0015eed0 ← FUN_0015de78 ← FUN_0015dbc8(now_ms)) begins with `FUN_001abab8(res+0x110 · now_ms, 1.0, repView)`. That is `repView.SetFrame(now_ms · 0.03)`.
- FUN_001abab8 runs the object's filters (FUN_001ac468) and recurses into its anim list, which holds the star.tnm copy.
- star.tnm filters: ScaleOffset(1, 0), then **MinMaxLoop(0, 100, loop)**. So **frame = fmod(now_ms · 0.03, 100)**: one revolution per 100 frames = **3.333 s**.
- Every star has the same phase, because the frame is absolute song time.
- star.tnm keys: trans "star_base.mesh" (retargeted to the slot copy), rot_slerp false, no trans keys, scale key (1,1,1).
- Rot keys (x,y,z,w):

| frame | quaternion | angle about +Z |
|---|---|---|
| 0 | (0,0,0,−1) | 0° |
| 25 | (0,0,−.707,−.707) | 90° |
| 50 | (0,0,−1,0) | 180° |
| 75 | (0,0,−.707,.707) | 270° |
| 100 | (0,0,0,1) | 360° |

- Constant angular speed, **+Z axis** (normal to the highway), 90° per 25 frames. Visual sense = whatever our quatToRot gives for other TransAnims [I].
- With no trans keys, the translation stays the slot's local translation; only rotation and scale are written.
- star_base.tnm has the same keys with only MinMaxLoop. It is used only by gem_star.view, an authoring preview the code never loads (the string is absent from the ELF).
- Nothing else spins on a star gem:
  - top_star is parented to the rep view, not to star_base.
  - star.mat and star_base.mat have no MatAnim. gem_glow.mnm exists but is never referenced [C: string absent].
- During Star Power the gems are look 2. Their "spin" is the gem_bonus.mnm env-map rotation (render_gems_dof.md §2.2), a different mechanism.

## 5. Transforms and pivot [C data]

| object | local xfm (relative to parent) | world in file |
|---|---|---|
| gem_star.view (authoring parent) | T(7.85, 6.527, 0) | same |
| star_base.mesh | R_z(≈192°: rows (−.978,−.207),(.207,−.978)) · T(0.004, 0.024, 0.062) | T(7.854, 6.551, 0.062) with that R |
| star_<c>.mesh, star_miss, star2 | **identity** (children of star_base) | = star_base world |
| top_star_*.mesh | T(0.004, 0.024, 0) (child of gem_star.view) | T(7.854, 6.551, 0) |

- **The 7.85 / 6.55 offset is only the authoring parent (gem_star.view at (7.85, 6.527)).** In the game the template's local xfm is used under the rep view at (laneX, y, 0).
- Star geometry is in star_base's local space and centred on its origin. Tips are at radius ≈1.87, star_<c> spans x −1.8..1.81 and y −1.48..1.87, and star_base's glow quad spans ±3.
- **The pivot is star_base's origin. No extra pivot is needed.**

Composition per star gem:

```
Rep      = T(laneX, y, 0)
StarBase = Rep · T(0.004, 0.024, 0.062) · R_z(star.tnm(frame))   // the authored 192° is overwritten
Star     = StarBase            (star_<c> / star_miss, identity local)
Star2    = StarBase
TopStar  = Rep · T(0.004, 0.024, 0)
```

(Milo row-vector order: world = local · parentWorld. Written here column-style, parent on the left.)

Equivalent with our scene: drive star.tnm on the shared star_base.mesh, call `updateTransforms()`, then draw with `translate(laneX − 7.85, y − 6.527, 0) · world`. All gems share the phase, so one shared anim is exact. **The subtracted y should be 6.527 (the view), not 6.55.** The difference is 0.023 units, cosmetic.

## 6. Colour selection [C data]

- star_green / red / yellow / blue / orange / miss.mesh all share **star.mat** (stargem.tex 3×2 atlas, wrap repeat, texgen 0, mat blend 3 SrcAlpha, stage blend 2, zmode Normal, colour (1,1,1,1), use_environ 0, vert_amb 0).
- They have identical positions, normals and faces (40 v / 20 f). **Only the UVs differ**: each one maps into its own atlas cell.

| mesh | UV cell |
|---|---|
| star_green | u 0–⅓, v 0–½ |
| star_red | u ⅓–⅔, v 0–½ |
| star_yellow | u ⅔–1, v 0–½ |
| star_blue | u 0–⅓, v ½–1 |
| star_orange | u ⅓–⅔, v ½–1 |
| star_miss | u ⅔–1, v ½–1 (blank / white cell) |

- **The colour comes from the UVs alone. The fret picks the mesh (res+0x70 + 4·lane).**
- All star meshes (and star2) carry vertex colours **(1, 0.165, 0, 1) and (1, 0, 0, 1)**. Those are authoring leftovers.
- On PS2 they are discarded: with use_environ 0 and vert_amb 0, `pass[1] = 1` and VU program 0x7c5 writes `out = matColor` (lighting.md §"Ambient-only program").
- Result: **star = stargem.tex(atlas UV) × (1,1,1,1)**. The GS scale is ×1 because stage blend is 2, not 3.
- Other materials:
  - **star_base.mat**: gem_glow.tex, SrcAlpha, colour (1,1,1,0.99), use_environ 0, vert_amb 0. Its vertex colours are 1.
  - **star2.mat**: SrcAlphaAdd, **zmode Force** (no depth test), texgen 5 Environ gem_specular.tex (rotated xfm), wrap repeat. Vertex colours (1,.165,0) are discarded → white. Its UVs (the red cell) are irrelevant under texgen 5.
  - **top.mat**: gem.tex atlas. top_star_black has a black ring; top_star_<c> has a coloured ring. Vertex colours are 1.

## 7. What engine/src/render/track_renderer.cpp does wrong

1. **Same colour for all star gems.** `SceneRenderer::drawMesh` (scene_renderer.cpp:125) uses only `liveState()`. It never calls `setMatFlags()` or `lightingFor()`, so `uLit = 0` and the shader does `vColor = aColor · uTint`.
   - The star meshes' vertex colours (1,.165,0)/(1,0,0) therefore multiply stargem.tex. Only the red channel survives, so every star looks red/brown.
   - Fix: apply the material's vert_amb/use_environ rules in drawMesh too. For `!vert_amb` mats, replace the vertex RGBA with the mat colour (at minimum when no env is lit), and set colorScale.
   - The same fix also gives tail mats their stage-blend-3 ×2 colorScale, which drawMesh currently skips.
2. **No spin.** star_base/star_<c> are drawn with their static file world (authored 192° rotation).
   - Needed each frame: `anims_->setFrame("star.tnm", nowMs * 0.03f)`. Its filters give the mod-100 loop.
   - Then call `anims_->updateTransforms()` **before** the gem loop. Today updateTransforms only runs later, inside `lanes()`.
   - Or compute R_z(2π · fmod(now·0.03, 100)/100) directly and compose as in §5.
   - Note: `transAt()` resets pos to 0 when there are no trans keys, so do not use it for the local without re-adding T(0.004, 0.024, 0.062).
3. **star2.mesh is missing.** Draw it after star_<c> with star_base's (spinning) transform: additive, zmode Force, texgen 5 env map.
4. **Missed star gems.** `Player::phraseIntact()` turns false as soon as the phrase breaks, so the missed gem falls into the normal branch (gem_miss + top).
   - The original keeps look 1 for the missed gem: star_base + **star_miss** + star2 + top_star_black / top_star_<c>, still spinning.
   - Only the *unplayed* gems of the broken phrase revert to normal gems.
   - The current `missed ? star_miss` inside the star branch is effectively unreachable.
5. **Offset.** Use (7.85, **6.527**) (gem_star.view), not (7.85, 6.55); otherwise the y position is off by 0.023. Better: compose the locals from §5.
6. **Order.** The original draws all tops (incl. top_star) → bodies → gem2 → star_base → star_<c> → star2, across all gems. star2 (Force, additive) therefore lands on top of everything. Per-gem order is fine for the depth-tested parts [I].
7. The bonus branch (`bonus && !missed`) and the HOPO choice of top_star_<c> versus top_star_black are already right.

## 8. Open

- FUN_00158838 sets star_base.mat +0x80 = 1 (also smash_flamelight*.mat). The field's meaning is unknown, probably a runtime render flag [I]; it does not affect colour or spin.
