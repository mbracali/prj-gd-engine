# Guitar Hero: Metallica (Xbox 360): front end

gh4 replaced GH3's flow manager (`*_fs` states) with a UI state machine and
added description-driven screens ("descs"). [C] = confirmed on the disc
data or at an XEX address; [I] = inferred.

## States (ui/ui_states.cpp)

- `UI_InitializeStateMachine` creates the composite object `ui`
  (eventcache, statemachinemanager, menustack) and `RegisterUIStates`
  registers every state: `RegisterState {name = <uistate_x> Task = {name =
  task_menu params = {base_name = 'x' anim_in_script pakname do_not_hide
  ...}}}` [C scripts].
- Scripts post events with `ui_event` -> `ui : eventcache_add event_id =
  menu_change | menu_replace | menu_back | menu_add | menu_refresh
  event_data = {state = ...}`; `ui_event_block` waits in `Block {type =
  taskmenu_callback}` until the event is handled [C scripts].
- A task_menu runs the scripts named after its base name: `ui_init_`,
  `ui_create_`, `ui_destroy_`, `ui_deinit_`, `ui_return_` [C: the prefixes
  at 0x82025698 .. 0x820256E4, used by 0x823090D8 .. 0x8230A7C0; the script
  call 0x82309028 merges the task params and the event's data]. The order
  of the calls is D4 [I].
- Boot path: boot_legal (6 s) -> boot_movie_atvi -> red_octane ->
  neversoft -> intro (each skippable with A) -> boot_iis (press start) ->
  signin -> memcard -> boot_download_scan -> optimal_drum (pop-up, first
  boot) -> boot_guitar -> mainmenu_intro -> mainmenu [C: walked].
- Quickplay: mainmenu (QUICKPLAY) -> game_mode -> select_instrument ->
  setlist (+ setlist_prompt, double_bass_popup the first time) -> pick a
  song (it joins the custom set list), Start -> select_difficulty ->
  play_song -> gameplay [C: walked].

## Descs (ui/ui_desc.cpp)

`CreateScreenElement {type = descinterface desc = '<name>'}` builds the
global `uidesc_<name>` (QB file `ui\uidesc_<name>.qb` in a zone or UI pak):
`{descversion name rect aliases props materials formatversion elements}`,
`elements = {props = {...} children = [...]}`; each props struct is a
screen element's (type, local_id, pos, pos_anchor, just, dims, texture,
text, font, z_priority, scale, rgba, hiddenlocal, ...) [C data].
`aliases` name a descendant by a path of local ids (`desc_resolvealias`);
`props` name a descendant's property (`target`), set with `se_setprops` and
read with `se_getprops` on the desc element [C data; rules I].

Element changes from GH3: `menuelement` (a vmenu, or an hmenu with
isvertical false), `pos_anchor` per element, sprite dims are the size (D6),
`se_setprops ... time = s` morphs, `se_waitprops`, `AssignAlias`,
`obj_spawnscript` on elements, A / B / X / Y raise every event xenon_pad
maps them to (pad_choose and pad_btn_bottom, ...).

## Paks the front end loads

`SetPakManCurrent map = zones pak = z_soundcheck` also loads the zones in
`GHZones.<zone>.zones` (z_frontend: the main menu's desc) [C data];
`map = ui_paks pakname = x` loads `DATA/COMPRESSED/PAK/x.pak.xen`. Their QB
files join the globals.

## A song's lifecycle: start, pause, quit, restart, end (GD10-060, D90)

What the original's scripts do on each step, and what the port runs [C
scripts unless marked]:

- **Set list -> difficulty -> play_song**: `menu_soundevent_in` (play_song,
  not training / tutorial / practice) spawns `Loading_Screen_Crowd_Swell`:
  Crowd_Beds +12 dB, then swells on Crowd_One_Shots every 0.5..1 s in a
  `begin ... repeat` that never ends by itself (and `Crowd_Loading_Whistle`).
- **start_gem_scroller**: `crowd_in_Front_End_state = 0`, ...,
  `GuitarEvent_EnterVenue`, per player `crowd_reset` (meters at 1;
  `change_crowd_looping_sfx` neutral, good for a boss / intro_zakk / ted /
  Metallifacts) -> `do_actual_changing_of_looping_sound` ->
  `Skate8_SFX_Backgrounds_New_Area` (the venue's bed, `<area>_FT` / `_BK`
  loops on Crowd_Beds, a cross-fade from the menus' bed) and
  `One_Shot_SoundEvent` (`SpawnedOneShotBeginRepeatLoop`: a one-shot, then
  0.9..1.6 x 5 s after it ends, while `crowd_in_Front_End_state` = 0). The
  port runs `crowd_reset` from SongHooks::begin.
- **The transition** (`Common_<t>_Transition`'s ScriptTable): FastIntro
  (Quickplay, career restarts) `GH_SFX_Intro_WarmUp_Fast`, Intro
  `GH_SFX_Intro_WarmUp`, MetallicaVOIntro `GH_MetallicaVO_Intro_WarmUp`, at
  100 ms: the venue's applause, `KillSpawnedScript` the swell and the
  whistle, Crowd_Beds back to its default. The port spawns that entry as the
  song starts (it ran Intro's only: Quickplay's swell screamed through every
  song and on in the menus, 4.3 crowd sounds / s).
- **In the song**: the bed, the one-shots, the meter's crossings
  (`crowd_transition_sfx_*`, `change_crowd_looping_sfx`), the chart's crowd
  notes, Star Power / streak cheers. Walks measure 0.26 crowd sounds / s.
- **Pause**: `PauseSoundsByBuss` on the song's busses; resume unpauses.
- **Quit** (pause -> Quit -> the warning's Quit): `quit_warning_select_quit`
  -> `GH3_SFX_fail_song_stop_sounds` (StopSoundsByBuss crowd, UI_Star_Power,
  ...; `BG_Crowd_Front_End_Silence` = the frontend area's silent bed) ->
  uistate_pausemenu_song_ended (Retry Song, New Song, Quit); leaving it,
  `ui_deinit_gameplay` spawns `kill_gem_scroller`: `crowd_in_Front_End_state
  = 1`, `SongUnLoad` (gh4; GH3 had SongUnLoadFSBIfDownloaded), the song's
  scripts killed, `GuitarEvent_KillSong`. The port's `SongUnLoad` ends the
  song (unhandled, the song played on under the menus, `UnpauseGame`
  resumed it, and its end's `GuitarEvent_SongWon_Spawned` pulled the menus
  back to gameplay). The set list then starts the menus' own bed:
  FrontEnd_Menu_Music = EXT_MD_Crowd_Good_Loop at 10% [C data
  Skate8_SFX_Background_Areas_And_States] -- that quiet crowd under the
  menu music is the original's.
- **Restart** (pause -> Restart): `restart_warning_select_restart` ->
  `GH3_SFX_fail_song_stop_sounds`, `career_restart_song` ->
  `kill_gem_scroller type = restart` + `restart_gem_scroller`.
- **The end** (won): `GuitarEvent_SongWon_Spawned` (the good bed), results,
  `ui_create_cash_rewards_3` / the set list -> FrontEnd_Menu_Music.

The port's voices: a voice stopped while paused (the pause menu paused it,
then a stop by sample / buss / event) dropped its pause too (it stayed
"playing" for ever: `IsSoundPlaying` true, never freed).

Checks: walks `qp_quit` (three quits in a row), `qp_restart`, `qp_finish`:
`check:crowd_rate<=1.0` in each song (crowd voices under User_Crowd per
second, and no `Loading_Screen_Crowd_Swell` running), `check:song_quiet`
after each (no song, no crowd voice but the menus' bed, no paused voice,
no swell / whistle / song script). `GD10_SFXLOG=1` logs every sound
start / stop, `GD10_SPAWNLOG=all` every spawn and KillSpawnedScript.
