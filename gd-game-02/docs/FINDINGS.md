# Guitar Hero II (Xbox 360) — gameplay rules, reverse-engineering findings

Status: first pass (2026-10-26). Addresses are Ghidra addresses in
`default.xex` (PowerPC, image base `0x82000000`). **[C]** = confirmed: read
from the code (function or instruction address given) or from the game's
own config (`.dta` file named). **[I]** = inferred, not proven. Compare with
`gd-game-01/docs/FINDINGS.md` (GH1, PS2), which this document follows
section by section. "GH1" below always means that document.

## Tooling notes (read before using `analysis/`)

- **The first exported decompilation is incomplete.** About 6,280 functions in
  `analysis/functions.tsv` are cut to 8 bytes. Every function that saves
  registers with the compiler helpers at `0x8239bcc0..0x8239bcfc`
  (`__savegprlr_NN`, a chain of `std` instructions that falls into the next
  helper) was treated by Ghidra as calling a non-returning function, so its
  body after the prologue is missing from `decomp/*.c`. Almost all gameplay
  code is affected (the watcher, the player, Star Power, scoring).
- **Fix used here:** the `.pdata` section (`0x820ece00`, 25,421 entries of
  `{u32 start, u32 flags}`, length = `((flags >> 8) & 0x3fffff) * 4`) lists
  every non-leaf function with its exact size. A headless post-script
  (read-only, nothing saved) marks the save/restore helpers as returning,
  clears and re-disassembles each `.pdata` range, creates the function with
  that body and decompiles it. Leaf functions have no `.pdata` entry; let
  Ghidra create those from their entry point. All findings below come from
  this re-decompilation plus raw disassembly of the key functions.
- **String references:** Ghidra did not resolve most `lis`/`addi` pairs, so
  `strings.tsv` shows no referencing function for most symbols (for example
  `hopo_threshold`). A linear scan of `.text` that pairs `lis rX, hi` with
  later `addi`/`lwz`/`lfs`/… on the same register gives about 40,000
  references and finds them.
- Config symbols are read with `Symbol(name)` + `DataArray::FindArray`
  (`0x82319530`) + `Int`/`Float` (`0x82317fe8` / `0x823180e8`).

## Configuration that drives the rules

| File (extracted/dta) | Block | Values | Read by |
|---|---|---|---|
| `config/gen/beatmatcher.dta` | `watcher` | `slop 100`, `cheat_error 0`, `pitch_bend_range 1`, `ms_to_full_pitch_bend 1000` | watcher constructor `0x82350ae0` [C] |
| `system/run/config/gen/beatmatch.dta` (merged) and `beatmatcher.dta` | `parser` | `hopo_threshold 170` | `0x823341d0` [C] |
| `config/gen/songs.dta` | per song block | optional `hopo_threshold 250` | `0x823341d0` (overrides the global value) [C] |
| `config/gen/scoring.dta` | `points`, `error_class`, `streaks`, `default_config` / `coop_config` (`crowd`, `star_power`), `star_ratings` | see below | scoring singleton `0x822d69c8`, SP config `0x822d5fc0`, crowd `0x822de640` [C] |
| `config/gen/modes.dta` | per game mode | `score_config`, `shared_star_meter`, `competitive`, `crowd_rating_active`, `star_power_disable`, `practice_speed` | various [C] |
| `config/gen/songs.dta`, `config/gen/sound.dta` | `practice_speeds`, `practice_mode default_speeds` | `(100 85 65 50)` / `(100 90 75 60)`, default `(100 90 80 60)` | `0x822cee60` [C] |

Keys in the data that the 360 executable never reads (the strings do not
exist in the binary) [C]: `ingame_stats missed_slop_threshold`,
`star_ratings thresholds_section`, `sound.dta practice_mode pitch_correct`
(`slice_frames`, `linear_attack`, …).

## Chart → gem list

- Notes are grouped into one chord gem when their note-on ticks differ by
  **at most 10 ticks** (`|Δ| < 11`, `0x82344de0`) [C]. Same as GH1.
- A gem whose length is **≤ 160 ticks** (`< 0xa1`), or any gem when the
  parser's `ignore_durations` is set, has no sustain (`0x82345480`) [C].
  Same as GH1 (shorter than 161).
- Gem record, 16 bytes (`0x82345480`, used everywhere below) [C]:
  `{f32 start_ms; s32 start_tick; u16 duration_ms; u16 duration_ticks; u8 fret_mask; u8 flags; u8 player_mask; u8 pad}`.
  Flags: **bit 7 (0x80) = HOPO-able**, **bit 6 (0x40) = no sustain**. (GH1
  used bits 0 and 1 of the same byte.) `player_mask` 0 = every player.
- One gem list per player; each list holds `{owner, hopo_threshold, gems}`
  (`0x8233fe50` → `0x8234e0f0`) [C].

### HOPO marking — `0x8234dcc8` (called from the add-gem routine `0x8234e190`) [C]

```
hopo(new) = list not empty
         && (prev.fret_mask & new.fret_mask) == 0      // prev = last gem added (a whole chord counts as one gem)
         && popcount(new.fret_mask) == 1                // chords are never HOPOs
         && new.tick - prev.tick < hopo_threshold       // strict: blt at 0x8234dd38
```

`hopo_threshold` = `beatmatcher parser hopo_threshold` (170), replaced by the
song block's `hopo_threshold` when present (250 for 9 songs)
(`0x823341d0`, instructions `0x82334264..0x823342e0`) [C]. With 170 this is
"≤ 169 ticks", identical to GH1's hard-coded rule; GH2 makes it data-driven
and per song. There are no forced HOPO notes [C, `docs/audio_and_charts.md`].
The threshold does not depend on difficulty [C: only those two sources].

## Input judgment ("watcher")

### Classes

| Watcher | Size | Constructor | vtable | Used for |
|---|---|---|---|---|
| base | — | `0x82350ae0` | `0x8205b464` | — |
| guitar | 0xac | `0x8234e9e8` | `0x8205b33c` | controller types `guitar`, `lefty_guitar`, `hx_guitar_xbox`, `hx_guitar_xbox_new` (`0x8234a620`) [C] |
| joypad | 0x98 | `0x8234e1f8` | `0x8205b204` | everything else |
| drum | 0x88 | `0x8234e690` | `0x8205b284` | only when `drum_solo` is set (not used by GH2 data) [I] |

vtable slots (all three): `+0x10` jump/reset, `+0x14` poll, `+0x18` Swing
(strum), `+0x1c` NonStrumSwing (hammer-on / pull-off), `+0x20` FretDown,
`+0x24` FretUp, `+0x44` OnHit, `+0x48` OnMiss (overstrum), `+0x4c` OnPass,
`+0x70` "may the sweep pass this gem", `+0x74` next gem, `+0x78` late window.

Watcher fields used below: `+0x14` slop (float, 100), `+0x18` last hit gem,
`+0x1c` first playable gem (practice start), `+0x24`/`+0x28` sustained gem /
its end time, `+0x2c` calibration offset added to the input time (set by
vtable `+0x40` from the player's latency setting) [C], `+0x4c` miss-sweep
cursor, `+0x54` time of last reported overstrum. Guitar only: `+0x84` held
fret mask, `+0x88` gem last hit by a HOPO, `+0x8c` gem locked after a wrong
hammer-on, `+0x90` time of last chord completed by a fret change, `+0x94`
time of last HOPO attempt, `+0x98/+0x9c/+0xa0/+0xa4` pending wrong strum
(gem, slot, deadline, "follows the HOPO gem"), `+0xa8` = **60.0** (constant
`0x82002af8`, set by the constructor; not from config) [C].

### Controller → watcher (guitar controller `0x8234c538` down / `0x8234c6d0` up) [C]

- Strum buttons (D-pad up/down, ids 0xc/0xe, also 0x10/0x12):
  `Swing(highest held fret or -1, strum=1)`.
- Fret press: `FretDown(slot)`; then, if no held fret is above it
  (`held < 1<<slot`), `NonStrumSwing(slot, hammer=1)`.
- Fret release: `FretUp(slot)`; then, if frets remain and the highest
  remaining one is below the released one, `NonStrumSwing(highest, hammer=0)`.
- The controller sets a "strummed" byte (`+0x48`) on a strum and clears it
  when the last fret is released, but **nothing in the controller or the
  player sink reads it** [C for the two controller functions and the sink
  `0x8233ab40`; no other reader found]. So, unlike GH1, HOPO attempts are not
  gated by "a strum happened and frets were never all released".
- Player sink `0x8233aa80` (Swing): a strum is **dropped if it comes ≤ 52 ms
  after the previous accepted strum** (`last + 52 >= now`, constant
  `0x8205807c` = 52.0) [C]. Same as GH1.

### Hit window [C]

`in_window(gem, t) = |(t + offset) − gem.start_ms| ≤ slop` (`0x8234f5d0`),
slop = **100 ms, the same for every difficulty** (only the watcher
constructor writes `+0x14`) [C]. Nearest gem: binary search, then the closer
of the two neighbours, ties to the earlier gem (`0x8234ddd0`) [C].

### Strum — `GuitarWatcher::Swing` `0x8234eb60` [C, from disassembly]

```
now = song clock
if now == last_chord_fix_time(+0x90) and !strum: return
i = nearest gem to now + offset
j = i
if !mine(i) or hit[i]:
    if i + 1 exists: j = i + 1
    if i == hopo_hit_gem(+0x88):            // strum right after a hammer-on/pull-off
        hopo_hit_gem = -1; notify "swingAtHopo"; return   // forgiven, no miss
if j < first_playable: return
if in_window(j, now) and !hit[j] and mine(j):
    single: match = (gem fret == slot)       // slot = highest held fret → anchoring allowed
    chord:  match = (gem mask == held mask)  // exact
    if match: OnHit(now, slot, j, hopo=false); return
    if a pending strum exists: OnMiss(pending) and clear it
    if strum or chord:
        pending = {gem j, slot, deadline = now + 60, after_hopo = (j-1 == hopo_hit_gem)}
        return
OnMiss(now, slot, j)                         // overstrum
```

### Pending wrong strum

- **Fret change** (`0x8234edf8`, called after FretDown with the pressed slot
  and after FretUp with −1), while `now < deadline` [C]:
  - single gem and a fret was pressed: hit if the **pressed** fret is the
    gem's fret;
  - otherwise (chord, or any release): hit if held mask == gem mask;
  - on a hit: `OnHit(hopo=false)`; for a chord, `+0x90 = now`;
  - a **pressed** fret that does not match → OnMiss for the pending gem now.
    A release that does not match keeps waiting.
- **Expiry** (`0x8234e968`, each poll): after the deadline the pending gem is
  reported as a miss **unless** it was set right after a HOPO hit
  (`after_hopo`); then it is dropped silently [C].
- The miss sweep stops at the pending gem until it is resolved
  (vtable `+0x70` = `0x8234e948`) [C].
- The HOPO-hit forgiveness (`+0x88`) ends when that gem leaves the hit window
  (`0x8234ef50`) [C].

### Overstrum / miss reporting — `0x823503d0` → `0x8234f860` [C]

- At most one overstrum per **50 ms** (`last_overstrum + 50 < now`,
  `0x82002228`) [C]. The reported gem is the nearest one, or the next one if
  the nearest was already passed by the sweep.
- It ends the watcher's current sustain and calls `miss(player, slot, gem)` on
  all sinks.

### Miss sweep — `0x82350718` [C, from disassembly]

```
for k = sweep + 1 ...:
    if k is the pending gem: stop
    late = (k < first_playable) ? 0 : slop + (hopo_eligible(k) ? 60 : 0) − offset   // vtable +0x78 = 0x8234f1c8
    deadline = gem[k].ms + late
    if k + 1 exists: deadline = min(deadline, (gem[k].ms + gem[k+1].ms)/2 − offset)
    if !mine(k): deadline = gem[k].ms
    if deadline >= now: stop
    sweep = k
    if !hit[k]: mine(k) ? (k < first_playable ? "skipped" : OnPass(k)) : "other player's gem"
```

`hopo_eligible(k)` (`0x8234efc8`) = gem k is HOPO-able, k ≥ 1, gem k−1 was
hit, and gem k−1 is the last hit gem [C]. Every unhit gem is swept (the
cursor does not jump to the last hit gem). The last gem has no midpoint
limit. (GH1 used `t_next = t + 4000`.)

### Hammer-ons / pull-offs — `NonStrumSwing` `0x8234f3e0` → `0x8234f210` [C, from disassembly]

```
if now == last_hopo_try(+0x94): return
k = nearest gem to now + offset, or k+1 when k is not mine or already hit
require: k >= first_playable
         hopo_eligible(k)                              // HOPO flag, previous gem hit and last hit
         dt = now + offset − gem[k].ms
         −slop ≤ dt ≤ slop + 60                        // 0x8234f294..0x8234f2b0: −100 … +160 ms
         !hit[k], mine(k), k != locked_gem(+0x8c)
if gem[k].mask == 1<<slot and no held fret above slot:
    OnHit(now, slot, k, hopo=true)                     // sets hopo_hit_gem = k
else if hammer (pressed, not released) and no held fret above slot:
    locked_gem = k                                     // a wrong hammer-on locks this gem for HOPO tries
```

- Window: **−100 … +160 ms**. GH1 was `|dt − 60| ≤ slop` = −40 … +160 ms;
  GH2 keeps the full slop on the early side [C].
- Lower frets may be held (anchoring) for HOPOs [C].
- A wrong **pull-off** never locks the gem; only a wrong hammer-on does [C].
- After any accepted HOPO the gem can also be strummed without penalty
  while it is in the window (forgiveness above) [C].
- The joypad watcher has no NonStrumSwing (`+0x1c` is a no-op) [C].

### Joypad mode — `0x8234e498` [C]

Every fret press is a Swing for that fret. Single gem: hit if the fret
matches, else overstrum. Chord: the first correct fret opens a **50 ms**
window (`+0x84` = 50.0, `0x82002228`) in which the remaining chord frets
must be pressed (`0x8234e350`); a wrong fret is a miss. GH1 gave chords 60 ms
on the joypad.

### Sustains (watcher side) [C]

- `OnHit` → `0x8234fe20`: marks the gem hit, sets last hit gem, notifies the
  sinks; when the gem has a sustain (flag 0x40 clear) it starts tracking it
  until `start_ms + duration_ms`.
- Released early when any fret of the sustained gem is let go (`0x8234f110`)
  or when a fret is pressed above a single note / any extra fret on a chord
  (`0x823500c8`, `0x8234f070`) → `sustain_released(remaining_ms)` (`0x82350128`).
  Same rules as GH1. No release tolerance exists: the sustain just stops
  scoring at the release time.
- Whammy (`0x8234ff18`): the whammy value counts once it rises above −0.35;
  the bend amount is `(value − baseline)/(baseline + 1)`; the audio pitch bend
  is `pitch_bend_range × min((now − sustain_start)/ms_to_full_pitch_bend, 1) ×
  amount`. Audio only; Star Power uses the separate test below.

### Base watcher poll — `0x82350d68` [C]

end finished sustain → autoplay cheat → miss sweep → passed-gem events →
whammy → guitar extra (`0x8234f028`: pending expiry, HOPO forgiveness
expiry).

## Player: scoring

Player object vtable `0x8204579c` (co-op player `0x82049004`, band
`0x82048ee4`). Score singleton `0x8280e05c`, built by `0x822d69c8` [C]:
`+0 head 50`, `+4 tail 25`, `+0xc good 70`, `+0x10 perfect 30`,
`+0x14` streak table, `+0x24` Star Power config.

- **Hit** (`0x822c9338`, order matters) [C]:
  1. ignore gems outside the practice range `[+0xf0, +0xf4)` (ticks);
  2. `+0x30` (`0x822ddf70`): streak += 1, hits += 1 (`0x822dcaa8`), send
     `build_streak` if the multiplier went up;
  3. finish the previous sustain's accrual;
  4. head points: `popcount(frets) × 50`, added with the **current**
     multiplier (`0x822c6b18` → `0x822c6a58`);
  5. gem with sustain: open a sustain record; otherwise rock meter target 1.0;
  6. Star Power gem hit.

  Because the streak is incremented before the points are added, **the
  10th consecutive note already scores at 2×** in GH2 (GH1: still 1×) [C].
- **Score add** (`0x822c6a58` → `0x822dcfb0`):
  `score += points × base_multiplier × sp_multiplier`, where
  `sp_multiplier` = `star_power multiplier` (2) while deployed, else 1
  (`0x822c6c20`) [C].
- **Multiplier** (`0x822d66b0`): table `(0 → 1)` plus `streaks`
  `(10 2) (20 3) (30 4)`: the last entry whose count ≤ streak [C]. Same
  values as GH1. Reaching 8× total sends the `eight_x` achievement event
  (`0x822dd298`) [C].
- **Sustain points** (`0x822de310`, `0x822de428`, `0x822d62b0`) [C]:
  `max_tail = round_half_away(duration_ticks × 25 / 480) × frets`;
  every poll `earned = round_half_away(clamp((t − start_ms)/duration_ms, 0, 1) × max_tail)`,
  the difference is added (× current multiplier). When `earned == max` the
  note is finished at once. Same as GH1.
- **Sustain end** (`0x822c86f8`): rock meter target
  `(earned + 50) / (max_tail + 50)`; if `earned/max < 0.1` and the gem starts
  on a "big note" (MIDI 110 of the part, within ±0.2 beat) the game sends
  `blew_big_note`, which only plays a crowd sound (`ui/gen/game.dta`) [C].
- **Miss / overstrum** (`0x822c9478` → `0x822c8d40`) [C]: `end_streak`
  event, streak reset (longest streak kept, `0x822dc518`), rock meter target
  0.0, Star Power phrase miss (unless the gem is a phrase start), sustain
  ended.
- **Pass** (`0x822c9498`) [C]: counts the pass; penalises (as above) **only if
  this gem was not already penalised by an overstrum** (`+0xac` = highest
  gem already penalised). GH1 instead re-attributed misses reported more
  than 500 ms early; that logic is not present in GH2's player.
- **Timing class** (`0x822d6260`): |error| ≤ 30 → 2, ≤ 70 → 1, else 0. It is
  computed on every hit (`0x822e2748`); no gameplay effect found [C/I].
- **Base score / stars** [C]: base = Σ over the player's gems
  `frets × (50 + (sustain ? duration_ticks × 25 / 480 (integer) : 0))`
  (`0x826a0ab0`). Stars (`0x822d6410`): 5 if score ≥ 2.8 × base, 4 if
  ≥ 2.0 × base, else 3 (`star_ratings thresholds`, every difficulty).
  GH1: 2.0 / 3.0. Co-op (`0x822ce5d8`): 5 stars at ≥ 2.0 × `max_scores[song][3]`,
  4 at ≥ 2.0 × `max_scores[song][1]`, else 3 (`coop_max_scores.dta`); the
  indices come from the keys `kDifficultyMedium` / `kDifficultyExpert` and do
  not depend on the chosen difficulty [C as decoded; intent unclear].

## Star Power

Object built by `0x822db498` (vtable `0x8204880c`); config = scoring
`star_power` block (`0x822d5fc0`): `[0] recharge_rate, [1] deploy_rate,
[2] star_phrase, [3] whammy_rate, [4] ready_level, [5] multiplier (int),
[6] crowd_boost, [7] min_length × 1000, [8] whammy_speed × 0.01,
[9] whammy_timeout × 1000` [C]. The meter is `{f32 value, f32 shown}`,
clamped to 0…1 (`0x822db788`).

- **Phrase membership** (`0x822bc1e8`) [C]: a gem is in a phrase when
  `phrase.start ≤ gem.tick < phrase.end`; bit 0 = first gem of the phrase,
  bit 1 = in phrase, bit 2 = last gem (the next gem is at or after the
  phrase end, or it is the song's last gem).
- **Gem hit** (`0x822dc090`) [C], phrase state 0 none / 1 broken / 2 armed /
  3 captured:
  - not deployed, gem in phrase: `star_gem_hit`; first gem → armed; when
    armed and this is the last gem → add `star_phrase` (0.25, or the value set
    by `set_star_power_phrase_boost`), `phrase_captured`, state 3;
  - deployed, gem in phrase: the phrase is broken (no capturing during Star
    Power), as in GH1;
  - gem not in a phrase: state 0.
- **Phrase miss** (`0x822dc368`) [C]: a miss on a phrase gem breaks the
  phrase (`phrase_miss`); an overstrum with no gem breaks an armed phrase.
- **Ready / deploy**: `star_power_ready` once the meter reaches
  `ready_level` 0.5; the ready flag is cleared when the meter empties
  (`0x822dbd60`). Deploying (`0x822c80f8`, tilt or Select) needs the ready
  flag and Star Power not already active [C].
- **Drain** (`0x822dbd60`, `0x822db3f8`): while deployed the meter loses
  `deploy_rate` per **measure** (0.125 → a full meter lasts 8 measures);
  at 0: `stop_using` / `depleted` [C]. Same as GH1.
- **Whammy fill** (`0x822dbd60`): while the whammy flag is set and Star Power
  is not active, `+ whammy_rate × Δbeats` (0.034 per beat) [C]. Same as GH1.
- **Whammy flag** (`0x822c8438`) [C]: only while holding a sustain whose
  phrase is armed or captured (state 2 or 3). The bar counts as moving when
  `|Δvalue / Δms| > whammy_speed × 0.01` (= 0.0005 per ms, i.e. half the
  range per second [I: the value range is assumed to be 0…1]) and stays
  "on" for `whammy_timeout` (500 ms) after the last such movement. Sends
  `whammy_start` / `whammy_end`.
- `min_length` (2.0 s) is copied into the player (`+0xd4`) but no reader was
  found [I: unused].
- `recharge_rate` is 0 and no refill-over-time code was found [I].
- Practice and attract modes set `star_power_disable` [C, `modes.dta`].

## Rock meter (crowd)

Crowd object inside the player (`+0x40`), set up by `0x822de640` from the
difficulty's `crowd` block [C]: `+8 raw`, `+0xc rating`, `+0x10/+0x14 range`,
`+0x18 note_weight`, `+0x1c warning_level`, `+0x20 lose_level`,
`+0x24 free_ride`. Starts at rating 0.5, raw = middle of the range.

Update `0x822de570` (called through `0x822dd3e0`) [C]:

```
k = (target > raw && star power active) ? crowd_boost : 1        // 6 solo, 5 co-op
w = note_weight × k
raw' = clamp01((1 − w)·raw + w·target)
rating' = raw' ≤ lo ? 0 : raw' ≥ hi ? 1 : (raw' − lo)/(hi − lo)
if rating ≤ lose_level and rating' ≤ rating: keep old values       // once failed, it can only rise
if rating' ≤ warning_level and progress ≤ free_ride: keep old values
store raw', rating'
fail when rating < lose_level, unless the mode is competitive (0x822b37f8)
```

Targets: hit without sustain 1.0, miss/overstrum/pass 0.0, sustain end
`(earned + 50)/(max + 50)`; weight 1 [C].

| Difficulty | note_weight | range | free_ride | warning | lose |
|---|---|---|---|---|---|
| Easy | 0.02 | 0.5–0.9 | 0.2 | 0.15 | 0.01 |
| Medium | 0.01 | 0.5–0.9 | 0.1 | 0.15 | 0.01 |
| Hard | 0.008 | 0.6–0.9 | 0 | 0.15 | 0.01 |
| Expert | 0.008 | 0.65–0.9 | 0 | 0.15 | 0.01 |
| co-op Easy | 0.01 | 0.5–0.9 | 0.2 | 0.15 | 0.01 |
| co-op Medium | 0.005 | 0.55–0.9 | 0.1 | 0.15 | 0.01 |
| co-op Hard | 0.004 | 0.65–0.9 | 0 | 0.15 | 0.01 |
| co-op Expert | 0.004 | 0.7–0.9 | 0 | 0.15 | 0.01 |

(`config/gen/scoring.dta`) [C]. "progress" = song position / song length
(`0x822dd238`) [C]. Peak: multiplier 4 and rating > 0.66 → `peak_on`
(`0x822dcef0`) [C].

No fail [C]: competitive two-player modes (`0x822b37f8`: more than one
player and the mode's `competitive` flag), practice and tutorials
(`crowd_rating_active FALSE`, `modes.dta`). GH1 skipped the fail in every
multiplayer game; in GH2 **co-op can fail** [C for the check; which meter
decides in co-op is [I], see below].

## Practice mode

- `update_practice_speed` (`0x822cee60`): speed = `practice_speeds[i]` of the
  song (or `sound.dta practice_mode default_speeds`) × 0.01, i =
  `gamecfg practice_speed` (0–3) → `set_music_speed` (`0x822cee10`) [C].
- `set_music_speed` sets that speed on every player's song stream
  (vtable `+0x68`, `0x826a22b8`: stored, then applied to every channel voice
  and the slip voices) and scales the song clock by `1/speed`
  (`0x822e1df8`) [C].
- The `song_practice_N` blocks (`_p85` … moggs, absent on the 360 disc) are
  not used to pick the audio: the practice mode's `song_block` is `song`
  (guitar) or `song_coop` (`modes.dta`); the only code that touches
  `song_practice_N` rewrites paths of downloaded songs (`0x822af468`) [C].
- No pitch correction: none of the `pitch_correct` keys exist in the
  executable [C]. The channel voices take the speed as a playback frequency
  ratio clamped to 0.01…2 (`0x8268a100`) [I: that this voice class is the one
  the song stream uses], so slowed practice audio is resampled and **its
  pitch drops** (85% ≈ −2.8 semitones) [I].
- When the speed is not 100% a click track (TRIGGERS notes 24/25/26) plays
  (`ui/gen/practice.dta`, `click_parser`) [C]. Practice also disables Star
  Power and failing, skips the intro and counts only gems inside the chosen
  section range (player `+0xf0/+0xf4`) [C].

## Two players

Modes (`config/gen/modes.dta`) [C]:

| Mode | Players | competitive | Chart | Score config | Notes |
|---|---|---|---|---|---|
| `multi_fo` (face-off) | 2 | TRUE | shared part, `separate_parts FALSE` | default | each gem belongs to one player via `player_mask` [I: from the face-off notes, as in GH1] |
| `multi_vs` (pro face-off) | 2 | TRUE | `midi_suffix VS` (no chart has it, falls back) | default | both play the full part |
| `multi_coop` | 2 | FALSE | lead + bass/rhythm, `song_coop` audio, `midi_suffix COOP` | `coop_config` | shared Star Power meter |

- A gem belongs to a player when its `player_mask` is 0 or has that player's
  bit (`0x82347688`); other players' gems are swept without penalty [C].
- Competitive modes cannot fail; the winner is the higher score
  (`0x822c6f30`) [C].
- **Score normalisation** (game property `use_normalization`, default TRUE,
  `0x822bb048`) [C]: when two tracks are played, every point a player scores
  is multiplied by `(r + 1)/2` (player 1) or `(1/r + 1)/2` (player 0), with
  `r = base_score[0] / base_score[1]` (`0x822bb320`, `0x826a0a50`). With one
  track, or both players on the same chart, the factor is 1. In co-op this
  makes lead and bass/rhythm worth the same.
- **Co-op** (player vtable `0x82049004`, band `0x82048ee4`) [C unless noted]:
  - each hit also raises the band's streak (`0x822deba8`), and a miss resets
    it (`0x822debf8`), so the band multiplier is shared; each player still
    keeps a personal streak;
  - every point is also added to the band score (normalised) (`0x822deb30`);
  - rock meter updates go to the player's own meter and to the band meter
    (`0x822dec68`) [I: which one ends the song; the band meter uses
    `coop_config`];
  - shared Star Power meter (`shared_star_meter TRUE`): deploying needs
    **both players to engage**; the second one triggers the deploy for both
    (`0x822c80f8`) [C];
  - `coop_config star_power`: deploy_rate 0.0833 (a full meter lasts 12
    measures), whammy_rate 0.028, crowd_boost 5 [C, `scoring.dta`].
- Bass: no scoring difference was found; `kTrackBass` only changes the miss
  sound (`config/gen/beatmatch.dta`) [C for the data, I for "no other
  difference"].

## GH1 vs GH2

| Rule | GH1 (PS2) | GH2 (360) |
|---|---|---|
| Hit window | ±100 ms (`slop`) | ±100 ms, all difficulties [C] |
| Strum repeat filter | 52 ms | 52 ms [C] |
| Chord grouping | ≤ 10 ticks | ≤ 10 ticks [C] |
| No-sustain cut | < 161 ticks | ≤ 160 ticks [C] (same) |
| Gem flags | bit 0 HOPO, bit 1 no sustain | bit 7 HOPO, bit 6 no sustain [C] |
| HOPO threshold | hard-coded ≤ 169 ticks | `hopo_threshold` 170 (strict <), per-song override 250 [C] |
| HOPO window | −40 … +160 ms | **−100 … +160 ms** [C] |
| HOPO needs a strum since frets were all released | yes | **no** (flag never read) [C] |
| Wrong hammer-on | locks the gem | locks the gem; a wrong pull-off does not [C] |
| Wrong strum grace | 60 ms; any matching fret change hits | 60 ms; a non-matching **press** misses at once, releases keep waiting [C] |
| Pending miss after a HOPO gem | reported | dropped silently [C] |
| Late window of HOPO gems in the sweep | — | slop + 60 [C] |
| Miss sweep | starts after the last hit gem | every unhit gem is swept [C] |
| Joypad chord window | 60 ms | 50 ms [C] |
| Overstrum limit | 1 per 50 ms | 1 per 50 ms [C] |
| Multiplier on the 10th note | 1× (previous multiplier) | **2×** (streak counted first) [C] |
| Early-miss re-attribution | > 500 ms → previous gem | none; a gem is penalised once (`+0xac`) [C] |
| Points / multiplier steps / SP × | 50 per fret, 25/beat sustain, 10/20/30 → 2/3/4, ×2 | same [C] |
| Star Power phrase / drain / whammy | 0.25 / 0.125 per measure / 0.034 per beat | same (solo); co-op 0.25 / 0.0833 / 0.028 [C] |
| Whammy detection | — | speed > 0.0005/ms, 500 ms hold [C] |
| Stars | 2.0 / 3.0 × base | 2.0 / 2.8 × base [C] |
| No fail | any multiplayer | competitive modes, practice, tutorial [C] |
| Co-op | — | shared SP (both must engage), band streak, score normalisation [C] |
| Practice audio | — | full mogg at 100/85/65/50 % (or 90/75/60), no pitch correction [C/I] |

## Open questions

1. Practice audio: confirm that the song stream's channel voices are the
   class whose speed is a frequency ratio (pitch drops), by tracing the
   stream's voice creation, or by listening on hardware/emulator.
2. Co-op failing: which meter (each player's or the band's) triggers
   `game_lost`, and how `0x822de800` mixes the two players' difficulties
   into the band meter.
3. Face-off section assignment (`player_mask`) in GH2's parser
   (`0x82344910`, `0x82344ea8`); assumed identical to GH1.
4. `star_power min_length` (2.0) and `recharge_rate`: no reader found.
5. The meaning of the co-op star thresholds using `max_scores` entries 1 and
   3 regardless of difficulty.
6. Range of the whammy value (assumed 0…1) and the controller's calibration
   offset (`watcher +0x2c`, from the player's lag setting).
7. Timing class (perfect/good): only computed; check the HUD or stats for a
   use.
8. `player_mask` in single player: the parser fills it from the face-off
   section notes in every chart, but applying "mine = mask 0 or my bit" to a
   single player makes the sweep pass gems early (≈296 guitar charts fail).
   gd02 applies ownership only in face-off [I]; find where the original
   decides that the mask is used.
