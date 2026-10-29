# gd03: menus and front end (this disc's differences from GH2 360)

The UI system is gd-game-02's (the game's own scripts through our DataArray
interpreter, gd-game-02/docs/ui_and_frontend.md). What this disc changes:

## Boot and memory card
- `$first_screen` is `bootup_load` (ui/init.dta). The flow (ui/mem_card.dta,
  splash.dta): bootup_load -> (no save) bootup_create_save_check -> yes ->
  bootup_create_save -> cut_scene_screen (plays `intro.pss`, then
  splash_screen if `campaign num_profiles` > 0, else guitar_help_screen) ->
  splash_screen -> any button -> main_screen. The port always has The
  Glitches, so the guitar help is skipped [C data].
- `memcard` (handler 0x0014c078 [C]): get_info, load_data, save_data,
  format, check_save_game, space_needed (0x3ac00 bytes), space_available,
  localize (`<token>_ps2`, {SLOT} = "1", {REQ_SPACE} = "235KB",
  0x0014c658 / 0x0014bff0 / 0x0014c588). Backed by career.json
  (ui/natives_meta.cpp MemcardObj); results come as memcard_result
  ($result $space_free) on the next frame.
- After a song: meta_loading_screen -> `autosave` -> the next screen
  (GH2 360: saving_screen).

## Menus
- Main menu: CAREER, QUICK PLAY (focus), MULTIPLAYER, TRAINING, OPTIONS;
  no leaderboards. `game is_missing_multi_controller` is never true
  (DECISIONS D4).
- Options: audio settings has a stereo / mono check (`options get_stereo` /
  `set_stereo`, saved in career.json; mono output not applied yet).
- Pause: RESUME, RESTART, AUDIO, VIDEO, QUIT; restart and quit act at once
  (no confirm screens).
- Multiplayer: no controller select (player n = pad n; the Session default),
  modes co-op, face-off (versus.btn), pro face-off (faceoff.btn, unlocked by
  the campaign's `unlock_game_modes multi_fo`).
- Song lists: `song_provider set_quickplay TRUE/FALSE` and `get_quickplay`
  (GH2 360: set_mode / get_mode); the list data is read again whenever a
  list takes the provider (UIList set_provider) [I].
- Characters (config/gh2.dta): punk1, alterna1, glam1, goth2, metal1, grim
  (one outfit each). Venues: battle small1 small2 theatre fest arena.
- Campaign (config/campaign.dta): six tiers of five songs (the fifth the
  encore); Easy plays one tier less (0x00130c58); required songs per
  difficulty (0x001313d8); star awards 100 / 150 / 200 (Medium..Expert).

## Cheats
- config/long_cheats.dta names PS2 guitar buttons: G_GREEN kPad_R2, G_RED
  kPad_Circle, G_YELLOW kPad_Tri, G_BLUE kPad_X, G_ORANGE kPad_Square. The
  port feeds the cheat matcher with those codes for the frets (whatever menu
  button a fret also is) (ui/ui_manager.cpp action()).
