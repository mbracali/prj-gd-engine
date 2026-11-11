# gd-game-10 progress log

Guitar Hero: Metallica (Xbox 360) -> `gd10`. Kickoff:
`docs/spikes/GD-GAME-10_KICKOFF.md` (wave 01, branch `feature/spike-metal`).
Newest entries at the bottom. "Verified" lines quote the command and its
output; "built only" and "not tested" are said explicitly.

Estimate (kickoff 2): 4-6 h for the proof of concept (M0..M4 on guitar);
stop at about 6 h whatever the milestone.

## 2026-11-10 23:40 M0 set-up

- gd04's engine, tools and build scripts copied, renamed gd10 (D1). Version
  0.0.0. Built: `ninja -C gd-game-10/engine/build/dev`: 0 warnings (one
  GCC 16 false positive in `port_gamepads.cpp` silenced by masking the index).
- Rip: `xdvdfs.py extract` -> "extracted 1470 files, 7.22 GB".
- Ghidra: import + `FixXexFunctions` + `ExportAnalysisXex` -> "Exported 30337
  functions, 30337 decompiled, 94894 data refs, 9246 strings".
- Verified: `pak.py check gd-game-10/extracted` -> "paks: 1060, files:
  33406, with problems: 0" (after the CHNK reader and the padded-pak rule).
- Verified: `qb.py check gd-game-10/extracted` -> "qb files: 1544, sections:
  38466, scripts: 8853, with problems: 0" (after the gh4 struct items and
  type 0x1C).
- Verified: headless run on the ISO boots, loads the disc and logs what
  fails: "Guitar Hero: Metallica disc: 0 songs", fonts "glyph table
  offset", "streamall.fsb.xen: not an FSB3 bank", "guitar_startup did not
  start". `--formattest`: 18,202 failures by reason (formats.md "Not
  decoded yet").

## 2026-11-11 02:25 M1-M3 (boot) and M4 (Quickplay song) reached in part

Time so far: about 2 h 45 min of agent time (23:39 -> 02:25).

Verified (fresh binary `engine/build/devv/gd10`, with FFmpeg from gd04's
local prefix copied under engine/build/ffmpeg/linux):
- `--walktest boot` -> "walktest boot: passed (UIstate_boot_movie_atvi >
  ... > uistate_boot_iis > uistate_optimal_drum > main_menu_fs)", "black
  frames: 0".
- `--walktest qp4` (Quickplay, being built) reaches the main menu, game
  mode, instrument, set list, picks a song (Albatross), difficulty Expert,
  the song starts in z_Forum ("song Albatross (Albatross, Expert): 951
  notes"), autoplays (streak 54, x4 at 70 s) and the original's YOU ROCK!
  shows; the walk then waits on gameplay (results not reached).
- `--selftest`: charts parse and autoplay (688 charts, 497,010 notes); 66
  failures: 4 DLC songs without audio on the disc (expected), bass open
  notes in the tapping check, 2 near misses, 5 virtual-controller input
  checks (gd04 GH2/GH3 types, not looked at).
- `--songaudiotest entersandman` decodes the three banks (9 stereo pairs).
- `pak.py check` / `qb.py check` / `qbscript.py check`: 0 problems.
- Screenshots looked at (git-ignored, agents/scratch/): press start,
  main menu, drum tip pop-up, double-bass pop-up, difficulty pop-up, set
  list (partial), the song (debug highway), YOU ROCK!.

Not done / approximate: the highway (debug highway, D13), the venue is not
drawn (scenes: "material name area not zero"), menu layouts off in places
(skull art over the career / difficulty menus, set list song names empty),
results screen, sound effects partly (XMA1 errors on streamall).
