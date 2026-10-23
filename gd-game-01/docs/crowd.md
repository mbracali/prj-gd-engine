# Crowd: reverse engineering report (GH1 PS2, SLUS_212.24)

Confidence labels: **[C]** confirmed in the decompiled code or data. **[I]** inference.
Milo conventions: Z is up. Transforms are row vectors (row0 = X/right, row1 = Y/forward, row2 = Z/up, then pos). `FUN_0024b608(a, b, out)` computes out = a then b, so R_out = R_a·R_b and p_out = p_a·R_b + p_b (it is the call `RndTransformable_v1` uses for local→world).

## Summary: the causes of the two reported problems

- **(a) "Static sheet of paper".** Two things are missing.
  1. **The quads are billboards.** The quad mesh gets transform constraint **8 = FastBillboardXYZ** (`FUN_001da750(quad+0x40, 8, 0, 0)` in `FUN_00171558`). The file's own `Crowd_*.mesh` also has `constraint 8`. `PsMultiMesh_v5` (0x1a34a0) handles constraint 8 as follows: for every instance, the matrix it uploads is **the current camera's world rotation rows plus the instance position**. The instance's rotation is ignored. Each sprite is therefore a screen-aligned quad (yaw, pitch and roll all follow the camera), centred on the instance. **[C]**
  2. **The impostor is rendered with the main camera's orientation every frame.** It is not a fixed front view. When the camera orbits or tilts, the sprite image shows the members from the matching angle. **[C]**
  - The engine reads the transformable `constraint` field and throws it away (`scene.cpp` `readTransformable`: `r.u32(); // constraint`).
- **(b) Theatre: "floor above the crowd".** This is not caused by crowd height offsets. It is a venue transform bug.
  - `theatre.rnd_ps2` has `stage.1.mesh` (stage_floor.mat) and `stage.3.mesh` (dome.mat) at local z = 74.6. They span x ±451, y −511..556, which covers the whole audience area.
  - `drum_riser.1.mesh` and `drum_riser.2.mesh` (z = 25.4, same footprint) have the same problem.
  - These meshes store **identity local and world** transforms and set the transformable's last field (`parent`, read by the engine as a parent) to `stage.mesh` / `drum_riser.mesh`.
  - In the game, that field is the **transform owner**. `RndTransformable_t1_v8` (0x2eadc8) stores it at `xfm+0x10`. All matrix reads and writes go through `*(xfm+0x10)`: `PsMesh_v5` draws with `*(mesh+0x50)+0x60`, `RndTransformable_v1` skips self-update when `+0x10 != self`, and `FUN_001da6c8` SetLocalXfm writes `*(+0x10)+0x20`. So these meshes are drawn with **stage.mesh's world**: scale (0.64, 0.22, 0.64), position (−0.55, 498.74, −63.54). That puts the stage top at z = 74.6·0.64 − 63.54 = **−15.8** (the stage surface; band_shadow is at −14.7), y 386..621. **[C]**
  - The engine draws `trans.world` (`scene_renderer.h:68`). `AnimPlayer` recomputes `world = local ∘ parent.world` only for dirty or animated nodes, so static proxies keep the stale identity world. That leaves a stage-floor-textured plane hanging at z = 74.6 over the crowd (heads reach about +77, floors are at −76/−48/−23). **[C for data and code; I that this is the plane the user sees, though it is the only floor-like surface above the audience]**
  - **Fix:** for any transformable whose `parent` field names another object, use that object's world (and local) instead of its own stored matrices.
  - Every venue has such proxies: arena 54 + 12, basement 8 + 1, big_club 55 + 10 (+30 shadowtest), fest 33, small_club 47 (+1 hot_white), small_club_multi 12, theatre 26. In all of these the stored world differs from the owner's. Examples: theatre `stage_hole.1`, `kickdrum0x.1`, `sconce0x.1`; big_club `main_hall_steps.*`, `roof1.1`, `bottomfloorrug_*`.

## 1. Flat crowd: config, quad, billboard, impostor

### Config (`FUN_00170650`, Crowd constructor; `this` = Crowd)

| Field | Key | Source | Default / notes |
|---|---|---|---|
| +0x60 vec3 | `crowd_walk_offset` | `arena.dta` (0 −500 0) | negated if facing_backward |
| +0xb8 | `crowd_facing_backward` | `arena.dta` FALSE | |
| +0x88 | `crowd_flat_height` = h | `arena.dta` 100.8, then optional override from `DAT_003637b4` (per-venue DataArray **[I]**) | |
| +0xbc | `crowd_walk_flags` | `arena.dta` (optional) | not present in data, so 0 |
| +0xa4 | `crowd_forward_facing` | `DAT_003637b4` (optional) | **99** |
| +0x8c | d = h·0.25 / tan(0x3e32b8c2 = 10°) | computed | `FUN_0027d600` = tan; 100.8 → **142.92** |
| +0xc4, +0xc0 | | | 1.0 (frame scale; +0xc0 is used by FUN_00173508) |
| +0xb0 | | | 1 (crowd shown = !hide_crowd) |

Other optional venue keys read in `FUN_00171558`: `crowd_striped_quad` (8-vertex quad variant) and `crowd_shadows` (+0xa8). **None of crowd_flat_height, crowd_forward_facing, crowd_striped_quad, crowd_shadows or crowd_walk_flags appear anywhere in the extracted .dta/.dtb.** Every venue therefore uses h = 100.8, forward_facing 99, the plain 4-vertex quad, no shadows and no walking. **[C]**

### Construction (`FUN_00171558`, called by `FUN_00169490` right after the arena's `finish_loading` handler, which runs the `crowd instance` calls)

1. **Render target** +0x98 is 128×256 (`FUN_001d5d98(rt, 0x80, 0x100, 0x10, 2, 1)`). If the CrowdChar pool +0x44 is empty, the bitmap `crowd128_mip.bmp` is used instead.
2. **Material** +0x9c, set up as follows **[C for the calls; field names from `tools/rnd_classes/material.py`]**:
   - `FUN_001be288(mat, 0, 0, 0, 0)`: use_environ (+0x40) = 0, vert_amb = 0, vert_dyn = 0, normalize = 0. The quad is **unlit**.
   - `FUN_001be408(mat, 3)`: blend = 3 (SrcAlpha).
   - +0x5c alpha_cutout = 1.
   - Stage 0: tex = rt, wrap = 0.
3. **Quad** +0xa0 = `FUN_00170160(h·0.5, h, quad, striped)`. Internally it halves both, so half-width = h/4 = 25.2 and half-height = h/2 = 50.4.
   - Vertices (x, 0, z) with uv: (−w, +h) uv(0,0); (−w, −h) uv(0,1); (+w, +h) uv(1,0); (+w, −h) uv(1,1).
   - Faces (0,1,2), (1,3,2).
   - The quad is centred on the instance origin and lies in the local XZ plane.
   - The striped variant uses x = ±w and ±w/10 with u = 0, .45, .55, 1, identical to the file's `Crowd_A36.mesh`.
   - Then `FUN_001c45e8(quad, mat)` and **`FUN_001da750(quad+0x40, 8, 0, 0)` set constraint = 8 (FastBillboardXYZ)**. Constraint names come from `FUN_001db750`: 5 BillboardZ, 6 BillboardXZ, 7 BillboardXYZ, 8 FastBillboardXYZ.
4. **`crowd_plane.mesh`** is loaded into +0xac and used only when `crowd_shadows` is set, which no venue does.
5. **Impostor camera** +0x18: `FUN_001b11e8(cam, rt)` (render to rt), then `FUN_001b1df8(10, 1000, 0x3eb2b8c2, cam)`: near 10, far 1000, **fov 0.349 rad = 20°**.
   - The projection (`FUN_001b1eb8`) uses x-scale 1/tan(fov/2) and up-scale 1/(tan(fov/2)·a), with a = (rt.h/rt.w)·(+0x320/+0x31c) = 2·(ratio, assumed 1) **[I for the ratio]**.
   - So **fov is horizontal**. At distance d the view covers width 2·d·tan10° = h/2 and height h, exactly the quad.
6. **Blocks.** For i = 1.. while `arena::Crowd%02d.mm` exists (up to 6 get an archetype):
   - **z shift:** `inst.pos.z += (this+0x88 − venue.crowd_flat_height)·0.5`. When the venue key is absent the shift is **0**, which is always the case.
   - `FUN_001b28a0(mm)` removes the multimesh from every view's draw list. **Only the Crowd draws it.**
   - The material of the file's own mesh gets its texture replaced by rt, then `FUN_001cbcf0(mm, quad)` replaces the mesh with the runtime quad.
   - Block record (0x1c bytes in vector +8):
     - [0] archetype CrowdChar
     - [1] multimesh
     - [2] instance count
     - +0x10 list of hidden-by-size instances
     - +0x18 list of instances removed by the active region
   - **Archetype for block i = the i-th char created by `crowd instance`** (pool list +0x44 is push-front, walked i steps from the tail). If fewer chars exist, the archetype is null.
   - The archetype gets flag +0xb0 = 1 and is placed at the **world origin with rotation M** (below).
   - With the shipped `crowd.dta` default branch, the archetypes are: block 1 = archetype 0 (head 0, mat 0), block 2 = 1 (head 4, mat 1), block 3 = 2 (head 5, mat 2), block 4 = 3 (head 1, mat 0), block 5 = 5 (head 5, mat 2), block 6 = 4 (head 0, mat 0). The heads differ from the engine's `headIndex % n`.
   - **M (crowd frame).** `this+0x70` accumulates the instance **Y axes** (forward rows) of blocks with i−1 < crowd_forward_facing, i.e. all of them. At the end it is normalised. `M = FUN_0024adf8(F = +0x70, up = (0,0,1))`, which is MakeRotMatrix: y = F̂, x = norm(y × up), z = x × y. For the theatre and basement every instance rotation is identity, so M = I. Fest has some instances yawed 25°.
7. **Regions:** see section 2.

### Per-frame draw (`FUN_00172350(crowd, 0)`; caller `FUN_001696d8` = ArenaPanel draw)

If crowd+0xb0 (not hidden):

1. `FUN_002ec850(DAT_003637e4)` draws/selects an unidentified drawable first. **[I]** It is probably the crowd environment or lighting; the engine uses `crowd.env`.
2. Draw the full 3D CrowdChars in lists +0x4c (active) and +0x54 (leaving).
3. Build the impostor camera transform:
   - Default (crowd+0xb4 == 0): N = C·Mᵀ, where C = the main camera world rotation (`*(DAT_00363824+8)` → +0x50 → rows +0x60/+0x70/+0x80). The camera position in local space is −d·N.row1.
   - Shot flag `force_cam_facing` → +0xb4 (`FUN_0016f3b8`) switches to the fixed view `FUN_001722a0`: rot diag(−1, −1, 1) at (0, +d, 0), i.e. looking at the archetype's front from +Y. With facing_backward it uses identity at (0, −d, 0).
   - All 46 shipped shots set `force_cam_facing 0`. The fixed view also applies to blocks with index ≥ crowd_forward_facing (99, never reached).
4. For each block i with instances:
   - If block i has an archetype A: take A's world (`FUN_0018e238`) and replace its position with A.world·(0, 0, h/2), the body centre. The impostor camera world = camLocal ∘ thatXfm:
     - **R = N·M = C**: the camera has exactly the main camera's world rotation.
     - **p = (0, 0, h/2)·M + A.pos − d·C.row1**, i.e. the archetype centre minus d along the main camera's forward axis.
     - The archetype is at the world origin, so p = (0, 0, 50.4) − 142.9·camFwd.
   - Set it with `FUN_001da6c8` + UpdateWorldXfm, select the camera (render into rt; clearing presumably happens on select **[I]**), and draw A (vtable +0x3c).
   - **Re-rendered every frame, for every block, into the single shared rt**, which is why the next step comes immediately.
   - Then select the main camera again and draw the block's multimesh (`FUN_002ec850` → `PsMultiMesh_v5`). Each instance uses the camera-aligned billboard matrix: rows = C, position = instance position.
   - If crowd_shadows is set (never), the multimesh is drawn a second time with `crowd_plane.mesh` swapped in.
5. **Impostor lighting.** The archetype is drawn at the world origin, in its own lit materials, under whatever environment is active (the one selected in step 1 **[I]**). The quad itself is unlit (use_environ 0).

**Recipe for the engine.**

- Impostor for block i: archetype at the origin with rotation M (identity in the theatre and basement). View rotation = the main camera's world rotation, eye = (0, 0, h/2) − d·camFwd, d = h/(4·tan 10°). Horizontal FOV 20°, aspect w:h = 1:2, near 10, far 1000.
- Draw each instance as a quad h/2 wide and h tall, centred at the instance position (no z offset in shipped data), spanned by the camera's right (C.row0) and up (C.row2) axes.
- Texture = rt with alpha cutout, SrcAlpha blend, unlit.
- Under perspective this lines up exactly with what a 3D member at that spot would look like, only flattened.

### Draw order (`FUN_001696d8`) [C]

1. Venue views: list +0xd8, each `FUN_002ec850(view+0x30)`.
2. Blur effect: `FUN_0016d4c8`.
3. Optional object at +0x144.
4. Select the environment `*(+0xdc)+8`.
5. Band characters (list +0x74).
6. **Crowd last:** 3D crowd chars, then per block impostor render + flat multimesh.

Everything is depth-tested (normal z-buffer **[I]**). The cutout quads cannot be hidden by geometry drawn later, because nothing follows the crowd.

## 2. Full 3D crowd members (regions)

### Region build (`FUN_00170da8`, once per `arena::crowd_limits%02d.mesh`, i = 0..)

- The limits mesh is removed from draw lists (`FUN_001b28a0`) and from its transform parent (`FUN_001d9f80`). **It is never drawn.**
- For every block and instance in order: transform the instance position into the limits mesh's local space (inverse of its world). Keep the instance if **0 < local z < h** and it passes the point-in-mesh test `FUN_001e3600`, which is a 2D polygon test on the floor patch **[I]**.
- At most `pool size` instances are kept (count of list +0x44 at build time, i.e. all `crowd instance` chars). When the limit is reached and `crowd_limits_strict` (TRUE in `arena.dta`; `FUN_001705f0` → `DAT_00363a70`) is set, a warning is printed.
- Region record:
  - [0..3] vector of instance ptrs, [4..7] vector of source blocks.
  - [8..0xb] centre = midpoint of the AABB of the kept positions.
  - [0xc] radius (sqrt of a squared length; exact operand unclear **[I]**).
  - **[0x10] floor z** = world z of vertex 0 of the limits mesh.
  - [0x11] limits mesh.
- The theatre has 12 regions, all flat: z −76.3 (front, y 160..526), −47.9, −23.5 (rear, y −188..−11). Instance z ≈ floor + h/2 (−27 / +2.5 / +26), so the quads stand on the raked floor.

### Region switch (`FUN_001727b0(crowd, idx)`; `crowd switch_region`, and every camera shot via `FUN_0016f3b8` with the shot's `crowd_region`, default −1)

1. **idx < 0 (auto):** for each region with centre c and radius ρ:
   - s = screen position of c (`FUN_001b1100`).
   - dist = |s − (0.5, 0.5)|.
   - size = |ρ·cam.projX / viewDepth(c)|·cam[0x31c].
   - score = min(size, 15) / max(dist, 0.2). The highest score wins.
   - If every score is ≤ 0, the last region is used. There is always an active region.
   - **[C for the formula; I for the field meanings]**
2. Return all leaving chars (list +0x54) to the pool.
3. If the region changed:
   - Splice each block's previously removed instances (+0x18) back into its multimesh.
   - Then **remove the new region's instances from their multimeshes** (into +0x18). The 3D members *replace* those flat sprites.
   - Assign the active 3D chars (list +0x4c) in order to region instances. The char transform is **the instance rotation rows** (identity, facing +Y toward the stage) with **position (inst.x, inst.y, region.floorZ)**: feet on the floor, not at the sprite centre.
   - Finally call `FUN_00172ec8(full, flat)` with the stored sizes.

### `set_sizes(full, flat)` (`FUN_00172ec8`, handler `FUN_00173df0`; stores +0x80 = full, +0x84 = flat)

- **flat:** for each block, visible = (int)((count − removedByRegion)·flat). The **first** `visible` instances in list order stay. The rest are spliced into the block's hidden list (+0x10) or restored from it. This is deterministic, not random.
- **full:** target = (int)(regionInstanceCount·full). While there are more active 3D chars than the target, the last one is removed:
  - if `DAT_00363780` is set, it goes straight back to the pool;
  - otherwise it moves to the leaving list +0x54, and if crowd_walk_flags is set (never) `FUN_00174ca8` turns it along `crowd_walk_offset` and plays a walk clip (flag 0x101).
  - A leaving char with fade +0x34 == 0 is returned to the pool by `FUN_001720e8`. **[I: what drives +0x34]**
- While there are fewer: take a char from the pool front (or steal one from the leaving list if the pool is empty) and place it on region instance [count−1] as above.
- Region spots beyond `full·N` are **empty**: they were removed from the flat blocks and have no 3D char.
- Script (`arena/crowd.dta`): `update_crowd` → `set_crowd_sizes(x, x)` with x = 0.1 / 0.25 / 0.5 / 1 / 1 per excitement. If the venue's `reduced_crowd_shots` lists the current `cam_msg` with a value < flat, flat becomes that value (e.g. theatre? no; venue entries at lines 99 and 174 of `venues.dta` use 0.8–0.9). `$no_crowd` gives 0 0. `init` gives 1 1.

### Pool / instances (`crowd instance <arch> (head h mat m)`, `FUN_00173cc8` → `FUN_00174380`/`CrowdChar_v3`)

- Each call creates a CrowdChar named `crowd%d` from archetype `<arch>`. It keeps head mesh h, removes the others, swaps outfit mat m, and is pushed to the pool front (`FUN_00172d88`).
- Default branch: 10 chars, +2 unless the venue is fest, +5 unless the venue is arena, big_club or theatre. **Theatre = 12, basement = 17.**
- The first 6 created double as the block archetypes (flag +0xb0) and are taken from the pool last.

### Animation and poll (`FUN_001720e8`, ArenaPanel poll)

- If the crowd is shown, every active, leaving and archetype char runs `FUN_00174ad0`: vtable +0x54 with the main camera (LOD **[I]**), then `FUN_001817c0(beat = ticks/480, Δbeat, Δms·0.001)`.
- `animate` (`FUN_00173e70`) shuffles all chars (active + pool) and splits them across clip groups by the given weights. `FUN_00173508` picks a clip from the group and adds a random phase.
- `set_frame_scale` (`FUN_001736c8`) sets +0x98 on every char (crowd_half/double/normal_tempo).
- Drawing: `CrowdChar_v6` draws only if +0x34 ≠ 0.

## 3. Theatre "floor above crowd": other candidates checked

- **Crowd height offsets:** none. There is no venue `crowd_flat_height`, so the z shift is 0. Quads are centred at the instance, whose z = floor + ~50, so they span the floor up to about the floor + 101.
- **`crowd.rnd_ps2`:** `crowd.view` has an empty draw list. The `crowd_limitsNN.mesh` patches (grey `crowd_limits00.mat`, showing = true) and the standalone `Crowd_A36/B42/C41/D43/E51.mesh` quads are never drawn in the game (limits are removed from draw lists; the Crowd draws the multimeshes). The engine draws only `<section>.view`, so it is fine as long as it never draws all showing meshes.
- **`crowd_plane.mesh`:** only drawn with `crowd_shadows`, which is unset.
- **Draw order:** the crowd comes last with depth test, so it cannot be painted over.
- **The actual cause:** the stale-world transform proxies described in the summary (`stage.1/.3.mesh` at z 74.6 and `drum_riser.1/.2.mesh` at z 25.4, both hanging over the audience at x ±450, y −511..556).

## Key addresses

| Address | Role |
|---|---|
| 0x170160 | quad builder |
| 0x170650 | Crowd constructor and config |
| 0x170da8 | region build |
| 0x171558 | crowd build |
| 0x1720e8 | poll |
| 0x1722a0 | fixed impostor view |
| 0x172350 | draw |
| 0x1727b0 | switch_region |
| 0x172ec8 | set_sizes |
| 0x173778 | Crowd handlers |
| 0x173cc8 | instance |
| 0x173e70 | animate |
| 0x174ca8 | walk away |
| 0x1a34a0 | PsMultiMesh_v5 (FastBillboardXYZ branch) |
| 0x1da848 | billboard constraint for PsMesh |
| 0x1db750 | constraint names |
| 0x1b1df8 / 0x1b1eb8 | camera fov/near/far and projection |
| 0x2eadc8 | Transformable load, `parent` = transform owner at +0x10 |
| 0x1da570 | UpdateWorldXfm |
| 0x2ec320 | PsMesh draw, uses owner's world |
| 0x1696d8 | arena draw order |
| 0x16f3b8 | per-shot hide_crowd / crowd_region / force_cam_facing |
