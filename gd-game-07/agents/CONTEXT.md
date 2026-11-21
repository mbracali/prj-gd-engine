# gd-game-07: working context (paths, tools, the game)

Read this first in a new session, then `PROGRESS.md`, `DECISIONS.md`,
`OPEN_QUESTIONS.md`, `docs/FORMATS.md` and the backlog's gd-game-07 section.
Guitar Hero World Tour is the first "gh4" engine; Guitar Hero: Metallica
(`gd-game-10`, released 2009) is built on it, so gd-game-10 is the model for
everything here, as gd-game-04 (GH3) is for Aerosmith (gd-game-06).

## Paths (relative to the repository root)

| What | Where |
| --- | --- |
| Disc image | `original_isos/X360 - Guitar Hero World Tour (USA, Europe) (En,Fr,De,Es,It).iso` (git-ignored) |
| Rip | `gd-game-07/extracted/` (`python3 -I gd-game-10/tools/xdvdfs.py extract <iso> gd-game-07/extracted`; 6,635 files, 6.30 GB, 13 s) |
| Name table | `gd-game-07/extracted/names.tsv` (126,468 names; a copy of `gd-game-10/tools/qbkey.py --table`, which reads `../extracted` next to the script) |
| Ghidra project | `gd-game-07/ghidra/ghwt.gpr` (program `default.xex`) |
| Ghidra export | `gd-game-07/ghidra/analysis/` (`functions.tsv` 30,030, all decompiled, `strings.tsv` 9,394, `refs.tsv` 94,578, `cfuncs.tsv` 1,773 entries / 1,708 names) |

`extracted/`, `ghidra/` and `agents/scratch/` are git-ignored: never commit
them, never paste decompiler output into tracked files (docs/legal.md).

## Tools

Since GD07-002 (M1) gd07 has its own engine (a copy of gd10's, DECISIONS D5).
Build (the FFmpeg prefix copied from gd10's build, git-ignored):

```sh
cmake -S gd-game-07/engine -B gd-game-07/engine/build/devv -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGD07_FFMPEG_DIR=$PWD/gd-game-07/engine/build/ffmpeg/linux/ffmpeg-prefix
ninja -j4 -C gd-game-07/engine/build/devv
gd-game-07/agents/scratch/ft.sh     # gd07 --formattest on the disc: "formattest: all files parse"
```

Before M1, two read-only ways were used:

- gd-game-10's Python tools (now copied into `gd-game-07/tools/`, whose
  `../extracted` is gd-game-07's rip). They are GH3-era for animations: their
  `anim.py` rejects every gh4 `.ska` (Metallica's too), so use them for paks,
  QB and scripts only.
- gd-game-10's C++ `--formattest` (the real gh4 decoders) on World Tour's
  disc, from any gd10 build:

```sh
E=../extracted   # run from gd-game-07/tools
python3 -I pak.py check $E         # paks: 929, files: 31554, with problems: 0
python3 -I qb.py check $E          # qb files: 1809, sections: 45233, scripts: 9606, with problems: 0
python3 -I qbscript.py check $E    # scripts: 9606, with problems: 0
ISO="$PWD/original_isos/X360 - Guitar Hero World Tour (USA, Europe) (En,Fr,De,Es,It).iso"
SDL_AUDIO_DRIVER=dummy SDL_VIDEO_DRIVER=offscreen XDG_DATA_HOME=$PWD/gd-game-07/agents/scratch/xdg \
  timeout 900 gd-game-10/engine/build/devv/gd10 --formattest "$ISO"   # 32 s; 10,656 failures, docs/FORMATS.md
```

Never run `fsb.py` or any song-bank decoder on the song banks (song audio is
an owner item, OPEN_QUESTIONS 1).

## Ghidra (headless, heap at most 6 GB; the project path must be absolute)

```sh
export JAVA_HOME=~/.local/opt/jdk-21.0.12.1+1 GHIDRA_HEADLESS_MAXMEM=6G
G=~/.local/opt/ghidra_12.1.4_PUBLIC/support/analyzeHeadless
P=$PWD/gd-game-07/ghidra; SP=$PWD/gd-game-07/tools/ghidra_scripts
$G $P ghwt -import $PWD/gd-game-07/extracted/default.xex                       # 4 min
$G $P ghwt -process default.xex -noanalysis -scriptPath $SP -postScript FixXexFunctions.java   # 2 min
$G $P ghwt -process default.xex -noanalysis -readOnly -scriptPath $SP -postScript DumpCFuncs.java $P/analysis
$G $P ghwt -process default.xex -noanalysis -readOnly -scriptPath $SP -postScript ExportAnalysisXex.java $P/analysis  # 1 min
$G $P ghwt -process default.xex -readOnly -noanalysis -scriptPath $SP -postScript DumpAsm.java fn:0x821AB988
```

`agents/scratch/ghidra_all.sh` runs the first four in a row (7 minutes).
REA cannot open the XEX (no XEX2 / PowerPC). Metallica's [C] addresses do not
carry over: re-find each function in World Tour's XEX (by its C-function
table entry, strings or callees) before marking it [C] here.

## The executable

- `default.xex`: XEX2, title ID **4156081A** (Metallica 41560830), base
  0x82000000, entry 0x821AB988 [C: XEX headers, Ghidra import log].
- 30,030 functions (Metallica 30,337); 24,571 (81.8%) pair with Metallica's
  by size alone, a lower bound (any code change in a function or its inlined
  callees changes the size).
- QScript C functions: 1,708 names, 1,692 shared with Metallica, 1,077 with
  GH3. World Tour only (16): `CleanupTemoraryRagdollSkels`, `GetBandStars`,
  `UnpackRawRagdoll`, `UnpackRawSkeleton`, `MassiveImpressionJob_Module`,
  `PMovies_TextureMovieUpdateJQ_module`, `decompress_gz_module`,
  `nxcull_{box,bvs,occluder,sphere}_module`,
  `s_process_job_{adddifference,blendtofullbody,blendtopartial,getsource,tweakbones}_module`
  (most are job-queue "module" entry points Metallica dropped or renamed).
  Metallica only (40): the music store / downloads (`MusicStore*`,
  `*DownloadSongList`), CAS asset unloads (`CASUpdateUnloads`,
  `CleanupUnpackedCASAssets`, `UnpackRawCASAsset`), achievements cache,
  light-type toggles, screensaver, `MathMax`, `FormatDecimals`.

## The game (from the disc)

- 131 song paks (`DATA/COMPRESSED/SONGS`), audio in `DATA/MUSIC/<song>_{1,2,3,preview}.fsb.xen`
  (+ `streamall.fsb.xen`), the same three-bank layout as Metallica; banks are
  not plain FSB (as Metallica's).
- 33 zones (`DATA/COMPRESSED/ZONES`): the venues (z_ballpark, z_bayou,
  z_castle, z_cathedral, z_diner, z_fairgrounds, z_frathouse, z_goth,
  z_harbor, z_hob, z_hotel, z_metalfest, z_military, z_newyork, z_pub,
  z_recordstore, z_scifi, z_space, z_tool, z_underground, z_vegas,
  z_wedding, z_wonderland, z_heaven, ...), plus z_frontend, z_in_game,
  z_soundcheck, z_studio / z_studio2 (music studio), z_board_room,
  z_garage, z_credits, z_whitebox. Metallica has 16 zones.
- The GHTunes / music studio: `DATA/COMPRESSED/JAMS/jam1.pak.xen` and the
  19 `.jam` entries (no decoder in gd10).
- Movies: 23 Bink files in `DATA/MOVIES/BIK` (logos, intro / outro per role,
  Line 6 POD, the Metallica trailer).
- Instruments: guitar, bass, drums, vocals, as Metallica (gd10 already
  plays drums and vocals).
- Full create-a-rocker (CAS), instrument creation, band career with tiers,
  battles; Metallica has a reduced CAS built on this one.
