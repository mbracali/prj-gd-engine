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
- Text blocks: fit_width (wrap, expand dims, scale each line if larger / to
  fit), fit_height (clip bottom / top lines, expand dims, scale down if
  larger, scale to fit), scale_mode, text_case [C data values; I rules].
- Menus: fit_major / fit_minor (size dims to content, fit content [if
  larger], expand if content larger, keep dims) [C data; I rules]; children
  placed by their pos_anchor.
- `GetScreenElementPosition ... summed` (on screen) and `pos = {absolute
  (x, y)}` (screen to parent-local) [C scripts mainmenu_focus etc.; I].
- `scrollingmenu` = GH3's VScrollingMenu, with adjust_visibility,
  center_selection, top_selection [C data uidesc_setlist_b; I rules].
- `meta_button_map` is an array of {name map} (guitar, drum, pad) [C data].

## Screens (flow order)

Counts: 21 screens looked at; 14 match (no difference seen), 5 differ, 2
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
| Settings | uistate_options | items, highlight, help bar + ENHANCED | - |
| Cheats | uistate_options_cheats | title, bone slots, snakes, scroll bar | cheat list box empty |
| Music Studio | uistate_jam | sign-in, memcard, download scan, jam | jam screen itself not audited (band characters missing: CAS) |
| Set list | uistate_setlist | songs, sections, scroll, stars, difficulty icons, help bar | long titles squeezed per line (fit rule [I]); help bar over the last row |
| Difficulty pop-up | uistate_select_difficulty | frame, five items, icons, title | - |
| Loading | create_loading_screen | tip, picture | - |
| Gameplay pause | uistate_pausemenu | PAUSED, six items, focus colours, Back resumes | item spacing tight |
| Results | uistate_song_breakdown | title, stars, score, multiplier, streak table, help bar | highest multiplier reads 1x on autoplay; black sides (venue) |
| Cash rewards | uistate_cash_rewards_3 | milestones, gig / career totals, rank, logo | Rock Rank title wraps small; milestone descriptions clipped |
| Credits | - | not reached | - |
| Extras | uistate_extras | reached (state posts now) | not looked at |

## Walks

- `--walktest menus` (every main menu item, its focused item, Back to the
  main menu; `GD10_WALK_MENUS=<digits>` picks items): passes.
- `--walktest quickplay`: passes (boot to the results and back to the set list).
- `--walktest qp_pause`: passes (Esc pauses, Back resumes, the pause items).
- `--walktest fuzz` (300 seeded random presses, `GD10_SEED`; a menu must
  keep the focus with text shown, never 8 s without): passes on seeds 7, 99.
