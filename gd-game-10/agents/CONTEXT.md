# gd-game-10: working context (build, test, run, tools)

Read this first in a new session, then `PROGRESS.md`, `DECISIONS.md`,
`OPEN_QUESTIONS.md` and the backlog's gd-game-10 section.

## Paths (relative to the repository root)

| What | Where |
|---|---|
| Disc image | `original_isos/Guitar Hero - Metallica (USA, Europe) (En,Fr,De,Es,It).iso` (git-ignored) |
| Rip | `gd-game-10/extracted/` (`tools/xdvdfs.py extract <iso> gd-game-10/extracted`; 1,470 files, 7.2 GB) |
| Name table | `gd-game-10/extracted/names.tsv` (`tools/qbkey.py --table gd-game-10/extracted/names.tsv`; 98,346 names) |
| Ghidra project | `gd-game-10/ghidra/ghm.gpr` (program `default.xex`) |
| Ghidra export | `gd-game-10/ghidra/analysis/` (`functions.tsv`: 30,337 functions, `strings.tsv`, `refs.tsv`, `decomp/`) |

Both `extracted/` and `ghidra/` are git-ignored: never commit them, never
paste decompiler output into tracked files.

## Build

The songs' XMA and the Bink movies need FFmpeg: the dev build `devv` uses
an FFmpeg prefix copied (not built) from gd-game-04's local build into
`gd-game-10/engine/build/ffmpeg/linux/ffmpeg-prefix` (git-ignored; same
configure as build_common.sh). Without it (build/dev, GD10_VIDEO OFF) the
boot stops at the first loading screen (it waits for its movie).

```sh
cmake -S gd-game-10/engine -B gd-game-10/engine/build/devv -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGD10_FFMPEG_DIR=$PWD/gd-game-10/engine/build/ffmpeg/linux/ffmpeg-prefix
ninja -C gd-game-10/engine/build/devv
```

Check the binary is newer than your edits (`ls -la --time-style=+%T .../gd10`).

## Tests (exact invocations; every run under timeout, one at a time)

```sh
ISO="$PWD/original_isos/Guitar Hero - Metallica (USA, Europe) (En,Fr,De,Es,It).iso"
B=gd-game-10/engine/build/devv/gd10
export SDL_GAMECONTROLLER_IGNORE_DEVICES=0x1bad/0x0002 SDL_JOYSTICK_IGNORE_DEVICES=0x1bad/0x0002 \
       SDL_AUDIO_DRIVER=dummy SDL_VIDEO_DRIVER=offscreen XDG_DATA_HOME=/tmp/gd10_xdg
timeout 600 $B --formattest "$ISO"     # "formattest: all files parse"; per-reason "failures" lines otherwise
GD10_QUIT_AFTER=1500 GD10_SCREENSHOT=gd-game-10/agents/scratch/x.bmp GD10_SCREENSHOT_AT=1400 timeout 60 $B "$ISO"
python3 -I gd-game-10/tools/pak.py check gd-game-10/extracted   # "paks: 1060, files: 33406, with problems: 0"
python3 -I gd-game-10/tools/qb.py check gd-game-10/extracted    # "qb files: 1544, ... with problems: 0"
python3 -I gd-game-10/tools/qbscript.py check gd-game-10/extracted  # "scripts: 8853, with problems: 0"
XDG_DATA_HOME=/tmp/gd10_xdg_walk timeout 600 $B --walktest boot "$ISO"       # "walktest boot: passed (...)"
XDG_DATA_HOME=/tmp/gd10_xdg_walk timeout 600 $B --walktest quickplay "$ISO"  # "walktest quickplay: passed (...)" (wipe the folder first: a new save)
timeout 300 $B --songaudiotest entersandman "$ISO"   # each stereo pair's level every 5 s
timeout 900 $B --selftest "$ISO"                     # 66 failures today (PROGRESS)
```

Dev aids added for gh4: GD10_FLOWLOG=1 (UI events, the state stack,
game_mode, pak loads), GD10_VMTRACE=1|2 (script runs / spawns with params;
2: natives too), GD10_CONDTRACE=1 (scripts used as conditions and their
values), GD10_TAGLOG=<tag>, GD10_GH4_HIGHWAY=1 (the gh4 highway layout
without its gems). agents/scratch/run.sh and walk.sh (git-ignored) wrap the
headless runs.

A test that prints the usage text has not run. Screenshots go to
`agents/scratch/` (git-ignored).

## Ghidra (headless; the project path must be absolute)

```sh
export JAVA_HOME=~/.local/opt/jdk-21.0.12.1+1
G=~/.local/opt/ghidra_12.1.4_PUBLIC/support/analyzeHeadless
P=$PWD/gd-game-10/ghidra
$G $P ghm -process default.xex -readOnly -noanalysis -scriptPath $PWD/gd-game-10/tools/ghidra_scripts \
   -postScript DumpAsm.java fn:0x82300000
```

## The executable

- `default.xex`: XEX2, title ID **41560830**, base 0x82000000, entry
  0x821B8BF8 [C: XEX headers, Ghidra import log].
