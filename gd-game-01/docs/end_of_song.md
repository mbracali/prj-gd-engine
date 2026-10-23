# GH1 star rating base, Player stats, and the end-of-song flow

[C] means read directly from the decompilation or the ELF bytes. [I] means inferred.
Sources: analysis/decomp/*.c, the ELF at original_files/SLUS_212.24, and extracted_dta/.

## 1. What B is: `base_max_score`

The chain:

- `FUN_001062a8(DAT_002edb60)` returns `*(Game+0x80) + 0x28`, the **SongDB** embedded in the game object. SongDB's ctor is `FUN_0010b7b8` and its vtable is `SongDB_vtable` @0x2f6900. [C]
- SongDB+0x5c is a **`MultiplayerAnalyzer`** (ctor `FUN_00131880`, vtable @0x2f7e68). [C]
- `FUN_00131bb0(analyzer, p)` returns `analyzer->vec.begin[0][p]`. The analyzer holds a vector of `int[2]` entries, one per song load, and B is entry 0 indexed by player. [C]
- Script access: the BeatMatch/Game handler (`BeatMatch_v1` @0x10f9c8) answers the message **`base_max_score <player>`** (string @0x3002b0) with `FUN_0010bac8(SongDB, player)`. `charsys/charbase.dta` calls `{beatmatch base_max_score 0}`. [C]

### How the array is filled

- `FUN_0010e040` → `FUN_0010bb48` (SongDB::LoadSong(midi_file, difficulty)) runs once while the game is built (`FUN_0010db90`). It does three things: [C]
  1. `FUN_00131980(analyzer)` pushes a zeroed `{0,0}` entry and sets `analyzer+0x30` to point at it. [C]
  2. It parses the MIDI (`FUN_00260ca8` → `FUN_00260d10`). [C] Each finished gem goes through this path:
     - `DifficultyParser` `FUN_002619d0` (on note-off) calls `SongData` vt+0x1c (`SongData_v2` @0x261170, AddGem). [C]
     - That forwards to each registered sink. `SongDB` was registered by `FUN_00260dd0`. [C]
     - `SongDB_v2` @0x10bce8 forwards to analyzer vt+0x1c = **`MultiplayerAnalyzer_v2` @0x131bd0**. [C]
     - Only the parser running at the selected difficulty emits gems (parser+0x14 == 0, `FUN_002627b8`). [C]
  3. Afterwards it sets `SongDB+0x40 = B[0] / B[1]` as a float (`FUN_00131b90`). [C]

The gem record passed to the sink is a stack struct built in `FUN_002619d0`: [C]

| off | field |
|---|---|
| +0x00 | parser+0x1c |
| +0x04 | fret mask (frets whose note-on is within 10 ticks of each other) |
| +0x08 | start_ms |
| +0x0c | dur_ms |
| +0x10 | start_tick |
| +0x14 | **dur_ticks** (int) |
| +0x18 | 0 |
| +0x1c | **no_sustain** = `dur_ticks < 161` |
| +0x20 | **player_mask** (byte, set by player-section notes; 0 means both players) |

### Exact formula (`MultiplayerAnalyzer_v2` @0x131bd0) [C]

```
head = scoring.points.head (50)   // analyzer+0x18, read in FUN_00131880 from strings
tail = scoring.points.tail (25)   // analyzer+0x1c   "scoring"/"points"/"head"/"tail" @0x305cc0..d8
for each gem g in the chart (one gem per chord):
  for p in {0,1}:
    if g.player_mask == 0 || (g.player_mask >> p) & 1:
      t = g.no_sustain ? 0 : (tail * g.dur_ticks) / 480     // C int division, truncates
      B[p] += (head + t) * popcount(g.fret_mask)              // popcount table @0x2f2160 [C]
```

- The sum runs at multiplier 1. There is no streak multiplier and no Star Power ×2. [C]
- Chords count head and tail **per fret**. [C]
- Sustains count only when dur_ticks ≥ 161. The tail is **truncated** per gem, then multiplied by the fret count. [C]
- This differs slightly from real play, which uses `round(tail × dur/480)` (`FUN_0010b3d8`, round-half-away). So B can be a few points lower than a perfect run's sustain total. [C]
- In single player every gem has mask 0, so B[0] == B[1] and the handicap ratio is 1.0. [C]/[I]

Examples:

- one green note: 50
- G+R chord with a 480-tick sustain: (50+25)×2 = 150
- single note, 200 ticks: 50 + (25·200)/480 = 50 + 10 = 60
- single note, 160 ticks: 50

## 2. Stars, from the Player_v1 messages (`Player_v1` @0x1174f8)

### Message strings

| message | address |
|---|---|
| `percent_complete` | 0x3018a0 |
| `score` | 0x301790 |
| `notes_hit` | 0x301920 |
| `percent_hit` | 0x301930 |
| `current_streak` | 0x301940 |
| `longest_streak` | 0x301950 |
| `win` | 0x301a48 |
| `lose` | 0x301a50 |
| `on_game_over` | 0x301a68 |
| `num_stars` | 0x301a78 |
| `star_rating` | 0x301a88 |

All [C].

### Player fields

| offset | field |
|---|---|
| +0x10 | player index |
| +0x14 | current song ms (written each poll) |
| +0x1c | raw score |
| +0x38 | game-over latch |
| +0x3c | SongDB* |
| +0x44 | notes_hit |
| +0x48 | current streak |
| +0x4c | longest streak |

All [C].

### Per-message behaviour

- **`score`** (`FUN_00116c80`): `(int)((float)raw_score × h)`. [C]
  - h = 1.0 for player index 0. Otherwise h = SongDB+0x40 = (float)B[0]/(float)B[1] (`FUN_0010bab0`). This handicap normalises P2 when the players' charts differ. [C]
- **`num_stars`** (`FUN_00117420` → `FUN_0011bdd0` → `FUN_0010b550` → `FUN_0010b5e0`): [C]
  - The input is `S = score` (the handicapped score above), `B = base_max_score(player_index)`, and the row `star_ratings.thresholds.<difficulty>` = `(kDifficultyX 2.0 3.0)`, where difficulty = GameConfig+0x90.
  - The loop runs i = 2 down to 1. If `thr[i] * (float)B <= (float)S` it returns `i + 3`. If neither passes it returns 3.
  - Net result: **5 if S ≥ 3.0·B, 4 if S ≥ 2.0·B, else 3** (float comparison, inclusive). The minimum is 3; there are no 0–2 star results.
- **`star_rating`** (`FUN_00117450` → `FUN_0010b680`): the symbol `star_ratings.symbols[num_stars−3]`, one of `3_star`, `4_star`, `5_star`. The endgame screen shows it. [C]
- **`percent_complete`** (`FUN_001173e8` / `FUN_00117388`): `(int)(min(1.0, now_ms / SongDB.end_ms) × 100)`, truncated. [C]
  - now_ms is player+0x14, set in the poll `FUN_00116498`.
  - `SongDB.end_ms` (+0x38) is set by `SongDB_v11` @0x10c238 when the MIDI text event **`end`** (string @0x2ffa90) is parsed. It is clamped to `GameConfig+0x9c` = `set_song_limit` seconds ×1000 (default FLT_MAX, @0x110000:2124/2962). [C]
  - The same ratio is the "song progress" used by the crowd meter's free_ride (`FUN_001171b8`). [C]
  - `lose.dta` shows percent_complete. [C]
- **`percent_hit`** (`FUN_001174a0`): `(int)((float)notes_hit / (float)N × 100)`, truncated. [C]
  - N (`FUN_0010c608`) is the number of gems in the difficulty's gem list whose player_mask is 0 or includes the player (`FUN_0025b688`). A chord counts as 1.
  - notes_hit goes up by 1 per hit gem (`FUN_0011d070` in `FUN_00116580`).
- **`win [n]`** (`FUN_00117290`): applies only if the latch is clear **and player index == 0**. [C]
  - If n > 0 (a debug cheat), it sets raw_score = `FUN_0011be48(n)` = `FUN_0010b6f8`. That returns 3 for n == 3, otherwise `(int)(thr[n−3]·B[0]) + 1`. Note the index here is n−3, so n=4 uses `thr[1]`=2.0. [C]
  - It then calls `FUN_0016a640(arena, 2)` and sets the latch. [C]
- **`lose`** (`FUN_001172f8`): if the latch is clear, calls `FUN_0016a640(arena, 3)` and sets the latch. [C]

## 3. End-of-song flow

### Triggers (C++)

- **Song end**: in the Player poll `FUN_00116498(now_ms, …)`, `if (SongDB.end_ms < now_ms) win(0)`. This is checked before `+0x14` is updated. [C]
  - It runs every frame, but only player 0 can win (index check) and the latch makes it fire once. [C]
  - So **game over happens on the first frame after the MIDI `[end]` text event time**, not at the last note. [C]
  - If a song had no `end` event, end_ms would stay 0 (ctor) and the win would fire immediately. [I] All shipped songs presumably have one.
- **Fail**: `FUN_001171b8` (crowd update on hit/miss/sustain release). If crowd_rating_active (+0x64) and `FUN_0011d3b8` (raw < lose_level) and **GameConfig+0x8c == 0**, it calls `lose`. [C]
  - Correction to FINDINGS: GameConfig+0x8c is **`multiplayer`**, not a "no-fail" flag. The `multiplayer` getter (string @0x301030) returns +0x8c (@0x110000:2806), and `set_multiplayer` writes it (@0x114548). So there is no fail in multiplayer. [C]
  - The fail is evaluated only when a crowd update happens, on a note event, not every frame. [C]

### `FUN_0016a640(arena @0x363748, result)` [C]

1. Sets DataArray variables:
   - `arena.game_over` = 1 (0x3104d0)
   - `arena.game_won` = (result != 3) (0x3104e0)
   - `arena.game_result` = result, 2 = won, 3 = lost (0x310520)
2. Sends the message **`game_over`** to the arena object (vtable +0x14 Handle).

`game_won`, `game_lost` and `game_outro` are not referenced as strings in C++. The scripts and the guitarist send them. [C]

### Script fan-out on arena `game_over`

All of these are `arena add_handlers`; each file's handler runs. [C] for the content. Execution order between files is [I]: likely load order.

- `arena/arena_game.dta`: `{player_mgr foreach_player $p {$p on_game_over}}`, `stop_sfx sp_deployed(_multi)`.
- `arena/venue.dta`: `{if_else $arena.game_won {arena game_won} {arena game_lost}}`. **This sends game_won or game_lost.**
- `arena/camera.dta`: final-win shot, or after WIN_CAMERA_DELAY a WIN shot, or a LOSE shot.
- `charsys/guitarist.dta`:
  - 1P: `{guitarist0 set_game_over $arena.game_won}`.
  - 2P: each guitarist gets `set_game_over (score >= max score)`.
- `crowd.dta`, `theband.dta`, and the venue dtas also react to game_won / game_lost (lights, band kBandWin/kBandLose).

### `arena game_won` (arena_game.dta)

`track_panel explode`, TRACK_EXPLODE_SFX, `game set_realtime TRUE`, `hud set_in_game FALSE`, crowd_win sfx, **`beatmatch set_paused TRUE`**, `hud set_win_showing TRUE`, then after **WIN_MESSAGE_MS = 3000 ms** `hud set_win_showing FALSE`. [C]

### `arena game_lost`

`hud show_message 0 song_failed`, `track_panel set_showing FALSE`, `game set_realtime TRUE`, `hud set_in_game FALSE`, `hud set_score_showing FALSE`, crowd_lose sfx. Then it interpolates music speed 1.0 → 0.0 over **LOSE_MUSIC_SLOW_MS = 2000 ms**, and after 2000 ms `beatmatch set_paused TRUE`. [C]

### `game_outro`, the timing

Guitarist `set_game_over(won)` (`FUN_00284fd8`):

- It plays the game-over clip on main.drv (and the hand set):
  - `kGuitarWinFinal` (0x4000000) if final_win_sequence (`FUN_00282818`). [C] for the flag, [I] for the name.
  - otherwise won → `kGuitarWin` (0x200000)
  - otherwise lost → `kGuitarLose` (0x40000), or `kGuitarLoseWalk` (0x8000000) while walking
- Mode: lost → 3 (kAnimFirst) on main, won → 1 (kAnimReset). The mapping is in the decompile. [C]
- It sets state +0x108 = 4. [C]

Each frame, `GuitarCharacter_v13` @0x284390 checks:

- state == 4, and
- the driver's current clip has `end <= current frame` (clip+0x10 vs entry+0x14), and
- the clip flags & 0xC240000 (any of the four game-over flags).

When all three hold, it sends **`game_outro`** to the arena (string @0x3415f8). [C] for the condition, [I] for the end/frame field naming. It repeats every frame while the condition holds; arena_game.dta's `$game.outro_played` makes it run once. [C]

So **game_outro arrives when guitarist0's win or lose animation (a kAnimRealTime clip, played in real seconds) reaches its last frame**. Its delay after game_over equals that clip's length plus any blend. It is not a fixed timer. [C]/[I]

`arena game_outro` then runs: `arena set_depth_of_field FALSE`, `synth stop_all_sfx`, `game set_paused TRUE`, `hud set_showing FALSE`, then:

- if won: `hud set_win_showing FALSE`, the career final_song check sets `meta won_campaign`, then `ui goto_screen {if_else $auto_restart loading post_show}`
- otherwise: `ui goto_screen {meta get lose_screen}` [C]

To reproduce without characters, use a fallback timer of about the clip length (the win/lose .acp `(end−start)/fps` seconds). [I]

### GamePanel_v13 @0x104b00 (the game panel's load/enter)

It does **not** send end-of-game messages. [C] What it does:

- Calls HudPanel_v13.
- Collects the per-player objects (player 1 only if +0x144, i.e. two players) into a new `FUN_0010a2a0` object stored at +0x1a0.
- Unless `DAT_002edc54+0x1a0` is set, it looks up the UI panel **`endgame`** (1P) or **`multi_compete`** (when GameConfig.multiplayer) and **preloads** it: `FUN_00205b60` increments the load refcount and loads on the first reference. The panel is stored at +0x1a8.

So it is only a preload of the post-game result panel. [C] for the actions, [I] for the purpose. `endgame.dta`'s enter shows `score`, `percent_hit`, `longest_streak` and `star_rating`, and uses `num_stars` for career.

## Reimplementation checklist

1. While parsing, compute `B[p] = Σ_gems (50 + (gem.no_sustain ? 0 : 25*dur_ticks/480)) * popcount(frets)`, with int truncation and dur_ticks < 161 counting as no sustain. Filter by player mask.
2. Stars: `S >= 3*B` gives 5, `S >= 2*B` gives 4, else 3.
3. `percent_hit = (int)(100*hits/gems)`. `percent_complete = (int)(100*min(1, now/end_ms))`, where end_ms is the time of the `[end]` text event.
4. Song end: on the first frame where `now > end_ms`, set game_won=1 / result=2 and send `game_over`; venue.dta turns that into `game_won`. On a fail (rock meter below lose_level, not multiplayer, checked on note events), set game_won=0 / result=3 and send `game_over`, which venue.dta turns into `game_lost`.
5. Send `game_outro` when the guitarist's win/lose clip ends. It is guarded once. Then go to post_show or lose_screen.
