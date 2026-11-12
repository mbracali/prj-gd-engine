# Guitar Hero: Metallica (Xbox 360): front-end audit

Screen-by-screen state of gd10's menus against the original, from the
disc's scripts and descs and the XEX (no emulator). Each screen: **matches**
/ **differs** / **missing**. Checked on the walks' screenshots
(`GD10_WALK_SHOTS=<dir> gd10 --walktest menus|quickplay <iso>`, every
reached screen saved just before the walk's first button; `GD10_UIDUMP=walk`
logs each screen's element tree). [C] = confirmed on the disc data or at an
XEX address; [I] = inferred.

## Engine rules found (all screens)

- Fonts: gh4's header is f32 `+0 ? (0.0), +4 vShift, +8 bearing, +0xC
  tracking, +0x10 line height`; glyph `+0x1C x offset, +0x20 advance
  adjust` are f32 [C 0x824D11B0 width, 0x82454C78 draw]. font_spacing, when
  given, replaces tracking and drops bearing [C 0x824D11B0].
- The menu fonts (fontgrid_text_a6, fontgrid_title_a1, buttons_x360, ...)
  live in `PAK/ui_shared.pak`; fontgrid_text_a8 in `ZONES/global.pak` [C data].
- A descinterface element wraps its desc: the desc's root is child 0; without
  dims it covers its parent top-left, with dims the call's just places it
  (D14). A desc's z_priorities sit on the nearest z given above it (the help
  bar, z 100000, over the screens' art).
- Script params: included structs nest (add_menu_frontend_item's
  `choose_state` reached generic_event_choose two levels down) [I].
- Start in a song: root_window's pad_start handler handle_pause_event, which
  opens uistate_pausemenu (with from_handler) [C scripts].
- ui_event_exists_in_stack needs `stack_size` from menustack_getstackcontents
  [C script].
- `setprops` / `SetScreenElementProps` / `se_setprops` reach a desc's named
  props; `se_getprops` without names returns all of them [C scripts].
- Text width: a glyph adds width + advance adjust - x offset - bearing +
  tracking, a space its width - bearing + tracking; font_spacing other
  than -1 replaces the tracking and drops the bearing [C 0x824D11B0].
- Text blocks: fit_width (wrap, expand dims, scale each line if larger / to
  fit), fit_height (clip bottom / top lines, expand dims, scale down if
  larger, scale to fit), scale_mode (per axis: a squeezed line keeps its
  height), text_case [C data values; I rules]. A block's lines are its
  children (dims = the line's text, scale = internal_scale x fit):
  ui_create_setlist_spawned puts the difficulty icon after the title and
  ui_options_cheats_focus sizes the highlight from them [C scripts; I].
- `summed` positions: the pos point on screen (parent's corner + anchor +
  pos), cached by the element's update [C 0x823F5AA8 reads +0x1B0,
  0x82375E08 writes it]; an element not laid out yet gives (0, 0) [I: the
  set list rows read their new title so, in the row's own frame].
- Descs: `se_setprops desc = ...` swaps the desc (the focused song's info
  panel); a descinterface inside a desc builds its own desc (results rows,
  set list stars); `autosizedims = true` takes the desc's rect size, the
  rect's corner on the element's top-left; a prop name on several elements
  sets all (new_cheat_alpha); `flip_h = true` / `flip_v = true` props
  [C data; I: the rect shift].
- Window sizes: the menus are 1280 x 720 units letterboxed into the window
  (16:9 as the 360; 4:3 and ultrawide get bars), so placement is the same
  at every size (checked 1280x720, 1920x1080, 1024x768, 2560x1080,
  3440x1440).
- Menus: fit_major / fit_minor (size dims to content, fit content [if
  larger], expand if content larger, keep dims) [C data; I rules]; children
  placed by their pos_anchor.
- `GetScreenElementPosition ... summed` (on screen) and `pos = {absolute
  (x, y)}` (screen to parent-local) [C scripts mainmenu_focus etc.; I].
- `scrollingmenu` = GH3's VScrollingMenu, with adjust_visibility,
  center_selection, top_selection [C data uidesc_setlist_b; I rules].
- `meta_button_map` is an array of {name map} (guitar, drum, pad) [C data].

## Screens (flow order)

Counts: 22 screens looked at; 18 match (no difference seen), 2 differ, 2
not reached; sub-screens of Backstage, Settings and Music Studio below the
first level are not audited yet.

| Screen | State | Matches | Differs / missing |
|---|---|---|---|
| Legal, boot movies | UIstate_boot_* | movies, skip with A | - |
| Press start | uistate_boot_iis | art, logo, text | - |
| Drum tip pop-up | uistate_optimal_drum | only with an RB drum kit (isRBDrum false) | - |
| Main menu | uistate_mainmenu | art, seven items (menu fonts), highlight, help bar, GAMEPADS pill | Xbox LIVE greyed (port: no online); item size vs logo unconfirmed |
| Game mode | uistate_game_mode | art, items, highlight, help bar | - |
| Band (game mode) | UIstate_generic_alert_popup | "two controllers" warning as the original | - |
| Instrument | uistate_select_instrument | art, items, highlight | - |
| Head to Head | uistate_select_controller | art, help bar, Back | controller icons not checked |
| Backstage | uistate_backstage | badge, typewriter items, highlight, help bar | - |
| Practice (Backstage) | uistate_select_instrument | reached | - |
| Tutorials (game mode > TRAINING) | uistate_select_tutorial | reached with focus; TRAINING's choose blocks the menu input until the tutorials list is up (SetButtonEventMappings), a second press no longer pushes the screen twice; Back (tutorials_event_back) goes back | list not audited |
| Settings | uistate_options | items, highlight, help bar + ENHANCED | - |
| Cheats | uistate_options_cheats | title, list, highlight on the focused item, snakes, scroll bar (the cheat bone hides: new_cheat_alpha) | - |
| Music Studio | uistate_jam | sign-in, memcard, download scan, jam | jam screen itself not audited (band characters missing: CAS) |
| Set list | uistate_setlist | songs (squeezed to 630 per axis), icons after each title, focused song's info panel (difficulty skulls, stars), sections, fade under the help bar (subtract gradient), custom set circles, scroll, help bar, the focused song's preview (music_setlist, D81) | - |
| Difficulty pop-up | uistate_select_difficulty | frame, five items, icons, title, the scribble highlight on the focused item (ui_setlist_diff_focus / ui_setlist_scribbles, D80) | - |
| Loading | create_loading_screen | tip, picture | - |
| Gameplay pause | uistate_pausemenu | PAUSED, six items 35 apart (selection_height 50, spacing_between -15 [C script]), focus colours, Back resumes | - |
| Results | uistate_song_breakdown | title, stars, score, highest multiplier, player row (rank, cut, icons), help bar | name shows the desc's placeholder (no gamertag), notes N/A and streak 0 (detailed stats); black sides (venue not drawn behind) |
| Cash rewards | uistate_cash_rewards_3 | reward rows (check, amount, description), gig / career totals, rank, logo | "Rock Rank" (367 wide in 350) wraps and scales down, its labels too: as the measure says [C 0x824D11B0], unconfirmed on the original |
| Credits | - | not reached | - |
| Extras | uistate_extras | reached (state posts now) | not looked at |

## Walks

- `--walktest menus` (every main menu item, its focused item, Back to the
  main menu; `GD10_WALK_MENUS=<digits>` picks items): passes.
- `--walktest quickplay`: passes (boot to the results and back to the set list).
- `--walktest qp_pause`: passes (Esc pauses, Back resumes, the pause items).
- `--walktest fuzz` (300 seeded random presses, `GD10_SEED`; a menu must
  keep the focus with text shown, never 8 s without; no song, tutorial,
  practice song or jam session starts, no character is made: CAS builds
  are not in the port yet): `tools/fuzz_seeds.sh <gd10> <iso>` runs the
  regression seeds (default, 7, 99 and the seeds that found the bugs
  below); 41 seeds were run (default and the odd seeds 1..79; GD10_SEED
  n and n|1 are the same walk).
- Leaving screens after the fuzz (`backto:`): the cheat entry leaves with
  Start (B is its red fret), pop-ups without a pad_back (LIVE sign-in
  warning, set list quit warning) with their second item, the set and
  double bass pop-ups and the generic alert (Back is nullscript) with A,
  as the original.
- Fixed from the fuzz walk (GD10-020): tutorials pushed twice and left
  unfocused (block_menu_input, spawn instance limits: D20); GHTunes' Sign
  In waited forever for the LIVE blade (D21); practice's section Back
  waited on GetContentFolderState; the extras' lyrics never took Back
  (IsObjectScript; BroadcastEvent did not end a Block on song_started).
