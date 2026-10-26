# GH2's front end: scripts, UI objects, screens, saves and videos

How gd02 runs Guitar Hero II's own menus (M7): the game's `ui/*.dtb` scripts
on gd01's interpreter, the UI classes of the panel milos, the screen manager,
the save, and the disc's `.wmv` videos. Addresses are in `default.xex`
(`analysis/`); **[C]** confirmed in code, **[I]** inferred.

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
  focus, button routing, key repeat, the help bar. `script_classes`: the
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

### Confirmed

- Quick play list (`0x82287600` / `0x822C5A68`): the first two tiers' songs
  (encores included), plus unlocks, bought songs and downloads; a fresh save
  shows 12. Rows show no score, stars or check (`0x822C3428` / `0x822C34E8`);
  the last tier song carries the `song_quickplay` blurb (`0x822C0070`);
  headers use `song_header_%s` (`0x822BFFF0`).
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

- Splashes run 3 s and can't be skipped; tier headers sit in the first
  song's row; a panel's tasks end when it exits (screens keep theirs);
  screens pass unknown messages to their focus panel; a panel without
  `(focus …)` focuses its first focusable component.
- Menu music starts with MetaPanel; loops at −6 dB; previews fade 1 s.
- Results: `star_rating` as "***__", `avg_multiplier` = score ÷ base score;
  game_won fires at the chart's end with a 6 s outro, game_lost 3 s;
  `fast_intro` cuts the lead-in to 1 s.
- Volume indices default to 11 (full). The endgame photo is hidden (GH2
  renders the band into it at run time).

## Save (`ui/career.*`)

GH2's Campaign::Save (`0x822869A0`, version 17, read back by `0x82287B00`)
[C]: eight band profiles, high scores, bought / unlocked items, options
(`0x822B0BC0`: lefty P1 / P2, volume indices, sync offset). gd02 keeps the
same data as `career.json` (format `gd02-save`) in the save folder, written
atomically with `career.json.bak`. Every setter marks the save dirty (L12).

## Port additions

- GAMEPADS screen: gd01's panel, reached from the main menu's help bar
  (yellow / fret 3) and a GAMEPADS button in Options. Defined in the port's
  own script (`ui/port_screens.cpp`).
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

- `--uitest`: 235 `new` objects, 83 panel milos, 357 component names checked
  in reachable handlers.
- `--walktest quickplay|lose|pause|attract`: drives the menus with scripted
  input and checks the screen order, the score in `career.json`, zero
  unhandled / unknown / script errors, and that every stub is whitelisted.
  Not covered: held keys, the GAMEPADS screen, volume sliders, lefty,
  the credits ending, bonus videos.
- `GD02_UILOG=1`, `GD02_SCREEN=<screen>`, `GD02_SKIP_BOOT=1`,
  `GD02_SKIP_MOVIES=1`, `GD02_DEBUG_MENU=1` (the old song list),
  `GD02_AUTOPLAY=1`, `GD02_SAVE_DIR`, `GD02_SHOTS=<dir>`. `GD02_KEYS` menu
  items are wall-clock (`<ms>:confirm`, `+key` / `-key`, `p2`), song items
  `s<ms>:…`.

## Not done

- Sound effects: GH2's banks are XMA SynthSamples (not decoded); `synth sfx`
  is a stub.
- The endgame stats receipt draws dark (`turn_off_ambient` /
  `reset_ambient` are no-ops).
- Volume sliders apply from the next song or preview.
- Career, multiplayer, store, tutorial and practice screens are generic
  panels (M10 / M11); the career list reuses the quick play rules.
- Help bar background boxes; GAMEPADS font sizes approximate.
