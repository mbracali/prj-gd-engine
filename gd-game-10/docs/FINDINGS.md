# Guitar Hero: Metallica (Xbox 360): findings

Each line **[C] address / data** (confirmed on our disc) or **[I]**
(inferred). Addresses are in our `default.xex` (title ID 41560830).

## Executable

- XEX2, title ID 41560830, media ID 2E0C8E5F, base 0x82000000, entry
  0x821B8BF8 [C: XEX headers].
- Ghidra: 30,337 functions after `FixXexFunctions.java`, 9,246 strings,
  94,894 data references [C: export of 2026-11-10].

## Data

See `formats.md` (CHNK, padded paks, QB struct items, type 0x1C).

## Front end

- UI state machine and descs: see `ui.md` (task_menu prefixes [C 0x82025698
  .. 0x820256E4, 0x823090D8 .. 0x8230A7C0, 0x82309028]).
- `default_lag_settings.xenon`: gem_lag_ms -45, input_lag_ms -61 (GH3's
  xenon_gem_offset / input_offset were -66) [C data].
- Health tables per instrument and difficulty (beginner .. expert) [C data].

## Audio

- Song banks: key generator [C 0x820E24F8], called for "%s_%d", n = 1..3,
  by [C 0x820E6948] (opening "music/%s"); previews by "a%s_preview.fsb"
  [C 0x820E27E8] (another path, not done).

## Multiplayer (2026-11-23 battle-mode deep dive)

Local multiplayer (co-op career, Face-Off, Pro Face-Off, Battle) is modelled
in `game/match.cpp` + `game/player.cpp`; the two-player HUD in `render/hud2p`
and the gh4 HUD in `ui/hud_widgets` / `ui/song_hooks`. The MP mode-select
offers FACE-OFF, PRO FACE-OFF, BATTLE, CO-OP [C data `menu_mp_select_mode.qb`
checksum list `[$dont_care,$p2_faceoff,$p2_pro_faceoff,$p2_battle,$p2_coop]`].
`--selftest` auto-plays co-op / face-off / battle per chart.

### BUG (fixed): Battle mode never started -- wrong power-up set

- Original: `battlemode_powerups` (`scripts\guitar\guitar_battle.qb`) on our
  disc holds **nine** power-ups, by `name`: `Lightning`, `DifficultyUp`,
  `DoubleNotes`, `PowerUpSteal`, `LeftyNotes`, `BrokenString`, `WhammyAttack`,
  **`Mines`** (`scr=battle_mine_attack`), **`blackout`** ("Hit The Lights",
  `scr=battle_blackout_attack`). GH3's eighth, **DeathLick**, is gone and
  Metallica has **no battle sudden death** (no `death_lick` / `sudden_death`
  in `guitar_battle.qb`). `max_num_powerups=3`, `battle_do_or_die_attack_scale
  =1.0`, `battle_*_drain_offset = {0,1500,3000,4500}` [C data, our disc].
- Port discrepancy: `match.h` hard-coded GH3's eight-attack enum
  (`...,kWhammy,kDeathLick,kAttacks=8`) and `BattleData::load` returned
  `powerups.size()==kAttacks(8)`. With Metallica's nine entries `load` returned
  false, so `Match::start` failed and **Battle mode could not begin at all**
  (err "the battle tables ... are missing"). The self-test silently skipped it
  (0 battles / 0 attacks / 0 KOs). The `kDeathLick=7` enum slot also collided
  with Mines (index 7), and index 8 (blackout) fell through to no-op.
- Fix: attacks now mapped **by `name`** into a canonical enum
  (`...,kWhammy,kMines,kBlackout,kAttacks=9`), `BattleData::load` fills a
  nine-slot table and requires every attack present (unknown names skipped,
  not fatal). The seven shared attacks (indices 0-6, byte-identical to GH3)
  are unchanged. `startAttack` adds `kMines` (victim mine window) and
  `kBlackout` (victim highway-dark window + Easy `params.easy_drain`); the
  DeathLick sudden-death select line and the DeathLick fire-bolt/heartbeat are
  removed. `song_hooks` iterates all nine, drops the DeathLick heartbeat, and
  plays `GH_SFX_BattleMode_Mine_Release_P<n>` for Mines.
  Files: `game/match.h`, `game/match.cpp` (`BattleData::load`, `select`,
  `fire`, `startAttack`, `battleFrame`), `ui/song_hooks.h/.cpp`,
  `tests/selftest_play.cpp` (start failure now hard-fails).
- Test: `--selftest` battles 0 -> **102 (2745 attacks, 7 KOs)**, `charts
  failing 0`, co-op/face-off unchanged. (31 FAILED are pre-existing missing
  DLC `.fsb.xen` audio, unrelated.)
- Still to do (grounded, handed off -- touches note-track / highway render):
  **Mines** place mine gems on the victim via `FillInputArrayForBattle
  player=.. attack=mine_attack` and cost health when strummed -- the gem
  placement + strum damage are not yet reproduced (only the card/drain/HUD
  book-keeping + window are). **blackout** darkens the victim's highway
  (`GHM_BattleMode_HitTheLights_*`, `blackout_highway`) -- the Match sets the
  `blackoutUntil` window; the highway renderer must read it to go dark.

### BUG (confirmed, NOT fixed -- separate lobby/flow subsystem): GD10-071

- Local multiplayer cannot be reached by a player: the MP lobby advances
  boot -> main menu -> `uistate_select_controller` -> `uistate_select_mp_mode`
  -> `uistate_character_hub`, then **never proceeds to `uistate_setlist`** when
  both players ready up (Start + P2 Start). Reproduced with
  `--walktest battle` (and faceoff/coop go through the same hub). The gh4
  band-mode lobby (`ui_band_mode.qb`, `ui_character_hub.qb`) ready->proceed
  transition is not driven. Script-driven flow bug, distinct from the in-game
  rules above; the dev hook `GD10_DEV_GAME_MODE` bypasses the lobby. Left for
  routing (flow/front-end), not fixed here to avoid a risky blind flow change.
## Career mode (RE 2026-11-23)

Deep-dive of the original career flow (data in `qb.pak.xen`:
`scripts\guitar\guitar_progression*.qb`, `...\menu\career_menu_flow.qb`,
`...\menu\states\layouts\ui_game_mode.qb`,
`...\ui_singleplayer_character_hub.qb`, `...\cas\ui_character_selection.qb`;
dumped with `tools/qbscript.py dis` and `tools/qb.py`). The career is
**script-driven**: the engine interprets the disc's QScript; the port's C++
supplies natives and the atoms engine. All claims below are [C scripts /
data] unless marked [I].

### Original progression model (differs from the GH3 model the code assumes)

- Metallica career is **gig/tier + star based**, not GH3's "win N songs".
  Each part has a `GH4_Career_<part>_Progression` atom list with only three
  atoms: `set_initial_states` (`Progression_Career_<part>_Init`),
  `<part>_career_gig1_complete` (`Progression_Career_Gig_Complete`, condition
  `scr Progression_Career_Check_Gig_Complete {gig=1}`) and
  `end_of_first_update`. No `depends_on` atom chains, no per-difficulty
  `required` arrays, no encore/boss atoms — so the port's `atoms.cpp`
  difficulty-`required`/encore/boss/relock paths are **dormant** for gd10.
- **Song complete** = `GetGlobalTags <song_checksum> params=stars` with
  `stars > 2` at any of the 5 `difficulty_index` values, i.e. **>= 3 stars at
  any difficulty** (`Progression_Career_Check_Gig_Complete`). A gig completes
  when every song in `tier<gig>.songs` is complete.
- **Tier unlock** = cumulative stars: `progression_check_for_total_stars`
  returns true when `tier.stars <= progression_total_stars_unlocked`;
  `progression_get_total_stars` sums each song's best-difficulty stars over
  all tiers (minus boss songs). `GH4_Career_Guitar_Songs` has `num_tiers = 9`,
  `prefix = 'career_guitar'`, per-tier `stars`/`encore_stars` thresholds
  (tier2 6, tier3 16, tier4 26, tier5 36, tier6 50, ...).
- Per-song-per-difficulty tag key: `format_globaltag_song_checksum` =>
  `FormatText '%s%p%d' s=<songname> p=<part text_nl> d=<difficulty text_nl>`
  hashed (`addtostringlookup`). Writes (`Progression_SongWon`,
  `update_song_star_rating` -> `player<i>_status.stars`, min 3) and reads
  (gig check) use the same script, so the checksum is self-consistent.
- Entry: main menu CAREER -> `main_menu_select_career` (`change game_mode =
  $p1_career`) -> `uistate_game_mode`; SINGLE -> `game_mode_select_career`
  (no `$band`: `game_mode=$p1_career`, 1 player), BAND (`$band`:
  `game_mode=$p2_career`, co-op, `uistate_band_mode`).

### BLOCKER: career mode is currently unreachable in the port (out of scope)

- `ui_create_game_mode` sets `first_character_hub_enter = 1` on every career
  entry. After instrument+difficulty the flow pushes
  `UIstate_singleplayer_character_hub`; `init_singleplayer_character_hub`
  only sets `first_character_hub_enter = 0` (and proceeds, returning `$true`)
  when `GetGlobalTags <savegame> last_singleplayer_character != none`.
  Otherwise it spawns `character_hub_continue_to_selection`, which waits for
  `menu_change uistate_character_selection`.
- `uistate_character_selection` focus runs `new_custom_char_focus` ->
  `generate_random_appearance` / `cas_queue_add_request` (CAS,
  `scripts\game\cas\cas_build_queue.qb`). `last_singleplayer_character` is set
  **only** by CAS paths (`choose_character_to_play`,
  `exit_save_changes`/`exit_skip_save`); `reset_globaltags` sets it to `none`.
- The port **does not implement CAS** (confirmed `tests/walk.cpp:1205` "the
  port builds no CAS characters yet, cas_queue never empties"; the `cas_*`
  scripts have no native support). Observed with `engine/build/linux-video`
  (video build reusing the repo's ffmpeg prefix), `--walktest career`:
  `uistate_select_difficulty` -> `UIstate_singleplayer_character_hub` ->
  `uistate_character_selection` (focus hits `cas_queue_add_request`) ->
  `menu_back uistate_select_difficulty`, looping; the walk never reaches the
  setlist/song. `--walktest quickplay` passes on the same build.
- Consequence: because the **first-ever** career entry can never set
  `last_singleplayer_character` without CAS, all of single-player career
  (hub, gig board, venue, setlist, the progression above, cash rewards,
  autosave) is unreachable. This is a **character-creation/selection**
  dependency (another agent's domain), not a defect in the career logic.
- Candidate minimal unblock (for the CAS/character agent to decide, NOT done
  here): on career entry with no custom character, seed
  `last_singleplayer_character` to a premade character (the script already
  falls back to `judy` via `profile_exists`), analogous to the existing "The
  Glitches" band stand-in; needs a premade singleplayer character that the
  port renders, and `cas_queue_new_character_profile` to no-op cleanly.

### Port notes (not player-visible bugs; for cleanup/other agents)

- `ui/glitches.cpp` `ensureGlitches()` (+ its `unlockList`, GH3-era names
  `GH3_Career_Songs`, `careertier`, `num_songs_to_progress`,
  `encore_unlocked`, `boss_unlocked`) is **dead code** — no caller; the port
  unlocks The Glitches via the original `unlock_all_profiles` /
  `purchase_all_profiles` scripts (`front_end.cpp:265`). Those GH3 names are
  not in gd10's data.
- `script/natives_core.cpp` `GetGlobalTags`/`SetGlobalTags` (lines 433/459)
  ignore the `savegame` and `all_active_players` params; fine for the single
  profile (read/write collapse to one namespace, so the gig star-check stays
  consistent), but would matter for band/multiplayer careers that write per
  savegame (note for the multiplayer agent).
- `SetProgressionMaxDifficulty` is a no-op stub (`natives_flow.cpp:67`); not
  exercised by the gig model above.
- `ui/front_end.cpp:84-98` forces `game_mode=p1_career` on the main-menu
  CAREER choose. The original `main_menu_select_career` already sets it; the
  C++ sets it before the game_mode screen's Single/Band choices are built
  (timing), so it is belt-and-suspenders, not required for correctness.
