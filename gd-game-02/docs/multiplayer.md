# Multiplayer: co-op, face-off and pro face-off (M10.2)

How Guitar Hero II (Xbox 360) plays two players, traced in `default.xex`
(`ghidra/analysis/`, image base `0x82000000`), and how gd02 does it.
**[C]** = confirmed in the code (address) or the game's data, **[I]** =
inferred. The single-player rules are in `FINDINGS.md`; the career's co-op
records in `career_and_store.md`.

## The modes (`config/modes.dta`)

| Mode | Shown as | Players | competitive | separate_parts | Score config | HUD | Chart / audio |
|---|---|---|---|---|---|---|---|
| `multi_coop` | Co-op | 2 | FALSE | FALSE | `coop_config` | `hud_coop` | lead + bass or rhythm; `song_coop`, `midi_suffix COOP` |
| `multi_vs` | Face-off | 2 | TRUE | TRUE (default) | `default_config` | `hud_vs` | the guitar part, `midi_suffix VS`, `song_vs` |
| `multi_fo` | Pro face-off | 2 | TRUE | FALSE | `default_config` | `hud_vs` | the guitar part, both play every gem |

`multi_vs` is **face-off** and `multi_fo` **pro face-off** [C]: the main
menu's `versus.btn` sets `multi_vs` and `faceoff.btn` (shown only when
`campaign is_unlocked multi_fo`, a tour won) sets `multi_fo`; `multi_fo`'s
results show `me_pfo_diff` and its difficulty screen is the single
`sel_difficulty_panel`; the world's camera filter for face-off sections
(`active_players_changed`) runs for `multi_vs` (`world_objects_worldbase.dta`)
[C data]. (The first FINDINGS table had them the other way round.)

- `game multiplayer` = more than one player; `game multiplayer_vs` =
  more than one player and the mode's `competitive` (0x822B8B18 ->
  0x822B37F8) [C]. `midi_parsers.dta` then reads the players' parts: in
  co-op `PART GUITAR COOP` for the guitar, `PART BASS`, `PART RHYTHM`; in
  the competitive modes `PART GUITAR VS1` / `VS2`, which no chart has (the
  parser falls back to the plain track [I]).
- The co-op lead plays `PART GUITAR COOP` (the parsers' track names,
  `midi_suffix COOP`) [C data]; bass / rhythm their own part (there is no
  `PART BASS COOP`) [I: the fallback].

## Charts: who plays a gem

- The face-off section notes (base + 9 = player 1, base + 10 = player 2, as
  GH1) set the gems' `player_mask` only when the parser has more than one
  player (`+0x38`, 0x822BD8B8) **and** its flag `+0x84`, which is the song's
  `separate_parts` (0x82346FA8 gets it from song data +0x10, set by
  0x822836B0 from the mode in 0x822BC818) [C 0x823450B0]. So only face-off
  (`multi_vs`) has masks; single player (one player), co-op and pro
  face-off (`separate_parts FALSE`) have none. This answers FINDINGS open
  questions 3 and 8: GH2 never fills masks in single player.
- A gem belongs to a player when its mask is 0 or has the player's bit
  (0x82347688) [C]; another player's gems are swept without penalty and are
  not drawn on that player's track [I: one gem list per player, 0x8233FE50].
- The base score (0x826A0AB0) counts, per track, only the gems the track's
  player owns [C].

## Scores

- **Normalisation** (`use_normalization`, Game +0x265, TRUE from the
  constructor 0x822BB048) [C]: after loading, r = base score 0 / base score 1
  when there are two tracks, else 1 (0x826A0A50, stored at +0x1c by
  0x822BC818); every point of player 0 is × (1/r + 1)/2, of player 1 ×
  (r + 1)/2 (0x822BB320, applied in 0x822C6A58) [C]. Co-op lead and bass
  are thus worth the same; face-off players with different sections or
  difficulties too; pro face-off (one chart, one difficulty) gets 1.
- The score is a float (Performer +0x6c): `score += multiplier × points`
  (0x822DCFB0), read with truncation (`fctiwz`, 0x822DCB80) [C]. Single
  player only ever adds integers, so nothing changes there.
- Each player's stars use its own base score (0x822D6410) [C].

## Co-op

GH2 builds a band performer (0x822DE170, vtable 0x82048EE4) when the mode has
`shared_star_meter`, and co-op players (0x822DE970, vtable 0x82049004) that
hold it at +0x114 (0x822C9E80) [C]. gd02: `game/band_performer.*`.

- **Streak**: each hit also hits the band (0x822DEBA8 -> Performer
  0x822DDF70: the band's streak + 1, `build_streak`); each penalty ends the
  band's streak too (0x822DEBF8) [C]. The band's multiplier is its streak's
  (the same `streaks` table) × player 0's Star Power multiplier
  (0x822DD1F8 -> 0x822DB690) [C].
- **Band score**: every point a co-op player scores, normalised, is also
  added to the band at the band's multiplier (0x822DEB30 -> 0x822DCFB0) [C].
- **Crowd / fail**: a co-op player's crowd update (0x822DEC68) updates its
  own meter (weight 1) and then the band's with target `a + (b − a) ×
  target` and weight × `w` (player +0x118 / +0x11c / +0x120) [C]. The band's
  crowd uses `coop_config` at the **harder** difficulty (0x822DE800 ->
  0x822DE640); the **easier** player (player 1 on a tie) gets
  `w = note_weight(easy) / note_weight(hard)`, `a = (hi_e·lo_h − lo_e·hi_h)
  / (hi_e − lo_e)`, `b = (hi_h − lo_h)/(hi_e − lo_e) + a`, which maps its
  difficulty's range onto the band's; the other player `0, 1, 1` [C]. The
  band's Star Power crowd boost is player 0's (0x822DD1B8) [C].
- Only the **band's** meter ends the song: a performer fails
  (0x822DD108) only when its vtable +0x50 says it can — a player only when it
  is alone and the mode is not competitive (0x822C6B98), the band always
  (0x8234E1E0) — and never in a competitive mode (0x822DD3E0 ->
  0x822B37F8) [C]. This answers FINDINGS open question 2. The players' own
  meters still move; nothing shows them.
- **Star Power**: one meter `{value, shown}` (0x822DB478) for both
  players' Star Power objects (0x822C69E8) [C]. Engaging (`force_mercury`,
  0x822C80F8) stores the state (+0xbc) and sends `sp_engage (player
  state)` (multi_coop's handler plays `sp_deploystart.cue`); with the ready
  flag and Star Power off it deploys, but with a shared meter only when the
  other player is engaged too, and then for both [C]. So both must hold
  Select or tilt at once. Each player's poll (0x822C9260 -> 0x822DBD60)
  drains the shared meter by `deploy_rate` (0.0833) per measure, so with
  both deployed it empties twice as fast: a full meter lasts 6 measures, half
  a meter 3 [C]. Each player's phrases and whammy fill the one meter; each
  object sets its own ready flag in its poll (`star_power_ready`) [C].
- **Excitement**: the band's rating (0x822CAAB0 -> 0x822C9BE8: the band when
  there is one, else player 0; competitive: the better player) [C].
- **Stars**: the band's (0x822DCDF0 -> 0x822CE5D8) [C]: thresholds from
  `scoring.dta (star_ratings (coop (thresholds (kDifficultyMedium 2.0)
  (kDifficultyExpert 2.0)) (max_scores ...)))`: the k-th threshold is
  value × `max_scores[song][index]` with the entry's difficulty as the index
  (Medium 1, Expert 3), whatever difficulty was played; 5 stars at or above
  the second, 4 at or above the first, else 3; a song without `max_scores`
  gets 5. This answers FINDINGS open question 5 (as decoded: the thresholds
  do not depend on the difficulty played).
- **Audio**: `song_coop` (17 songs) when the song has it [C data]; else the
  song's block [I]. Each player's instrument channels (`tracks`) are muted
  while that player misses [I: GH1's UnmuteBeatMatchAudio per player].
- The co-op HUD shows the band: its score, streak, multiplier, the shared
  star meter and the band's rock meter [C data: `hud_coop`'s displays have
  no player number].

## Face-off and pro face-off

- Competitive: nobody fails (0x822DD3E0 skips the fail when
  `game multiplayer_vs`) [C]; the winner is the higher score, a tie shows
  `me_caption_tie` (`multiplayer.dta update_winner_info`) [C data].
- Face-off: players own the sections' gems (above); while the song plays the
  game walks the gem list by time and sends `active_players_changed
  (mask)` to its sinks whenever the next gem's mask changes (0x822CFA70,
  Game +0xb0 / +0xb4) [C]: the world's camera filter (facing left for
  player 1, right for player 2) and the characters.
- Pro face-off: both players play every gem at one difficulty (the single
  `multi_fo_seldiff_screen`); it is unlocked by winning a tour (Campaign
  +0x68, `docs/career_and_store.md`) [C].
- Both tracks scroll at the mean of the two difficulties' `track_speed`
  (0x826932D0) [C].
- With one part for both, the guitar channels follow the latest hit or miss
  of either player [I].

## The menus (GH2's `ui/gen/multiplayer.dta`)

main menu MULTIPLAYER -> `multi_controller_select_screen` (each pad moves to
P1 / P2 and confirms; `gamecfg set_player`) -> `multi_screen` (Co-op,
Face-off, Pro Face-off once unlocked) ->
- co-op: venue -> song -> `selpart_screen` (songs with bass or rhythm; the
  two players must pick different parts) -> both difficulties ->
  `multi_sel_character_screen` -> `multi_sel_guitar_screen` -> loading;
- face-off: characters -> guitars -> venue -> song -> both difficulties;
- pro face-off: the same with one difficulty for both
  (`sel_difficulty_panel` sets player 1 too when there are two players
  [C data]);
- results: `multi_compete_coop_screen` (the band's score, stars and streak;
  `campaign finish_coop_song`, then `multi_unlock_bass_screen` for each bass
  awarded), `multi_compete_screen` / `multi_compete_fo_screen` (the winner
  or a tie).

Native classes (`ui/multi_select.*`) [C]:
- **MultiSelectScreen** (0x822981A8 / 0x82296EF8 enter, handler
  0x822978C0): panels `<panel_base_name>0/1`, active when `player_num <
  num_players`; `button_down` to that player's panel, `component_select`
  then the ready check (0x82297750), `multi_select` (exclusive screens: to
  every panel), `component_focus` to every active panel; not ready while a
  panel is active / required without a selection (0x82294C98); all ready:
  `set_selection` per active panel, then `all_ready` (0x82297528).
- **MultiSelectPanel** (0x82295790, 0x82295130, 0x822958A0, 0x82295070):
  selection, Tri clears it, exclusive parts disable the other panel's
  button (not re-enabled on release: GH2's quirk, kept), the ready label.
- **MultiCharSelPanel** (0x8228B460, 0x8228A590, 0x8228A408, 0x8228A248):
  the unlocked `char_navigator`, scrolling that skips locked characters and
  the other player's, select / deselect, outfits.
- `song_provider get_instrument / has_instrument` read the mode's song
  block's `tracks` (0x822BF2C0, 0x822BF1F8, 0x822B38F8); `gamecfg set_track`
  sets the instrument and track type (0x822B6BD0).
- `finish_coop_song` (0x822861A8 -> 0x822A6848 / 0x822A66E0 /
  0x822A5FF0): the band's best score and stars per song (Campaign +0x4c,
  stars never go down), a gold flag when both players hit 100 % on Expert,
  and, on a new score, every co-op bass whose `(require (songs n) (stars
  s))` (guitars.dta) is met is awarded; quick play's list shows the co-op
  record in co-op (0x822C3428 / 0x822C34E8). Saved in `career.json`
  (`coop`, `coop_awards`). Details: `career_and_store.md`.

Port choices and inferences:
- `is_multiple_controllers` is GH2's "more than one pad" (0x822B4248); the
  port always enables MULTIPLAYER, since the keyboard's split layout lets
  one keyboard play both (H J K L ; frets, N / M strum, B Star Power, O
  Start; on from the controller select when fewer than two pads are
  connected, off at the main menu).
- In a two-player mode a pad's buttons reach the scripts as the player
  whose pad it is [I]; co-op basses are locked until awarded [I]; the
  entry flags 0x80 / 0x04 that keep songs out of the bass count are not
  modelled [I].

## Other open questions answered

- `star_power min_length` (2.0) is copied into the player (+0xd4,
  0x822C9720) and never read; `recharge_rate` has no term in the Star Power
  poll (0x822DBD60) [C as far as the export shows: no other reader]
  (FINDINGS open question 4).
- The timing class (0x822D6260) is computed by 0x822E2748 and its result
  dropped [C] (FINDINGS open question 7): no gameplay or stats use.

## gd02

- `game/band_performer.*`: the band; `normaliseScores`, `linkCoop` wire two
  players as 0x822C9E80 does. `game/player.*`: the normalisation, the
  shared meter and engage (`starPowerEngage`, Select / tilt held), the
  co-op band hooks, `PerformerState` for the HUD.
- `app.cpp`: the mode from the menus' GameSetup (`multi_coop`, `multi_vs`,
  `multi_fo`) or `GD02_MODE=coop|faceoff|proface` without the menus
  (`GD02_DIFF2`, `GD02_PARTS`); each pad plays its player (`gamecfg
  set_player`); two tracks and `hud_vs` / `hud_coop` (loaded again when the
  mode changes); `active_players_changed` in face-off; the band's fail ends a
  co-op song; the stage band with both players' looks (a co-op bass player's
  character strums the bass track).
- `--walktest coop|faceoff|proface`: both players through the menus
  (controller select, mode, venue, song, parts, difficulties, characters,
  guitars) to an autoplayed song and the results; co-op checks the
  co-op record and the bass award in `career.json`.
- `--selftest` "two-player rule checks": co-op shared Star Power (fill,
  engage alone, both engage, double drain), band streak / multiplier /
  score, the band's fail after exactly the expected passes, the easier
  player's mix, normalisation, face-off masks and ownership, pro face-off
  playing every gem, competitive no-fail, co-op stars.

## Open

- Face-off HOPOs: GH2 marks HOPOs per player's gem list (0x8233FE50 ->
  0x8234E0F0), so a gem after a section change may HOPO from that player's
  previous gem; gd02 marks them on the whole chart [I].
- Whether the other player's face-off gems are drawn on a track (gd02:
  not) [I].
- The per-player guitar mute in face-off / pro face-off [I].
