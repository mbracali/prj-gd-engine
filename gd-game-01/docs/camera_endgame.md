# GH1 PS2 RE: shaky cam, eyes, crowd_region, and the win / lose / game-over camera

Marks: [C] = read in decomp/disassembly/data; [I] = inference. Addresses are EE virtual addresses (SLUS_212.24).
Prerequisite reading: `docs/camera_walk_crowd_lighting.md` §1 (VenueCam math, picker), `docs/end_of_song.md` (game_over / game_outro flow).

## 0. Summary

- **`shaky`** is purely a per-shot data flag; it is not tied to excitement, Star Power or the song. 68 shots set it: arena 4, basement 9, big_club 24, fest 10, small_club 9, theatre 12. No WIN/LOSE/FINAL shot is shaky. The only special shot that is shaky is basement `Intro01`.
  - On a shaky shot, `shaky_cam1.tnm` (a position-only TransAnim in `arena/fx.rnd`) is sampled on the real-time ms clock, looping every 3200 ms.
  - Its small offset (|x| ≤ 0.23, |z| ≤ 0.2, y = 0) does two things:
    - **It pre-multiplies the camera path transform.** This barely shows.
    - **It is added to the band-focus position every frame.** This is cumulative, so the focus drifts against the cam_filter pull. The drift is ~5–12 world units with a 3.2 s period, which gives the handheld sway [C code, I magnitude].
- **`eyes`** is an int mode sent to the guitarist's eye controller on every shot switch. The default is **3, look at the crowd**. The data only ever writes `eyes 0` (eyes at rest), on 10 shots: the theatre and small_club solo shots, basement flr_far_lft01, and the tutorial. Modes 1 (look at the camera, with jitter) and 2 (look forward) exist but no shipped shot uses them.
- **`crowd_region`** (default −1 = automatic) chooses which 3D near-stage crowd region is filled for the shot. See `docs/camera_walk_crowd_lighting.md` §3 and `docs/crowd.md`. It only matters once 3D crowd members exist.
- **Game over camera** (`arena/gen/camera.dta` `game_over` handler) [C]:
  1. **Final win** (the career final song is won, or `$final_win` is set) → a random `$camedit.FINAL_WIN` shot, or `FINAL_WIN_GRIM` if the character is "grim". It runs at once. Only the arena defines these: `Cam_t_p_pan`, 19 s, real time.
  2. **Won** → a WIN shot after a **1750 ms** delay (`WIN_CAMERA_DELAY`, `game delay_task`). Until then the regular shot keeps running.
  3. **Lost** → a LOSE shot (named `lose01` in every venue) at once. Its `pre_switch_cam` triggers `char_sys lose_teleport`.
  4. In every case `$camera.bars_left = 100`, so downbeat timeouts cannot replace the shot. The shot then holds until `game_outro` changes the screen. That happens when the guitarist's win or lose clip ends, about 4–10 s later (18.3 s for the final win).
- **Our engine** has the picker, focus, DOF and hide_crowd. It has **no** game-over camera at all (the regular director keeps cutting after the song ends), no shaky, no eyes and no crowd_region. Its lose_teleport trigger tests for `"LOSE"`, but the shot is named `lose01`, so it never fires. The guitarist's game-over play modes are wrong (both use kAnimNow), and WinFinal is missing.

---

## 1. Per-shot keys read by `switch_cam` (`FUN_0016f3b8`, VenueCam) [C]

The keys are interned at 0x311260…0x311358. The defaults are the values written before each `FUN_0022d…` lookup.

| key | reader | stored at | default |
|---|---|---|---|
| start / end | float | local, ×(path length/100) | 0 / 100 |
| duration | float | local | VenueCam+0x30 = `cam_default_duration` (1920) |
| loop | int | +0x3c | 0 |
| offset_in / offset_out | vec3 | +0x50 / +0x60 | 0 |
| hide_crowd | int | crowd+0xb0 = !v | 0 |
| force_cam_facing | int | crowd+0xb4 | 0 |
| **eyes** | int (`FUN_0022da70`) | → focus char vtable slot 5 | **3** |
| singer_in / singer_out | vec2 | +0x1c / +0x24 | 0 |
| enable_dof | int | +0x98 | 0 |
| **crowd_region** | int | → `FUN_001727b0(crowd, r)` | **−1** |
| ease | float | ATan interpolator if ≠ 0 | 0 |
| **shaky** | int | → `FUN_0016f248(this,"shaky_cam1.tnm")` or +0x78 = 0 | 0 |
| real_time | int | +0x9c (clock select, `FUN_0016e370`) | 0 |
| guard | vec2 | DAT_00364740/44 (0 → `cam_guard_band`) | 0 0 |
| near / far / fov_in / fov_out | float | +0x40 / +0x44 / +0x48 / +0x4c (deg→rad) | the current camera's |

Order inside `FUN_0016f3b8` (it matters for lose_teleport):
1. `+0xac = cmd`, `+0x70 = cam_msg` (shot name = switch_cam arg 3).
2. Send **`pre_switch_cam`** to arena.
3. Read the keys; pick the focus character index +0x74 (0 in single player; MULTIPLAYER: "Left" → 0, "Right" → 1, else −1).
4. **Eyes:** `if (+0x74 != −1 && focus list non-empty) focusChar->vtbl[5](eyes)`.
5. **Shaky** setup.
6. Target / parent / path; frame task (`FUN_00248898` linear or `FUN_00248d50` ease).
7. Focus snapped to the head (`FUN_0016e080` → `FUN_001da730`).
8. Crowd: region, `!hide_crowd`, force_cam_facing.
9. fov/near/far.
10. Send **`post_switch_cam`**.
11. +0xa0 = −FLT_MAX (forces a camera update).

Clock (`FUN_0016e370`): `real_time ? DAT_00363798 (ms) : DAT_0036379c (ticks)`.

---

## 2. Shaky cam [C unless marked]

### 2.1 Data
`../../system/run/arena/fx.rnd` (the ark has `_up_/_up_/system/run/arena/gen/fx.rnd_ps2`; string 0x310288 is loaded by `FUN_00167fe0`, the arena loader) holds `shaky_cam1.tnm` and `shaky_cam2.tnm`. Only `shaky_cam1` is referenced (0x311370).

`shaky_cam1.tnm`: TransAnim v4, `trans_spline 1` (Hermite), no rotation or scale keys.
- Anim filters: `ScaleOffset(scale 1, offset 0)`, then `MinMaxLoop(0, 3200, loop)`. So **frame = elapsed ms mod 3200**.
- Position keys (frame: x y z), with y always 0:

```
   0:  0      0  0        398: -0.10  0  0.09     796: -0.20  0  0.10
1024: -0.176  0  0.0176  1240: -0.13   0 -0.095  1706:  0.00  0 -0.02
1856:  0.0555 0 -0.16    2093:  0.143  0 -0.026  2497:  0.23  0  0.20
2624:  0.201  0  0.193   2858:  0.096  0  0.146  3200:  0     0  0
```

(`shaky_cam2.tnm`: 14 keys around (6, 0.7, 64), frames 0–100 scaled by 1/32; unused.)

### 2.2 Start (`FUN_0016f248`, also reachable through the unused script command `cam rumble_cam <tnm>` → `FUN_0016ef58`)
- `+0x78 = tnm`; `+0x7c = now_ms` (`DAT_00363798`, always the real-time clock, whatever the shot's `real_time`).
- If the anim has a looping filter (`FUN_001ac5a0`) → `+0x80 = 1e30`, so it never ends. Otherwise `+0x80 = start + length_ms`.
- shaky_cam1 loops, so **the shake lasts until the next switch_cam**. A non-shaky switch sets `+0x78 = 0`.

### 2.3 Per frame (`FUN_0016e390`)
1. `S = identity`. If `+0x78`:
   - `frame = filters(now_ms − +0x7c)` (`FUN_001ac468`);
   - `S = TransAnim(frame)` (`FUN_001de038`; rotation identity, position = shake `s`);
   - if `+0x80 ≤ now`, clear +0x78 (never, for shaky_cam1).
2. The usual `u`, singer lerp, and head→screen error. `t = cam_filter · min(err, 1)`.
3. **`focus.localPos = lerp(focus.localPos, head, t) + s`** (`auStack_110 += auStack_130`). The old value is the focus's *local* position (`*(focus+0x50)+0x50`), so the shake **accumulates** into the focus every frame. The cam_filter error pull is what keeps it bounded.
4. `P = TransAnim(path, frame)`; **`P = S · P`** (`FUN_0024b608(&S, &P, &P)`). The shake translates the camera in path-local axes, by ≤ 0.23 units.
5. `P.pos += lerp(offset_in, offset_out, u)`; `× parent world`; aim; singer shift; fov; DOF. All as before.

Effect [I, simulated at 60 Hz, fov 45°, 4:3]: the focus wanders up to ~5 units (D = 60) to ~12 units (D = 400) from the head, with the 3.2 s shake period.
- For `Cam_t_p_*` paths (parent = target = focus), the whole view translates.
- For `Cam_t_np_*` paths (parent = venue.view, target = focus), the camera pans.
- The drift is frame-rate dependent in the original; the PS2 runs at 60 Hz [I].

---

## 3. Eyes [C]

- **Creation:** `charbase.dta` → `{$this create_eyes (parent bone_head.mesh) (constraint 0.925) (lid_lower 0.5)}` (`FUN_0018dbf0`). This builds an eye object at char+0xe8 (`FUN_0018fd58`, size 0x110) that drives `L-eye.mesh` / `R-eye.mesh`.
- **Mode set:** `Character_v5 @0x18d4d8` (vtable 0 slot 5) → `FUN_0018fd10(eyes, mode)`.
  - mode 0 → `FUN_00190720` (eyes back to rest).
  - else: clear the offset; `+0x20 next = −1`, which re-targets at once.
- **Config** (`config/charsys.dta`, read by `FUN_0018f8a0`):
  - `eye_camera_max_offset 12`, `eye_camera_interval 1920 3840`;
  - `eye_forward_distance 80`, `eye_forward_max_offset 12`, `eye_forward_interval 960 2000`;
  - `eye_crowd_interval 960 1920`.
  - Intervals are in ticks: `now = char.beat(+0x84) · 480`.
- **Per frame** `FUN_0018fa18` (from `Character_v13`):
  - **mode 1 (camera)** (also any value ≥ 4):
    - when `now ≥ next`: `off = (rnd(±12), rnd(±12), 0)`, `next = now + rnd(1920, 3840)`;
    - target = venue camera world pos (`DAT_00363824+8` → +0x50 → +0x90) + off.
  - **mode 2 (forward):**
    - when `now ≥ next`: `off = (rnd(±12), 0, rnd(±12))`, `next = now + rnd(960, 2000)`;
    - target = head xfm · (off.x, 80, off.z).
  - **mode 3 (crowd):**
    - when `now ≥ next`: target = a random active 3D crowd member's position (`FUN_00172cc8`; **(0,0,0)** if none is active, e.g. in the arena);
    - `target.z += (head.z − target.z)·0.7`;
    - `next = now + rnd(960, 1920)`.
  - Then `FUN_00190110`, for each eye:
    - direction to the target;
    - clamped to a cone of cos = `constraint` (0.925, about 22°) around the head's forward;
    - **slerp 0.25 per frame** toward it;
    - lower lid (`lid_lower 0.5`).
- **Camera use:** every switch_cam calls slot 5 with the shot's `eyes` (default 3) on the focus character (guitarist0 in 1P). So the guitarist's eyes look at the crowd on every shot, except the 10 `eyes 0` shots. The band members' eye objects are never given a mode, so they stay at rest [I: no other caller of slot 5 found].

---

## 4. crowd_region / force_cam_facing / hide_crowd [C]
`FUN_001727b0(crowd, crowd_region)`, `crowd+0xb0 = !hide_crowd`, `crowd+0xb4 = force_cam_facing`. Defaults −1 / 0 / 0.
- Region semantics, the per-venue region tables and the "empty region = no 3D members" convention are in `docs/camera_walk_crowd_lighting.md` §3 ("Regions", "Per-shot use") and `docs/crowd.md` §2.
- −1 = automatic: regions are scored by on-screen size over distance from screen centre.
- The 3D crowd member list is also what eyes mode 3 looks at.

---

## 5. Win / lose / game-over sequence

### 5.1 Trigger chain [C]
1. The song end (`now > end`), or a rock-meter fail, sets `$arena.game_won` and sends **`game_over`** to arena. See `docs/end_of_song.md`.
2. All `arena add_handlers` for `game_over` run [I: load order]:
   - **arena_game.dta:** `$p on_game_over`; stop `sp_deployed(_multi)`.
   - **venue.dta:** `{if_else $arena.game_won {arena game_won} {arena game_lost}}`.
   - **guitarist.dta:** 1P `{guitarist0 set_game_over $arena.game_won}`. 2P: each guitarist `set_game_over (score ≥ best)`.
   - **camera.dta:** the shot selection in 5.2.
   - **crowd.dta:** `(game_over)` is empty.
3. **`game_won`** (arena_game.dta):
   - `track_panel explode`, TRACK_EXPLODE_SFX;
   - **`game set_realtime TRUE`**;
   - hud off, crowd_win, `beatmatch set_paused TRUE`;
   - win message shown for 3000 ms.

   Also on game_won: **venue.dta** `set_lights_great_verse`, **theband.dta** `band_play kBandWin kAnimFirst`, and the venue dtas' own (`anim_great`, cymbal/piston tnm resets).
4. **`game_lost`** (arena_game.dta):
   - `song_failed` message, track hidden;
   - **`game set_realtime TRUE`**;
   - hud and score off, crowd_lose;
   - music speed 1→0 over 2000 ms, then paused.

   Also on game_lost: **venue.dta** `set_lights_bad`; **theband.dta**, for each stage char with `animate_char`, `main.drv play kBandLose kAnimNow`; the venue dtas' `anim_bad`.
5. **`game_outro`**:
   - Sent by `GuitarCharacter_v13 @0x284390` while `state == 4` and the main driver's clip has flags & 0xC240000 and has reached its end. arena_game.dta guards it to run once.
   - It runs `set_depth_of_field FALSE`, stop all sfx, `game set_paused TRUE`, hud off, then `goto post_show` (won) or `lose_screen`.

"Realtime" mode keeps the game clock running from the CPU counter: ms, and `ticks = base + Δms/ms_per_beat·480` (`docs/endgame_and_hud.md` §3.1). So **tick-clock shots keep moving after game over at the song's last tempo** [C clock, I applied here]. The arena LOSE shot is the only special shot with `real_time 0`: its 10000-tick duration lasts 10000/480 beats at that tempo.

### 5.2 Camera (`arena/gen/camera.dta`, handler `game_over`) [C]
```
set $camera.bars_left 100            ; downbeat check_shot can't time out
if {char_sys final_win_sequence}     ; FUN_00282818
   eval_shot {random_elem (game get_character == "grim" ? $camedit.FINAL_WIN_GRIM : $camedit.FINAL_WIN)}
elif $arena.game_won
   game delay_task 1750 {eval_shot {random_elem $camedit.WIN}}     ; WIN_CAMERA_DELAY (ms, game panel task)
else
   eval_shot {random_elem $camedit.LOSE}
```
- `eval_shot` evaluates the stored `{arena switch_cam …}` directly (VenueCam `FUN_0016f3b8`) when `cam get_enabled`. It does **not** go through the CameraSystem picker.
  - The native "current shot" (`DAT_002edb78`, used by `beat_check` `FUN_00111aa8`) therefore stays the last regular shot.
  - [I] In theory a later `beat_check` could re-pick a regular shot over the WIN/LOSE shot if that stale shot became invalid (for example, a lose_teleport changes the nearest walk spot onto a `bad_walk_spots` entry, or an excitement drop hits fest's one `low_excitement_ok 0` shot). This is a rare original quirk; do not emulate it.
- `final_win_sequence` (`MyCharSys_v1` → `FUN_00282818`) = `FUN_00282870() && $arena.game_won`.
  - `FUN_00282870` = `$final_win` (debug/cheat var, string 0x340e50) **or** (`campaign final_song` (`FUN_00139568`) && GameConfig(`DAT_002edb88`)+0x90 > 0 [I: career mode / players]).
- `random_elem` over one element: every venue has exactly one WIN, one LOSE and (arena only) one FINAL_WIN / FINAL_WIN_GRIM shot, so the choice is fixed.
- Multiplayer (`small_club_multi`) has no WIN/LOSE groups. `camera_multiplayer.dta` has no game_over handler, so [I] the eval does nothing and the MULTIPLAYER shot stays.
- After game over, `downbeat` still decrements bars_left, but it starts at 100. `one_bar_to` / `excitement` re-picks are not guarded by the script. [I] They practically never fire: on a win the beatmatch is paused and the song is over; on a mid-song lose the music stops within 2 s.

### 5.3 The shots [C, venues/<v>/gen/camera.dta]
`fov 45/45`, `near 10`, `far 10000` unless noted. `start/end` are % of the path. "rt" = real_time 1 (ms), else ticks.

| venue | outcome | name | path (parent / target) | start→end | duration | singer in→out | offset in→out | other |
|---|---|---|---|---|---|---|---|---|
| arena | LOSE | lose01 | Cam_t_p_zoom (focus/focus) | 100→0 | 10000 **ticks** | (0,.4)→(0,.4) | (−250,−100,400)→(−250,−100,300) | ease 0, crowd_region 1 |
| arena | WIN | win01 | Cam_t_p_circle03 (focus/focus) | 60→70 | 10000 rt | 0→0 | (0,−120,30) | ease 1, **dof**, cr 1 |
| arena | FINAL_WIN | final_win01 | Cam_t_p_pan (focus/focus) | 100→0 | 19000 rt | (0,0)→(0,.2) | (−80,190,90)→(0,−200,500) | ease 1, cr 1 |
| arena | FINAL_WIN_GRIM | final_win_grim | Cam_t_p_pan | 100→0 | 19000 rt | (0,0)→(0,.2) | (−380,190,90)→(0,−200,500) | ease 1 |
| basement | LOSE | lose01 | Cam_basement_lose (venue.view / focus) | 0→100 | 10000 rt | (0,.5) | (0,0,0)→(0,0,25) | ease .5, hide_crowd, dof |
| basement | WIN | Win01 | Cam_t_np_circle01 (venue.view / fret20) | 0→25 | 10000 rt | (0,.6) | (0,40,120) | hide_crowd, dof, cr 0 |
| big_club | LOSE | lose01 | Cam_t_p_zoom | 40→60 | 10000 rt | (0,.3) | (−150,0,10)→(−40,50,0) | ease 1, dof |
| big_club | WIN | win01 | Cam_t_p_circle03 | 10→20 | 10000 rt | (0,.4) | (0,60,80) | ease 1, dof |
| fest | LOSE | lose01 | Cam_t_p_zoom | 100→60 | 10000 rt | (0,0)→(0,.5) | (−160,0,−40) | ease 1, dof, hide_crowd |
| fest | WIN | Win01 | Cam_t_p_circle03 | 0→30 | 10000 rt | (0,.5) | (0,0,80) | ease 1, dof, hide_crowd |
| small_club | LOSE | lose01 | Cam_nt_np_circle02 (venue.view / stage_spot_01) | 25→5 | 10000 rt | (−1,−.7)→(−1,−.3) | (0,0,130) | ease .5, dof, cr 0 |
| small_club | WIN | Win01 | Cam_t_np_circle03 (venue.view / focus) | 72→60 | 10000 rt | (−.5,.6) | (20,−15,100)→(20,−15,150) | ease .5, dof |
| theatre | LOSE | lose01 | Cam_t_p_zoom | 40→0 | 10000 rt | (0,.4) | (−180,−300,−30)→(−180,−300,−50) | ease 1 |
| theatre | WIN | Win01 | Cam_t_np_circle02 (venue.view / focus) | 0→40 | 10000 rt | (0,.3) | (−60,350,120) | ease 1 |

- None of these shots is shaky or sets eyes, so the guitarist's eyes use mode 3 (crowd).
- The INTROs (for reference): all are 10000 rt; basement's is shaky.
- Paths without a cam_paths entry (`Cam_t_p_*`) use the band focus, i.e. the filtered guitarist head, for both parent and target. So WIN/LOSE shots follow the win/lose clip's head motion with the cam_filter lag.

### 5.4 Guitarist and band clips at game over

`set_game_over` (`FUN_00284fd8`) [C]:

| outcome | flags (main.drv) | main mode | wings.drv mode (Grim) |
|---|---|---|---|
| final win | kGuitarWinFinal 0x4000000 | 3 kAnimFirst | 1 kAnimReset |
| won | kGuitarWin 0x200000 | 3 kAnimFirst | 1 kAnimReset |
| lost | kGuitarLose 0x40000 | 1 kAnimReset | 3 kAnimFirst |
| lost while walking (`FUN_00197548`) | kGuitarLoseWalk 0x8000000 | 1 kAnimReset | 1 kAnimReset |

After the play it cancels the walk (`FUN_001971e8`), re-syncs the hands (`FUN_002b0890/0868`) and sets state +0x108 = 4.

(`docs/end_of_song.md` gives the main mode the other way round; the decomp is `uVar7 = won ? 3 : 1` for main.drv.)

- **Graph** (`hero_graphs.dta`):
  - From Normal/Extreme/Idle/Bad/Special/WalkStop, kGuitarWin/kGuitarLose use `max_dist 2.2`, `min_dist 1`, `end_dist 3`, `restrict b_start < 3`. So a won game's kAnimFirst waits up to about 2.2 beats for a transition.
  - kGuitarWinFinal has the same values with `restrict b_start < 0.1`.
  - kAnimReset (lose) cuts in at once.
- **Clips** (`.acp` `(end−start)/fps` s, all `kAnimRealTime`). Examples:
  - metal: win 5.6/8.2/5.9, lose 5.3/7.6/9.4 (`metal_lose_01` is also the LoseWalk clip).
  - The other archetypes are similar: win 4.1–9.0 s, lose 5.3–9.4 s.
  - All `*_win_final` clips: 18.33 s. `WIN_FINAL` macro (`config/macros.dta`):
    - enter_event: `char_sys start_final_win_fx` (`FUN_002829d0`: `charsys_god.part` / `charsys_god1.part` / `charsys_god_rays.mesh` on `bone_pelvis` / `bone_spine1`) and `crowd_audio play_stream 1.0 wingame`;
    - at clip frame **20.5**: `fade start_fade 2000 (color 1 1 1)` (white fade).
- So **`game_outro` arrives at roughly clip length (+ ≤2.2 beats when won) after game_over**, and that is how long the WIN/LOSE shot is on screen:
  - WIN: ~3–7 s visible after its 1.75 s delay;
  - LOSE: 5–9.5 s;
  - FINAL: 18.3 s, ending in white.
- **Band** (theband.dta):
  - won: `band_play kBandWin kAnimFirst`;
  - lost: `main.drv play kBandLose kAnimNow` for stage chars where `animate_char` is true;
  - `$theband.game_over = 1` stops changeups.
  - Clips (`*_win`/`*_lose`) are ~10 s.
- **lose_teleport:** arena_game.dta `pre_switch_cam`: `if cam_msg == "lose01" → char_sys lose_teleport` (`FUN_00283610`; details in camera_walk_crowd_lighting.md §2.7).
  - It runs **before** the LOSE shot's focus snap, since pre_switch_cam is the first thing in `FUN_0016f3b8`.
  - [I] The head's world xfm only updates on the next poll, so the focus may snap to the pre-teleport head and then filter over.

### 5.5 Star Power poses (kGuitarSpecial) [C]
- **Trigger:** `player.dta` `start_using` → `{player get guitarist} start_starpower` → `GuitarCharacter_v1` → **`FUN_002858c8`**. It plays `kGuitarSpecial` 0x2000000 with **kAnimFirst (3)** only if:
  - the walk state (`walk+0x54`) is 0 or 3 (not walking),
  - no Special clip has weight > 0 (`FUN_00285888`),
  - and state ≠ 4 (not game over).
- **Graph:** to kGuitarSpecial only from Normal/Extreme/Idle/Bad/WalkStop (**not from kGuitarSolo**, so no Star Power pose while in a solo mood), with `max_dist 4`, `min_dist 1.8`, `end_dist 3`, `restrict b_start < 3`. So the pose starts 1.8–4 beats later.
- **Clips:** `<arch>_special_01/02`, real time, 3.4–7.6 s. `metal_special_01` excludes `small_club_multi`.
- **While Special plays:**
  - mood changes (`FUN_002856a0`) only re-queue (`FUN_002855f8`); the mood clip resumes with `kAnimLongest` afterwards.
  - Camera: shots without kCamNear are invalid (reason 7). `beat_check` re-picks at once to a `flr_near_*` shot (or SOLO_FAR during a solo); SOLO_NEAR is not allowed.
  - Because `downbeat` skips `check_shot` while `in_star_mode`, that shot then holds for the rest of Star Power. The only exception is the `excitement` → `about_to_switch` re-pick on the beat handler.

---

## 6. Our code vs the original

| item | engine (file) | status |
|---|---|---|
| picker, pools, validity, star-mode hold, specialClip Near rule | `engine/src/game/venue_camera.cpp` | done |
| head focus + cam_filter | `VenueCamera::focus()` | done. The fallback (no guitarist) is stateless, so shake can't accumulate there |
| about_to_switch | `downbeat()` | runs on the downbeat; the original runs it on the beat with `$beats_left ≤ 1`, about 1 beat earlier |
| one_bar_to `$camera.beat > 0` guard | `upcomingSection()` | missing (minor) |
| `shaky` | — | **missing**; `fx.rnd_ps2` is not loaded |
| `eyes` | — | **missing** (no eye controller; `scene/character.h:29`) |
| `crowd_region`, `force_cam_facing` | — | **missing** (no 3D crowd members) |
| `force_char_lod` (post_switch_cam `set_min_lod`) | — | missing (minor) |
| default duration 1920 | `Shot::duration = 0` | harmless (every shot sets it) |
| game_over camera (WIN delay 1750 / LOSE / FINAL_WIN, bars_left 100) | — | **missing**; the regular director keeps cutting after game over |
| lose_teleport trigger | `venue_renderer.cpp:170` tests `shotName().find("LOSE")` | **bug**: the shot is `lose01`, and the camera never switches to it anyway |
| guitarist game-over modes | `VenueRenderer::gameOver` | **wrong**: won kAnimNow → kAnimFirst; lost kAnimNow → kAnimReset; WinFinal missing; Grim wings.drv missing |
| band game-over | `gameOver` | ok (kBandWin First / kBandLose Now); lose should skip chars without `animate_char` |
| start_starpower | `VenueRenderer::startStarPower` | the walk-state guard is missing (it must not start while walking) |
| outro timing | `outroDone()` (clip at end, not blending) | ok; also requires the flags & 0xC240000 clip to be the current one |
| WIN_FINAL fx (god rays, white fade at frame 20.5, wingame stream) | — | missing |

---

## 7. Implementation plan

1. **Load `arena/fx.rnd_ps2`** (ark path `../../system/run/arena/gen/fx.rnd_ps2`) as an extra scene in `VenueRenderer` next to campaths, so `anims_->transAt("shaky_cam1.tnm", f, xfm)` works. Fallback: hard-code the 12 keys from §2.1 with Hermite interpolation (the same tangents as `transAt`).

2. **Parse the extra shot keys** in `VenueCamera`: `shaky` (bool), `eyes` (int, default **3**), `crowd_region` (int, default −1), `force_cam_facing`, `force_char_lod`, `guard`. Default `duration` to 1920.

3. **Shaky in `VenueCamera`:**
   - In `switchTo`: `shakeOn_ = shot.shaky; shakeStartMs_ = ms_;` (the real ms clock, even for tick shots).
   - Each `viewProj`:
     ```
     s = shakeOn_ ? sample(shaky_cam1, fmod(ms - shakeStartMs_, 3200)) : 0
     // focus: lerp toward the head as now, then add s scaled to 60 Hz frames
     focus_ = focus_ + (head - focus_)*t60 + s * (dt_ms*0.06)      // [I] frame-rate fix
     cam.pos += s.x*path.right + s.y*path.fwd + s.z*path.up       // S·P, before offsets
     ```
   - Make the no-guitarist fallback stateful as well: `focus_` lerps toward `walk_spot_02 + 60`.

4. **Eyes:**
   - Add an `EyeController` to `Character`, created when the model has `L-eye.mesh`/`R-eye.mesh` under `bone_head` (guitarists; charbase `create_eyes`).
   - Implement modes 0–3 and the charsys.dta constants (§3). Per frame:
     - aim each eye bone at the target;
     - clamp to acos(0.925) around the head's forward;
     - slerp 0.25 per 60 Hz frame (scale by dt).
   - `VenueCamera` exposes `eyesMode()`; `VenueRenderer` calls `guitarist_->setEyes(mode)` on each shot change, including the WIN/LOSE evals.
   - Mode 3 with no 3D crowd members targets (0, 0, 0.7·head.z) as in the original.

5. **Game-over camera:** add `VenueCamera::gameOver(bool won, bool finalWin, bool grim)`:
   ```
   barsLeft_ = 100; ended_ = true;
   if (finalWin && groups_ has FINAL_WIN[_GRIM]) switchTo(random(FINAL_WIN or FINAL_WIN_GRIM));
   else if (won) winAtMs_ = ms_ + 1750;
   else if (groups_ has LOSE) switchTo(random(LOSE));
   ```
   - In `viewProj` (or a new `update(ms)`): `if (winAtMs_ >= 0 && ms_ >= winAtMs_) { switchTo(random(WIN)); winAtMs_ = -1; }`.
   - While `ended_`: make `upcomingSection`, `excitementChanged`/about_to_switch, `downbeat` and `beat` no-ops [I: a safe reading of bars_left=100 + paused beatmatch].
   - The tick clock keeps running from `tempo.msToTick(t)` (the app already advances `t` in real time after `endWallNs_`), which matches realtime mode, so the arena's tick-based LOSE shot moves correctly.

6. **Pre-switch hook / lose_teleport:**
   - Give `VenueCamera` a `std::function<void(const std::string&)> preSwitch`, called at the top of `switchTo` with the shot name.
   - `VenueRenderer` sets it to: `if (name == "lose01") walk_->loseTeleport(guitarist, singerPelvis)`. Remove the `"LOSE"` substring check at `venue_renderer.cpp:170`.
   - Also use the hook (or a post-switch hook) for `hideCrowd`, the crowd region and `force_char_lod`.

7. **`VenueRenderer::gameOver(won, finalWin, grim)`:**
   - main: `finalWin ? kGuitarWinFinal(0x4000000) : won ? kGuitarWin : kGuitarLose`, with mode `won ? kAnimFirst : kAnimReset`; LoseWalk (0x8000000) with kAnimReset when walking.
   - wings.drv (if present): the same flags with mode `won ? kAnimReset : kAnimFirst` (LoseWalk: kAnimReset).
   - Then cancel the walk, set guitarState_ = 4, and call `camera_->gameOver(...)`.
   - Band lose: only members with `animate_char`.
   - `App` computes `finalWin = won && career && campaign.final_song(current)` (`docs/career.md` §1.7), plus `$final_win` from the cheat var. It computes `grim` from the selected character.
   - Make `outroDone()` also require the current clip flags & 0xC240000.

8. **Star Power:** in `startStarPower` add `if (walkingNow()) return;`. The rest already matches (Special guard, state 4 guard, kAnimFirst; specialClip feeds the camera's Near rule).

9. **Small fidelity fixes:**
   - Move about_to_switch to the beat handler (`beats_left ≤ 1`, i.e. the last beat of the bar).
   - Guard `upcomingSection` with "a beat has happened".
   - WIN_FINAL extras: god-rays particles at the start of the clip, a white 2000 ms fade at clip frame 20.5, and the `wingame` crowd stream.

### Key addresses
- `FUN_0016f3b8` VenueCam switch_cam; `FUN_0016e390` per frame; `FUN_0016f248` shake start; `FUN_0016e370` clock select; `FUN_0016ef58` rumble_cam.
- `FUN_001ac468` / `FUN_001ac5a0` / `FUN_001ac4e8` anim-filter time mapping / loop test / length.
- `Character_v5 @0x18d4d8` → `FUN_0018fd10` eye mode; `FUN_0018fa18` eye update; `FUN_00190110` eye aim and clamp; `FUN_0018f8a0` eye config; `FUN_0018dbf0` create_eyes; `FUN_00172cc8` random crowd member.
- `FUN_001727b0` crowd region.
- `FUN_00284fd8` set_game_over; `FUN_00282818` / `FUN_00282870` final_win_sequence; `FUN_002829d0` start_final_win_fx; `FUN_00283610` lose_teleport.
- `FUN_002858c8` start_starpower; `FUN_00285888` Special weight; `FUN_002856a0` / `FUN_002855f8` mood re-queue.
- `FUN_00111aa8` beat_check; `FUN_00110040` shot validity.
