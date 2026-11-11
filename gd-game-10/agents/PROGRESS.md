# gd-game-10 progress log

Guitar Hero: Metallica (Xbox 360) -> `gd10`. Kickoff:
`docs/spikes/GD-GAME-10_KICKOFF.md` (wave 01, branch `feature/spike-metal`).
Newest entries at the bottom. "Verified" lines quote the command and its
output; "built only" and "not tested" are said explicitly.

## Final report, proof of concept (2026-11-11, about 3 h of agent time, 23:39 -> 02:40)

Scope (defaults chosen, owner to confirm: kickoff "Owner's answers"): boot
first, then one Quickplay song on guitar. Version stays 0.0.0;
ready_to_play false; not in the launcher's packages.

**Milestones.** M0 done. M1 done for what the boot and a song need (paks,
QB, scripts, fonts, UI images, QS strings, song audio, Bink), not for
scenes / anims / skins / .wav. M2: charts, tuning and the three song banks
in; the judging rules are still GH3's [I]. M3: gh4's UI state machine and
descs run the original's own boot to the main menu. M4 in part: Quickplay
from the main menu to a song, YOU ROCK!, the results, cash rewards and back
to the set list, but on the labelled debug highway and with no venue drawn.

**What boots (verified, fresh `engine/build/devv/gd10`, headless):**
- `--walktest boot`: passed (legal, the four boot movies skipped with A,
  press start, sign-in, memcard, drum tip pop-up, main menu); black frames 0.
- `--walktest quickplay`: passed (main menu > QUICKPLAY > game mode >
  guitar > set list (+ first-time pop-ups) > Albatross > Start > Expert >
  song in z_Forum on autoplay > YOU ROCK! > song breakdown > cash rewards >
  autosave > set list).
- `--scripttest`: "every script tokenizes; boot ran (71 natives to write)".
- `pak.py` / `qb.py` / `qbscript.py check`: 0 problems over the whole disc.
- `--selftest`: 688 charts autoplayed; 66 failures (4 Death Magnetic DLC
  songs have no audio on the disc, bass open notes in the tapping check, 2
  near misses, 5 virtual-controller checks inherited from gd04).
- `--songaudiotest entersandman`: the three banks decode (9 stereo pairs).
- Build: 0 warnings (Linux GCC, dev builds).

**Screenshots checked** (agents/scratch/, git-ignored): press start
(correct), main menu (correct layout and art), drum tip and double-bass
pop-ups (correct), difficulty pop-up (text clipped), set list (song names
empty, tabs overlap), career / difficulty menus (skull art over the menu),
the song (debug highway), YOU ROCK!, song breakdown (score 000,000, text
clipped).

**Built only / not tested:** MinGW, Android and Steam Deck builds, the
launcher module, Wine, real controllers, audio output (dummy driver).

**Approximations (all in DECISIONS):** D4 state call order, D6 sprite dims,
D8 stem routing, D9 note flags, D10 Glitches stand-in, D11 switch
fallthrough, D12 IsSongAvailable, D13 debug highway.

**Next (in order):** gh4 scenes (venues, the 3D gems of Gem_Set_01, the
highway skins), the score into the results (gh4 reads player info, not
GH3's playerN_status fields), the set list's song names and the text
fitting (fit_width / fit_height), the remaining 71 natives (--scripttest),
the judging rules from Metallica's XEX (hit window, HOPO, Star Power,
whammy), streamall's XMA (sound effects), song previews' key, then M5+.

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
