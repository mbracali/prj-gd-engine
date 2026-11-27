# Guitar Hero II (Xbox 360) — gameplay rules, reverse-engineering findings

Status: first pass (2026-10-26). Addresses are Ghidra addresses in
`default.xex` (PowerPC, image base `0x82000000`). **[C]** = confirmed: read
from the code (function or instruction address given) or from the game's
own config (`.dta` file named). **[I]** = inferred, not proven. Compare with
`gd-game-01/docs/FINDINGS.md` (GH1, PS2), which this document follows
section by section. "GH1" below always means that document.

## Tooling notes (read before using `ghidra/analysis/`)

- **The first exported decompilation is incomplete.** About 6,280 functions in
  `ghidra/analysis/functions.tsv` are cut to 8 bytes. Every function that saves
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

- **When input is live** (wave B #1) [C]: while GamePanel runs its intro
  clock (song time below −25 ms, `0x822CDEB8`) every PlayerMatcher's
  controller is off (`0x822CED30` → matcher vtable `+0x40` with 1) and drops
  the buttons; the matcher poll `0x822E2510` turns it back on (`+0x40` with
  0) once GamePanel is off the intro clock (`+0x5c`) and not `+0x5b`. So no
  strum, Star Power or miss counts before the track is up (it extends at
  −2000 ms). gd02: `App::playerAction` drops them, the frets held then are
  pressed when the controllers come on [I: GH1's FUN_0025c070 sync].

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

### Fret matching: anchoring and chords (verified 2026-10-27) [C]

The question "may lower frets be held on a single note?" (GH1 allowed it)
is answered by the code, and GH2 allows it too:

- The guitar controller passes the **highest held fret** to Swing on a
  strum (`0x8234c538`, the loop over the controller's held mask `+0x38`).
- Swing (`0x8234eb60`, `0x8234ecd8..0x8234ecf0`) compares a single gem's
  fret (its lowest set bit) with that slot: green + yellow held on a yellow
  note is a hit; yellow + blue is not. A chord (`popcount > 1`,
  `0x82334630`) compares the watcher's held mask `+0x84` with the gem's
  mask: **exactly** its frets, no extra lower fret, none missing.
- HOPOs (`0x8234f210`): the gem's mask must be `1 << slot` and the held
  mask `< 1 << (slot + 1)`: no fret above, any below.
- Sustains: a fret pressed **above** a sustained single note ends it, a
  lower one (an anchor) does not; on a chord any press ends it
  (`0x823500c8` walks the gem's frets from the top: a fret below the
  pressed slot, or a second fret, releases). Letting go of any of the
  gem's frets ends it (`0x8234f110`: `(held & mask) != mask`).

So gd02's rules were already GH2's here; nothing about anchoring changed.
Small differences fixed in this pass (all from the disassembly):

- `+0x94` (the time NonStrumSwing compares, `now == +0x94` → no try) is
  set by every **hit** (OnHit → vtable `+0x6c` = `0x8234eb28`, which also
  stores the HOPO-hit gem and clears the pending strum), not by every try.
- A pending wrong strum resolved by a fret change on a HOPO-able gem also
  notifies the sinks' "swingAtHopo" (`0x8234fcb8` at `0x8234eee4`).
- The overstrum report (`0x8234f860`) ends the current sustain **before**
  the 50 ms gap test, so a suppressed report still ends it.

Guitar watcher vtable `0x8205b33c` [C, read from the image]: `+0x14` poll
`0x82350d68`, `+0x18` Swing `0x8234eb60`, `+0x1c` NonStrumSwing
`0x8234f3e0`, `+0x20` FretDown `0x8234f070`, `+0x24` FretUp `0x8234f110`,
`+0x44` OnHit `0x823502e0`, `+0x48` OnMiss `0x823503d0`, `+0x4c` OnPass
`0x82350488`, `+0x58` `0x823526f8`, `+0x5c` `0x82352660`, `+0x60` guitar
poll extra `0x8234f028`, `+0x64` `0x8234eac8` (reset: pending, `+0x88`,
`+0x8c`, `+0x90`), `+0x68` `0x8234eb00` (clear pending), `+0x6c`
`0x8234eb28`, `+0x70` `0x8234e948`, `+0x74` `0x8234f768` (next gem or −1),
`+0x78` `0x8234f1c8`.

**The bass path (GD02-036).** OnHit (`0x823502e0`), OnMiss (`0x823503d0`) and
a wrong HOPO (`0x8234f210`) first ask the watcher's `+0x10` object (vtable
`+0x28`) about the gem's / the song's tick; when it answers true they neither
hit nor miss but call `+0x58` (`0x823526f8`: plays `bass_%d.cue` per slot,
string `0x8205b738`). `+0x10` is the Beatmatcher (`0x8233c0e8` passes itself;
slot `+0x28` = `0x8233aef0`): for a track type 0, 1 or 3, past `+0xc0`, the
tick inside a range of the part's list (SongData +0x7c / +0x80 / +0x84,
`0x823318a0`, `0x82343120`) [C]. No Sfx `bass_N.cue` exists in any bank on the
disc [C data]; what fills the list is not traced, so gd02 takes the normal path.

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
  (`after_hopo`); then it is dropped silently [C]. Any hit clears it
  (`0x8234eb28`) [C].
- The miss sweep stops at the pending gem until it is resolved
  (vtable `+0x70` = `0x8234e948`) [C].
- The HOPO-hit forgiveness (`+0x88`) ends when that gem leaves the hit window
  (`0x8234ef50`) [C].

### Overstrum / miss reporting — `0x823503d0` → `0x8234f860` [C, from disassembly]

- First the watcher's current sustain ends (`0x8234f600`), then at most one
  report per **50 ms** (`last_overstrum + 50 < now`, `0x82002228`).
- The reported gem: the given one; the next (`+0x74`) when it is the gem
  the sweep passed last (`+0x4c`); the list's last gem when that leaves
  none (−1). Then `miss(player, slot, gem)` on all sinks.

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
- `+0x94` is the time of the last hit (`0x8234eb28`), so a hammer-on in
  the same instant as a hit is ignored [C].
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
  computed on every hit by `0x822e2748`, which drops the result [C].
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
- `min_length` (2.0 s) is copied into the player (`+0xd4`, 0x822C9720) and
  never read [C as far as the export shows].
- `recharge_rate` is 0 and the poll (0x822DBD60) has no refill term [C].
- The ready flag is set in the poll (0x822DBD60) once the meter reaches
  `ready_level`; deploying is "engaging" (0x822C80F8: the held state, see
  `docs/multiplayer.md` for co-op's shared meter) [C].
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

## HUD rock-meter drop shadow at song end (issue #125, 2026-11-23)

Issue #125: at "You Rock" the rock meter leaves the screen but its drop
shadow lingered in the port. The original removes the shadow together with
the meters. Traced from the binary.

The in-song HUD is a `HudPanel` driven by `ui/gen/hud_panel.dtb` [C data].
Its song-end handlers:

```
(game_won_msg  {$this on_game_over} {$this set_win_showing TRUE} …)   ; "You Rock"
(game_lost     {$this on_game_over} {$this show_message … song_failed})
```

`on_game_over`, `set_meters_showing`, `set_win_showing` and `set_showing`
are native `HudPanel` methods, dispatched by `Function_8268EBD8`
(`0x8268ebd8`) [C]. Message names resolved from the dispatcher's cached
symbol hashes: `on_game_over` (string `0x8204501c`) runs
`set_meters_showing FALSE` then, if the win record exists, `0x82690d98`.

`set_meters_showing` = `FUN_8268e9f8` (`0x8268e9f8`) [C]. It writes the
`showing` byte (`+0x88`) on every meter display held in the panel's three
vectors (`HudPanel +0x50`, `+0x5c`, `+0x68`) and the two single displays
(`+0x74`, `+0x78`), **and** clears the group at `HudPanel +0x90` via its
`+0x18` byte, and sets the panel flag `+0x80`.

`HudPanel::load` = `Function_82690308` (`0x82690308`) [C]. Its last act:

```
+0x90 = DataFindObj(dir, "meter_extras.grp")     ; Function_82254C00, string 0x820d6740
```

So `meter_extras.grp` is the object at `+0x90` — i.e. `set_meters_showing`
hides it with the meters. In `hud_sp.milo_xbox` `meter_extras.grp` holds a
single mesh, `spraypaint.mesh`: the sprayed drop shadow behind the meters.
(Same load also hides `youwin.mesh` at `+0x7c` and every `mask*` object at
load, matching the port's existing note.)

**Conclusion [C]:** in GH2 the rock-meter drop shadow (`meter_extras.grp` →
`spraypaint.mesh`) is itself a "meter"; `on_game_over` → `set_meters_showing
FALSE` clears its `showing` flag at the same instant as the dial, so it
leaves the screen together with the meter at "You Rock" (and at "Song
Failed").

**Port bug:** `meter_extras.grp` is a top-level `Group` in the HUD root
`NewPanelDir`, not inside a `Band*` display proxy. `HudView::gameOver()`
set `displaysHidden_`, but that flag only gated the `DirObj` proxy branch of
`HudView::drawObject` (`render/hud_view.cpp`), so the root-level shadow kept
drawing after the meters were gone.

**Fix:** `render/hud_view.cpp` `HudView::drawDir` now skips `meter_extras.grp`
while `displaysHidden_`, matching `set_meters_showing FALSE`. Instrumented
with `HudView::shadowShown()` / the `hud_shadow_shown` counter and an
assertion in the `hud_after_song` walktest (shadow present during play, gone
once the displays hide).

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
  executable [C], `FxSendPitchShift` / SoundTouch are only an Fx class no
  song stream sends to [C], and the stream's channel voices (`0x826a1d40` ->
  the voice's speed, `0x8268a100`) take the speed as a frequency ratio
  clamped to 0.01…2. So slowed practice audio is resampled and **its pitch
  drops** (85% ≈ −2.8 semitones) [C for the path; GD02-034].
- The background is not heard below 100% [C]: `0x822ceab0` -> `0x822ce830`
  sets the background fader (+0x20, on every channel that is not a playing
  track's, `0x82335c30` / `0x82336910`) to the option's value, or to −96 dB
  in practice with `practice_speed` ≠ 0 and not the dev cheat `$stretch_bg`.
  Practice also sets fader +0x30 to `practice_attenuation` (−7.5 dB,
  `0x822d08f0` -> `0x823347b8`) on the same channels at every speed. So
  below 100% only the player's part and the click track sound.
- The clock (`0x822cbd18`) starts a measure before the start section's
  measure (in the first measure: −4 beats); `practice_end` is a task at the
  end section's end [C].
- When the speed is not 100% a click track (TRIGGERS notes 24/25/26) plays
  (`ui/gen/practice.dta`, `click_parser`) [C]. Practice also disables Star
  Power and failing, skips the intro and counts only gems inside the chosen
  section range (player `+0xf0/+0xf4`) [C].

## Two players

Details and gd02's implementation: `docs/multiplayer.md` (M10.2).

Modes (`config/modes.dta`) [C]:

| Mode | Shown as | competitive | Chart | Score config | Notes |
|---|---|---|---|---|---|
| `multi_vs` | Face-off | TRUE | the guitar part, `separate_parts TRUE` | default | each gem belongs to one player via `player_mask` (0x823450B0) |
| `multi_fo` | Pro face-off | TRUE | the guitar part, `separate_parts FALSE` | default | both play every gem at one difficulty; unlocked by winning a tour |
| `multi_coop` | Co-op | FALSE | lead (`PART GUITAR COOP`) + bass/rhythm, `song_coop` audio | `coop_config` | shared Star Power meter, the band performer |

(An earlier version of this table had face-off and pro face-off swapped:
`versus.btn` sets `multi_vs`, the locked `faceoff.btn` `multi_fo`, whose
results show the pro face-off difficulty.)

- The parser fills `player_mask` from the face-off section notes only with
  more than one player and `separate_parts` (0x823450B0, 0x82346FA8,
  0x822836B0) [C]: face-off only. A gem belongs to a player when its mask is
  0 or has that player's bit (0x82347688) [C].
- Competitive modes cannot fail (0x822DD3E0 -> 0x822B37F8); the winner is
  the higher score [C].
- **Score normalisation** (`use_normalization`, default TRUE, 0x822BB048)
  [C]: every point is × `(r + 1)/2` (player 1) or `(1/r + 1)/2` (player 0),
  `r = base_score[0] / base_score[1]` over each player's own gems
  (0x822BB320, 0x826A0A50, 0x826A0AB0), 1 with one track.
- **Co-op** [C]: a band performer (0x822DE170) gets every hit (its streak
  and multiplier), every point (normalised, at the band's multiplier) and
  every crowd update (the easier player's mapped onto the band's range, the
  band's crowd at the harder difficulty, 0x822DE800 / 0x822DEC68); **only
  the band's meter fails the song** (0x822C6B98, 0x8234E1E0); one Star Power
  meter that both players must engage at once (0x822C80F8) and that both
  players' polls drain (a full meter lasts 6 measures with deploy_rate
  0.0833); the band's stars from `coop_max_scores.dta` (0x822CE5D8).
- Bass: no scoring difference was found; `kTrackBass` only changes the miss
  sound (`config/gen/beatmatch.dta`) [C for the data, I for "no other
  difference"].

## Career: tours, encores and cash

Details, the save and the store: `docs/career_and_store.md`.

- Tiers: 8 venues of 6 songs (campaign.dta `order`), the last of each the
  encore; Easy has 7 tiers (0x822A2BC8) and no encores (0x822A9B38) [C].
- `required_songs` (0x822A3468): Easy 4 (last tier 5), Medium 5 (6), Hard
  and Expert 6. The encore unlocks at `required − 1` songs passed
  (0x822A9578); at Medium and up only the encore completes the venue (with
  `required` passed), on Easy any song (0x822A9BB0); the next venue then
  unlocks with its songs [C].
- Passed = three stars or more (0x822AE8F8); status = completed tiers + 1
  when every tier song has five stars (0x822A9D30) [C].
- Cash (0x822A8078 / 0x822AA598): each new star 3 / 4 / 5 pays 150 / 200 /
  250 at Medium, Hard, Expert (0 on Easy), at the difficulty played **and
  every easier one** where the song had fewer stars (not for store songs);
  each new status pays `status_awards` (100 200 300 400 500 700 800 1000 0),
  plus the easier difficulties' for the same status unless a harder one
  completed the venue; $50 to start (not on Easy) [C].
- A tour passed / five-starred unlocks guitars.dta's reward guitars for the
  store (`in_stock` 0x822C02A8) and is announced (unlock_guitar_screen); a
  tour won unlocks pro face-off (Campaign +0x68) [C].
- An encore starts with the crowd full (SetRating 1, 0x822CCA60 /
  0x822DE530) and the encore intro; a song that can unlock the encore ends
  with `game_won_msg 1` (the crowd chants) or `encore_lost` [C].

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
2. ~~Co-op failing~~ Answered (M10.2): only the band's meter fails a
   co-op song (0x822C6B98, 0x8234E1E0); 0x822DE800 builds the band's crowd
   at the harder difficulty and maps the easier player's targets onto it.
   `docs/multiplayer.md`.
3. ~~Face-off section assignment~~ Answered: base+9 / base+10 set the mask
   bits, only with two players and `separate_parts` (0x823450B0; the
   functions named before, 0x82344910 / 0x82344EA8, parse text events).
4. ~~`min_length` and `recharge_rate`~~ Answered: `min_length` is copied to
   player +0xd4 (0x822C9720) and never read; the Star Power poll
   (0x822DBD60) has no recharge term [C as far as the export shows].
5. ~~Lefty flip, input sampling, latency~~ (GD02-037) Lefty changes the track's
   drawing (slot x mirrored, `0x82699E78`) and, on a joypad, the controller type
   (`lefty_joypad_guitar`, `0x8233c650`); a guitar's frets stay [C]. The
   controller latency is the sync offset, added to the input time at watcher
   `+0x2c` [C]. Sampling: the matcher reads the song time of its poll (`+0x8c`,
   `0x8233aa80`), so GH2's inputs land on frames; gd02 takes each event's own
   timestamp (`Mixer::songTimeMs`), at most a frame earlier [I: a deliberate
   difference, see the backlog].
5b. ~~Co-op star thresholds~~ Answered as decoded: 0x822CE5D8 uses
   `max_scores[song][1]` and `[3]` (the entries' difficulty keys) whatever
   difficulty was played; a song without max scores gets 5 stars.
6. Range of the whammy value (assumed 0…1) and the controller's calibration
   offset (`watcher +0x2c`, from the player's lag setting).
7. ~~Timing class~~ Answered: 0x822E2748 computes it (0x822D6260) and
   drops the result; no use.
8. ~~`player_mask` in single player~~ Answered: GH2's parser fills the
   mask only with two players and `separate_parts` (0x823450B0), i.e. in
   face-off; gd02's parser fills it always but applies it only in face-off,
   which is equivalent.
9. The bass-cue path of OnHit / OnMiss / wrong HOPOs (`+0x10` vtable
   `+0x28` by tick → `0x823526f8`, `bass_%d.cue`): which object answers and
   what chart data marks those ticks (see "Fret matching").

## Campaign ending: the win-game letter faded to black (issue #127, 2026-11-23)

**Symptom.** After beating the final career tier, the ending screen
(`win_game_screen`) rendered as a plain black screen in the port: the whole
end-of-game sequence "does not display correctly".

**What the original does.** The ending is a full-screen "fan letter"
newspaper, one per difficulty, in `ui/gen/win_<difficulty>.milo_xbox`
(`win_easy` / `win_medium` / `win_hard` / `win_expert`; `PanelDir`, 31
objects: a cinderblock wall, newspaper/tape/letter/skull/sticker meshes, and
five `BandLabel`s). `win_game_panel` (`ui/gen/endgame.dta`) loads
`{sprintf "win_%s.milo" {gamecfg get_difficulty_sym 0}}` and fills
`we_bandname.lbl` with the band's profile name; the labels' localized text
(tokens `win_easy`, `LETTERHEAD1..3`, from `ui/eng/locale.dtb`) is the
"Death Threat Records / The Management" letter.

`win_game_screen`'s enter runs
`{fade_panel start_fade WIN_GAME_FADE_IN_SCREEN_SEC (color 1 1 1)
(fade_synth FALSE) (fade_out FALSE)}` — a **white** overlay that **clears**
over 4 s to reveal the letter — and its `BUTTON_DOWN_MSG` leaves with
`(color 0 0 0) ... (fade_out TRUE)` (fade to black, 1 s).

`FadeOut::StartFade` (`Function_82318240` refs → start handler
**`0x822D5510`**) reads three keyword options, defaulting to
`color (0 0 0)` (string `0x82004bb0`), `fade_synth TRUE` (`0x82047bc8`) and
`fade_out TRUE` (`0x8201ea44`), plus the positional duration; the state is
stored by `0x822D50E0` (colour at +0x44..+0x50, fade_out at +0x59). The
per-frame alpha is `0x822D51D8`: with `progress = clamp((now-start)/dur,0,1)`,
`fade_out TRUE → alpha = progress` (0→1, the scene fades **to** the colour)
and `fade_out FALSE → alpha = 1 - progress` (1→0, the colour overlay clears).

**The discrepancy (port bug).** `FadePanelObj::start_fade`
(`engine/src/ui/panels.cpp`) read only `args[0]` (the duration) and **ignored
the `(color …)` and `(fade_out …)` options**, always drawing a
transparent→opaque **black** quad (`alpha = progress`, colour hard-coded
black). That is correct for `start_fade DURATION` (the pause/lose fade,
`game.dta`), but for the ending's `(color 1 1 1) (fade_out FALSE)` it did the
opposite of the original: instead of a white cover clearing to show the
letter, it faded the letter **to black**. Measured offscreen
(`SDL_VIDEO_DRIVER=offscreen`, `GD02_SCREEN=win_game_screen`): the letter was
visible at t≈500 ms, then darkened to ~100 % black by t≈8 s.

**Fix.** `start_fade` now parses the keyword options (same defaults as the
binary: black, fade to colour) and the draw uses
`alpha = fade_out ? progress : 1 - progress` with the parsed colour. After
the fix the screen starts white, clears over 4 s and holds the letter
(stable at t = 5/8/15 s); `(color 0 0 0) (fade_out TRUE)` on exit and the
plain `start_fade DURATION` path are unchanged. `--uitest` and the
`main`/`endgame`/`lose` screens are unaffected.

## Campaign ending part 2: the stone-venue UFO abduction finale (issue #127, 2026-11-23)

**The original finale, end to end.** After the final career tier is beaten,
the in-game sequence on the **stone** venue (Stonehenge; final tier songs
`beastandtheharlot carrymehome institutionalized misirlou hangar18 freebird`,
`config/campaign.dta`) is: the band plays its `win_finals` animation, a UFO
descends and **abducts the guitarist** with a tractor beam, then the game
hands off to `win_game_screen` (the "Death Threat Records" letter, fixed in
the section above).

Driver: `world/gen/world_objects_worldbase.dta`, the WorldDir base type, which
every venue's `WorldDir` inherits (`world_objects.dta` line 62
`#include world_objects_worldbase.dta`; the port loads this and sets it as the
`world` object's typeDef, `world.cpp` ~L289). Its `game_won_msg` handler
(fired by the game at song end, `WorldDriver::gameWon` → `world.message
("game_won_msg")`, binary `0x822CB5B8`):

1. `$category = $encore ? WIN_ENCORE : WIN`; then **`if {gamecfg
   win_campaign_song} {set $category WIN_GAME}`** (else, if `game
   want_encore_fx`, `(WIN_ENCORE_SONG WIN)`).
2. after `WIN_CAMERA_DELAY`, `pick_shot $category` → the stone venue's
   `win_game.shot` CamShot (`stone.milo`); for `WIN_ENCORE`/`WIN_GAME` it then
   schedules `game set_outro_complete` after `shot.duration / 30` s.
3. `set_lighting` to `{if_else {gamecfg win_campaign_song} (WIN_GAME WIN)
   {if_else $encore (WIN_ENCORE WIN) (WIN)}}`.
4. `{handle ($this game_won)}` — fires the `game_won` venue event.
5. **`{if {gamecfg win_campaign_song} {handle ($this campaign_won)}}`** — fires
   the `campaign_won` venue event only on the career's last song.

`campaign_won` (listed in `config/macros.dta` WORLDEVENTS) fires the stone
venue's **`win.trig`** EventTrigger (`world/stone/ng/gen/stone_geom.milo_xbox`),
which plays the abduction: anims `comet.filt`, `cometalpha.filt`,
`sky01_alpha1.filt`, `sky01_alpha.filt`, `starflare_filter.filt`,
**`ufo_win_fly.filt`** (the UFO's flight-in `TransAnim` `ufo01.tnm`/`ufo02.tnm`/
`ufo_parent.tnm`), `stars_black.filt`, `ufo02.filt`; and shows `win_sky.grp`,
`UFO2.grp`, `ufo.grp`. Supporting assets in `stone_geom.milo`: meshes
`ufo01.mesh` (60 KB), `ufo02.mesh`, `ufo_parent.mesh`, `ufobeam.mesh`;
textures `ufo01`, `ufo02`, `ufo_beam`/`ufobeam`, `ufo_second_stage`,
`ufo_glow01/02`, `ufo_flare`, `ufo_particle`, `ufo_emissive`; `ufobeam.mnm`
(the beam), `UFO_energy_field.trig`/`ufo_energy_field.mnm` (the energy field),
`ufolight.lit`, plus the night sky (`moon`, `comet`, `starfield`,
`starmessage.milo`). `world/stone/streams/wingame.mogg` is the victory music.
(The stone UFO is on stage throughout the song too, reacting to frets via
`UFO_fret_*`/`ufo_show_ufo_*`/`ufo_beam_move` triggers — those already play in
the port; only the `campaign_won` finale was missing.)

**What the port was missing.** `GameConfig::win_campaign_song`
(`natives_game.cpp`, binary `0x822B8B18`) was a stub returning `0`.
Consequently the worldbase `game_won_msg` handler — which the port *does* run
(the world shares the UI interp during a menu game, `app.cpp:2293`) — always
took the non-campaign branch: category stayed `WIN` (not `WIN_GAME`, so the
`win_game.shot` camera was never picked), lighting was `(WIN)` not
`(WIN_GAME WIN)`, and **`campaign_won` was never fired**, so `win.trig` never
ran and the UFO never appeared. `campaign_won` was a known world event
(`world.cpp` worldEvents) with no sender.

**Fix.** `gamecfg win_campaign_song` now returns `career && {campaign
final_song}` — the same test the port already uses for the band's
`campaignFinal` (`win_finals` + `kWinGameWalkSpot`), so the camera, lighting
and UFO all agree. `final_song` is still true at `game_won_msg` (the song is
not marked passed until `finish_song` on `post_show_screen`). No change to the
venue trigger machinery was needed — the port already parses and fires
`EventTrigger`s; it just never received `campaign_won`.
---------------------------------------------------------------------------------------------------
## 2026-11-23 — Issue #123: tutorial property read (shared root cause with gd03)

**Context:** Issue #123 reports gd02 tutorial trouble (bugged drawings; the
guitar audio track dropping out and "repercussing" into the rest of the game).
While reverse-engineering the tutorial I found the same engine-level defect that
breaks gd03's tutorial pause, and it is present here too with byte-identical
data.

**The defect (engine/src/script/script.cpp).** A property read
(`{o get key}` / `[key]`) fell back to `handler()`, whose list is built
back-to-front so later-added handlers win message dispatch (correct for
overrides) — but that returned the **last** duplicate `(key value)` array, while
Harmonix's `DataArray::FindData` returns the **first**. The `tutorial` object
(`new TutorialPanel tutorial`, ui/gen/tutorials.dta) declares the property twice:
`(state "")` at the top (line ~60) and a trailing `(state intro)` at the end
(line ~369). The original reads `""` during a lesson; the port read `intro`.

**Effect on the tutorial pause.** `tut_pause_screen`'s `BUTTON_DOWN_MSG` swallows
every button while its local `[paused]` is `FALSE`, and `[paused]` is armed by
the `pause` handler, which `enter` calls only when `{== {tutorial get state} ""}`
(tutorials.dta line ~503). With the port reading `intro`, the pause never armed:
the game was not actually frozen (`paused=0` while `tut_pause_screen` was up) and
Resume/Restart/Quit did nothing. Verified at runtime (`tutorials_screen` ->
tut_1 -> Start -> Resume): before, `paused=0` and the Resume press produced no
screen change; after, `paused=1` while the overlay is up and Resume pops back to
`tut_script_screen` with in-game input re-armed.

**Fix.** Added `ScriptObject::propDef(key)` (first-defined `(key value)` array,
matching `DataArray::FindData`) and used it from `getProp`/`hasProp`; message
dispatch (`handler()`, last-wins override) is unchanged. Objects that declare a
property once are unaffected. Validated: `--scripttest` ok, `--uitest` 26/0.
(`--walktest pause` fails identically on baseline and with the fix — a pre-existing
headless-audio artifact: 0 samples decoded / 195 device failures under the dummy
driver, "the synth stayed paused" — unrelated to this change.)

**On the reported drawings / guitar-track symptoms (honest status).** This fix
restores the tutorial pause. It is also a plausible *upstream* cause of the
"guitar audio track disappears, repercusses" report: before it, pressing Start
did not freeze the game, so the song kept running under a dead pause menu with no
hits to un-mute the player's guitar stem (the stem mutes on every miss). I could
not reproduce the bugged drawings headless, and I ruled out guitar-mute
persistence across songs (Mixer::play resets `guitarGain_`/`guitarTarget_` to 1
and loadSong resets `setVolumeDb(0)`), so if the drawings and guitar-track are a
*separate* defect from the pause, they remain open.

## Controls audit (2026-11-24)

Self-contained review of GH2's input handling vs our gd02 port, for the
cross-game control-model work. Scope: the raw input layer
(`engine/src/input/input.{cpp,h}`) and how gameplay consumes it
(`engine/src/app.cpp`, `engine/src/game/player.cpp`, `engine/src/game/rules.h`).
GH2 evidence is the decoded material already in this file (addresses only, no
decompiler text). GH2 is guitar-only (no drums/vocals). `[C]` = from the
game's code/data; `[I]` = inferred by the port for arbitrary SDL hardware.

### (a) GH2 input per area, with evidence and exact numbers

GH2 ran on fixed Xbox 360 hardware, so it has two distinct layers:

1. **Device → controller object.** A beatmatch controller type is picked per
   player at `0x822b3c50` from the joypad type that `joypad.dta` detects:
   `ro_guitar_xbox` → `hx_guitar_xbox_new` (or `hx_guitar_xbox` with
   `old_guitar`); `ro_guitar_xbox_old`/`_old_2` (debug) → `hx_guitar_xbox`;
   `ro_guitar` (PS2 guitar via a PC adapter) → `guitar`; anything else (a pad)
   → `joypad_guitar`, or `lefty_joypad_guitar` when the player's lefty flag is
   set [C]. The fret slots per type are `beatmatch_controller.dta`'s `slots`;
   `force_mercury kPad_Select` (Star Power) is in every type [C].
2. **Controller object → watcher (judgment).** `guitar`, `lefty_guitar`,
   `hx_guitar_xbox`, `hx_guitar_xbox_new` get the guitar watcher (strum bar +
   frets, ctor `0x8234e9e8`, selected at `0x8234a620`); every other type gets
   the joypad watcher (each fret press is a strum, ctor `0x8234e1f8`) [C].

Exact timing/threshold constants (all `[C]` unless noted), the numbers the
universal model must reconcile:

| Area | GH2 value | Evidence |
|---|---|---|
| Hit window (slop) | **±100 ms**, every difficulty | `beatmatcher.dta watcher slop 100` → watcher `+0x14`, ctor `0x82350ae0`; window test `0x8234f5d0` |
| Strum repeat filter | **52 ms** (`last + 52 >= now` drops it) | player sink `0x8233aa80`, constant `0x8205807c` = 52.0; guitar-mode strums only |
| Wrong-strum grace | **60 ms** (`now + 60` deadline) | watcher `+0xa8` = 60.0, constant `0x82002af8` |
| HOPO threshold | **170 ticks** global (strict `<`), per-song override **250** | parser `0x823341d0` |
| HOPO hit window | **−100 … +160 ms** | `0x8234f294..0x8234f2b0` (−slop … slop+60) |
| Overstrum report limit | **1 per 50 ms** | `0x8234f860`, constant `0x82002228` |
| Joypad chord window | **50 ms** (first correct fret opens it) | joypad watcher `+0x84` = 50.0 |
| Chord grouping | ≤ 10 ticks | chord test `0x82334630` |
| Whammy → audio pitch bend | counts once value rises **above −0.35**; bend = `pitch_bend_range(1) × min(Δt/ms_to_full_pitch_bend(1000),1) × amount` | `0x8234ff18`; `beatmatcher.dta` `pitch_bend_range 1`, `ms_to_full_pitch_bend 1000` |
| Star-Power whammy "moving" flag | speed **> 0.0005 /ms** (`whammy_speed×0.01`), held **500 ms** after the last move (`whammy_timeout`) | `0x822c8438`; scoring `star_power[8]`,`[9]` |
| Star-Power whammy fill | **0.034 / beat** (`whammy_rate`), only during an armed/captured sustain phrase | `0x822dbd60` |
| Star-Power ready / deploy | ready at meter **0.5** (`ready_level`); deploy = **held** engage, needs ready + not-active; drain **0.125 / measure** | ready `0x822dbd60`; deploy `0x822c80f8`; `star_power[4]`,`[1]` |
| Star-Power deploy triggers | **tilt sensor** OR **Select/Back** (`force_mercury kPad_Select`), instant | `0x822c80f8` |
| Controller latency | the player's lag setting is added to the input time at watcher `+0x2c` | set via watcher vtable `+0x40` |
| Strum bar buttons (guitar ctrl) | D-pad up/down ids `0xc`/`0xe`, also `0x10`/`0x12` | guitar controller `0x8234c538` |
| Input-live gate | controllers are OFF and drop buttons during the intro clock (song time < −25 ms) | off `0x822CED30`, on `0x822E2510` |

Note there is **no input-layer strum debounce timer** in GH2 beyond the
52 ms judgment-sink filter, and that filter applies to **guitar-mode strums
only** — joypad fret-strums and chord completions are not debounced (a 52 ms
filter there would block chords) [C/I].

### (b) Our gd02 port — what is faithful, and the gaps/errors

**Faithful (judgment layer).** `rules.h` / `player.cpp` reproduce every GH2
number above exactly: `slopMs 100`, `strumRepeatMs 52` (guitar strum only,
`player.cpp:91`), `pendingStrumMs 60`, `hopoThresholdTicks 170`/250,
`overstrumGapMs 50`, `joypadChordMs 50`, HOPO window −100…+160, SP whammy
speed 0.0005/ms + 500 ms hold (`whammyPoll`), SP fill 0.034/beat, ready 0.5,
deploy as a held engage (`starPowerEngage`). Frame-time stamping (`lastT_`,
GD02-152), the intro controllers-off gate (`controllersLive_`), the analog
whammy fed per frame (`setWhammyValue`, `app.cpp:1778/1782`), and the 300 ms
axis settle (`Pad::settleNs`) are all present. The judgment layer is **not**
the fragility and must not be touched.

**Gaps/errors (raw SDL→action layer, `input.cpp`).** These are the parts GH2
never had to solve (fixed hardware) and that the port infers for arbitrary
SDL controllers — this is where "very fragile" lives:

- **G1 — axis-as-button threshold is one global constant, uncalibrated.**
  `Input::sourceActive` (`input.cpp:927`) presses at `|value−rest| > 16000`
  and releases at `< 10000` (hysteresis), rest sampled during the 300 ms
  settle. On a 32767-span axis that is **≈0.49 to press / ≈0.31 to release**.
  It governs frets on triggers (`Ps2Guitar` green = RT; `Joypad` green = LT,
  blue = RT), the `XboxGuitar` alternate strum on LEFTY, and the whammy/gyro
  axis-any detection. GH3 (gd04) and Rock Band both use **0.75** for
  axis-as-button; our 0.49 is markedly lower, so a trigger or stick resting or
  jittering near half-deflection can flicker a fret/strum on its own. Lower
  threshold = more false positives. [I]
- **G2 — tilt (Gyro) has no debounce and no calibration.** `XboxGuitar` binds
  `Gyro` to `AxisAny` on RIGHTY (`input.cpp:208`) plus the Back button. A
  single frame of RIGHTY crossing ≈0.49 from the sampled rest emits
  `StarPower` immediately — no frame hold and no per-controller calibrated
  position. Contrast: GH3 deploys tilt only after a per-controller
  **calibrated `star_power_position`** threshold held **3 frames** (Select
  instant); Rock Band uses a "mercury filter" (0.93 held **12 frames**). GH2's
  own deploy is gated on the ready flag, which our port preserves
  (`player.cpp` ready check), so an empty meter can't deploy — but when ready,
  stick drift past ≈0.49 or a brief flick deploys SP by accident. `AxisAny`
  also means **either** tilt direction deploys. This is the most likely single
  cause of "accidental Star Power." [I]
- **G3 — guitar analog mappings are inferred and unverified.** Whammy = RIGHTX
  and tilt = RIGHTY on `XboxGuitar` are `[I]` (the real X-plorer's whammy/tilt
  reports are not traced; REA can't open the XEX2). A pad forced to the guitar
  type therefore reads right-stick motion as whammy **and** tilt at once.
- **G4 — input-layer Whammy action threshold (0.15) and deadzone (0.08) are a
  simplification**, not GH2's model. `Input::update` (`input.cpp:952-960`)
  applies a 0.08 deadzone and fires the `Whammy` *action* above 0.15. This
  only affects **digital** (key/button) whammy; analog pads skip the action
  (`app.cpp:1630 !a.analog`) and feed the per-frame value into the faithful
  speed-based SP flag, so SP whammy is correct. GH2's audio onset at −0.35 and
  the 0…1 value range are **uncertain** (open question 6) — document, don't
  "fix" the number blind.

### (c) Prioritized improvement list

**Small-safe (data-derived, unit-testable) — but gated on real-device logs.**
Per the owner's "objective checks, not eyeballing" rule, every input-layer
threshold change needs a real controller capture (expected-vs-measured), which
this environment cannot produce (no hardware; REA cannot open the XEX2). So
these are *prepared* fixes, not applied here:
1. Raise the axis-as-button press threshold toward the GH3/RB **0.75**
   (≈24500/32767) with release at ≈0.5, behind the existing hysteresis (G1).
   Smallest, most isolated change; add a `sourceActive` unit test with
   synthetic axis traces. Needs a drift/jitter capture to confirm it removes
   false frets without making real triggers feel stiff.
2. Give tilt (Gyro) a short frame-hold and a one-direction test (`AxisPos`,
   tilt-up) instead of `AxisAny`, mirroring GH3's 3-frame deploy (G2). Needs a
   guitar tilt capture to set the frame count/threshold.

**Needs-coordination (the universal control model).** These are the real
reconciliation items for one model across games:
3. **One axis-as-button threshold** shared by gd02/gd03/gd04 (GH1/2/3), with
   per-device calibration like GH3's `star_power_position` (G1+G2). The GH1/2
   "home" reference contributes: 52 ms strum filter in the **judgment** layer,
   guitar-mode only (not joypad/chords); ±100 ms slop; 60 ms wrong-strum
   grace; 50 ms overstrum and joypad-chord windows.
4. **Unified tilt/Star-Power deploy**: calibrated threshold + frame-hold +
   single direction, reconciling GH3 (3-frame, calibrated) and RB (0.93 ×12
   frames) with GH2's ready-gated held engage (G2).
5. **Verified guitar analog mappings** (whammy/tilt axis identity) from
   real-device captures before trusting RIGHTX/RIGHTY (G3).
6. Decide whether the input-layer digital-whammy action threshold (0.15) and
   the GH2 audio onset (−0.35) should be unified or kept separate (G4).

### (d) Fix applied

**None.** The judgment layer is faithful, and every candidate input-layer fix
(G1/G2) changes feel and, by the owner's input rules, requires real-controller
logs that cannot be captured here. A documented, number-exact findings set
with no risky change is the deliverable.

### (e) Uncertain / blocked

- GH2's raw whammy value range and the −0.35 audio onset vs a 0…1 bar remain
  open (this file's open question 6); the X-plorer's whammy/tilt axis identities
  are untraced (REA can't open the XEX2).
- The GH3/RB numbers (0.75 axis-as-button, 3-frame tilt, 0.93×12 mercury) are
  from the sibling audits (gd04/Rock Band), cited for reconciliation, not
  re-verified here.
