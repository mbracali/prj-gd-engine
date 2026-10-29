# GH3 (Xbox 360) front-end flow: boot to song to results (the walk tests follow it)

Traced from the disc's own scripts (qb.pak.xen) with `tools/qbscript.py dis` and
`tools/qb.py dump`, 2026-10-29. Tags: [C] read directly from the script bytecode
or QB data (or cfuncs.tsv for the addresses); [I] inference about what a native
does (from how the scripts use its results; not checked in the XEX).
Addresses are the C function addresses in `ghidra/analysis/cfuncs.tsv` (column 2).
No decompiler output is reproduced here.

## 0. Mechanics everything below relies on

- **Flow states** are QB structs `{Create, destroy, [create_params], [popup],
  actions = [{action, flow_state | flow_state_func | use_last_flow_state,
  [func, func_params, spawned_func, transition_screen, transition_left/right]}]}` [C].
- `start_flow_manager {flow_state, player=1}`: SetArrayElement on the global
  array `ui_flow_manager_state[player-1]` (`globalarray`, `resolveglobals = 0`:
  stores the *name*, not the struct), then calls `state.Create player=.. <create_params>` [C].
- `ui_flow_manager_respond_to_action {action, device_num, play_sound, create_params, ...}` [C]:
  1. returns at once if `$transitions_locked = 1` or `$menu_flow_locked = 1`;
  2. finds the action in `current.actions`; plays `Generic_menu_back_SFX`
     (go_back) or `ui_sfx_select` via `SoundEvent` (if `menu_flow_play_sound`
     and `play_sound`);
  3. new state = `flow_state`, or previous state (`use_last_flow_state`), or the
     `flow_state` *returned* by `flow_state_func` (called with `transitions_locked = 1`;
     ScriptAssert if it returns none);
  4. if `$playing_song = 0` and the action has `transition_screen`: calls its Create
     (e.g. `default_loading_screen` = create_loading_screen / destroy_loading_screen);
  5. popup states keep the old state "under" (`ui_flow_manager_under`), else calls
     `current.destroy` (and the under-state's destroy); LaunchEvent unfocus/focus root_window;
  6. SetArrayElement previous/current state;
  7. calls the action's `func device_num=..` **synchronously** (spawned if `spawned_func`);
     for Quickplay this is where the whole song load happens (quickplay_start_song);
  8. transition_screen destroy; then `new_state.Create player=.. <create_params>`.
  -> the VM must support: calling a script whose name is in a local
  (`<arg> create_func ...`), `return {flow_state = ...}` values flowing back to
  the caller, nested respond_to_action calls from inside a Create
  (create_download_scan_menu does this), and Waits inside calls nested several deep [C].
- **Menu input**: menus are ScreenElement trees (`vscrollingmenu` + `vmenu` +
  `TextElement` children, made by script `new_menu`) with `event_handlers`
  `[{focus X}, {unfocus X}, {pad_choose X params={..}}, {pad_back X}, {pad_up/pad_down X}, ...]`.
  The engine must turn controller input into pad_* events for the focused
  element (respecting `exclusive_device`), move focus on pad_up/pad_down in a
  vmenu (with wrap unless `dont_allow_wrap`), and deliver `pad_start` to
  `root_window` (handler set by restore_start_key_binding = gh3_start_pressed;
  kill_start_key_binding sets it to null_script, via
  `SetScreenElementProps {id=root_window event_handlers=[..] replace_handlers}`) [C].
  `LaunchEvent {Type=focus|unfocus|pad_down target=id}` must run synchronously:
  create_setlist_menu and create_select_difficulty_menu move the cursor with
  `LaunchEvent pad_down` in a loop and read globals the focus handlers set [C].
- **Game events**: guitar_startup spawns `create_guitar_events` per player, which
  registers `SetEventHandler response=call_script event=<event><player_text>
  scr=event_spawner params={event_spawned=i}` for every entry of `$guitar_events`
  (missed_note, hit_note(s), star_power_on/off, song_failed, song_won, ...;
  the event checksum is `ExtendCRC <event> 'p1'`) and then `Block`s forever [C].
  `BroadcastEvent Type=..` (scripts) and the C gameplay code raise them [I].

## 1. Boot (already running in the engine)

`guitar_startup` (scripts\guitar\guitar.qb) [C]: DisplayLoadingScreen 'boot_legal';
GetTrueStartTime -> `legal_start_time`; heaps, COIM, anim, lights, paks
(global.pak, perm_anims, fonts, global_sfx), LoadFSB streamall, CreatePakManMap,
SetPakManCurrentBlock z_soundcheck, setup_globaltags (creates every tag with
defaults), setup_sprites, kill_start_key_binding, spawns create_guitar_events
x max_num_players, then (autolaunch_startnow = 0, skip_boot_menu = 0)
`start_flow_manager {flow_state = $bootup_sequence_fs}`; create_guitarist x2;
then **busy-waits** `begin GetTrueElapsedTime starttime=<legal_start_time>;
if elapsedtime >= 6000 { change legal_timer = 1; break } repeat` (no Wait in the
loop: the VM must yield on its own or GetTrueElapsedTime must advance), then
hideloadingscreen [C].
- `NotCD` (822873c8) must return **false** (retail): otherwise
  enable_button_cheats stays 1 (LaunchViewer, debug menu), movies are skipped
  (`show_movies = 0` in guitar.qb) and attract mode starts after 5 s [C].
  `CD` (822873b8) true (skips StartKeyboardHandler) [C].

## 2. bootup_sequence_fs  (bootup_menu_flow.qb)

- Create `start_bootup_sequence`: (NotCD false) -> spawn `bootup_sequence` [C].
- `bootup_sequence`: `wait_for_legal_timer` (loop: `$legal_timer = 1` set by
  guitar_startup at 6 s; Wait 1 gameframe) -> `StartRendering` (script ->
  StartRendering_C 82266618, change pause_no_render = 0) -> PlayMovieAndWait
  'atvi', 'ro_logo', 'ns_logo', 'intro' -> spawn respond_to_action
  `skip_bootup_sequence` (play_sound 0) -> **bootup_press_any_button_fs** [C].
- `PlayMovieAndWait {movie, [noblack], [noletterbox]}` (guitar_movies.qb) [C]:
  mark_unsafe_for_shutdown; `fadetoblack {ON time=0 alpha=1 z_priority=-10}`
  (screen elements); GetDisplaySettings (82278820) -> `widescreen` (return true
  [I]); `SetScreen {hardware_letterbox = 0|1}` (82275f08, stub);
  `PlayMovie {textureslot=0 texturepri=1000 no_looping no_hold movie=<name>}`
  (8227bff0); Wait 2 gameframes; loop: `if not IsMoviePlaying {textureslot=0}`
  (822766f8) break; `GetButtonsPressed {startanda}` (820e7be8) -> `makes`; a
  press after a frame with nothing held -> `KillMovie {textureslot=0}`
  (8227c0c8) and break; Wait 1 gameframe. Then fadetoblack OFF, SetScreen
  letterbox 0, mark_safe_for_shutdown (-> UnPauseSpawnedScript) [C].
  Stub path: IsMoviePlaying false makes every movie a no-op [I].
  Movies are `movies\bik\<name>.bik.xen` (the engine's `--playmovie` decodes them) [I].

## 3. bootup_press_any_button_fs  (menu_press_any_button.qb)

- Create `create_press_any_button_menu` [C]: ReAcquireControllers (82283d20, stub);
  primary_controller_assigned = 0; main_menu_movie_first_time = 0;
  SoundEvent Menu_Guitar_Lick_SFX; spawn Menu_Music_On {waitforguitarlick}
  (SetSpawnInstanceLimits max=1, Wait 3 s, EnableUserMusic, loop SoundEvent
  Menu_Music_SE / Menu_music_Checking = loop while IsSoundEventPlaying, 1 s);
  create_menu_backdrop {texture = bootup_copyright_bg} (container + 1280x720
  SpriteElement); container pab_container; TextBlockElement "PRESS ANY BUTTON
  TO ROCK..." font text_a6; spawn `check_for_any_input`; spawn
  `attract_mode_spawner` (110 s with NotCD false, then action
  `enter_attract_mode` -> bootup_attract_mode_fs) [C].
- `check_for_any_input {mode={}}` [C]: loop `GetButtonsPressed` until `makes = 0`
  (all released), then loop `GetButtonsPressed <mode>`: if `$invite_controller != -1`
  (it is -1) ...; if `makes != 0` -> spawn respond_to_action `continue`
  `flow_state_func_params = {device_num = <device_num>}`.
  => **GetButtonsPressed must return `makes` (nonzero on a new press) and
  `device_num` (the pad index)** [I].
- action `continue` -> flow_state_func **bootup_check_for_sign_in** [C].
- destroy: destroy_menu pab_container, destroy_menu_backdrop, KillSpawnedScript
  check_for_any_input / attract_mode_spawner [C].

## 4. Sign-in (flow_state_func bootup_check_for_sign_in)

[C]: KillSpawnedScript attract_mode_spawner; enable_saving = 1; primary_controller =
device_num; primary_controller_assigned = 1; player1_status.controller = it.
`if not CheckForSignin local controller_index=<device_num>` -> on Xenon
`NetSessionFunc {func=showsigninui}` + wait_for_blade_complete; still not
signed in -> `bootup_signin_warning_fs` (popup: continue without saving / choose
device / continue without signing in).
`if CheckForSignin` (no `local`: Live) -> NetSessionFunc stats/motd init, splash,
getbeatadevlist. Then IsXenon -> `process_signin_complete`: RefreshSigninStatus;
StartGameProfileSettingsRead; **`begin if GameProfileSettingsFinished break repeat`
with no Wait** (hard hang unless it returns true); start_checking_for_signin_change
(respond_to_signin_changed = 1, menu_select_difficulty_first_time = 1);
returns `flow_state = bootup_do_memcard_sequence_fs`.
**PC answers**: CheckForSignIn (820ee070) `local` -> true, without `local` -> false
(no Live); RefreshSigninStatus (820e7e80), StartGameProfileSettingsRead
(821408f8) stubs; GameProfileSettingsFinished (82140920) -> true;
NetSessionFunc (821643e8) stub (not reached with those answers) [I].

## 5. bootup_do_memcard_sequence_fs  (guitar_memcard.qb, memcard_using_new_save_system = 1)

- Create `memcard_sequence_begin_bootup {StorageSelectorForce = 1}` -> spawn
  `memcard_sequence_begin_bootup_logic`: MemcardDoneScript =
  memcard_sequence_generic_done, saving, MemcardSuccess false;
  `memcard_validate_card_data StorageSelectorForce=1 validateprev=0` [C]:
  - memcard_choose_storage_device: create_checking_memory_card_screen (popup),
    Wait 3 gameframes, `NewShowStorageSelector force=1 filetype=progress`.
  - memcard_check_for_card: `if not CardIsInSlot` -> goto create_storagedevice_warning_menu.
  - memcard_check_for_existing_save: `MC_EnumerateFolders` (-> result; false ->
    storage warning), `MC_WaitAsyncOpsFinished`, check card,
    `if MC_FolderExists {foldername='Progress'}`: `MC_SetActiveFolder`,
    `MC_LoadTOCInActiveFolder` (-> result), `if MemCardFileExists
    {filename='GH3Progress' filetype=progress}` -> found=1 corrupt=0; else
    found=0. RequireParams [found corrupt]; corrupt -> create_corrupted_data_menu.
  - found = 1 -> `goto memcard_load_file {LoadConfirmed=1}`: MC_WaitAsyncOpsFinished,
    card check, ResetTimer, MC_SetActiveFolder, create_load_file_menu,
    `LoadFromMemoryCard {filename filetype=progress}` (-> result, errorcode),
    MemcardSuccess = true, memcard_wait_for_timer (loop until `TimeGreaterThan 3.0`
    since ResetTimer), create_load_success_menu, memcard_post_load_progress
    (restore_options_from_global_tags, scan_globaltag_downloads), Wait 1 s,
    memcard_sequence_quit -> goto memcard_sequence_generic_done -> action
    `memcard_sequence_load_success` -> **bootup_download_scan_fs** (skips 6).
  - found = 0 -> `goto memcard_save_file`: card check, ResetTimer,
    MC_FolderExists false -> create_save_menu, `MC_CreateFolder
    {name='Progress' desc=guitarcontent}` (-> result, errorcode outofspace),
    memcard_pre_save_progress (career only), `SaveToMemoryCard {filename
    filetype=progress usepaddingslot=always}` (-> result), MemcardSuccess true,
    timer 3 s, create_save_success_menu, Wait 1 s, quit -> action
    `memcard_sequence_save_success` -> **bootup_using_guitar_controller_fs** [C].
- destroy memcard_sequence_cleanup_generic: MC_WaitAsyncOpsFinished, popup cleanup [C].
- **PC answers** [I]: CardIsInSlot true; NewShowStorageSelector no-op;
  MC_EnumerateFolders/MC_LoadTOCInActiveFolder/MC_CreateFolder return
  `result = true`; MC_FolderExists/MemCardFileExists = does the save file exist
  (a per-user file, e.g. `<user data>/GH3Progress`); SaveToMemoryCard serialises
  the global tags (+ whatever else the progress file holds) and returns
  `result = true`; LoadFromMemoryCard restores them; ResetTimer/TimeGreaterThan
  a wall-clock timer (the 3 s minimum "saving..." popup is the original's).
  A cheaper first step: CardIsInSlot false -> storage warning popup -> "CONTINUE
  WITHOUT SAVING" (memcard_disable_saves_and_quit) -> save_failed ->
  using_guitar_controller (needs the popup menu to work).

## 6. bootup_using_guitar_controller_fs  (menu_using_guitar_controller.qb; first boot only)

Create: backdrop boot_usingGuitar_BG, sprites/text (displaySprite/displayText
scripts -> CreateScreenElement), GetEnterButtonAssignment (820ebe00) for the
labels, spawn go_through_using_guitar_text, spawn `check_for_any_input
{mode = startanda}`; returns play_sound 0. Press -> `continue` ->
**bootup_download_scan_fs** [C].

## 7. bootup_download_scan_fs  (guitar_download.qb)

Create `create_download_scan_menu`: menu_flow_play_sound = 0; `downloadcontent_enabled`
is 1 -> popup "Checking for downloadable content..." -> `Downloads_EnumContent`:
`if EnumContentFiles {download dofiles}` loop until EnumContentFilesFinished;
IsEnumContentFilesDamaged; GetLatestContentIndexFile (-> filename; then
LoadPakAsync); Downloads_PostEnumContent; then calls
`ui_flow_manager_respond_to_action {action = continue}` **from inside the
Create** -> **main_menu_fs** [C].
PC: EnumContentFiles (82134ef8) false, IsEnumContentFilesDamaged (82134188)
false, GetLatestContentIndexFile (82134408) false [I].

## 8. main_menu_fs  (main_menu_flow.qb; Create in guitar_menu.qb)

- Create `create_main_menu` [C]: `GetGlobalTags user_options` -> guitar_volume,
  band_volume, sfx_volume (menu_audio_settings_update_*: SoundBussUnLock /
  SetSoundBussParams / SoundBussLock); fadetoblack; create_main_menu_backdrop
  (GH3_Main_Menu_BG + glowing eyes sprites, RunScriptOnScreenElement
  glow_menu_element); first time: `PlayMovieAndWait {movie='GH3_Intro' noblack
  noletterbox}`; SetMenuAutoRepeatTimes (0.3, 0.05); kill_start_key_binding;
  UnPauseGame; current_num_players = 1; disable_pause; spawn Menu_Music_On;
  DeRegisterAtoms / RegisterAtoms achievement (stubs); resets setlist globals,
  current_song = WelcomeToTheJungle, character_id axel; safe_create_gh3_pause_menu
  (only if no Pause_Menu element); `new_menu scrollid=main_menu_scrolling_menu
  vmenuid=vmenu_main_menu`; TextElements CAREER / CO-OP CAREER / QUICKPLAY /
  MULTIPLAYER / TRAINING / XBOX LIVE / OPTIONS (GetScreenElementDims +
  fit_text_in_rectangle); 7 empty TextElements in the vmenu whose handlers
  highlight (guitar_menu_highlighter) and choose; add_user_control_helper
  SELECT / UP-DOWN; `LaunchEvent {Type=focus target=vmenu_main_menu}`.
- Quickplay item: `pad_choose main_menu_select_quickplay` -> game_mode =
  p1_quickplay, current_num_players = 1, player1/2 part = guitar,
  respond_to_action `select_quickplay` -> **quickplay_select_difficulty_fs**
  (transition_right: menu_transition_out_right / in_right screen-element morphs) [C].
- destroy_main_menu: kills helper scripts, destroy_menu x2, backdrop [C].
- No character or guitar select on the Quickplay path: quickplay_start_song
  picks a random guitarist, guitar and venue (section 11) [C].

## 9. quickplay_select_difficulty_fs  (menu_select_difficulty.qb)

Create `create_select_difficulty_menu` [C]: disable_pause; spawn Menu_Music_On;
new_menu scrolling_select_difficulty / vmenu_select_difficulty
(exclusive_device = primary_controller); backdrop Venue_BG + sprites;
TextElements EASY/MEDIUM/HARD/EXPERT with `{focus sd_highlight_menuitem}`,
`{pad_choose selected_difficulty params={diff=easy|medium|hard|expert}}`;
first time on Xenon `if GetGameProfileSetting {gsid=1}` -> pad_down x
game_setting (PC: return false [I]); later: `LaunchEvent pad_down` x index of
`$current_difficulty`; language check `English` (8218c880; return true, the
others false [I]) picks the title texture; user control helpers.
`selected_difficulty`: LaunchEvent unfocus, current_difficulty(2) = diff,
action `continue` -> **quickplay_setlist_fs**; `go_back` -> main_menu_fs [C].

## 10. quickplay_setlist_fs  (menu_setlist.qb)

Create `create_setlist_menu` [C]: Menu_Music_Off (EnableUserMusic disable,
KillSpawnedScript Menu_Music_On, StopSoundEvent); `get_progression_globals
game_mode=p1_quickplay` -> `tier_global = GH3_General_Songs` (8 tiers, prefix
'general'); change g_gh3_setlist; `create_setlist_scrolling_menu` -> `new_menu
tierlist=g_gh3_setlist event_handlers=$setlist_event_handlers
on_choose=setlist_choose_song ...`; new_menu walks tiers: `GetGlobalTags
<'generaltier%i'> param=unlocked` (default 1 if absent), per song
`IsSongAvailable song_checksum='general_song%i_tier%s' song=<id>` (script:
is_song_downloaded -> download=0 true for disc songs; then `GetGlobalTags
<song_checksum> param=unlocked`), get_song_title from gh3_songlist_props,
TextElement with focus/unfocus/pad_choose/left/right/l3 handlers; change_tab;
setlist_display_random_bg_image; cursor restore loop with `LaunchEvent pad_down`
until setlist_selection_tier/song (set by focus handlers) reach the previous
values or stop changing; SpawnScriptLater setlist_songpreview_monitor (PlaySound /
IsSoundPlaying / StopSound preview).
Unlocks come from data via setup_setlisttags: tier1/tier2 `defaultunlocked = 4`
-> first 4 songs of tiers 1-2 unlocked on a fresh save; a faithful
GetGlobalTags/SetGlobalTags gives exactly the original's list [C].
`setlist_choose_song {tier song_count device_num}`: LaunchEvent unfocus
vmenu_setlist; current_song = g_gh3_setlist.tier<N>.songs[i]; SetGlobalTags
Progression {current_tier, current_song_count}; current_level =
g_last_venue_selected; boss songs -> `show_help`; else enable_pause and
action `continue device_num=..` [C].
`continue`: `transition_screen = default_loading_screen` (create_loading_screen
runs first), `func = quickplay_start_song`, flow_state **quickplay_play_song_fs** [C].

## 11. Loading + start (quickplay_start_song -> start_song)

- `create_loading_screen {mode=play_song}` [C]: kill_start_key_binding;
  is_changing_levels = 1; random tip; movie 'loading_flying': `IsMovieInBuffer`,
  FreeMovieBuffer, AllocateMovieBuffer {buffer_slot=0 movie='movies\bik\loading_flying.bik.xen'},
  LoadMovieIntoBuffer, `PlayMovieFromBuffer {buffer_slot textureslot=2 no_hold
  wait_until_rendered texturepri=4999}`; TextBlockElement tip; "LOADING" letters
  (split_text_into_array_elements, StringToCharArray) + jiggle script.
- `quickplay_start_song` [C]: cs_get_total_guitarists, GetRandomValue ->
  random guitarist (character_id, style 1, outfit 1); random guitar from
  Bonus_Guitars + Secret_Guitars with `unlocked_for_purchase` (GetGlobalTags,
  AddArrayElement); random venue (get_total_num_venues / get_valid_venue_index
  -> current_level); `start_song device_num`.
- `start_song` (guitar_gems.qb) [C]: mark_unsafe_for_shutdown;
  set_rich_presence_game_mode; xenon_singleplayer_session_init (CheckForSignin
  false -> nothing on Xenon); **Load_Venue** (GetPakManCurrentName map=zones;
  ResetWaypoints; SetPakManCurrentBlock zones pak=none, then
  `SetPakManCurrentBlock map=zones pak=LevelZones.<level>.zone` = SetPakManCurrent +
  BlockPendingPakManLoads (loop `while PendingPakManLoads` Wait 1 frame) +
  spawn `<zone>_setup` + set_hidebytype; GH3_Change_crowd_reverb_settings_by_Venue);
  Transition_SelectTransition; player1_device/controller = device_num;
  starttime 0, difficulty from globals; **restart_gem_scroller**.
- `restart_gem_scroller` [C]: disable_pause; input_mode = record; **pausegame**;
  LaunchEvent unfocus root_window; kill_gem_scroller restarting;
  **start_gem_scroller**; check_for_unplugged_controllers = 1;
  UnBindGuitarControllerStatus; LaunchEvent focus root_window; **gh3_start_pressed**
  (the game is paused, so this *unpauses*: unpausegh3 -> UnpauseGh3Sounds,
  UnPauseGame, ResumeMovie, action `select_resume` (not in
  quickplay_play_song_fs: no-op), spawn destroy_gh3_pause_menu, BroadcastEvent
  event_unpause_game) [C].
- `start_gem_scroller` [C], in order: playing_song = 1; Menu_Music_Off;
  GuitarEvent_EnterVenue; init_play_log; **load_songqpak** (`LoadPakAsync
  pak_name='songs/<prefix>_song.pak' heap=heap_song async=1`: LoadPak with
  `load_callback = LoadPakAsync_callback`, then loop until
  `character_pak_loadpak_done = 1`, which only the callback sets when called
  with `end` -> **LoadPak must call load_callback** [C]; spawn `<prefix>_setup`
  if it exists); Xenon offsets (default_gem_offset = xenon_gem_offset ...);
  begin_singleplayer_game; current_* globals; Progression_SetProgressionNodeFlags
  (ChangeNodeFlag); bot setup (GetInputHandlerBotIndex) only for bot_play;
  call_startup_scripts; setup_bg_viewport; create_cameracuts (SetNoteMappings);
  Transition_GetTime -> starttime -= transition_time; `SetSlomo 0.001`;
  **reset_song_time** (current_time, `ResetSongTimer starttime=s`);
  create_movie_viewport, create_crowd_models, crowd_create_lighters,
  Crowd_StageDiver_Hide; band info structs; `create_band {async=1}` (false ->
  DownloadContentLost); **preload_song** (SongLoadFSB -> `LoadFSB
  filename='music/<prefix>' numstreams=5 encryptionkey='5atu6w4zaw'` -> fsb_index;
  SoundBussUnlock/SetSoundBussParams/SoundBussLock Band_Balance, Guitar_Balance;
  `PreloadStream <prefix>_song|_guitar|_rhythm|_crowd buss=master` -> unique_id;
  waitforpreload_song (loops `PreloadStreamDone <unique_id>`); song_paused = 1;
  SetLastGuitarVolume; startpreloadpaused_song (`StartPreloadedStream <id>
  startpaused=1 buss=.. pitch=current_speedfactor*100`); SetSeekPosition_Song
  (SetSoundSeekPosition)); reset_score; `GetGlobalTags user_options`
  (lefty_flip_p1, lag_calibration); per player: UpdateGuitarVolume,
  get_resting_whammy_position / get_star_power_position, setup_hud,
  SpawnScriptLater update_score_fast, crowd_reset (CrowdReset),
  star_power_reset, difficulty_setup, setup_highway, reset_hud, **spawn
  gem_scroller, button_checker, SpawnScriptLater check_for_star_power**,
  calc_songscoreinfo (CalcSongScoreInfo); GH3_Set_Guitar_Verb_And_Echo_to_Dry;
  **Transition_Play Type=current_transition** (fastintro); Wait 1 frame;
  StopRendering; destroy_loading_screen (KillMovie slot 2, spawn
  destroy_loading_screen_spawned, HideLoadingScreen); `SetSlomo
  current_speedfactor`; spawn **begin_song_after_intro** (loop GetSongTimeMs until
  `time >= starttimeafterintro`, then begin_song: LockDSP, `PauseSound
  unique_id=.. pause=0` on every stream, UnLockDSP, song_paused = 0);
  mark_safe_for_shutdown.
- Then the flow manager runs transition_screen destroy (destroy_loading_screen)
  and `create_play_song_menu` (empty script) [C].
- Song clock [I]: ResetSongTimer sets the song time (ms) to starttime (negative
  by the intro length); GetSongTimeMs advances with game time scaled by slomo and
  stops while the game is paused (pausegame); audio is started by
  begin_song_after_intro when the clock reaches the real start. Transition_Wait /
  Transition_Play_Spawned also time out by GetSongTimeMs, so the clock must also
  run after the song ends (song won).

### What gameplay calls every frame (all C, spawned by start_gem_scroller / gem_scroller)

Pattern [C]: `X_CFunc_Setup; begin if X_CFunc break; Wait 1 gameframe repeat;
X_CFunc_Cleanup` (the CFunc returns true when done):
| Script (spawner) | Natives each frame |
|---|---|
| gem_scroller -> gem_iterator -> gem_array_stepper x (fill_array, create_gem, bot...) | gem_array_stepper_CFunc_Setup/_CFunc/_Cleanup (821080d0/82108148/82108120); calls strum_function fill_input_array -> FillInputArray (82104b90), gem_function create_gem -> Create2DGem (82121c58); gem_step -> GemStep (82122b10) |
| gem_array_events | SetEventHandler, Block |
| fretbar_iterator x3 (create_fretbar, GuitarEvent_PreFretbar, GuitarEvent_Fretbar) | fretbar_iterator_CFunc_* (821074a0/821111b8/821074f0), Create2DFretbar (82121e00) |
| fretbar_update_tempo | fretbar_update_tempo_CFunc_* (82107918/82107990/82107968) |
| fretbar_update_hammer_on_tolerance | fretbar_update_hammer_on_tolerance_CFunc_* (82107518/82107590/82107568) |
| check_buttons_fast | GuitarInputLogicInit (8210d258) once; GuitarInputLogicPerFrame (8211fa28) until false |
| button_checker | ButtonCheckerInit (82105de8), ButtonCheckerPerFrame (8211b1e0), ButtonCheckerCleanup (82106280) |
| check_for_star_power | check_for_star_power_CFunc_Setup/_CFunc/_Cleanup (82108890/821129d8/82108920) |
| update_score_fast | UpdateScoreFastInit (82114fe8); GetSongTimeMs + UpdateScoreFastPerFrame (821167b0) |
| check_note_hold (per sustain) | CheckNoteHoldInit/Wait/Start/PerFrame/End (82106758/8211c578/8211c658/8211c9d0/8211d3b8) |
| Strum/FretFingers/FretPos/Drum iterators (band animation) | FretFingers_iterator_CFunc_*, Drum_iterator_CFunc_*, TimeMarkerReached(_SetParams/_ClearParams) (82271520/82271660/82271638) |
| lightshow_iterator, cameracuts_iterator, event_iterator, notemap_iterator | TimeMarkerReached*, LightShow_*, GetNoteMapping, SymbolIsCFunc |
| move_highway_2d, star_power_whammy, wait_for_inactive | GetDeltaTime, GuitarGetAnalogueInfo (82101228), IsGuitarController (82100d00), MakeStarWhammy/MakeNormalWhammy |
| GuitarEvent_* (spawned on events) | GuitarEvent_HitNotes_CFunc (82110d98), NoteFX, StarPowerOn (8211da70), WhammyFXOn/Off, UpdateGuitarVolume (821105c8), CrowdDecrease (82110570), UpdateNixie, Create2DParticleSystem/Destroy2DParticleSystem |
| win_song | GetArraySize of the gem array, get_song_end_time, `Wait end_s Seconds`, `Wait Song_Win_Delay Seconds`, PreLoadMovie {textureslot=1 ...}, ExtendCRC song_won 'p1', **BroadcastEvent** |
The engine already has the judge (docs/FINDINGS.md); these natives are the
interface the scripts expect from it. Fail: health <= 0 is decided in C
(0x8210FD18 per FINDINGS.md); the script side only listens for event
`song_failed`+'p1' (no script raises it) -> the native must raise that event [I].

## 12. Song won -> results -> back to setlist

- win_song (spawned by gem_scroller) waits to the end marker + Song_Win_Delay,
  then `BroadcastEvent Type=song_wonp1` -> event_spawner -> GuitarEvent_SongWon
  (stats dump only if NotCD) -> spawn **GuitarEvent_SongWon_Spawned** [C]:
  Progression_EndCredits_Done; **pausegame**; kill_start_key_binding; SoundEvent
  You_Rock_End_SFX; PreLoadMovie (KillMovie slot 1); spawn
  wait_and_play_you_rock_movie (loop `IsMoviePreLoaded textureslot=1` ->
  StartPreLoadedMovie; harmless if it never loads: killed later);
  "You Rock!" letters (StringLength, split_text_into_array_elements);
  waitAndKillHighway, jiggle_text_array_elements; Progression_SongWon (global
  tags: scores, stars, unlocks); **UnPauseGame; Transition_Play songwon;
  Transition_Wait** (loop until `transition_playing = false`, set by
  Transition_Play_Spawned when GetSongTimeMs passes the transition time);
  pausegame; destroy_menu yourock_text...; **action `win_song`** (func
  kill_gem_scroller: StopRendering, SongUnLoadFSBIfDownloaded, destroy HUD /
  highway, Destroy2DParticleSystem all, `LaunchGemEvent {event=kill_objects}`,
  KillSpawnedScript of every gameplay script) -> **quickplay_win_song_fs**;
  agora/net stats (stubs) [C].
- quickplay_win_song_fs Create `create_newspaper_menu` (1,369 lines: newspaper
  stats page; `GetGlobalTags Progression`, `GetGlobalTags user_options`,
  StopSoundsByBuss, new_menu newspaper_vmenu, np_create_options_menu) [C].
  Options (game_mode p1_quickplay): CONTINUE (`continue`), RETRY SONG
  (`try_again`: func start_song + loading screen -> quickplay_play_song_fs),
  MORE STATS (`select_detailed_stats` -> quickplay_detailed_stats_fs), QUIT (`quit` -> main menu) [C].
- `continue` -> **quickplay_toprockers_fs** (create_top_rockers_menu:
  backdrop toprockers_bg, poster, menu_top_rockers_check_for_new_top_score; a
  new top score -> band-name entry (StringRemoveTrailingWhitespace); else a
  continue button whose handler sends `continue`) -> **quickplay_autosave_fs**
  (memcard_sequence_begin_autosave -> spawn _logic: disable_pause; `GetGlobalTags
  user_options` autosave = 0 -> goto memcard_sequence_quit; CardIsInSlot;
  memcard_validate_card_data validateprev=1 (MC_HasActiveFolder ...); found ->
  memcard_save_file OverwriteConfirmed=1) -> `memcard_sequence_save_success`
  or `_failed` -> **quickplay_setlist_fs** [C].

## 13. Fail path

C judge raises `song_failedp1` [I] -> GuitarEvent_SongFailed -> spawn
**GuitarEvent_SongFailed_Spawned** [C]: disable_highway_prepass,
disable_bg_viewport; `GetSongTimeMs` -> failed_song_time; Achievements_SongFailed;
pausegame; Progression_SongFailed; xenon session uninit; UnPauseGame; SoundEvent
Crowd_Fail_Song_SFX / GH_SFX_You_Lose_Single_Player; **Transition_Play songlost;
Transition_Wait**; pausegame; restore_start_key_binding; spawn action
`fail_song` -> **quickplay_fail_song_fs** [C].
Create `create_fail_song_menu`: get_song_end_time, completion % =
100*failed_song_time/total_end_time (CasttoInteger), difficulty text, items
RETRY / PRACTICE / NEW SONG / QUIT [C].
- RETRY: fail_song_menu_select_retry_song: GH3_SFX_fail_song_stop_sounds;
  UnPauseGame; action `select_retry` -> quickplay_play_song_fs (Create empty);
  then **restart_song** (create_loading_screen mode=restart_song,
  current_transition = fastintro, restart_gem_scroller with the current globals) [C].
- NEW SONG: `select_new_song` (func kill_gem_scroller) -> quickplay_setlist_fs [C].
- QUIT: `select_quit` (func quickplay_song_select_quit) -> main_menu_fs [C].

## 14. Pause menu

During the song root_window's pad_start handler is gh3_start_pressed (set by
enable_pause in setlist_choose_song; restart_gem_scroller's own call unpauses).
gh3_start_pressed {device_num}: device must equal player1_status.controller;
`GameIsPaused` false -> remember device -> spawn gh3_start_pressed_spawned ->
**pausegh3**: BroadcastEvent event_pause_game; PauseGh3Sounds (LockDSP,
PauseSoundsByBuss, UnLockDSP); PauseFullScreenMovie; **pausegame**;
PauseMovie slots 0/1 if playing; action `pause_game` -> **quickplay_pause_fs**;
then safe_create_gh3_pause_menu, viewer_buttons_enabled = 0, spawn block_input
(SetButtonEventMappings) [C].
Create `create_pause_menu` (721 lines; `GetGlobalTags user_options`): RESUME
(`pad_choose gh3_start_pressed` -> GameIsPaused true -> unpausegh3:
UnpauseGh3Sounds, UnPauseGame, ResumeMovie, ResumeControllerChecking, action
`select_resume` -> quickplay_play_song_fs), RESTART (`select_restart` ->
quickplay_restart_warning_fs -> `continue` func career_restart_song + loading
screen), OPTIONS (quickplay_pause_options_fs), QUIT (`select_quit` ->
quickplay_quit_warning_fs -> `continue` -> quickplay_song_ended_fs: retry /
new song / quit, each with kill_gem_scroller) [C].
=> PauseGame (820eabd8) / UnPauseGame (820eac20) / GameIsPaused (820eac50) must
be real: pausing must freeze the song clock, gameplay CFunc scripts and
"pausable" spawned scripts while menu scripts keep running [I].

## 15. Natives needed, prioritised

Priority: P0 = the flow stops without a real implementation; P1 = needed for a
faithful, playable path; S = safe as a stub with the given return.
"HAVE" = already in engine/src/script/natives_core.cpp.

| P | Native | Addr | Uses on path | Required behaviour |
|---|---|---|---|---|
| P0 | SetArrayElement | 82269a88 | flow manager (every transition), difficulty | `arrayname` (global with `globalarray`) `index` `newvalue`; `resolveglobals = 0` stores the checksum as-is |
| P0 | GetGlobalTags / SetGlobalTags | 82118488 / 821238b8 | setlist unlocks, user_options, progression, memcard | keyed struct store; Get copies all fields (or `param=x`) into locals, returns false if the tag is absent (`noassert`); Set merges `params` |
| P0 | PushGlobalTags / PopGlobalTags | 821239d0 / 82123a98 | progression push/pop (save) | stack copy of tags |
| P0 | AddParams, RequireParams, Goto, ExtendCrc, ScriptExists, GlobalExists, CastToInteger, MathMin, StringLength, AddArrayElement | 82263e80, 8226a368, 822648b0, 822653f0, 82265b98, 82263f70, 82262d28, 82272398, 822670b8, 8218ee58 | memcard, progression, menus, loading | script utilities; Goto = tail-call into another script with `params` |
| P0 | SetEventHandler, Block, BroadcastEvent, LaunchEvent, OnExitRun, SetSpawnInstanceLimits, ClearEventHandlerGroup, ResetEventHandlersFromTable | 82266088, 82268b38, 82158160, 82158188, 82266310, 8226a0c8, 82266238, 82266178 | guitar events (song won/failed), menus, music | event system: call_script handlers on a blocked script; LaunchEvent synchronous to a screen element |
| P0 | NotCD / CD | 822873c8 / 822873b8 | boot, movies, cheats | NotCD false, CD true (retail) |
| P0 | GetButtonsPressed | 820e7be8 | press any button, movie skip | `makes` (new presses, 0 when none), `device_num`; mode `startanda` = Start/A only |
| P0 | CheckForSignIn | 820ee070 | sign-in | `local` -> true; Live -> false |
| P0 | GameProfileSettingsFinished | 82140920 | sign-in busy loop | true |
| P0 | CreateScreenElement, DestroyScreenElement, ScreenElementExists, SetScreenElementProps, GetScreenElementProps, GetScreenElementDims, DoScreenElementMorph, RunScriptOnScreenElement, SetScreenElementLock, GetScreenElementChildren, ResolveScreenElementID, GetScreenElementPosition, TerminateObjectsScripts | 82171670, 82172640, 82172518, 821720e0, 82171740, 82172310, 821721b0, 82171ec0, 82172258, 821718f8, 82171a50, 82172408, 821562c8 | every menu | element tree (container, SpriteElement, TextElement, TextBlockElement, vmenu, vscrollingmenu), event_handlers, focus, exclusive_device, pad events from input; returns `id`, `width`/`height`; member functions DoMorph, Die, SetProps, GetTags/SetTags/GetSingleTag/RemoveTags, Obj_GetID (no cfuncs entry) |
| P0 | SetMenuAutoRepeatTimes | 8214af40 | main menu | pad repeat (0.3 s, 0.05 s) |
| P0 | PauseGame / UnPauseGame / GameIsPaused | 820eabd8 / 820eac20 / 820eac50 | song start (restart_gem_scroller pauses, gh3_start_pressed unpauses), win, fail, pause | real pause state; see section 14 |
| P0 | LoadPak / UnloadPak / WaitUnloadPak | 8218b378 / 8218c548 / 8218c138 | song pak (async with load_callback), venues | load; **call `load_callback` with `end`** when an async load finishes |
| P0 | SetPakManCurrent, PendingPakManLoads, GetPakManCurrent, GetPakManCurrentName | 82182d18, 82182af8, 82182df8, 821844a0 | Load_Venue, transitions, crowd | zone map; return `pak` / `pakname` (e.g. z_party), Pending false when loaded |
| P0 | ResetSongTimer, GetSongTimeMs, GetSongTime, GetDeltaTime, SetSlomo | 82271058, 82271130, 822710f0, 82271290, 820ead08 | intro wait, transitions, score, iterators | song clock (section 11) |
| P0 | LoadFSB, PreLoadStream, PreLoadStreamDone, StartPreLoadedStream, PauseSound, SetSoundSeekPosition, StopStream, UnLoadFSB, LockDSP, UnLockDSP | 822806d0, 8227f410, 8227f4b8, 8227f4f0, 8227f7b0, 8227eb48, 8227e580, 8227e0e8, 8227e130, 8227e158 | song audio | `fsb_index`, `unique_id`; PreLoadStreamDone true once ready |
| P0 | Gameplay CFuncs (table in section 11) incl. InputArrayCreate 8210a598, GuitarInputLogicInit/PerFrame, ButtonChecker*, check_for_star_power_CFunc*, UpdateScoreFast*, gem_array_stepper_CFunc*, Create2DGem, GemStep, FillInputArray, fretbar_*_CFunc*, Create2DFretbar, CheckNoteHold*, CalcSongScoreInfo 8210af70, CrowdReset 821062a8, TimeMarkerReached* | see table | the song | the existing judge behind the script interface; raise `song_failed<p>` events; `LaunchGemEvent {event=kill_objects}` (8211a570) on kill |
| P1 | PlayMovie, IsMoviePlaying, KillMovie, PreLoadMovie, IsMoviePreLoaded, StartPreLoadedMovie, PauseMovie, ResumeMovie, IsMovieInBuffer, AllocateMovieBuffer, FreeMovieBuffer, LoadMovieIntoBuffer, PlayMovieFromBuffer | 8227bff0, 822766f8, 8227c0c8, 82276378, 82276400, 822763a0, 82276458, 822764b8, 82276a70, 822767e8, 822768a8, 82276978, 82276a00 | logos, intro, loading, You Rock | stub: IsMoviePlaying/IsMoviePreLoaded false (skips) |
| P1 | Memcard set: CardIsInSlot, NewShowStorageSelector, MC_EnumerateFolders, MC_WaitAsyncOpsFinished, MC_FolderExists, MC_SetActiveFolder, MC_HasActiveFolder, MC_LoadTOCInActiveFolder, MemCardFileExists, MC_CreateFolder, SaveToMemoryCard, LoadFromMemoryCard, SetSaveFileName, ResetTimer, TimeGreaterThan | 82133d28, 821350d0, 82133788, 82133a18, 82133a48, 82133838, 82133b38, 821339c8, 82133c38, 821337b8, 82133e40, 82133fc8, 82134148, 82270dd0, 82270e00 | bootup + autosave | section 5 (file-backed save) |
| P1 | SoundEventFast (via script SoundEvent), StopSoundEvent, IsSoundEventPlaying, PlaySound, StopSound, IsSoundPlaying, SetSoundBussParams, SoundBussLock/UnLock, SetSoundBussEffects, PauseSoundsByBuss, UnPauseSoundsByBuss, StopSoundsByBuss, EnableUserMusic | 82280028, 8227dee0, 8227df38, 8227e750, 8227ea90, 8227f158, 8227ce58, 8227cdc8/8227ce10, 8227ff10, 8227ecc8, 8227ed08, 8227e618, 8227fd58 | menu SFX/music, previews | stubs OK first (IsSoundEventPlaying/IsSoundPlaying false) |
| S | GetDisplaySettings, SetScreen, StartRendering_C, StopRendering_C, DisplayLoadingScreen, HideLoadingScreen | 82278820, 82275f08, 82266618, 822665e8, 8218dde8, 8218e0a0 | boot, loading | widescreen = true; no-ops |
| S | ReAcquireControllers, GetEnterButtonAssignment, ResumeControllerChecking, UnBindGuitarControllerStatus, SetInput, SetButtonEventMappings | 82283d20, 820ebe00, 820e7bd0, 82100d98, 82100bb8, 8214b0a0 | press any button, song start, pause | no-ops; enter button = A [I] |
| S | NetSessionFunc, RefreshSigninStatus, StartGameProfileSettingsRead, GetGameProfileSetting, MassiveInit, IsHost, SendNetMessage | 821643e8, 820e7e80, 821408f8, 82140950, 822437e8, 82201e18, 820dad60 | sign-in, stats | no-ops; GetGameProfileSetting false, IsHost false |
| S | EnumContentFiles, EnumContentFilesFinished, IsEnumContentFilesDamaged, GetLatestContentIndexFile, GetContentFolderIndexFromFile | 82134ef8, 82134ff0, 82134188, 82134408, 82134378 | DLC scan, song/venue paks | false / `device` not `content` |
| S | RegisterAtoms, DeRegisterAtoms, UpdateAtoms, PrintScriptInfo, UnPauseSpawnedScript, English (true), French/German/Spanish/Italian (false) | 821237f0, 82118088, 821182a8, 82265538, 82266ac0, 8218c880 ... | menus | no-ops |
| S | DataBufferPut*/Get*, ClearDataBuffer, ReadDataBuffer, GetRandomSeeds, SetRandomSeeds, TextOutput* | 820ffc68 ... | replay recording, stats dump | no-ops (replay only read with `replay` param) |
| S (first song) | venue/band/crowd/camera/lights: CompositeObjectExists (false), CreateCompositeObject*, IsCreated (false), IsInNodeArray (false), Create/Kill, SetViewportProperties, PrepassViewport*, CreateViewportTextureOverride, ScreenFX_*, LightShow_*, PlayIGCCam, CameraCut_*, Create2DParticleSystem, DeleteTeslaEffectObject, ChangeNodeFlag, GetNodeFlag, ResetWaypoints, DoesWaypointExist (false) | various | start_gem_scroller, kill_gem_scroller | returns that skip creation; none of their loops wait on them except crowd_create_lighters (bounded to 15) |

## 16. Loops that wait on natives (hang if the native never answers)

- guitar_startup: GetTrueElapsedTime >= 6000 (no Wait in the loop).
- process_signin_complete: GameProfileSettingsFinished (no Wait).
- PlayMovieAndWait: IsMoviePlaying (exits when false).
- check_for_any_input: GetButtonsPressed `makes`.
- memcard_choose_storage_device (old system only): StorageSelectorFinished.
- memcard_wait_for_timer: TimeGreaterThan 3.0.
- Downloads_EnumContent: EnumContentFilesFinished (only if EnumContentFiles true).
- LoadPakAsync: `character_pak_loadpak_done` (set by LoadPakAsync_callback from LoadPak).
- BlockPendingPakManLoads: PendingPakManLoads.
- waitforpreload_stream: PreLoadStreamDone.
- begin_song_after_intro: GetSongTimeMs >= starttimeafterintro.
- Transition_Wait / Transition_Play_Spawned: GetSongTimeMs passes the transition time.
- every *_CFunc iterator: its CFunc returning true (end of chart).
- wait_and_play_you_rock_movie: IsMoviePreLoaded (killed by kill_gem_scroller, harmless).
- Menu_music_Checking: IsSoundEventPlaying (exits when false).
