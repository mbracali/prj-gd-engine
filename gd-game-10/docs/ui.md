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
