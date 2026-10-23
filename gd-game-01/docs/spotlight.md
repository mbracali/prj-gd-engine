# VenueSpotLight: the follow spot on the guitarist

This file is the spec for the venue spotlight that follows the guitarist. The spotlight has four parts:

- `spotlight01.lit`, which is re-aimed every frame;
- its beam (cone) mesh, a child of the light;
- a floor glow ellipse under the guitarist's feet;
- the light's effect on the characters: it is a light in the solo environ, and it also sets the shadow direction.

Tags: [C] = read from code or data. [I] = inference.

Related: `camera_walk_crowd_lighting.md` §4.5 (summary; this file replaces it), `lighting.md` (light direction = +Y row), §1 there (BillboardZ), §2 (draw order).

---

## 1. Class and data format

### 1.1 Nothing is serialized [C]

`VenueSpotLight` (RTTI strings `14VenueSpotLight` 0x310c58, `P14…`, `PP14…`) is a **plain runtime C++ struct of 0xC bytes**. It is not an `Object`: it has no Load, no factory and no file record.

| off | field | set in |
|---|---|---|
| +0 | `RndMesh* floor`: a new Mesh, a copy of `floorspot_char.mesh` | ctor |
| +4 | `RndTransformable* light`: the `spot_lights` entry, found with `RndTransformable_typeinfo` 0x2c5320. In the data this is always `arena::spotlight01.lit`. | ctor |
| +8 | `Character* target`, stored as the `char+0xB0` sub-object (vtable 0), or 0 | ctor |

| function | address |
|---|---|
| ctor | `FUN_00179190` |
| dtor | `FUN_001793b8` (deletes the floor mesh) |
| poll | `FUN_00179420` |
| draw | `FUN_00179690` |
| show | `FUN_001796b0` |
| config read | `FUN_00179108` |

The Arena keeps a `vector<VenueSpotLight*>` at `arena+0xC4..0xCC`. The arena global is `DAT_00363748`.

### 1.2 Config [C]

`FUN_00179108` runs once from the Arena ctor `FUN_00167fe0`. It reads the config `arena` block:

| key | where | value | global |
|---|---|---|---|
| `spot_lights` | `config/gen/gh.dta` (arena) | `"arena::spotlight01.lit"` (one entry) | – |
| `floor_spot_height` | `system/run/config/arena.dta` = 1.5; `gh.dta` has a second `(arena ... (floor_spot_height 1))` | **1** if the `...` block overrides, otherwise 1.5 [I] | `DAT_00363b68` |
| `floor_spot_scale` | `system/run/config/arena.dta` | 100.0 | `DAT_00363b6c` |

Notes:
- The `spot_lights` in `system/run/config/arena.dta` is empty, but it is pulled in with `#merge`, which only adds keys that are missing, so gh.dta's entry is the one used.
- A venue can turn the feature off with `(no_spotlights …)` in its venue data (`arena+0x6C`), which `FUN_00168808` checks. **No venue sets it.**
- No script calls `{arena show_floorspot i bool}` (handler `FUN_0016bd10` → `FUN_001796b0`). That handler only works when a target character exists.

### 1.3 Files and objects [C]

**Floor mesh template.** `floorspot_char.mesh` lives only in `system/run/arena/gen/fx.rnd_ps2` (extracted at `extracted/_up_/_up_/system/run/arena/gen/fx.rnd_ps2`):
- The Arena ctor loads it into the arena dir as `../../system/run/arena/fx.rnd`.
- It is a quad with 4 vertices: x ∈ [−0.5, 0.5], **y ∈ [−0.6, 0.4]**, z = 0, normals +Z.
- UVs run u 0.140..0.839, v 0.154..0.853, framing the circle in the texture.
- Its own material is fx's `floorspot_glow.mat`: `glodrum.tex`, colour (0.4, 0.3, 0.2), Xfm ×1.6. This is only a fallback.

**Per-venue objects** (`extracted/venues/<v>/gen/lighting.rnd_ps2`):

| venue | `spotlight01.lit` (Directional, file colour) | beam child of the light | `spotlight01.tnm` | light colour anim | in env |
|---|---|---|---|---|---|
| big_club | (2, 0.51, 0.44), pos (22.7, 42.1, 311.8) | `light_solo_opt.mesh` (`spot_BEAM.mat`) | 62 keys, x 115 → −95, z≈312 | `spotlight.litanim` in `full.anim` | `singer1.env` |
| theatre | (4, 2, 0.376) | `solo_obj.view` → `light_solo_opt.mesh` (`spot_BEAM.mat`) | x 150 → −120, z 258.6 | `spotlight.litanim` in `solo.anim` | `singer1.env` |
| fest | (1, 1, 1), pos (160, −97, 324) | `light_solo_opt.mesh` (`light_beam.mat`) | x 160 → −160, z 324 (266 at 1920) | `spotlight01.litanim` targets **no light** (`light ''`), so it does nothing | `singer1.env`, `lighting.env` |
| arena | (5, 5, 0.53) | `solo_beam01.mesh` (`light_spotlight.mat`) | f0 (155, 11.9, −42), f960 (−200, 11.9), f1920 (−1.7, −300) | `spotlight01.litanim` in `solo.anim` | `singer1.env`, `lighting.env` |
| small_club | (2, 0.81, 0.22), pos (34, 50.5, 150.7), static | none | none | `spotlight.litanim` in `solo.anim` | `singer1.env` |
| basement | (0, 0, 0): no effect | none | none | none | `singer2.env` |
| small_club_multi | (1, 1, 1); cone/glow/cannon children | yes | none | none | none |

Every venue except small_club_multi has its own `floorspot_glow.mat` and `spotlight.matanim` (see §4.2).

**Beam meshes [C].** All have constraint **5 (BillboardZ)**, blend SrcAlphaAdd, zmode Transparent, `spot_beam.tex`, `use_environ = 1` and `vert_amb = 1`. Their colour therefore comes from the vertex colours × the ambient of the env drawn before them in `solo.view` / `sololight.view`: `solo.env` / `sololight01.env`, driven by `solo.envanim` (see §4.3).

In mesh-local space the beam runs along **−Z from ≈0 to −316** (big_club/theatre; fest −309, arena −377). It is offset −57…−104 on local y.

---

## 2. Setup

### 2.1 `FUN_00168808(arena)` [C]

Called from the Arena poll `FUN_001691a0` loading state, once the sections, crowd and characters are ready. It runs after walk spots (`FUN_001682f0`) and before `FUN_00168668` / `FUN_00168ed8`.

```
if venue data has no `no_spotlights`:
    list = config(arena, spot_lights)
    for i in 1 .. list.size-1:                       // list[0] is the key
        chr = (i-1 < arena.chars.size) ? arena.chars[i-1] : 0
        arena.spots.push(new VenueSpotLight(list[i].str, chr))
delete "arena::floorspot_char.mesh", "arena::floorspot_char01.mesh"   // FUN_00167e28
```

`arena.chars` is `arena+0x3C`, which is `DAT_00363784`. This is the camera's focus list: every CharSys character of class `guitarist`, stored as `char+0xB0` (filled in `ArenaPanel_v13`). So **the spotlight is paired with guitarist0**. In two-player games guitarist 1 gets no spotlight, because there is only one entry [I].

### 2.2 Ctor `FUN_00179190(this, name, chr)` [C]

```
light  = Find<RndTransformable>(name, fatal)
floor  = new RndMesh;  floor->Copy(Find("floorspot_char.mesh", fatal))     // vtbl+0x44 [I: Copy]
mat    = Find<RndMat>("arena::floorspot_glow.mat", nonfatal)
if mat: floor->SetMat(mat)                                                  // FUN_001c45e8
light->SetTransConstraint(1 = LocalRotate, target 0, preserve_scale 0)      // FUN_001da750
if chr: chr->vtbl0[6](light)        // Character_v6 @0x18d500 → FUN_00196200(chr+0xE4, light)
else:   floor->SetShowing(false)
```

Notes:
- **LocalRotate (1)** (`FUN_001da1a8`, the constraint evaluator): world rotation = local rotation, ignoring the parent. World position is computed as usual. `spotlight01.lit` has no parent, so world = local.
- **Material choice.** If both fx.rnd and the venue define `floorspot_glow.mat` in the arena dir, the venue's is the one animated by `spotlight.matanim`, so treat that as the one used [I]. Use fx's only when the venue has none (small_club_multi).

### 2.3 Shadow hookup `FUN_00196200(shadow = chr+0xE4, light)` [C]

The character object at `+0xE4` is the drop-shadow object. `Character_v6` only calls this when that object exists.

```
for each object in the shadow's dir that is a RndTransformable:
    if strncasecmp(name, "bone_", 4) == 0:
        obj->SetTransConstraint(3 = ShadowTarget, target = light, 0)
shadow->drawable.SetShowing(true)          // (*shadow)+0x30, vtbl+0x24 [I]
```

**ShadowTarget** (0x1da3e8):
- It moves the bone's world into the target's (light's) frame.
- It projects it along the light's +Y onto the shadow plane `DAT_00368e40` (charsys `shadow_plane_height`).
- It then moves it back.

So **the guitarist's drop shadow falls away from the spotlight** [C for the mechanism; I for the exact plane source]. The engine has no character shadows yet (see §6, optional).

---

## 3. Per-frame algorithm `FUN_00179420(this)` [C]

Called from the Arena poll `FUN_001691a0` for every spot, only while the arena is showing (`arena+0x140`). It runs after the sections' anims (`FUN_00178ce8`, which apply `spotlight01.tnm` / the litanims / the matanims) and the camera. The guitarist's root pose may be one frame old [I].

```
c = this.target ?: (focusList.empty ? return : focusList[0])
t = c->vtbl0[1]()                 // Character_v1 → FUN_0018e238: the character ROOT xfm (feet, root motion)
    .pos                          // NOT a bone; walking moves it
L = light.world.pos               // light xfm owner +0x90
d = t - L

// 1. aim the light (FUN_0024adf8 = "look along Y, Z up")
Y = normalize(d)
X = normalize(Y × (0,0,1))
Z = X × Y
light.local.rot = [X; Y; Z]       // FUN_001da700; LocalRotate ⇒ world rot = this
                                  // the light now travels along +Y, straight at the feet

// 2. floor ellipse
len = |d|            (3-D, before flattening)
dz  = d.z
Yh  = normalize(d.x, d.y, 0);  Xh = normalize(Yh × (0,0,1));  Zh = (0,0,1)
S   = floor_spot_scale (100)
Ls  = S * len / |dz|             // stretch away from the light: 1/cos(angle from vertical)
floor.local.rot = [Xh*S; Yh*Ls; Zh*1]
floor.local.pos = (t.x + 0.25*Ls*Yh.x,  t.y + 0.25*Ls*Yh.y,  t.z + floor_spot_height)
floor->UpdateWorld()             // xfm vtbl+0x14; the floor mesh has no parent ⇒ world = local
```

Resulting footprint:
- The quad's local y range is [−0.6, 0.4] and its centre is pushed 0.25·Ls away from the light. So the quad spans **t − 0.35·Ls … t + 0.65·Ls** along the light's horizontal direction, and ±S/2 = ±50 across it.
- The circle centre is at **t + 0.15·Ls**: slightly past the feet, away from the light.

Worked example, big_club solo on walk_spot_01:
- L = (115, 43.5, 312.1), t = (103.6, 43.5, 0.4).
- d = (−11.4, 0, −311.7), |d| = 311.9, Ls ≈ 100.1.
- The result is a near-circle of diameter 100 centred 15 units toward −X of the feet.
- The beam is 316 long, which ≈ |d|: **the beam is sized so it reaches the floor at the walk spots**. The length is never scaled at runtime.

**The beam follows automatically [C].** The beam mesh is a transform child of the light (or of `solo_obj.view`, which is a child of the light), so its world = local × light world. Its world Z row is −Y_light, the beam axis. At draw time BillboardZ (`FUN_001da848`) keeps that Z axis and spins the card about it to face the camera: `Y = pos − cam`, `X = normalize(Y × Z)`, `Y = Z × X`.

---

## 4. Draw rules

### 4.1 Order [C]

`ArenaPanel_v11`:
1. Section views and crowd.
2. Characters (`FUN_002826d0`).
3. `FUN_00169818`. It runs only if the arena is showing:
   1. **floor spots**: for each VenueSpotLight, `FUN_00179690` → `RndDrawable::Draw(floor)` (`FUN_002ec850`: showing flag → sphere cull → DrawShowing);
   2. the sections' `*_transparent.view`s;
   3. fire, name lights and camera fade.

The floor glow is not in any View. It is drawn on its own, after the band and before the transparent views.

The beam is drawn wherever its View is: `solo.view` / `sololight.view`, inside the lighting section's normal view tree (big_club also lists it under `lighting.view`).

### 4.2 Floor glow material [C]

**All venues** share these settings:
- blend 2 "Add", both on the material and on the stage;
- zmode Transparent: depth test on, **no z write**;
- `use_environ = 0`, `vert_amb = 0`, MultipassLit;
- Lit path with ambient = 1 and no lights ⇒ vertex colour = min(mat.color, 1);
- Stage-0 blend 2 with mat blend 2 and `!vert_amb` ⇒ GS TFX HIGHLIGHT (lighting.md): `rgb = tex·col + α_col` with α = 0.035, a negligible bias;
- Texture Clamp, texgen Xfm (identity in the venues), mesh UVs;
- `cull False` (the fx fallback has `cull True`).

Effective result: **dst += tex.rgb × matColor.rgb** (additive, ignores alpha).

Colour and texture come from `spotlight.matanim` (MatAnim on `floorspot_glow.mat`). Texture keys step; colour is linear. Key alpha is garbage (≈1e-42) and unused.

| state (frame range) | big_club | theatre | fest | small_club | arena | tex |
|---|---|---|---|---|---|---|
| great verse/chorus 0–13840, okay verse/chorus 30000–45000 | 0.34, 0.20, 0.13 | same | 0.50, 0.31, 0.14 | 0.34, 0.20, 0.13 | 0.50, 0.20, 0.20 | `spot_circle_alt.tex` |
| **great solo 20000–23840** | 1.00, 0.51, 0.44 | 0.98, 0.80, 0.48 | 0.80, 0.80, 0.10 | 0.49, 0.42, 0.17 | 0.68, 0.67, 0.35 | `spot_circle.tex` |
| **okay solo 50000–55000** | 0.44, 1.00, 0.70 | 0.17, 0.83, 0.74 | 0.11, 0.33, 0.91 | 0.23, 0.50, 0.51 | 0.27, 0.56, 0.54 | `spot_circle.tex` |
| bad 60000 | 0.34, 0.20, 0.13 | – | – | – | 0.07, 0.15, 0.33 | `spot_circle_alt.tex` |

basement has only one colour key (0.34, 0.2, 0.13).

How the matanim is driven:
- big_club, theatre, fest, basement: through `full.anim`.
- small_club: through `lighting.view`.
- arena: `set_lights_*` switches `spotlight.matanim` directly with the same loop ranges.

**The glow is always visible** (a dim warm circle) whenever guitarist0 exists, and brightens to the solo colour with the sharper `spot_circle.tex` during solos.

### 4.3 Beam [C]

- Blend SrcAlphaAdd, Transparent, BillboardZ.
- Colour = vertex colour × `solo.env` ambient (`use_environ` + `vert_amb`).
- `solo.envanim` keys:

| venue | great solo (20000) | okay solo (50000) | off (99999, via `switch_anim solo.anim OFF`) |
|---|---|---|---|
| big_club | (0.91, 0.65, 0.51) | (0.22, 0.83, 0.58) | 0 |
| arena | (0.99, 0.98, 0.53) | (0.74, 0.97, 0.95) | 0 (also 0 at 60000) |
| theatre `sololight01` | (0.98, 0.80, 0.48) | (0.17, 0.83, 0.74) | 0 |

So the beam is visible only during solos. Scripts also hide it per shot:

| script | rule |
|---|---|
| big_club `post_switch_cam` | `light_solo_opt.mesh set_showing {!= {arena cam_msg} "SOLO_NEAR01"}` |
| arena `post_switch_cam` | `solo_beam01.mesh set_showing {&& {!= cam_msg …}…}` (line ~1265) |
| fest `post_switch_cam` | same, line ~619 |
| theatre `intro_start` | hides it until the first solo |

---

## 5. Triggers

| what | how | where |
|---|---|---|
| light aim, floor xfm | every frame (§3) | code |
| light **position** | `spotlight01.tnm` (TransAnim → `spotlight01.lit`) is removed from `lighting.view` by `add_handlers {spotlight01.tnm unhook_anim_parents}`, then set by `set_lights_okay_solo` / `set_lights_great_solo` (see the table below this one) | venue .dta |
| light colour | the LightAnim keys at 20000 / 50000 inside `solo.anim` / `full.anim` (the `set_lights_*` loop ranges) | lighting.rnd |
| lights the guitarist | `{arena set_singer_env singer1.env}` in the solo states selects the env that contains `spotlight01.lit` (basement: `singer2.env`, colour 0). Other states use `singer0`/`singer2` without it. | venue .dta |
| floor glow colour | `spotlight.matanim` per state (§4.2) | |
| beam on/off | `solo.envanim` (solo.anim OFF ⇒ black), plus `post_switch_cam` `set_showing` | |
| star power, excitement | no direct hook; only through the lighting state (`set_lights_great_*` vs `okay_*` vs `bad`) | |

Light position per walk spot (`{switch {char_sys get_spot guitarist0} …}`):

| venue | spot 0 | spot 1 | spot 2 |
|---|---|---|---|
| big_club, theatre, fest | `set_frame 0` | `set_frame 3840` | – |
| arena | `set_frame 0` | `set_frame 1920` | `set_frame 960` |

- `get_spot` = `FUN_0018ef80`, the nearest walk spot (0-based, hub included). The hub has no case, so the light stays where it was.
- The position only changes when a solo lighting state is entered. Until the first solo the light stays at its file position (big_club (22.7, 42.1, 311.8): mid-stage, high). It is still aimed at the guitarist every frame, so the floor glow under him is stretched accordingly.
- Arena and fest `lighting.env` contain only `spotlight01.lit`, so venue geometry drawn with that env is lit by the aimed spotlight too [C data; which meshes use it is not checked].

---

## 6. Implementation plan for gd01

Files: `engine/src/render/venue_renderer.{h,cpp}`, `engine/src/game/venue_script.cpp`, `engine/src/scene/anim.{h,cpp}`, `engine/src/render/scene_renderer.{h,cpp}`.

**1. Config (load).**
- Read the `arena` block from `config/gen/gh.dtb`: `spot_lights`, `floor_spot_height`, `floor_spot_scale`.
- Fall back to `../../system/run/config/gen/arena.dtb` (1.5 / 100), the same pattern as `crowd_renderer.cpp:53`.
- Use height **1.0** (gh.dta `...` override). This needs checking against the DTA merge rules; 1.5 is the fallback.
- Skip everything if the venue entry in `venues.dta` has `no_spotlights`.

**2. Load fx.**
- Load `../../system/run/arena/gen/fx.rnd_ps2` into a `Scene` (it is also needed for `shaky_cam*.tnm`).
- Keep `floorspot_char.mesh` geometry (4 verts) as the template.
- Material: `anims_->find("floorspot_glow.mat")`, i.e. the venue lighting section's object, which `spotlight.matanim` already animates through AnimPlayer. Fall back to fx's copy if the venue has none.

**3. State.**
```cpp
struct SpotLight { SceneObject* light; const SceneObject* floorTemplate; const SceneObject* mat; Xfm floorWorld; bool show; };
```
- Create one per `spot_lights` entry (strip `arena::`), paired with `guitarist_` (index 0).
- With no guitarist: `show = false`.
- At creation, set `light->trans.constraint = 1`. Nothing else is needed for LocalRotate, because the light has no parent.

**4. Per frame**, in `VenueRenderer::update`:
- **Where:** after `script_->update` (which applies `spotlight01.tnm` frames and litanims) and after `guitarist_->poll`, so the root is current. It must come before the camera code's `updateAttached` and before any draw.
- **Target:** `t = guitarist_->placement().pos`. This is the root, including walk root motion; FUN_0018e238 is the same root xfm.
- **Light:** use `L = light->transformable()->world.pos` and apply §3 step 1. Write the rows into **both** `local.rot` and `world.rot`, because the character lighting reads `world` (+Y row = light direction, `scene_renderer.cpp` `lightingFor`).
- **Children:** recompute the light's children (the beam, `solo_obj.view`). Add `AnimPlayer::markMoved(const std::string&)`, which sets `dirty_[nodeIndex_[name]] = 1`, then call `anims_->updateTransforms()`. Alternatively, compose each child directly: `child.world = compose(child.local, light.world)`.
- **Floor:** compute `floorWorld` per §3 step 2. Watch for `|dz| < ε`: skip, or clamp.

**5. Draw**, in `VenueRenderer::draw`, after `band_` and before the `_transparent.view` loop:
- Draw the floor quad with `floorWorld` and the venue's `floorspot_glow.mat`.
- Add a small `SceneRenderer::drawMesh(const SceneObject& mesh, const Xfm& world, const MatObj* matOverride, const Mat4& vp)`. It can reuse the mesh path with an explicit world and material instead of `world(*dm)`.
- State:
  - Blend::Add (ONE, ONE);
  - depth test on, depth write off;
  - no culling;
  - colour = tex × min(mat.color, 1), which is the existing lit path with `use_environ = 0`: ambient 1 and no lights;
  - fog per the current env, as the other meshes do;
  - ignore the 0.035 HIGHLIGHT bias.

**6. Script support** (`venue_script.cpp`):
- Run `{switch <expr> (value cmds…)…}` where `<expr>` is `{char_sys get_spot guitarist0}`. Evaluate it via a callback that returns `VenueRenderer::walkSpotNow()` (nearest spot, hub included). Without this the light never moves to the solo spot, which is the main visible difference.
- (Optional; this also fixes other per-shot hides) Evaluate computed `set_showing` with `{!= {arena cam_msg} "NAME"}` and `{&& …}`, so the beam is hidden on SOLO_NEAR01 and similar shots. Today `runCommand` drops computed values.

**7. Already in place; check only:**
- the light is in `singer1.env` and the guitarist is drawn with `script_->singerEnv()`;
- BillboardZ for the beam (`scene_renderer.cpp:298/453`);
- `solo.envanim`;
- the MatAnim texture step for `spot_circle*.tex`.

**8. Optional, later.** Character drop shadow: planar projection of the guitarist along the spotlight's +Y onto `z = shadow_plane_height` (charsys = 2), using the shadow rnd's `bone_*` transforms with ShadowTarget (§2.3).

**Test.** Take `--selftest` screenshots of big_club and arena in a great solo, from a wide shot. Check:
- the beam starts at the lamp and ends at the guitarist's feet;
- the glow ellipse is centred just past the feet, away from the light;
- after a walk to the other spot, the next solo moves the lamp (frame 3840 / 1920);
- outside solos only a dim `spot_circle_alt` glow remains.

---

## 7. Address index

| addr | what |
|---|---|
| 0x167fe0 | Arena ctor: loads fx.rnd, calls FUN_00179108 |
| 0x168808 | create the spotlights (`no_spotlights`, `spot_lights`); delete the templates |
| 0x1691a0 | Arena poll: calls FUN_00168808 (load), FUN_00179420 (per frame) |
| 0x169818 | post-character draw: floor spots, then transparent views |
| 0x16bd10 | `show_floorspot` handler |
| 0x179108 | read `floor_spot_height` / `floor_spot_scale` → 0x363b68 / 0x363b6c |
| 0x179190 | VenueSpotLight ctor |
| 0x1793b8 | dtor |
| 0x179420 | per-frame aim + floor xfm |
| 0x179690 | draw floor (→ RndDrawable::Draw 0x2ec850) |
| 0x1796b0 | SetShowing(floor) if there is a target |
| 0x18d500 | Character_v6: set the shadow light → 0x196200 |
| 0x196200 | shadow `bone_*` → ShadowTarget(light) |
| 0x18e238 | character root xfm (the spotlight target) |
| 0x24adf8 | build rotation: Y = dir, X = Y×up, Z = X×Y |
| 0x1da700 / 0x1da730 / 0x1da750 | SetLocalRot / SetLocalPos / SetTransConstraint(type, target, preserve_scale) |
| 0x1da1a8 | constraint eval: 2 LookAt, 3 ShadowTarget (0x1da3e8), 1 LocalRotate |
| 0x1db750 | constraint names: None, LocalRotate, LookAtTarget, ShadowTarget, MirrorTarget, BillboardZ, BillboardXZ, BillboardXYZ, FastBillboardXYZ |
| 0x18ef80 | `get_spot`: nearest walk spot |
