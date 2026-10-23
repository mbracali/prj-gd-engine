# GH1 PS2 venue effects: particles, flares, TRIGGERS, hit_gem reactions

[C] = read from code (EE decomp, VU1 microcode, scene data, scripts). [I] = inference.
Addresses are EE (SLUS_212.24). Related: docs/hit_fx.md §5 (ParticleSys format and simulation),
docs/camera_walk_crowd_lighting.md §4.6–4.7 (first flare/particle survey), FINDINGS.md "Venues", docs/lighting.md.

---------------------------------------------------------------------------------------------------
## 0. Summary

- **Particles.** The engine already loads, simulates and draws every venue ParticleSys that sits in a section
  view's anim list and draw list (AnimPlayer → `particleSetFrame`, `SceneRenderer::drawParticles`).
  What is still missing:
  1. **Preserved particles** (`preserve_particles`): basement christmas lights (656 sprites) and fest `bulbs.part`
     (240). `decodeParticleSys` throws the list away. They draw as a static sprite list and never simulate.
  2. **Colour rule** (VU1 program 0x5cc, §2.3). The particle colour is `rgba × environ ambient` when the mat has
     `use_environ`. The **material colour is not used**, and there is no fog. The engine tints by `mat.color`
     instead, which is wrong. The christmas lights blink only because their environs' ambient is EnvAnim'd.
  3. **Script control.** Most emitters are switched on through `{arena switch_anim[_rt] <x>.partanim …}`. Many of
     those calls sit inside `if`, `delay_task` bodies or funcs with arguments, and the mini-runner skips all of
     them. For example, the arena chorus fire bursts and the fest nuke burst never fire.
- **Flares (RndFlare).** Not loaded at all. A flare is a 2D additive sprite at the projected point. It is
  occlusion-tested with a 1-pixel Z-buffer readback at the end of the frame (one frame of latency). Its
  brightness fades in and out by ±1 step per frame, scaled by distance (§3). The `chorus` handler sets
  `set_steps 0`, so a flare pops on instantly, and hides `flare_obstruct.view`, the Z-only blocker meshes. Then
  `set_steps 180/360` restores a slow fade-out once the blockers come back.
- **TRIGGERS track.** Uses `config/gen/midi_triggers.dta`: note **60 → `kick_drum`, sent 90 ms early**; note
  **61 → `bass_hit`, sent 50 ms early**. Each is sent to the arena as a message with no arguments (§4). 49 of the
  58 songs have the track.
- **hit_gem.** On every hit gem, PlayerMatcher sets the global **`$slot` = the lowest fret of the gem** and sends
  `{arena hit_gem}`. The venues pump speaker cones through `hit_guitar_amp $slot` (§5). This needs funcs with
  parameters, `foreach`, `switch` and `random_int` in the venue runner.

---------------------------------------------------------------------------------------------------
## 1. Per-venue inventory [C data + scripts]

Clocks:
- A section's `<s>.view` runs on song **ticks**, `<s>_rt.view` on **ms**.
- `switch_anim` uses ticks and `switch_anim_rt` uses ms.
- `game anim_task` uses ms.
- `delay_task` uses ticks.

A ParticleSys inherits the clock of the view whose anim list holds it. Its life, emit rate and speed are
therefore **per tick** for every venue system (480 ticks = 1 beat). "Dead" means that, in the shipped data, the
effect never appears.

### arena (`venues/arena/gen/arena.rnd_ps2`, `arena.dta`)

| object | data | driven by |
|---|---|---|
| `fire01`, `fire2`, `fire3`, `fire4` (ParticleSys) | `fire_glow.mat` (Add, use_environ 0). Life 400–1000 ticks. Box ±50/±5/±5. Speed 0.1–0.4. Yaw ±30°. Size 10–20 → 0. Force z −0.0003. Bubble on. Max 50. Rate 0 in the file. | In `venue.view` anims (ticks) and drawn by `venue_transparent.view`. Emit rate comes from `fire1..4.partanim`, which all use `fire1.partanim`'s keys: 0.2 at 0 → 0.1 at 240 → 0 at 480. |
| `fire1..4.partanim` | Also children of `venue.view`. They come after their particle system in the list, so the system emits with the rate the slot set (one frame of lag). | `intro_start`: `switch_anim_rt fireN.partanim (range 480 480)` (off). **`chorus`** handler, only if `$arena.excitement > kExcitementBad`: `switch_anim_rt fireN.partanim (range 0 480) (scale 1) (blend 0)`. That is a 480 ms burst, with `flames.matanim` / `fire_glow.matanim` `(range 1 10) (scale 0.012)` on ms. |
| `stage_flame`, `stage_flame1` | `stage_flame_tex` (Add). Life 20, emit 0.5, speed 2, pitch/yaw ±45°, size 75 → 25, max 40. Own filter ScaleOffset ×0.04, so 1 unit = 25 ticks. | Inside view `stage_flames`, which is both drawn and animated from `venue.view`/`venue_transparent.view`. **Always emitting** (no partanim). |
| `dry_ice1.partanim`, `dry_ice2.partanim` | target `''` | dead |
| Flare7–12 | `flare.mat` (Add, zmode 2, colour 0). Sizes (1,1)/(1,.5)/(.5,.1). Range (0, 3000–5000). Steps 20. | Drawn by `flare_draw.view` (in `venue_transparent.view`). Chorus script: `FLARES = Flare7..12`, `FLARE_UP 0`, `FLARE_DOWN 180`, `delay_task 1920`. |
| Flare1–6 | | Not in any draw list: dead |
| `flare_obstruct.view` | `flare_blocker01..04.mesh`, `flare_blocker.Mat` (SrcAlpha, alpha 0, zmode Normal): Z only | Shown at `intro_start`; hidden for the chorus window |

### basement

| object | data | driven by |
|---|---|---|
| `furnace_sparks.part` | `furnace_glow.mat`. Life 700–800. Emit 30–50 (from its partanim). Max 30. Force z −1e−4. | `venue.view` (ticks). `furnace_sparks.partanim`: 0 at 0, 30–50 at 1. `anim_great` sets `switch_anim_rt … (range 1 1)`; bad/okay set `(range 0 0)`. |
| `washing_machine_bubbles.part` | `bubbles.mat` (use_environ). Emit 10–20 via `soap_bubble.partanim`. | Same pattern as the furnace sparks (great only) |
| `steam01.part`, `steam2..4.part` | `steam.mat` (use_environ). Size 2 → 10–30 or 20 → 30. Steam4 lives up to 1800. | `lighting.view`. `STEAM_ON` / `STEAM_OFF` macros (`steamN.partanim` `(range 1 1)` / `(range 0 0)`, rt), on in `anim_great` |
| `plaster.partanim` | target `''` | dead |
| `lightbulb.part` | `glored.mat`. Life 1500, emit 0.1, max 5. | `lighting.view`, drawn in `lighting_transparent`. `lightbulb.partanim` is `(range 1 1)` only in `set_lights_great_solo`. |
| `christmas_blue/yellow/red01/red02.part` | **preserved**: 213 + 204 + 119 + 120 sprites, size 7, emitter at identity (world positions), `glow_christmas.mat` (Add, **use_environ**) | In no anim list (no simulation). `christmas.view` draws `blue_light.env, christmas_blue.part, …, red01_light.env, christmas_red01.part, …`, so each string takes the ambient of the environ drawn just before it. `full.anim` animates those ambients (`blue_christmas.envanim` etc., on/off every 480–960 ticks), which makes **the lights blink**. |
| `cop_red`, `cop_blue` (Flare) | sizes 0.5 → 10, range (0, 3840), steps 10 | `showing = FALSE` in the file and never shown by script: dead |

### big_club

| object | data | driven by |
|---|---|---|
| `fan_flare` (Flare) | `flare_mat` (SrcAlphaAdd, alpha 0.8, `lightred.tex`). Sizes 1.5/1.5. Range (1,1), so the distance factor is always 1. Steps 10. | In `band_POV_hide.view` (main pass). `anim_great` shows it; `anim_bad`/`anim_okay` hide it. |
| `fogright.part` | mat `''`, so the default mat `DAT_002edd78` | In `venue.view` draws but in no anim list, so it never simulates: dead |
| `fogright1.part`, `fogrleft.part`, `fogrleft1.part` | | Hidden and not animated: dead |

### fest

| object | data | driven by |
|---|---|---|
| `toxic4` | `toxic_bubble` (Add). Life 1000–2000. Size 40–60. Max 30. `toxic4.partanim`: 0.1 at frame ≥ 1. | `venue.view`. `anim_bad`/`anim_okay`: `delay_task 3840 {if {== $reactorState FALSE} {switch_anim toxic4.partanim (range 1 1)}}`. `anim_great` sets `(range 0 0)`. |
| `nuke_toxic` | `sun_flare.mat` (Add). Life 500–1500. Speed 0.5–0.7. Max 400. `nuke_toxic.partanim`: 1/tick from 0 to 450, 0 at 500. | `anim_great`: `delay_task 1200 {if {== $reactorState TRUE} {switch_anim nuke_toxic.partanim (range 0 500)} {switch_anim_rt blast.matanim (range 0 9) (scale .02)}}`. Bad/okay set `(range 500 500)` (off). Also needs `animate_to arena reactor_set.view`. |
| `bulbs.part` (lighting) | **preserved** 240 sprites, size 13, `purpglow.mat` (Add, use_environ **0**, mat colour (1,0,0) **ignored** by the VU) | Static, in `bulbs.view` |

### small_club and small_club_multi

| object | data | driven by |
|---|---|---|
| `ParticleSys1` / `ParticleSys2` (dry ice, lighting) | `dry_icesmokemat.mat` (Add, `part_condensor.tex`). Life 8000–15000 (multi: 15000–35000). Speed 0.005–0.025. Size 2–15 → 5–15. Bubble on. Max 400. | `lighting.view`, drawn in `lighting_transparent`. small_club: **`ParticleSys1` has showing = FALSE**, so only PS2 draws. `dry_ice1.partanim` keys: 0.05 from frame 1 to 7680, then 0 to 15360. `set_lights_great_*` uses `switch_anim dry_iceN.partanim (loop 1 15360)` on ticks, which gives 4 bars on and 4 bars off. All other `set_lights_*` and `intro_start` use `(range 0 0)`. small_club_multi uses `(range 1 1)` in great, a constant 0.05. |
| Flare1/3/5/7/9/11 | `glowwhite_big.mat` (Add, colour 0, `glodrum_green.tex`). Sizes 0.5 → 0.75. Range (0, 10000). Steps 60. | `chorus_flares.view` in `venue_transparent.view`. Chorus: `set_steps 0`, then `delay_task 960`, then `set_steps 180`. Blockers: `flare_obstruct.view` (`flare01..11.mesh`, `flare_transparency`). |

### theatre

| object | data | driven by |
|---|---|---|
| `fire01..fire12` (sparks) | `sparks.mat`. Life 200–500. Emit 2. Speed 0.07–0.4. Pitch ≈ ±75–105°. Max 10. | Partanims hang under `stage.anim`, `stage1.anim` and `stage2.anim`, which `set_lights_great_*` switches with `switch_anim_rt (loop 0 3840)`. `fire01/02/09.partanim` have ScaleOffset +30 and emit for frames 0–100 of each 3.84 s loop. **But all 12 have `showing = FALSE` and nothing shows them.** They simulate but never draw: dead [C]. |
| `ok_chorus01..08.flare` | `glowwhite_big.mat` (SrcAlphaAdd here). Sizes 0.1 → 1.5. Range (0, 2000). Steps 60. | `flare_draw.view` in `lighting_transparent`. Chorus: `set_steps 0`, `delay_task 240`, `set_steps 360`. |

---------------------------------------------------------------------------------------------------
## 2. Particle details that matter for venues

### 2.1 SetFrame and emit gate [C]
`PsParticleSys_v1 @0x1cffc0`:
```
if last != -9999999:
    dt = frame - last
    update(dt)
    if (+0x138 == -9999999):
        emit(dt)
last = frame
```
- +0x138 is the "explicit emit clock". It is used only by fireworks (`FUN_001cf9a0` / `FUN_001d0048`), which are dormant. For venues, emit always runs.

### 2.2 Anim-list order [C data, effect C]
- A section view's SetFrame(ticks) also SetFrames its partanims at the song tick. That usually means a rate of 0
  late in a song.
- The script slots (`FUN_0017a518`) then overwrite the rate.
- In every live case the ParticleSys comes **before** its partanim in the view's anim list. So the system emits
  with the rate the slot left last frame.
- Our order (section views, then slots, in `VenueScript::update`) reproduces this. Keep it.

### 2.3 Draw: VU1 program 0x5cc [C, disassembled from `.vutext`, listing in the scratchpad `all.txt` 0x5cc–0x613]
- MVP = P (0x2a4..7) × view-projection (0x2bc..f).
- **Colour scale** `vf9 = GS scale (0x2b7)`. If `use_environ` (0x2b0.w, branch at 0x5dc), multiply by the
  **environ ambient (0x2a9)** at 0x5de. The material colour 0x2b2 is **never loaded**.
- Per particle:
  - two corners `MVP·pos ∓ vf8·(size/2)`, where vf8 = 0x2bb is the camera's clip-space corner vector [I: content];
  - a GS **SPRITE**, UV (0,0)–(1,1);
  - colour `FTOI0(rgba × vf9)`.
- `FCAND` drops the particle if a corner is clipped.
- `PsParticleSys_t1_v5 @0x1a2fd8`:
  - clears PRIM bit 0x20 (FGE), so there is **no fog**;
  - when a pass's TFX is HIGHLIGHT (pass+0x14 == 2), resets TEX0.TFX to **MODULATE** (`FUN_002eb5d8(…,6,0,0x1800000000)`). Particles are never HIGHLIGHT.
  - A null mat uses `DAT_002edd78`.
- Engine fix: `tint = mat.useEnviron ? currentEnv.ambient : 1`, never `mat.color`, with fog off.

### 2.4 Preserved particles [C]
- Load: `FUN_001d24e0` per particle (`pos3`, `rgba`, `size`) into the live list.
- `FUN_001a2a20` then bakes a static VU packet (+0x2b0, qword count +0x2b4).
- The draw `FUN_001a2df0` sends **only** the baked packet when +0x2b0 ≠ 0.
- The draw is the same 0x5cc sprite: half-size `size·0.5`, colour × ambient if use_environ.
- None of these systems is in an anim list, so no simulation is needed. Positions are in P space; the emitters are identity, so P equals world space.

---------------------------------------------------------------------------------------------------
## 3. RndFlare

### 3.1 Format [C] (`RndFlare_t2_v8 @0x1f4710`, decoder `tools/rnd_classes/material.py decode_flare`, all data v3)
```
u32 version (3)
Transformable              // world pos = owner(+0x10) world +0x90
Drawable                   // showing at drawable+4
v>0:  ref  mat             // +0x10c
v<3:  f32 size             // sizes = (s, s)
else: f32 sizes[2]         // +0x104 s0, +0x108 s1
v>1:  f32 range[2]         // +0x114 r0 ("near"), +0x118 r1 ("far")
      u32 steps            // +0x11c
```
Runtime fields:
- `visible` at +0x100;
- `count` at +0x120, starting at 0 [I].

Vtables:
- RndFlare 0x2fac70 (Drawable slot 5 = draw `RndFlare_v5 @0x1f4d08`);
- 0x2facb0 (Transformable);
- 0x2facd0 (Object: Handle `RndFlare_t2_v1 @0x1f5158`, Print `RndFlare_t2_v3`).

### 3.2 Per draw: `RndFlare_v5 @0x1f4d08` [C]
1. **Queue the visibility test** with `FUN_001d2e38(rnd, worldPos, flare)`.
   - If the camera renders to a texture (cam+0x324), it prints "Flare %s can't be drawn in rendered texture" and sets `visible = 0`.
   - Otherwise `depth = FUN_001b1100(cam, pos, &sx,&sy)`, where depth is view w and sx, sy are in 0..1 over the camera's screen rect.
   - If `near(cam+0x300) ≤ depth ≤ far(cam+0x304)` and `0 ≤ sx,sy < 1`, it queues `{flare, px = sx·W, py = sy·H, z = cam.DepthToZ(depth)}`.
   - Otherwise `visible = 0` immediately.
2. `count = clamp(count + 2·visible − 1, 0, steps)`. `visible` is last frame's readback unless step 1 just cleared it. If `count == 0`, return.
3. `d = FUN_001b1100(...)`. If `d ≤ 0` (behind the camera), set `count = 0` and return.
4. If there is a mat:
   - `t = (r0 == r1) ? 1 : clamp((d − r1)/(r0 − r1), 0, 1)`. All data has r0 = 0, so `t = 1 − d/r1`: full brightness at the camera, 0 at `r1`.
   - `mat.rgb = t·count/steps`, with alpha untouched, and the mat is marked dirty (+0x64 |= 1).
   - The mats' own colour is 0, so this write is the flare's brightness. Every flare writes before its own DrawRect, so flares that share a mat still get their own value.
5. If stage 0 uses texgen 1 (Xfm): stage rows become `R(a)` with `a = sx − 0.5` rad, rotating the UV about (0,0) by the screen x. No shipped flare mat uses texgen 1, so this can be skipped.
6. Size:
   - `s = s0`.
   - If `s1 ≠ s0`: `s = s0 + (s1 − s0)·max(0, −dot(camWorldY, flareWorldY))`. Rows are at +0x70 of each world xfm, and the camera looks along +Y. The flare reaches s1 when its +Y points back at the camera.
7. Rectangle:
   - `w = s·W`; `h = (w·H/W)/0.75` (`PsRnd_v16` returns 0.75). On the 640×448 4:3 screen that is a **square on the display**.
   - `x = sx·W − w/2`, `y = sy·H − h/2`.
   - Drawn with `PsRnd_v8 @0x19b598` (DrawRect) and the flare mat's passes (`FUN_002ebcb0`): blend, zmode, texture with UV 0..1.
   - Vertex colour is `mat.color` because vert_amb is 0 for every flare mat. A HIGHLIGHT pass sets vertex alpha to `0.035·128` (`FUN_00199578`), which adds about +0.035 brightness.
   - Z is the GS maximum (nearest), so the depth test always passes. zmode 2 means no Z write.

### 3.3 Readback: `FUN_0019ae48`, end of frame in `PsRnd_v11` [C]
- For each queued test it reads the Z-buffer pixel at (px, py) and sets `visible = zbuf < z`.
- PS2 Z grows toward the camera, so "visible" means nothing in front of the flare was drawn at that pixel.
- The Z buffer at that point holds the whole frame: venue, crowd, band, and the Z-only `flare_obstruct` meshes. Whether the highway or HUD writes Z there is [I]. Test against the venue pass only.

### 3.4 `set_steps n`: `FUN_001f50e0`, reached from Handle with symbol `set_steps` [C]
```
n = max(n, 1)                        // n < 2 -> 1
if count == steps: count = n
else:              count = count * (int)(n / steps)   // int truncation of the RATIO
steps = n
```
Consequences:
- `set_steps 0`, so steps becomes 1. Unless the flare was fully on, count becomes `count·(int)(1/20) = 0`.
- From then on, one visible frame gives full brightness: the flare **pops** as soon as the blockers go away.
- `set_steps 180/360` after the delay: count is 1 = steps, so count becomes 180 (full).
- The blockers return, so `visible = 0`, and count decays by 1 per frame: a **3 s / 6 s fade-out at 60 fps** [I: 60 Hz frame rate].
- Before the first qualifying chorus, the file steps (20/60) apply and the blockers hide the flares.

### 3.5 Chorus sequence (venue scripts) [C]
```
(chorus {if {> $arena.excitement kExcitementBad}
   {foreach $flare FLARES {$flare set_steps FLARE_UP}}      ; 0
   {flare_obstruct.view set_showing FALSE}
   {arena delay_task D {foreach $flare FLARES {$flare set_steps FLARE_DOWN}}
                        {flare_obstruct.view set_showing TRUE}}  ...})
```

| venue | FLARES | D (ticks) | FLARE_DOWN | extra |
|---|---|---|---|---|
| arena | Flare7..12 | 1920 | 180 | crowd_lightc.litanim (range 50 100), then (range 0 49); fire bursts (§1) |
| small_club | Flare1,3,5,7,9,11 | 960 | 180 | crowd_lightc.litanim (range 0 49) after |
| small_club_multi | same | 960 | 180 | crowd_lightc.litanim (range 50 100), then (0 49) |
| theatre | ok_chorus01 (twice), 02..08 | 240 | 360 | none |

`intro_start` shows `flare_obstruct.view` in every flare venue. `chorus` is the EVENTS text event that the venue runner already receives.

---------------------------------------------------------------------------------------------------
## 4. TRIGGERS track [C]

- **Config.** `config/gen/midi_triggers.dta` is included by `gh.dta` as `(midi_triggers …)`:
  `(triggers (60 kick_drum 90) (61 bass_hit 50))` = (note, message, lead in ms).
  - The SongDB ctor `FUN_0010b7b8` keeps that array at SongDB+0x48.
- **Parse.**
  - `DifficultyParser_v5 @0x262100` maps the track-name meta `TRIGGERS` to state 5. The other names are `T1 GEMS` = 1, `SOLO` = 2, `PITCH` = 3 and `EVENTS` = 4.
  - The MIDI reader `FUN_0020d2b8` turns velocity-0 note-ons into 0x80.
  - The shipped files encode every note-off that way: 0 real 0x80 events, and as many vel-0 events as note-ons.
  - `FUN_00262058` takes **note-on (0x90) only**. If the tick and note equal the previous event, it warns and still forwards. It then calls the sink at vt+0x54.
- **Store.** `SongDB_v9 @0x10bf08(tick, note)`:
  - looks up the note row (warns if missing);
  - computes `adj = MsToTick(TickToMs(tick) − lead)` (tempo map vt+0x14 / vt+0x1c);
  - inserts `(adj, note)` into the sorted vector at SongDB+0x4c..0x58.
- **Dispatch.** `FUN_00106348(game, tick)` runs at the end of `GamePanel_v10 @0x105808` each frame, with the audio-driven song tick. It is skipped in free-run mode, game+0x94.
  ```
  while cursor(+0x1a4) < n && triggers[cursor].tick < nowTick:
      {arena <row[1]>}        ; message with no args: kick_drum / bass_hit
      cursor++
  ```
- **Data.** 49 of the 58 songs have TRIGGERS, with notes 60 and 61 only; for example aceofspades has 541 kicks and 653 bass hits.
- **What the venues do** (`arena add_handlers`):

| venue | kick_drum | bass_hit |
|---|---|---|
| arena | `game anim_task kickdrum01.tnm 80 0 200`, same for kickdrum02 | `hit_speakers (stack_speakers05.tnm stack_speakers06.tnm)` (range rnd 110–130) |
| basement | `anim_task kickdrum01.tnm 80 0 500` | `switch_anim Cylinder02.tnm (range {random_int 110 130} 0) (scale 1) (blend 0)` |
| big_club | `anim_task kickdrum01.tnm 80 0 200` | `hit_speakers (greycone1..5.tnm)` (110–130) |
| fest | `anim_task kickdrum01.tnm 80 0 200` | `hit_speakers (swr_cone1..4.tnm)` (70–100) |
| small_club(_multi) | `anim_task kickdrum01.tnm 80 0 200` | `hit_speakers (speaker1..6.tnm)` (110–130) |
| theatre | `anim_task kickdrum01.tnm 80 0 200` + `anim_task grey_cone_anm.mnm 80 0 120` (a MeshAnim) | `hit_speakers (greycone06..10.tnm)` (150–180) |

- `anim_task a from to ms` sweeps the frame from→to over ms (AnimateTaskPeriod `FUN_001ee988`). kickdrum keys are scale 1.0 at frame 0 and 1.04–1.06 at frame 100, with filter ScaleOffset 0.5 [I: whether anim_task applies the filters]. The result is a bulge that relaxes.
- `hit_speakers`: `foreach $anim $speakers {arena switch_anim $anim (range {random_int lo hi} 0) (scale 1) (blend 0)}`.
  - `FUN_0017a1d0` sorts the range, so the anim plays forward from frame 0 to R on ticks (raw frames: slots skip the top-level filters) and then **holds at R**.
  - The cone TransAnims push out by frames 20–30 and are back by about 90–100, so R = 110–180 is one full pump.
- The engine's `switchAnim` reads `{random_int …}` as 0 today, giving range 0..0, so nothing moves.

---------------------------------------------------------------------------------------------------
## 5. hit_gem reactions [C]

`PlayerMatcher_v3 @0x108a90`, after the beat-match `hit` message:
- `$slot` (symbol "slot" at 0x2ff2d8) = the index of the **lowest set bit** of the gem's fret byte (gem+0xc), so a chord gives its lowest fret.
- Then it sends the static message `{hit_gem}` (0x2ff2e0) to the arena (`Arena_v1`, handle 0x363748).
- It fires once per hit gem, for HOPOs too, and for every player in multiplayer [I].

Venue handlers are all `(hit_gem {hit_guitar_amp $slot})`, except basement, which has none.

| venue | hit_speakers (R) | hit_speakers_lite (R) | sets per slot 0..4 |
|---|---|---|---|
| arena | 110–130 | none | ignores slot: always `stack_speakers.tnm`, `stack_speakers02.tnm` |
| big_club | 110–130 | 25–45 | full: Cylinder40/42/53, 41/43/51, 42/53/52, 43/50/51, 53/51/52/43; lite: the other Cylinder4x/5x |
| fest | 70–100 | 25–45 | full: cone1/2, 2/3, 1/4, 3/4, 3/4; lite: cone3/4, 1/4, 2/3, 1/2, 1/2 |
| small_club(_multi) | 110–130 | 25–45 | full: tan_cone5/4, 6/3, 3/4, 2/3/6, 1/4/5; lite: the other tan_cones |
| theatre | 150–180 | 34–55 | full: greycone5/4, 5/3, 3/4, 3/2, 4/3; lite: greycone1/2/3, 1/2/4, 5/1/2, 1/4/5, 1/2/5 |

- "lite" ranges stop at 25–55, which is about the peak excursion. Those cones therefore **stay pushed out** until another hit replays them [C data effect].
- Blend is 0, so there is no crossfade.
- Every listed .tnm is unhooked from its view at `finish_loading`, which the engine already does.

Other GH1 hit_gem consumers: the GuitarCharacter "hitting" flag (the engine has it) and the track smash effects (docs/hit_fx.md).

---------------------------------------------------------------------------------------------------
## 6. Implementation plan for gd01

### 6.1 Venue script runner (prerequisite for everything script-driven)
The mini-runner in `engine/src/game/venue_script.cpp` cannot evaluate `if`, `foreach`, `switch`, `random_int`,
variables, funcs with parameters or `delay_task` bodies that contain them. Recommended: run venue scripts on the existing
`Interp` (`engine/src/script/script.h`), which already has `func` with params, `if`/`if_else`, `switch`,
`foreach`, `random_int` ([lo, hi)), `set`, comparisons and macros.
- `VenueScript` owns a private `Interp` plus a `ScriptLoader`, so `kExcitement*` from `config/macros.dta` resolves.
- It loads the venue `.dta` through the loader and evaluates the top level, which defines funcs and runs `arena add_handlers` / `load_section`.
- A `VenueArenaObj : ScriptObject("arena")` provides the natives:
  - `switch_anim` / `switch_anim_rt`: move today's `switchAnim` and take evaluated `Value` args;
  - `delay_task` (lazy, ticks);
  - `set_singer_env`, `load_section`;
  - `cam_msg`, which returns the current shot name and is needed by `post_switch_cam`;
  - handlers through `ScriptObject::addHandlers`.
- `game` provides `anim_task`.
- `animate_to` (fest `reactor_set.view`) is a top-level `{animate_to arena <anim> <frame> <ms>}`. Add it as a builtin: an anim task from the current frame [I].
- A resolver maps any scene-object name to a `SceneObjProxy` with these natives:
  - `set_showing`, `set_frame`, `remove_anim`, `add_trans`, `unhook_anim_parents`;
  - **`set_steps`**, forwarded to the FlareObj.
- Variables: `$arena.excitement`, `$arena.last_excitement`, `$song_pos`, `$lights_need_update`, `$arena.game_won`, `$slot`.
  - Then run `arena/gen/venue.dta` itself instead of the hand-coded rules in `message()` / `lightsPerExcitement()`.
- Also send `init` before `finish_loading`. fest unhooks its anims in `(init …)`, and today that is skipped [I: order].

### 6.2 TRIGGERS
- `game/chart.h`: add `struct SongTrigger { float ms; int tick; std::string name; }` and `std::vector<SongTrigger> triggers` to the song.
- `game/chart.cpp`, after the EVENTS parse:
  - find the `TRIGGERS` track;
  - for each `isNoteOn()`: skip a duplicate (same tick and note as the previous one); look the note up in `midi_triggers` (load `config/gen/midi_triggers.dta` once, or hard-code 60 → (kick_drum, 90) and 61 → (bass_hit, 50));
  - `tick' = tempo.msToTick(tempo.tickToMs(tick) − lead)`;
  - `ms = tempo.tickToMs(tick')`;
  - sort by tick.
- `app.cpp` `App::updateVenue`:
  - keep a cursor `venueTrigger_`, reset where `venueEvent_` resets;
  - `while (cursor < n && triggers[cursor].tick < f.ticks) f.events.push_back(name)`, strict `<` [C];
  - `VenueScript::update` already turns `f.events` into `message(name)`.

### 6.3 hit_gem
- `app.cpp`, in the `PlayerEvent::Kind::Hit` branch: `if (venue_) venue_->hitGem(std::countr_zero(unsigned(g.frets)));`
- `VenueRenderer::hitGem(int slot)` calls `script_->hitGem(slot)`, which sets `$slot = slot` and sends `message("hit_gem")`.
- Requires §6.1.

### 6.4 Flares
- `scene/scene.h`: add a `FlareObj { Transformable trans; Drawable draw; std::string mat; float sizes[2], range[2]; int steps; int count = 0; bool visible = false; bool queued = false; float px, py, depthNdc; }` and `std::unique_ptr<FlareObj> flare` in `SceneObject`. Add it to `transformable()` and `drawable()`.
- `scene/scene.cpp`: add `decodeFlare(Rec&)`, version ≤ 3, following `decode_flare`, and an `o.type == "Flare"` branch. Then `updateTransforms` gives it a world position like any Transformable.
- `FlareObj::setSteps(int n)`: §3.4 exactly, including `(int)(n/steps)`.
- `SceneRenderer::drawObject`: `if (o.flare) { if (o.flare->draw.showing) drawFlare(o, viewProj); return; }`. `drawFlare`:
  1. Project the world position.
     - Behind the camera, outside near/far, or off-screen: `visible = false`, nothing queued.
     - Otherwise queue {flare, pixel, NDC depth} into `SceneRenderer::flareQueries_`.
  2. Step `count` at a fixed 60 Hz.
     - Keep `accum += dt·60` and apply `count = clamp(count + 2·visible − 1, 0, steps)` once per whole step.
     - That needs a `dt` passed through `SceneRenderer`, or a per-frame `tickFlares(dt)` called by VenueRenderer before drawing.
  3. If count > 0 and the view depth > 0:
     - `t = r0 == r1 ? 1 : clamp((d − r1)/(r0 − r1), 0, 1)`;
     - colour rgb = `t·count/steps`, alpha = mat alpha;
     - size from sizes and the camera/flare +Y dot;
     - draw a screen-space quad of side `s·(4/3)·viewportH` [I: the PS2 square-on-4:3 rule kept for widescreen];
     - use the mat's blend and stage-0 texture, depth test **off**, no depth write.
- `VenueRenderer::draw`:
  - After the transparent views are drawn (venue pass complete, before the highway and HUD), resolve the queries.
  - Read the depth at each queued pixel, either with a `Renderer::readDepth(x, y)` built on `glReadPixels(GL_DEPTH_COMPONENT)` (fewer than 15 per frame), or with occlusion queries on a 1-pixel point.
  - Set `visible = sceneDepth >= flareDepth − ε`, i.e. nothing nearer.
  - Use the result next frame (PS2 latency).
- Verify that the `flare_obstruct.view` meshes (alpha 0, SrcAlpha, zmode Normal) **write depth** in our renderer even though they are invisible: no alpha-0 or empty-texture early-out.
- Script: `set_steps` through §6.1. Without the full runner, a stop-gap is to special-case the `chorus` handler: check `excitement > Bad`, then do `set_steps`, set showing on `flare_obstruct.view`, and use a `delay_task`.

### 6.5 Particles (engine/src/scene, render)
1. `scene.cpp decodeParticleSys`: keep the preserved list (`struct Static { pos3, rgba, size }`) in `ParticleSysObj::preserved` instead of skipping it.
2. `SceneRenderer::drawParticles`:
   - if `!p.preserved.empty()`, draw those (cached mesh, built once) and **not** `live`;
   - remove the `p.live.empty()` early-out for that case.
3. `drawParticles` colour:
   - `st.tint = (mat && mat.useEnviron && env_) ? env_->ambient : {1,1,1,1}`;
   - do not use `mat.color`; disable fog for particles; no HIGHLIGHT;
   - keep `depthTest` from zmode and blend from `blendFor(mat.blend)`;
   - with no mat, use a white untextured Add [I: `DAT_002edd78`].
   - Check that hit_fx smash mats (`smash_*`, `fuse_burn*` colour 1) are unaffected.
   - `env_` is already the environ selected by the last Environ in the draw walk, which gives the christmas per-string colours.
4. Optional: drop a particle when its screen quad crosses the frustum (`FCAND`), to match PS2 edge pops.
5. Units need no change. Venue emitters run on the section view's tick frame, which AnimPlayer already passes.

### 6.6 What this unlocks, per venue
- **arena:**
  - chorus fire bursts (needs §6.1 `if`);
  - 6 flares and blockers (§6.4);
  - speaker pumps on hit and bass hit, kick drum (§6.2, §6.3).
  - The stage flames should already show; verify.
- **basement:**
  - blinking christmas lights (§6.5.1–3);
  - steam, sparks and bubbles at Great (funcs without arguments already work);
  - lightbulb in great solo;
  - kick and bass_hit.
- **big_club:** fan_flare at Great; pumps; kick.
- **fest:**
  - 240 bulbs;
  - toxic bubbles and the nuke burst (needs `delay_task` + `if` + `set` + `animate_to`);
  - pumps; kick.
- **small_club(_multi):**
  - dry ice at Great (already plays if the transforms are right);
  - chorus flares; pumps; kick.
- **theatre:**
  - 8 chorus flares;
  - pumps, including `grey_cone_anm.mnm` on the kick.
  - The spark systems are hidden in the original. Do not show them.

### 6.7 Address index
| what | address |
|---|---|
| Flare draw | RndFlare_v5 0x1f4d08 |
| Flare load / handle / set_steps | RndFlare_t2_v8 0x1f4710 / RndFlare_t2_v1 0x1f5158 / FUN_001f50e0 |
| Flare query / readback | FUN_001d2e38 / FUN_0019ae48 (in PsRnd_v11) |
| Project to screen | FUN_001b1100 |
| DrawRect | PsRnd_v8 0x19b598; colour FUN_00199578; aspect PsRnd_v16 = 0.75 |
| Particle SetFrame / update / emit | PsParticleSys_v1 0x1cffc0 / FUN_001d0a60 / FUN_001d0060 |
| Particle draw / baked static list | PsParticleSys_t1_v5 0x1a2fd8, FUN_001a2df0 / FUN_001a2a20, FUN_001d24e0 |
| Particle VU1 program | 0x5cc (`.vutext`) |
| Mat pass setup (TFX, HIGHLIGHT) | FUN_002ebcb0, FUN_0019d3b8 |
| Draw walk (showing + sphere cull) | FUN_002ec850 |
| TRIGGERS track / note sink / store / dispatch | DifficultyParser_v5 0x262100 / FUN_00262058 / SongDB_v9 0x10bf08 / FUN_00106348 (from GamePanel_v10 0x105808) |
| vel-0 note-on → note-off | FUN_0020d2b8 |
| midi_triggers config | SongDB ctor FUN_0010b7b8 (strings 0x2ffa48 / 0x2ffa58) |
| hit_gem + $slot | PlayerMatcher_v3 0x108a90 |
| switch_anim slot | FUN_0017a1d0 / FUN_0017a0b0 / FUN_0017a518 |
