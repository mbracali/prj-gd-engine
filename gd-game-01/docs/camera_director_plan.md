# GH1 PS2 RE: the camera director (CameraSystem shot picker) and how it compares with `venue_camera.cpp`

Marks: [C] = read in decomp/disassembly/data; [I] = inference. Addresses are EE virtual addresses (SLUS_212.24).
Related docs:
- `docs/camera_walk_crowd_lighting.md` §1: VenueCam math and the focus head. Its §1.3 is a short version of this doc.
- `docs/camera_endgame.md`: switch_cam keys, shaky, eyes, and the WIN/LOSE/FINAL_WIN shots.
- `docs/endgame_and_hud.md` §3: the song clock and the intro.

This doc covers the part that **chooses** shots. How a chosen shot is rendered (switch_cam / VenueCam) is only summarised here (§5).

---

## 0. Summary

- The director has two layers:
  - **Script** (`arena/gen/camera.dta`): decides **when** to cut and which kind of shot (regular or solo). Cuts come only from the `beat`, `downbeat` and `one_bar_to` messages, `intro_start`/`intro_skip` and `game_over`.
  - **Native `CameraSystem`** (vtable 0x2f6b68, handlers `CameraSystem_v1 @0x111b20`, script object `cam`): decides **which** shot. It uses flags, a freshness bit per shot, validity rules (walk, star power, excitement), side/distance rules against the previous shot, and a weighted random draw over pools.
- Shot length is counted in **downbeats** (`$camera.bars_left`). It is set to `random_int(lo, hi)` from the venue's `camera_durations[excitement]`. **`random_int` excludes the upper bound** (`lo + rand % (hi−lo)`), so `(3 6)` gives 3, 4 or 5 bars. Our engine includes it (bug).
- There are no blends between shots. Every switch is a hard cut: `arena set_fade 0` at init, so VenueCam+0x34 = 0 and `FUN_00167eb8` is never called in-game.
- RNG: one global HMX lagged-XOR generator (`0x422638`, about R250). It is seeded at boot from the RTC as `sec + 60·min + 3600·hour`, and shared with every other `random_*` user. The sequences cannot be reproduced; only the distributions matter.
- Our `VenueCamera` already ports most of this (flags, pools, freshness, validity, side/distance). The remaining differences:
  - the duration range (off by one);
  - star mode stops the bar countdown;
  - the excitement re-pick lands on the wrong beat;
  - message order (one_bar_to before downbeat);
  - `$camera.beat > 0` gate missing;
  - "current shot" includes the INTRO/WIN/LOSE shots, and `intro_camera_flags` is ignored;
  - freshness is re-marked over too many shots;
  - `update_crowd` runs on the wrong shots;
  - `reduced_crowd_shots` is missing;
  - the RNG.

  §6 lists each difference with the fix.

---

## 1. Data [C]

### 1.1 Flags (`config/gen/macros.dta`)
`kCamLeft 1, kCamRight 2, kCamCloseup 4, kCamNear 8, kCamFar 16, kCamBehind 32, kCamSolo 64, kCamNoSolo 128, kCamSpecial 256`.

### 1.2 Groups and pools (`arena/gen/camera.dta`)

| group (`$camedit.<g>`) | flags | pool (weight) |
|---|---|---|
| flr_near_lft / flr_near_rt | NoSolo, Near, Left / Right (0x89 / 0x8a) | flr_near (0.5) |
| flr_far_lft / flr_far_rt | NoSolo, Far, Left / Right (0x91 / 0x92) | flr_far (0.5) |
| band_POV | NoSolo, Behind (0xa0) | band (0.2) |
| balcony_lft / balcony_rt | NoSolo, Far, Left / Right (0x91 / 0x92) | balcony (0.3) |
| SOLO_NEAR | Solo, Closeup (0x44) | solo (1.0) |
| SOLO_FAR | Solo, Near (0x48) | solo (1.0) |
| LOSE, WIN, INTRO | Special (0x100) | — (not in any pool) |

- Pool order matters for the cumulative table: flr_near, flr_far, band, balcony, solo.
- The groups `SINGER`, `TUTORIAL`, `MULTIPLAYER*`, `FINAL_WIN*` and `guitar_god` are not in `$camera.groups`. They are only ever run with `eval_shot` or `eval`.
- Shots per pool per venue are at most 15 (fest flr_near: 7 + 7). The native cap of 32 candidates per list therefore never applies.

### 1.3 Per-venue (`arena/gen/venues.dta`)

| venue | camera_durations Boot / Bad / Okay / Great / Peak (`random_int` args) | real bars | intro_camera_flags |
|---|---|---|---|
| basement, small_club, small_club_multi | 3 6 / 3 6 / 3 5 / 3 4 / 3 4 | 3–5 / 3–5 / 3–4 / 3 / 3 | Near, Right (multi: 0) |
| big_club, fest | 3 6 / 3 6 / 3 5 / 2 4 / 2 4 | 3–5 / 3–5 / 3–4 / 2–3 / 2–3 | big_club Near, Right; fest Near, Left |
| theatre | 3 5 / 3 5 / 3 4 / 2 3 / 2 3 | 3–4 / 3–4 / **3** / **2** / 2 | Far, Right |
| arena | 3 4 / 3 4 / 2 4 / 1 3 / 1 3 | **3** / 3 / 2–3 / **1–2** / 1–2 | Near, Left |

- `camera_durations` is looked up **by key** (`find … camera_durations $arena.excitement`). The keys are `kExcitementBoot..Peak` = 0..4.
- `reduced_crowd_shots`:
  - big_club: flr_far_lft02 0.8, flr_far_lft03 0.9, flr_far_rt02 0.9;
  - fest: flr_far_lft03 0.8, flr_far_lft05 0.85, flr_far_rt01x2w 0.9, flr_far_rt03 0.8.

  They are read by crowd.dta `set_crowd_sizes`, not by the picker (§4.4).

### 1.4 Per-shot keys that the **picker** reads
The picker reads these keys from the shot's `{arena switch_cam …}` command array (`FUN_0022db10` = find with default):

| key | default | used by |
|---|---|---|
| `walk_ok` | **1** (key 0x300648) | rule 3 |
| `bad_walk_spots (list)` | absent = no restriction (0x300650) | rule 4; walk spot indices are 0-based, hub included |
| `low_excitement_ok` | **1** (0x300660) | rule 8; only fest has a shot with `0` |

All other keys belong to switch_cam (camera_endgame.md §1).

---

## 2. Native CameraSystem [C]

### 2.1 Objects

**CameraSystem** (ctor `FUN_00110dc8`, dtor `CameraSystem_v0 @0x110e28`, `CameraSystem_v3` registers the object name 0x300678 = `cam` [I name]):

| off | field |
|---|---|
| +0x08..+0x14 | `vector<CameraGroup>` (0x14 bytes each) |
| +0x18..+0x24 | `vector<CameraPool>` (0x18 bytes each) |
| +0x28 | −1 (set by reset; unused by the picker) |
| +0x2c | enabled (1; `toggle_enabled` / `get_enabled`) |
| +0x30 | last current (debug; written when a pick fails) |

**CameraGroup** (`FUN_001104a0`): `{+0 name sym, +4/+8/+0x10 vector<CameraShot>}`.

**CameraShot** (0x18 bytes, `FUN_00110550`):

| off | field |
|---|---|
| +0x00 | DataNode, the `{arena switch_cam …}` command |
| +0x08 | group name |
| +0x0c | **flags** (the group's int) |
| +0x10 | **fresh** = 1 |
| +0x14 | **reason** = 0 (last rejection code, debug) |

**CameraPool** (`FUN_001108c8`): `{+0 name, +4 weight (float), +8/+0xc/+0x14 vector<CameraShot*>}`. `add_pool` (`FUN_001110b0`) looks up each named group (`FUN_00111740`, linear search by name) and appends a pointer to **each shot** of the group (`FUN_001108f8` → `FUN_00110b90`).

**Globals:**

| addr | meaning |
|---|---|
| `DAT_002edb78` | **current shot**: the last shot chosen *by the picker* |
| `DAT_002edb7c` | `intro_camera_flags` of the venue |
| `0x344590` | the chooser `{vector<{shot*, cumWeight}>, +0x10 total}` (static init `FUN_00111e28`) |

### 2.2 Handlers (`CameraSystem_v1 @0x111b20`)

| message | code | effect |
|---|---|---|
| `reset` | `FUN_00111030` | current = NULL; `intro_camera_flags` = `{venue_data}` (`DAT_003637b4`) elem 1; +0x28 = −1; +0x30 = 0 |
| `add_pool (name w (groups))` | `FUN_001110b0` | see above |
| `change_to_regular_shot` | `FUN_00111a68` → `Pick(0x80)` | |
| `change_to_solo_shot` | `FUN_00111a88` → `Pick(0x40)` | |
| `make_group (name shots flags)` | `FUN_001116c0` → `FUN_00111398` | |
| `inited` | | groups vector non-empty |
| `beat_check` | `FUN_00111aa8` | `if current && !Valid(current)` → `Pick($camera.solo ? 0x40 : 0x80)` (data var `camera.solo`, 0x3006f8) |
| `toggle_enabled` / `get_enabled` | +0x2c ^= 1 / read | |
| other | | returns kDataUnhandled |

The `arena init` handler (camera.dta) runs `setup_groups` and `setup_pools` (once, guarded by `cam inited`), then `cam reset` on every arena init.

### 2.3 Pick(wanted) — `FUN_001118b8` / `FUN_001117a0` (exact)

```
Pick(wanted):
  chooser.clear()                                     // FUN_001101e8(0x344590)
  for pool in pools (data order):                     // FUN_001109b0(pool, chooser, wanted)
      if every shot in pool has fresh == 0:           // FUN_00110cd8 / d10 / d60: whole-pool refresh,
          for s in pool: s.fresh = 1                  //   independent of `wanted`
      A = []; B = []                                  // fresh / used candidates, max 32 each
      for s in pool (pool order = group order, then data order):
          if (s.flags & wanted) != wanted: continue
          if !Compatible(s): continue                 // FUN_00110128 (includes Valid)
          if s.fresh: A.push(s)   (stop scanning at 32)
          else:       B.push(s)   (ignored past 32)
      if A.empty() and !B.empty():
          for s in B: s.fresh = 1; A = B              // only the valid used ones are re-freshed
      for s in A: chooser.add(s, pool.weight / |A|)   // FUN_00110228: total += w; push (s, total)
  // FUN_00110410:
  r = RandomFloat(0, chooser.total)                   // FUN_0024a218
  chosen = first entry with r <= entry.cum            // NULL if the chooser is empty
  chooser.clear()
  if chosen:
      chosen.fresh = 0; current = chosen              // FUN_00111880
      execute {eval_shot <chosen.command>}            // script func; Command built at FUN_001118b8
  else:
      this+0x30 = current                             // "Couldn't find shot with flags %d" (0x3006c0) is not printed in the retail build
```

- Each pool that has at least one candidate contributes its **full weight**, split evenly among its candidates. A pool with no candidate contributes nothing. Example (regular pick, all pools have candidates): flr_near 0.5/1.5 = 33 %, flr_far 33 %, band 13 %, balcony 20 %.
- The weighted draw uses `r <= cum`. `RandomFloat` has 16-bit resolution (§3).

### 2.4 Compatible(s) — `FUN_00110128`; Valid(s) — `FUN_00110040`

Rejection codes, which index the strings 0x300558…0x300630:

```
Compatible(s):
  if s == current: reason 1 "shot same as current"; return false
  if !Valid(s): return false
  // side, FUN_0010ff60(current, s, 3); a NULL current uses intro_camera_flags:
  c = current ? current.flags : intro_camera_flags
  if (c & 3) != 0 and (c & 3) == (s.flags & 3): reason 5 "shot from wrong direction"; return false
  // distance, FUN_0010ffd0: skipped when current == NULL
  if current:
      a = current.flags & 0x3c; b = s.flags & 0x3c
      if a != 0 and ((a & b & 0x34) != 0 or (a | b) == 0x30):
          reason 6 "shot at wrong distance"; return false
  return true

Valid(s):
  if s.flags & 0x100: reason 2 "shot is special"
  elif IsWalking(guitarist0) and !s.walk_ok: reason 3 "shot not okay during walk"
          // FUN_0010feb0 → FUN_00197548(char+0xdc)
  elif NearestWalkSpot(guitarist0) in s.bad_walk_spots: reason 4 "…current walk spot"
          // FUN_0010ff00 → FUN_0018ef80; this rule does NOT depend on walking
  elif SpecialWeight(guitarist0) > 0 and !(s.flags & kCamNear): reason 7 "shot too far for star power"
          // FUN_0010fed8 → FUN_00285888: blend weight of 0x2000000 (kGuitarSpecial) clips > 0
  elif excitement(DAT_00363780) != 3 and !s.low_excitement_ok: reason 8 "excitement too low"
          // `!= 3`: Peak (4) also counts as "low"
  else valid
```

`guitarist0` = `FUN_002827e8(0x42b9f8, 0)` (MyCharSys player 0).

What these rules imply:
- **Side:** a shot never follows a shot from the same side (Left→Left, Right→Right). Shots with no side bit (band_POV, SOLO_*) are always fine.
- **Distance:** these pairs are banned: Closeup→Closeup, Far→Far, Behind→Behind, Far↔Behind. Near→Near is allowed.
  - So flr_far and balcony never follow each other, and never follow band_POV.
  - SOLO_NEAR never follows SOLO_NEAR.
  - In a solo the camera goes SOLO_NEAR → SOLO_FAR → (SOLO_FAR other | SOLO_NEAR) …
- **First pick of a song** (current is NULL after `reset`): only the side rule applies, against `intro_camera_flags`. For example, in the basement (Near|Right) the first regular shot is never a Right shot. The INTRO shot itself never becomes "current", because it is run with `eval_shot` directly.
- **Star Power special clip:** only Near shots qualify (flr_near_*, SOLO_FAR). In a solo that means SOLO_FAR only.

### 2.5 beat_check
- It runs on every `beat` message.
- It re-picks as soon as the **picker's** current shot fails `Valid`, for example when a walk starts on a walk_ok 0 shot, the guitarist reaches a bad walk spot, or a Star Power clip begins on a non-Near shot.
- It does **not** touch `bars_left`, and does **not** run `update_crowd`.
- With current = NULL (during the intro) it does nothing.

---

## 3. RNG [C]

- **State:** `0x422638`: `{+0 i, +4 j, +8 uint32 t[256]}`.
- **Seed** `FUN_0024a028(seed)`:
  - for k in 0..255: `x1 = seed·0x41c64e6d + 0x3039; seed = x1·0x41c64e6d + 0x3039; t[k] = (x1 >> 16) | (seed & 0x7fff0000)`;
  - then `i = 0`, `j = 0x67` (103).
- **Next** `FUN_0024a140`:
  - `v = t[i] ^ t[j]; t[i] = v; i = (i+1 > 248 ? 0 : i+1); j = (j+1 > 248 ? 0 : j+1); return v`.
  - The ring is 249 words, although 256 are filled.
- **Derived functions:**
  - `RandomInt(a, b)` `FUN_0024a1d0` = `a + (int)next % (b − a)`. The upper bound is **exclusive**. It traps when a == b.
  - `RandomFloat01` `FUN_0024a1f8` = `(next & 0xffff) / 65536`.
  - `RandomFloat(a, b)` `FUN_0024a218` = `a + RandomFloat01·(b − a)`.
- **Script commands** (`FUN_00234c98` table):
  - `random_int` → `FUN_00233728` = RandomInt(a, b);
  - `random_float` → `FUN_00233798`;
  - `random_elem` → `FUN_00233828` = elem `RandomInt(0, size)`.
- **Seeding:**
  - Static init `FUN_0024a238` → seed 0x29a (666).
  - Boot (`FUN_00223f08`) reseeds with `b0 + 60·b1 + 3600·b2` from the clock read by `FUN_0022a278` [C formula; I that b0..b2 are sec/min/hour].
  - The same generator feeds crowd, walks, script `random_*` and others. The camera sequence therefore depends on the boot time and on everything else that draws numbers.

---

## 4. Script timing [C] (`arena/gen/camera.dta`)

### 4.1 Messages and their order

The BeatMatch beat tracker (`BeatMatch_v1 @0x10e458`) runs once per frame. Its input is `{tick, measure, beatInMeasure (0-based), tickInBeat, beatsPerMeasure}` (`FUN_00239718`). Each frame it does, in this order:

1. If `beatInMeasure` changed: set `$beat = tick/480` and `$beats_left = (beatInMeasure == 0 ? 0 : beatsPerMeasure − beatInMeasure)`, then send **`beat`**.
   - So `$beats_left` = 3, 2, 1 on beats 2–4 of a 4/4 bar, and **0 on the downbeat**.
2. If `measure` changed: set `$measure`; if measure == 2 send `music_start`; send **`downbeat`**.
3. `upcoming_beat` / `upcoming_downbeat` (480 ticks early; not used by the camera).
4. `FUN_0010f700`: send **`one_bar_to`** (with `$upcoming_section`) for every song event with `tick ≤ now + 1920` whose name is exactly `verse`, `chorus` or `solo` (strcmp). The cursor +0x8c advances over all events.
   - The lead is a fixed **1920 ticks**, not one measure.

Other messages:
- `excitement` comes from `FUN_0016a3c8`, sent only when the level (`arena+0x38`) changes.
- `intro_start` / `intro_skip` come from `FUN_00105b80`, at song time −D, where D = `$camedit.INTRO[0]` duration = 10000 ms.
- `game_over` comes from `FUN_0016a640`.

### 4.2 Handlers (verbatim logic)

```
intro_start:  reset_camera (beat 0, solo F, about_to_switch F, shot NULL)
              bars_left = 6; eval_shot INTRO[0]           // not through the picker
intro_skip:   reset_camera; bars_left = 0; check_shot       // immediate picker shot
beat:         $camera.beat = $beat
              cam beat_check
              if about_to_switch and $beats_left <= 1:
                  about_to_switch = F; bars_left = get_shot_duration(); pick_shot
downbeat:     if bars_left: bars_left -= 1                  // ALWAYS, also in star mode
              if !player0.in_star_mode: check_shot
check_shot:   if bars_left <= 0: bars_left = get_shot_duration(); pick_shot
excitement:   about_to_switch = T
one_bar_to:   if $camera.beat > 0:
                  $camera.solo = (section == solo)           // verse/chorus → F
                  bars_left = get_shot_duration()
                  pick_regular_shot                          // ALWAYS regular, even before a solo
game_over:    bars_left = 100; FINAL_WIN[_GRIM] now | WIN after 1750 ms (game delay_task) | LOSE now
pick_shot:    if !stare_at_singer: ($camera.solo ? pick_solo_shot : pick_regular_shot)
pick_*_shot:  cam change_to_*_shot; update_crowd             // update_crowd even if no shot was found
get_shot_duration: random_int(camera_durations[$arena.excitement])   // [lo, hi)
eval_shot($s): if cam get_enabled: eval $s (→ arena switch_cam …); $camera.shot = $s
```

### 4.3 Consequences
- **Cuts are on bar lines.** Timeouts happen on downbeats. one_bar_to happens 1920 ticks before a section, which is a bar line in 4/4. beat_check cuts can happen on any beat. Excitement cuts happen on the last beat of a bar (beats_left 1) or on the downbeat, whichever is the first `beat` after the change.
- **The intro shot** (10 s, real time) is held until the 6th downbeat after the music starts, unless a `one_bar_to` comes first.
  - one_bar_to is ignored while `$camera.beat == 0`, i.e. before the first `beat` message with tick ≥ 480 [I that the first message is at beat 1].
- **Duration of a one_bar_to shot.** If a downbeat and one_bar_to land in the same frame, the downbeat runs first. It may time out and pick a shot, which becomes "current" for the rules and is marked used. Then one_bar_to picks again and sets bars_left = N. Shots set at one_bar_to therefore last **N full bars**.
- **Excitement re-pick on a last beat** with duration N: the next downbeat decrements, so the shot lasts N−1 bars + 1 beat.
  - In the arena at Great, N can be 1. The re-pick then runs on the downbeat (beats_left 0), and the downbeat handler decrements to 0 and picks **again** in the same frame.
- **Solos:**
  - one_bar_to(solo) sets solo = T but picks a regular shot.
  - When that times out inside the solo, the solo shots begin.
  - The next one_bar_to(verse/chorus) clears solo and picks a regular shot.
  - beat_check during a solo re-picks from the solo pool.
- **Star Power:** no timeout picks while player 0 is in star mode, but bars_left keeps counting down. On the first downbeat after Star Power ends with bars_left ≤ 0, a new shot is picked at once.
  - one_bar_to, excitement and beat_check still pick during Star Power. While the kGuitarSpecial clip plays, only Near shots are valid.
- **After game_over** the script does not stop `beat` / `one_bar_to` / `excitement`. Only the downbeat timeout is blocked (bars_left 100).
  - In the won case, `beatmatch set_paused TRUE` stops the beat tracker [I].
  - In the lost case the music slows for 2 s before pausing, so a one_bar_to in that window could replace the LOSE shot [I; untested].

### 4.4 `update_crowd` and `reduced_crowd_shots` (crowd.dta) [C]
`update_crowd` runs after every `pick_regular_shot` / `pick_solo_shot`:
- it sets crowd sizes by excitement (0.1 / 0.25 / 0.5 / 1 / 1);
- it sets the animate_crowd weights.

So it runs after the one_bar_to, timeout and excitement picks, including the ones where no shot was found. It does **not** run after the intro, WIN/LOSE or beat_check re-picks.

`set_crowd_sizes(full, flat)`: if the venue lists the current `cam_msg` in `reduced_crowd_shots` with a value v < flat, then `crowd set_sizes full v`.

---

## 5. Shot parameters and transitions (summary; details in camera_endgame.md §1) [C]

- **switch_cam** (`FUN_0016f3b8`): `start` / `end` are % of the path length (frames). `duration` is in ticks, or ms when `real_time 1` (default 1920). The frame task is linear, or ATan when `ease ≠ 0`. `loop` wraps it.
  - `u = (frame − min(f0,f1)) / (max − min)`, or 1 for a static shot (start == end).
  - `offset`, `singer` and `fov` lerp from `_in` to `_out` by u.
  - Also read: near/far, enable_dof, hide_crowd, crowd_region, shaky, eyes, guard, force_cam_facing; post_switch_cam `force_char_lod`.
  - Most regular shots are static (`start 0 end 0 duration 0`): they show the path's frame 0 with the `_out` values.
- **Focus character:** VenueCam+0x74 = 0 (guitarist0) in single player. MULTIPLAYER shot names with Left/Right pick 0/1; −1 = the average.
  - The focus is guitarist0's `bone_head`, filtered with cam_filter 0.1 (walk doc §1.2).
- **Transition:** always a cut.
  - The focus snaps to the head (`FUN_0016e080`) and `+0xa0 = −FLT_MAX` forces an update.
  - The optional per-shot screen fade (`set_fade` → VenueCam+0x34 → `FUN_00167eb8` camera_fade) is 0 in the game; only camedit/camtest set 240.
- **Special shots:**
  - INTRO: `eval_shot INTRO[0]` at −10 s. Its duration sets the intro length.
  - intro_skip: picker. Tutorial: `TUTORIAL[0]` on intro_skip. Multiplayer: `MULTIPLAYER*` on intro / active_players_changed.
  - WIN / LOSE / FINAL_WIN: camera_endgame.md §4.
  - `SINGER` (small_club only) is a debug "stare at singer" lipsync mode (`toggle_camera_lipsync`).
- **Solo near** = the SOLO_NEAR group (Closeup). See walk doc §1.1 for each venue's shots.

---

## 6. Our `engine/src/game/venue_camera.cpp` compared with the original

What already matches [C]:
- The flag constants and the group→flags and pool tables (hard-coded, equal to the data).
- Per-shot `walk_ok`, `low_excitement_ok` and `bad_walk_spots` parsing, with the right defaults.
- `valid()` rules 2/3/4/7, and rule 8 for levels 0..3.
- The side and distance rules of `compatible()`.
- Weight = pool weight / candidates; the chosen shot is marked used; a failed pick leaves the shot alone.
- `beat()` re-picks on an invalid shot without touching bars.
- `one_bar_to` sets solo and picks a regular shot; the INTRO shot runs with bars = 6.
- `gameOver` (FINAL_WIN / WIN +1750 ms / LOSE, bars 100); the switch_cam math; cut transitions.

Differences, in priority order:

| # | original [C] | ours | effect |
|---|---|---|---|
| D1 | `random_int(lo,hi)` ∈ [lo, hi) | `barsFor`: `uniform_int_distribution(lo, hi)` (inclusive) | shots are 1 bar too long on average; e.g. arena Great gives 1–3 bars instead of 1–2, theatre Okay 3–4 instead of always 3 |
| D2 | downbeat: `bars_left--` always; only `check_shot` is gated by star mode | `downbeat()`: `if (starMode) return;` before the decrement | after Star Power, the original cuts on the first downbeat (timer ran out); ours waits for the full remaining bars |
| D3 | excitement re-pick in the **beat** handler on the first beat with `beats_left ≤ 1`, then the downbeat still decrements and may run check_shot | done in `downbeat()` with an early `return` (no decrement, no check) | cut lands a beat late or early; length differs by 1 bar |
| D4 | per frame: beat → downbeat → one_bar_to | `venue_renderer.cpp:244-250`: beat → excitement → **upcoming → downbeat** | our one_bar_to shot loses one bar (decremented right after being set); the original's possible double pick (timeout then one_bar_to) is missing |
| D5 | one_bar_to ignored while `$camera.beat == 0` | not gated | a section in the first ~2400 ticks cuts away from the INTRO shot in ours |
| D6 | "current" (`DAT_002edb78`) is set **only by the picker**, is NULL after reset, and the first pick checks the side against `intro_camera_flags` | `currentShot_` is set by every `switchTo` (INTRO, WIN, LOSE); `intro_camera_flags` is never read | the first regular shot can be on the intro's side; the rules after game over compare against WIN/LOSE |
| D7 | refresh: (a) the whole pool when **every** shot in it is used, checked before filtering; (b) otherwise, when no fresh candidate exists, only the **valid used candidates** are re-marked fresh | when no fresh valid candidate exists, all shots of the pool are re-marked fresh | the freshness rotation differs (invalid shots get a fresh bit too early) |
| D8 | rule 8: `excitement != 3` | `excitement < 3` | none today (we never produce Peak = 4); keep `!= 3` for Peak |
| D9 | rule 7: kGuitarSpecial **blend weight** > 0 (`FUN_00180518`) | `guitarist_->clipFlags() & kGuitarSpecial` (current clip) | edge frames while the special clip blends in or out [I] |
| D10 | `update_crowd` only after `pick_*_shot` (one_bar_to, timeout, excitement), even when the pick fails; not after intro, WIN/LOSE or beat_check | `CrowdRenderer::update(newShot = shot name changed)` resizes on **every** shot change | crowd size/anim re-rolls on beat_check / intro / end shots; no re-roll when a pick fails |
| D11 | `set_crowd_sizes` caps the flat size by `reduced_crowd_shots[cam_msg]` | missing | big_club/fest far shots show the full flat crowd |
| D12 | RNG: HMX lagged-XOR, seed from RTC h/m/s, 16-bit float draw, `r <= cum` | `std::mt19937{random_device}`, `r < w` subtractive walk | same distribution in practice; optional fidelity |
| D13 | after game_over, beat_check / one_bar_to / excitement still run (§4.3) | `ended_` blocks everything | ours is the safer choice; keep it [I that the original rarely differs] |
| D14 | groups and pools come from `$camera.groups` / `$camera.pools` | hard-coded `kGroupFlags` / `kPools` | equal today; data-driven parsing is optional |
| D15 | intro length = INTRO[0] `duration` | `app.cpp kIntroMs = 10000` | equal for the shipped data |
| D16 | intro_skip → picker shot; multiplayer / tutorial camera handlers | not implemented | only matters for restart-skip, 2P and the tutorial |

---

## 7. Implementation steps

All changes are in `engine/src/game/venue_camera.{h,cpp}`, `engine/src/render/venue_renderer.{h,cpp}` and `engine/src/app.cpp`.

1. **D1** `VenueCamera::barsFor`: use `lo + rand % (hi − lo)` (guard `hi <= lo` → `lo`), i.e. `uniform_int_distribution<int>(lo, hi − 1)`. Also index `durations_` by the key's value (parse the `kExcitement*` ints in `(camera_durations (0 3 6) …)`) rather than by position. The positions agree today.
2. **D2/D3**: split `downbeat()` and add the beat-side logic:
   ```cpp
   void VenueCamera::beat(int beatsLeft, int beatNumber, int excitement) {   // the `beat` message
       if (ended_) return;
       beatNumber_ = beatNumber;                                            // $camera.beat
       if (pickerCurrent_ && !valid(*pickerCurrent_)) pick(solo_ ? kCamSolo : kCamNoSolo);   // beat_check
       if (aboutToSwitch_ && beatsLeft <= 1) {
           aboutToSwitch_ = false; barsLeft_ = barsFor(excitement); pickShot();   // pickShot = pick + crowd-update flag
       }
   }
   void VenueCamera::downbeat(int excitement) {
       if (ended_) return;
       if (barsLeft_ > 0) --barsLeft_;
       if (!state_.starMode && barsLeft_ <= 0) { barsLeft_ = barsFor(excitement); pickShot(); }
   }
   ```
3. **D4/D5**: in `VenueRenderer::update` call `camera_->beat(...)` for each crossed beat, then `downbeat`, then `upcomingSection` (move line 249 after 250). In `upcomingSection`: `if (beatNumber_ <= 0) return;`.
4. **Beat data**: in `App::updateVenue` fill `VenueFrame::beatsLeft` and `beatNumber` for a crossed beat line:
   - `beatNumber = tick / 480` (integer division);
   - `beatsLeft = 0` on a measure line, else `beatsPerMeasure − beatInMeasure`, from the `song_->beats` measure flags (count the lines since the last measure line, and the count up to the next one).
   - Drive `camera_->beat` from these lines instead of `floor(beat)`.
5. **D6**: add `const Shot* pickerCurrent_ = nullptr;`.
   - Set it only in `pick()`; reset it in `start()`.
   - Use it in `compatible()`:
     - same-shot test;
     - side test `c = pickerCurrent_ ? pickerCurrent_->flags : introFlags_`, reject when `(c & 3) && (c & 3) == (s.flags & 3)`;
     - distance test only when `pickerCurrent_`, as `a = prev & 0x3c; b = s & 0x3c; a && ((a & b & 0x34) || (a | b) == 0x30)`.
   - Parse `intro_camera_flags` from the venue entry in `arena/venues.dtb`. It is `{| a b}` after macro expansion, so OR the ints, or take the int. Pass it to the ctor next to `durations`.
   - `currentShot_` stays the displayed shot. `beat()` uses `pickerCurrent_` (remove the Special test).
6. **D7**: in `pick()`, per pool:
   - first, `if (all_of(p.shots, !fresh)) for s: s->fresh = true;`;
   - then build `fresh`/`used` from the shots that pass `(flags & wanted) == wanted && compatible(s)`. `compatible` calls `valid`; fold `valid` into it, as the original does;
   - `if (fresh.empty() && !used.empty()) { for s in used: s->fresh = true; fresh = used; }`.
7. **D8**: `if (state_.excitement != 3 && !s.lowExcitementOk) return false;`.
8. **D10/D11**:
   - Add `int crowdUpdates() const` (a counter bumped in `pickShot()`, i.e. after one_bar_to, timeout and excitement picks, even when `pick` found nothing). Let `VenueRenderer` call the size/weight part of `CrowdRenderer::update` on a counter change.
   - Keep `switchRegion` on every shot change: it belongs to switch_cam.
   - Add `reduced_crowd_shots` (`venues.dtb`), giving `flat = min(flat, v)` when `shotName()` matches.
9. **D12 (optional)**: add `HmxRng` (`engine/src/game/hmx_rng.h`) with `seed(uint32)`, `next()`, `randomInt(a,b)`, `randomFloat(a,b)`, as in §3. Seed it once from local time `s + 60m + 3600h`, and share it with the crowd and walks to match the original's coupling. Use `r <= cum` in `pick()`.
10. **Optional**:
    - parse `$camera.groups` / `$camera.pools` from `arena/gen/camera.dtb` instead of `kGroupFlags` / `kPools`;
    - intro length from `INTRO[0].duration`;
    - `intro_skip` → `barsLeft_ = 0; downbeat-style check` (D15/D16).

**Verification:**
- `GD01_LOG_SHOTS=1` on a 4/4 song at each venue. Expect:
  - durations in `[lo, hi)` bars;
  - no Left→Left / Right→Right;
  - no Far/balcony↔Far/band_POV pairs;
  - the first regular shot not on the `intro_camera_flags` side;
  - a cut on the first downbeat after Star Power when the timer expired;
  - one_bar_to shots lasting exactly N bars.
- `--selftest` should still pass.

---

## 8. Function index
- **CameraSystem:**
  - `FUN_00110dc8` ctor; `CameraSystem_v0` dtor; `CameraSystem_v1 @0x111b20` handlers; `FUN_00111030` reset;
  - `FUN_001110b0` add_pool; `FUN_001116c0`/`FUN_00111398`/`FUN_001104a0`/`FUN_00110550` make_group; `FUN_00111740` find group;
  - `FUN_001118b8` Pick; `FUN_001117a0` build+choose; `FUN_001109b0` pool candidates; `FUN_00110cd8`/`d10`/`d60` pool refresh;
  - `FUN_00110228` chooser add; `FUN_00110410` chooser draw; `FUN_00111880` set current;
  - `FUN_00110128` Compatible; `FUN_00110040` Valid; `FUN_0010ff60` side; `FUN_0010ffd0` distance;
  - `FUN_0010fd30` walk_ok; `FUN_0010fd80` bad_walk_spots; `FUN_0010fe38` low_excitement_ok;
  - `FUN_0010feb0` IsWalking; `FUN_0010ff00` nearest spot; `FUN_0010fed8`→`FUN_00285888` special-clip weight; `FUN_0010ff38` excitement != 3;
  - `FUN_00111aa8` beat_check.
- **Messages:** `BeatMatch_v1 @0x10e458` beat/downbeat/music_start; `FUN_0010f700` one_bar_to; `FUN_0016a3c8` excitement; `FUN_00105b80` intro_start/intro_skip.
- **RNG:** `FUN_0024a028` seed; `FUN_0024a140` next; `FUN_0024a1d0` int; `FUN_0024a218` float; `FUN_00223f08` boot reseed; script `FUN_00233728`/`798`/`828`.
- **VenueCam:** `FUN_0016f3b8` switch_cam; `FUN_0016e390` per frame; `FUN_00167eb8` camera_fade.
