# gd05: context for agents

How to build, test and run gd-game-05 (Rock Band, Xbox 360). Read the
kickoff (`docs/spikes/GD-GAME-05_KICKOFF.md`), `PROGRESS.md`, `DECISIONS.md`
and `OPEN_QUESTIONS.md` first.

## Layout
- `engine/`: the C++ engine, a renamed copy of gd-game-02's (DECISIONS.md D1).
  Addresses in comments inherited from gd02 are GH2's; RB's are written
  `RB 0x82...` in code and `[C] 0x82...` in `docs/`.
- `tools/`: Python decoders (`xdvdfs.py`, `ark.py`, `dtb.py`, `milo.py`,
  `tex.py`, `mogg.py`, `midi_chart.py`) and `ghidra_scripts/` (gd-game-04's).
- `extracted/` (git-ignored): `disc/` (xdvdfs.py extract of the game
  partition), `ark/` (ark.py extract, 4,588 files), `dta/` (dtb.py dump, every
  `.dtb` as text, 291 files).
- `ghidra/` (git-ignored): project `rb1`, program `default.xex`, exports in
  `ghidra/analysis/` (functions.tsv, strings.tsv, refs.tsv, decomp/*.c).

## Disc
`ISO="$HOME/Development/prj-gd-engine/original_isos/Rock Band (USA).iso"`
(7.8 GB, XGD2, title ID 45410829). `default.xex`: XEX2, image base
0x82000000, entry 0x824F9000 [C: XEX headers, Ghidra import log].

## Reverse-engineering tools
- **Ghidra 12.1.4 headless** + XEXLoaderWV (language `PowerPC:BE:64:A2ALT-32addr`):
  ```sh
  export JAVA_HOME=~/.local/opt/jdk-21.0.12.1+1
  G=~/.local/opt/ghidra_12.1.4_PUBLIC/support/analyzeHeadless
  P=$HOME/Development/prj-gd-engine/gd-game-05/ghidra
  S=$HOME/Development/prj-gd-engine/gd-game-05/tools/ghidra_scripts
  # done once (2026-11-09): import, FixXexFunctions (41,175 functions), export
  $G $P rb1 -process default.xex -noanalysis -readOnly -scriptPath $S -postScript ExportAnalysisXex.java $P/analysis
  # assembly of a function / a range (rule 4: read call sites in assembly)
  $G $P rb1 -process default.xex -noanalysis -readOnly -scriptPath $S -postScript DumpAsm.java fn:0x82300000 0x82300000:40
  # data words
  $G $P rb1 -process default.xex -noanalysis -readOnly -scriptPath $S -postScript ReadF.java 0x82001234:f:4 0x82005678:str
  ```
  The project path must be absolute with no `.` element.
- REA is not used: on gd-game-04 it could not open a XEX nor a PowerPC raw
  image (gd-game-04/agents/CONTEXT.md).

## Build
From `gd-game-05/engine`:
- configure once (FFmpeg with Bink, see `build_common.sh`):
  `bash -c '. ./build_common.sh; build_ffmpeg build/ffmpeg/linux Linux --disable-x86asm'`, then
  `cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DGD05_VIDEO=ON -DGD05_FFMPEG_DIR="$PWD/build/ffmpeg/linux/ffmpeg-prefix"`
- build: `ninja -C build/linux` (0 warnings required)
- Windows (MinGW): `./build_windows.sh`; all platforms: `../local_build.sh all`.
- Check the binary is newer than your edits before testing (kickoff rule 4.2).

## Tests (all take the disc path first; a usage text means it did not run)
Always with the owner's devices ignored, the dummy audio driver and a
scratch data folder:
```sh
cd gd-game-05
export XDG_DATA_HOME=$PWD/agents/scratch/xdg SDL_AUDIO_DRIVER=dummy SDL_VIDEO_DRIVER=offscreen
export SDL_GAMECONTROLLER_IGNORE_DEVICES=0x1bad/0x0002 SDL_JOYSTICK_IGNORE_DEVICES=0x1bad/0x0002
timeout 120 engine/build/linux/gd05 "$ISO" --scenetest
```
Each milestone adds its tests here; `PROGRESS.md` has each one's last
verified output.

Front end (wave 03, `docs/frontend.md`):
- `--walktest rb_boot`: the title and main menu over the background video,
  no venue sound before the first song, a click on the song list's help bar
  Back. `--walktest rb_owner_bugs`: the song list's layout and help bar,
  then keyboard 2 during the song's start does not reload it.
- `python3 -I tools/frontend.py extracted/dta [screen...]` (or `--md`): the
  scripts' screens, panels, back and the screens each goes to.
- Development aids: `GD05_SFXLOG=1` logs every sound object started
  (`synth: ...`, "over N playing", looping samples) and the crowd streams;
  `GD05_SHOTS=<dir>` saves each settled screen; `GD05_UILOG=1` logs screen
  changes; `GD05_SONGLIST_3D=1` draws the song list through its banner
  camera. Python checks:
`python3 -I tools/milo.py scan extracted/ark`, `python3 -I tools/dtb.py dump extracted/ark extracted/dta`.

Wave 04 (the cloth, the loading band, transitions, the fret board):
- `--walktest rb_pause_quit`: pause, QUIT, confirm yes, the main menu.
  `rb_boot` probes black frames (`probe_black` ... `no_black`; RB's own fx
  fade through black is not counted); `rb_owner_bugs` checks
  `song_list_layout` (the cloth polled) and `loading_icons`.
- Development aids: `GD05_SONGLIST_FLAT=1` shows the song list's texture
  flat (wave 03's); `GD05_CLOTHLOG=1` logs the cloth's root and its screen
  bounds; `GD05_TRACKLOG=1` logs the track's camera, the now bar's screen
  place, the draw group's and the smashers' materials, the tails;
  `GD05_TAILTEST=1` gives every guitar / bass gem a 600 ms tail;
  `GD05_BLACK_SHOTS=<dir>` saves the black frames the probe counts.
- Highway shots: `gd05 "$ISO" --song <id> --autoplay --part guitar|bass|drum
  --difficulty 3` with `GD05_SCREENSHOT=<f.bmp> GD05_SCREENSHOT_AT=<song
  ms>[,<ms>...]` (song times; the autoplay lets go of the sustains it hits).

## Clocks
Inherited from gd02 (`gd-game-02/docs/audio_and_charts.md`): the song clock
is the mixer's (audio frames played, held back by the device latency), the
track clock follows it; the sfx log and screenshot timestamps are wall
clock. Rock Band adds the vocal pitch clock (the mic capture) and the
input / video calibration: documented in `docs/audio_and_charts.md` as they
land.

Wave 05 retest (2026-11-10):
- `--walktest rb_keys_play`: a quick play song played with key events only
  (frets 1-5, strum Down, Tab Overdrive, Enter pause / resume) beside a
  virtual guitar holding player 1; `rb_keys_rebind`: GAMEPADS' capture and
  Save of green on Q, then played with Q.
- `--walktest rb_fuzz`: seeded random mouse / key input on the main menu and
  its sub-menus (`GD05_FUZZ_SEED`, default 1; `GD05_FUZZ_STEPS`, default
  2000; ~10 min); every few steps the screen's panels must be up, shown and
  draw their buttons on screen, with a focused panel. On a failure it prints
  the last actions and saves `fuzz_fail.bmp` under `GD05_WALK_SHOTS`.

Wave G06 band (GD05-066/067/068):
- `GD05_BANDCHECK=1 GD05_END_AT=60000 gd05 "$ISO" --song inbloom --autoplay
  --part guitar --difficulty 3`: `bandcheck:` lines when the band goes
  (stretch of the body's skinned bones vs 1.2 x bind, the fret finger and
  pick on their IK targets, the drummer's motion on the drum notes, the
  singer's jaw, the mic in the hand and hands.grp's draw_only), then
  `bandcheck: passed` or `FAILED (n problem(s))`. `GD05_BAND_LEGACY=1` runs
  the band without the wave's fixes (no RB parsers, GH2 weight setters,
  draw_only ignored, no RB face) to show the checks failing.
- `GD05_FACE_CAM=player_guitar0:50:0:bone_spine2.mesh GD05_POST=0
  GD05_TRACK=0 GD05_HUD=0` with `GD05_SCREENSHOT(_AT)`: close shots of a member.

Wave G06 highway and crowd (GD05-070 / GD05-071):
- `--walktest rb_highway`: App::rbProbes under every frame: the Overdrive
  meter alone (no luma jump straight back: `meter_flickers` 0), the
  track's draw order against static.grp's + setup_draworder
  (`rb_track_order_errors` 0), the HUD's directories in draw order
  (`rb_hud_order_inversions` 0), star tails on tail_bonus.mat and near
  white (`rb_star_tails_wrong` 0, `star_tail_chroma_pct` <= 20).
  GD05_HWY_LEGACY=1 runs the code before the fixes (the checks fail).
- `--walktest rb_crowd`: a shot with a crowd (GD05_FORCE_SHOT `#crowdN`),
  the audience alone every 30th frame (`crowd_px_max`, `crowd_clips`);
  GD05_VENUE picks the venue, GD05_CROWD_LEGACY=1 the code before.
- The same probes in `--song` runs: GD05_METER_PROBE / GD05_TAIL_PROBE /
  GD05_CROWD_PROBE=1 log `rb probe:` / `crowd probe:` lines;
  `agents/scratch/hwy6/crowd_sweep.sh` (git-ignored) ran every venue.
