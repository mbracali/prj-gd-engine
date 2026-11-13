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
  `bandcheck: passed` or `FAILED (n problem(s))`; `channels within their
  clips` (D78): every servo position channel within its drivers' largest
  clip values (GD05_BLENDER_LEGACY=1: the hand drivers without
  limbs.blender, fails). `GD05_BAND_LEGACY=1` runs
  the band without the wave's fixes (no RB parsers, GH2 weight setters,
  draw_only ignored, no RB face) to show the checks failing.
- `GD05_FACE_CAM=player_guitar0:50:0:bone_spine2.mesh GD05_POST=0
  GD05_TRACK=0 GD05_HUD=0` with `GD05_SCREENSHOT(_AT)`: close shots of a member.
  `GD05_PREFAB_VOCALS=vocals2` (any instrument: GD05_PREFAB_GUITAR, _BASS,
  _DRUM) picks the premade; bandcheck's "face rest" line checks each
  member's jaw against its face clips (D94).

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

Wave G06 general bugs (GD05-073):
- `--walktest rb_songlist_highlight`: the song list scrolled down and up;
  at each step exactly one drawn name (the selected row's) lies inside the
  highlight's quad (`songlist highlight:` lines). GD05_SONGLIST_LEGACY=1:
  rows without song_select_gap, every song listed (fails).
- `--walktest rb_stutter`: previews while the list scrolls and a song's
  start: `frame_spike:preview:30` (median of the previews' 30-frame
  windows' longest frame / the run's median frame, x10, <= 40),
  `frame_spike:song_begin!:240` (<= 150), `song_gpu_uploads` 0.
  GD05_MUSIC_LEGACY=1 / GD05_WARM_LEGACY=1 run the code before.
  GD05_FRAMELOG=1 logs the 240 frames after each mark (preview,
  preview_play, song_begin); GD05_HITCHLOG=<ms> (wave 05) the slow ones.
- `--walktest rb_qp_locked`: the quick play list against the data's
  available songs, 300 Random Song picks, the song started.
- `--walktest rb_od_keys | rb_od_pad | rb_od_guitar | rb_od_drums |
  rb_od_vocals`: Overdrive by Tab / a pad's Back / the RB guitar's tilt and
  Back / the drum fill / a shout in a gap: refused at 45 %, deployed at
  60 %, drained over 32 beats; rb_od_keys also checks the crowd meter's
  deploy triggers. Walk hooks: `od_set:<pct>`, setPlayerStarPower
  `vocal_meter` / `vocal_shout`, `dev_axis <n> <v>`, `unlock_all_songs`.
- Loads: GD05_UILOG=1 logs `ui: menu backdrop loaded in`, `world warmed:`,
  each screen's enter / exit; agents/scratch/gen6/pmp.sh (git-ignored) is a
  gdb stack sampler (`pmp.sh out N delay args...`, fold.py folds it).

Wave G06 career, shop and creator (GD05-072, D67..D70):
- `--walktest rb_career`: Solo > Tour, a new character (attitude, skin and
  hair chosen in the creator), the Rock Shop (a shirt at the catalog's
  price), a gig on autoplay (the character on stage in its clothes and
  attitude; the cash up by reward_per_star x stars, the event's outfit
  won), then the game restarts (runApp) and `rb_career_restarted` reads it
  all back from rb_profile.json (cash, look, clothes, seen hints).
- `--walktest rb_career_band`: a guitarist made in the Solo Tour, a
  restart, Band World Tour with a drummer (GD05_BAND_PLAYERS 2): the
  member's character is paid the band event's reward, the band's money is
  its members'; a second restart checks the band and the money.
- `--walktest rb_shop_items`: the instrument shop (char/instruments.dta):
  too dear for a new character (shop_error_money_screen), bought after
  RB's cheat_add_money 2000 at the model's price, played on stage, kept
  through a restart.
- The walk's final line is `walktest <walk>_restarted: OK` (each
  restart appends `_restarted`; the process's exit code is the last
  part's). `GD05_WALK_KEEP_GOING=1` runs every check of a walk and prints
  each failure (no verdict: for older builds).
- `agents/scratch/car6/gate_par.sh [jobs]` (git-ignored) runs gate1.sh's
  walks and the five tests in parallel, each with its own XDG_DATA_HOME.

Wave G06 tour rules and finishes (GD05-085 / GD05-077 / GD05-083, D75 / D76):
- `--walktest rb_tour_rules`: a new guitarist's gig guide against
  config/tour_solo.dta and the songs' ranks (`rb_tour_solo_events`: the
  open towns' open events and each one's song), the first (unlock N) gig
  played (`rb_tour_unlocked`: new_unlock the next bonus song, then (unlock
  2) listed), restarted, quick play's list (`rb_tour_qp`: modes.dta's
  first songs + the open town's guitar songs + the unlocked bonus songs).
  GD05_TOUR_UNLOCKS_LEGACY=1: quick play without the tours (fails).
- `rb_career_band` checks `rb_bwt_buzz` after the gig: buzz from the
  venue's performance table, fans on the (fans ...) line, the gig's songs
  offered in quick play.
- `rb_shop_items` now goes through group_select_screen
  (`rb_shop_finishes`: the model's finishes as the data's) and buys the
  second finish.
- `agents/scratch/car7/gate_par.sh [jobs]` (git-ignored): gate1.sh's walks
  + rb_highway, rb_crowd + the five tests, each its own XDG_DATA_HOME;
  B_OVERRIDE=<binary> runs another build.

Wave G06 lobby, shops, deform, menu characters (GD05-082 / 083 / 084, D80..D83):
- `--walktest rb_band_lobby`: Band World Tour > a new band > the real
  lobby with two pads (GD05_INSTRUMENT guitar,drum, no GD05_BAND_PLAYERS):
  the slots' states (`rb_lobby_slots`), each player's character list
  (`rb_lobby_rows`), each makes a character from the lobby (the creator and
  back), the leader's CONTINUE, the first gig: each player's own look in
  its part (`rb_lobby_stage`, counter band_slot_piece:<slot>:<piece>);
  restarted, a new band, each picks the saved one (`rb_lobby_chars`).
  GD05_BAND_PLAYERS=<n> still skips the lobby (D10) for the other walks.
- `--walktest rb_shop_drum | rb_shop_mic`: a new drummer / singer buys the
  first drum kit / mic with a cost (char/instruments.dta) through
  group_select_screen, worn on stage (band_piece_suffix:_drums_<finish>).
- `--walktest rb_body_deform`: the creator's weight / height sliders at the
  top, the guitarist's bones on stage against the deform clip's sample 1
  (counter band_bone:<slot>:<bone>:<axis>, x 1000).
- `rb_preview_drawn` (in rb_body_deform and rb_shop_drum): the menu's
  character preview drew meshes and changes > 500 pixels (the frame with
  and without it, RbCharPreview::hidden).
- `agents/scratch/car8/gate_par.sh [jobs]` (git-ignored; WALKS="..."
  for a subset, B_OVERRIDE=<binary>).
