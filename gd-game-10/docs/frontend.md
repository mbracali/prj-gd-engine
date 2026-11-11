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
- A descinterface element wraps its desc: the desc's root is child 0 (D14).
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

| Screen | State | Matches | Differs / missing |
|---|---|---|---|
| Legal, boot movies | UIstate_boot_* | movies, skip with A | - |
| Press start | uistate_boot_iis | art, logo, text | - |
| Drum tip pop-up | uistate_optimal_drum | only with an RB drum kit (isRBDrum false) | - |
| Main menu | uistate_mainmenu | art, seven items, highlight scribble, help bar pills | item size vs logo unconfirmed |
| Career / Quickplay game mode | uistate_game_mode | art (hand, jaw, skull), items, highlight | - |
| Instrument | uistate_select_instrument | as game mode | - |
| Head to Head | uistate_select_controller | art, Back works | help bar partly behind the art |
| Xbox LIVE | uistate_signin_warning | warning pop-up text | port: no online (shows the original's not-signed-in warning) |
| Backstage | uistate_backstage | badge art, items (typewriter font), highlight | sub-screens not audited |
| Settings | uistate_options | items, highlight | sub-screens (cheats, save/load, calibrate, audio, functional) not audited |
| Music Studio | uistate_jam | sign-in -> memcard -> download scan -> jam | jam screen not audited |
| Set list | uistate_setlist | songs, sections, scroll bar, help bar | scrolling position being fixed |
| Difficulty pop-up | uistate_select_difficulty | frame, five items, icons | - |
| Loading | create_loading_screen | tip, picture | - |
| Results | uistate_song_breakdown | title, stars, score, multiplier, streak table | background behind the sheet black (venue) |
| Cash rewards | uistate_cash_rewards_3 | milestones, gig / career totals, rank | milestone descriptions clipped |
| Pause | uistate_pausemenu | not audited | - |
| Credits, Extras | - | not audited | - |
