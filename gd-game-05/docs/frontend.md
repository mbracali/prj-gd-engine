# gd-game-05 front end: screen by screen (wave 03)

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

## The flow

| # | Screen | Status | What / how checked |
|---|---|---|---|
| 1 | intro_movie_screen | matches | rb_cinematic.bik (videos.dtb intro, fill_width), now at its own 16:9 (was squeezed 4:3); Start / A skip it as main.dta says (the port also takes Enter, Esc, a click, universal). Shot. |
| 2 | intro_to_splash_screen | matches | no panels, TRANSITION_COMPLETE_MSG to the title. Walk rb_boot. |
| 3 | splash_screen | matches | ROCK BAND / PRESS START over movie_panel's background video (rb_video, rb_video1, rb_video2, MoviePanel's pick rule 0x828557E8), the shell music and shell_crowd.cue start (meta music_start). Walk rb_boot (`menu_movie_frames`), shot. |
| 4 | attract_screen | differs | after the attract timeout (syscfg attract time_to_enter) the attract videos play, picked by the same rule (was always the first). Not run this wave. |
| 5 | main_screen | matches | the five metal words (button_main.milo's word meshes) lit by buttons.grp's main_buttons.env (they were lit by logo.env: dark first letters), the focused pill golden, background video, pat_rock_3 lower third. Shot. |
| 6 | main_solomode_screen, main_multimode_screen, training_menu_screen, options_screen | matches | the side pills; text colours from color_presets.dta (D17). Shots. Options' port row ENHANCED OPTIONS has no pill [I]. |
| 7 | qp_selsong_screen | differs | the list is drawn by its TexRenderer into its 480x552 texture (rows at the labels' own text sizes, headings in rows of their own, the highlight bar); the texture shows flat under the title bar, not on the swaying cloth (GD05-022, D17). Help bar: Back / Change Sorting / Choose Song with fret icons. Walk checks `song_list_layout`, `rb_helpbar`; shots. |
| 8 | qp_seldiff_solo_screen | matches | seldiff_solo.milo drawn as RB draws it: the koi banner (CHOOSE DIFFICULTY, the song and artist), the branches, the player card from its TexRenderer; GUITAR / BASS first, then the difficulties, READY; the slots no player has hidden. Walks quickplay / keys, shots. |
| 9 | loading_screen > preloading > real_loading > loading_tip_screen(_2, _3) | matches | the tip picked as RB's RandomContext (weights, conditions; it showed a raw tips.dta entry before). Shot. |
| 10 | game_screen | out of scope | the HUD is still the port's stand-in (wave 02). Red before the first note no longer leaves the screen (#owner bug 2). |
| 11 | pause_screen | not checked | walk keys enters and resumes it; no shot this wave. |
| 12 | endgame_waiting_screen > qp_endgame_screen | differs | song, score, the player's panel (tour_endgame_player.milo: PLAYER 1, the instrument, difficulty, % hit, note streak), Continue. The stars label is empty [I: the stars font's glyphs]. Shot. |
| 13 | qp_complete_screen | matches | NEW SONG / REPLAY / EXIT, Select. Shot. |
| 14 | lose_screen | differs | SONG FAILED, the song, RETRY ... QUIT; "% complete" reads 0 after the song closed. Walks rb_lose, rb_lose_quit, shot. |
| 15 | lose_confirm_quit_screen (dialog_panel) | differs | the message and YES / NO draw small [I: the dialog labels' text size]. Shot. |
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

## Left for the next wave

- The song list's cloth (mesh, camera move, sway) instead of the flat texture.
- The pause screen's look; the lose screen's % complete; the dialog's text size.
- The stars on the results.
- The universal help bar's mouse access buttons on RB's help bar.
- The attract mode, the credits, the options' sub-screens, the tours'
  screens and the band lobby (D10) screen by screen.
