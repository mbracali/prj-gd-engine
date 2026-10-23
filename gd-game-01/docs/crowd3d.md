# 3D crowd members (CrowdChar, `crowd_limitsNN` regions): reverse engineering spec

GH1 PS2 (SLUS_212.24). Builds on `docs/crowd.md` (flat crowd, impostors, set_sizes) and `docs/camera_walk_crowd_lighting.md` §1–§4 (sizes timing, animate, lighting). This file corrects and completes those for the full 3D members.

Labels: **[C]** confirmed in the decompiled code, disassembly or data. **[I]** inference.
Conventions: Milo, Z up, row vectors (row0 = X, row1 = Y/forward, row2 = Z, then position). `p_world = p_local·R + t`.

## 0. Summary

- **Who.** The pool is the `crowd instance` characters (`crowd0` … `crowdN`): **10 (fest), 12 (arena, big_club, theatre), 17 (basement, small_club, small_club_multi)**. The first 6 are also the flat-block impostor archetypes. There is no other source of 3D crowd people. [C]
- **Where.** Each venue has 1–16 `crowd_limitsNN.mesh` floor patches. Each one becomes a **region**: the flat-crowd instances (Crowd01..05.mm) inside it, capped at the pool size. **One region is active at a time.** Its instances are removed from the flat blocks. Up to `(int)(regionCount·full)` pool chars stand on them, feet on the region floor and facing the instance's rotation (+Y, toward the stage). [C]
- **Which region.** Every camera shot calls `switch_region(shot.crowd_region)`. −1 means auto: the region with the best on-screen size over distance from the screen centre. Artists use an **empty region** (0 members) to mean "no 3D crowd": arena r1, big_club r2, fest r10, small_club r7, theatre r11. [C]
- **How many.** `set_sizes(full, flat)` from `update_crowd` on every regular or solo shot pick: full = 0.1 / 0.25 / 0.5 / 1 by excitement, 1 during the intro (`init`). [C]
- **Visibility.** Each frame, each member is culled by its bounding sphere (`sphere 0 0 0 53 bone_pelvis.mesh`) against the camera frustum. Nothing is drawn or animated while the shot has `hide_crowd 1`. There is no LOD (crowd chars have no `lod_screen_sizes` and no `lodN_` meshes). [C]
- **Animation.** The same `animate` as the impostors: shuffle, split by excitement weights, shared-LRU clip per group, loop, random phase. Members are animated on the song beat, scaled by `crowd_*_tempo`. They do not react to individual notes or star power. [C]
- **Lighting.** `crowd.env`, selected just before the members are drawn. Same as the impostors, after the band. [C]
- **Cost.** At most `pool` (≤ 17) skinned characters, one LOD, no shadows. Pool chars that are not active are not polled, except the 6 archetypes, which are polled every frame for the impostors. [C]

## 1. Data inventory

### 1.1 Pool (`arena/crowd.dta` `finish_loading`, default `$cheat_crowd` branch) [C]

`crowd instance a (head h mat m)` → `FUN_00173cc8`. It creates a `CrowdChar` named `crowd%d` (%d = current pool size) from archetype `a`, then runs `CrowdChar_v3` (head/outfit setup) and pushes it to the **front** of the pool list (+0x44, `FUN_00172d88`).

Archetype index a (order of `load_archetypes`): 0 crowd_male01, 1 crowd_male02, 2 crowd_male03, 3 crowd_female01, 4 crowd_female02, 5 crowd_female03. Clip restrict: male01/male03 = C_MALE1, male02 = C_MALE2, females = C_FEMALE.

| # | archetype | head (keep index h of the 8 `*_head.mesh`) | outfit mat | notes |
|---|---|---|---|---|
| 0 | male01 | 0 | 0 `crowd_male1outfit` | block 1 impostor |
| 1 | male02 | 4 | 1 `…outfit02` | block 2 impostor |
| 2 | male03 | 5 | 2 `…outfit03` | block 3 impostor |
| 3 | female01 | 1 | 0 | block 4 impostor |
| 4 | female03 | 5 | 2 | block 5 impostor |
| 5 | female02 | 0 | 0 | block 6 archetype (no Crowd06.mm in any venue) |
| 6 | male01 | 3 | 0 | |
| 7 | female03 | 3 | 0 | |
| 8 | male02 | 1 | 0 | |
| 9 | male03 | 2 | 0 | |
| 10 | female02 | 4 | 1 | not fest |
| 11 | female01 | 2 | 0 | not fest |
| 12 | male01 | 5 | 2 | only basement, small_club, small_club_multi |
| 13 | male03 | 4 | 1 | ″ |
| 14 | male03 | 1 | 0 | ″ |
| 15 | female03 | 1 | 0 | ″ |
| 16 | female01 | 5 | 2 | ″ |

**`CrowdChar_v3`** (0x1743d0) [C]. It loads the archetype's `top.view`, then, for each `(key value)` pair of the instance args:

- **`remove (head (m0) (m1) …)`:** every entry except entry `value` (0-based) has its meshes deleted. Only head `value` remains.
- **`swap (mat (outfit) (outfit02) (outfit03))`:** every mesh whose material is in another entry gets entry `value`'s material at the same position (`FUN_001c45e8(mesh, mat)`). mat 0 is a no-op.

The hands follow the `reset` handler (`set_hand 2`, horn) and the clip `HAND_*` events (already in the engine).

**Pool order.** The pool is LIFO: creation pushes to the front, `FUN_00172e58` takes from the front, and `FUN_00172d88` returns to the front. A fresh venue therefore hands out the **last created first** (crowd16, crowd15, …), and the archetypes (crowd0..5) last. [C]

### 1.2 Regions per venue

Computed with the exact game test (§2.1) from the shipped scenes. The script reproduces `FUN_00170da8` with the full 3×3 inverse (`FUN_0024b748`) and the XY triangle test (`FUN_001e3600`), with 0 < local z < 100.8 and a cap at the pool size (no region reached the cap). n = member spots. floor = region floor z (world z of limits vertex 0). centre = AABB midpoint of the spots. ρ = **full AABB diagonal** (§2.1).

Venue files: arena, big_club, small_club and small_club_multi in `gen/lighting.rnd_ps2`; basement, fest and theatre in `gen/crowd.rnd_ps2`. `big_club/gen/shadowtest.rnd_ps2` has 33 limits but is a test scene that is not loaded **[I]**.

| venue (pool) | region: n, floor z, centre (x,y), ρ |
|---|---|
| arena (12) | r0: 8, −387.4, (86,−437), 145 · **r1: 0** |
| basement (17) | r0 17 −2.1 (−42,0) 250 · r1 17 (−26,−55) 285 · r2 17 (−22,−118) 287 · r3 16 (−36,−178) 275 · r4 12 (−35,−225) 290 · r5 12 (118,−184) 128 · r6 15 (117,−63) 154 · r7 7 (2,−103) 100 · r8 6 (−144,−18) 81 · r9 9 (−118,−102) 118 · r10 10 (−40,−12) 114 · r11 9 (66,−15) 115 (all floor −2.1) |
| big_club (12) | r0 11 −49.5 (−1,−49) 196 · r1 11 −49.5 (−2,−91) 197 · **r2 0** (88.4) · r3 11 −49.5 (−212,14) 134 · r4 9 −49.5 (212,−45) 131 · r5 10 88.4 (375,−549) 300 · r6 9 88.4 (−432,−268) 211 · r7 10 88.4 (422,−265) 219 · r8 9 88.4 (−373,−544) 294 · r9 12 88.4 (−3,−649) 420 · r10 12 −49.5 (−196,−508) 181 · r11 12 −49.5 (−232,−201) 162 · r12 12 −49.5 (229,−502) 159 · r13 12 −49.5 (237,−205) 133 · r14 12 −49.5 (−1,−382) 238 |
| fest (10) | r0 9 (19,−399) 122 · r1 10 (16,−443) 151 · r2 8 (−501,−658) 163 · r3 8 (488,−646) 152 · r4 10 (121,−324) 159 · r5 10 (888,−1954) 105 · r6 10 (−204,−399) 131 · r7 10 (−65,−1088) 245 · r8 10 (−121,−328) 162 · r9 9 (134,−363) 122 · **r10 0** (all floor −108.1) |
| small_club (17) | r0 17 (−39,−54) 251 · r1 17 (−37,−126) 245 · r2 17 (−79,−175) 261 · r3 17 (−86,−233) 309 · r4 17 (−48,−293) 299 · r5 17 (−58,−413) 262 · r6 17 (−46,−351) 293 · **r7 0** · r8 10 (−137,−73) 86 · r9 16 (−127,−507) 174 · r10 11 (−53,−72) 109 · r11 12 (53,−72) 105 · r12 11 (−133,−156) 111 · r13 12 (8,−154) 117 · r14 17 (−6,−507) 168 · r15 17 (−32,−83) 250 (all floor 0) |
| small_club_multi (17) | r0 5, 0, (47,−43), 71 (one instance from each block, i.e. the whole crowd) |
| theatre (12) | r0 12 −76.3 (−11,364) 252 · r1 11 −23.5 (−404,−41) 281 · r2 12 −47.9 (384,185) 216 · r3 11 −23.5 (384,−38) 294 · r4 12 −76.3 (385,282) 198 · r5 12 −76.3 (−234,410) 229 · r6 11 −76.3 (243,405) 231 · r7 11 −76.3 (−258,381) 112 · r8 12 −47.9 (−382,190) 244 · r9 12 −76.3 (−401,238) 224 · r10 12 −76.3 (374,312) 192 · **r11 0** (−23.5) |

- All limits meshes have 25 verts / 32 faces (arena: 15/16), material `crowd_limits00.mat`, `showing` true, their own geometry, and local == world.
- Many have scaled or sheared worlds (e.g. theatre r5: rows (0.89,−0.46,0), (0.31,0.59,0)), so a **full inverse** is required, not a transpose. [C]
- Shot usage of `crowd_region` / `hide_crowd` per venue: see `docs/camera_walk_crowd_lighting.md` §2 "Per-shot use". Arena always uses r1 (empty), so the arena never shows 3D members.

## 2. Algorithms

### 2.1 Region build: `FUN_00170da8(crowd, region, limitsMesh)` [C]

Called from `FUN_00171558` (crowd build, after the blocks) for i = 0, 1, … while `arena::crowd_limits%02d.mesh` exists. Regions go into the list crowd+0x20 in index order (push back; node 0x60 bytes, record at node+0x10).

```
mesh.removeFromAllDrawLists()          // FUN_001b28a0: never drawn
mesh.trans.setParent(null)             // FUN_001d9f80
poolN = size(crowd.pool +0x44)         // all crowd instance chars
Winv = inverse3x3(mesh.world.R), tinv = -mesh.world.t · Winv     // FUN_0024b748
first = true
for block in crowd.blocks (Crowd01..):                // vector +8, 0x1c bytes each
  for inst in block.mm.instances (list order):
    l = inst.pos · Winv + tinv
    if 0 < l.z < crowd.flatHeight (+0x88 = 100.8) and pointInMesh(geomOwner(mesh), l):   // FUN_001e3600
      if region.insts.size >= poolN:
        if crowd_limits_strict: print warning; stop the WHOLE scan (all blocks)
      region.insts.push(inst node); region.blocks.push(block)
      grow AABB (min,max) by inst.pos   (first: min = max = pos)   // FUN_00244bd8
v0 = geomOwner(mesh).verts[0].pos · mesh.world
region.floorZ  (+0x40) = v0.z
region.centre  (+0x20) = (min + max)/2
region.radius  (+0x30) = |max − min|        // full diagonal (disasm 0x1713f4: vmul/vadd/vsqrt of max−min)
region.limits  (+0x44) = mesh
```

- **`FUN_001e3600` pointInMesh** (0x1e3600) [C]. For each face (a, b, c) of the geometry owner (mesh+0x128 → verts +0x100 stride 0x40, faces +0x108), it solves `p − a = u·(b − a) + v·(c − a)` in XY only (two branches to avoid dividing by a near-zero `c.x − a.x`, epsilon 1e-5). It returns true on `0 ≤ u ≤ 1, v ≥ 0, u + v ≤ 1`.
- **Empty region:** min/max are never written, so centre and radius are uninitialised stack data **[C that they are not set; I what the values are]**.
- **Radius readout:** the Q→GPR move decodes as `cfc2 v0, vc6`. I take it as the sqrt result **[I]**.

### 2.2 Region switch: `FUN_001727b0(crowd, idx)` [C]

Callers:

- the per-shot setup at 0x16f3b8 region (decomp line 00160000.c:10110), right after the shot's camera transform is set (`FUN_001da730`), followed by `crowd+0xb0 = !hide_crowd` and `crowd+0xb4 = force_cam_facing`;
- `crowd switch_region n` (handler in `Crowd_v1`; no shipped script calls it).

The camera script then calls `update_crowd` (set_sizes + animate) **after** `change_to_regular_shot` / `change_to_solo_shot` (`arena/camera.dta` 177–187). Intro, win and lose shots only switch the region.

```
if no regions: return
if idx < 0:                                   // auto
  best = 0; R = last region
  cam = main camera (DAT_00363824+8)
  for r in regions (in order):
    s = screen(cam, r.centre)                 // FUN_001b1100: NDC → [0,1] within the cam's screen rect (+0x314.. +0x320); no behind-camera check
    dist = |s − (0.5, 0.5)|
    depth = dot(r.centre, camWorld.row1) + cam[+0x134]   // view-space forward distance
    size = |r.radius · cam[+0x140] / depth| · cam[+0x31c]  // +0x140 = projection x scale 1/tan(fovX/2) [I]
    score = min(size, 15) / max(dist, 0.2)
    if score > best: best = score; R = r
else:
  R = regions[idx]                            // walks idx nodes; no bounds check
for c in leaving (+0x54): returnToPool(c); clear leaving
if R != active (+0x24):
  for block: splice block.removed (+0x18) back to the FRONT of block.mm list    // 0x172ac0
  for each (inst, block) of R: move inst node to the FRONT of block.removed       // 0x172b48
  active = R
  for i, c in enumerate(active list +0x4c) while i < R.count:
    c.setLocalXfm(rot = R.inst[i].rot rows, pos = (R.inst[i].x, R.inst[i].y, R.floorZ))   // vtable +0x44 = CrowdChar_v7
  set_sizes(crowd.full +0x80, crowd.flat +0x84)
```

- `CrowdChar_v7` = `CharBase_v7` (SetLocalXfm on the char's top transform + UpdateWorld), then `FUN_00174b60` (walk flag +0xb4 = 0).
- Members stand **on the floor** (`floorZ`), not at the sprite centre. Their rotation is the instance rotation, which is identity except for the yawed fest instances, i.e. facing +Y toward the stage.

### 2.3 Sizes: `FUN_00172ec8(full, flat)` (script `crowd set_sizes`) [C]

Flat part, per block (corrects `docs/crowd.md`; the list operations are confirmed in the disassembly at 0x173070 and 0x1730e0):

```
visible = (int)((block.count(+8) − size(block.removed)) · flat)
cur = size(block.mm list)
if visible > cur: move the first (visible − cur) nodes of block.hidden (+0x10) to the FRONT of mm
if visible < cur: move the first (cur − visible) nodes of mm to the FRONT of block.hidden
```

So shrinking hides the **front** of the list, and hidden and region-removed nodes come back at the front. The visible set is a stack-like permutation, not "the first N of the file". Reproduce it with two linked lists per block.

3D part (only if a region is active):

```
target = (int)(R.count · full)
while active.size > target:
  c = active.back; active.pop_back
  if excitement (DAT_00363780) != 0: returnToPool(c)
  else:                                            // Boot only
    leaving.push_back(c)
    if crowd_walk_flags (+0xbc, never set): FUN_00174ca8 walk away along crowd_walk_offset
while active.size < target:
  if pool empty: returnToPool(leaving.front); leaving.pop_front
  c = pool.pop_front (c.frameScale = crowd.frameScale +0xc4)
  active.push_back(c)
  i = active.size − 1
  c.setLocalXfm(R.inst[i].rot, (R.inst[i].x, R.inst[i].y, R.floorZ))
```

- Invariant: `active[i]` stands on `R.inst[i]`. Spots `≥ target` are empty: their sprites were removed, and no char is there. [C]
- **Leaving chars** (Boot only) keep standing, are drawn and animated, and go back to the pool in the poll once they are off screen (+0x34 == 0), or at the next region switch. With no walk flags in GH1 they simply stand at their old spot until culled. **[C; I that this is ever noticeable, since a shot cut usually follows]**
- `returnToPool(c)` (`FUN_00172d88`):
  - if c is not an archetype (+0xb0 bit0 clear), `CrowdChar_v5` runs: `AnimInstance_v5` (CharBase reset to LOD 0, frame scale 1, clears the driver, sends `reset`, so hand 2), then stop walking;
  - then +0x98 = crowd frame scale and push to the pool front.
- `FUN_00172c88` (arena exit, `FUN_00169b48`): frame scale 1, `set_sizes(0, 0)`.

### 2.4 Per-frame poll: `FUN_001720e8` (ArenaPanel poll `FUN_001691a0`) [C]

```
for c in leaving: if c.screenSize(+0x34) == 0: returnToPool(c); erase      // last frame's value
if crowd shown (+0xb0 = !hide_crowd):
  for c in active, leaving, and pool chars with archetype flag: FUN_00174ad0(c)
FUN_00174ad0(c):
  c.vtbl+0x54 (CrowdChar_v9)(mainCam)        // cull/size
  FUN_001817c0(c, beat = ticks/480, dBeat, dSec = dms·0.001)   // ×c.frameScale(+0x98) on beat and dBeat
```

- **`CrowdChar_v9` (0x174c20):** archetypes force +0x34 = 1 (always "visible") and set LOD 0. Everyone else runs `CharBase_v9` (0x18e0a8):
  - sphere = (+0x40 offset, +0x50 radius) in the space of bone +0x60, from the char's `sphere` key (`FUN_0018e3d8`, key "sphere"): crowd `(sphere 0 0 0 53 bone_pelvis.mesh)`;
  - if `FUN_002480d8(sphere, cam frustum +0x2a0)` culls it: +0x34 = 0;
  - else: +0x34 = |r·cam[+0x140] / depth|·cam[+0x31c] (screen size). LOD = from `lod_screen_sizes` with 0.91/1.09 hysteresis. The crowd has none, so the LOD is always 0.
- **`FUN_001817c0`** always stores the times. When +0x34 == 0 it only runs the negative-priority pollables and **skips the pose/skeleton update** (goto end) **[C for the branch; I that the driver is among the negative-priority pollables, so clip time still advances]**.
- Pool chars that are neither active nor archetypes are **not polled at all**: their time freezes.

### 2.5 Draw: `FUN_00172350(crowd)` (last in `FUN_001696d8`, after the venue views, blur and band) [C]

```
if !shown: return
select crowd.env (DAT_003637e4 = Arena+0x9c = arena::crowd.env)
for c in active:  c.draw()     // CrowdChar_v6: if +0x34 != 0 draw top.view (FUN_002ec850)
for c in leaving: c.draw()
… impostor renders + flat blocks (docs/crowd.md)
```

- Members are depth-tested against the venue. There is no shadow (`CROWD_COMMON` has no `show_shadow`) and no outline or alpha.
- The impostor camera is relative to the archetype's **current** world. An archetype that is also an active member therefore still renders correctly (`docs/crowd.md` §1).

### 2.6 Animation (`crowd animate`, `FUN_00173e70` / `FUN_00173508`) [C, details in camera_walk_crowd_lighting.md §3]

- **Chars:** active list, then the whole pool (n = pool size, archetypes included, leaving excluded). Fisher–Yates shuffle with `FUN_0024a1d0`. Split by weights with `round(cum·n/total)` boundaries.
- **Per char:**
  - `FUN_0017fc28(driver, (group flags), kAnimReset|kAnimGraphLoop)` picks the least recently used clip matching `group & restrict` (shared `main::crowd.cset` LRU).
  - The clip loops.
  - frame = start + fmod(frame + rand(lo, hi) − start, end − crowd.c0(1.0) − start), wrapped positive.
- **Weights per excitement** (bad, ok, great, idle): Boot (1,0,0,0), Bad (.8,.2,0,0), Okay (0,.8,.2,0), Great/Peak (0,0,1,0), init (0,.9,.1,0). Phase ranges: bad ±5, ok ±0.25, great −1..2, idle ±0.25 beats.
- **Order per shot pick:** switch_region, then set_sizes (new members come from the pool with whatever clip the last animate gave them, or none if they were reset), then animate (everyone gets a fresh clip). There is no visible frame without a clip.
- **Tempo:** song events `crowd_half_tempo` / `crowd_double_tempo` / `crowd_normal_tempo` → `set_frame_scale` (`FUN_001736c8`) on active and pool chars (+0x98), and crowd+0xc4 for chars taken later.
- **No other reactions:** misses, star power, win and lose only act through excitement on the next shot pick (camera_walk_crowd_lighting.md §1).

### 2.7 Consumers

- **Guitarist eyes, mode 3** (`FUN_00172cc8`, called at 00180000.c:10907):
  - if active is empty: target (0, 0, 0);
  - else `c = active[randInt(0, n)]` (`FUN_0024a1d0`), target = `c.top.view` world position + (0, 0, 48) (`FUN_00174c80`: `*(c+0x18)+0x80` → +0x90);
  - the caller then applies `z += (charRootZ − z)·0.7` (docs/faces_shadows.md).
  - The engine's current fallback (0, 0, 0.7·headZ) already matches the empty case. [C]
- **Camera** `crowd_region` / `force_cam_facing` / `hide_crowd`: §2.2.

## 3. Engine implementation plan

Files: `engine/src/render/crowd_renderer.{h,cpp}`, `engine/src/render/venue_renderer.cpp`, `engine/src/game/venue_camera.{h,cpp}`, `engine/src/scene/character.{h,cpp}`.

1. **Pool (replaces `archetypes_`).**
   - Parse crowd.dta's `finish_loading` default branch, or hard-code the §1.1 table with the two venue conditions.
   - Create `poolN` `Character`s with the existing crowd `Spec` (servos 0, restrict, tempo 3, folder "crowd").
   - Per char, keep head `h` (`setShowing`, as now) and **swap outfit mat m**: add `Character::swapMaterial(from, to)` that replaces the material on every mesh using `from`, driven by the archetype's `swap (mat …)` list.
   - Chars 0..4 are the block 1..5 impostors (as now, `target` RTs only for those).
   - `std::list<int> pool_` (front = last created), `active_` (vector, index = region spot), `leaving_`.
2. **Regions.** In the ctor, for `crowd_limits%02d.mesh` (i from 0 until missing) do exactly §2.1:
   - use the `geomOwner` mesh's `verts`/`faces`, the mesh's `trans.world` with a full 3×3 inverse, the XY barycentric test, `0 < l.z < height_`, and a cap at `poolN` that stops the whole scan;
   - store `{vector<(block, instIdx)>, centre, radius = |max − min|, floorZ = (v0·world).z}`;
   - mark the limits meshes never drawn (they are not in `<section>.view`, so nothing to do beyond not drawing them).
3. **Flat lists.** Give each `Block` three `std::list<int>` (instance indices): `mm` (initially 0..count−1), `hidden`, `regionRemoved`. Implement §2.3's splices literally. `draw()` iterates `mm` instead of `[0, count·shown_)`.
4. **API driven by the camera** (VenueRenderer, on each shot switch):
   - `crowd_->switchRegion(shot.crowdRegion)` with the new shot's camera (position, rotation, fovX, aspect) for the auto score; then, only for regular and solo picks, `crowd_->setSizes(full, flat)` and `crowd_->animate(weights)` (as `update_crowd`, including `reduced_crowd_shots` capping flat and `$no_crowd`);
   - at song start (`init`): `setSizes(1, 1)`, `animate(0, .9, .1, 0)`;
   - add `crowdRegion` (default −1) and `forceCamFacing` to `VenueCamera::Shot` (parse `crowd_region`, `force_cam_facing`);
   - auto score: s = projected centre in [0,1]², depth = dot(c − camPos, camFwd), size = |ρ·(1/tan(fovX/2))/depth|, score = min(size, 15)/max(|s − ½|, 0.2), strict `>` against 0, default = last region;
   - **skip empty regions in auto [I]** (their centre and radius are garbage in the original);
   - clamp explicit idx to the region count.
5. **Poll** (`update`, every frame, only when `!hideCrowd`):
   - return culled leaving chars (Boot only);
   - for active + leaving + archetypes: sphere test (pelvis world + radius 53 against the main camera frustum) → `visible` flag;
   - `poll(beat·frameScale, ms)` (skip the skinning update when not visible, as an optimisation equal to the original);
   - do not poll other pool chars.
6. **Draw** (inside `CrowdRenderer::draw`, before the flat quads):
   - for active and leaving with `visible`: `character->draw(vp, crowd.env)`, placement = `Xfm{inst.rot, (inst.x, inst.y, floorZ)}` set at assignment time (`setPlacement`);
   - the impostor pass keeps rendering archetypes from their current placement (eye = A.pos + (0, 0, h/2)·M − d·camFwd), so an archetype that is a member still renders right;
   - order: venue views → band → crowd (members, then impostors), as `FUN_001696d8`.
7. **Animate** (§2.6):
   - one shared LRU per clip set (static map clip → last-used stamp), shuffle over active + pool (archetypes included);
   - `kAnimReset|kAnimGraphLoop`, random phase;
   - `returnToPool` resets the char (hand 2, no clip, frame scale = crowd's).
8. **Eyes.** `CrowdRenderer::randomMemberTarget(rng) → optional<vec3>`: a random `active_` member's placement position + (0, 0, 48). VenueRenderer passes it to `guitarist_->setEyeCrowdTarget()` when eye mode 3 re-targets (keep the (0, 0, 0) fallback).
9. **Budget:** ≤ 17 extra skinned chars plus 5 impostor RTs per frame. Load the six crowd rnds once and instance the per-char scene state if memory matters. The original keeps a full copy per CrowdChar **[I]**.

Verification: theatre shot `flr_far_*` with `crowd_region 0` should show 12 lit people on the front rows (floor −76.3) in place of their sprites, 6 at Okay. Arena shows none. Eye mode 3 should glance at them.

## 4. Key addresses

| Address | Role |
|---|---|
| 0x170da8 | region build (limits → instances, centre, ρ, floorZ) |
| 0x1e3600 | point in mesh, XY triangle test |
| 0x24b748 | 3×3 inverse |
| 0x171558 | crowd build (blocks, then the limits loop) |
| 0x1727b0 | switch_region (auto score, splices, placement) |
| 0x172ec8 | set_sizes (flat splices, 3D add/remove) |
| 0x172d88 / 0x172e58 | return to pool (front) / take from pool front |
| 0x1720e8 | poll (leaving cleanup, per-char cull + anim) |
| 0x174ad0 | per-char poll: vtbl+0x54 then FUN_001817c0 |
| 0x174c20 / 0x18e0a8 | CrowdChar_v9 / CharBase_v9: sphere cull, screen size, LOD |
| 0x18e3d8 | CharBase load of `top.view` and `sphere` |
| 0x18e380 / 0x18e280 | SetLod: show `lodN_*` meshes (unused by the crowd) |
| 0x172350 | draw (crowd.env, members, impostors) |
| 0x174be8 | CrowdChar_v6 draw if +0x34 ≠ 0 |
| 0x173cc8 / 0x174380 / 0x1743d0 | instance / CrowdChar ctor / head+outfit setup |
| 0x173e70 / 0x173508 | animate / group clip + random phase |
| 0x1736c8 | set_frame_scale |
| 0x174ca8 | walk away (unused: no crowd_walk_flags) |
| 0x172cc8 / 0x174c80 | random active member for eyes, top.view pos + (0, 0, 48) |
| 0x1b1100 | world → screen [0,1] in the camera rect |
| 0x2aa0a8 | list transfer(position, first, last) |
| DAT_00363780 | arena excitement (Boot = 0 → leaving list instead of pool) |
| DAT_00363a70 | crowd_limits_strict (warning only) |

Crowd object fields: +0x8 blocks, +0x20 regions, +0x24 active region, +0x28 archetypes, +0x44 pool, +0x4c active, +0x54 leaving, +0x80 full, +0x84 flat, +0x88 flat height, +0xb0 shown, +0xb4 force_cam_facing, +0xbc walk flags, +0xc4 frame scale. CrowdChar: +0x1c LOD, +0x34 screen size (0 = culled), +0x40/+0x50 sphere, +0x98 frame scale, +0xb0 archetype flag, +0xb4 walking, +0xb8 instance args.
