# GH1 PS2 RE: hand close-up camera, walking, crowd, lighting

Marks: [C] = read in decomp/disassembly/data; [I] = inference. Addresses are EE virtual addresses.

## 0. Summary
- **Hand close-ups** are the `SOLO_NEAR` group (`kCamSolo|kCamCloseup`). Two kinds:
  - `Cam_t_p_close` shots with `singer_y` 0.9–1.3: framed on the guitarist's `bone_head`, with the head pushed above the top of the frame, so the guitar and hands fill it.
  - `Cam_t_p_circle01` (arena, fest): the camera rides `spot_neck_fret01` and looks at `spot_neck_fret20`.
  - They are picked only inside solos, by the native CameraSystem picker (flags, freshness, side/distance/walk/star-power rules).
  - A test run of our current build shows they do appear in solos. But our focus is a fixed point (walk_spot_02+60) rather than the filtered head, the picker rules and timing differ, and DOF, hide_crowd and shaky are missing.
- **Walking:** CharWalk at char+0xdc, guitarist only.
  - Random walks: every 35–55 s at Okay, 20–40 s at Great. Planned walks before solos (`pre_solo1`/`pre_solo2`; arena goes to solo_walk_point 1).
  - Path: current → hub (the last walk spot) → destination.
  - Clips: turn/walk/stop chosen by angle; movement from bone_facing root motion.
  - Single player starts on **walk_spot_01** (not 02). The band never walks.
  - Our `character.cpp:170` drops every clip with kWalk* flags.
- **Crowd:**
  - P0 bug: `character.cpp:170` filters `kWalkMask 0x3fe0`, which also removes the crowd's C_OK/C_GOOD/C_GREAT/C_CLAP/C_JUMP/C_CHEER clips. Only bad/boo clips survive, so the crowd never cheers.
  - Sizes and animation groups are applied on shot picks (`update_crowd`), starting from 1/1 at init.
  - 3D near-stage members, hide_crowd and outfit swaps are missing.
- **Lighting:**
  - Billboard constraints are thrown away (about 280 light cones face the wrong way).
  - `_transparent` views must be drawn after the band.
  - The unlit vert_amb colour is wrong.
  - Extra material stages, VenueSpotLight + floor spot, RndFlare, particles and DOF are missing.
  - The blur, fireworks and fire are never triggered by shipped data.

## 1. Hand / guitar close-up camera shots

### 1.1 Where they are in the data [C]

- `config/gen/macros.dta`: `kCamLeft 1, kCamRight 2, kCamCloseup 4, kCamNear 8, kCamFar 16, kCamBehind 32, kCamSolo 64, kCamNoSolo 128, kCamSpecial 256`.
- `arena/gen/camera.dta` `$camera.groups`: only one group carries **kCamCloseup**:
  `SOLO_NEAR {| kCamSolo kCamCloseup}` (4|64 = 0x44). `SOLO_FAR` is `kCamSolo kCamNear` (0x48). All regular groups are `kCamNoSolo` + Near/Far/Behind + Left/Right; LOSE/WIN/INTRO are `kCamSpecial`.
- `$camera.pools`: `flr_near 0.5`, `flr_far 0.5`, `band 0.2`, `balcony 0.3`, **`solo 1.0 (SOLO_NEAR SOLO_FAR)`**. The solo pool is a normal pool; it is only selectable because its groups are the only ones with kCamSolo.
- The close-ups themselves (`venues/<v>/gen/camera.dta`, `$camedit.SOLO_NEAR`, 2 shots per venue):

| venue | shot | path | singer_out (x,y) | offset_out | fov in→out | other |
|---|---|---|---|---|---|---|
| arena | SOLO_NEAR01 | **Cam_t_p_circle01** | 0.2, 0.1 | 44 34 63 | 45→60 | dof |
| arena | SOLO_NEAR02 | **Cam_t_p_circle01** | 0.0, 0.5 | 64 31 −115 | 45→60 | dof |
| fest | SOLO_NEAR01 | **Cam_t_p_circle01** | 0.0, 0.4 | 60 31 −115 | 45→60 | dof, crowd_region 10 |
| fest | SOLO_NEAR02 | Cam_t_p_close | −0.3, 1.3 | −15 −10 15 | 45 | dof |
| basement | 01 / 02 | Cam_t_p_close | −0.4, 1.2 / −0.3, 1.0 | −65 −25 −5 / 15 −30 20 | 45 | hide_crowd, dof |
| big_club | 01 / 02 | Cam_t_p_close | −0.3, 1.1 / −0.3, 0.9 | −30 −35 −10 / 20 −30 20 | 45 | hide_crowd, dof |
| small_club | 01 / 02 | Cam_t_p_close (start=end=100) | −0.4, 1.0 / −0.2, 1.1 | −70 −35 35 / 10 −25 60 | 45 | hide_crowd, dof |
| theatre | 01 / 02 | Cam_t_p_close | −0.3, 1.2 / −0.4, 1.1 | 0 −30 30 / −55 −35 0 | 45 | crowd_region 11, dof |

  All have `start 0 end 0 duration 0` (a static frame of the path) except small_club (frame 100%).
- Paths (`arena/gen/cam_paths.dta`) [C]:
  - `Cam_t_p_circle01`: **parent `guitarist0::spot_neck_fret01.mesh`, target `guitarist0::spot_neck_fret20.mesh`** (fallback `arena::stage_spot_01.mesh`). The camera is carried by the nut end of the guitar neck and looks down the neck toward the body/strumming hand: this is the "fret-board / hands" shot (arena, fest). Path keys (campaths.rnd_ps2): 68 position keys, constant (−43.8, −32.3, −13.9), no rotation keys.
  - `Cam_t_p_circle02`: target `spot_neck_fret01` (fret-hand end of the neck); `Cam_nt_np_circle01`: target fret20, parent venue.view. Used by band_POV/flr_far/WIN/LOSE shots, not by SOLO_NEAR.
  - `Cam_t_p_close` / `Cam_t_p_*` with no cam_paths entry: **parent and target = the VenueCam focus object** (below). `Cam_t_p_close` keys: (25.3, −33.3, 16.1) at frame 0 → (25.7, −33.5, −35.5) at 1920; no rotation keys.
- The "close" SOLO_NEAR shots are hand shots because of `singer_out y ≥ 0.9`: the focus (the guitarist's **head**, below) is pushed to/above the top edge of the screen, so the frame shows the torso, guitar and both hands from ~45–80 units away. Worked example, theatre SOLO_NEAR01: camera = head + (25.3, −63.3, 46.1), aimed at the head (looking ~34° down), then shifted down by 1.2·dist·tan(fov/2)/aspect ≈ 30 units, keeping orientation → the view centre lands on the guitar body / strumming hand.
- No other group contains close-ups; all regular `Cam_t_p_close` shots use singer_y ≤ 0.6 (waist-up / full body). There is no `bone_*` hand target in any camera data; the only guitarist sub-object targets are `spot_neck_fret01/20` and `singer::bone_neck` (Cam_close_singer).

### 1.2 The focus object ("band focus") — corrected [C]

- VenueCam ctor `FUN_0016d9f8`: creates a Transformable at VenueCam+0x18 (the focus) and points the default target (+0x14) and parent (+0xc) at it; reads `cam_filter` (+0x2c, config `arena.dta` 0.1), `cam_default_duration` (+0x30, 1920), `cam_singer_height` (+0x94, 0), `cam_guard_band` (+0xa4, 4 4), `cam_z_range`, `free_camera`.
- Switch `FUN_0016f3b8`: `cam_paths` `target` (`FUN_0016f160`) / `parent` (`FUN_0016f078`); missing → the focus object. Then the focus is placed at `FUN_0016e080`.
- `FUN_0016e080`: if the focus-character list `DAT_00363784..788` is empty → `walk_spot_02 + (0,0,60)`; otherwise the shot's character index (+0x74: 0 in single player; MULTIPLAYER shots pick 0/1 by `Left`/`Right` in the shot name, −1 = average of all) → `vtbl[0](char+0xb0)`.
  - The list is filled in `ArenaPanel_v13` (0x10c758, line ~8310) with every CharSys character of class `guitarist`, stored as `char+0xb0`.
  - `Character_v0 @0x18d3c0` (Character vtable 0 slot 0, delta −0xb0) returns **`bone_head.mesh` world xfm** (string 0x316ca0); +0x30 = world position. Slot 1 (`0x2b0800` → `FUN_0018e238`) is the character root xfm; slot 5 (`0x18d4d8`) drives the `eyes` look-at.
  - So the focus is the **guitarist's head** (metal bind pose: head z = 59.75 above the root; this is why the "+60" fallback looks almost right on walk_spot_02).
- Per frame (`FUN_0016e390`): head → screen (`FUN_001b1100`) vs. the desired screen position ((sx+1)/2, (1−sy)/2); `t = cam_filter · min(|error|, 1)`; **focus = lerp(focus, head, t)**. So the focus follows the head (soft, frame-rate dependent: 10 % per frame at full error), so shots track the guitarist when he crouches, leans or walks.
- Camera: path TransAnim at the frame (`FUN_001de038`), pre-multiplied by the `shaky_cam1.tnm` matrix when `shaky`; + lerp(offset_in, offset_out, u); × parent world (the full world, including scale for the guitar spots); if `cam_singer_height` ≠ 0 and the parent is the focus, z ≥ root.z + height (0 in data). Look at target keeping the path's up row; shift by `right·(−sx·d/P[0x140]) + up·(sy·d/P[0x164])`; `P[0x140] = 1/tan(fov/2)`, `P[0x164] = −1/(tan(fov/2)·aspect)` (`FUN_001b1eb8`), so +sy = target higher on screen (our sign is right). fov/near/far via `FUN_001b1df8`; `FUN_0019adf0(enable_dof ? 2·dist : 0)` sets the depth-of-field focus.

### 1.3 How the director chooses them [C]

Script (`arena/gen/camera.dta`):
- `one_bar_to` (one bar before a `verse`/`chorus`/`solo` section, only when `$camera.beat > 0`): `$camera.solo = (section == solo)`, `bars_left = random_int(camera_durations[excitement])`, then **`pick_regular_shot`** (always regular, even before a solo).
- `downbeat`: `bars_left -= 1`; **if player0 is not in star mode**, `check_shot`: at `bars_left ≤ 0` → new duration and `pick_shot` = `$camera.solo ? change_to_solo_shot : change_to_regular_shot`. So a solo shot appears only when the shot chosen at `one_bar_to` times out (2–6 bars, `camera_durations`) inside the solo, and never while Star Power is active.
- `beat`: `cam beat_check`; and after an `excitement` message (`about_to_switch`) a re-pick with a new duration.
- `game_over`, `intro_start`: special shots.

Native `CameraSystem` (`CameraSystem_v1 @0x111b20` handlers):
- `make_group` (`FUN_001116c0` → `FUN_00111398`) stores the group flags on each shot; `add_pool` (`FUN_001110b0`) appends each shot of the pool's groups (`FUN_001108f8`/`FUN_00110b90`) to a pool `{weight, shots}`.
- `change_to_regular_shot` = `FUN_001118b8(0x80)`, `change_to_solo_shot` = `FUN_001118b8(0x40)`; `beat_check` (`FUN_00111aa8`): if the current shot is **no longer valid** (`FUN_00110040` ≠ 1) it re-picks at once with `$camera.solo ? 0x40 : 0x80`.
- Pick (`FUN_001117a0` → per pool `FUN_001109b0`, then `FUN_00110410`):
  1. Candidates = shots with `(flags & wanted) == wanted` that pass `FUN_00110128`.
  2. Each shot has a "fresh" bit (+0x10). Fresh valid candidates are used; if none is fresh but some valid ones exist, they are all re-marked fresh. When a whole pool is used up it is refreshed (`FUN_00110cd8`).
  3. Each candidate gets weight `pool.weight / nCandidates`; one weighted random draw over all pools (`FUN_00110410`, `FUN_0024a218` random float).
  4. The chosen shot's fresh bit is cleared and it becomes current (`FUN_00111880`); `eval_shot` runs its `switch_cam` command.
  5. If nothing qualifies: "Couldn't find shot with flags %d", the shot stays.
- Validity `FUN_00110040` (reason codes = strings 0x300558..0x300660): special (0x100) → 2; **walking and `walk_ok 0`** → 3 (`FUN_00197548` on char+0xdc walk object); **current walk spot in `bad_walk_spots`** → 4 (spot = nearest walk spot, `FUN_0018ef80`); **guitarist playing a kGuitarSpecial clip (Star Power move, `FUN_00285888`) and shot lacks kCamNear** → 7 "shot too far for star power"; excitement not Great and `low_excitement_ok 0` → 8.
- Compatibility with the previous shot `FUN_00110128`: same shot → 1; **same side** (both share a set Left/Right bit, `FUN_0010ff60`, first shot compared against the venue's `intro_camera_flags`) → 5 "wrong direction"; distance `FUN_0010ffd0`: prev and new share **Closeup, Far or Behind** (Near→Near is allowed), or the pair is Far↔Behind → 6 "wrong distance".
- Consequences for the close-ups: SOLO_NEAR (Closeup) can never follow SOLO_NEAR; inside a solo the camera alternates SOLO_NEAR / SOLO_FAR / SOLO_FAR…; SOLO_NEAR is invalid while a Star Power special clip plays (it is not kCamNear), so Star Power during a solo forces a SOLO_FAR shot. No weighting by difficulty or note density exists [C: none of these inputs are read].

### 1.4 Why our `venue_camera.cpp` does not show them, and what to add

What the engine does: `upcomingSection("solo")` → `pickSolo()` → uniform over SOLO_NEAR ∪ SOLO_FAR (minus the current name). So the code path exists; the reasons the close-ups don't read as hand shots (or don't come up) are:

1. **Focus is a fixed point.** `VenueCamera::focus()` always returns `walk_spot_02 + 60` (the game's no-character fallback). The game uses the guitarist's animated `bone_head` (filtered by cam_filter). Every `Cam_t_p_*` shot (parent *and* target = focus) is therefore framed on an empty point in space: when the solo clips crouch, kneel or lean (kGuitarSolo / kGuitarExtreme clips move the head a lot), the 45–80-unit close-up frames air or the head instead of the guitar and hands, and once walking exists the guitarist leaves the frame entirely.
   - Fix: give VenueCamera a `focusLookup` returning `guitarist0` `bone_head.mesh` world pos; keep a `focus_` state; on `switchTo` set `focus_ = head`; every frame project the head with the previous frame's camera, `err = |screen − ((sx+1)/2, (1−sy)/2)|` (screen in [0,1], y down), `t = 0.1·min(err,1)`, `focus_ += (head − focus_)·t`; use `focus_` as parent pos (identity rotation) and default target.
2. **Solo-shot timing differs and is not filtered.** The game picks a *regular* shot at `one_bar_to`, then solo shots only on timeouts during the solo; it never picks while in star mode; SOLO_NEAR cannot follow SOLO_NEAR and is rejected during the Star Power special clip. Our `pickSolo` runs at `one_bar_to` (a bar early, so the first "solo" shot frames the end of the previous section), picks uniformly with no side or distance rules (so it can show SOLO_NEAR → SOLO_NEAR or SOLO_FAR → SOLO_FAR repeatedly), keeps switching during Star Power, and uses a fixed seed (`rng_{12345}`), so the same song always gets the same sequence.
   - Fix: port the native picker: per-shot flags from `$camera.groups` (parse arena/gen/camera.dta instead of hard-coding pools), per-shot fresh bits, validity (walk_ok, bad_walk_spots, star-power Near rule, low_excitement_ok), side and distance rules, weight = pool weight / candidate count; `one_bar_to` → regular pick; `downbeat` → `check_shot` unless star mode; `beat` → `beat_check` re-pick when the current shot became invalid. Seed the RNG from time.
3. **Arena/fest circle01 close-ups.** Parent/target resolve through `guitarist0::spot_neck_fret01/20` (works via `Character::find` on the guitar scene). Keep the parent's full world (with the spots' scaled Z rows) as the game does. If the guitarist is missing, the fallback is `arena::stage_spot_01.mesh`. Check that the guitar spot worlds are current at camera time (the camera runs after `guitarist_->poll` in `VenueRenderer::update`, which is the right order).
4. Missing per-shot extras that make the close-ups look like the original: `enable_dof` (depth of field focused at 2·dist; all SOLO_NEAR shots set it), `hide_crowd`, `crowd_region`, `shaky` (`shaky_cam1.tnm` multiplied in), `eyes` (guitarist looks at the camera), `force_char_lod`.

**Verification run** (the current `engine/build/linux/gd01`, `--song smokeonthewater --autoplay --at theatre|arena`, 16 screenshots between 184 s and 219 s, inside the first solo, which runs 182.4–223.7 s): our build *does* show SOLO_NEAR shots during solos. In the theatre, 5 of 8 frames are torso/guitar close-ups (Cam_t_p_close, head above the frame). In the arena, the 194 s frame is the Cam_t_p_circle01 neck shot, with the camera on the neck looking toward the body and the strumming hand. So the claim that we never show them is too strong. What is actually wrong:
- **(a) Frequency.** Solos are short and rare (1–2 per song, about 20 bars). Outside solos the data has no close-up at all, so they only "exist" there. In the original they also appear only in solos, but after the first regular shot times out.
- **(b) Framing.** The focus is a fixed point, so the close-up drifts off the hands whenever the solo clip moves the head.
- **(c) Look.** There's no depth of field, which the original applies on every SOLO_NEAR shot and which is what sells them as close-ups. There's no `hide_crowd`, and no shaky camera.

If the user still never sees them, check that the song actually has `solo` EVENTS (`smokeonthewater`, `spanishcastlemagic` and `evenrats` do).

**What to add (priority order):** the head focus with cam_filter (1), the native picker rules and timing (2), DOF / hide_crowd / shaky / eyes (4). Shot weights need no data changes.

## 2. Walking (full report)
### Guitarist WALK system (SLUS_212.24)

Marks: **[C]** read from code or data. **[I]** inferred.

Conventions used below:
- Positions are world, Z-up, and the crowd is toward −Y.
- Transforms are 3 rows plus pos, applied to row vectors; row1 is "forward".
- Yaw: `yaw(M) = −atan2(M.row1.x, M.row1.y)` (`FUN_0024a670`). A heading toward d is `−atan2(d.x, d.y)`.
- `wrapPi(x)` = `fmod(x+π, 2π) ± π` (`FUN_0027d9b0`).

---

### 1. Objects and fields

### 1.1 CharWalk (`create_walk`) [C]
- RTTI string `Q28CharWalk8WalkClip` (`CharWalk::WalkClip`); the debug strings say `CharWalk:Start`.
- Created by the `create_walk` handler `FUN_0018dc50`, which `Character_v1 @0x18d9f0` dispatches. It allocates 0x70 bytes with ctor `FUN_001967f0` and stores the object at **char+0xdc**. Dtor `FUN_00196938`.
- Only `charbase.dta` (the guitarist base) calls `create_walk`. The band never gets a CharWalk (see §8).

| off | field | notes |
|---|---|---|
| +0x00 | Character* | |
| +0x04 | turn flags | `(turn kWalkTurn)` = 0x20 (dta). Overwritten for each walk by `FUN_002848c0` |
| +0x08 | stop flags | `(stop kWalkStop)` = 0x40 |
| +0x0c | walk flags | `(walk kWalkWalk)` = 0x80 |
| +0x10 | `slop` | default 12.0. Maximum lateral deviation of the steering target from the path |
| +0x14 | `overshoot` | default 0 |
| +0x18 | `undershoot` | default −18.0 |
| +0x1c..+0x28 | `vector<Vector4>` path | [current root pos, waypoints…] |
| +0x2c | destination yaw | `yaw(spot xfm)` |
| +0x30 | clip current when the walk was planned | |
| +0x40..+0x48 | unit direction of the current path segment | `FUN_00199488` |
| +0x50 | speed correction | units per beat, added along the motion direction |
| +0x54 | **state** | 0 idle, 1 planned (waiting for layout), 2 queuing/walking, 3 stop queued/playing |
| +0x58 | number of chain clips already queued (k) | |
| +0x5c | current path segment index | |
| +0x60..+0x6c | `vector<WalkClip>` chain | 12 bytes each: `{clip, startFrame, srcFrame}`. `startFrame` is the first frame in this clip. `srcFrame` is the frame of the previous chain clip at which this clip starts (the transition) |

- `slop`, `overshoot` and `undershoot` can be overridden in the `create_walk` block, but charbase does not do so. [C]
- Methods:

| addr | role |
|---|---|
| `FUN_00196a30` | `walk_to(path)` (CharMan `walk_to` handler `FUN_0018f028`; no script uses it) |
| `FUN_00196c88` | **Start / plan** |
| `FUN_00197c50` | choose turn |
| `FUN_001981c0` | choose walk |
| `FUN_00198570` | choose stop |
| `FUN_001989c8` | layout: number of walk loops and speed correction |
| `FUN_00197388` | per-frame state machine |
| `FUN_001975f0` | per-frame steering |
| `FUN_00197548` | **IsWalking** |
| `FUN_001971e8` | Cancel/Clear |
| `FUN_00199500` | clip is in the chain |
| `FUN_00196588` | angle from a clip name |
| `FUN_00196690` | signed angle between two vectors (asin of the normalised cross z, lengths clamped to at least 5) |

### 1.2 Pose-hold "servo" at char+0xd8 [C]
- 0x30 bytes, ctor `FUN_002b0838`. Poll is `FUN_001958b0`, run from `Character_v13` after the walk poll.
- Fields:
  - +0: target yaw (1e30 = none)
  - +0x10: target pos (1e30 = none)
  - +0x20: position tolerance (0)
  - +0x24: gain (1)
  - +0x28: char
- Cleared by `FUN_002b0890` / `FUN_002b0868`.

### 1.3 GuitarCharacter walk fields (ctor `FUN_00283cf8`, reset `GuitarCharacter_v5 @0x284300`) [C]

| off | meaning |
|---|---|
| +0x108 | state (0 idle, 1 playing, 2 wail, 3 solo, 4 game over) |
| +0x10c | `walks_enabled`, from the char dta (charbase `TRUE`). Cheat `set_walks_enabled` sets it and `walks_enabled` reads it (handlers in `GuitarCharacter_v1 @0x285978`; `toggle_walks` cheat) |
| +0x110 | 0 if the selected character's name is in `no_walk_chars` (empty list) |
| +0x114 | venue `allow_walks` (`DAT_003637b4` = venue dta) |
| +0x118[5] | `walk_delays` (min,max) seconds, **indexed by crowd excitement** 0..4. `FALSE` gives the marker `DAT_0042bae8` (`FUN_00284078`) |
| +0x140[5] | rolled delay per excitement level = `random_float(min,max)` (`FUN_00284618`) |
| +0x154 | time (s, `ms·0.001`) of the last reset of the walk timer. Set to 0 at song reset |
| +0x158 / +0x15c | spot the last walk started from / went to (debug) |
| +0x174 | pre_solo1 counter (>0: random walks go to `solo_walk_point`) |
| +0x178 | pre_solo2 counter (≥1: random walks blocked) |
| +0x17c | venue `solo_walk_point` (−1 if absent) |
| +0x180 | walks are blocked until song tick > this. Initial value −1e30; `solo_off` sets it to now + 1920 ticks (4 beats) |

**Excitement index [C]:**
- `DAT_00363780` is `arena+0x38` (Arena object `0x363748`), written by `FUN_0016a3c8`, the `arena.excitement` setter.
- `FUN_0010d370` maps the crowd rating to 0 (boot) / 1 (bad, <0.33) / 2 (okay) / 3 (great, ≥0.66). Level 4 is Peak (set by script). [I] for 4.
- Result with the data `walk_delays FALSE FALSE (35 55) (20 40) FALSE`:
  - Boot / Bad / Peak: **no random walks**.
  - Okay: one walk every 35–55 s.
  - Great: one walk every 20–40 s.
- Global clocks [C]: `DAT_00363798` = song ms, `DAT_0036379c` = song ticks.

---

### 2. Walk spots [C]

- `FUN_001682f0` → `FUN_00167a38("arena::walk_spot_%02d.mesh", arena+0xf0)`.
- It counts `walk_spot_01, 02, …` until the first missing one. Spot `i` (0-based) = `walk_spot_{i+1}`, stored in `DAT_00363838..DAT_0036383c`.
- Each entry is a 0x40-byte xfm: rows normalised, pos at +0x30 (world pos).
- The same function loads `stage_spot` / `fire_spot` / `fireworks` / `name_lights_*`.
- **The LAST spot is a hub (via point), not a destination.**
  - Every walk path goes current → hub → destination (`FUN_002848c0`).
  - Random destinations are drawn among indices `0..n−2` (`FUN_00284818`).
  - Distance estimates (`FUN_002824a8`, `FUN_002825d0`) are `|a−hub| + |hub−b|`.
  - Lose-teleport (`FUN_00283610`) excludes the hub.
- "Nearest spot" `FUN_0018ef80` (root world pos +0x90 against every spot, **including** the hub) gives the "current spot" used by the camera and by destination selection.

Spot positions (world). Every spot's row1 points to −Y (it faces the crowd, yaw −180°). Venue files: `extracted/venues/<v>/gen/lighting.rnd_ps2` (arena: `arena.rnd_ps2`).

| venue | allow_walks / lose_teleport | idx0 = 01 | idx1 = 02 | idx2 = 03 | idx3 = 04 | hub |
|---|---|---|---|---|---|---|
| basement | F / F | (26.0,108.9,8.6) | (27.8,88.9,8.8) | – | – | 02 |
| small_club | F / F | (36.2,52.0,20.7) | (−6.9,50.5,21.3) | – | – | 02 |
| small_club_multi | F / F | (36.2,52.0,20.7) | (−17.3,50.5,21.3) | – | – | 02 |
| big_club | T / T | (103.6,43.5,0.4) right | (−80.4,40.4,0.4) left | (6.3,58.6,0.4) | – | 03 |
| theatre | T / T | (141.0,460.4,−15.7) | (−86.1,448.1,−15.5) | (24.5,488.6,−15.4) | – | 03 |
| fest | T / T | (146.7,−97.3,−16.5) | (−141.2,−95.9,−16.5) | (−2.3,−27.4,−16.5) | – | 03 |
| arena | T / F, `solo_walk_point 1` | (220.2,9.6,−335.1) right | (−1.7,−382.7,−335.1) **front of catwalk** | (−213.2,9.0,−335.1) left | (−4.3,−120.1,−335.1) | 04 |

**Start spot [C]** (`FUN_00281cd8`, MyCharSys `+0x68`, initialised to 0 at `0x280e18` / `MyCharSys_v10`):
- Single player starts on **index 0 = walk_spot_01**. In big_club that is the stage-right spot, x = +103.
- Exception: if the first solo's `pre_solo2` falls before tick 3840 (8 beats), the start spot is `solo_walk_point` (`FUN_00281fb8`).
- In 2-player games, player k (1-based) stands on spot `k % 2`: P1 on walk_spot_02, P2 on walk_spot_01.
- ⇒ The engine's fixed `walk_spot_02` is the P1 spot of a **multiplayer** game. For single player it should be walk_spot_01. In the arena, walk_spot_02 is the far end of the catwalk.

---

### 3. When a walk starts

### 3.1 Random walks (`GuitarCharacter_v13 @0x284390`, each frame, before the anim/walk poll) [C]

```
if no blend in progress (FUN_0017fc00: driver head has no prev):
    if state == 4: skip
    elif WalkDue() (FUN_00284798):
        r = randint[1, n-1)            // FUN_0024a1d0, n = spot count incl. hub
        WalkStep(r)                    // FUN_00284818
    else: queue next mood clip (FUN_002855f8)
...poll (Character_v13 → walk poll)...
if walk state went from {1,2} to {0,3} this frame: ResetTimer()   // FUN_00284618
```

`WalkAllowed` (`FUN_00284690`) = `walks_enabled && venue allow_walks && char not in no_walk_chars`.

`CanRandomWalk` (`FUN_002846b8`) requires all of:
- `WalkAllowed`
- walk state == 0
- guitarist state == **1 (playing)**. Idle, wail, solo and game over block it.
- pre_solo2 counter < 1
- `walk_delays[excitement]` is not FALSE
- `+0x180 < songTicks`

`WalkDue` = `CanRandomWalk && delay[excitement] < now_s − lastReset_s`.

`ResetTimer` sets `lastReset = now_s` and re-rolls all 5 delays. It runs:
- at song reset (then `lastReset` = 0)
- whenever a walk reaches the stop clip or is cancelled.

`WalkStep(r)` (`FUN_00284818`; also used by cheats `walk_prev` with r = −1 and `walk_next` with r = +1):
```
cur  = nearest spot (incl. hub)
dest = (cur + r) mod (n-1)  (made non-negative)
if solo_walk_point >= 0 and pre_solo1 counter > 0: dest = solo_walk_point
StartWalkTo(dest, mode = kAnimLongest 4)        // FUN_002848c0
```
- Destination is always a spot other than the current one.
- With 3 spots (big_club, theatre, fest) it alternates 01 ↔ 02.
- With 4 spots (arena) it picks one of the two other spots at random.

`StartWalkTo` (`FUN_002848c0`):
- Does nothing unless `WalkAllowed`, walk state ∈ {0,3} and `dest != nearest`.
- Otherwise it builds the path `[hub, dest]` and sets the flags (§5.1).
- Then it calls `CharWalk::Start(destYaw, path, mode)`. Start prepends the current root position.

### 3.2 Solos (`FUN_00281fb8`, run once at song setup; only if players < 2 and venue allow_walks) [C]

It inserts `pre_solo1` / `pre_solo2` events into the song's EVENTS track (`FUN_00268f60(track, tick, name)`). The guitarist handles them itself.

With:
- `bpi = solo.walk_beats_per_inch` (0.069)
- `endB = walk_end_beats` (3)
- `maxW = walk_max_wait` (`MAX_WALK_WAIT` = 6)

Define:
- `Wmax = maxPath·bpi + endB`, where `maxPath = max over pairs i<j (non-hub) of |i−hub|+|hub−j|` (`FUN_002824a8`).
- `Wsolo = soloPath·bpi + endB + maxW`, where `soloPath = |solo−hub| + max over i≠solo of |i−hub|` (`FUN_002825d0`).

Events inserted, for every `solo_on` event tick T:
- If `solo_walk_point < 0`: `pre_solo2` at `T − Wmax·480`.
- Otherwise:
  - `pre_solo1` at `T − (Wsolo+Wmax)·480`
  - `pre_solo2` at `T − Wsolo·480`
  - if that tick < 3840, start on the solo spot (§2).
- Also `pre_solo2` at `lastEvent.tick − Wmax·480`, i.e. before the end of the song.

Example lead times: big_club Wmax = 15.9 beats, theatre 19.4, fest 25.0, arena 39.0. In the arena, pre_solo2 is 45 beats and pre_solo1 84 beats before each solo.

**Meaning.** `walk_beats_per_inch` and `walk_end_beats` are only a **duration estimate** for scheduling: beats per world unit, plus beats for the stop. They do not scale clip speed. `walk_max_wait` = the maximum beats before the turn transition can happen (the graph criterion `max_dist MAX_WALK_WAIT` on idle/normal/extreme → kWalkTurn edges in `hero_graphs.dta`). [C for use, I for the "estimate" wording]

Handlers (`GuitarCharacter_v1`) [C]:
- `pre_solo1` (`FUN_00285ee8`):
  - `+0x174++`.
  - On the first one: if a walk is planned but the character is not yet moving (state ∉ {0,3} and !IsWalking), cancel it.
  - Random walks from now on go to `solo_walk_point`.
- `pre_solo2` (`FUN_00285f70`):
  - `+0x178++`, which blocks random walks.
  - On the first one: cancel a not-yet-moving planned walk.
  - If `solo_walk_point >= 0`: `StartWalkTo(solo_walk_point, mode kAnimFirst 3)`. This is ignored if already walking (state 2) or already there.
- `set_solo off` (`FUN_00284f60`): only if the state was 3. Then state = 1, `+0x174--`, `+0x178--`, `+0x180 = ticks + 1920`, i.e. no random walk for 4 beats.
  - Quirk: in venues without a solo point, `+0x174` goes negative. This is harmless.
- `set_wail` / `set_solo on` move the state off 1, which stops random walks. A running walk is not interrupted.

So:
- Venues without a solo point: walks are frozen from `Wmax` beats before each solo until 4 beats after it, and also near the end of the song.
- Arena: the guitarist ends up on the catwalk front (walk_spot_02) for every solo.

### 3.3 What blocks or cancels [C]
- Game over (`set_game_over`, `FUN_00284fd8`) cancels the walk (`FUN_001971e8`) and sets state 4.
- Multiplayer: `MyCharSys_v10 @0x280fc0` clears the "enabled" bit 0x80000000 on every clip with flags & 0x3fe0 (all kWalk*). The same happens if the venue has `allow_walks FALSE`.
- Star power: there is no explicit check.
  - `start_starpower` plays kGuitarSpecial with kAnimFirst, which competes with the chain. [I] It can replace a queued walk clip; Start then fails the "current clip == +0x30" check and cancels.
  - The mood replays are skipped while the walk state is 1/2 (`FUN_002855f8` / `FUN_002856a0` return early). So `upcoming_*` mood changes do not interrupt walks.
- Camera: nothing blocks walks. The camera only rejects shots (§9).

---

### 4. Planning (`CharWalk::Start` `FUN_00196c88`) [C]

1. `path = [rootPos] + points`. With +0x2c = dest yaw.
2. **Turn** (`FUN_00197c50`):
   - `cur` = the newest driver that has started blending (`FUN_00180670`).
   - For each clip entry with `(turnFlags & entry.flags) == turnFlags` and a loaded clip:
     - `tr = FUN_0017f1d0(cur.frame, set, cur.clip, cand, mode)`. This is the graph edge for First/Longest, or the computed fallback; it is never null for modes 3/4.
     - Predicted start pose `(P, θ)`: the char+0xd8 hold targets if set; otherwise the root pose advanced by integrating `cur.clip` root motion from `cur.frame` to `tr.src` (`FUN_001883d8`).
     - `heading = −atan2(d.x, d.y)` with `d = path[1] − P` (toward the hub).
     - `a = angle(name after "turn_")` (`FUN_00196588`): `forward` → 0, `backward` → 180°, `left_N` → +N°, `right_N` → −N°.
     - `err = |wrapPi(wrapPi(θ + a) − heading)|`.
   - Keep the entry with the smallest err **< 45°**. Strict `<`, so ties go to the first in dta order. The tempo / enable bit is **not** checked.
   - Push `{turn, tr.dest, tr.src}` and set `path[0] = P` (the predicted pose where the turn starts).
   - Fail if none is found.
3. **Walk** (`FUN_001981c0`):
   - Candidates: entries with the enable bit 0x80000000 (song tempo match; `MyCharSys_v10` → `FUN_0017e138(set, tempo, 3, 1)`), `(walkFlags & flags) == walkFlags`, and an explicit `.acg` Longest edge turn→walk (`FUN_0017efe0`, no fallback).
   - Pick the **LRU** one (smallest entry+0x18) and stamp it (`FUN_0017e7e0`).
   - Push `{walk, edge.dest, edge.src}`. If none, fail.
4. **Stop** (`FUN_00198570`):
   - `h = atan2(dx,dy)` of the last path segment `path[n−1]−path[n−2]`.
   - `need = wrapPi(destYaw + h)`, which is `destYaw − walkingHeading`.
   - Among entries with `(stopFlags & flags) == stopFlags` (no tempo check), take the min `|wrapPi(need − angle(name after "stop_"))|` (no limit).
   - Push `{stop, floor(stop.end − 2.2 − stop.blend_width/2), ?}`. `.src` is filled by the layout.
5. If all three succeed:
   - addref the clips;
   - `+0x30 = current clip`;
   - **state = 1**;
   - call Layout immediately.
6. On any failure the walk is cleared and nothing happens.

### 4.1 Layout (`FUN_001989c8`, run while state = 1) [C, details of the error sign I]

1. **Preconditions.** Current clip == +0x30, `cur.frame ≤ chain[0].src` (the turn transition has not passed yet), and all chain clips are loaded. Otherwise cancel.
2. **Walk loop edge.** `loop = FUN_0017f1d0(…, walk, walk, Longest)`, the self-edge, giving `{loopDest, loopSrc}`.
3. **Start of motion.**
   - `seg = 1`, `dir = normalise(path[1]−path[0])`.
   - Clear the 0xd8 hold.
   - **bone servo `use_delta` = 1** (`FUN_00182d80(char+0x90, 1)`).
4. **Required distance L.**
   - S = the position where the stop clip must start. It is found by integrating the stop clip's root motion **backward** from its end, placed at `path[last]` with `destYaw`, to its start frame.
   - `L = Σ |path[i]−path[i+1]|` up to `path[n−2]`, plus `|path[n−2] − S|`.
5. **Simulate in 2-beat steps.** Start at the turn's start frame (`ceil`, parity-matched to the stop's start frame so the beat phase is kept for align-2 clips).
   - Integrate the root-motion chord of each step through turn → walk → walk loops.
   - When a step crosses a transition frame, continue in the next clip at its start frame.
   - Insert extra `{walk, loopDest, loopSrc}` entries before the stop as needed.
   - Subtract each step's displacement length from L.
   - Stop when the remaining distance `rem ≤ −undershoot` (18).
6. **Choose the step end.**
   - Default: this step.
   - Alternative: the previous step (rem_prev > 18). It is used only if this step overshoots by more than 18 (`−rem > 18`) **and** `(rem_prev − 18) < (−rem − overshoot)`.
   - Trim surplus loop entries.
   - `stop.src` = the chosen frame in the last walk clip.
7. **Speed correction.** state = 2, k = 0. If the residual error (outside the [−undershoot, overshoot] band) > 0: `+0x50 = ±err / totalBeats`, where `totalBeats = Σ_i (exitFrame_i − startFrame_i)` over the chain (exit of the last = stop end). The leftover is also pushed onto the last path point along the last segment.

Net effect:
- The number of walk cycles is quantised to whole beats (2-beat steps).
- The remaining few units are absorbed by the speed correction plus the steering target.
- The clips are **not** time-scaled.

### 4.2 Root-motion integration `FUN_001883d8(f0, f1, step, clip, &pos, &yaw, vel*)` [C]
- Samples `bone_facing.pos` and `bone_facing.rotz` at f0 and f1 (one chord, no sub-steps).
- `pos += rotZ(yaw − rotz(f0)) · (P(f1) − P(f0))`, then `yaw = wrapPi(yaw + wrapPi(rotz(f1) − rotz(f0)))`.
- f1 < f0 gives backward integration.
- This is the same maths as the bone servo's per-frame `use_delta` path (character_animation.md §1.4), so the planning matches what the servo will do.

---

### 5. Clip flags per walk (`FUN_002848c0`) [C]

### 5.1 Direction
- `d = hub − charPos`, expressed in the **destination spot's** frame: `x = d·row0` (character's right when facing the crowd is +), `y = d·row1` (toward the crowd is +).

| case | turn flags | walk flags | stop flags |
|---|---|---|---|
| lateral, `|y| < 2|x|`, side = x>0 ? Right(0x200) : Left(0x100) | `side|kWalkTurn` | `side|kWalkWalk`; with probability 0.25 (`random_float < 0.25`) `kWalkForward|kWalkWalk` instead | `kWalkStop` (any direction) |
| longitudinal, y>0 → Forward(0x400) | `kWalkTurn` (any) | `Forward|Walk` | `Forward|Stop` |
| longitudinal, y≤0 → Backward(0x800) | `kWalkTurn|kWalkBackward` | `Backward|Walk` | `Backward|Stop` |

- Then `walk |= excitement < 3 ? kWalkNormal(0x1000) : kWalkExtreme(0x2000)`.
  - Clip names: `walk_normal_<dir>_<tempo>` vs `walk_<dir>_<tempo>` (extreme).
  - Since random walks happen only at Okay/Great, Extreme means Great. Solo walks can be Extreme at Peak.
- Within the flag set, the turn **angle** (45/90/135/180, forward = 0, backward = 180) and the stop angle are chosen by the geometric best fit (§4), not by the flags.
- Tempo: walk clips are filtered by the song's `anim_tempo` (enable bit). Turn and stop clips are not; on equal angle error, the first in file order wins (`*_fast` is listed before `*_medium`). [C]
- Graph (`HERO_GRAPH` in `hero_graphs.dta`):
  - edges: (Normal, Extreme, Idle) → Turn (`min_dist 4`, `max_dist 6`, `end_dist 2`, `b_start < 1.2`), Turn → Walk, Walk → self (loop), and moods / Special / Win / Lose from Stop;
  - exclusions: `(Left Right Backward)` and `(Forward Backward)`.

### 5.2 Example: big_club, from spot 01 (x = +103) to 02 (x = −80) via the hub (6, 58)
- d = (−97, +15); in the spot frame (yaw 180°: row0 = (−1,0,0), row1 = (0,−1,0)) that is x = +97, y = −15 → lateral, **Right**.
- Turn: facing −Y, heading to the hub ≈ +81° → need −99° → `turn_right_90`.
- Walk: `walk(_normal)_right` (or 25% forward).
- At the hub the steering bends the path.
- Stop: walking heading +90° to destYaw −180° → need +90° → `stop_left_90`.

[I] The walk_left/right clips move along the root's facing while the body faces the crowd; the stop clip rotates the facing back to the crowd.

---

### 6. Execution

### 6.1 Per-frame state machine (`FUN_00197388`, called from `Character_v13` after the anim poll, so after the servos have applied this frame's root delta) [C]

```
state 1: if !Layout(): return (cancelled)          // then falls into state 2
state 2: cur = newest started driver
         if (no driver newer than cur is pending, FUN_00180630 == 0) or k == 0:
             if k < chain.size():
                 e = chain[k]
                 queue ClipDriver(e.clip, frame = e.startFrame, delay = e.srcFrame − cur.frame)  // FUN_0017fd18
                 k++
             else state = 3
         Steer()
state 3: if currentClip != chain.back().clip:     // a mood clip has taken over from the stop
             Clear()
             hold.pos = root pos; hold.yaw = destYaw  // char+0xd8 targets
         else Steer()
```

- The chain clips are queued one at a time. Each is queued as soon as the previous one has started blending, and waits on its source frame.
- After the stop clip has blended in, `GuitarCharacter_v13` (no blend in progress, state 3 allowed) queues the next mood clip with kAnimLongest, through the graph Stop → mood edges. When that clip becomes current, the walk ends.

### 6.2 IsWalking `FUN_00197548` [C]
- `state ≥ 2` and `currentClip == chain[min(k−2, len−1)].clip`.
- That is true from the moment the turn becomes the playing clip until the stop clip starts blending. It is false while the turn is still pending, and false during the stop clip.

### 6.3 Steering `FUN_001975f0` (only when dBeat ≠ 0 and IsWalking) [C]

1. Clear the 0xd8 hold. Read the root pose `P`, `θ`.
2. `pred` = the root pose predicted **1 beat ahead** by integrating the chain from the current frame. It can cross into later chain clips.
   - If that runs past the end of the last clip, `frac = 1 − excess`; otherwise `frac = 1`.
3. **Target T:**
   - On the last segment (`seg == npts−1`): T = the "ideal" position at now+1 beat. Start from `path[last]` with `destYaw` and integrate the stop clip, then the queued walk loops, **backward** to the frame reached 1 beat from now.
   - Otherwise: `T = path[seg]`. If `(pred − path[seg])·dir ≥ 0`, then `seg++` and `dir` is updated.
4. **Clamp to the corridor.**
   - `foot` = the projection of T onto the segment line through `path[seg−1]`, `e = T − foot`.
   - If `|e| > slop` (12): `foot += dir·(|e| − slop)` and `T = foot + e·slop/|e|`.
5. `α = asin(cross_z(T−P, pred−P) / (max(|T−P|,5)·max(|pred−P|,5)))`.
6. If `corr ≠ 0`: `P += (pred−P)·(dBeat·corr/|pred−P|)`, i.e. `corr·dBeat` units along the motion direction.
7. `rootYaw += −α / max(frac, 0.25) · dBeat` (`FUN_0024af18`), so the root turns toward T within about one beat.

### 6.4 Root motion
- The actual translation and rotation come from the walk/turn/stop clips' `bone_facing` deltas through the bone servo `use_delta` (character_animation.md §1.4). Idle/play clips have a constant `bone_facing`, so Δ = 0.
- CharWalk only adds the steering yaw and the small `corr` displacement.
- **There is no snap at arrival** [C]. The character stops wherever the clips put it, usually within a few units of the spot.

### 6.5 End facing and hold (`FUN_001958b0`, char+0xd8) [C]

When the walk ends: `hold.yaw = destYaw` (the spot faces the crowd), `hold.pos = current root pos`.

On every later frame where a queued clip is still waiting on its delay (`delay ≥ 0`):
1. Predict the root pose at the moment that clip starts.
2. `rate = dBeat·1.1 / max(delay, clip.blend_width)`.
3. If `|hold.pos − predPos| > tol(0)`: `rootPos += (hold.pos − predPos)·rate·gain`.
4. `rootYaw += wrapPi(hold.yaw − predYaw)·rate`.

So the guitarist eases back to face the crowd exactly at the next clip transitions. This runs only when `use_delta` is set. Start/Cancel clear the hold.

---

### 7. Game over and lose_teleport [C]

**`set_game_over` (`FUN_00284fd8`)**:
- won: `FUN_00282818` (final win) → kGuitarWinFinal, else kGuitarWin, with mode kAnimFirst;
- lost → kGuitarLose with mode **kAnimReset**;
- lost **and IsWalking** → **kGuitarLoseWalk** (0x8000000) with kAnimReset. That flag is only on `<arch>_lose_01` (e.g. `metal_lose_01`), a realtime clip.

Then cancel the walk, play the same flags on `wings.drv` (Grim), and set state = 4. Once a WinFinal/Win/Lose/LoseWalk clip reaches its end, `GuitarCharacter_v13` sends `game_outro` to the arena.

**lose_teleport** (`arena_game.dta`: on `pre_switch_cam`, if `{arena cam_msg} == "lose01"` → `{char_sys lose_teleport}`; handler `FUN_00283610`):
1. Only if the venue has `(lose_teleport TRUE)` (big_club, theatre, fest).
2. Find the `singer` (else `keyboard`) character and take its `bone_pelvis.mesh` world xy, with the guitarist root z.
3. If the guitarist root is **within 72 units** of it:
   - find the nearest walk spot (hub excluded);
   - set the guitarist's root transform to that spot's xfm (`FUN_0018f060` → vtable+0x44). This snaps position and facing.

[I] It moves a guitarist who lost while walking past or into the singer before the lose camera shot.

---

### 8. Band members [C]
- `bandbase.dta`, `theband.dta` and `band_chars.dta` contain no `create_walk` and no walk clips, so the band never walks.
- CharMan `walk_to <char> <spot> <mode>` (`CharMan_v1` → `FUN_0018f028`) and `teleport` (→ `FUN_0018f060`) exist, but no script uses them.
- Band and guitarist placement come from stage_spot / walk_spot transforms set once in `FUN_00281cd8`.

---

### 9. Camera interaction [C] (`FUN_00110040`, shot validity; reason strings at 0x300558…)

| reason | condition |
|---|---|
| 2 "shot is special" | shot flags & 0x100 |
| 3 "shot not okay during walk" | guitarist0 IsWalking (`FUN_0010feb0` → `FUN_00197548`) and the shot has `(walk_ok 0)`. `walk_ok` defaults to 1 if absent (`FUN_0010fd30`) |
| 4 "shot not okay with current walk spot" | the shot's `(bad_walk_spots i j …)` contains `FUN_0018ef80` = **0-based index of the walk spot nearest to the guitarist, hub included** (`FUN_0010fd80`). It is evaluated continuously, so mid-walk the "current spot" changes as the guitarist passes the hub |
| 7 "shot too far for star power" | Special clip weight > 0 (`FUN_00285888`) and the shot lacks flag 8 |
| 8 "excitement too low" | `low_excitement_ok` check |

Data:

| venue | shots with walk_ok 0 | bad_walk_spots |
|---|---|---|
| big_club | 15 | (0) ×8, (1) ×9 |
| theatre | 7 | (0) ×5, (1) ×6 |
| fest | 21 | (0) ×10, (1) ×11 |
| arena | 8 | (0 1) ×3, (0 2) ×5, (1 2) ×3, (2) ×1 |

- Index 0 = walk_spot_01 etc., the same indexing as §2.
- No shot uses the hub index (2 in 3-spot venues, 3 in the arena). Shots framing one side are banned when the guitarist stands on the other side.

---

### 10. Implementable algorithm for gd01

Engine today:
- `VenueRenderer::update` sets `guitarist_->setPlacement(walk_spot_02)` every frame.
- `Character` has no root motion. The placement is the root world xfm; `compose(local, placement_)`.

Changes needed (do not edit here):
1. Character API additions:
   - `rootFacingAt(clip, frame) → {vec3 pos, float rotz}`: sample the `bone_facing.pos` / `.rotz` channels; `AnimClip::sample` already gives every channel.
   - `currentDriver()`: the newest chain entry with blend > 0, giving its clip and frame.
   - `hasPending()`: is there an entry newer than the current one.
   - `queue(clip, frame, delay)`: the direct ctor, like `playAt` but with an explicit frame/delay and blend 0.
   - `transition(fromClip, fromFrame, toClip, mode)`: the existing `start()` logic exposed, returning `{src, dest}`.
   - `clipsWith(flags, requireTempo)` plus `clipName(i)`, `clipEnd/Start/blendWidth(i)`, LRU `touch(i)`.
   - Per-frame root-delta output: for each started driver, `w·Δ` of `bone_facing` between the previous and current frame, as in §1.4: `Δθ` and `Δp` rotated by −θ(t), weighted with the blend weights.
2. A `GuitarWalker` owned by `VenueRenderer` that holds `Xfm root_` and passes it to `setPlacement`.

```cpp
// ---- data ----
spots = venue walk_spot_01..NN (normalised xfm); hub = spots.back(); n = spots.size()
allow  = venues.dta allow_walks && players == 1;  soloPt = venues.dta solo_walk_point (-1)
delays = {none, none, {35,55}, {20,40}, none}     // charbase walk_delays, by excitement 0..4
bpi=0.069, endB=3, maxWait=6, slop=12, over=0, under=-18
root = spots[startIdx]   // 0 for single player (or soloPt if first solo is very early), not 02

// ---- song setup (FUN_00281fb8) ----
if (allow) {
  Wmax  = maxPair(|i-hub|+|hub-j|)*bpi + endB
  Wsolo = soloPt<0 ? 0 : (|solo-hub| + max_{i!=solo}|i-hub|)*bpi + endB + maxWait
  for each solo_on tick T:
     if soloPt<0: addEvent(T - Wmax*480, PRE_SOLO2)
     else { addEvent(T-(Wsolo+Wmax)*480, PRE_SOLO1); addEvent(T-Wsolo*480, PRE_SOLO2) }
  addEvent(lastEventTick - Wmax*480, PRE_SOLO2)
}
lastReset = 0; roll(); pre1 = pre2 = 0; noWalkUntilTick = -inf

// ---- events ----
PRE_SOLO1: if (++pre1==1 && planned && !isWalking()) cancel();
PRE_SOLO2: if (++pre2==1) { if (planned && !isWalking()) cancel(); if (soloPt>=0) startWalkTo(soloPt, kAnimFirst); }
solo_off (state was 3): --pre1; --pre2; noWalkUntilTick = ticks + 1920;
game over: lost && isWalking() ? play(kGuitarLoseWalk, Reset) : ...; cancel();
lose01 camera cut (lose_teleport venues): if |root.xy - singerPelvis.xy| <= 72: root = nearest non-hub spot xfm

// ---- each frame, BEFORE character poll (GuitarCharacter_v13 order) ----
if (!guitarist.blending()) {
  if (gstate == 4) ;
  else if (allow && walk.state==0 && gstate==1 && pre2<1 && delays[exc] && noWalkUntilTick < ticks
           && delay[exc] < now_s - lastReset) {
      int cur = nearestSpot(root.pos /*incl hub*/);
      int dest = mod(cur + randint(1, n-1), n-1);
      if (soloPt>=0 && pre1>0) dest = soloPt;
      startWalkTo(dest, kAnimLongest);
  } else if (walk.state==0 || walk.state==3) queueMood(kAnimLongest);   // existing code
}
int before = walk.state;
guitarist.poll(...);                 // applies clip root deltas to root (use_delta) -> root
walkPoll();                          // state machine + steering on root
holdPoll();                          // ease yaw to destYaw (and pos) at next transitions
if ((before==1||before==2) && (walk.state==0||walk.state==3)) { lastReset = now_s; roll(); }

// ---- startWalkTo(dest, mode) ----
if (!allow || !(walk.state==0||walk.state==3) || dest==nearestSpot(root.pos)) return;
D = spots[dest]; d = hub.pos - root.pos; x = dot(d,D.row0); y = dot(d,D.row1);
if (fabs(y) < fabs(2*x)) { side = x>0? Right:Left; turnF = side|Turn;
     walkF = rand01()<0.25 ? Forward|Walk : side|Walk; stopF = Stop; }
else { dir = y>0? Forward:Backward; turnF = dir==Backward? Turn|Backward : Turn;
     walkF = dir|Walk; stopF = dir|Stop; }
walkF |= exc<3 ? Normal : Extreme;
path = {root.pos, hub.pos, D.pos}; destYaw = yaw(D);
// plan (§4): turn = argmin_{flags⊇turnF} |wrap(predYaw + nameAngle - heading(path[1]-predPos))| < 45deg
//            path[0] = predPos (pose integrated through current clip to tr.src)
//            walk = LRU_{flags⊇walkF, tempo ok, has acg edge turn->walk}
//            stop = argmin_{flags⊇stopF} |wrap(destYaw - heading(lastSeg) - nameAngle)|
// layout (§4.1): S = backward-integrate stop from (D.pos,destYaw); L = pathLen to S;
//            step 2 beats through turn/walk loops subtracting chord lengths until L<=18;
//            pick this/previous step per §4.1; chain = [turn, walk xK, stop]; corr = residual/totalBeats
walk.state = 2; k = 0; seg = 1; hold.clear();

// ---- walkPoll() ----
// state 2: if (!guitarist.hasPending() || k==0) { if (k<chain.size()) { e=chain[k++];
//          guitarist.queue(e.clip, e.start, e.src - guitarist.currentDriver().frame); } else state=3; }
//          steer();
// state 3: if (currentClip != chain.back().clip) { state=0; hold = {root.pos, destYaw}; } else steer();
// steer(): §6.3 (pred = root advanced 1 beat via chain root motion; waypoint switch;
//          slop clamp; alpha; root.pos += corr*dBeat along motion; root.yaw -= alpha/max(frac,.25)*dBeat)
```

Simplifications that keep the look:
- Use the chain with a fixed number of walk loops: `K = round((L − 18) / loopDistance)` with `loopDistance = |P(loopSrc)−P(loopDest)|` of the walk clip.
- Do the per-frame steering only toward `path[seg]`. Skip the backward-integrated ideal target, but keep the 12-unit corridor.
- Optionally, after the stop clip, lerp the root yaw to `destYaw` over the next clip's blend.

**Fix regardless of walking:** single-player start spot = `walk_spot_01` (index 0), not 02. In the arena, 02 is the solo/catwalk spot.

---

### Addresses quick list
- **CharWalk:**
  - ctor `0x1967f0`
  - start `0x196c88`
  - turn `0x197c50`
  - walk `0x1981c0`
  - stop `0x198570`
  - layout `0x1989c8`
  - poll `0x197388`
  - steer `0x1975f0`
  - isWalking `0x197548`
  - clear `0x1971e8`
  - segDir `0x199488`
  - nameAngle `0x196588`
  - integrate `0x1883d8`
- **Hold servo** `0x1958b0`.
- **CharMan:** nearest spot `0x18ef80`, walk_to `0x18f028`, teleport `0x18f060`.
- **GuitarCharacter:**
  - ctor `0x283cf8`
  - walk_delays parse `0x284078`
  - v5 reset `0x284300`
  - v13 `0x284390`
  - resetTimer `0x284618`
  - allowed `0x284690`
  - canRandom `0x2846b8`
  - due `0x284798`
  - step `0x284818`
  - startWalkTo `0x2848c0`
  - set_solo `0x284f60`
  - set_game_over `0x284fd8`
  - pre_solo1 `0x285ee8`
  - pre_solo2 `0x285f70`
  - handlers `0x285978`
- **MyCharSys:**
  - solo scheduling `0x281fb8`
  - maxPath `0x2824a8`
  - soloPath `0x2825d0`
  - placement `0x281cd8`
  - clip enable `0x280fc0`
  - lose_teleport `0x283610`
- **Spots** loader `0x1682f0` / `0x167a38`.
- **Camera** shot check `0x110040` (`walk_ok` `0x10fd30`, `bad_walk_spots` `0x10fd80`).

**Engine note [C]:** `engine/src/scene/character.cpp:170` skips any clip with `flags & (kWalkMask 0x3fe0 | kGuitarUI)`, so no turn, walk or stop clip is loaded today. Walking needs these clips loaded, but kept out of the mood selection; select them only through the walker.

## 3. Crowd (full report)
### Crowd: why ours looks thinner and does not react (GH1 PS2, SLUS_212.24)

Labels: **[C]** confirmed in code or data. **[I]** inference.
This builds on `docs/crowd.md` (impostor, set_sizes and region mechanics) and does not repeat it.

### 0. TL;DR (root causes, most important first)

1. **Engine bug: only the BAD/BOO clips are ever loaded for the crowd.** [C]
   - `Character::Character` (engine/src/scene/character.cpp:170) drops every clip with `flags & (kWalkMask | kGuitarUI)`, where `kWalkMask = 0x3fe0` (bits 5..13).
   - The crowd flags share those bits: C_OK 32, C_GOOD 64, C_GREAT 128, C_CLAP 256, C_JUMP 512, C_CHEER 1024.
   - What survives: only `*_bad` and `*_boo`, which are `C_<gender>|C_BAD` (16 + 1/2/4).
   - What gets rejected: idle, ok, 1armpump, 2armpump, clap, clap_high and jump. (`*_idle` is C_OK|C_IDLE, so it is rejected too.)
   - `select(kOk/kGreat)` then finds nothing and walks the guitar/band fallbacks:
     - `kBandActive = 4` happens to equal C_FEMALE, so females pick bad/boo.
     - Males fall through to `c = 0`, which is their first loaded clip, `*_bad`.
   - **Net effect: at every excitement level the whole crowd slumps or boos.** That explains "does not react". The slumped poses also make the impostors look smaller and thinner. **[I]**
2. **The full 3D crowd members (crowd_limits regions) are missing.** These are the 10 to 17 closest, most visible people. See section 2.
3. **Wrong timing and semantics for sizes and animation.**
   - The game re-runs `update_crowd` (set_sizes + animate) **on every regular or solo shot change**, not when excitement changes.
   - It starts with the **full crowd** (`init`: 1 1) during the intro. The engine applies the Okay size (0.5) from frame 0.
   - It honours `reduced_crowd_shots`, `hide_crowd` and the theatre's per-shot Crowd02/05 hiding.
4. Smaller items: outfit material swaps, `crowd_*_tempo`, the shared LRU and random phase, and the fest crowd-frame rotation M.

### 1. Crowd sizes

### Script [C] (`arena/gen/crowd.dta`, `arena/gen/camera.dta`)

| When | full | flat | animate_crowd (bad, ok, great, idle) |
|---|---|---|---|
| arena `init` (before intro) | 1 | 1 | (0, 0.9, 0.1, 0) |
| kExcitementBoot (0) | 0.1 | 0.1 | (1, 0, 0, 0) |
| kExcitementBad (1) | 0.25 | 0.25 | (0.8, 0.2, 0, 0) |
| kExcitementOkay (2) | 0.5 | 0.5 | (0, 0.8, 0.2, 0) |
| kExcitementGreat (3) | 1 | 1 | (0, 0, 1, 0) |
| kExcitementPeak (4, unused in GH1) | 1 | 1 | (0, 0, 1, 0) |
| `$no_crowd` cheat | 0 | 0 | (no animate) |

- **Who calls `update_crowd`:** only `pick_regular_shot` and `pick_solo_shot` (camera.dta:177-187).
  - Those run from `check_shot` (downbeat, when bars_left ≤ 0, and **not during star power**), from the `beat` handler after an `excitement` message (about_to_switch, at the end of the bar), and from `one_bar_to` (section change).
  - Intro, win, lose and singer shots use `eval_shot` and **do not** update the crowd.
  - So from `init` until the first regular shot (intro bars_left = 6), the crowd is **100 % with mostly OK clips**. An excitement change only shows on the next shot pick, about one bar later.
- **`set_crowd_sizes`:** if `venues.<venue>.reduced_crowd_shots` lists the current `cam_msg` and flat > that value, flat becomes the value. **full is unchanged.**
  - big_club: flr_far_lft02 0.8, flr_far_lft03 0.9, flr_far_rt02 0.9.
  - fest: flr_far_lft03 0.8, flr_far_lft05 0.85, flr_far_rt01x2w 0.9, flr_far_rt03 0.8.
  - Other venues have none.
- **The arena handlers `(excitement)(game_over)(chorus)(verse)` in crowd.dta are empty.** Star power has **no** crowd handler.
  - `config/player.dta` `star_power_handlers start_using` only plays the sfx `crowd_upto_good`.
  - arena_game.dta `beat` plays the `claps` **sfx** sequence each beat (+440 ticks) while any player is in star mode. Nothing visual. [C]
  - Star power affects the crowd only indirectly: the rock meter rises, excitement reaches Great, and the next shot pick applies (1, 1) and C_GREAT. During star power, `check_shot` is skipped on downbeats, but an excitement change still forces a pick through `beat`.
- **`game_won` / `game_lost`** play only the sfx `crowd_win` / `crowd_lose`. The crowd keeps its last state. [C]

### Flat instances visible (per venue; `(int)(count·flat)` per block, summed) [C counts from the scenes]

Only 5 blocks (Crowd01..05.mm) exist in every venue, so archetype 5 (block 6) is never an impostor.

| venue (section) | per-block counts | total | 0.1 | 0.25 | 0.5 | 1.0 |
|---|---|---|---|---|---|---|
| arena (lighting) | 132/83/92/96/83 | 486 | 47 | 120 | 242 | 486 |
| basement (crowd) | 16/18/15/20/23 | 92 | 7 | 21 | 45 | 92 |
| big_club (lighting) | 92/102/96/82/51 | 423 | 41 | 104 | 211 | 423 |
| fest (crowd) | 133/104/123/111/127 | 598 | 58 | 147 | 297 | 598 |
| small_club (lighting) | 26/33/29/33/40 | 161 | 14 | 39 | 79 | 161 |
| small_club_multi (lighting) | 1/1/1/1/1 | 5 | 0 | 0 | 0 | 5 |
| theatre (crowd) | 97/104/93/99/107 | 500 | 47 | 123 | 248 | 500 |

- **When a region is active,** its instances are first removed from the blocks. Flat per block then becomes `(int)((count − removed)·flat)`. Up to `(int)(regionN·full)` of the removed spots are filled by 3D members; the rest stay empty.
- **Engine:** shows the first `(int)(count·shown_)` per block. Region spots stay flat sprites. That matches the game's head-count closely; the main differences are the intro (game 100 %, ours 50 %) and the lack of 3D members up front.
- **small_club_multi:** shows nothing below Great (1 instance per block). [C arithmetic]

### 2. Full 3D crowd members

### Pool (`crowd instance`, crowd.dta `finish_loading`) [C]

- 10 base chars.
- +2 (`instance 4 h4 m1`, `instance 3 h2 m0`) unless the venue is fest.
- +5 more unless the venue is fest, arena, big_club or theatre.

| venue | pool |
|---|---|
| fest | 10 |
| arena | 12 |
| big_club | 12 |
| theatre | 12 |
| basement | 17 |
| small_club | 17 |
| small_club_multi | 17 |

The first 6 created double as the block archetypes; the region code takes them last.

### Regions (instances inside `crowd_limitsNN.mesh`, capped at the pool) [I: my point-in-triangle test in limits-local XY with 0 < z < 100.8 approximates `FUN_001e3600`]

| venue | regions (instance count; **0 = empty region**) |
|---|---|
| arena | r0 8, **r1 0** |
| basement | 17, 17, 17, 16, 12, 12, 15, 7, 6, 9, 10, 9 |
| big_club | 11, 11, **r2 0**, 11, 9, 10, 9, 10, 9, 12, 12, 12, 12, 12, 12 |
| fest | 9, 10, 8, 8, 10, 10, 10, 10, 10, 9, **r10 0** |
| small_club | 17×7, **r7 0**, 10, 16, 11, 12, 11, 12, 17, 17 |
| small_club_multi | r0 5 |
| theatre | 12, 11, 12, 11, 12, 12, 11, 11, 12, 12, 12, **r11 0** |

### Per-shot use [C defaults: `crowd_region` −1 (auto), `hide_crowd` 0; `FUN_0016f3b8` → `FUN_001727b0(crowd, region)`, `crowd+0xb0 = !hide_crowd`, `+0xb4 = force_cam_facing`]

| venue | shots | hide_crowd 1 | crowd_region use |
|---|---|---|---|
| arena | 33 | 5 | all 33 use **1 (empty)**, so arena **never shows 3D members** |
| basement | 25 | 13 | −1 ×8, 0 ×16, 6 ×1 |
| big_club | 38 | 11 | −1 ×15, 2 (empty) ×8, assorted others |
| fest | 45 | 18 | −1 ×17, 10 (empty) ×22, others ×1 each |
| small_club | 24 | 8 | −1 ×12, 0 ×7, 3/4/8/11 |
| small_club_multi | 4 | 2 | −1 ×2, 0 ×2 |
| theatre | 32 | 3 | −1 ×6, 0 ×7, 11 (empty) ×13, 3/4/5/6 |

- The artists use an empty region as "no 3D members". Auto (−1) scores regions by on-screen size over distance from screen centre (see `docs/crowd.md` §2).
- **Theatre's own script** (`theatre.dta` `post_switch_cam`) also hides blocks:
  - `arena::Crowd02.mm set_showing` = cam_msg ∉ {flr_far_rt02x2, flr_far_rt04, flr_far_lft03}.
  - `Crowd05.mm` = cam_msg ∉ {flr_far_rt02x2, flr_far_rt04}.
  - [I] Hiding works because the Crowd draws the multimesh through `FUN_002ec850`, i.e. the normal drawable path that checks showing.
- **3D member placement** [C, `docs/crowd.md`]:
  - Rotation = the instance's rotation rows. Position = (inst.x, inst.y, region floor z).
  - The skinned crowd char is drawn after the crowd env is selected, before the impostor blocks.
  - Animation is polled only while the crowd is shown (`FUN_001720e8`: active +0x4c, leaving +0x54, and pool chars with the archetype flag +0xb0).
- **Engine:** none of this exists (no regions, no 3D members, no hide_crowd, no Crowd02/05 hiding).

### 3. Animation

### `crowd animate` (`FUN_00173e70`) [C]

- **Inputs:** `{crowd animate flags (group weight lo hi) ...}` with flags `kAnimReset|kAnimGraphLoop`.
  - Groups: `(C_BAD) bad −5 5`, `(C_OK) ok −0.25 0.25`, `(C_GREAT) great −1 2`, `(C_IDLE) idle −0.25 0.25`.
  - There is no C_GOOD group, and no C_CLAP, C_JUMP or C_CHEER request.
- **Character list:** the active 3D chars (+0x4c), then the whole pool (+0x44, which includes the 6 archetypes). n = pool size (10 to 17). Leaving chars are excluded.
- **Shuffle:** for i in 0..n−2, swap(a[i], a[randInt(i+1, n)]), using `FUN_0024a1d0` → `FUN_0024a080`.
- **Split:** total = Σ positive weights. For each group with weight > 0: cum += w·n/total, end = (int)(cum + 0.5), and the next (end − prevEnd) chars get `FUN_00173508(crowd, char, group, flags)`.
  - Example: Okay with n = 12 gives `(int)(9.6+.5)` = 10 OK chars and 2 GREAT.
  - So **each archetype (block) gets its own randomly drawn group and clip**, and blocks differ from each other.
- **`FUN_00173508`:**
  - `FUN_0017fc28(char.driver(+0x94), groupList, flags)` → `FUN_0017e9a0(clipset, (C_X), restrict)`: among clips with `(clip.flags & restrict) == restrict` that match the group flags, pick the **least recently used**, then stamp its time (+0x18).
  - The clipset `main::crowd.cset` is loaded once by `crowd load_anims`, so **the LRU is shared by all crowd chars**. [I for "shared"] Chars of one gender in one group therefore get different clips, round-robin.
  - The clip plays with kAnimReset (no blend) and kAnimGraphLoop, so **the same clip keeps looping** through its graph self-edge until the next `animate`.
  - **Random phase:** if the new driver's weight is ≥ 1, frame = start + fmod(frame + rand(lo, hi) − start, end − 1 − start), wrapped positive. crowd+0xc0 = 1.0.
  - Crowd clips have `play_flags 0` (no beat phase lock), so the offset persists. Units are beats (frames advance by dBeat).
- **Clips per gender** (`anims_macros.dta` CROWD_ANIMS, restrict C_MALE1 for male01/male03, C_MALE2 for male02, C_FEMALE for female01..03):

  | group | clips |
  |---|---|
  | C_BAD | `_bad` (FIST), `_boo` (CLAP) |
  | C_OK | `_ok` (CLAP), `_idle` (CLAP, also C_IDLE) |
  | C_GREAT | `_1armpump` (FIST), `_2armpump` (HORN), `_clap` (CLAP), `_clap_high` (CLAP), `_jump_01` (FIST), `_jump_02` (HORN); male02 has one `_jump` |

  - Only the female clap clips carry C_CLAP, and C_JUMP is on the jumps. Nothing requests either flag (`restrict_anims` exists in Crowd_v1 but no script calls it).
- **Hands:** each clip's `HAND_CLAP`/`HAND_FIST`/`HAND_HORN` enter event calls `set_hand 0/1/2`. That shows `hand<N>_left/right.mesh` and hides the previous set. The `reset` handler starts at hand 2 (horn). Engine: implemented. [C]
- **Heads and outfits** (`crowd instance a (head h mat m)`): keep head h, remove the other 7, and swap the outfit to `crowd_<x>outfit`, `outfit02` or `outfit03` (m = 0/1/2). Block archetypes are 0/h0/m0, 1/h4/m1, 2/h5/m2, 3/h1/m0, 5/h5/m2. **Engine:** heads are done; **outfit mats 1 and 2 are not swapped** (blocks 2, 3 and 5 wrong). The flat sprites of a block all share the archetype's head and outfit.
- **Tempo:** song text events `crowd_half_tempo` / `crowd_double_tempo` / `crowd_normal_tempo` (sent by `FUN_00106348`) → `crowd set_frame_scale 0.5/2/1` (`FUN_001736c8`) → char +0x98 on active and pool chars. The beat seen by the char is multiplied by it. **Engine:** not handled for the crowd (`crowd_->update(ticks/480)` is unscaled).
- **Win, lose, intro and missed notes:** there is no direct crowd reaction. Misses lower the rock meter, which lowers excitement, which changes the next shot's sizes and clips.

### 4. Lighting

- **[C]** `FUN_00172350` first calls `FUN_002ec850(DAT_003637e4)`.
  - From the instructions at 0x17238c: `lui v0,0x36; addiu v0,0x3748; lw a0,0x9c(v0)`, so DAT_003637e4 = `*(Arena 0x363748 + 0x9c)`.
  - `FUN_00168370` stores `arena::crowd.env` at Arena+0x9c and removes it from draw lists. Neighbours: +0x84 = `arena::stagechar.env`, +0x88 vector = `arena::singer%i.env`.
  - **So the crowd env is selected before the 3D members and before each impostor render.** The archetype's materials are all `use_environ 1`, blend 3. The impostor quad material is unlit (use_environ 0, SrcAlpha, alpha_cutout) with a white material and vertex colour, so it carries **no tint** [I for the colour default]. The lighting is baked into the RT image.
- **crowd.env per venue** (all lights Directional, so position does not matter, only rotation):

  | venue | lights | ambient |
  |---|---|---|
  | arena | crowd_lighta (1, .2, .2), crowd_lightb (.1, .5, .8), crowd_light (0) | 0 |
  | basement | crowd01/02/03 (dim warm/red) | (.01, .01, .15) |
  | big_club | crowd_lighta/b (.34, .22, 0), crowd_lightc (.13, .09, .03) | 0 |
  | fest | crowd01/02 (.4, .27, 0), crowd03 (.23, .24, .16) | (.004, 0, .02) |
  | small_club / small_club_multi | crowd_lighta (1.5, .12, .12), crowd_lightb (0, 0, .5) | 0 |
  | theatre | crowd_lighta/b (.19, .1, .07) | (.106, .039, .004) |

- **The crowd lights are animated per lighting preset** [C], so crowd lighting changes with excitement and song section (`set_lights_per_excitement` in `arena/venue.dta`):
  - arena, small_club and small_club_multi switch `crowd_lighta/b(.1).litanim` explicitly in their `set_lights_*` handlers.
  - theatre, fest and big_club have `crowd_light*.litanim` / `crowd0N.litanim` inside `full.anim`. big_club also has them in `crowd.view`.
  - basement has them in `police.anim`.
- **Engine:** uses `crowd.env` for the impostors (correct). Verify that the venue AnimPlayer actually drives these LightAnims and that Character::draw applies the env ambient plus animated directional colours. If not, the crowd is static and dark (theatre ≈ 0.2 max).

### 5. Gap list vs `engine/src/render/crowd_renderer.cpp` (prioritised)

1. **P0: crowd clip filtering (character.cpp:170, 330-345).** For crowd specs (`Spec` category "crowd"), skip the `kWalkMask|kGuitarUI` filter and the guitar/band fallback list in `select()`. The crowd flag namespace (1..1024) overlaps the walk (0x3fe0) and band (`kBandActive` = 4 = C_FEMALE) constants.
   - Also skip the `kTempoMask` test (C_MALE1/2 are bits 0/1).
   - Expected after the fix: 6 C_GREAT, 2 C_OK and 2 C_BAD clips per gender.
2. **P0: animate semantics.** Replace the per-archetype "random on excitement change, then kAnimLongest re-select forever" logic with `animate`:
   - Run it whenever the venue camera picks a regular or solo shot (VenueCamera has the hook), using the current excitement's weights (table above). Use `init` (0, .9, .1, 0) at song start.
   - Shuffle all chars (pool + 3D + archetypes; if only archetypes exist, use them, n = 6 or the pool size). Split by `round(cum·n/total)`.
   - Pick the LRU clip **shared per clipset/gender** (one LRU table for the crowd, not one per Character).
   - Play with kAnimReset|kAnimGraphLoop (loop the same clip, no re-select). Add the random phase rand(lo, hi) beats, wrapped into [start, end−1).
   - Remove the per-frame `play(category, kAnimLongest)` path for the crowd: `update(..., oneShot=true)` or a dedicated call.
3. **P1: sizes timing.** Apply set_sizes at shot pick (not on the excitement change). Start at 1/1 from `init` until the first regular shot. Apply `reduced_crowd_shots[venue][cam_msg]` (cap flat only) and `$no_crowd`.
4. **P1: per-shot crowd visibility.** `hide_crowd 1` means draw nothing (no impostor render, no anim poll). Run the theatre `post_switch_cam` Crowd02/05 `set_showing` (or honour `mm->draw.showing` changed by venue scripts: today the constructor forces it false and `draw()` ignores it; keep a separate "owned by crowd" flag).
5. **P1: 3D crowd members.**
   - Build regions from `crowd_limitsNN.mesh`: limits-local 0 < z < h plus the point-in-polygon test, capped at the pool size, centre/radius, floor z = world z of vertex 0.
   - Implement switch_region on every shot (explicit index or the auto score).
   - Remove region instances from the flat blocks. Place `(int)(N·full)` pool chars (head/mat per `crowd instance` order, 10/12/17 per venue) at (inst.x, inst.y, floorZ) with the instance rotation. Draw them lit by crowd.env before the flat blocks. Leftover spots stay empty.
   - The impostor camera is relative to the archetype's current world (archetypes can also be active 3D members), so keep `eye = A.worldPos + (0, 0, h/2)·M − d·camFwd`.
   - Arena never needs it (region 1 is empty), and many theatre/fest shots use the empty region.
6. **P2: outfit swap** for mats 1/2 (`swap (mat crowd_<x>outfit.mat outfit02 outfit03)`) on the archetypes (and on 3D members).
7. **P2: `crowd_half/double/normal_tempo`** song events → crowd frame scale (multiply the crowd's beat; also delta-beat).
8. **P2: lighting check.** Make sure `crowd.env` lights are posed and coloured by the venue's light anims (full.anim / litanim / police.anim) at impostor-render time, with the env ambient included.
9. **P3: crowd frame M** (normalised sum of instance forward rows; only fest has yawed instances): rotate the archetypes by M at the origin.
10. **P3: no pose update while hidden.** Poll anims only while the crowd is shown, which matches `FUN_001720e8`.

### Key addresses

| Address | Role |
|---|---|
| 0x173e70 | animate (shuffle/split) |
| 0x173508 | play group clip + random phase |
| 0x17fc28 / 0x17e9a0 | LRU clip select with restrict |
| 0x1736c8 | set_frame_scale |
| 0x173620 | restrict_anims (unused by scripts) |
| 0x173778 | Crowd handlers: load_archetypes / load_anims / instance / animate / set_sizes / set_frame_scale / restrict_anims / switch_region |
| 0x172350 | draw; its first call selects `*(0x363748+0x9c)` = crowd.env |
| 0x168370 | Arena resolves crowd.env (+0x9c), stagechar.env (+0x84), singer%i.env (+0x88) |
| 0x16f3b8 | per-shot hide_crowd (default 0) / crowd_region (default −1) / force_cam_facing |
| 0x1720e8 / 0x174ad0 | crowd poll (beat = ticks/480 through the char's frame scale) |
| 0x106348 | song text events (crowd_*_tempo) |

## 4. Lighting and visual effects (full report)
### GH1 venue lighting / visual effects: what the original does that gd01 does not

[C] = read from code (decomp / VU / data), [I] = inference. Addresses are EE (SLUS_212.24).
Data counts come from decoding every venue `.rnd_ps2` (scripts in `scratchpad/lt/`:
`survey.py`, `views.py`, `fx.py`; run with `python3 survey.py flare|ps|mat [all]|light|lanim|env|matanim|tex|movie|cam`).
`shadowtest.rnd_ps2` (big_club) is never loaded and is left out of the counts.

### Already handled by the engine (no action needed)

- **Environ selection, ambient, ≤3 directionals / 1 point, fog** (`scene_renderer.cpp` lightingFor/setMatFlags, `renderer.cpp` shader). These match docs/lighting.md.
- **LightAnim colour keys** (`anim.cpp` Kind::Light). 5–14 animated lights per venue, mostly 20–45 keys on ticks, driven through the section views and `switch_anim` slots.
- **EnvAnim** (ambient, fog colour, fog range). This drives the "light beam" environs: `beam.env`, `chorus01.env`, `bank01.env`, `color_plane.env`, … have no lights. Their animated ambient colours the additive cone meshes drawn after them.
- **MatAnim**: colour, alpha, stage translation and texture flip keys (all stages' data are updated).
- **Blends**: mat blends 0..5 map to GL. ALPHA 0x4a/0x68+FIX80/0x44/0x48/0xa1+FIX80 decoded in FUN_0019d3b8 agree with `blendFor`. **zmodes**, **alpha cutout**, **Multiply2 ×2**.
- **`add_trans` of the rim light and `color_plane.mesh` to "6 foot camera.cam"**.

---

### Ranked list of what is missing

### 1. Transform constraints: billboarded light cones, glows and smoke cards — HIGH [C]

`readTransformable` (`scene.cpp:84`) throws the constraint away. FINDINGS only covers it for crowd impostors. `PsMesh_v5` (0x2ec320) draws every mesh with `FUN_001da848(xfm, world)`, which rebuilds the world rotation from the current camera (`DAT_002f1be8`) when the constraint (xfm+0xa4) is ≥ 5.

Enum (`FUN_001db750` print):

| value | name |
|---|---|
| 0 | None |
| 1 | LocalRotate |
| 2 | LookAtTarget |
| 3 | ShadowTarget |
| 4 | MirrorTarget |
| 5 | BillboardZ |
| 6 | BillboardXZ |
| 7 | BillboardXYZ |
| 8 | FastBillboardXYZ |

Rows are row-vector world rows X, Y, Z and pos. `c` = camera world pos (cam+0x90); camera rows are at cam+0x60/0x70/0x80.

- **5 BillboardZ**: `Y = pos − c` (unnormalised); `Z = world Z` (normalised if preserve_scale); `X = normalize(Y × Z)`; `Y = Z × X`. The card spins about its own Z axis to face the camera, like a cylinder billboard.
- **6 BillboardXZ**: `Y = normalize(pos − c)`; `X = normalize(Y × worldZ)`; `Z = X × Y`.
- **7 BillboardXYZ**: `Y = normalize(pos − c)`; `X = normalize(Y × camZ)`; `Z = X × Y`.
- **8 FastBillboardXYZ**: rotation = camera rotation rows.
- If xfm+0xa8 (preserve_scale) is set, each row is multiplied by the original row's scale (`FUN_0024a420`).

Data (meshes per constraint; these are essentially all the volumetric light cones and beams):

| file | 5 BillboardZ | 7 BillboardXYZ | 8 FastBillboardXYZ |
|---|---|---|---|
| arena lighting | 49 (`pink_beam*`, `red_beam*`, …) | | |
| big_club lighting | 30 (`light_beam_opt*`) | | 19 `smokeplane*` |
| fest lighting | 22 (`light_cone_white*`) | | |
| small_club lighting | 45 (`light_cone_*`) | 42 (`light_glow_*`) | |
| small_club_multi | 44 | 42 | |
| theatre lighting | 46 (`light_cone_verse*`) | | 12 `glow*` |
| basement lighting | 1 | | 15 `smoke*` |

- **Today:** cones are drawn as fixed flat cards. From most camera angles they are edge-on (thin slivers or invisible) or show their flat outline. This is very likely part of the reported "venue artifacts".
- **Implement:**
  - Keep `constraint` and `preserve_scale` in `Transformable`.
  - In `SceneRenderer::drawObject` (mesh and multimesh paths), replace the rotation part of `world(*dm)` with the formulas above, using the current camera world. Pass the camera Xfm into `SceneRenderer::draw`.

### 2. `_transparent` views are drawn after the band, not inside the main view — HIGH [C]

- `FUN_00178db8` loads `<s>_transparent.view` into VenueSection+8 and calls `FUN_001b28a0`. That function walks the view's referrers and removes it from every parent View's draw list (`FUN_001ccdc0`).
- `ArenaPanel_v11` (0x10d488) draws in this order:
  1. `FUN_001696d8`: each section's main view (`FUN_00178d68`), the blur overlay `FUN_0016d4c8`, the crowd (`FUN_00178948`, `FUN_00172350`).
  2. `FUN_002826d0`: the characters.
  3. `FUN_00169818`:
     1. VenueSpotLight floor-spot meshes (`FUN_00179690`).
     2. Each section's transparent view (`FUN_00178d90`).
     3. Fire (`FUN_00175588`), name lights / fireworks (`FUN_00177888`), camera fade (`FUN_00169048`).

What the transparent views hold (`views.py`):

| file | contents |
|---|---|
| arena lighting | 128 meshes: 82 Add + 45 SrcAlphaAdd beams/spots, `color_plane` |
| arena venue | 6 Flares, `fx_ray*`, flames, 6 ParticleSys |
| small_club lighting | 201 meshes (178 additive cones/glows), dry-ice particles |
| theatre lighting | cones, 8 flares + `flare_obstruct`, 12 spark systems |
| fest lighting | `color_plane.mesh`, `bank_glow*` |
| basement lighting | christmas glows + 5 ParticleSys, `color_plane` |
| big_club lighting | beams, fan light, solo light |

- **Today:** `venue_renderer.cpp:180` draws them inside the section view, before the crowd and band. Additive beams don't write Z, so the band paints over every beam, glow and flare in front of it.
- The camera-attached **`color_plane.mesh`** (additive, zmode Disable or Transparent, colour from the EnvAnim-driven `color_plane.env`) is the full-screen colour wash. It currently never tints the band or crowd.
- **Implement:**
  - At load, remove `<s>_transparent.view` from its parents' draw lists (or skip it while drawing the main view).
  - Draw it after the band, preceded by the floor spots (#5).
  - Keep the environ state as the original leaves it. `env_` carries over within a SceneRenderer.

### 3. Unlit `vert_amb` materials must ignore the material colour — HIGH for fest [C]

- Per docs/lighting.md and `FUN_002ebcb0`: a pass with `!(use_environ || !vert_amb)` runs VU program 0x614 (`JR` only). The vertex colour goes to the GS untouched; the material colour (0x2b2) is not used.
- **Today:** `vColor = aColor * uTint` multiplies by the material colour.

| material | colour | effect today |
|---|---|---|
| fest `grass.mat` | **0,0,0** | ground renders black |
| fest `mountain_range.mat` | 0.35 | |
| arena `arena.mat` | 0.4 | |
| arena `4 - Default.mat` | 0.1 | |
| arena `24 - Default.mat` | 0.92,0,0 | |
| `dry_icesmokemat` | 0.4 | |
| small_club `walk_spot_02.mat` | | |
| fest `Material #1142` | | |
| big_club `red_glow.mat` | | |

- **Implement:** in the unlit branch, use `vColor = aColor` when `vert_amb && !use_environ`. Keep the tint only for the paths the VU actually multiplies.
- This also means MatAnim colour/alpha keys have no effect on such materials, by design.

### 4. Extra material stages (multipass glow layers, sphere/environ maps) — HIGH [C]

`FUN_0019d3b8(mat, pass)` builds one GS pass per stage. Stages whose blend is 0 (Dest) are skipped (`puVar20[4]` next-pass loop). Rules:

- **Pass 0:** blend = mat.blend; TFX from stage-0 blend (`DAT_002edd80`); lit as usual.
- **Pass k>0:** blend = **stage k's blend**, via the same ALPHA table: 2 = add, 3 = alpha, 4 = srcalpha-add, 5 = sub.
  - TFX index = 1, which is **DECAL**: the texture colour is used raw, unlit, and the pass is marked unlit (`puVar20[1]=0`).
  - Exception: `mat.multipass == 2` (MultipassLit) uses MODULATE with the lit vertex colour.
  - Multitexture (0) and MultipassSrc (1) behave identically.
  - Colour scale ×128; no fog unless the stage blend is 1 or 3 (`puVar20[2]`).
  - Z test only, no Z write (zmode 1/2 → ZTST GEQUAL, `0x100000000` mask). zmode 3 or 4 on pass > 0 also masks Z.
- **Texgen per stage:**
  - 1 Xfm → VU 0x17c with the stage UV transform.
  - 2 Sphere → 0x347 (sphere map from the view-space normal).
  - 3 Projected → 0x410.
  - 5 Environ → 0x139 (the 3x3 is rotated by a fixed axis swap; uv offset 0.5).
  - Exact sphere/environ VU maths: not traced [I]. A standard view-space-normal sphere map, uv = n.xy·0.5+0.5, is the obvious stand-in.

Data: 39 materials. Meshes per file:

| file | meshes | stage-1 layer |
|---|---|---|
| arena | 20 `track light obj` | `track_light_bright` |
| arena | `suite.mat`, `arena.mat` | `suite_glow` (suite windows glow) |
| arena lighting | 4 `lens1.mat` | stage-1 tex flip-booked by `beamglow.matanim` per lighting state |
| small_club | 6 `bones` | `bones_glow` |
| small_club | 4 `crown_sign` | `crown_glow`, SrcAlphaAdd |
| small_club | 5 `snake_neon` | `snake_glow` |
| small_club | 2 `swill` | `swill_glow` |
| small_club | `window_cbgb`, `Material #507` | Environ maps `sphere_REF01/02` |
| small_club (+ _multi) lighting | 21 `light_bright00` | MultipassLit, same tex added again |
| big_club lighting | 5 `tracklight` | `track_light_obj_j` |
| basement | `red`/`green`/`yellow` | Sphere `sphere_REF01` (env-mapped drum kit) |
| basement lighting | `lava` | `lava_add` |
| basement lighting | `lightstring` | `christ_lights_glow` |
| theatre | `paramount_lights` | `paramount_lights_add`, Xfm-scrolled |
| theatre | `hot_white` | `hot_can_add` |
| fest | `atom_bomb` | 3 stages |
| big_club | `fire` | `lightred` |

- **Today:** only `mat.stages[0]` is drawn (`scene_renderer.cpp:91`), so every neon/glow/light-rig "add" layer and every reflection is missing.
- **Implement:**
  - Build one `DrawMesh` state per extra stage with blend from `stage.blend`, `depthWrite=false`, texture `stage.tex`, and its UV transform.
  - Use unlit `tint=1` (DECAL: colour = tex rgba) unless MultipassLit.
  - Issue it right after pass 0 with the same geometry.
  - Add a sphere-map texgen path in the shader for texgen 2/5.

### 5. VenueSpotLight: the spotlight tracks the guitarist, plus a floor glow under him — MEDIUM-HIGH [C]

- Config: `config/gen/gh.dta` has `(spot_lights "arena::spotlight01.lit")`; `arena.dta` has `(floor_spot_height 1.5)(floor_spot_scale 100)`. No venue sets `no_spotlights`.
- Setup:
  - `FUN_00168808` creates one VenueSpotLight per entry, paired with arena character i (the guitarist).
  - `FUN_00179190`: copies `floorspot_char.mesh`, gives it material `arena::floorspot_glow.mat`, attaches it to the character, then deletes the template meshes (`FUN_00167e28`).
- Per frame, `FUN_00179420` (called from `FUN_001691a0`). `t` = character root world pos, `d = t − lightPos`:
  - Light rotation = `FUN_0024adf8(d, up=(0,0,1))`: `Y = normalize(d)`, `X = normalize(Y × up)`, `Z = X × Y`. The light now shines along +Y straight at the guitarist.
  - Floor mesh:
    - rotation from the horizontal `(dx,dy,0)` the same way;
    - row X × 100;
    - row Y × `100·|d|/|dz|` (an ellipse stretched away from the light);
    - pos = `t.xy + 0.25·rowY.xy`, `z = t.z + 1.5`.
  - Drawn in phase 2, before the transparent views.
- Data:
  - `spotlight01.lit` exists in every venue's lighting scene (colours 1–5, e.g. theatre 4,2,0.38) and sits in `singer1.env`, `lighting.env` and others. Scripts select `singer1.env` during solos (`set_singer_env`).
  - `spotlight01.litanim` keys sit at frames 20000–23840 (solo).
  - Each venue's lighting scene has its own `floorspot_glow.mat` (additive, `spot_circle*.tex`) with `spotlight.matanim`: 9 colour + 9 texture keys per lighting state, e.g. big_club solo (1,0.51,0.44).
  - The mesh template is only in `system/run/arena/gen/fx.rnd_ps2` (loaded by `FUN_00167fe0` as `../../system/run/arena/fx.rnd`).
- **Today:** `spotlight01` keeps its file direction (straight down), and there is no floor spot.
- **Implement:**
  - Load `fx.rnd_ps2` (floorspot mesh; also `shaky_cam*.tnm`).
  - Re-aim `spotlight01.lit` every frame before lighting the band.
  - Draw the floor ellipse with the venue's `floorspot_glow.mat`.

### 6. Lens flares (RndFlare) — MEDIUM [C]

- `RndFlare_v5` (0x1f4d08) is the draw.
- Fields: `sizes` (+0x104/+0x108), `mat` (+0x10c), `range` (+0x114 near, +0x118 far), `steps` (+0x11c), `count` (+0x120), `visible` (+0x100).
- Each frame:
  1. **Occlusion query.** `FUN_001d2e38` projects the world pos (`FUN_001b1100`). Outside the near/far range or off-screen → `visible=0`. Otherwise it queues a 1-pixel test (screen x,y and the Z for that depth, cam vt+0x3c). `FUN_0019ae48`, run in `PsRnd_v11` at frame end, reads the GS Z-buffer pixel back: `visible = zbuf < flareZ`. The scene did not cover it.
  2. `count = clamp(count + 2·visible − 1, 0, steps)`. 0 → not drawn.
  3. `t = clamp((depth − range.far)/(range.near − range.far), 0, 1)`. Mat RGB = `t·count/steps` (the file mat colour is 0; the flare writes it).
  4. If stage 0 texgen = Xfm, the texture rotates by angle `sx − 0.5`, where `sx` is the screen x in 0..1.
  5. Size: `s = sizes[0]`. If `sizes[1] ≠ sizes[0]`, `s = lerp(s0, s1, max(0, −dot(camY, flareY)))`, growing when the flare's +Y faces the camera.
  6. Screen rect: width `s·screenW`, height `s·screenW·(H/W)/aspect`, centred on the projected point. Drawn through rnd DrawRect (vt+0x4c) with the flare mat. A 2D sprite.
- Script: `{$flare set_steps n}` (`FUN_001f50e0`) rescales `count` proportionally; n<2 → 1.
  - On `chorus` (excitement > Bad) the venue scripts call `set_steps FLARE_UP (0)` and hide `flare_obstruct.view`.
  - 240 ticks later they call `set_steps FLARE_DOWN (180/360)` and show it again.
  - `flare_obstruct.view` holds `flare0x.mesh` with `flare_transparency` (alpha 0, SrcAlpha, zmode Normal). These write Z only (GS TEST AFAIL=ZB_ONLY for blend 3, `FUN_0019d3b8` 0x200d), so they occlude the flares.
- Data: 36 flares.

| venue | flares |
|---|---|
| arena | 12 (`flare.mat`, sizes 0.1–1.0, range 0–5000, steps 20) |
| small_club | 6 (`glowwhite_big.mat`, 0.5→0.75, steps 60) |
| small_club_multi | 6 |
| theatre | 8 `ok_chorus0x.flare` (0.1→1.5, range 2000) |
| big_club | 1 `fan_flare` (shown/hidden by script) |
| basement | 2 `cop_red`/`cop_blue` (0.5→10: police beacons flash huge when facing the camera; driven by `police.anim`) |

- **Today:** Flare objects are dropped, and `set_steps` is skipped (`venue_script.h:20`).
- **Implement:**
  - A Flare draw item in the transparent pass.
  - For occlusion, do a depth-buffer read at the projected pixel after the scene (one-frame latency, as on PS2), or use GL occlusion queries.
  - Add a script hook for `set_steps`.

### 7. Particle systems — MEDIUM [C emitter; I sprite details]

- Simulation:
  - `PsParticleSys_v1` (0x1cffc0, Animatable SetFrame): `dt = frame − lastFrame` in the driving view's units (ticks for `<s>.view`, ms for `_rt`). Then update `FUN_001d0a60` and emit `FUN_001d0060`.
  - Emit: `accum += rand(emit_rate)·dt`, one particle per whole unit.
  - Each particle draws life, position in the pos_low/high box, speed, pitch/yaw direction, start/end size and colour (with mid colour), and `force`, from the record's ranges.
  - An emitter mesh, if set, gives random positions on its triangles.
  - `ParticleSysAnim` keys animate emit rate, colours, speed, life and size.
- Draw (`PsParticleSys_t1_v5` 0x1a2fd8 → `FUN_001a2df0`):
  - Per particle {rgba, pos, size·0.5} go to VU program 0x5cc in batches of 127, with the system's world matrix and the material passes (`FUN_002ebcb0`).
  - They are expanded to camera-facing squares [I] with colour = particle rgba (blended by the mat).
- Data (41 systems in loaded scenes):
  - small_club and _multi: 2 dry-ice smoke systems each (400 max, life 8–35 s).
  - basement: steam ×4, furnace sparks, washer bubbles, lightbulb; **christmas lights ×4 = 656 preserved static sprites** (`preserve_particles`, size 7).
  - fest: **bulbs.part = 240 preserved bulbs**, toxic bubbles, nuke_toxic (400).
  - theatre: 12 spark bursts (`fire01..12`, ParticleSysAnim emit 2/frame for 10–100 frames when switched in).
  - arena: `fire01..4` + 2 `stage_flame` systems.
  - big_club: 4 fog systems.
- Preserved particles load with pos/colour/size only (`FUN_001d24e0`). Whether they age out is [I]; treat them as permanent glow sprites.
- **Today:** not loaded at all.
- **Implement:**
  - A CPU emitter that follows the record (formulas in `tools/rnd_classes/animation.py`, `FUN_001d0060`).
  - Point sprites/quads in the transparent pass.
  - Start with the preserved static sprites, which are cheap and give the biggest visual gain (christmas lights, fest bulbs).

### 8. Depth of field (background blur) — MEDIUM [C]

- VenueCam per frame (`FUN_0016e390` end): `FUN_0019adf0(enable_dof ? 2·dist : 0, rnd, cam)`, with `dist = |camera − target|`. It sets `rnd+0x518 = cam.DepthToZ(2·dist)`.
- `set_depth_of_field` (`FUN_0019c168`) turns it off; `arena_game.dta:234` does that for the outro.
- End of frame, `FUN_0019afa8` (in `PsRnd_v11`) when +0x518 ≠ 0, via `FUN_0019a998`. Bilinear full-screen sprites, frame buffer as texture into the other buffer:
  1. Copy ×0.25 (ALPHA 0xa8, FIX 0x20).
  2. Three more copies, each added ×0.25 (ALPHA 0x68, FIX 0x20), offset by (0,3), (3,0), (3,3) px. Together a 4-tap ~3 px box blur.
  3. Composite the blurred image back at offset −1.5 px, as a sprite at Z = focus with **ZTST GEQUAL**. Only pixels whose depth is **beyond 2× the target distance** are replaced.
- Data: `enable_dof 1` on **107 of 162** shots that set it (arena 33, fest 31, big_club 26, theatre 24, small_club 22, basement 22, small_club_multi 4).
- **Today:** not implemented (`venue_camera.cpp` ignores `enable_dof`).
- **Implement:** render the venue into an FBO with a depth texture, box-blur it (3 px at 448 lines, scaled to resolution), and composite where `depth > focusDepth`.
- Whether the PS2 pass also covers the HUD/highway depends on the highway's Z [I]. Apply it to the venue pass only.

### 9. MatAnim stage rotation/scale keys — LOW [C data]

- The engine applies only translation keys (`anim.cpp` Mat).
- Rotation/scale keys exist for:
  - small_club `record.matanim` / `record_g` (spinning records);
  - theatre `grey_cone_anm.mnm` (rot);
  - fest `sun.matanim` (rot 2 keys).
- Implement the stage 3x3 from scale/rot keys (`FUN_001c2518`).

---

### Present in code but dormant in the shipped data (skip)

All [C]: grep of `extracted_dta` finds no caller, and there is no matching scene data.

- **Blur feedback** (`enter_blur`/`exit_blur`, `FUN_0016d6e8`/`FUN_0016d4c8`, `blur_alpha 0.8 / grow 0.01 / duration 1000`):
  - Each frame it draws the previous frame zoomed in by `grow` (uv rect inset) at alpha 0.8. After `duration` ms the alpha fades to 0 over `duration/2`. It is a zoom-trail effect.
  - Only the Arena `enter_blur` handler starts it, and no script sends it. `exit_blur` is sent at reset (`FUN_00169ed8`).
- **camera_fade** (`FUN_00169048`, full-screen mat fading 1→0) and **VenueCam set_fade**: only in `camtest.dta`/`camedit.dta`.
- **Fireworks / ArenaFire / NameLights** (`arena.dta` fireworks/fire/name_lights, `launch_fireworks`, `ignite_fire`, `set_name_lights_*`): no venue has `fire_spot_*`, `fireworks_*` or `name_lights_*` meshes, and no script calls them. The venues' fire is plain ParticleSys (#7).
- **song_movie / singer video** (`FUN_001796e8`, `arena/movies/%s.ipu`, `movie_*_singer`): `use_video 0` in all venues. No venue scene has a Movie or a movie-type Tex.
- **Bloom/glow post**: none exists. `PsRnd_v11` only does the flare readback and DOF. RndBlur exists as a class but no scene uses it.
- **Spotlight cones as a class**: none. Cones are plain additive meshes (#1, #2) coloured by EnvAnim'd environs.

### Suggested order of work

1. **#1 billboards** (small change, every venue).
2. **#2 transparent pass after the band** (small change).
3. **#3 unlit colour fix** (one-line shader change).
4. **#4 extra stages.**
5. **#5 spot tracking + floor spot.**
6. **#6 flares.**
7. **#7 particles**, preserved sprites first.
8. **#8 DOF.**
9. **#9.**
