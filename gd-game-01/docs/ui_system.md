# GH1 (PS2) menu system: spec for reimplementation

Sources: `extracted_dta/ghui/gen/*.dta`, `extracted_dta/config/gen/{gh,macros,system_script,songs,campaign,synth}.dta`,
`extracted_dta/_up_/_up_/system/run/config/gen/{macros,joypad}.dta`, and the decomp in `analysis/decomp`.
**[I]** marks an inference that the code does not prove. Everything else was read from scripts or code.
A dta tip: `$S/flat.sh file.dta` (in my scratchpad) puts a whole script on one line per object, which makes it much easier to read.

---------------------------------------------------------------------------------------------------
## 0. Bootstrapping the UI (config/gen/gh.dta)

```
(ui (rnd_extension gh) (shared_rnd_files ghui/common.gh)
    (create #include ../ghui/ui.dta  {meta set_defaults} {ui my_init}
            {if {! {app app_child}} {ui goto_screen $first_screen}})
    #include ../ghui/sfx.dta)                 ; global sound handlers on the `ui` object
(ghui #include ../ghui/config.dta)            ; component styles
(locale ghui/eng/locale.dta)
(joypad (hold_ms 1000) (repeat_ms 80) ...)    ; menu key-repeat defaults
```
- `ui.dta` includes all the screen scripts and preloads 4 panels (`{ui panel pause} load`, also tut_pause, lose and helpbar).
- `meta set_defaults` (splash.dta) sets `$first_screen = bootup_load`, `$autosave FALSE`, `$main_entered FALSE` and calls `setup_quickplay`.
- UIManager script verbs, registered in FUN_002094d8: `new_panel new_screen new_style new_button new_label new_icon new_lrbutton new_list new_text_entry new_picture`.
  GH adds `new_gh_panel` (FUN_001426b8), `new_gh_screen` (FUN_00143138), `new_metagame_panel`, `new_helpbar_panel`,
  `new_charsys_panel`, `new_guitar_display_panel`, `new_guitar_select_panel`, `new_store_panel`, `new_status_panel`,
  `new_slider_panel`, `new_credits_panel`, `new_game_panel`, `new_arena_panel`, `new_fade_panel` and `new_hud_panel`.
  Each one takes `name` followed by a typedef body: properties plus message handlers, which works like a class definition.
- Each named panel or screen is also a global object (`sel_song`, `store`, `nameprof`, `helpbar`, `meta`, ...).
  Scripts reach them as `{ui panel X}` / `{ui screen X}`, or directly by name when the panel sets `object_name`.
- Panel and screen handlers run with the namespace `ui` pushed (FUN_0023e408(…,"ui")). A bare component name
  (`main_msg.view`, `cm_cash.lbl`) is looked up in the current panel's RndDir. `ui::foo.view` looks up `foo.view` in the ui namespace,
  which in practice means the panel's own scene objects **[I]**.

## 1. Screens, panels, transitions

### 1.1 Panel (UIPanel; GHPanel = `new_gh_panel`)
Properties, read in ctor FUN_00205870 and Load HudPanel_v13 0x205e38:

| prop | default | meaning |
|---|---|---|
| `rnd_file` | `ghui/<name>.gh` | panel scene (RndDir); `""` means no scene (meta, game panels) |
| `object_name` | name | global object name |
| `retain_last_focus` | TRUE | keep the last focused component when re-entered (otherwise use `(focus …)`) |
| `force_exit` | FALSE | exit and re-enter even when the next screen also uses this panel |
| `player_num` | -1 | when ≥0, the panel only accepts that pad's buttons (splash and guitar_help use 0) |
| `focus` | – | initially focused component |
| `navigator` | – | `(navigator (wrap BOOL)? (vertical c1 c2 …) (horizontal …))` |
| `joypad` (GHPanel) | – | `(joypad (hold_ms N) (repeat_ms N))`, a key-repeat override while the panel is up (sel_song uses 500/80) |
| `clear_vram` (GHPanel) | – | PS2 texture flush; ignore |

Panel state (+0x4c): 0 = unloaded, 2 = loaded/down, 1 = up (`is_up`).

- **Enter** (UIPanel slot3, 0x206b90): state=up. Focus is re-applied through `SetFocus(saved)`, so a `FOCUS_MSG` fires. Then the components' Enter runs,
  then the panel's `enter` script.
- **Exit** (slot4, 0x206e38): state=down, then the `exit` script, then the components' Exit.
- **Poll** (slot10, 0x2070b0): the panel TaskMgr ticks (`anim_task`/`delay_task`), then the `poll` script, then the components' Poll, then the RndDir anim poll.
- `finish_load` is called after the scene loads (scripts use it to reset view frames). `load`/`unload` are messages.
- **SetFocus(c)** (slot12, 0x207328): no-op if c is already focused. Otherwise it **first** sends `FOCUS_MSG(new,old,panel,screen)`,
  then sets old → state kNormal and new → state kFocused.
- GHPanel methods (GHPanel_v1 0x142b20):
  - `focus_name` returns the name of the focused component.
  - `enable_component c` sets c to kNormal, but only if it is disabled.
  - `disable_component c`: if c has focus, focus first moves to the next item of the *vertical* navigator, the same as pressing Down. Then c becomes kDisabled.
  - `set_showing B` shows or hides the panel scene.
- UIPanel methods (0x207748): `is_up`, `set_paused`, `set_focus c`, `load`, `unload`. Scripts also use
  `$this set/get/has key` (property bag), `anim_task obj ms from to`, `delay_task ms [(preserve $this)] {…}`, `clear_tasks`.

### 1.2 Navigator (FUN_00207b68 + FUN_002074a0)
This runs only on `button_down`, before the panel script sees the button.
- Buttons 12/14 (DUp/DDown) use the `vertical` list. DDown moves to the next item (index+1), DUp to the previous one.
  Buttons 13/15 (DRight/DLeft) use the `horizontal` list. DRight moves to the next item.
- The navigator finds the list that contains the focused component and steps from there.
  - **`wrap` defaults to TRUE**, so stepping is modulo the list length. `(wrap FALSE)` clamps at both ends (sel_venue uses it).
  - Components whose state is **kDisabled (2) are skipped**. If the walk comes back to the start (wrap) or hits an end (clamp), the result is "unhandled".
- On success it calls `SetFocus(target)` and returns handled. The global sfx handler plays `button_toggle` on FOCUS_MSG.

### 1.3 Screen (UIScreen; GHScreen = `new_gh_screen`)
Properties (FUN_00207e10, FUN_00142d80):

| prop | default | meaning |
|---|---|---|
| `panels` | – | ordered panel list; this is also the **draw order** **[I]** |
| `focus` | – | which panel gets input (the "focus panel") |
| `allow_back` | TRUE | the back button auto-returns to `back_screen` |
| `in_game` | FALSE | in-game screen; in single player, input from player ≠0 is swallowed |
| `animate_transition` | TRUE | play the 500 ms camera/lighting transition (see below) |
| `helpbar` | – | `(helpbar (default (elems…)) (<component_name> (elems…)) …)` |
| any other `(key val)` | | custom data that the screen reads back (`sel_song_career`, `auto_back`) |

GHScreen also tracks `back_screen`, plus the global flags `going_back` and `forward` (anim direction).

- **Exit(next)** (UIScreen_v5 0x208c78, GHScreen_v5 0x143418):
  1. GH: `going_back = next is in this screen's back_screen chain` (FUN_00143b40).
  2. Run the screen's `exit` script.
  3. Start async-loading next's panels.
  4. For each own panel, call `Exit()` **only if** `force_exit`, or `next==null`, or the panel is **not** in `next.panels`.
     So shared panels (meta, helpbar, sel_song, the GAME_PANELS) stay up and do not re-run `enter`.
  5. If `animate_transition`, start the out-anim.
- **Enter(prev)** (UIScreen_v6 0x208878, GHScreen_v6 0x143170):
  1. Unload prev's panels (refcounted).
  2. `Enter()` every own panel that is not already up, in list order.
  3. Store the enter time, run the screen's `enter` script, then do one Poll.
  4. GH: if not going back and prev has `allow_back`, set `back_screen = prev`.
  5. Load the helpbar for the focused component (falling back to `default`).
  6. If `animate_transition`, start the in-anim.
- **Poll** (slot4): polls all panels in order and the helpbar. Screen-level `poll` handlers exist in scripts (game, win_easy, win_game)
  and must run every frame **[I: call site not located]**.
- **Transition anim** (FUN_00143290). It uses objects from `ghui/common.gh`: `ui.enm` (ambient EnvAnim), `meta_cam_right_in.tnm`,
  `meta_cam_right_out.tnm` and `meta_cam_pause_right_out.tnm`. Two 500 ms anims run together:
  - `ui.enm`: frames 0→500 on enter, 500→0 on exit.
  - The camera tnm. Entering forward uses `right_in` (0→500). Entering backwards uses `right_out`. Exiting forward uses
    `pause_right_out`, and exiting backwards plays `right_in` in reverse.
  - The transition counts as "busy" until the 500 ms have passed. After that the `forward` flag resets to 1.
  - Script `{$this backwards_anim}` sets `forward=0` before a `goto_screen`.
  - For a simple port: cross-fade or slide for 500 ms, and reverse the direction when going back.
- `turn_off_ambient` is `ui::ui.enm set_frame 0`; `reset_ambient` is `set_frame 500` (loading.dta). These are the menu lighting levels.

### 1.4 UIManager (`ui` object, handler UIManager_t1_v1 0x20aa28; poll FUN_00209ec0)
Methods: `goto_screen S [force]` (also `transition_to_screen`), `push_screen S`, `pop_screen [S]`, `screen name`,
`panel name`, `component name`, `current_screen`, `focus_panel`, `frame` (UI time in **ms**), `in_transition`, `use_joypad B`.

Transition state machine (`state` 0 = idle, 1 = goto, 2 = entering, 3 = popping), driven by `goto_screen T` (FUN_0020a4b0):
1. If T is already the target of a running transition, ignore the call. Otherwise cancel any running transition (FUN_0020a418).
2. Send `SCREEN_CHANGE_MSG(new=T, old=cur)`. Set state=1 and target=T. Call `cur.Exit(T)`, or just load T's panels if there is no current screen.
3. Poll, state 1: once every panel of T is loaded **and** `cur` has finished its out-anim:
   set state=2, prev=cur, cur=T, and call `T.Enter(prev)`.
4. Poll, state 2: once T has finished its in-anim, send `TRANSITION_COMPLETE_MSG(new=T, old=prev)` and set state=0.
- `push_screen S`: push cur onto a stack, set cur=null, then goto S. Stacked screens still Poll (and stay drawn **[I]**).
  Used by modal dialogs: error_*, store_bought.
- `pop_screen [S]`: set state=3. Once cur's out-anim is done, unload it, pop the stack top into cur, and send TRANSITION_COMPLETE (or goto S).
- **Input gating**: `button_down`/`button_up` (and one more input type) are dropped while `state≠0`, or while a blocking object is active (FUN_0020a738).

### 1.5 Message routing (order matters)
A message sent to the UIManager (from input or from a component) goes through these stages in order, and stops at the first stage that handles it:
1. UIManager built-in methods.
2. The `ui` object's script handlers (`sfx.dta`: SELECT/SCROLL/FOCUS/BAD_SELECT/SCREEN_BACK). These only play sounds and return kDataUnhandled.
3. Message sinks.
4. The **current screen**:
   - **GHScreen_v1** (0x143bc0):
     - `backwards_anim` sets the direction flag.
     - On `button_down` it runs the **auto-back** logic (FUN_001435e0):
       - If `in_game`, the game is single player, and player≠0, the button is swallowed.
       - If `allow_back`, `back_screen` is set and `meta is_back_button` is true:
         1. Run the screen's script handler for this message, then the focus panel's Handle.
         2. If **both** return kDataUnhandled: set backwards, `goto_screen back_screen`, send `SCREEN_BACK_MSG(old,prev)`, and return handled.
     - On `FOCUS_MSG` from the focus panel, it reloads the helpbar keyed by the new component name.
     - On `TRANSITION_COMPLETE` for itself, it reloads the helpbar.
   - **UIScreen_v1**: the screen script handler, then sinks, then the **focus panel** Handle.
     - GHPanel methods.
     - UIPanel: the **navigator** (button_down only), then the **panel script handler**, then sinks, then the **focused component** Handle.
- Handler return value: a handler that ends in `kDataUnhandled` passes the message on. Any other value counts as handled.
  Example: `qp_diff`'s screen handler runs first and does `{game set_quickplay}…{ui goto_screen loading} kDataUnhandled`, so panel `sel_difficulty` then also gets
  `SELECT_MSG` and calls `game set_difficulty`. This works because goto_screen is asynchronous.
- Components raise `SELECT_MSG` by sending it to the UIManager, so it goes through the whole chain above.
  On every screen, the screen handler sees it **before** the panel handler.

Message names (`macros.dta`, args become `$vars` in handlers):

| macro | symbol | args |
|---|---|---|
| BUTTON_DOWN_MSG / BUTTON_UP_MSG | button_down / button_up | $button $player_num |
| SELECT_MSG | component_select | $component $panel $screen $button |
| FOCUS_MSG | component_focus | $new_focus $old_focus $panel $screen |
| SCREEN_CHANGE_MSG | screen_change | $new_screen $old_screen |
| TRANSITION_COMPLETE_MSG | transition_complete | $new_screen $old_screen |
| SCREEN_BACK_MSG | screen_back | $old_screen $prev_screen |
| SCROLL_MSG | component_scroll | $component $player_num $screen $direction $done |
| SCROLL_START_MSG | component_scroll_start | $component $panel $screen |
| GH_SELECT_MSG | gh_component_select | $component $player_num $screen $button (guitar select panel) |
| GH_CANCEL_MSG | component_cancel_selection | $component $panel $screen $button (slider cancel) |
| BAD_SELECT_MSG | component_bad_selection | … (plays button_error) |
| TEXT_ENTRY_MSG / TEXT_ENTRY_INVALID_MSG | text_entry / text_entry_invalid | $component $text / … |
| JOYPAD_CONNECT_MSG | joypad_connect | $player_num $connected |
| MEMCARD_RESULT_MSG | memcard_result | $result $space_free |

Lifecycle handlers: `enter`, `exit`, `poll`, `finish_load`, `load`, `unload`. Panel and screen can also have custom methods,
called as `{$this name args}` or `{obj name args}` with a `($arg …)` parameter list.

### 1.6 Helpbar (HelpBarPanel, `new_helpbar_panel helpbar (max_labels 4)(max_buttons 4)(button_spacing 40)(strumbar_spacing 80)(text_spacing 25)`)
- The input is an array of `(elem token)`, where elem ∈ `fret1 fret2 strum start` **[start I]**. The icon material is `hb_<elem>.mat` (helpbar.gh;
  images `ghui/image/helpbar_fret1/fret2/strum/start`). The text is `localize token` (e.g. help_continue → "CONTINUE").
  The template objects in resources.rnd_ps2 are `help_bar.txt`, `help_bar.mesh`, `help_bar_strum.mesh` and `help_box_{left,mid,right}.mesh`.
- Layout: buttons are laid out with these spacings **[exact layout I]**.
- Screens set the helpbar contents through `(helpbar …)`. Scripts override it with `{helpbar set_display ((fret1 help_select) …)}`; an empty `()` hides all elements.

## 2. Components

All components share the state set: `kNormal 0`, `kFocused 1`, `kDisabled 2`. ButtonEx and Slider also have a transient `selected` state.
Styles live in `ghui/config.dta` under `(components (<class> (colors …) (styles (<style> …))))`.
They reference template objects by name in `ghui/gen/resources.rnd_ps2`, looked up as `ghui::<name>`.
Default colours are in `(colors (text (normal .5 .5 .5)(focused .9 .9 .9)(selected 0 0 0)(disabled .5 .5 .5)) (mesh (normal .3)(focused .7)(selected .7)(disabled .4)))`.
A class colour such as `(text_color text)` means "use that per-state table".

- **ButtonEx** (RndButtonEx; ctor FUN_0012afb0, state FUN_0012cee8/FUN_0012c090/FUN_0012c1c8, input FUN_0012d060)
  - Construction: the button builds its RndText by copying `ghui::button.txt` (class default `(text "button.txt")`) and centring it (align 0x22).
    It also gets its own copy of `ghui::button.tnm`. The record fields are `box_width/box_height` (default 400×40), jitter, style and text token.
  - The style gives `font` (e.g. `main_menu` → `impactor2.font`) and `colors text_color` with normal/focused/selected/disabled.
    Example main_menu: normal (.9,.9,.9), focused and selected (.9,.8,.1), disabled (.4,.4,.4).
  - UI state maps to the style state `normal/focused/disabled`. Each state change re-applies the text colour and font.
  - **Select**: on `button_down` with `meta is_select_button` (and state ≠ disabled):
    1. Set the style state `selected` for `select_frames` (=10) frames, then fall back to `focused` (FUN_0012cfb0, run per poll).
    2. Send `SELECT_MSG(component, focus_panel, current_screen, button=6)`.
    3. Return handled.
  - Methods: `set_text token` (localized), `set_localized_text "raw"`, `get_text`, `set_state kX`, `set_showing`, `showing`.
- **LabelEx** (RndLabelEx, Load 0x11e9b8)
  - The style gives `(text <template>.txt)`, which is copied from `ghui::<template>` (FUN_0011efc0), plus a single `colors text_color` (not per state).
    Example label_serif_black → `label_serif_black.txt`, colour 0,0,0. The `default` style uses `label.txt`.
  - Record fields: fit (when TRUE, scale x = fit_width/text width and z = fit_height/text height, FUN_0011f868), leading, align, per-glyph jitter (seed + x/z/rot), caps (upper-case **[I]**), text token, wrap width.
  - `set_text tok` looks the token up in the locale table and **uses the token itself when it is missing** (FUN_0011db60).
  - `set_localized_text s` uses s verbatim. Also `set_wrap_width`.
- **PictureEx**: the style gives `mesh` (e.g. `pic_char_sel_metal.mesh`) and `tex_path "image"`.
  An optional `disabled_mesh` (e.g. the lock graphic `pic_char_sel_grim_lock.mesh`) is used when the component is kDisabled **[I]**.
  `set_tex_file "x.png"` loads `ghui/image/x`.
  Focus highlight is done by script (e.g. sel_character toggles `sc_char_<c>_focus.view`).
- **PictureToggle**: style `checkbox` = `checkbox_toggle.mesh`, with tex_off `checkbox_empty.png` and tex_on `checkbox_checked.png`. Methods: `toggle`, `set_state B`, `get_state`.
- **List** (RndScrollList; input FUN_00123150; scroll FUN_001261c8)
  - Style `song`: `element_view "song_element.view"`, `back_arrow_mesh song_back.mesh`, `next_arrow_mesh song_next.mesh`, `max_step 1.0` (scroll speed).
  - Colours: text_color .1, `highlight_text_color` per state (normal .1 / focused 0,0,1 / selected 0,1,0), highlight_mesh_color, `header_text_color` (.66,0,0).
  - Record fields: count (60), spacing (40), mode.
  - Input: DUp moves −1 and DDown +1 (scrolling). Select (when not mid-scroll) sends SELECT_MSG with the pressed button. The list returns unhandled, so screen and panel scripts still get the button.
  - Scroll modes:
    - mode 0: wraps modulo the count.
    - mode 1: moving highlight inside a window, clamped.
    - mode 2 (sel_song): clamped, with animated scroll. During the scroll, `scrolling` is TRUE and `scroll_percent` runs 0→1. It sends SCROLL_START_MSG at the start and SCROLL_MSG with `$done` at the end.
  - Methods: `set_provider p`, `selected_pos`, `set_selected_pos i`, `spaced_selected_pos` (position counting header rows; sel_song uses it as the frame of `sel_song.view`), `refresh`, `scrolling`, `scroll_percent`, `show_element`, `hide_element`.
- **Providers** (ListProvider vtable): `Text(i,col)`, `NumData`, `IsActive(i)`, header query. Script methods: `get_text i`, `get_symbol i`, `list_length`.
  - `song_provider` (SongProvider, ctor FUN_00118400) also has `set_quickplay B`/`get_quickplay` and `set_store_mode B`.
  - Data: in quickplay mode, all songs in `campaign.dta (order …)` order plus the store/bonus songs, **filtered to unlocked songs** unless an unlock-all flag is set (FUN_00137d58).
    In career mode, only the songs of the current venue tier (FUN_00137c18).
  - Song display text is `songs.dta (name "…")`; `get_song_text` and `get_song_artist_text` return `name` and `artist`.
  - Header rows: before the first song of each tier there is a header showing `localize song_header_<tier>` (e.g. "1. Opening Licks"; "Bonus Tracks" for `store`).
  - Stars column: "***" / "****" / "*****" (career result, FUN_00118cb8).
  - Footer hint after the last unlocked song: `song_quickplay` "Play career mode to unlock more songs", `song_current_venue` "Beat %i/5…", `song_last_venue` or `song_old_venue` (FUN_00118dd8).
  - Text is drawn with resources `song_name.txt`, `song_stars.txt`, `song_header.txt`, `song_score.txt` and `song_checkmark.mesh` **[column mapping I]**.
  - Other providers: `character_provider`, `store_guitar_provider`, `store_skin_provider`, `{game video_provider}`.
- **Slider** (SliderPanel `new_slider_panel` + RndSlider)
  - Style `char`: `slider_mesh char_slider.mesh`, `bar_mesh`, `pod_mesh`, `pod_anim char_slider.tnm` (frame ∝ position), `text char_slider.txt`, and per-state `text_color`/`mesh_color` (selected mesh green).
  - Behaviour: focus, then select enters "slider_selected" mode. In that mode up/down change `selected_pos` (sending SCROLL_START_MSG), fret1 confirms and fret2 cancels (GH_CANCEL_MSG).
  - Methods: `set_num_steps n`, `num_steps`, `selected_pos`, `set_selected_pos`; on the panel, `slider_selected`.
- **TextEntry**
  - Styles: `high_score` (label_hand_pen.txt, length 8) and `band_name` (entry_profile.txt, length 20). Both use characters `"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !?.'"`. Arrow meshes are `char_back.mesh`/`char_next.mesh`.
  - Behaviour from the helpbar logic in splash.dta: strum up/down cycles the current letter, fret1 moves to the next letter, fret2 deletes (or goes back when the entry is empty), and START accepts (locale `HOW_TO_END`). Accepting sends `TEXT_ENTRY_MSG`.
  - Methods: `get_text`, `set_text`, `is_done`, `no_text_entered`, `user_can_scroll`, `resume_input`.
- **Localization**: `ghui/eng/gen/locale.dtb`, decrypted in `extracted_dta/ghui/eng/gen/locale.dta` (1328 lines, about 600 entries).
  - Format: flat DTA pairs `(token "String")`. Tokens are case-sensitive symbols (`QUICK_PLAY`, `tutorials`, `help_back`). `\q` inside a string is `"`.
  - The table is built from the `(locale file…)` config entry (FUN_002389a8) and queried by FUN_00238ce0. Script `{localize tok}` returns the string; `memcard localize` is the same with a "Please add %s to ghui/eng/locale.dta" warning.
  - Example: main.gh button `main_quickspin.btn` has text token `QUICK_PLAY` → "QUICK PLAY".

## 3. Native objects and methods used by the main/quickplay/options/pause/endgame/loading/game/splash scripts

`meta` (MetaPanel, `new_metagame_panel meta (rnd_file "")`):
- Native methods:
  - `is_select_button $b $p`, `is_back_button $b $p` and `is_any_button $b $p` (see §4).
  - `play_movie file` (`videos/%s`, .pss; `cut_scene` uses `ghintro.pss`).
  - `print_current_screen`, `set_tool_movie`.
  - Plays metamusic: `config/synth.dta (metamusic (music meta_loop1a…g) (background_sfx (sequence meta_people) (min_delay_sec 10)(max_delay_sec 20)))`.
- Script-defined methods:
  - `set_defaults`.
  - `setup_quickplay`: main_screen=main, continue_screen=qp_selsong, win_screen=endgame, lose_screen=lose, mode=quickplay.
  - `setup_career`: main_screen=career, continue_screen=sel_song.
  - `setup_multiplayer`.
  - `get/set` keys: `mode`, `main_screen`, `continue_screen`, `win_screen`, `lose_screen`, `won_campaign`.

`game` (GameConfig, 0x113e18; PlayerConfig methods apply to player 0):
- `get_controller i` returns `guitar` or `joypad`.
- `is_missing_controller`, `is_missing_multi_controller`, `set_multiplayer B`, `multiplayer`.
- `set_difficulty kDifficultyX` (0–3 = easy/medium/hard/expert), `get_difficulty`, `get_difficulty_sym` (returns easy/medium/hard/expert, which are also locale tokens).
- `set_venue sym`, `get_venue`, `get_venue_index`, `is_venue_available`.
- `set_song sym`, `get_song`, `set_song_index i`, `get_song_index` (index into song_provider), `get_song_text`, `get_song_artist_text`.
- `set_quickplay` (FUN_001139e0): applies the song's `(quickplay (venue …)(character …)(guitar …)(guitar_skin …)?)` block from songs.dta.
  This is why **quickplay has no character/guitar/venue screens**.
- `set_career_song`, `set_career_venue`.
- `set_character`, `get_character`, `set_guitar`, `set_guitar_index`, `get_guitar_skin`, `get_guitar_desc`, `get_guitar_skin_desc`, `get_player_config i`, `video_provider`.
- On the game panel (GamePanel): `restart`, `set_paused B`, `get_bank_loader` (→ `reset`), `anim_task`, `delay_task`.

`campaign`:
- Profiles: `num_profiles`, `max_num_profiles`, `is_empty_profile i`, `profile_name i`, `profile_slot`, `set_profile_slot i`, `set_profile_name s`, `has_profile_name s editing`, `empty_slot`.
- Save state: `needs_save`, `save_complete`.
- Tutorials and unlocks: `tutorials_done`, `tutorial_access`, `is_unlocked sym`, `unlock_global_item sym`, `get_random_song`.
- Career progress: `new_campaign`, `last_difficulty`, `update_difficulty`, `get_status_progress diff`, `get_cur_status_sym`, `status`, `final_song`.
- Character info: `get/set_character_info`.
- Money and scores: `cash`, `career_score`, `finish_song score $venue $status` (out-params), `has_cash_award`, `next_cash_award $amt $reason`, `buy_item`, `beat_song`, `cheat_beat_song`, `foreach_venue $v {…}`.
- For quickplay only, it is enough to stub `num_profiles` (>0 skips profile creation), `profile_name`, `is_unlocked`→TRUE, `tutorials_done`, `tutorial_access`, `needs_save`→FALSE and `get_random_song`.

`ui`: `goto_screen`, `push_screen`, `pop_screen`, `screen`, `panel`, `current_screen`, `focus_panel`, `frame` (ms), `in_transition`.

`synth`: `play_sequence name [(volume v)(pan p)]`, `stop_all_sfx`. Global funcs: `play_sfx name [(volume)(pan)(stop B)]`, `stop_sfx`, `play_game_sfx`, `play_game_sequence`.
Names used: `meta_lick button_select button_back button_toggle button_play button_error cash_register cash_award postshow_loop beat_easy beat_other beat_venue crowd_win flush slider_band slider_gtr slider_sfx meter_slide sp_available`.
Stubbing these as no-ops is acceptable.

Global script funcs:
- `meta_music_start`, `meta_music_stop` (FUN_0020dcd8).
- `song_preview sym|''`: loops the song's `(preview start_ms end_ms)` (FUN_00151fd8).
- `turn_off_ambient`, `reset_ambient`, `animate_forever_30fps panel obj`.
- `autosave_goto S`: when `campaign needs_save && $autosave && !$disable_save`, goes to the `autosave` screen and then S; otherwise goes to S.
- `meta_loading_goto S`: sets `meta_loading.next_screen` and goes to the `meta_loading` screen, which does `autosave_goto next` on TRANSITION_COMPLETE.
- `goto_endgame_complete`.

Other objects:
- `options`: `get/set_band_volume`, `get/set_guitar_volume`, `get/set_fx_volume` (0..1), `get/set_stereo`, `get/set_lefty p`.
- `tips random_tip`.
- `tutorial is_tutorial_running`, `tutorial set_tutorial_running B`.
- `app set_paused B`, `app set_compact_heap B` (no-op).
- `fade start_fade ms (color r g b) (fade_synth B) (fade_out B)`.
- `hud …`, `track_panel …`, `arena …`, `arena_panel start/set_paused`, `mtv_overlay show_overlay B`, `crowd_audio turn_off`.
- `player0::player score`, `percent_complete`, `percent_hit`, `longest_streak`, `num_stars`, `star_rating`.
- `highscores check_highscore score`, `get_highscore slot $name $score`, `add name score`.
- `memcard get_info`, `load_data obj`, `save_data obj overwrite`, `localize` (stub success: send MEMCARD_RESULT_MSG kMCNoError).
- `dialog setup b1 b2 default msg title`, `dialog message msg title`.

## 4. Controller mapping (MetaPanel_v1 0x14cfb8 → FUN_0011dbe8/0011dbb0/0011dc40)

Button numbers (`kPad_*`): L2 0, R2 1, L1 2, R1 3, Tri 4, Circle 5, X 6, Square 7, Select 8, L3 9, R3 10, Start 11, DUp 12, DRight 13, DDown 14, DLeft 15.
A pad counts as a guitar when FUN_002218e0 is true (pad type flag).

| action | guitar | joypad |
|---|---|---|
| select / continue ("fret1") | R2 (green fret) | X or kPad_Select(8) |
| back ("fret2") | Circle (red fret) | Triangle |
| any button (splash) | any except L2 | any |
| navigate | strum = DUp/DDown | D-pad (Left/Right for horizontal navigators) |
| pause / resume in game | Start | Start |

- Guitar fret slots (`beatmatch_controller.dta`): green R2, red Circle, yellow Tri, blue X, orange Square.
- Lefty flip (Options FUN_00107768): for a guitar player with lefty on, DUp and DDown are swapped before the UI sees them.
- Key repeat while a button is held: hold 1000 ms, then repeat every 80 ms (gh.dta `joypad`). Panels can override this; sel_song uses 500/80.
- Some screens test raw buttons:
  - `kPad_Start` in game/pause.
  - DUp/DDown on store and sel_venue.
  - The `is_updown_button` func on sel_song (stops the preview while scrolling and restarts it on button_up).

## 5. Screen flow

**Boot** (`$first_screen`):
1. `bootup_load`: memcard dialog. On success, wait 1 s.
2. `cut_scene`: plays `ghintro.pss` on TRANSITION_COMPLETE.
3. `splash` (panels meta, splash; press any button):
   - After 60 s idle (`DELAY_BEFORE_ATTRACT_MS`), it enters **attract mode**: `meta setup_quickplay`, `game set_song {campaign get_random_song}`, expert, `game set_quickplay`, `$attract_mode TRUE`, then `loading` → `game`.
     In attract mode, player 0 auto-plays. After 120 s or any button, it fades for 2 s, goes to `finish_fade`, then `meta_loading_goto splash`.
   - On a button press:
     - If profiles exist → `chooseprof` (pick a band slot: an existing one → `main`, an empty one → `nameprof`).
     - Otherwise → `guitar_help` → `nameprof` → TEXT_ENTRY → `autosave_goto main`.
- For a port with no memory card, start at `cut_scene` or `splash`, and treat the profile as existing.

**Main** (screen `main`: panels meta main helpbar char_multi; focus main; helpbar fret1 CONTINUE, strum UP/DOWN):
- Vertical navigator, top to bottom: career, quickspin, multiplayer, tutorial, options. Focus starts on `main_career.btn`.
- On first entry it plays `meta_lick`. If the controller is a guitar and no tutorial has been done, focus starts on the tutorial button.
- Poll:
  - Multiplayer is disabled when the 2nd controller is missing.
  - Tutorial is disabled when `!tutorial_access` and the controller is a joypad.
- `reset_player_settings` on enter: medium, venue basement, song 0, character metal, guitar 0.
- SELECT targets:

  | button | goes to |
  |---|---|
  | career | `sel_difficulty` |
  | quickspin | `qp_selsong` |
  | multiplayer | `multi_sel_character` |
  | tutorial | `tutorials` |
  | options | `options` |

  `main` has no back target, because chooseprof/nameprof have `allow_back FALSE`.

**Quickplay**:
1. `qp_selsong` (panels meta sel_song helpbar):
   - Panel sel_song enter: `song_provider set_quickplay TRUE`, `ss_song.lst set_provider song_provider`, select `game get_song_index`, stop metamusic and start `song_preview`.
   - SCROLL_START sets `game set_song_index`.
   - The background view `sel_song.view` frame follows `spaced_selected_pos`, interpolated by `scroll_percent`.
   - SELECT → `meta setup_quickplay` → `qp_diff`.
   - Back (Circle/Tri) → auto-back to `main`.
2. `qp_diff` (panels meta sel_difficulty helpbar):
   - Vertical navigator: `sd_diff1..4.btn` (easy..expert, wraps). Focus starts on the current difficulty.
   - SELECT runs `game set_quickplay`, `set_multiplayer FALSE` and goes to `loading`, then the panel sets the difficulty.
   - Back → `qp_selsong`.
3. `loading` (panels loading; no anim): its enter runs `reset_ambient`, `stop_all_sfx`, `bank_loader reset`, `meta set won_campaign FALSE`, then **immediately** `goto_screen game`.
   The goto completes once the game panels have loaded, so the loading panel (tip label `tips random_tip` and an animated poster) shows during the load.
4. `game` (panels game arena track hud mtv_overlay fade):
   - Start → `pause`. A missing controller → `pause_controller`.
   - At the end (arena_game.dta `game_outro`):
     - Won: `post_show` → `{meta get win_screen}` (= `endgame` in quickplay; career final song → win_easy/win_game).
     - Lost: `{meta get lose_screen}` = `lose`.
5. `pause` (GAME_PANELS + pause; `in_game`, no back, no anim):
   - Enter: `game set_paused TRUE`, `app set_paused TRUE`. Navigator: resume / restart / options / quit.
   - resume or Start: `$game_unpause_on_enter TRUE` → `game`, which unpauses on TRANSITION_COMPLETE.
   - restart: `game restart` → `game`.
   - options → `pause_settings` (sliders; back → `pause`).
   - quit: `meta_loading_goto {meta get continue_screen}` (= `qp_selsong`).
6. `lose`: shows song, difficulty and "%d%% complete". restart → game; selsong → `meta_loading_goto continue_screen`; quit → `meta_loading_goto main_screen`.
7. `endgame` (results):
   - Fields: song text, difficulty, score, `eg_percent_format` "%d%%", streak, star rating, and a random newspaper headline (ADJS/VERBS tables by stars−3).
   - Select → career: `status`/`cashaward`/complete. Quickplay: `highscore`.
8. `highscore`:
   - If the score made the table, a TextEntry `hs_entry<i>.ten` takes the name. Then the flush anim plays and after 7 s it calls `goto_endgame_complete`. Otherwise select continues.
   - `goto_endgame_complete` → `complete` (navigator selsong / restart / quit). selsong → `meta_loading_goto continue_screen` (qp_selsong); quit → main.

**Options**:
- `options` (navigator game/data/bonus/credits).
- `game_settings` (sliders band, guitar, sfx with 12 steps; toggles for stereo, lefty p1 and p2). On back it calls `set_volumes`, then `autosave_goto options`.
- `data_settings` (profile operations, memcard).
- `bonus_material` (videos).
- `credits`.

**Career-only screens** (not used by quickplay): `sel_difficulty`(career) → `sel_character_new` → `sel_guitar` → `career` → `sel_venue` (wrap FALSE) → `sel_song`.
`sel_character_edit`, `sel_guitar_edit` and `store` are reached from `career`.
