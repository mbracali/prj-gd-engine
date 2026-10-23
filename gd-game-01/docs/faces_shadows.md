# Faces, eyes and character shadows

This doc covers three runtime systems for characters: face morphs (expressions and singer lip-sync), eye look-at, and the guitarist's projected shadow. It gives file formats, the per-frame algorithms with their ELF addresses, and an implementation plan for `engine/`.

The conventions follow `docs/character_animation.md`:
- Matrices are row-vector. Row *i* of a 3x3 is the image of axis *i*.
- A Transformable's xfm block (`*(obj+0x10)`) holds the local rows at +0x20/+0x30/+0x40, the local pos at +0x50, the world rows at +0x60/+0x70/+0x80, the world pos at +0x90, the dirty flag at +0xa0, the constraint at +0xa4, preserve_scale at +0xa8 and the target at +0xac.
- `S(x) = 0.5 − 0.5·cos(πx)`.
- `FUN_0024cb70` = sinf.

Unmarked statements were read from the decompile or the data. **[I]** marks an inference.

The classes involved have no RTTI names: there is no CharEyes, CharLipSync or shadow class in `vtables.tsv`. They are small helper objects that hang off `Character` and `GameCharacter`:

| Object | Owner field | Size | Created by |
|---|---|---|---|
| Eyes | `Character+0xe8` | 0x110 | `create_eyes` → `FUN_0018fd58` |
| Walk | `Character+0xdc` | 0x70 | `create_walk` → `FUN_001967f0` (see camera_walk doc) |
| Shadow | `Character+0xe4` | 0x24 | `create_shadow` → `FUN_00195bd8` |
| Face | `GameCharacter+0xf0` | 0x30 | `GameCharacter_v3` (0x283998) if the char has `face_data` → `FUN_00287378` |
| Face blender | `Face+0x28` | 0x14 | `FUN_00287d88` |

The script handler for these is `Character_v1` @ 0x18d9f0. Its symbols are `create_eyes` 0x316ce8, `create_walk` 0x316cf8 and `create_shadow` 0x316d08.

There is **no lip-sync data file** (.lipsync/.voc/.anim) anywhere. Singer lip-sync comes only from MIDI note 108 in the `T1 GEMS` track (§A.4).

---

## A. Faces

### A.1 Data

**charsys.dta** (`extracted_dta/charsys/gen/charsys.dta`):

```
#define HERO_FACES ((face_data
   (poses ref bad01 bad02 bad03 good01 good02 good03 good04 good05 blink)
   (excitement_poses
      (kExcitementBoot  bad01 bad02 bad03)        ; kExcitement* = 0..4 (macros.dta)
      (kExcitementBad   bad01 bad02 bad03)
      (kExcitementOkay  ref good01)
      (kExcitementGreat good01 good02 good03 good04 good05)
      (kExcitementPeak  good01 good02 good03 good04 good05))
   (blend_time 0.5 (step 0.1))
   (pose_length 1.0 (step 0.1))))
#define SINGER_FACES ((face_data
   (poses ref open)
   (event_list singer)
   (event_offset 257)
   (blend_time 0.1 (step 0.1))))
(singer_events (108 open))
(face_events (113 good01) (114 good02))
```

- Each hero archetype adds `HERO_FACES (face_file "charsys/<c>/<c>_face.rnd")`.
- The singers in `band_chars.dta` add `SINGER_FACES (face_file "charsys/metal_singer/metal_face.rnd")` or `charsys/female_singer/face_female_singer.rnd`.
- Bass, drummer and keyboard have no `face_data`.

**Face files** (`extracted/charsys/<c>/gen/*_face.rnd_ps2`), for example metal:

| Object | Content |
|---|---|
| `ref.mesh`, `bad01..03.mesh`, `good01..05.mesh`, `blink.mesh` | Full-face pose meshes, 686–691 verts, 1160 faces. They are not drawn. |
| `ref.2.mesh` … `blink.2.mesh` | Eyelash pose meshes, 42 verts |
| `face.mrf` | Morph v3. Pose *k* (in `poses` order) has weight keys `(1 @ frame k, 0 @ every other integer frame)`. `target` is empty, intensity is 1. |
| `lashes.mrf` | Same structure, over the `.2` meshes |

The singer face files hold `ref.1.mesh`, `open.1.mesh` (242 verts for metal, 249 for female) and `face.mrf` (frame 0 = ref, frame 1 = open).

Morph frame *k* therefore equals pose index *k* in `face_data.poses` (ref = 0, bad01 = 1, …, good05 = 8, blink = 9; singer: ref = 0, open = 1).

**Targets in the character file:**
- Hero: `face.mesh` (686 verts, `head.mat`, mutable 31) and `lashes.mesh` (42 verts, `hair.mat`). Both are rigid children of `bone_head.mesh`.
- Singer: `face.mesh` (242 verts, `head-singer.mat`).
- Some pose meshes have more verts than the target (bad01 has 689, good02 has 691). The morph only touches `min(target, pose)` verts, so the extra verts are ignored.

**Morph record** (`RndMorph` Load, decoded by `tools/rnd_classes/geometry.py::decode_morph`):
```
u32 version (3), Animatable, u32 nPoses, per pose { ref mesh, u32 nKeys, nKeys × {f32 weight, f32 frame} },
ref target, bool8 normals (+0x40), bool8 spline (+0x44), f32 intensity (+0x48)
```
In memory: the pose vector sits at +0x24/+0x28 with 0x18-byte elements, and the target at +0x38.

### A.2 Binding: `FUN_00287608` (GameCharacter_v12, post-load)

1. The face file is loaded with RndLoader (`FUN_00287548`, `face_file` 0x342010) into the character's directory.
2. For every `RndMorph` in that loaded directory:
   - The first Morph becomes `Face+8`, the "master". Each later Morph is appended to the master's child-anim list (+0x10), so `SetFrame` on the master recurses into it (`FUN_001abab8`). `lashes.mrf` therefore follows `face.mrf`.
   - `morph+0x40 normals = 1` and `morph+0x44 spline = 0` are forced.
   - Target = `"<morph name without extension>.mesh"` (format `%s.mesh` 0x342020) looked up in the character's dir: `face.mrf → face.mesh`, `lashes.mrf → lashes.mesh`.
   - The mesh must have mutable verts (`mesh+0x134 & 0x1f`). Otherwise the game logs "In %s, mesh %s must have mutable verts" and leaves the target unset.
3. The blender is created (`FUN_00287d88`): `{+0 master morph, +4 cur = 0, +8 tgt = 0, +0xc t = 1.0, +0x10 rate}`.
4. Parameters are read (`FUN_00287490`): `rate = 1/blend_time`, `Face+0x14 = pose_length`, `Face+0x20 = event_offset`.

Face object fields:

| Offset | Field |
|---|---|
| +0 | `face_data` array |
| +4 | loader |
| +8 | master morph |
| +0xc | `excitement_poses` (0 if absent) |
| +0x10 | pose timer (s) |
| +0x14 | `pose_length` (default 1.0) |
| +0x18 | event list (0 if absent) |
| +0x1c | current event |
| +0x20 | `event_offset` (ms) |
| +0x24 | "event open" flag |
| +0x28 | blender |
| +0x2c | character |

**Reset/enter** (`FUN_002878c0`, GameCharacter_v5):
- Releases the global face mutex (`FUN_00287d40`) and zeroes the timer and event fields.
- Binds the event list:
  - `event_list singer` → GuitarAnimInfo `+0x18` (singer events).
  - `event_list face` (string 0x342090) → `+0x1c` (face events).
  - The GuitarAnimInfo object is `*(FUN_001062a8(songDB)+0x44)`.
- Resets the blender to `cur = tgt = 0, t = 1`.

### A.3 Hero expressions: excitement-driven (`FUN_00287b70`)

The per-poll entry is `FUN_002879d8`, called from GameCharacter_v13 for every poll, visible or not.

```
dt = char+0x8c (seconds)
if face.eventList:      singerPath(char+0x84 beat)              // §A.4
if face.excitementPoses:
    timer += dt
    if timer > pose_length and claimMutex(face):                // FUN_00287d50: one global owner DAT_002f5868
        list = excitement_poses[DAT_00363780]                   // global venue excitement 0..4 (Boot..Peak)
        i = randInt[1, len(list))                               // FUN_0024a1d0 = lo + rand % (hi-lo)
        p = poseIndex(list[i])                                  // FUN_00287c78: position in `poses` (0-based)
        if p == blender.cur: p = poseIndex(list[i+1 < len ? i+1 : 1])
        timer = 0
        blender.setTarget(p)                                    // FUN_00287dd8
blender.advance(dt)                                             // FUN_00287e38
if blender.t == 1: releaseMutex(face)                           // FUN_00287d70
```

- **One face changes at a time, game-wide.** The mutex `DAT_002f5868` is claimed when a new pose is picked and released when its blend completes. In two-player mode the two guitarists' faces therefore take turns.
- So a hero gets a new random expression of the current excitement level about every `pose_length` (1.0 s), each blended over `blend_time` (0.5 s).
- **Blink is never used.** It is in `poses`, but no `excitement_poses` list contains it and the ELF has no "blink" string anywhere. Heroes do not blink.
- **No win/lose faces.** Faces depend only on the excitement level. The `face_events` notes 113/114 would feed `+0x1c`, but no face has `(event_list face)` and no shipped chart has notes 113/114. They are dead data.
- The menu characters have no faces. CharsysPanel builds a plain `Character` (`FUN_0018d260`, 0xf0 bytes), not a `GameCharacter`, so `face.mesh` stays at its authored (ref) shape.

### A.4 Singer lip-sync (`FUN_00287a80`)

**Event source.** `GuitarAnimInfo` reads the song MIDI:
- `GuitarAnimInfo_v5` picks the track mode from the type-3 track-name event:
  - `"ANIM"` → mode 1.
  - `strcmp(name, "T1 GEMS") != 0` → mode 0 (ignored).
  - `"T1 GEMS"` → **mode 2**.
- `docs/guitarist_hands.md` §1.2A has the last two cases swapped. Mode 2 is `T1 GEMS`.
- In mode 2 (`FUN_00115d68`), notes > 107 are paired by `FUN_00115f38`: note-on 0x90 stores the tick, note-off **0x80** emits. Each note is looked up first in `face_events` (→ list `+0x1c`), then in `singer_events` (→ list `+0x18`).
- Emitted event: `{start = onTick/480, end = offTick/480, symbol}`, 16 bytes each (`FUN_00287028`).
- Note 108 (`open`) appears in every shipped chart's `T1 GEMS`: 236–288 notes per song, 30–12300 ticks long.

**Per poll:**
```
b  = char+0x84                                              // character beat (beat × frame_scale)
b' = b + event_offset / msPerBeat(tick = b·480)             // FUN_0010bcb8 → MultiTempoTempoMap_v3: µs/qn / 1000
ev = lastEventWithStart<=b'(list)                           // FUN_002872d8 / FUN_00287258 (cursor, rewinds on seek back)
if ev and ev != face.cur:
    face.cur = ev; face.open = 1
    blender.setTarget(poseIndex(ev.symbol))                 // "open" → 1
if face.cur and face.cur.end < b' and face.open:
    blender.setTarget(0); face.open = 0                     // back to ref
blender.advance(dt)
```

- The mouth opens **257 ms before** the MIDI note and closes 257 ms before its end. Each change blends over 0.1 s.
- **[I]** The beat is the frame-scaled character beat. `sing_half_tempo` / `sing_double_tempo` would therefore desync lip-sync, but none of the shipped charts use those events. Use the unscaled song beat.

### A.5 Blender (`FUN_00287dd8` / `FUN_00287e38` / `FUN_00287f38`)

```
setTarget(p):
    if t != 1 and p == cur:  swap(cur, tgt); t = 1 - t; return        // reverse a blend in flight
    if (t != 1 ? tgt : cur) != p:  tgt = p; t = 0                     // a new target while blending restarts from cur
advance(dt):
    if t != 1: t += rate·dt; if t > 1: t = 1; cur = tgt
apply()  (draw time: GameCharacter_v6, only if visible (+0x34 > 0) and LOD index (+0x1c) == 0):
    morph.SetFrame(cur, 1.0)
    if t != 1: morph.SetFrame(tgt, S(t))                              // 0.5 - 0.5·sin(tπ + π/2)
```

### A.6 `RndMorph::SetFrame(frame, blend)` (RndMorph_v1 @ 0x1fb3b0)

The target geometry is the target's geometry owner (`mesh+0x128`). Its verts are 0x40 bytes each: pos at +0, normal at +0x10.

```
if blend != 1: scale every target pos (and normal if normals) by (1 - blend)
remaining = 1
for pose i = last .. first:
    w = (i == first) ? remaining : intensity · keys_i(frame)       // FUN_001fb140, linear (spline forced off)
    if i != first: remaining -= w
    if w == 0: continue
    w *= blend
    for v < min(nTarget, nPose_i):
        target.pos[v] (+)= w · pose_i.pos[v]     // "=" for the first contribution when blend == 1
        target.nrm[v] (+)= w · pose_i.nrm[v]
if normals: renormalise every target normal
mesh.Sync(1)                                     // re-upload verts
```

Pose keys are one-hot at integer frames, so `SetFrame(k, 1)` writes pose *k* exactly. The blender produces `lerp(pose[cur], pose[tgt], S(t))` for positions and normals, with the normals renormalised. The pose meshes are in the target's local space, which is `bone_head` space for the hero.

---

## B. Eyes (look-at)

### B.1 Setup

`charbase.dta`, non-meta branch only:
```
{$this create_eyes (parent bone_head.mesh) (constraint 0.925) (lid_lower 0.5)}
```

- Only guitarists in a song get eyes. The band and the menu (`meta`) characters do not, and the singer models have no eye meshes.
- Hero files hold `L-eye.mesh` and `R-eye.mesh` (57 verts each, `eyes.mat`). Both are rigid children of `bone_head.mesh`.

Eyes object (0x110), from the ctor `FUN_0018fd58`, `FUN_0018fe18` (bind, also on Character_v11) and `FUN_0018ffc8` (Character_v12):

| Offset | Field |
|---|---|
| +0 | mode (0 off, 1 camera, 2 forward, 3 crowd); ctor 0 |
| +0x10/0x14/0x18 | random target offset |
| +0x20 | next re-target tick |
| +0x24 | character |
| +0x28 / +0x2c | `L-eye.mesh` / `R-eye.mesh` |
| +0x30 / +0x70 | saved original local xfm of L / R (restored when switched off) |
| +0xb0 | `parent` transform (bone_head) |
| +0xc0 | current look target (world) |
| +0xd0 | active flag |
| +0xd4 | `lid_lower` LinearInterpolator `(0, lid_lower, 0.075, 0.5)`. It is **never read**, so there is no lid motion. |
| +0xf0 / +0xf4 | `sin` / `cos` of the cone: `cos = constraint = 0.925` (22.3°) |
| +0xf8 | 1.0 |
| +0xfc | params array |
| +0x100 | row scales of the L-eye's world at bind (`FUN_0024a420`) |

Global config, `charsys` (system `config/charsys.dta`, merged into `charsys/charsys.dta`; read by `FUN_0018f8a0`):
```
(eye_camera_max_offset 12) (eye_camera_interval 1920 3840)
(eye_forward_distance 80)  (eye_forward_max_offset 12) (eye_forward_interval 960 2000)
(eye_crowd_interval 960 1920)
```
The intervals are in ticks: 480 per beat.

### B.2 Mode selection: from camera shots

- `VenueCam` shot parsing (`FUN_0016f3b8` region, key `eyes` 0x3112b8, **default 3**) calls the player's character vtable slot 5 = `Character_v5` (0x18d4d8) → `FUN_0018fd10(eyes, mode)`.
  - `mode != 0`: sets the mode, clears the offset and sets `next = −1` (re-target now).
  - `mode == 0`: `FUN_00190720` sets the eye constraints back to 0 and restores the saved local xfms. The eyes then follow the head again.
- The receiving player:
  - With two players, shot names starting `Left` → player 0, `Right` → player 1, any other name → nobody.
  - Single player: player 0.
- Data: only 10 shots set `(eyes 0)` (the close solo shots in small_club, theatre and basement). Every other shot is mode 3 (crowd). Modes 1 and 2 exist in code but no shipped shot uses them.

### B.3 Re-targeting (`FUN_0018fa18`, Character_v13, only when visible)

`now = char+0x84 · 480` (ticks). Re-targeting happens when `next <= now`. `rand()` is uniform [0,1) (`FUN_0024a108`). `randRange(a,b) = a + rand·(b−a)`.

| Mode | Offset | Target | Next interval |
|---|---|---|---|
| 1 camera | `(±rand·12, ±rand·12, 0)` | current camera world pos + offset (`*(DAT_00363824+8)` cam, xfm+0x90) | `randRange(1920, 3840)` |
| 2 forward | `(±rand·12, 0, ±rand·12)` | `(off.x, 80, off.z)` × character world matrix (char+0xb0 vfunc 0xc) | `randRange(960, 2000)` |
| 3 crowd | — | random member of the 3D crowd list (`crowd+0x4c`, `FUN_00172cc8`): its top.view world pos + (0, 0, 48), then `z = z + (charRootZ − z)·0.7` | `randRange(960, 1920)` |

Here `±rand·m` means `(2·rand − 1)·m`.

Each new target goes through `FUN_001906b8`, which sets `active = 1`, gives both eyes the **constraint 1 (LocalRotate)** and stores the target. Then, while `active`, the per-frame update runs.

### B.4 Per-frame orientation (`FUN_00190110`)

For each eye *E* (L then R):

```
d    = normalize(target - E.worldPos)
look = rows(X = normalize(d × Z), Y = d, Z = X × d)           // Z = world up (0,0,1)
rest = rows of E's current world, orthonormalised: Y' = n(E.Y), X' = n(Y' × E.Z), Z' = X' × Y'
q    = nlerp(quat(rest), quat(look), 0.25)                   // FUN_0024ab90; per poll, ≈ exponential smoothing
M    = matrix(q)                                             // rows X, Y, Z
H    = head rows orthonormalised: Hy = n(head.world.Y), Hx = n(Hy × head.world.Z), Hz = Hx × Hy
if dot(Hy, M.Y) < cosC (0.925):                              // clamp to a 22.3° cone around the head's +Y
    lx = dot(Hx, M.Y); lz = dot(Hz, M.Y)
    (lx, lz) = (lx, lz)·sinC/|(lx,lz)|   (or (sinC, 0) if both 0)
    Y = n(lx·Hx + cosC·Hy + lz·Hz); X = n(Y × Z_up); Z = X × Y
rows *= (+0x100 scales)                                      // the eye's original scale
E.local rotation = rows   (FUN_001da700)
```

With LocalRotate (`FUN_001da1a8` case 1): **world rotation = local rotation rows**, and world position = local pos · parent chain as usual. The eye's forward is its **+Y** axis. The 0.25 nlerp per poll makes saccades ease in over about 10 frames.

---

## C. Character shadows

### C.1 Who gets one

`charbase.dta`:
```
(show_shadow ($rnd) {if_else {&& {exists game} {! {game multiplayer}}}
                         {$this create_shadow (namespace 128 2000) (rnd $rnd)} 0})
```
- Each hero archetype's `my_preload` calls `{$this show_shadow "charsys/<c>/<c>_shadow.rnd"}`. Note `classic` → `male_shadow.rnd` and `hair_metal` → `hairmetal_shadow.rnd`.
- `game` is the always-present GamePanel. So guitarists get a shadow **in single player, both in songs and in the menus**, and none in multiplayer.
- The band has no character shadow. Venues instead hold static blob decals: `band_shadow.mesh` / `band_shadow1.mesh` with `band_shadow.mat` (textured, zmode Decal). `band_shadow1` is hidden when there is no singer or keyboard (`theband.dta` init).

### C.2 Shadow file (`<c>_shadow.rnd_ps2`; metal shown)

- A low-poly copy of the skeleton: `bone_*` meshes, `Bip01`, `Bip01 Footsteps`.
- Ten skinned body pieces `body.mesh`, `body.1..9.mesh` (16–309 verts, up to 4 bones each), plus a rigid `lespaul_burst.mesh` under `bone_pos_guitar`. Every archetype uses the Les Paul shape whatever guitar is held.
- `top.view` draws the body pieces and the guitar.
- All drawable pieces use **`charsys_shadow.mat`**: blend 3 (SrcAlpha), colour (0.173, 0.173, 0.173, **alpha 0**), unlit, no cull, zmode Normal, no texture. Every shadow file has the same material.

### C.3 Load and bind

`FUN_00195bd8` (create):
- Creates sub-directory `"<charname>_shadow"` (`%s_shadow`) with the given namespace sizes.
- Stores the `rnd` path. `FUN_00195d98` loads it.

`FUN_00195e10` (Character_v12, post-load, with the character's dir):
1. `top.view` (+0) → `SetShowing(0)`.
2. For every Transformable in the shadow dir whose name **starts with `bone_`**:
   - unlink it from its parent (`FUN_001d9f80`);
   - make it a child of the **same-named bone of the character** (`FUN_001da100`);
   - set its local to identity.
   The shadow skeleton then copies the animated skeleton exactly: world = charBone.world before the constraint.

### C.4 Light

`Character_v6` (0x18d500, vtable slot 6; "set shadow light") → `FUN_00196200`:
- Every `bone_*` transform in the shadow dir gets **constraint 3 (ShadowTarget)** with target = the light.
- Then `top.view` is shown.

Callers:

**In a song**, `VenueSpotLight` (`FUN_00179190`) is created per entry of `(arena (spot_lights "arena::spotlight01.lit"))`:
- It pairs player *i*'s character with that light and calls slot 6 with it.
- Every frame `FUN_00179420` aims the light (LocalRotate) with `look rows: Y = n(charPos − lightPos), X = n(Y × Z), Z = X × Y` (`FUN_0024adf8`).
- So the **shadow direction D = light +Y = normalize(characterPos − spotlightPos)**. The shadow is cast away from `spotlight01.lit`, which exists in every venue's `lighting.rnd_ps2`.

**In the menus**, CharsysPanel (`FUN_0013fa90`) calls slot 6 with `ui::<panel>_shadow.lit`:
- `char_single_shadow.lit` is directional with Y row (0.171, 0.970, −0.174).
- It also writes the plane globals `DAT_00368e40..4c = (0, −1, 0, 8)`, a wall at y = 8.
- **[I]** That plane is overwritten at the next poll (C.5). The result in the menus has no visible effect (C.6), so it can be ignored.

### C.5 Plane and projection

**Plane.** In every Character poll (`Character_v13`), if the character is visible and has a shadow, `FUN_00196498(shadow, top.view world)` sets the global plane:

```
plane = (0, 0, 1, −(root.z + shadow_plane_height))        // DAT_00368e40..4c; z = root.z + h
```

- `root` is the character's `top.view` world position (`*(char+0x18)` = top.view, via `FUN_0018e3d8`).
- `shadow_plane_height` is from `(charsys …)`, read by `FUN_00195b58`. The system `charsys.dta` says 3.0; `gh.dta` also has a trailing `(charsys (shadow_plane_height 2))`.
- **[I]** First-match lookup gives 3.0. Either value lies above the floor spot (height 1.5 / 1), which is what matters.
- The engine default `(rnd (shadow_plane 0 0 1 0))` is read by `FUN_001d9b90`.

**Projection.** Constraint 3 is applied in the world update (`FUN_001da1a8` case 3), with T = the light's world:

```
W  = W · T⁻¹                       // into light space (T orthonormal: transpose + −p·Rᵀ)
(a,b,c,d) = plane in light space   // FUN_00244d50(0x368e40, T⁻¹)
P  = | 1        0  0       |       // y' = −(a·x + c·z + d)/b : drop along the light's +Y onto the plane
     | −a/b     0  −c/b    |   (as columns of y)
     | 0        0  1       |
     pos (0, −d/b, 0)
W  = W · P · T
```

In world terms, every point of a shadow bone's frame, and so every skinned vertex, is moved along `D` onto the plane:

```
p' = p − D · (n·p + d) / (n·D)        // n = (0,0,1), d = −(root.z + h)
```

This is a planar projected (directional) shadow. The skinned pieces flatten because their bones are flattened. The rigid guitar piece flattens through its `bone_pos_guitar` parent.

### C.6 Drawing: a depth-only silhouette

The draw is `Character_v6` (0x18d860) → `CharBase_v6` (body), then `FUN_00196540` → `FUN_002ec850(top.view)` (shadow), right after the character.

`charsys_shadow.mat` has alpha 0 and blend 3. `FUN_0019d3b8` therefore sets:
- ALPHA = 0x44: `(Cs−Cd)·As + Cd`, i.e. no colour change;
- TEST = **0x200d**: alpha test GREATER 0, AFAIL = **ZB_ONLY**.

So the shadow **writes only depth**: a flattened silhouette on the plane `root.z + h`.

Arena draw order (`ArenaPanel_v11`) is venue → characters (with shadows) → `FUN_00169818`: floor-spot meshes (`floorspot_glow.mat`: additive, zmode **Transparent**, so depth-tested without writing) → transparent views.
- The floor spot sits at `root.z + floor_spot_height` (1.5 / 1), below the shadow plane (3 / 2).
- **[I]** So the silhouette cuts the guitarist's shadow out of the coloured spotlight pool on the floor. That is the visible "shadow".
- Additive beams and smoke drawn later are also masked where they pass behind the silhouette. This is a minor side effect.
- In the menus nothing is drawn after the character on that plane, so the menu shadow has no visible effect **[I]**.

---

## D. Address index

| Address | Function |
|---|---|
| 0x18d9f0 `Character_v1` | `create_eyes` / `create_walk` / `create_shadow` handler |
| 0x18d4d8 `Character_v5` (slot 5) | set eye mode → `FUN_0018fd10` |
| 0x18d500 `Character_v6` (slot 6) | set shadow light → `FUN_00196200` |
| 0x18d698 `Character_v12` | post-load: `FUN_00195e10` (shadow bind), `FUN_0018ffc8` (eyes init) |
| 0x18d780 `Character_v13` | poll: walk, `FUN_00196498` (plane), `FUN_0018fa18` (eyes) |
| 0x18d860 | draw: body + `FUN_00196540` (shadow view) |
| 0x18fd58 / 0x18fe18 / 0x18ffc8 | eyes ctor / bind meshes / read constraint and lid |
| 0x18fa18 | eyes re-target (modes) |
| 0x190110 | eyes orient + cone clamp |
| 0x1906b8 / 0x190720 | eyes start (LocalRotate) / stop (restore) |
| 0x18f8a0 | read `eye_*` config |
| 0x195b58 | read `shadow_plane_height` |
| 0x195bd8 / 0x195d98 / 0x195e10 | shadow ctor / load / bind bones |
| 0x196200 / 0x196498 / 0x196540 | shadow set-light / set plane / draw |
| 0x1da1a8 | transform constraints (1 LocalRotate, 2 LookAt, 3 ShadowTarget) |
| 0x179190 / 0x179420 | VenueSpotLight ctor (calls slot 6) / aim |
| 0x13fa90 | CharsysPanel load: `ui::%s_shadow.lit`, plane (0,−1,0,8) |
| 0x283998 `GameCharacter_v3` | create Face if `face_data` |
| 0x283a58 / 0x283a90 / 0x283ac8 / 0x283b48 | Face bind / reset / poll / draw-apply hooks |
| 0x287378 / 0x287490 / 0x287548 / 0x287608 | Face ctor / params / load / bind morphs |
| 0x2878c0 | Face reset (event list) |
| 0x2879d8 | Face poll |
| 0x287a80 | singer event path |
| 0x287b70 | excitement path |
| 0x287c78 | pose index |
| 0x287d40..0x287d70 | global face mutex |
| 0x287dd8 / 0x287e38 / 0x287f38 | blender setTarget / advance / apply |
| 0x2872d8 / 0x287258 | event at beat |
| 0x1fb3b0 `RndMorph_v1` | Morph SetFrame |
| 0x1fb140 | Morph key eval |
| 0x1abab8 | Animatable SetFrame (+ child anims) |
| 0x115860 `GuitarAnimInfo_v5` | track mode (`ANIM` 1, `T1 GEMS` 2) |
| 0x115d68 / 0x115f38 | mode-2 face/singer notes / note pairing |
| 0x10bcb8 → 0x288270 | ms per beat at tick |

---

## E. Implementation plan (engine)

### E.1 Data

1. `scene.cpp`: read `Morph` records (v3) into a `MorphObj {poses: [{mesh, keys[(w, frame)]}], intensity}`. The decoder already exists in `tools/rnd_classes/geometry.py::decode_morph`.
2. `chart.cpp`: also parse track `T1 GEMS` notes ≥ 108, paired on note-off.
   - Look each note up in `charsys:face_events`, then `charsys:singer_events` (`DataNode`).
   - Store `song.singerEvents` / `song.faceEvents` as `{startBeat, endBeat, symbol}`, with beats = tick/480.
   - Also fix the mode mapping in `docs/guitarist_hands.md` §1.2A.

### E.2 `FaceController` (new, `scene/face.{h,cpp}`, owned by Character)

Construct it when the spec's archetype or band char has `face_data`:
- Guitarists in songs: HERO_FACES.
- Singers: SINGER_FACES with `face_file` from `band_chars.dta`.
- Not menu characters.

Load:
- Load the `face_file` scene (`.rnd` → `gen/*.rnd_ps2`).
- For each Morph `X.mrf`, bind it to the character's `X.mesh`.
- Keep per-pose `pos`/`normal` arrays and the target's base arrays.

State: `cur, tgt, t, rate = 1/blend_time, timer, poseLength, eventOffsetMs, evCursor, curEv, open`. Use a static `owner` pointer as the global mutex.

`poll(beat, dSec, excitement, msPerBeat)`:
- Run §A.3 and/or §A.4 and the blender advance.
- For the singer, use the **unscaled** song beat.

`apply()` (before drawing, only for the LOD0 body):
- For `v < min(nTarget, nPose)`: `pos = lerp(P[cur], P[tgt], S(t))`, same for the normal, then renormalise.
- For `lashes`, write the same into its own target.
- Bump `MeshObj::revision`. The renderer already re-uploads on a revision change.
- Skip the work when `cur == tgt && t == 1` and nothing changed.

Excitement comes from the same global excitement the venue uses (0..4).

### E.3 `EyeController` (new, owned by guitarist Characters in songs)

Bind:
- Bind `L-eye.mesh`, `R-eye.mesh` and `bone_head.mesh`.
- Save the eyes' bind locals.
- Read `charsys` `eye_*` config and `create_eyes (constraint 0.925)`.

Mode:
- Set by the camera system on every `switch_cam`: `(eyes N)`, default 3.
- With 2 players, route by the `Left`/`Right` shot-name prefix.
- `setMode(0)` restores the bind locals.

Poll, after the skeleton is posed and before `updateWorld` of the eyes:
- Re-target per §B.3, using the crowd member list from `crowd_renderer` (3D members), the camera position and the character root.
- Orient per §B.4.
- Override each eye's **world rotation** with the result, keeping its world position from `bone_head`. This is LocalRotate.
- Character currently computes worlds in `updateWorld()`. Add a per-object override hook, e.g. `std::map<std::string, std::array<float,9>> worldRot_`, applied when composing.
- Each eye's current world rotation is the next frame's `rest`, so store it between polls.

### E.4 Shadow (`ShadowCaster` in Character, guitarists, single player)

1. Load `charsys/<c>/<c>_shadow.rnd`. The file name comes from the archetype's `show_shadow` call in `charsys.dta` (classic → `male_shadow`, hair_metal → `hairmetal_shadow`).
2. Each frame after posing:
   - For every shadow `bone_X`: `W = charBone_X.world`, then flatten each row/pos with `p' = p − D·(n·p + d)/(n·D)`.
   - For the rotation rows, apply the linear part: `r' = r − D·(n·r)/(n·D)`.
   - `D = normalize(charRootPos − spotlight01.worldPos)`. In menus use `ui::<panel>_shadow.lit`'s +Y, or just skip menus.
   - `n = (0,0,1)`, `d = −(root.z + 3.0)`.
   - Then compose `lespaul_burst.mesh` under the flattened `bone_pos_guitar` and skin the `body*.mesh` pieces with the flattened bones, using the existing skinning path with these bone worlds.
3. Draw `top.view` right after the character with **colour writes off, depth test and depth write on**. Exactly that: the PS2 alpha test fails and only Z is written.
4. Prerequisite: the floor spot (`docs/camera_walk_crowd_lighting.md` §5: spotlight aiming + `floorspot_glow.mat` ellipse drawn after the characters, depth-tested). Without it the shadow has no visible effect, so implement the spotlight/floor-spot first, then the shadow.
5. Optional debug toggle: draw the silhouette in dark grey (0.173) to check the projection visually.

### E.5 Testing

- `--selftest`: load every `*_face.rnd_ps2` and `*_shadow.rnd_ps2`, bind the morphs to the targets (`min` vert counts), and check that every shadow `bone_*` exists in the character.
- Screenshot hook:
  - singer close-up across a note-108 window (mouth opens about 257 ms early);
  - guitarist close-up at Peak excitement (expression changes about once a second);
  - a wide shot with the spotlight pool showing the silhouette cut-out.
