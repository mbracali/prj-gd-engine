# GH2's front end: scripts, UI objects, screens, saves and videos

How gd02 runs Guitar Hero II's own menus (M7): the game's `ui/*.dtb` scripts
on gd01's interpreter, the UI classes of the panel milos, the screen manager,
the save, and the disc's `.wmv` videos. Addresses are in `default.xex`
(`ghidra/analysis/`); **[C]** confirmed in code, **[I]** inferred.

## Script interpreter (`engine/src/script/`)

gd01's interpreter, with GH2's differences:

- `#merge` merges arrays into existing ones (`ScriptLoader::mergeArrays`);
  `#include` paths use backslashes; `HX_XBOX` and `_SHIP` are defined.
- `{new Class name (defs)}` builds an object through a registered class
  factory; the definition's arrays become handlers and property defaults.
  An unknown class logs UNKNOWN and gets a generic object.
- `handle` sends quietly; send order is script handler → natives → generic
  get / set / has / name → unhandled.
- Tasks run on units: seconds, ui_seconds, beats, tutorial seconds. The host
  sets each clock every frame (the song clock stops while paused).
- Each file is loaded with its own loader seeded with the boot macros
  (macros leaked between files otherwise).
- L1 (gd01's lesson): every unhandled message, stub, unknown class and
  script error is counted and logged (`GD02_UILOG=1`).

`--scripttest`: all 177 `.dtb` load, boot builds 251 `new` objects with no
UNKNOWN / UNHANDLED, 740 handlers smoke-run, 44 unit checks.

## UI objects and text (`scene/ui_objects`, `scene/ui_styles`, `render/text_layout`, `render/ui_view`)

- Typed classes: UIComponent (`nav_right`, `nav_down`), UILabel, UIButton,
  BandLabel v12, BandButton v10, UIList v3, CheckBox, UIPicture, UIProxy,
  BandSlider v2, BandTextEntry v3, UITrigger, ScreenMask v2, BandPlacer.
- Text chain: the component's type in `ui_objects.dta` → its
  `resource_file` milo → the template Text / Font; 649 chains resolve.
  Locales eng / fre / deu / ita / esl.
- Button colours come from the panel's normal / focus / disabled /
  selecting colours; the scale from `button.tnm` frames 1 / 2 / 3.

## Screens and panels (`engine/src/ui/`)

- `ui_manager`: screens, panels, transitions (0.5 s out, 0.5 s in [I]),
  focus, button routing, key repeat (reset while a transition runs, as in
  gd01), the help bar. `script_classes`: the
  `new` classes. `natives_*`: the game objects scripts talk to (game,
  gamecfg, player configs, beatmatch, track_panel / hud, campaign,
  song_provider, memcard, profilemgr, options, highscores, tips, synth,
  meta_music).
- Boot: splashes → `intro.wmv` → title / attract → storage (memory card
  emulation) → guitar help → main menu.
- Quick play: song list → difficulty → loading → game screen (the M6
  highway and HUD) → post show → endgame (Daily Dose) → high scores →
  complete. Pause (resume / restart / quit), lose, options, credits, bonus
  videos.

### Controls in the menus

The input layer (`input/input.*`, gd01's) gives actions; `UiManager::action`
turns them into GH2's kPad buttons, the way gd01's `App::uiButton` does:

- Every controller's green fret is kPad_X (select) and its red fret kPad_Tri
  (back): a guitar's, the keyboard's 1 / 2, a joypad's LT / LB (with gamepad
  play on). The guitars' frets are the 360 guitar's A B Y X LB
  (`hx_guitar_xbox_new` [C]), so yellow / blue / orange are kPad_Circle /
  kPad_Square / kPad_L1 (the cheats' codes); a joypad's yellow / blue /
  orange (RB, RT, A) are not menu buttons, since its A already selects (it
  sent select and kPad_L1 at once before), but they still enter the codes.
- Start is kPad_Start; Star Power (Back / Select) is kPad_Select; strum and
  the D-pad move. Enter and Esc always select / go back; Enter strums in a
  song.
- In a song, Back (Esc, a joypad's B) pauses like Start (gd01); game_screen
  pauses on kPad_Start, pause_screen resumes on it (`pause.dta`).
- Esc three times in a row on the main menu quits (gd01).
- The main menu's yellow fret (any controller's; a joypad's RB, and its
  yellow Y button) opens GAMEPADS, gd01's main-menu item (the port's; the
  help bar shows a yellow GAMEPADS item before the strum bar). GH2 enters its
  cheat codes there with the same frets, so it opens once no other button
  follows within the codes' 750 ms; a completed code or any other button
  cancels it (`UiManager::yellowButton`). Clicking the help item opens it at
  once. GAMEPADS' back returns to the main menu.
- The mouse (gd01's, `guidelines/universal_launcher.md` at the repository root):
  every panel's view records where it drew each button, list row, slider,
  check box, picture and text entry last frame, projected to menu units
  (`gfx::UiView::hits`). `PanelObj::mouse` focuses the component under the
  pointer, the nearest centre first, as the pad would (`component_focus`, the
  help bar updates). Only components the navigation reaches count. A list
  takes clicks only: the selection moves to the row with Up / Down presses,
  then select. A click selects. A screen with nothing to choose from
  continues on a click. The help bar's items act as their buttons. A right
  click is back (kPad_Tri) and the wheel is Up / Down. The port's own screens
  test their own rows. The pointer shows while it moves (only while the
  window has focus), hides 3 s later, on a key or button press and during a
  song (`App::updateCursor`). Enhanced Options > Enable Mouse Selection turns
  it off. Touch taps are clicks.
- The input layer (`Input`, gd01's) never presses by itself:
  - A D-pad that SDL's mapping reads from an axis (`dpleft:-a6`, common for
    adapters and guitars) is judged against that axis's rest, read as the
    controller connects and for 300 ms after, with hysteresis (half way to
    press, a third to release). SDL's own test is the middle of the range,
    whatever the rest. An axis resting off centre held left (or chattered)
    for good, and the menus' key repeat (hold 1000 ms, repeat 80 ms) moved
    sideways by itself.
  - A button held as the controller connects counts once released.
  - A controller that goes away releases what it held.
  - The menu controls' rebinding takes buttons and keys only.
- Two players (M10.2, `docs/multiplayer.md`): each controller is a pad slot;
  after the controller select a pad's buttons are its player's. Without a
  second controller the keyboard splits: player 2 on H J K L ; (frets), N / M
  (strum), B (Star Power), O (Start).
- The boot splashes skip on select, back, Start or a click / tap (the port;
  the original's can't be skipped [I]); the videos skip on Start, select,
  back or green (gd01's set; the original: Start or A [C]).

### Metacam (transitions)

GH2 loads one `ui/metacam.milo` (0x822A1070) that every panel's camera and
bricks hang from. A transition plays one of its groups over 0.5 s
(0x8229FB20): `trans_out_forward` / `trans_out_backward` leaving a screen,
`trans_in_forward` / `trans_in_backward` entering one; when no transition
runs, `trans_in_forward.grp` is set to its end, the resting pose
(0x8229FC98) [C]. The port's panel views each pose their own copy, so
`UiManager::setMetacamFrame` sets every loaded view, a view made later
replays the groups since the last reset, and `resetMetacam` runs after every
transition. (Before, only the outgoing / incoming panels were posed and
nothing was reset: going back, the main menu kept its forward exit's camera
under the backward entrance and came back off to one side.)

The backward groups hold AnimFilters on the forward TransAnims
(`in_backward.filt` -> `meta_cam_right_out.tnm`, `out_backward.filt` ->
`meta_cam_right_in.tnm`) with start 500, end 0. RndAnimFilter (Scale
0x822078D8, SetFrame 0x822079B0, Start / EndFrame 0x82207AF8 / 0x82207B80)
[C]: without a period the scale is negated when end < start, and the frame
is scale x frame + offset + (start - end), kept in the range: those filters
play the forward moves backwards. Before bug-fix 4 the port ignored the
reversal, so going back played the forward exit / entrance again.

Panel triggers: UIPanel Enter (0x82373EB8 -> 0x823737F0) sends `ui_enter`
and then `ui_enter_forward` or `ui_enter_back` (the transition's direction),
Exit (0x82374010) `ui_exit` and `ui_exit_forward` / `ui_exit_back` [C]. The
data uses the directional ones only in `meta_bricks.milo` /
`meta_bricks_on.milo`: `meta_light_off.mnm` on ui_exit_back,
`meta_light_on.mnm` on ui_enter_back. meta_bricks is a subdirectory of
main, options, training and most menus, and GH2 loads it once (ObjDirPtr
reuses a loaded directory), so its light is one state for all of them; the
port's views pose their own copies, so `UiManager::setViewFrame` gives an
animation of a shared subdirectory to every view showing the same object
(and to views made later). Before, options or training left with Back kept
their copy dark and came back darker (`--walktest back` checks the bricks'
light).

### Confirmed

- Quick play list (`0x82287600` / `0x822C5A68`): the first two tiers' songs
  (encores included), plus every song a band has unlocked (bought songs
  too) and downloads; a fresh save shows 12. Rows show no score, stars or
  check (`0x822C3428` / `0x822C34E8`); the last tier song carries the
  `song_quickplay` blurb (`0x822C0070`); headers use `song_header_%s`
  (`0x822BFFF0`). The career list: docs/career_and_store.md.
- Screens (UIManager 0x8236B8B0): `push_screen` (0x8236B800) enters a
  screen over the current one, which is not exited; `pop_screen`
  (0x8236AE40) returns to it as it was. `set_focus_panel` changes a
  screen's focus panel until it enters again.
- Navigation (`0x82373A08` / `0x82374160`): nav_down / nav_right and their
  inverses, skipping disabled components; disabling moves focus on
  (`0x82374500`).
- A button selects only on kPad_X while focused (`0x823704F8`);
  `select_frames` 30.
- Back (`0x8229EED0`): kPad_Tri after the focus panel declines, to the
  screen we came from if it allows back (`0x822A0408`).
- Text entry (`0x821433B0`): X adds, Tri removes, up / down picks, Start
  finishes.
- High score defaults (`0x822A4988`): 5 per song and difficulty, 50000 down
  to 10000, names from `highscore_dummy_N`.
- Headline tokens `0x82291428`; `is_any_button` = buttons below 16.
- Credits rows `0x8229B590`, music `0x8229B310`. Help bar `0x8229A8C0` /
  `0x8229A818` / `0x82299CD8` (partly).

### Inferred

- Splashes run 3 s and can't be skipped (the port skips them, above); tier headers sit in the first
  song's row; a panel's tasks end when it exits (screens keep theirs);
  screens pass unknown messages to their focus panel; a panel without
  `(focus …)` focuses its first focusable component.
- Menu music starts with MetaPanel; loops at −6 dB; previews fade 1 s.
- Results: `star_rating` as "***__", `avg_multiplier` = score ÷ base score;
  game_won fires at the chart's end with a 6 s outro, game_lost 3 s;
  `fast_intro` cuts the lead-in to 1 s.
- Volume indices default to 11 (full).
- The endgame photo is one of the player's outfit's four pictures
  (`photo_<outfit><0..3>_keep.bmp`, picked as the song loads, GamePanel
  0x822CC730 / 0x822CC9B0) on `winner_photo0.tex` [C]; see docs/band.md
  "Characters, guitars and photos".

## Save (`ui/career.*`)

GH2's Campaign::Save (`0x822869A0`, version 17, read back by `0x82287B00`)
[C]: eight band profiles, high scores, bought / unlocked items, options
(`0x822B0BC0`: lefty P1 / P2, volume indices, sync offset). gd02 keeps the
same data as `career.json` (format `gd02-save`) in the save folder, written
atomically with `career.json.bak`. Every setter marks the save dirty (L12).
Each profile also keeps per difficulty the band's look (character, outfit,
guitar, skin; the four symbols 0x822A8D00 saves) and its last difficulty
(+0x2c); the band's name (set_profile_name) and cash (`add_cash`,
`starting_cash` 50 from campaign.dta) [C]; `career_score` (each song's
best score at any difficulty, summed, 0x82287310) [C]. The character /
guitar screens: docs/band.md; the tours, cash and store items:
docs/career_and_store.md.

The Glitches (the port's, as gd01's): every save has a band named "The
Glitches" (`Career::ensureGlitches`): a fresh save in its last slot, a save
without one in its last free slot (when all eight are taken it is not
added), each time the save is read. It is a band like any other, $50 to
start, with GH2's default look at every difficulty (main.dta's
reset_player_settings: punk1, the first guitar and skin), so its career
starts at the venue list.

## Port additions

### Options and Enhanced Options

- Options keeps GH2's six entries in their order (Audio Settings, Video
  Settings, Manage Band, Data Settings, Bonus Videos, Credits) and adds the
  port's **Enhanced Options** after Video Settings: a copy of Manage Band's
  button (the flattest), and the seven share the six's column down to a row
  below Credits, scaled to the closer rows (`UiManager::portPanelLoaded`).
  Options shows GH2's own help bar (its `(helpbar ...)`).
- Audio Settings' help bar: audio_settings_panel shows it from its
  FOCUS_MSG (`update_help_display`); the port now sends a panel's own
  component_focus for the focus it enters with [I: UIPanel Enter sets the
  focus, 0x82376140]. Its poster (game_settings.milo, 40 units higher than
  Video Settings' and taller) had its top and top tape above the picture:
  the port lowers the poster, tapes, title and sliders by 24 units [I: not
  compared with a capture].
- **Enhanced Options** (`ui/enhanced_panel.*`, screen
  `enhanced_options_screen` in `ui/port_screens.cpp`): gd01's Enhanced
  Settings list in its sections and order, drawn in GH2's menu font
  (helveticablack) and colours (gold, white focus, the posters' red-orange
  titles) on a dark glass box, with the focused setting's explanation
  bottom right and GH2's help bar (select, back, up / down). Select (or
  left / right) changes the focused setting; a guitar needs only the frets
  and the strum bar. Its layout, widgets and flow are gd01's Enhanced
  Settings: collapsible sections (Steam Deck starts collapsed), separators,
  check boxes, dropdowns, framed buttons, the warning sign on rows not built
  yet, the explanation box bottom right, the mouse, and the page sliding with
  the transition (`ui/port_draw.*`;
  `guidelines/universal_launcher.md` at the repository root). The names keep "AND",
  since helveticablack has no `&`. Working: Gamepads (opens the GAMEPADS
  screen), Enable Mouse Selection, Screen Mode (dropdown: windowed / full
  screen / no border), In-Game Max FPS (dropdown: 60 / 120 / 240), Show FPS
  Counter (check box), and Internal Resolution (dropdown: the window's own,
  448p (ps2), 480p, 720p (x360), 800p ... 4K). The PS2's GH2 drew 640 ×
  448 (gd01's docs); the 360's draws 1280 × 720 (0x823032C8 sets the back
  buffer 720 tall, 1280 or 960 wide [C]). Every other entry is greyed, "NOT
  AVAILABLE YET", and cannot be chosen. Values are saved in
  `enhanced.json` (format `gd02-enhanced`) next to `career.json` and applied
  at once (`App::applyEnhanced`); Internal Resolution draws every frame
  into an off-screen target of that height and scales it to the window as
  it is presented (`RenderDevice::setInternalHeight`).
- The still backdrop (`App::menuBackdrop`, gd01's posed band): the second
  venue (small1) loads when the screen enters. Its band runs 45 fixed 33 ms
  steps (1.5 s), from a quarter into the first song of its tier, silent, under
  the venue's chorus light preset, and then nothing updates again. The camera
  stands in front of the stage (from the drummer towards the singer), on the
  singer's chest, far enough for the guitarist and the drummer, above the
  crowd's heads. It looks a little to the singer's left, so the band is on
  the right of the list. The panel darkens the shot slightly. The venue
  unloads when the screen exits.
- GAMEPADS screen: gd01's panel (layout, rows, flow, live highlight,
  rebinding, per-controller type, the mouse and its "x" that clears a
  binding), in GH2's menu font and colours (`ui/port_draw.*`). It opens with
  the main menu's yellow fret (above) or Enhanced Options > Gamepads; back
  returns where it came from. The help bar's yellow icon is the green one
  recoloured: the disc's `hb_fret3.tex` is an unused placeholder without
  alpha.

### Cheats

GH2's long cheats (`config/long_cheats.dta`, in gh2.dta's `(long_cheats
...)`) run as the original's CheatsManager (0x8235B5B0) [C]: every button
pressed (any controller, the joypad's yellow / blue / orange too) goes into
a list of the last 16; a press more than 750 ms after the previous one
starts the list afresh and completes nothing; when the latest presses are a
cheat's buttons its script runs and the list starts afresh. The scripts
themselves check the main menu (`CHEATS_ALLOWED`), set the variables, show
GH2's "CHEAT ENABLED / DISABLED" message and play `cheat` / `cheat_off`.
The codes (G_* = the 360 guitar's frets):

| Cheat (GH2's name) | Code | In gd02 |
|---|---|---|
| Unlock All (saving disabled) | B Y O R Y O B Y B Y B Y B Y B Y | works (`campaign set_all_access`, `$disable_save`) |
| Air Guitar | Y B Y O Y B | works: `$cheat_guitar` is every guitarist's guitar (0x822B6250 [C]), `air_guitar.milo` |
| Performance Mode | B B Y B B O B B | works: the track's and HUD's scripts hide them (the HUD's `on_reset` now runs after its reset, 0x8268E440) |
| Hyper Speed | B O Y O B O Y Y | works: track_speed × 1.5 (0x826967B8 [C]) |
| Flaming Head | Y O Y Y O Y Y O | the code works, nothing burns: the band's particles are STUBs (`gd-backlog.md`) |
| Eyeball Head Crowd | Y O B B B O Y | works (char_objects.dta's crowd `enter`) |
| Monkey Head Crowd | O Y B B Y O B B | works |

(Y yellow, B blue, O orange, R red.) GH2 also names a Horse Head cheat
(`cheat_horse_head_mode`) with no code (cut). Enhanced Options' CHEATS
section lists the seven with GH2's names and toggles the same variables
(`UiManager::setCheat`; Flaming Head greyed); like GH2, cheats are not saved
(the title screen's `reset_all_cheats` turns them off).

### Other

- Leaderboards without Xbox Live shows a port message. Xbox Live calls
  (achievements, leaderboards, marketplace, sign-in) are whitelisted stubs
  (`engine/tests/ui_stubs.txt`).

## Videos (`engine/src/video/`)

The disc's `videos/intro.wmv`, `hmx.wmv`, `wg.wmv` are ASF with WMV3 (VC-1
Main) 720×480 video and WMA v2 44.1 kHz stereo [C data]. MetaPanel
`play_movie` (`0x822A1850` → `0x822A1630`, player `0x82311310`) [C] is a
blocking modal loop, skipped by Start or A, with two extra frames at the end.

gd02 decodes with FFmpeg 7.1.5, built from source (release signature
checked) with only the ASF demuxer and the WMV3 / WMA v2 decoders, LGPL only,
and shipped as shared libraries with `licenses/FFmpeg-LICENSE.LGPL-2.1.txt`
and `FFmpeg-NOTICE.txt` (source link). The mixer's clock drives the pictures;
the YUV planes are converted in our own shader. The pictures are
byte-identical to a reference FFmpeg decode. Built with `GD02_VIDEO=0`, the
videos are skipped. gd02 links FFmpeg directly, so it needs the libraries
next to it to start.

## Tests and development aids

- `--uitest`: 237 `new` objects, 82 panel milos, 357 component names checked
  in reachable handlers.
- `--walktest quickplay|lose|pause|attract|career|career_full|career_song|back|enhanced|cheats|mouse|gamepads|stickyaxis`: drives the menus
  with scripted input and checks the screen order, the score in
  `career.json` and the endgame photo (quickplay), the band's look in
  `career.json` (career: Choose Band > NEW BAND, character / outfit / guitar / skin
  screens; career_full: the first tier, the encore and the store, see
  docs/career_and_store.md), zero unhandled / unknown / script errors, and that every stub
  is whitelisted. quickplay also skips the splashes with a button, pause
  resumes with Start once, back goes main → Options → Audio Settings →
  back → back and checks the main menu's camera and scene are where they
  were. enhanced goes main → Options → Enhanced Options, changes Internal
  Resolution and checks it and `enhanced.json`, then back → back; cheats
  enters Air Guitar and Hyper Speed's codes at the main menu (on), then Air
  Guitar's again after a pause (off); quickplay and career check the fresh
  `career.json` has The Glitches.
  mouse goes main → Options → Enhanced Options with the mouse alone
  (hover, clicks, a dropdown option, right click, the help bar's BACK);
  gamepads opens GAMEPADS with the keyboard's yellow fret, rebinds the
  keyboard's Whammy to E (saved in `controllers.json`), goes back, then
  clicks the help bar's GAMEPADS; stickyaxis plugs in a virtual adapter
  whose D-pad is an axis resting at -32768 (no left / right press in 2 s;
  moved, one right press). Not covered: held keys, volume sliders, lefty,
  the credits ending, bonus videos.
- `GD02_UILOG=1`, `GD02_SCREEN=<screen>`, `GD02_SKIP_BOOT=1`,
  `GD02_SKIP_MOVIES=1`, `GD02_DEBUG_MENU=1` (the old song list),
  `GD02_AUTOPLAY=1`, `GD02_SAVE_DIR`, `GD02_SHOTS=<dir>`. `GD02_KEYS` menu
  items are wall-clock (`<ms>:confirm`, `+key` / `-key`, `p2`), song items
  `s<ms>:…`; `<ms>:k<SDL key name>` (`kTab`, `kReturn`, `k1`) is a real
  key and `<ms>:j<SDL button>` (`ja`, `jstart`, `jdpup`) a button of
  `GD02_VIRTUAL_PAD`'s controller, both through the input layer (bindings,
  the frets' menu buttons) instead of straight to the menus;
  `<ms>:j<axis>=<v>` (`jleftx=-20000`, `ja6=0` the controller's own axis 6)
  moves one of its axes; `hover<x>_<y>`, `click<x>_<y>`, `rclick<x>_<y>`
  (menu units), `wheelup` / `wheeldown` are the mouse.
  `GD02_VIRTUAL_PAD=<vid>:<pid>:dpadaxis=<rest>` is a pad whose D-pad is its
  axes 6 / 7, axis 6 resting at `<rest>`. `GD02_INPUTLOG=1` logs every input
  action. `GD02_SAVE_DIR` also holds `controllers.json`.

## Not done

- Sound effects play (docs/audio_and_charts.md section 4) without the
  reverb sends, the ADSR envelopes or the mute cheat (`synth
  set_master_vol`).
- The endgame stats receipt draws dark (`turn_off_ambient` /
  `reset_ambient` are no-ops).
- Volume sliders apply from the next song or preview.
- Tutorial and practice screens are generic panels (M10 /
  M11). The career and the store: docs/career_and_store.md.
- Help bar background boxes.
- The main menu's words are stacked tightly (re-checked in bug-fix 3: rows
  14 units apart, each `fit_text` 1 into 9 units tall against size 30 ×
  leading 0.7 (Text +0x11c × +0x100, 0x821CFF08), not the glyphs' height):
  BandButton `fit_text`
  kStretched (0x8214C7B0) scales each into its width × height box, the
  height against size × leading (0x821CFF08) while a glyph is size × the
  font's cell height / width tall (0x821CFB28), inside main_buttons.view's
  own (0.55, 1, 1.9) scale. All of it matches the code; not compared with
  a capture of the original yet (Q1).
