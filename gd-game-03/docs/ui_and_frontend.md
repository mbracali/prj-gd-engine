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

## The picture: 4:3 or 16:9 (GD03-016)
- The PS2 drew a 4:3 picture, so that is the default: the menus, the
  highway, the HUD, the venues and the movies are drawn in the middle 4:3 of
  the 16:9 frame, between black bars. Enhanced Options > Performance and
  Graphics > Widescreen (16:9), a check box after Internal Resolution, gives
  the whole 16:9 frame instead. Nothing is stretched: every camera keeps its
  vertical field of view and gets the picture's aspect, so 16:9 shows more of
  the scene at the sides (the menus' posters stay where they are).
- How: `gfx::Renderer` keeps the 16:9 frame (`frameRect`, the 1280 x 720
  logical screen letterboxed in the window) and the picture in it
  (`pictureRect`: the centred 4:3 part, or the frame); every 3D view takes
  its viewport and aspect from the picture, and `gfx::pictureAspect()`
  (render/camera.h) is the default aspect of every camera made, the menus'
  too. The 2D canvas stays in frame units: its projection spans only the
  picture's part of them (160..1120 in 4:3), so what lies outside is cut off
  as the bars. The menus' screen masks, the HUD's messages and the mouse's
  hit boxes (`UiView::addHit`: 640 + x * 360 * aspect) are placed in the
  picture's part of the frame, so the mouse and touch (window -> frame units,
  unchanged) hit the components in both modes (`--walktest mouse` checks
  OPTIONS' hit box in 4:3 and 16:9).
- The port's own screens (Enhanced Options, GAMEPADS) are laid out on the
  whole 16:9 frame (gd-game-01's coordinates) in either mode
  (`PortDraw::FullFrame`); the Enhanced Options' venue backdrop is the
  game's picture, so the aspect change shows on it at once. The help bars
  below them are the game's panels, in the picture.
- Screenshots (GD03_SCREENSHOT, the walk's shots) are the whole frame, bars
  included. The development viewers without the menus (--view) stay 16:9;
  `--song` reads the Enhanced Options as the menus do.

## Enhanced Options defaults (GD03-019) and layout (M8)
- Every option starts at the original game's look: windowed, In-Game Max
  FPS 60, no FPS counter, Internal Resolution "Window", Widescreen off, the
  greyed (not yet built) effects off, the cheats off. Enable Mouse Selection
  starts on: it is a control, and every gd-game works with the mouse
  (guidelines/universal_launcher.md). `--walktest enhanced` checks them, the
  4:3 picture with its bars, and 16:9 after Widescreen is switched on.
- The page follows gd-game-01's Enhanced Settings (ui/enhanced_panel.cpp):
  the options box on the left, the still close-up of the singer with the
  band on the right, the explanation box bottom right; dropdowns for the
  multiple choices (every greyed one lists gd-game-01's values), check boxes
  for on / off and for the two-value choices (Forgiving Hammer-ons and
  Pull-offs), Upscaling Quality and Dust in Light Beams shown only with the
  row they depend on, the "on" tick green. GAMEPADS opens from the main
  menu's yellow fret (after the cheat codes' 750 ms), the help bar's
  GAMEPADS and the Gamepads row.
