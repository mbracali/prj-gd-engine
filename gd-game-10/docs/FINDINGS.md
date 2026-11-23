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
