# gd-game-05 front end: screen by screen (waves 03 and 04)

The front end and user flow of Rock Band (boot, menus, the way into and out
of a song) against the disc's own data and the executable. Each screen is
**matches**, **differs** (what) or **missing**, with how it was checked.
Facts from the disc are [C data], from the XEX [C 0x...], the port's choices
[I] (DECISIONS.md D15..D18). No emulator was used.

## How the screens were listed

`python3 -I tools/frontend.py extracted/dta` prints every screen the scripts
create (345 in ui/, tour/, tutorial/, drum_trainer/): its panels, focus,
`(back ...)`, `(helpbar ...)` and the screens it goes to (goto_screen,
push_screen, sync_screen, confirm_goto, through the #define macros);
`--md` prints the table. The flow below is the one the owner plays:
boot, Solo > Quickplay, a song, the results, back. "Walk" means a
`--walktest` scenario that passes with this build; "shot" means a screenshot
(GD05_SCREENSHOT / GD05_WALK_SHOTS) looked at.

## The owner's bugs (wave 03)

- **People screaming at the start (GD05-023).** The port's Enhanced Options
  backdrop (a venue posed at boot, before the intro movie) sent its crowd an
  excitement change, which played world/'s crowd_upto_good cheer. The
  backdrop's world is now silent (D16). RB's own menu ambience stays as its
  scripts play it: meta.dta `music_start` plays the shell music (a random
  sfx/streams/shellmusic_* at -12 dB) and shell_crowd.cue; the song list
  and difficulty screens play shell_crowd_muffled.cue (postsong_sfx_panel).
  Check: walk `rb_boot` / `rb_owner_bugs` `counter_max:menu_venue_sounds:0`;
  GD05_SFXLOG=1 logs every sound object started (`synth: ...`) and the crowd
  streams (`crowd audio: ...`).
- **Key 2 loads another song (GD05-024).** Key 2 is the red fret (the
  universal keyboard map), which the menus take as kPad_Tri (back). The port
  kept GH2's GHScreen rule "back to the screen we came from", so on
  game_screen (before the first note, while the frets still go to the menus)
  red went back to loading_tip_screen_3, which loaded the song again. RB's
  UIScreen (0x8225AEE8) goes back only to a screen's own `(back ...)` (D15).
  Check: walk `rb_owner_bugs` presses keyboard 2 eight times during the
  song's start: `counter_max:song_loads:1`.
- Keys audited for the menus: Enter (Start, or select on a list), Esc /
  Backspace (back), arrows (move), 1..5 the frets (green select, red back,
  yellow / blue / orange as the screens' own handlers say: sorting on the
  song list, GAMEPADS on the main menu as gd-game-02), Tab (Select). The
  development keys (GD05_KEYS, the cheats' keyboard keys) need an
  environment variable or the Enhanced Options' developer switch (off by
  default); none acts in a launcher run.

## Wave 04: the owner's points (2026-11-10)

- **The cloth (GD05-022).** The song list, the difficulty / instrument
  card and the shop, tour and creator rooms are RenderDirs whose cloth hangs
  from bones a CharHair simulates; a CharTransCopy (RB 0x8233F6F8) copies
  controller.trans (rocked by controller.tnm on loop.trg, frames 150..250 at
  15 fps, looping) onto the cloth's root, and the cloth mesh is skinned to
  the bones. The port now polls the RenderDir's pollables every frame and
  skins the cloth (render/ui_cloth.cpp, D20); the song list is drawn through
  cloth.cam like the others (banner_lower3rd.tnm plays 40 -> 0). Check: walk
  `rb_owner_bugs` `song_list_layout` (on the cloth, polled, rows inside it);
  shots at several times (the cloth sways). The RenderDir's own transform
  (turned 9.5 degrees, set back) carries the cloth: with it the first row
  sits below the title bar, the tour hub's menu cloth is on screen (D20).
- **Loading screen band icons (GD05-026).** loading.milo's four icons
  (load_icon_guitar / vox / drum / bass, from their proxy files whose root is
  "loading") all stood on one spot and did not move: a proxy object
  parented to its file's root now hangs from its holder (guitar, vox, drum
  under load_icon_bass), and a RndDir used as an anim sets its MatAnims (the
  AnimFilters' texture swaps) (D22). Check: walk `rb_owner_bugs`
  `loading_icons` (four distinct x), shots (the drummer and singer change
  frames; the guitarist's filter plays 0..0, so it holds, as the data says).
- **Menu feel (GD05-027).** UITriggers play as RndAnimatable::Animate
  (0x8221BE38): start -> end at the rate's fps x scale (or the period), with
  their delays (the pause buttons' 0.09 s cascade) and loops (the lights,
  the menus' sunrise and trim, the loading animations); script `animate`
  takes (loop) and (delay) (0x8221C040). A main menu button's selection
  plays its type's selecting_anim over select_frames (30) (0x822D4848)
  (D21). Shots of the main menu during a selection.
- **Strange behaviour (GD05-028).** Every screen change took a fixed 0.5 s
  out + 0.5 s in (GH2's metacam), with input dropped and nothing moving: RB
  waits only while a panel's block_transition trigger is running
  (UITrigger 0x82262918, PanelDir 0x8225FEE0 / 0x8225FF60), so most RB
  screens change at once and the dialogs wait for their exit.trg (D21). The
  walks' boot press (meant for the skipped intro) now waited for the title.

## The flow

| # | Screen | Status | What / how checked |
|---|---|---|---|
| 1 | intro_movie_screen | matches | rb_cinematic.bik (videos.dtb intro, fill_width), now at its own 16:9 (was squeezed 4:3); Start / A skip it as main.dta says (the port also takes Enter, Esc, a click, universal). Shot. |
| 2 | intro_to_splash_screen | matches | no panels, TRANSITION_COMPLETE_MSG to the title. Walk rb_boot. |
| 3 | splash_screen | matches | ROCK BAND / PRESS START over movie_panel's background video (rb_video, rb_video1, rb_video2, MoviePanel's pick rule 0x828557E8), the shell music and shell_crowd.cue start (meta music_start). Walk rb_boot (`menu_movie_frames`), shot. |
| 4 | attract_screen | matches | 30 s on the title (config/attract.dta time_to_enter) goes to the attract videos, picked by MoviePanel's rule (was always the first), and back to the title; the menu music and crowd stop and start again. Log of a 75 s idle run. |
| 5 | main_screen | matches | the five metal words (button_main.milo's word meshes) lit by buttons.grp's main_buttons.env (they were lit by logo.env: dark first letters), the focused pill golden, background video, pat_rock_3 lower third with its skull wave (waves_skulls_2.milo, textured from its own subdirectory); focus moves play button_toggle, select button_select, back button_back (ui.dta). The port adds GAMEPADS to RB's help bar here (D19). Shots, GD05_SFXLOG. |
| 6 | main_solomode_screen, main_multimode_screen, main_bandcoop_screen, training_menu_screen, options_screen | matches | the side pills; text colours from color_presets.dta (D17); back plays the backward transition and button_back. Shots. Options' port row ENHANCED OPTIONS has no pill [I]. |
| 6b | options_audio_screen, options_gameplay_screen, options_savedata_screen | differs | the panels, PLAYER pill, help bar and DOLBY(R) DIGITAL (markup) show; the audio sliders show their bar but not their pod, two not at all. Shots. |
| 7 | qp_selsong_screen | matches | the list is drawn by its TexRenderer into its 480x552 texture, shown on the swaying cloth through cloth.cam, the cloth turned and set back by the RenderDir's own transform (wave 04, D20); seven rows as ss_song.lst's display count. Help bar: Back / Change Sorting / Choose Song with fret icons. Walk checks `song_list_layout` (on the cloth, polled), `rb_helpbar`; shots. |
| 8 | qp_seldiff_solo_screen | matches | seldiff_solo.milo drawn as RB draws it: the koi banner (CHOOSE DIFFICULTY, the song and artist), the branches, the player card from its TexRenderer, hanging as a cloth that sways (wave 04); GUITAR / BASS first, then the difficulties, READY; the slots no player has hidden. Walks quickplay / keys, shots. |
| 9 | loading_screen > preloading > real_loading > loading_tip_screen(_2, _3) | matches | the tip picked as RB's RandomContext (weights, conditions; it showed a raw tips.dta entry before); the band's four icons side by side, animated (wave 04, D22). Walk `loading_icons`, shots. |
| 10 | game_screen | out of scope | the HUD is still the port's stand-in (wave 02). Red before the first note no longer leaves the screen (#owner bug 2). |
| 11 | pause_screen | matches | PLAYER 1, PAUSED, RESUME / RESTART / LEFTY MODE / NEW SONG / QUIT, help bar Return to Game / Select. Walk keys (pause, resume); wave 04: walk `rb_pause_quit` (QUIT, confirm_quit_screen with no.btn focused, yes, main menu, the song loaded once), shots. |
| 12 | endgame_waiting_screen > qp_endgame_screen | differs | song, score, the player's panel (tour_endgame_player.milo: PLAYER 1, the instrument, difficulty, % hit, note streak), Continue. The stars label is empty [I: the stars font's glyphs]. Shot. |
| 13 | qp_complete_screen | matches | NEW SONG / REPLAY / EXIT, Select. Shot. |
| 14 | lose_screen | matches | SONG FAILED, the song, "4% complete" (kept with the result after the song closed; it read 0), RETRY / CHANGE DIFFICULTY / NEW SONG / PRACTICE / QUIT. Walks rb_lose, rb_lose_quit, shot. |
| 15 | lose_confirm_quit_screen (dialog_panel) | matches | the message wraps in its box (kFitJust in a multi-line box [I]); popup.trg plays all its anims now (it played only the first). Walk rb_lose_quit, shot. |
| 16 | meta_loading_continue > meta_loading > autosave_wait > qp_selsong_screen | matches | back to the song list. Walk quickplay. |

## What changed in the engine (shared by every screen)

- RenderDir panels (qp_selsong, seldiff, the shop, the tours, the
  creator): their TexRenderers' panels into textures, their own scene
  through their own camera (D17).
- UI inline proxied directories use their proxy files (the slots, the
  results' player panels, the title bars); they draw under the object that
  holds them.
- Player slots (MultiFocusScreen) work on their own slot directory.
- Text colours: color_presets.dta by text style (BandTextComp).
- Labels: RB's own text fields (size, alignment, fit); kFitStretched /
  kFitJust never wrap; a fit box side of 0 does not constrain.
- `{rnd aspect}` is widescreen; macro arrays are shared; random_context is
  RB's (D18).

## Script component names (`--uitest`)

42 names the scripts use are not in the panel's own milo (unchanged count).
They are names another panel of the same screen owns (solo.btn and
multi.btn in the main menu's side panels, from main_panel), the head-to-head
and tour endgame labels the quickplay panels share through the
ENDGAME_PANEL_HANDLERS macro (rank0.lbl ...), and PS3 / Xbox LIVE community
buttons. At run time the port resolves a name through the screen's panels
(UiManager::resolve), as the walks show; none is a lookup the port misses.

## Counts

Wave 04: of the 17 rows above (the 16 of the flow and 6b): 14 match, 2
differ (the results' stars, the audio sliders' pods), 1 is out of scope
(game_screen's HUD), none is missing. (Wave 03: 13 / 3 / 1.)

## Left for the next wave

- The results' stars (RB's star_rating string: the stars font has `*` and
  letters, not `_`; the native was not read), the audio sliders' pods.
- The confirm dialogs' YES / NO are dim: their text is lit by splash.grp's
  environ [I: whether RB lights the text the same way].
- The credits (behind Community > Special Features), the band lobby (D10)
  screen by screen; the creator's 3D preview ("Loading" stays behind it).
