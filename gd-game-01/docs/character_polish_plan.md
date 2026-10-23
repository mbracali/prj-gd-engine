# Character polish plan: strum pluck, band realign, crowd variety, native walk layout

This is an implementation-ready plan for four items. Each one gives the original behaviour (with addresses), our current status, and the exact steps to implement it.

Marks: **[C]** confirmed from the decomp (`analysis/decomp/*.c`) or the data. **[I]** inferred.

Conventions:
- Frames are in beats unless the clip is realtime.
- Positions are world, Z-up. Row-vector transforms; row1 is "forward".
- `yaw(M) = −atan2(M.row1.x, M.row1.y)` (`FUN_0024a670`).
- `wrapPi` = `FUN_0027d9b0` folded into (−π, π].
- `ceil` = `FUN_0027d248`, `floor` = `FUN_0027d428` (both verified in `00270000.c`).

Related docs: `character_animation.md`, `guitarist_hands.md`, `camera_walk_crowd_lighting.md` §2, `crowd.md`, `crowd3d.md`.

---

## 1. Player-strum pluck (right hand)

**Result: in normal play, the player's strum does not drive the right hand.** The strum hand plays clips scheduled from the chart, and input matters only through the hit/miss "hitting" gate. The direct path from a strum to `strum_pluck_down` is live only under the debug cheat "Auto freestyle", and only inside solo extents. Our normal-play behaviour already matches; one doc fix is needed, and one optional feature.

### 1.1 Original: the swing path [C]

1. **BeatMatcher strum `FUN_0025a0e0`**:
   - Debounce: `!guitarStrum || lastSwing(+0xa8) + 52.0 < now(+0x88)`.
   - Then it calls the sink (+0x18) vtable +0x1c = **`PlayerMatcher_v2` ("swing", 0x2ff2f8)**.
   - Then it dispatches on state `+0xb8`: 0 = normal matching (`FUN_00263248`), 1 = freestyle soloer (`FUN_0025e470`).
2. **`PlayerMatcher_v2` @0x109728**:
   ```
   if (Player(+0x18)->+0x28 != 0 && (ch = FUN_002827e8(charList 0x42b9f8, playerIdx +0x14)))
       FUN_00284250(ch);    // -> FUN_00286530(ch+0x160 hand object)
   broadcast "swing" (script handlers only)
   ```
3. **`FUN_00286530`**:
   - `clip = right_hand.drv(hand+0xa0)->set["strum_pluck_down"]` (string 0x341b90).
   - It calls `FUN_0017fd18(frame = clip.start, delay = 0, drv, clip, mode 0)`: a pushed driver that blends in over the clip's blend width (0.24 beats).
   - The clip is 0..0.5 beats long and holds its last sample.
   - There is no same-clip check and no active check: every debounced swing re-triggers it.
4. **The gate `Player+0x28`** ("freestyle live"):
   - It is written only by `PlayerMatcher_v21` (sink +0xb4) as `(arg == 1)`. The Player ctor `FUN_00116198` sets it to 0.
   - `_v21` is called only from `FUN_0025a350(bm, state)`. That gets `state = 1` only from `FUN_0025a770` (per frame), when `bm+0xb4 != 0 && FUN_0025f9b8(songData+0x48, now)`.
   - `FUN_0025f9b8` is a range test `start <= t < end` over solo extents. That they are SoloExtents is [I], from the strings `10SoloExtent` and `soloer`.
   - `bm+0xb4` is set only by `FUN_0025a3e0`. Its callers are the `player_matcher set_auto_solo` handler `FUN_00109e10` (string 0x2ff338) and `_v21` (with 0).
   - `set_auto_solo` is sent only by the cheat `toggle_auto_solo` (`extracted_dta/config/gen/cheats_funcs.dta:393-412`, "Auto freestyle").
   - **So in normal play `Player+0x28` is always 0.**
5. Under the cheat, `FUN_00109e10` also sets the hand's auto_solo flag (+0x60, `FUN_00284270` → `FUN_00286528`).
   - `FUN_002867b8` then takes the `FUN_002865e0` branch, which never schedules the right hand.
   - So the right hand moves only through swing plucks inside solo extents.
   - The left hand plays `finger_vibrato_*[highest held fret]` (mode 0x80), or `finger_open` (0x82) when no fret is held. The fret bits come from `_v11` / `_v12` → hand+0x54.

### 1.2 Original: what input does in normal play [C]

- **Hitting gate.**
  - Hit (`_v3`) calls `FUN_00109dd0(p, 1)`, which sets hand+0x8c = 1.
  - Miss (`_v4`, including overstrum from `FUN_002641e0`) and pass (`_v5`, `FUN_002642e8`) set it to 0.
  - `active = hand+0x8c && hand+0x90` (`FUN_00286fd8`).
- **Right-hand scheduling `FUN_00286bc8`** (from `FUN_002867b8` when `beat >= nextR`):
  - Inactive (`!active || beat <= ev.start − 4`): play `strum_open` with kAnimNow, unless it is already the head; `nextR = ev.start − 0.25`.
  - Active: `nextR = ev.end`; push `set[ev.symbol]` with frame = start, `delay = max(0, ev.start − blendWidth − beat)`, mode 0.
- **Strum events** are built once from the chart by `FUN_00114dd8`: `{gem − 0.0625, gem + 0.1875}`, with the clip from `hand_strum_mapping` / `hand_events` (`charsys.dta:794-797`).
- HOPO (`_v16` / `_v17`), whammy (`_v18`) [I], star power and sustains have no hand effect, apart from the build-time long/short clip choice.

### 1.3 Ours

| piece | where | status |
|---|---|---|
| Strum event build | `game/guitar_anim.cpp:62-108` | matches |
| Right-hand scheduling | `game/guitar_anim.cpp:179-189` | matches: 4.0 / 0.25 thresholds, mode 0, the delay formula |
| Hitting gate | `app.cpp:161`, `:429`, `:971-977` → `venue_renderer.cpp:185` | matches |
| Player strum input | `app.cpp:858` → `Player::strum` (52 ms debounce) | correctly never reaches the hands |
| Auto-freestyle path | — | missing (debug cheat only) |

### 1.4 Steps

1. **Normal play: no code change.**
   - Verify with `--autoplay`: the right hand should strike about 0.3 beats before each gem.
   - Miss on purpose: the hand should go to `strum_open` and resume on the next hit.
2. **Fix `docs/guitarist_hands.md` §3.4 ("Player strum (inferred)", around line 290)** with the confirmed gate chain from §1.1.
3. **Optional, only if the "Auto freestyle" cheat is added:**
   - Add `GuitarHands::playerSwing(Character& c) { c.handPlay(Character::kRightHand, "strum_pluck_down", 0, 0.0f, true); }`.
   - Call it from the `Action::Strum` handler (`app.cpp:858`) after `Player::strum` passes its debounce, but only when `freestyleLive = autoSolo && insideSoloExtent(now)`.
   - While autoSolo is on, `GuitarHands::update` takes the `FUN_002865e0` branch instead: no strums or fingers scheduled, the left-hand vibrato/open behaviour from §1.1 step 5, and the auto_solo fret placement.
   - The SoloExtent source in the song data still needs RE.

---

## 2. Band `realign` (drummer and keyboard)

### 2.1 Original

`realign` has nothing to do with where a character stands. It is a beat-phase setting on the AnimDriver, and it only matters when a band member's scaled beat jumps.

- **Who sets it** [C]:
  - It is a dta property only; no message or script sends it.
  - `extracted_dta/charsys/gen/band_chars.dta` has `(main.drv (realign 1))` on DRUMMER_* (line 56) and KEYBOARD_* (line 94) only.
  - Bass, singer and the guitarists do not have it.
- **Reader** [C]: the AnimDriver constructor `FUN_0017f978` (`00170000.c:9745`) reads three keys:
  - `"anims"` (0x314140) → +0xc
  - `"restrict_flags"` (0x314148) → +0x10
  - `"realign"` (0x314158) → **+0x28**, read with `FUN_0022db10`, default 0.
  - The string 0x314158 has no other reference.
- **Handler: AnimDriver Poll `AnimDriver_v0` @0x17ff18** [C]. `last` = driver +0x24, initialised to 1e30.

```
if no clip drivers: return
if last == 1e30: last = beat
if realign && floor(beat) != floor(last):
    H  = newest clip driver with blend != 0                 // FUN_00180670
    if H:
        Ah = H.playFlags & 0xf                               // the driver's OWN copy (+0xc)
        A  = max(playFlags & 0xf) over H and every older driver
        if A - 1 >= 1:
            x = int(floor(beat)) ^ (int(floor(last)) + 1)    // 0 when exactly one beat boundary was crossed
            if x & (A-1):
                for H and all older drivers: playFlags &= ~0xf    // they stop phase-snapping
                if Ah-1 > 0 && (x & (Ah-1)):
                    FUN_0017fc28(drv, H.clip, 0x102)         // replay: kAnimNow | kAnimGraphLoop
last = beat
... then the usual starved / GraphLoop logic
```

- **Why this matters** [C]. The phase snap `FUN_0018a9d0` (`00180000.c:7368`) reads the driver's copy of the align nibble. When the scaled beat jumps by k beats, every aligned driver would otherwise jump by up to ±A/2 frames in a single tick, which shows as a pop.
  - With realign, the old drivers keep running unsnapped while they cross-fade out.
  - The fresh kAnimNow driver copies the clip's align again and starts phase-aligned.
- **When it fires** [I]: in practice on `set_frame_scale` changes from the `drum_/keys_ half_tempo | double_tempo | normal_tempo` events (character_animation.md §3.1), or on a seek.
- **Constants** [C]:
  - A = 2 for every band clip (BAND_GRAPH default `kAnimBeatAlign2`, `band_anims.dta:9`).
  - A = 4 for `drummer_active_{fast,medium}_half` (`band_anims.dta:135`, `:152`).
  - A = 0 for realtime clips.
- **No placement effect** [C]:
  - `band_channels.dta` has no `bone_facing*` channels (only `hero_channels.dta:66-69` has them).
  - `(bone.servo (use_delta 1))` appears only on the guitarist archetypes in `charsys.dta`.
  - So the band has no root motion and cannot drift. The pose-hold servo at char+0xd8 is a guitarist/walk mechanism (§4.7).

### 2.2 Ours

- **Placement is correct.** `venue_renderer.cpp:215-217` pins every member to `stage_spot_NN` each frame, and root motion is on only for the guitarist (`venue_renderer.cpp:89`).
- **realign is missing.** `character.h:31` lists it as not yet done.
- **No per-driver align copy.** `Character::Driver` (`character.h:168`) has no align field, and `pollChain` (`character.cpp:423`) reads `c.playFlags() & 0xf` from the clip every tick.
- **Related bug: the beat delta after a tempo change.** `Character::poll` derives `dBeat = max(0, beat − beat_)` from the already-scaled beat (`venue_renderer.cpp:196`, `:218` pass `beat * m.frameScale`).
  - When the frame scale goes up, dBeat becomes huge for one frame: every frame and delay advances by tens of beats, and clips run past their end.
  - When the scale goes down, dBeat clamps to 0.
  - The original advances frames by `dBeat·k` and lets only the phase target jump. That jump is exactly what realign absorbs.

### 2.3 Steps

1. Add `int align` to `Character::Driver`. Set it to `clip.playFlags() & 0xf` wherever a Driver is built: `start()` (`character.cpp:355-390`), `playAt` (`:398-407`), the Reset / first-clip paths, and the hand drivers.
2. In `pollChain`, use `d.align` instead of `c.playFlags() & 0xf`. Keep the `!realtime` and `!(mode & kAnimLoop)` conditions.
3. Give `Character` `bool realign_` and `float realignLast_ = 1e30f`.
   - Set realign_ from the role's `main.drv (realign 1)` in `band_chars.dta` (parse it in `VenueRenderer::loadBand`, `venue_renderer.cpp:369-400`).
   - A fallback is to hard-wire it for drummer and keyboard.
4. Split the scaled beat from the scaled delta: add `Character::poll(float beat, float dBeat, float ms)`, or pass the frame scale in.
   - `VenueRenderer` passes `beat·k` and `rawDBeat·k`.
   - Keep the current behaviour when k = 1.
5. At the top of `Character::poll`, before `pollChain`, add the realign block from §2.1. The replay is `start(chain_[h].clip, kAnimNow | kAnimGraphLoop)`. Leave the existing GraphLoop re-queue (`character.cpp:713`) after it.
6. Do not touch placement. Remove "drummer/keyboard realign" from `character.h:31`.
7. Test with a song that has `drum_half_tempo` / `drum_double_tempo`. The drummer should cross-fade over about one beat, with no frame pop and no frozen last pose.

---

## 3. Crowd variety, striped quad, `crowd_walk_offset`, facing, lighting

### 3.1 Heads and outfits

**Original** [C]:
- **3D members.** Nothing is random. Variety comes only from the fixed `crowd instance <arch> (head h mat m)` list in `arena/gen/crowd.dta` `finish_loading` (default branch, around line 737).
  - Each entry is built through `FUN_00173cc8` → ctor `FUN_00174380` → `CrowdChar_v3` @0x1743d0.
  - `remove (head …)` deletes every head mesh except head h (of `crowd_male1_head` … `male8_head`).
  - `swap (mat (outfit) (outfit02) (outfit03))` swaps materials through `FUN_001c45e8`; mat 0 is a no-op.
  - Pool sizes: fest 10; arena, big_club and theatre 12; basement and small_club(_multi) 17 (`crowd3d.md` §1.1).
  - The cheats `$cheat_crowd` 1/2 (`crowd.dta:466`; toggles in `config/gen/long_cheats.dta:163-200`) build the same pool but with every head set to 6 or 7. The default lists never use heads 6 and 7.
- **Impostors (flat crowd).** There is no per-instance variety.
  - Block i (`Crowd0i.mm`) shows exactly one image: the i-th pool character (crowd0..crowd4), re-rendered into the shared 128×256 render target each frame (`FUN_00172350`).
  - Multimesh instances are bare transforms. Every instance of a block is the same person in the same frame.

**Ours**:
- `crowd_renderer.cpp:111-115` hard-codes the default pool table, which matches the data.
- `:126` keeps head `h`.
- `:127` swaps outfits with `swapMaterials`.
- Block → member mapping is at `:47` and `:384-385`.
- **Default parity: already done.**

**Steps**:
- None for the default game.
- Optional: implement the `$cheat_crowd` 1/2 branches (head 6 or 7). Better still, parse the `finish_loading` switch from crowd.dta instead of the hard-coded table.

### 3.2 Striped quad (`crowd_striped_quad`)

**Original** [C]:
- `FUN_00170160(w, h, mesh, striped)` is called from `FUN_00171558` with `(h·0.5, h)`, and halves both, so W = h/4 and H = h/2.
- Plain quad: 4 vertices.
  - v0 (−W, 0, +H) uv(0,0); v1 (−W, 0, −H) uv(0,1); v2 (+W, 0, +H) uv(1,0); v3 (+W, 0, −H) uv(1,1).
  - Faces (0,1,2), (1,3,2).
- Striped quad: 8 vertices in columns at x = −W, −W/10, +W/10, +W with u = 0, 0.45, 0.55, 1.
  - Faces (0,1,2), (1,3,2), (2,3,4), (3,5,4), (4,5,6), (5,7,6).
  - u is linear in x, so it shows the same image as the plain quad. It is a tessellation, not a sprite sheet.
- The key is read from the venue dta `DAT_003637b4`, but **no venue sets it**, so GH1 always uses the 4-vertex quad.

**Ours**: `crowd_renderer.cpp:146-150` builds the 4-vertex quad (v flipped for the render-target origin). That is correct.

**Steps**: none. If modded data ever sets the key, build the 8-vertex version above with the same v flip.

### 3.3 `crowd_walk_offset`

**Original** [C]:
- The Crowd ctor `FUN_00170650` reads it as a vec3 into crowd +0x60 (string 0x311718). `config/gen/arena.dta:38` sets it to (0 −500 0).
- It is negated when `crowd_facing_backward` (+0xb8, FALSE in the data) is set.
- Its only use is in set_sizes `FUN_00172ec8`. When a 3D member leaves the active list **and** `crowd_walk_flags` (+0xbc) is non-null, it calls `FUN_00174ca8(char, offset, flags)`, which:
  1. stops any walk (`FUN_00174b60`);
  2. sets the local rotation to `MakeRot(offset, z)`, so the member faces away from the stage;
  3. plays the walk flags with mode 0x101 and a random phase in `[0, (end − start)/2)`;
  4. sets walking (+0xb4 = 1) and `use_delta` (`FUN_00182d80(+0x90, 1)`), presumably so the clip's root motion carries the member away [I].
- `crowd_walk_flags` is absent from all data, so **this never runs in GH1**. Leaving members just stand until they are culled. That only happens in Boot (`DAT_00363780 == 0`); otherwise they go straight back to the pool.

**Ours**: `setSizes` (`crowd_renderer.cpp:282-285`) returns removed members straight to the pool. That is correct except in Boot.

**Steps** (low priority):
- In Boot, move removed members to a `leaving_` list. Keep polling and drawing them, and return them to the pool when they are culled or the region switches.
- Skip the walk itself, since no data sets the walk flags.

### 3.4 Crowd facing ("average block facing")

**Original** [C]:
- **3D members** take the region instance's rotation rows and position `(inst.x, inst.y, region.floorZ)`.
  - This happens in switch_region `FUN_001727b0` and set_sizes `FUN_00172ec8`.
  - There is no per-frame turning toward the stage or the guitarist.
- **Billboards** use FastBillboardXYZ (constraint 8, `PsMultiMesh_v5` @0x1a34a0): the main camera's rotation at each instance position.
- **Crowd frame M** (`FUN_00171558`, `00170000.c` around lines 1090-1145):
  - For each block i (1-based) with `i−1 < crowd_forward_facing` (+0xa4 = 99, so every block), every instance's row1 is added into crowd +0x70.
  - Right after block i's sum, archetype i gets flag +0xb0 = 1 and local transform `MakeRot(Σ forwards of blocks 1..i, z)` at the origin. That is a **running** sum, which corrects crowd.md (it said every archetype used the final M).
  - After the loop, +0x70 is normalised to F̂.
- **Impostor camera** in draw `FUN_00172350`:
  - `M = MakeRot(F̂, z)` and `N = C·Mᵀ`, with C = the main camera's world rotation.
  - The camera sits at local (0, −d, 0) in N, so `R_imp = C·Mᵀ·R_A`, positioned around `A.world·(0, 0, h/2)`.
  - Net effect: the archetype is seen as if it faced the average block forward F̂, from the main camera's direction.
- `force_cam_facing` (+0xb4, per shot, 0 in all data) switches to the fixed view `FUN_001722a0`: rot diag(−1, −1, 1) at (0, +d, 0).
- Only fest has M ≠ I; theatre and basement are exactly identity.

**Ours**:
- Members use the instance rotation (`crowd_renderer.cpp:214-222`) and billboards use `camRot_` (`:389`). Both are correct.
- `renderImpostors` (`:342-361`) uses `camera.rot` directly with `eye = placement.pos + (0,0,h/2) − d·camFwd`.
  - That is only right when M = I and the archetype's placement is identity.
  - It is wrong in fest, and wherever an archetype is also standing as an active member with a yawed instance rotation.

**Steps**:
1. In the `CrowdRenderer` ctor, after the blocks are loaded:
   - Accumulate `F += inst.rot.row1` (rot[3..5]) per block.
   - After block i, set archetype i's base rotation to `M_i = makeRot(F, (0,0,1))`, where y = F̂, x = norm(y × z), z = x × y.
   - At the end, store `Mfinal = makeRot(normalize(F))`.
2. In `renderImpostors`, for archetype A with placement rotation R_A and position p_A:
   - `R_imp = C · Mfinalᵀ · R_A`
   - `eye = p_A + (0,0,h/2)·R_A − d·R_imp.row1`
   - `Mat4::cameraView(R_imp, eye)`
3. Optional: a shot-level `force_cam_facing` that uses the fixed view.

### 3.5 Crowd lighting environment `DAT_003637e4`

**Original** [C]:
- The first statement of crowd draw `FUN_00172350` is `FUN_002ec850(DAT_003637e4)` (`00170000.c:1355`), which selects an Environ. It is the only reference.
- `DAT_003637e4` = Arena (0x363748) + 0x9c = **`arena::crowd.env`**. `FUN_00168370` stores it there and removes it from the draw lists. (Neighbours: +0x84 `stagechar.env`, +0x88 `singer%i.env`.)
- That one env lights, in order:
  1. the active 3D members;
  2. the leaving members;
  3. every impostor render of the archetypes (materials have `use_environ 1`).
- The billboard quads are unlit (`use_environ 0`, blend 3, alpha cutout); their lighting is baked into the render target.
- Draw order in ArenaPanel draw `FUN_001696d8`: venue views, blur, the environment at *(+0xdc)+8, band, then **crowd last**.
- `crowd.env` per venue is all Directional lights, animated by the venue's light anims (table in `camera_walk_crowd_lighting.md` §4).

**Ours**:
- `crowd.env` already lights the impostors (`crowd_renderer.cpp:347-358`) and the members (`:366-378`), with venue LightAnims resolved through the world lookup.
- Differences:
  1. `venue_renderer.cpp:286` draws the crowd **before** the guitarist and band (`:290-303`). The original draws it after them.
  2. `renderImpostors` (`venue_renderer.cpp:279`) runs even under `hide_crowd`, and it renders all 6 archetype render targets even for blocks that do not exist.
  3. The flat quads get no fog [I].

**Steps**:
1. Move `crowd_->draw(vp)` after the band loop, still before the transparent views.
2. Skip `renderImpostors` when `hideCrowd`, and render only archetypes whose block exists and has instances showing.
3. Check `crowd.env` `fogEnable` per venue. If any venue sets it, give the quad RenderState the env fog, as `SceneRenderer::setMatFlags` does. [I]

**Other crowd gaps** (already in crowd docs, listed for completeness):
- Animation assignment should be a Fisher–Yates split with a shared LRU, `kAnimReset|kAnimGraphLoop` and a random phase (`FUN_00173e70` / `FUN_00173508`).
- The intro `set_sizes(1, 1)` is missing.
- The `crowd_*_tempo` frame scale is missing.
- Culling should be a sphere test: `(sphere 0 0 0 53 bone_pelvis.mesh)`.

---

## 4. Walking: the native CharWalk layout

This section replaces our "whole walk cycles + steering + home-spot ease-back" with GH1's exact CharWalk.

All addresses are in `analysis/decomp/00190000.c` unless noted. The CharWalk object sits at char+0xdc; its field table is in `camera_walk_crowd_lighting.md` §2 1.1. The fields used here:

| field | meaning |
|---|---|
| `+0x10` slop | 12 |
| `+0x14` overshoot | 0 |
| `+0x18` undershoot | −18 |
| `+0x1c` | path `vector<Vector4>` |
| `+0x2c` | destination yaw |
| `+0x30` | clip current at planning |
| `+0x40` | segment direction |
| `+0x50` | `corr` (speed correction) |
| `+0x54` | state |
| `+0x58` | k (chain clips queued) |
| `+0x5c` | seg (path segment index) |
| `+0x60` | chain `vector<WalkClip{clip, startFrame, srcFrame}>` |

In a chain entry, **`startFrame`** is the frame of this clip at which it enters. **`srcFrame`** is the frame of the *previous* chain clip (or, for entry 0, of the mood clip playing at planning) at which this clip is queued to start.

### 4.1 Per-frame order (original) [C]

1. `GuitarCharacter_v13` @0x284390:
   - If no blend is in progress: random walk (`WalkDue` → `WalkStep`), otherwise queue the next mood clip.
2. `Character_v13`:
   1. AnimDriver poll, with `use_delta` root motion (character_animation.md §1.4);
   2. **walk poll `FUN_00197388`** (queues chain clips, steers);
   3. **pose-hold poll `FUN_001958b0`** (char+0xd8).
3. If the walk state went from {1,2} to {0,3} this frame: `ResetTimer` (`FUN_00284618`).

### 4.2 WalkStep / StartWalkTo (`FUN_00284818`, `FUN_002848c0`) [C]

- `r = FUN_0024a1d0(1, n−1)` = `1 + rand % (n−2)`. That is the range **[1, n−1) exclusive** (`FUN_0024a080`), where n is the spot count including the hub.
- `dest = (nearest(incl. hub) + r) mod (n−1)`, made non-negative.
- If `soloPt ≥ 0 && pre1 > 0`: `dest = soloPt`.
- `StartWalkTo(dest, 4)` does nothing unless: walks are allowed, walk state ∈ **{0, 3}**, and `dest != nearest`.
- It then builds the flags (doc §5.1, our code already matches) and the path `[hub, dest]`, and calls `Start(destYaw = yaw(spot[dest]), path, mode)`.

### 4.3 Start `FUN_00196c88` [C]

1. `path = [root.pos] + points` (root local pos, `FUN_0018e238` +0x30); `+0x2c = destYaw`.
2. **Turn `FUN_00197c50(mode)`**:
   - `cur` = the newest driver with blend ≠ 0 (`FUN_00180670`). `set` = cur.clip's clip set (+0x28).
   - For every entry in the set (stride 0x30: +4 clip, +0xc flags) with a loaded clip and `(turnFlags & flags) == turnFlags`:
     - `tr = FUN_0017f1d0(cur.frame, set, cur.clip, cand, mode)`; skip if null. `tr+4` = src frame (in cur), `tr+8` = dest frame (in cand).
     - Predicted pose (P, θ):
       - If the 0xd8 hold has both pos and yaw set: P = hold.pos, θ = hold.yaw.
       - Otherwise start from the root pose and integrate cur.clip from cur.frame to tr.src (`FUN_001883d8`).
       - Either hold value that is set overrides its part.
     - `heading = −atan2(path[1].x − P.x, path[1].y − P.y)`.
     - `a = FUN_00196588("turn_", name)`.
     - `err = |wrapPi(wrapPi(θ + a) − heading)|`.
     - Keep the entry if `err < best` (best starts at **π/4**, strict `<`, so the first entry in file order wins ties). There is no tempo or enable check.
   - Push `{turn, tr.dest, tr.src}` and set `path[0] = P`. Fail if no entry was kept.
3. **Walk `FUN_001981c0`**:
   - Entries with the enable bit 0x80000000 (song tempo), `(walkFlags & flags) == walkFlags`, LRU stamp (+0x18, float) below the best so far, and an **explicit graph edge** from turn to walk (`FUN_0017efe0`; no computed fallback).
   - Keep the smallest stamp, then stamp it (`FUN_0017e7e0`).
   - Push `{walk, edge+8 (dest), edge+4 (src)}`.
4. **Stop `FUN_00198570`**:
   - `need = wrapPi(destYaw + atan2(dx, dy))`, with d = `path[n−1] − path[n−2]` (that is, `destYaw − walking heading`).
   - Over entries with `(stopFlags & flags) == stopFlags`, take the min `|wrapPi(need − FUN_00196588("stop_", name))|`. No limit, first wins ties.
   - Push `{stop, floor(stop.end − 2.2 − stop.blendWidth·0.5), (unset)}`. Here `stop.end` is clip +0x10 and blend width is clip +0x20.
5. If all three succeed: addref the clips, set `+0x30 = currentClip` (`FUN_00180710`), set `state = 1`, and call **Layout** right away.
6. On any failure: clear the path and the chain; nothing happens.

### 4.4 Layout `FUN_001989c8` (exact) [C unless marked]

```
Layout():
  cur = newest started driver (FUN_00180670)
  if !cur || cur.clip != +0x30 || cur.frame > chain[0].src || any chain clip not loaded: Cancel(); return false
  loop = FUN_0017f1d0(chain[1].startFrame, set, walk, walk, kAnimLongest)   // self edge; null -> Cancel
  loopSrc = loop+4 ; loopDest = loop+8
  SetSegment(1)                     // FUN_00199488: seg=1, dir=norm(path[1]-path[0])
  clear hold (FUN_002b0890 / FUN_002b0868); use_delta = 1 (FUN_00182d80(char+0x90, 1))

  // required distance L: straight path up to path[n-2], then to the stop clip's start position S
  L = Σ_{j=0}^{n-3} |path[j+1] - path[j]|           // n = path size (3 for root,hub,dest)
  S = path[n-1], yaw = destYaw; Integrate(stop, from stop.end, to stop.startFrame, &S, &yaw)   // backward
  L += |path[n-2] - S|

  // first step end: ceil(turn start), parity-matched to the stop start
  f0 = chain[0].startFrame;  t = ceil(f0)
  if (round(t) & 1) != (round(chain.back().startFrame) & 1): t += 1     // round = ±0.5 then trunc
  i = 0                       // chain index of the clip being simulated
  next = chain[1].srcFrame    // the frame in clip i where clip i+1 starts
  rem = L
  loop:
    prevRem = rem ; stepClip = i ; stepStart = f0 ; end = t
    if next <= end:                              // the step crosses into the next chain clip
        rem -= |Integrate(chain[i].clip, f0, next)|  ; if rem <= 18: goto done   // 18 = -undershoot
        i += 1
        if i == chain.size()-1:                  // reached the stop: insert one more walk loop
            chain.insert(before stop, {walk, loopDest, loopSrc})
        over = end - next
        f0 = chain[i].startFrame ; end = f0 + over ; next = loopSrc
        rem -= |Integrate(chain[i].clip, f0, end)|
    else:
        rem -= |Integrate(chain[i].clip, f0, end)|
    if rem <= 18: goto done
    f0 = end ; t = end + 2.0                     // 2-beat steps
  done:
    errThis = -rem - overshoot                   // > 0: past S by more than overshoot (0)
    use this step if -rem <= 18
    else errPrev = prevRem + undershoot          // = prevRem - 18, how far short beyond the allowance
         if errPrev < errThis: use the previous step: i = stepClip, end = stepStart, rem = prevRem, err = errPrev,
                               sign = short
    // trim: keep chain[0..i] and the stop
    while chain.size() > i + 2: release + erase chain[i+1]
    chain.back().srcFrame = end                  // the stop starts at `end` in chain[i]
    corr = 0 ; state = 2 ; k = 0
    residual = -rem                              // >0 past S, <0 short of S
    if err > 0:
        total = Σ_i (exit_i - chain[i].startFrame),  exit_i = chain[i+1].srcFrame, exit_last = stop.end
        signedErr = residual > 0 ? err : -err
        corr = -signedErr / total                // units per beat (+ = speed up along the motion)
        residual -= signedErr
    path[n-1] += unit(path[n-1] - path[n-2]) * residual     // [I] see note
    return true
```

Notes:
- `Integrate(clip, f0, f1)` is `FUN_001883d8` with step 0.01. The 0.01 is used only for the optional velocity output (null here).
  - It takes a single chord of `bone_facing.pos/rotz` between f0 and f1: `pos += rotZ(yaw − rotz(f0))·(P(f1) − P(f0))`, then `yaw += wrapPi(rotz(f1) − rotz(f0))`. f1 < f0 integrates backward.
  - In Layout, each step's length is taken from a pos reset to 0, so only |Δ| matters. Our `Character::rootDelta` already gives the same chord.
- The 2-beat steps and the parity match keep the beat phase of the align-2 walk clips. The walk length is therefore quantised to whole beat pairs, and the remainder becomes `corr`.
- What the decision rule means:
  - A step that ends within 18 units short of S, or anywhere past it, is taken.
  - If the step overshoots by more than 18, the previous step is taken instead when its shortfall beyond 18 is smaller.
  - With overshoot 0, any overshoot (even 1 unit) gets a negative `corr`. A shortfall of up to 18 gets **no** correction: the guitarist simply stops short.
- Quirk: if the threshold is hit in the first half of a crossing step (before `next`), `i` is not advanced and the stop's src is `end` in the old clip, past its original exit frame. Reproduce this as-is.
- The final path tweak [I]: the decompile multiplies the *unnormalised* segment `path[n−1] − path[n−2]` by the residual, right after normalising the step vector (which is then unused). Read literally, the last point would move by hundreds of units, so a register mix-up in the VU0 decompile is the likely explanation.
  - Implement it as `+= unit(segment)·residual`: the last-segment steering target then ends where the walk actually ends.
  - Verify in the disassembly at about 0x199400 if time allows.

### 4.5 Walk poll `FUN_00197388` (exact) [C]

```
switch state:
  1: if !Layout(): return        // then falls into state 2
  2: cur = newest started driver
     if (!PendingAfter(cur) || k == 0):        // FUN_00180630: is a newer driver still waiting?
         if k < chain.size():
             e = chain[k]
             QueueClip(e.clip, frame = e.startFrame, delay = e.srcFrame - cur.frame)   // FUN_0017fd18, ref flags 4
         else: state = 3
         k += 1                                // ALSO when entering state 3, so k = chain.size()+1
     Steer()
  3: if currentClip (FUN_00180710) != chain.back().clip:   // a mood clip has taken over from the stop
         Cancel()
         hold.pos = root.pos ; hold.yaw = destYaw
     else Steer()
```

`IsWalking` `FUN_00197548` is `state ≥ 2 && cur.clip == chain[min(k−2, size−1)].clip` [C]. Because k is `size+1` once state 3 is entered, IsWalking is:
- false until the turn starts blending;
- true through the turn and the walks;
- briefly false on the frame the stop starts blending;
- **true again for the whole stop clip**, until a mood clip replaces it.

This corrects camera_walk_crowd_lighting.md §6.2 ("false during the stop"). It affects the camera's `walk_ok` and `kGuitarLoseWalk`.

Cancel `FUN_001971e8` [C]:
- If state ≠ 0 and the driver head (+0x2c) is a queued chain clip that has not started (blend == 0, has a predecessor), pop it (`FUN_0017fe20`).
- Release the clips, clear the chain and the path, and set state = 0. The hold is **not** set by Cancel.

### 4.6 Steering `FUN_001975f0` (exact) [C]

It runs only when dBeat (char+0x88) ≠ 0 and the IsWalking condition holds (the same inline test).

```
clear hold
P, θ = root pos, yaw
// pred: root pose 1 beat ahead through the chain
i = min(k-2, size-1); f = cur.frame; tgt = f + 1; frac = 1; pred = P; pθ = θ
while i < size-1 && chain[i+1].srcFrame < tgt:
    Integrate(chain[i], f, chain[i+1].srcFrame, &pred, &pθ)
    tgt = chain[i+1].startFrame + (tgt - chain[i+1].srcFrame); f = chain[i+1].startFrame; i++
if i == size-1 && clipEnd(chain[i]) < tgt: frac -= tgt - clipEnd; tgt = clipEnd
Integrate(chain[i], f, tgt, &pred, &pθ)
// target T
if seg == npts-1:                  // last segment: the ideal pose 1 beat from now
    T = path[last], Tθ = destYaw; e = stop.end
    for j = size-1 down to i+1: Integrate(chain[j], e, chain[j].startFrame, &T, &Tθ); e = chain[j].srcFrame
    Integrate(chain[i], e, tgt, &T, &Tθ)
else:
    T = path[seg]
    if (pred - path[seg])·dir >= 0: SetSegment(seg+1)
// corridor clamp (dir and seg after any advance)
foot = path[seg-1] + dir*((T - path[seg-1])·dir); e = T - foot
if |e| > slop(12): foot += dir*(|e| - slop); T = foot + e*slop/|e|
α = FUN_00196690(P, T, pred) = asin(cross_z(T-P, pred-P) / (max(|T-P|,5)·max(|pred-P|,5)))
if corr != 0 && |pred-P| > 0: P += (pred-P) * (dBeat·corr/|pred-P|)
θ += (-α / max(frac, 0.25)) * dBeat         // FUN_0024af18 rotates the root rows about Z
```

### 4.7 Pose hold `FUN_001958b0` (char+0xd8) [C]

- Fields: +0 yaw, +0x10 pos (1e30 = unset), +0x20 tol = 0, +0x24 gain = 1.
- It does nothing unless the hold has a pos or yaw set **and** `use_delta` is on (bone servo +0x78).
- When there is a queued driver whose delay ≥ 0 (a clip waiting to start), using the driver it follows (`curD`):
  1. Predict the pose at its start: integrate curD.clip from curD.frame to curD.frame + delay.
  2. `rate = dBeat·1.1 / max(delay, curD.clip.blendWidth)`. Which clip's blend width this is depends on the driver link at +0x28 [I].
  3. If `|hold.pos − predPos| > tol`: `root.pos += (hold.pos − predPos)·rate·gain`.
  4. `root.yaw += wrapPi(hold.yaw − predYaw)·rate`.
- `hold.pos` is the root position **where the walk ended**, not the spot. So the hold cancels the stand clips' residual drift and turns the guitarist to face the crowd at each mood transition, but **never pulls him to the spot**.
- The hold is cleared by Layout and by every Steer call. It persists across mood clips until the next walk.

### 4.8 Ours vs original

| aspect | ours (`game/guitar_walk.cpp`) | original |
|---|---|---|
| random r | `uniform_int(1, n−1)` inclusive (`:154`). With 3 spots, r = 2 gives dest = cur, the walk is rejected and the timer re-rolls, so about half the walks are lost | [1, n−1) exclusive |
| StartWalkTo state | `state_ == 0` only | {0, 3} |
| turn start pose | the current root (`:177`) | predicted at the transition src (or the hold), and `path[0] = P` |
| chain queueing | `playClip` through the graph at queue time, whenever the head has blended in (`:139-153`) | explicit `{clip, startFrame, srcFrame}` queued as soon as the previous one starts blending, with delay = src − cur.frame |
| walk count | `ceil((L − turn − stop)/cycle) − 1` loops (`:232-242`) | 2-beat simulation, end choice, `corr` |
| stop start | graph edge | `floor(end − 2.2 − bw/2)`, src = the chosen step frame |
| steering | rotates toward `path[seg]` using the cycle's mean direction, `min(1, dBeat)` (`:261-282`) | 1-beat prediction, corridor, ideal backward target on the last leg, `corr` displacement, `/max(frac, 0.25)` |
| IsWalking | turn or walk current | §4.5 (includes the stop) |
| after the walk | continuous ease of pos **and** yaw to the spot (`:283-298`) | hold: yaw to destYaw, pos to the end position, only while a clip is pending |
| cancel | clears state | also pops an unstarted queued chain clip |

### 4.9 Character API needed (`scene/character.h/.cpp`)

1. **`struct DriverInfo { int clip; float frame, delay, blend; }`** with:
   - `newestStarted()` (`FUN_00180670`: the newest with blend > 0);
   - `pendingAfter(newestStarted)` (`FUN_00180630`);
   - `head()`.
2. **`Transition transition(int fromClip, float fromFrame, int toClip, int mode)`**: the edge search from `start()` (`character.cpp:355-388`) factored out without pushing. It returns `{src, dest, found}`.
   - `fromFrame` is a parameter, since Layout queries the walk's self-edge at `chain[1].startFrame`.
   - Keep the computed fallback for modes 3/4.
3. **`bool explicitEdge(int fromClip, int toClip, Transition&)`** for `FUN_0017efe0` (graph only, Longest cost).
4. **`void queueClip(int clip, float frame, float delay)`**: push `Driver{clip, kAnimLongest, frame, delay, delay == 0 ? 1e-10f : 0}`. Do not stamp it.
5. **`void popUnstarted()`** for Cancel: drop `chain_.back()` if its blend == 0 and the chain has more than one entry.
6. **`float clipBlendWidth(int clip)`**, `clipStart`/`clipEnd` (exist), and `touchClip(int)` (the LRU stamp; `playClip` does it today).
7. **`Integrate`**: a free function on top of `rootDelta`:
   `void integrate(const Character&, int clip, float f0, float f1, V3& pos, float& yaw)` → `pos += rotZ(yaw)·Δ` (Δ already rotated by −rotz(f0)), `yaw = wrapPi(yaw + dθ)`.
   - Check the sign of `rotZ(yaw)` against `applyRootMotion` (rows rotated by +dθ, then pos += Δ·rows).
8. `dBeat` for steering and the hold: keep `GuitarWalk::lastBeat_` (exists).

### 4.10 Implementation steps (`game/guitar_walk.cpp/.h`)

1. **Data members.** Replace `chain_` (`vector<int>`) and `loopsLeft_` with:
   - `struct WalkClip { int clip; float start, src; }; std::vector<WalkClip> chain_;`
   - `int k_ = 0, seg_ = 1; V3 dir_; float corr_ = 0, destYaw_ = 0; int planClip_ = -1;`
   - `struct Hold { bool hasPos = false, hasYaw = false; V3 pos; float yaw; } hold_;`
   - Constants: `kSlop = 12, kOver = 0, kUnder = -18`.
2. **`update()`** keeps the random-walk / pre_solo logic, with these changes:
   - `r = 1 + rng % (n − 2)` (exclusive upper bound).
   - Accept state ∈ {0, 3} in `startWalkTo`.
   - Remove the chain-queueing branch (lines 138-158); it moves to `poll()`.
   - Return `state_ == 1 || state_ == 2`, so the mood clips wait as in `FUN_002855f8`. Do **not** block the mood queue in state 3: the mood clip ends the walk.
   - Reset the timer on the {1,2} → {0,3} edge.
3. **`startWalkTo()`**:
   - Keep the flags code (`:165-180`).
   - Build `path_ = {root.pos, hub, D.pos}` and `destYaw_ = yawOf(D)`.
   - Then implement Turn / Walk / Stop exactly as §4.3, using `transition()` / `explicitEdge()` and the hold-or-integrated start pose.
   - Set `planClip_ = currentClip`, `state_ = 1`, then call `layout()`.
   - **Do not call `playClip`.**
4. **`layout()`**: §4.4 verbatim. On failure call `cancel()`.
5. **`poll(Character&, float beat)`**, called **after** `guitarist_->poll()`, replacing `steer()`:
   - §4.5 state machine (state 1 → layout; state 2 → queue `chain_[k_]` with `queueClip(start, src − cur.frame)`; k_++; steer; state 3 → end check or steer);
   - then `hold()` (§4.7).
   - `venue_renderer.cpp:187` already calls at this point.
6. **`steer()`**: §4.6 verbatim. Use `c.placement()` / `setPlacement()` for P and θ, and rotate the rows with the existing `rotateZ`.
7. **`hold()`**: §4.7. It needs `pendingAfter()` → `{clip, frame, delay}` of the waiting driver and of the driver before it.
8. **`walking()`**: §4.5 (`state_ >= 2 && currentClip == chain_[min(k_−2, size−1)].clip`, with index ≥ 0).
9. **`cancel()`**: `popUnstarted()` if the head is a chain clip with blend 0, then clear `chain_`, `path_` and state.
   - Delete `rehome_` / `home_` and the whole "keeps to his spot" branch (`:283-298`). If drift appears again, the cause is in root motion (for example, stand clips with non-constant `bone_facing`); the hold should absorb it at each transition.
10. **`loseTeleport`**: unchanged, but set `hold_` to the spot's pos/yaw afterwards, or clear it [I]. The original only sets the root (`FUN_0018f060`).
11. **Callers.** `VenueRenderer` (`:167`, `:181-187`) keeps `walkBusy` gating the mood `playAt` / `play`. Gate `playAt` on `state_ ∈ {1,2}` rather than on `walkingNow()`.
12. **Debug.** Keep `GD01_LOG_SHOTS` logging of the chain (clip names, start/src), `corr`, and the final residual.
13. **Checks.**
    - big_club 01 → 02: `turn_right_90`, `walk(_normal)_right` × N, `stop_left_90`. The end position should be within 18 units of walk_spot_02, facing −Y after the next mood transition.
    - Arena solo walk to the catwalk front.
    - `walk_ok 0` shots are rejected during the stop clip.
